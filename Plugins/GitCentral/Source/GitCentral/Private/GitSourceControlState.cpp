// Copyright (c) 2017-2020 Samuel Kahn (samuel@kahncode.com). All Rights Reserved.

#include "GitSourceControlState.h"

#define LOCTEXT_NAMESPACE "GitSourceControl.State"

namespace EWorkingCopyState
{
	TCHAR ToChar(Type State)
	{
		switch(State)
		{
		case Added:
			return 'A';
		case Deleted:
			return 'D';
		case Modified:
			return 'M';
		case CheckedOut:
		case ForcedWriteable:
			return 'L'; //Lock
		case Conflicted:
			return 'U';
		case NotControlled:
			return '?';
		case Ignored:
			return '!';
		case Outdated:
		case Missing:
		case Unknown:
		case Unchanged:
		default:
			//Not returning empty space as empty space will not work well with config file
			return '0'; 
		}
	}

	EWorkingCopyState::Type FromChar(TCHAR State)
	{
		switch(State)
		{
		case 'A':
			return Added;
		case 'D':
			return Deleted;
		case 'M':
			return Modified;
		case 'L': //Lock
			return CheckedOut;
		case 'U':
			return Conflicted;
		case '?':
			return NotControlled;
		case '!':
			return Ignored;
		case ' ':
		case '0':
		default:
			return Unknown;
		}
	}

};

void FGitSourceControlState::CombineWithLocalState(const FGitSourceControlState& Other)
{
	check(Other.GetFilename() == GetFilename());

	//Take most recent timestamp
	if(Other.TimeStamp > TimeStamp)
		TimeStamp = Other.TimeStamp;

	EWorkingCopyState::Type OtherWorkingCopyState = Other.WorkingCopyState;

	switch(OtherWorkingCopyState)
	{
	case EWorkingCopyState::Added:
	{
		switch(WorkingCopyState)
		{
		case EWorkingCopyState::Deleted:
			WorkingCopyState = EWorkingCopyState::Modified;
			break;
		default:
			GITCENTRAL_ERROR(TEXT("Unhandled state combine case %s -> %s"), *GetDisplayName().ToString(), *Other.GetDisplayName().ToString());
			break;
		}
		break;
	}
	case EWorkingCopyState::Deleted:
		WorkingCopyState = EWorkingCopyState::Deleted;
		break;
	case EWorkingCopyState::Modified:
	{
		switch(WorkingCopyState)
		{
		case EWorkingCopyState::Added:
			WorkingCopyState = EWorkingCopyState::Added;
			break;
		case EWorkingCopyState::Modified:
			WorkingCopyState = EWorkingCopyState::Modified;
			break;
		default:
			GITCENTRAL_ERROR(TEXT("Unhandled state combine case %s -> %s"), *GetDisplayName().ToString(), *Other.GetDisplayName().ToString());//TODO : here we hit something
			break;
		}
		break;
	}
	case EWorkingCopyState::Conflicted:
		// Conflicted status always takes precedence
		WorkingCopyState = EWorkingCopyState::Conflicted;
		break;

	//Server states only, should not happen
	case EWorkingCopyState::Outdated:
	case EWorkingCopyState::Missing:
	{
		GITCENTRAL_ERROR(TEXT("Unhandled state combine case %s -> %s"), *GetDisplayName().ToString(), *Other.GetDisplayName().ToString());
	}
	case EWorkingCopyState::NotControlled:
	case EWorkingCopyState::Ignored:
	case EWorkingCopyState::Unknown:
	case EWorkingCopyState::Unchanged:
	default:
		// Return current state unless unknown
		if(WorkingCopyState == EWorkingCopyState::Unknown)
			WorkingCopyState = OtherWorkingCopyState;
	}
}

void FGitSourceControlState::CombineWithSavedState(const SavedState& SavedState, const FString& LocalBranchSha)
{
	CheckedOutRevision = SavedState.CheckedOutRevision;

	switch(SavedState.State)
	{
	case EWorkingCopyState::Modified: //Modified is never set in the saved file at the moment
	case EWorkingCopyState::CheckedOut:
	{
		switch(WorkingCopyState)
		{
		case EWorkingCopyState::Unknown:
			//Here it could be deleted however we will check that on disk
		case EWorkingCopyState::Unchanged:
			WorkingCopyState = EWorkingCopyState::CheckedOut;
			break;
		case EWorkingCopyState::NotControlled:
			if(CheckedOutRevision != "0") //File is new locally but a remote revision exists
			{
				//Technically could also be unchanged but this would need an actual diff
				WorkingCopyState = EWorkingCopyState::Modified;
				break;
			}
		default:
			break; //Keep state
		}
		break;
	}
	case EWorkingCopyState::Conflicted:
		// Conflicted status always takes precedence
		WorkingCopyState = EWorkingCopyState::Conflicted;
		break;
	case EWorkingCopyState::Unknown:
	case EWorkingCopyState::Unchanged:
		//If we synced a file at a more recent revision then the state is "unchanged" from the reference revision
		//If the file had been properly changed, we should not land in this case
		//(note that normally older revisions saved in the file are cleaned up when loading, pushing or pulling)
		if (IsModified() && CheckedOutRevision != "0" && CheckedOutRevision != LocalBranchSha)
		{
			WorkingCopyState = EWorkingCopyState::Unchanged;
		}
		break;
	case EWorkingCopyState::Deleted:
		//Do not apply deleted state because the file may exist
		break;
	case EWorkingCopyState::Outdated:
	case EWorkingCopyState::Missing:
	case EWorkingCopyState::Added:
	case EWorkingCopyState::NotControlled:
	case EWorkingCopyState::Ignored:
	default:
		// Return current state unless unknown
		if(WorkingCopyState == EWorkingCopyState::Unknown)
			WorkingCopyState = SavedState.State;
		break;
	}

}

void FGitSourceControlState::CombineWithRemoteState(const FGitSourceControlState& Other)
{
	check(Other.GetFilename() == GetFilename());

	//Take most recent timestamp
	if(Other.TimeStamp > TimeStamp)
		TimeStamp = Other.TimeStamp;

	RemoteState = Other.WorkingCopyState;

	switch(RemoteState)
	{
	case EWorkingCopyState::Added:
		switch(WorkingCopyState)
		{
		//This does not create a conflict as it can not be resolved, only getting latest can update this
		case EWorkingCopyState::Deleted:
			WorkingCopyState = EWorkingCopyState::Deleted;
			bOutdated = false;
			break;
		case EWorkingCopyState::Unknown:
			WorkingCopyState = EWorkingCopyState::Missing;
			bOutdated = true;
			break;
		case EWorkingCopyState::CheckedOut: //This is not modified so it should maybe only be outdated as a result?
		case EWorkingCopyState::Modified:
		case EWorkingCopyState::Added:
		case EWorkingCopyState::NotControlled:
			WorkingCopyState = EWorkingCopyState::Conflicted;
			bOutdated = true;
			break;
		default:
			GITCENTRAL_ERROR(TEXT("Unhandled state combine case %s -> %s"), *GetDisplayName().ToString(), *Other.GetDisplayName().ToString());
			break;
		}
		break;
	case EWorkingCopyState::Deleted:
		if(WorkingCopyState == EWorkingCopyState::Deleted)
		{
			//Unknown and not unchanged as the file probably doesn't exist on disk
			WorkingCopyState = EWorkingCopyState::Unknown;
			break;
		}
		WorkingCopyState = IsModified() || IsCheckedOut() ? EWorkingCopyState::Conflicted : EWorkingCopyState::Outdated;
		bOutdated = true;
		break;
	case EWorkingCopyState::Modified:
		//Deleted or renamed cannot be resolved so let's no create a conflict for it
		if(WorkingCopyState == EWorkingCopyState::Deleted)
			break;
		WorkingCopyState = IsModified() || IsCheckedOut() ? EWorkingCopyState::Conflicted : EWorkingCopyState::Outdated;
		bOutdated = true;
		break;
	case EWorkingCopyState::Outdated:
	case EWorkingCopyState::Missing:
	case EWorkingCopyState::Conflicted:
	case EWorkingCopyState::NotControlled:
	case EWorkingCopyState::CheckedOut:
	{
		GITCENTRAL_ERROR(TEXT("Unhandled state combine case %s -> %s"), *GetDisplayName().ToString(), *Other.GetDisplayName().ToString());
	}
	case EWorkingCopyState::Ignored:
	case EWorkingCopyState::Unknown:
	case EWorkingCopyState::Unchanged:
	default:
		break;
	}
}

void FGitSourceControlState::CombineWithLockedState(const FGitSourceControlState& Other)
{
	check(Other.GetFilename() == GetFilename());

	UserLocked = Other.UserLocked;
	bLockedByOther = Other.bLockedByOther;
	LockId = Other.LockId;

	if (IsLockedByMe())
	{
		switch (WorkingCopyState)
		{
			//All modified states as well as outdated are more interesting information than the locked state 
			//and virtually behave the same whether or not you have the file locked
		case EWorkingCopyState::Added:
		case EWorkingCopyState::Deleted:
		case EWorkingCopyState::Modified:
		case EWorkingCopyState::Outdated:
		case EWorkingCopyState::NotControlled:
		case EWorkingCopyState::Conflicted:
		case EWorkingCopyState::CheckedOut:
			break;
		case EWorkingCopyState::Ignored:
		case EWorkingCopyState::Unknown:
		case EWorkingCopyState::Unchanged:
			WorkingCopyState = EWorkingCopyState::CheckedOut; //Locked implies CheckedOut
			break;
		case EWorkingCopyState::Missing:
		default:
		{
			GITCENTRAL_ERROR(TEXT("Unhandled state combine case %s -> %s"), *GetDisplayName().ToString(), *Other.GetDisplayName().ToString());
		}
		break;
		}
	}
	else if(IsModified() || IsCheckedOut()) //locked by other
	{
		WorkingCopyState = EWorkingCopyState::ForcedWriteable;
	}
}

void FGitSourceControlState::ResolveConflict(EWorkingCopyState::Type OldState)
{
	WorkingCopyState = OldState;
	bOutdated = false;
	if (RemoteState == EWorkingCopyState::Deleted && FPaths::FileExists(GetFilename()))
	{
		WorkingCopyState = EWorkingCopyState::Added;
	}
}

bool FGitSourceControlState::operator==(const FGitSourceControlState& Other) const
{
	//Note: Currently does not take into account history data here.
	return WorkingCopyState == Other.WorkingCopyState 
		&& RemoteState == Other.RemoteState
		&& bOutdated == Other.bOutdated
		&& bStaged == Other.bStaged
		&& CheckedOutRevision == Other.CheckedOutRevision
		&& AbsoluteFilename == Other.AbsoluteFilename
		&& UserLocked == Other.UserLocked
		&& bLockedByOther == Other.bLockedByOther;
}

bool FGitSourceControlState::operator!=(const FGitSourceControlState& Other) const
{
	return !((*this) == Other);
}

int32 FGitSourceControlState::GetHistorySize() const
{
	return History.Num();
}

TSharedPtr<class ISourceControlRevision, ESPMode::ThreadSafe> FGitSourceControlState::GetHistoryItem( int32 HistoryIndex ) const
{
	check(History.IsValidIndex(HistoryIndex));
	return History[HistoryIndex];
}

TSharedPtr<class ISourceControlRevision, ESPMode::ThreadSafe> FGitSourceControlState::FindHistoryRevision( int32 RevisionNumber ) const
{
	for(const auto& Revision : History)
	{
		if(Revision->GetRevisionNumber() == RevisionNumber)
		{
			return Revision;
		}
	}

	return nullptr;
}

TSharedPtr<class ISourceControlRevision, ESPMode::ThreadSafe> FGitSourceControlState::FindHistoryRevision(const FString& InRevision) const
{
	for(const auto& Revision : History)
	{
		if(Revision->GetRevision() == InRevision)
		{
			return Revision;
		}
	}

	return nullptr;
}

TSharedPtr<class ISourceControlRevision, ESPMode::ThreadSafe> FGitSourceControlState::GetBaseRevForMerge() const
{
	//Always merging against the latest remote version
	//TODO : what will happen in case the file was deleted on remote?
	check(History.Num() > 0);
	return History[0];
}

FName FGitSourceControlState::GetIconName() const
{
	//TODO : make a new icon for delete on remote as this is a bit of a surprise if you sync only to lose the file
	switch(WorkingCopyState)
	{
	case EWorkingCopyState::CheckedOut:
	case EWorkingCopyState::Modified:
		return FName("Subversion.CheckedOut");
	case EWorkingCopyState::ForcedWriteable:
		return FName("Perforce.CheckedOutByOtherUserOtherBranch");
	case EWorkingCopyState::Added:
	case EWorkingCopyState::NotControlled:
		return FName("Subversion.OpenForAdd");
	case EWorkingCopyState::Deleted:
		return FName("Subversion.MarkedForDelete");
	case EWorkingCopyState::Conflicted:
		//TODO: Using "?" for conflicts for now, make a better icon
		return FName("Subversion.NotInDepot");
	case EWorkingCopyState::Outdated:
	case EWorkingCopyState::Missing:
		return FName("Subversion.NotAtHeadRevision");
	default:
		if (IsCheckedOutOther())
		{
			return FName("Perforce.CheckedOutByOtherUser");
		}
		return NAME_None;
	}
}

FName FGitSourceControlState::GetSmallIconName() const
{
	switch(WorkingCopyState)
	{
	case EWorkingCopyState::CheckedOut:
	case EWorkingCopyState::Modified:
		return FName("Subversion.CheckedOut_Small");
	case EWorkingCopyState::ForcedWriteable:
		return FName("Perforce.CheckedOutByOtherUserOtherBranch_Small");
	case EWorkingCopyState::Added:
	case EWorkingCopyState::NotControlled:
		return FName("Subversion.OpenForAdd_Small");
	case EWorkingCopyState::Deleted:
		return FName("Subversion.MarkedForDelete_Small");
	case EWorkingCopyState::Conflicted:
		//TODO: Using "?" for conflicts for now, make a better icon
		return FName("Subversion.NotInDepot_Small");
	case EWorkingCopyState::Outdated:
	case EWorkingCopyState::Missing:
		return FName("Subversion.NotAtHeadRevision_Small");
	default:
		if (IsCheckedOutOther())
		{
			return FName("Perforce.CheckedOutByOtherUser");
		}
		return NAME_None;
	}
}

FText FGitSourceControlState::GetDisplayName() const
{
	switch(WorkingCopyState)
	{
	case EWorkingCopyState::Unknown:
		return LOCTEXT("Unknown", "Unknown");
	case EWorkingCopyState::Unchanged:
		return LOCTEXT("Unchanged", "Unchanged");
	case EWorkingCopyState::Added:
		return LOCTEXT("Added", "Added");
	case EWorkingCopyState::Deleted:
		return LOCTEXT("Deleted", "Deleted");
	case EWorkingCopyState::Modified:
		return LOCTEXT("Modified", "Modified");
	case EWorkingCopyState::CheckedOut:
		return LOCTEXT("CheckedOut", "Checked Out");
	case EWorkingCopyState::ForcedWriteable:
		return FText::Format(LOCTEXT("ForcedWriteable", "Modified locally but locked by: {0}"), FText::FromString(UserLocked));
	case EWorkingCopyState::Conflicted:
		return LOCTEXT("ContentsConflict", "Contents Conflict");
	case EWorkingCopyState::Ignored:
		return LOCTEXT("Ignored", "Ignored");
	case EWorkingCopyState::NotControlled:
		return LOCTEXT("NotControlled", "Not Under Source Control");
	case EWorkingCopyState::Missing:
		return LOCTEXT("Missing", "Missing");
	case EWorkingCopyState::Outdated:
		return LOCTEXT("Outdated", "Outdated");
	}

	if (IsCheckedOutOther())
	{
		return FText::Format(LOCTEXT("LockedOther", "Locked by: {0}"), FText::FromString(UserLocked));
	}

	return FText();
}

FText FGitSourceControlState::GetDisplayTooltip() const
{
	FText tooltip;

	switch(WorkingCopyState)
	{
	case EWorkingCopyState::Unknown:
		tooltip = LOCTEXT("Unknown_Tooltip", "Unknown source control state");
		break;
	case EWorkingCopyState::Unchanged:
		//Not necessary anymore, better to display nothing.
		//tooltip = LOCTEXT("Pristine_Tooltip", "There are no modifications");
		break;
	case EWorkingCopyState::Added:
		tooltip = LOCTEXT("Added_Tooltip", "Item is scheduled for addition");
		break;
	case EWorkingCopyState::Deleted:
		tooltip = LOCTEXT("Deleted_Tooltip", "Item is scheduled for deletion");
		break;
	case EWorkingCopyState::Modified:
		tooltip = LOCTEXT("Modified_Tooltip", "Item has been modified");
		break;
	case EWorkingCopyState::ForcedWriteable:
		tooltip = LOCTEXT("ForcedWriteable_Tooltip", "Item has been modified locally but is"); //Only happens when CheckedOutOther, see below
		break;
	case EWorkingCopyState::CheckedOut:
		tooltip = LOCTEXT("CheckedOut_Tooltip", "Item is checked out by you but not modified");
		break;
	case EWorkingCopyState::Conflicted:
		tooltip = LOCTEXT("ContentsConflict_Tooltip", "The file has been modified locally and remotely, a resolve is needed.");
		break;
	case EWorkingCopyState::Ignored:
		tooltip = LOCTEXT("Ignored_Tooltip", "Item is being ignored.");
		break;
	case EWorkingCopyState::NotControlled:
		tooltip = LOCTEXT("NotControlled_Tooltip", "Item is not under version control.");
		break;
	case EWorkingCopyState::Missing:
		tooltip = LOCTEXT("Missing_Tooltip", "Item is missing, it has been added in a newer version.");
		break;
	case EWorkingCopyState::Outdated:
		tooltip = LOCTEXT("Missing_Tooltip", "Item is outdated, a new version is available from the server.");
		break;
	}

	if (IsCheckedOutOther())
	{
		tooltip = FText::Format(LOCTEXT("LockedOther_Tooltip", " {0} Locked by: {1}"), tooltip, FText::FromString(UserLocked));
	}

	return tooltip;
}

const FString& FGitSourceControlState::GetFilename() const
{
	return AbsoluteFilename;
}

const FDateTime& FGitSourceControlState::GetTimeStamp() const
{
	return TimeStamp;
}

bool FGitSourceControlState::CanCheckIn() const
{
	//Here normally IsCheckedOut should not be submittable if not modified, but testing that workflow leads to errors
	//TODO: IsCheckedOut or IsLocked only should not be submittable! We need to figure out which exact errors are caused
	return (IsModified() || IsAdded() || IsCheckedOut()) && IsCurrent() && !IsConflicted() && !IsCheckedOutOther();
}

bool FGitSourceControlState::CanRevert() const
{
	//CanRevert:
	//Original implementation uses simply CanCheckIn(), however conflicted files may also be reverted (which makes them outdated), 
	//and Added files should not be revert-able as it results in delete and user may not have expected it
	return IsCheckedOut() || IsConflicted();
}

bool FGitSourceControlState::CanCheckout() const
{
	return IsSourceControlled() && !IsModified() && !IsCheckedOut() && !IsCheckedOutOther() && IsCurrent();
}

bool FGitSourceControlState::IsModified() const
{
	//Note: CheckedOut or Locked state is not modified locally, Modified will take precedence over CheckedOut when computing status
	switch (WorkingCopyState)
	{
	case EWorkingCopyState::Added:
	case EWorkingCopyState::Deleted:
	case EWorkingCopyState::Modified:
	case EWorkingCopyState::Conflicted: //Conflicted could also not be modified if it is a simple check-out
	case EWorkingCopyState::NotControlled:
	case EWorkingCopyState::ForcedWriteable: //Forced Writeable could also not be modified if it is a simple check-out
		return true;
	default:
		return false;
	}
}

bool FGitSourceControlState::IsCheckedOut() const
{
	//Returns true for files that are marked for check-out or modified
	//Locally changed files are added and not checked out
	//Unchanged files are not considered "checked out"
	switch (WorkingCopyState)
	{
	case EWorkingCopyState::Added: //TODO: test this case
	case EWorkingCopyState::Deleted:
	case EWorkingCopyState::Modified:
	case EWorkingCopyState::Conflicted: 
	case EWorkingCopyState::CheckedOut:
	case EWorkingCopyState::ForcedWriteable:
		return true;
	case EWorkingCopyState::NotControlled: //Not controlled does not count as checked out state
	default:
		return IsLockedByMe();
	}
}

bool FGitSourceControlState::IsCheckedOutOther(FString* Who) const
{
	if (!bLockedByOther)//Remote state is used to denote remote lock
		return false;

	if (Who != nullptr)
	{
		*Who = UserLocked;
	}
	return !UserLocked.IsEmpty();
}

bool FGitSourceControlState::IsCurrent() const
{
	return !bOutdated;
}

bool FGitSourceControlState::IsSourceControlled() const
{
	return WorkingCopyState != EWorkingCopyState::Ignored && WorkingCopyState != EWorkingCopyState::Unknown;
}

bool FGitSourceControlState::IsAdded() const
{
	return WorkingCopyState == EWorkingCopyState::Added || WorkingCopyState == EWorkingCopyState::NotControlled;
}

bool FGitSourceControlState::IsDeleted() const
{
	return WorkingCopyState == EWorkingCopyState::Deleted;
}

bool FGitSourceControlState::IsIgnored() const
{
	return WorkingCopyState == EWorkingCopyState::Ignored;
}

bool FGitSourceControlState::CanEdit() const
{
	// With Git all files in the working copy are always editable (as opposed to Perforce)
	// Git lfs locks has an option to make locked files by others readonly
	// the setting is lfs.setlockablereadonly, also more settings here https://www.mankier.com/5/git-lfs-config
	// we do however allow to mark files writable and most operations will remove readonly flags for safety
	return !IsCheckedOutOther() && !IsModified(); 
}

bool FGitSourceControlState::CanDelete() const
{
   return IsSourceControlled() && IsCurrent() && CanEdit();
}

bool FGitSourceControlState::IsUnknown() const
{
	return WorkingCopyState == EWorkingCopyState::Unknown;
}

bool FGitSourceControlState::CanAdd() const
{
	//No need to mark for add in our workflow
	//We also do NOT lock when adding files
	return false;
}

bool FGitSourceControlState::IsConflicted() const
{
	return WorkingCopyState == EWorkingCopyState::Conflicted;
}

bool FGitSourceControlState::IsLockedByMe() const
{
	return !bLockedByOther && !UserLocked.IsEmpty();
}

bool FGitSourceControlState::CanFixLock() const
{
	return CanLock() && IsModified();
}

bool FGitSourceControlState::CanLock() const
{
	return UserLocked.IsEmpty();
}

bool FGitSourceControlState::CanUnlock() const
{
	return IsLockedByMe();
}

bool FGitSourceControlState::HasValidLockId() const
{
	return LockId != -1;
}

bool FGitSourceControlState::CanForceWriteable() const
{
	//Must match the condition in FGitForceWriteableWorker::Execute
	return IsCheckedOutOther() && !IsModified() && !IsCheckedOut();
}

void FGitSourceControlState::DebugPrint() const
{
	FString debugStr = FString::Printf(TEXT("Status of (%s): state(%s) remoteState(%s) checkedOutRev(%s) outdated(%s)"), *AbsoluteFilename,
		*UENUM_TO_DISPLAYNAME(EWorkingCopyState, WorkingCopyState), *UENUM_TO_DISPLAYNAME(EWorkingCopyState, RemoteState), 
		*CheckedOutRevision, bOutdated ? TEXT("true") : TEXT("false"));

	if (bLockedByOther)
		debugStr += FString::Printf(TEXT(" lockedByOther(%s)"), *UserLocked);

	GITCENTRAL_LOG(TEXT("%s"), *debugStr);
}

#undef LOCTEXT_NAMESPACE


//SavedState serialization logic

bool SavedState::FromJson(const TSharedPtr<FJsonObject>& Json)
{
	if(!Json.IsValid())
		return false;

	FString StateString;
	if(!Json->TryGetStringField("state", StateString) && StateString.Len() == 1)
		return false;

	State = EWorkingCopyState::FromChar(StateString[0]);

	if(!Json->TryGetStringField("revision", CheckedOutRevision))
		return false;

	return true;
}

TSharedPtr<FJsonObject> SavedState::ToJSon() const
{
	TSharedPtr<FJsonObject> JsonObject = MakeShared<FJsonObject>();

	JsonObject->SetStringField("state", *FString::Printf(TEXT("%c"), EWorkingCopyState::ToChar(State)));
	JsonObject->SetStringField("revision", CheckedOutRevision);

	return JsonObject;
}

