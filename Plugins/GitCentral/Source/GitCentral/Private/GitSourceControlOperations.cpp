// Copyright (c) 2017-2020 Samuel Kahn (samuel@kahncode.com). All Rights Reserved.

#include "GitSourceControlOperations.h"
#include "GitSourceControlState.h"
#include "GitSourceControlCommand.h"
#include "GitSourceControlModule.h"
#include "GitSourceControlUtils.h"

#include "SourceControlOperations.h"

#define LOCTEXT_NAMESPACE "GitCentral"

#define GITCENTRAL_HEAD TEXT("GitCentral_Head")
#define GITCENTRAL_STASH TEXT("GitCentral_Stash")

FName FGitConnectWorker::GetName() const
{
	return "Connect";
}

bool FGitConnectWorker::Execute(FGitSourceControlCommand& InCommand)
{
	check(InCommand.Operation->GetName() == GetName());

	bConnected = false;

	//Check index validity first
	GitIndexState IndexValid = GitSourceControlUtils::RunCheckIndexValid(InCommand);
	if(IndexValid != GitIndexState::Valid)
	{
		//Try to restore index validity 
		TArray<FString> StdOut;
		InCommand.bCommandSuccessful = GitSourceControlUtils::RunCommand(TEXT("reset"), InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot, TArray<FString>(), TArray<FString>(), StdOut, InCommand.ErrorMessages);

		IndexValid = GitSourceControlUtils::RunCheckIndexValid(InCommand);
		if(IndexValid != GitIndexState::Valid)
		{
			InCommand.ErrorMessages.Add(TEXT("The index must be empty for GitCentral to function correctly. You must resolve these inconsistencies manually."));
			InCommand.bCommandSuccessful = false;
			return false;
		}
	}

	//List all remote branches
	//Could also skip this step and run fetch directly
	TArray<FString> Parameters;
	Parameters.Add(InCommand.Remote);
	TArray<FString> StdOut;
	InCommand.bCommandSuccessful = GitSourceControlUtils::RunCommand(TEXT("ls-remote -h --quiet"), InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot, Parameters, TArray<FString>(), StdOut, InCommand.ErrorMessages);
	if(InCommand.bCommandSuccessful && InCommand.ErrorMessages.Num() == 0)
	{
		bool bRemoteTracksBranch = false;
		for(auto& Message : StdOut)
		{
			if(Message.EndsWith(InCommand.Branch))
			{
				bRemoteTracksBranch = true;
				break;
			}
		}

		if(bRemoteTracksBranch)
		{
			InCommand.InfoMessages.Add(*FString::Printf(TEXT("Remote %s is tracking branch %s"), *InCommand.Remote, *InCommand.Branch));

			//fetch on connect
			InCommand.bCommandSuccessful = GitSourceControlUtils::RunFetch(InCommand);
			bConnected = InCommand.bCommandSuccessful;

			GitSourceControlUtils::CleanupStatusFile(InCommand);
		}
		else
		{
			InCommand.ErrorMessages.Add(*FString::Printf(TEXT("Remote %s is not tracking branch %s"), *InCommand.Remote, *InCommand.Branch));
		}

	}

	return InCommand.bCommandSuccessful;
}

bool FGitConnectWorker::UpdateStates() const
{
	//Clear cache and load state file
	FGitSourceControlModule& GitSourceControl = FGitSourceControlModule::GetInstance();
	FGitSourceControlProvider& Provider = GitSourceControl.GetProvider();
	Provider.ClearCache();

	return GitSourceControl.GetStatusFile().Load(Provider.GetPathToRepositoryRoot());//what if the file didn't exit ? need error handling there
}

bool FGitConnectWorker::IsConnected() const
{
	return bConnected;
}

//////////////////////////////////////////////////////////////////////////

static FText ParseCommitResults(const TArray<FString>& InResults, const FString& InBranch)
{
	FString Match("[");
	Match += InBranch;

	for (auto& Message : InResults)
	{
		if(Message.StartsWith(Match))
			return FText::Format(LOCTEXT("CommitMessage", "Commited {0}."), FText::FromString(Message));
	}
	return LOCTEXT("CommitMessageUnknown", "Commit successful");
}

FName FGitCheckOutWorker::GetName() const
{
	return "CheckOut";
}

bool FGitCheckOutWorker::Execute(class FGitSourceControlCommand& InCommand)
{
	//Note: This does ever get latest. Checking out an outdated file will result in a conflict
	//We cannot implement get latest here as the file is kept locked by the editor
	check(InCommand.Operation->GetName() == GetName());

	//Assumes status is up to date

	//Get all local states
	FGitSourceControlModule& GitSourceControl = FGitSourceControlModule::GetInstance();
	FGitSourceControlProvider& Provider = GitSourceControl.GetProvider();
	FGitSourceControlStatusFile& StatusFile = GitSourceControl.GetStatusFile();

	TArray<TSharedRef<ISourceControlState, ESPMode::ThreadSafe>> LocalStates;
	Provider.GetState(InCommand.Files, LocalStates, EStateCacheUsage::Use);

	if (InCommand.bUseLocking)
	{
		TArray<FString> FilesToLock;
		FilesToLock.Reserve(InCommand.Files.Num());

		for (const auto& State : LocalStates)
		{
			//only lock files you can
			FGitSourceControlState* GitState = (FGitSourceControlState*)(&State.Get());
			if (GitState->CanLock())
			{
				FilesToLock.Add(State->GetFilename());
			}
		}
			
		InCommand.bCommandSuccessful = GitSourceControlUtils::RunLockFiles(InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot, InCommand.Remote, FilesToLock, InCommand.ErrorMessages);
		if (!InCommand.bCommandSuccessful)
			return false;
	}

	//Security, make all files writeable
	GitSourceControlUtils::MakeWriteable(InCommand.Files);
	
	for(const auto& State : LocalStates)
	{
		if(State->IsCheckedOut() || State->IsConflicted() || State->IsCheckedOutOther())//Must resolve conflicted state
			continue;

		const FString& File = State->GetFilename();
		const auto& FileState = StatusFile.GetState(File);
		if(FileState.State == EWorkingCopyState::Unknown || FileState.State == EWorkingCopyState::Unchanged)
		{
			auto NewFileState = FileState;
			NewFileState.State = EWorkingCopyState::CheckedOut;
			StatusFile.SetState(File, NewFileState, InCommand.PathToRepositoryRoot, false);
		}
	}

	if(!StatusFile.Save(InCommand.PathToRepositoryRoot))
	{
		InCommand.bCommandSuccessful = false;
		return false; //Error
	}

	// now update the status of our files
	InCommand.bCommandSuccessful = GitSourceControlUtils::RunUpdateStatus(InCommand, InCommand.Files, InCommand.ErrorMessages, States);
	
	return true;
}

bool FGitCheckOutWorker::UpdateStates() const
{
	return GitSourceControlUtils::UpdateCachedStates(States);
}

//////////////////////////////////////////////////////////////////////////

FName FGitCheckInWorker::GetName() const
{
	return "CheckIn";
}

bool FGitCheckInWorker::Execute(FGitSourceControlCommand& InCommand)
{
	check(InCommand.Operation->GetName() == GetName());

	//Assumes status is up to date

	//Note: even when submitting directories, the parameters will be individual files selected in the dialog, no need to handle directories here.

	FGitSourceControlModule& GitSourceControl = FGitSourceControlModule::GetInstance();
	FGitSourceControlProvider& Provider = GitSourceControl.GetProvider();

	TArray<TSharedRef<ISourceControlState, ESPMode::ThreadSafe>> LocalStates;
	Provider.GetState(InCommand.Files, LocalStates, EStateCacheUsage::Use);
	for(int i = LocalStates.Num() - 1; i >= 0; i--)
	{
		auto& State = LocalStates[i];
		if(!State->CanCheckIn())
		{
			InCommand.Files.RemoveSingleSwap(State->GetFilename());
			LocalStates.RemoveAtSwap(i);
		}
	}

	//Fail command if no files could really be checked in
	if(InCommand.Files.Num() == 0)
	{
		InCommand.bCommandSuccessful = false;
		return false;
	}

	//Check index validity first
	GitIndexState IndexValid = GitSourceControlUtils::RunCheckIndexValid(InCommand);
	if(IndexValid != GitIndexState::Valid)
	{
		InCommand.ErrorMessages.Add(TEXT("The index must be empty for GitCentral to function correctly. You must resolve these inconsistencies manually."));
		InCommand.bCommandSuccessful = false;
		return false;
	}

	const FString RemoteBranch = InCommand.GetRemoteBranch();

	//must fetch before updating status
	InCommand.bCommandSuccessful = GitSourceControlUtils::RunFetch(InCommand);
	if(!InCommand.bCommandSuccessful)
		return false;

	TArray<FString> StdOut;

	//Tag the current HEAD
	{
		TArray<FString> Parameters;
		Parameters.Add(GITCENTRAL_HEAD);
		InCommand.bCommandSuccessful &= GitSourceControlUtils::RunCommand(TEXT("tag -f"), InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot, Parameters, TArray<FString>(), StdOut, InCommand.ErrorMessages);
	}

	//Test if we are currently at or ahead the latest remote commit
	const FString MergeBase = GitSourceControlUtils::GetMergeBase(InCommand.Branch, RemoteBranch, InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot);
	const FString RemoteBranchSha = GitSourceControlUtils::GetCommitShaForBranch(RemoteBranch, InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot);
	bool bUpToDate = (MergeBase == RemoteBranchSha);

	if(!bUpToDate) //need to trick git in thinking we are up to date
	{
		//Move the HEAD to the latest commit
		TArray<FString> Parameters;
		Parameters.Add(RemoteBranch);
		InCommand.bCommandSuccessful &= GitSourceControlUtils::RunCommand(TEXT("reset --soft"), InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot, Parameters, TArray<FString>(), StdOut, InCommand.ErrorMessages);

		//Clear the index as it is filled with non-relevant changes, index should now be valid
		InCommand.bCommandSuccessful &= GitSourceControlUtils::RunCommand(TEXT("reset"), InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot, TArray<FString>(), TArray<FString>(), StdOut, InCommand.ErrorMessages);

		//Check index validity again
		IndexValid = GitSourceControlUtils::RunCheckIndexValid(InCommand);
		if(IndexValid != GitIndexState::Valid)
		{
			InCommand.ErrorMessages.Add(TEXT("Files are still staged after performing Check-In, please unstage them manually"));
			InCommand.bCommandSuccessful = false;
		}

		if(!InCommand.bCommandSuccessful)
		{
			Cleanup(InCommand, true);
			return false;
		}
	}

	TSharedRef<FCheckIn, ESPMode::ThreadSafe> Operation = StaticCastSharedRef<FCheckIn>(InCommand.Operation);

	FString CommitMsg("\"");
	if(!Operation->GetDescription().IsEmpty())
		CommitMsg += Operation->GetDescription().ToString();
	else
		CommitMsg += "Git Central: Commited assets.";
	CommitMsg += "\"";

	//Only files that have a valid status can be added
	TArray<FString> FilesToAdd;
	FilesToAdd.Reserve(InCommand.Files.Num());

	{
		TArray<FString> StatusResults;
		TMap<FString, FGitSourceControlState> StatusStates;

		//Note: if a file is in a new folder, git status --porcelain without parameters will only return the directory status. We pass the filenames to ensure the returned status maps to the actual files.
		//Another option would be to use -u to show all untracked files in directories but it would not limit files in the command parameters
		InCommand.bCommandSuccessful &= GitSourceControlUtils::RunCommand(TEXT("status --porcelain"), InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot, TArray<FString>(), InCommand.Files, StatusResults, InCommand.ErrorMessages);
		GitSourceControlUtils::ParseStatusResults(InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot, {}, StatusResults, StatusStates);

		for(auto& State : StatusStates)
		{
			FilesToAdd.Add(State.Value.GetFilename());
		}
	}

	//Note that there are not always any files to commit if everything has already been comitted locally
	bool bShouldPerformAdd = FilesToAdd.Num() != 0;

	//add
	if(InCommand.bCommandSuccessful && bShouldPerformAdd)
	{
		InCommand.bCommandSuccessful &= GitSourceControlUtils::RunCommand(TEXT("add"),
			InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot, TArray<FString>(), FilesToAdd, InCommand.InfoMessages, InCommand.ErrorMessages);
	}

	//commit
	if(InCommand.bCommandSuccessful && bShouldPerformAdd)
	{
		TArray<FString> Parameters;
		Parameters.Add(TEXT("-m"));
		Parameters.Add(CommitMsg);

		InCommand.bCommandSuccessful &= GitSourceControlUtils::RunCommand(TEXT("commit"),
			InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot, Parameters, TArray<FString>(), InCommand.InfoMessages, InCommand.ErrorMessages);
	}

	//push
	if(InCommand.bCommandSuccessful)
	{
		TArray<FString> Parameters;
		Parameters.Add(InCommand.Remote);
		Parameters.Add(InCommand.Branch);

		InCommand.bCommandSuccessful &= GitSourceControlUtils::RunCommand(TEXT("push --quiet"),
			InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot, Parameters, TArray<FString>(), InCommand.InfoMessages, InCommand.ErrorMessages);
	}

	if(InCommand.bCommandSuccessful)
	{
		const FString NewCommitSha = GitSourceControlUtils::GetCommitShaForBranch(InCommand.Branch, InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot);

		// Update Saved State and Have Revision of files we just pushed
		FGitSourceControlStatusFile& StatusFile = GitSourceControl.GetStatusFile();

		for(const auto& State : LocalStates)
		{
			if(!State->CanCheckIn())
				continue;

			const FString& File = State->GetFilename();
			const auto& FileState = StatusFile.GetState(File);

			auto NewFileState = FileState;
			NewFileState.CheckedOutRevision = NewCommitSha;
			NewFileState.State = EWorkingCopyState::Unchanged;
			StatusFile.SetState(File, NewFileState, InCommand.PathToRepositoryRoot, false);
		}

		if(!StatusFile.Save(InCommand.PathToRepositoryRoot))
		{
			InCommand.bCommandSuccessful = false;
			return false; //Error
		}
	}

	//unlock files
	if (InCommand.bCommandSuccessful && InCommand.bUseLocking)
	{
		//Note: this is the longest part of the process when submitting many files, push can take a minute and unlock 10s of minutes.
		//Let's optimize for the case where many files are added by only attempting to unlock files that require it.
		TArray<FString> FilesToUnlock;
		FilesToUnlock.Reserve(InCommand.Files.Num());

		for (const auto& State : LocalStates)
		{
			if (!((FGitSourceControlState*)&State.Get())->CanUnlock())
				continue;

			FilesToUnlock.Add(State->GetFilename());
		}

		if(FilesToUnlock.Num() > 0)
			InCommand.bCommandSuccessful = GitSourceControlUtils::RunUnlockFiles(InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot, InCommand.Remote, FilesToUnlock, InCommand.ErrorMessages);
	}

	if (InCommand.bCommandSuccessful)
	{
		// Remove any deleted files from status cache
		for (const auto& State : LocalStates)
		{
			if (State->IsDeleted())
			{
				Provider.RemoveFileFromCache(State->GetFilename());
			}
		}

		Operation->SetSuccessMessage(ParseCommitResults(InCommand.InfoMessages, InCommand.Branch));
	}

	//Cleanup to remove the tag and check everything went fine
	Cleanup(InCommand, !bUpToDate);

	// now update the status of our files
	GitSourceControlUtils::RunUpdateStatus(InCommand, InCommand.Files, InCommand.ErrorMessages, States);

	return InCommand.bCommandSuccessful;
}

bool FGitCheckInWorker::UpdateStates() const
{
	return GitSourceControlUtils::UpdateCachedStates(States);
}

void FGitCheckInWorker::Cleanup(FGitSourceControlCommand& InCommand, bool bShouldReset)
{
	//Something went wrong, let's get back to where we started
	if(bShouldReset || !InCommand.bCommandSuccessful)
	{
		TArray<FString> StdOut;

		//Clear the index first (in case push failed our changes will be there)
		InCommand.bCommandSuccessful &= GitSourceControlUtils::RunCommand(TEXT("reset"), InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot, TArray<FString>(), TArray<FString>(), StdOut, InCommand.ErrorMessages);

		if(bShouldReset)
		{
			//Move the HEAD back to the starting point tag
			TArray<FString> Parameters;
			Parameters.Add(GITCENTRAL_HEAD);
			InCommand.bCommandSuccessful &= GitSourceControlUtils::RunCommand(TEXT("reset --soft"), InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot, Parameters, TArray<FString>(), StdOut, InCommand.ErrorMessages);

			//Clear the index as it is filled with non-relevant changes, index should now be valid
			InCommand.bCommandSuccessful &= GitSourceControlUtils::RunCommand(TEXT("reset"), InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot, TArray<FString>(), TArray<FString>(), StdOut, InCommand.ErrorMessages);
		}

		//Check index validity first
		GitIndexState IndexValid = GitSourceControlUtils::RunCheckIndexValid(InCommand);
		if(IndexValid != GitIndexState::Valid)
		{
			InCommand.ErrorMessages.Add(TEXT("Files are still staged after performing Check-In, please unstage them manually"));
		}
	}

	//Remove the tag if it existsTag the current HEAD just in case
	{
		TArray<FString> StdOut;
		TArray<FString> StdErr;
		TArray<FString> Parameters;
		Parameters.Add(GITCENTRAL_HEAD);

		//We don't care if this succeeds
		GitSourceControlUtils::RunCommand(TEXT("tag -d"), InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot, Parameters, TArray<FString>(), StdOut, StdErr);
	}
}

//////////////////////////////////////////////////////////////////////////

FName FGitMarkForAddWorker::GetName() const
{
	return "MarkForAdd";
}

bool FGitMarkForAddWorker::Execute(FGitSourceControlCommand& InCommand)
{
	check(InCommand.Operation->GetName() == GetName());

	//Files are automatically marked for add when necessary, 
	//Even though this should never be called as State::CanAdd() returns false unconditionally
	//It is called when creating new files
	//Note that we currently do NOT lock files when marked for add, so the conflict may only appear after one file has actually been submitted

	// This update status ensures update status is called after writing the file on disk when creating new files
	GitSourceControlUtils::RunUpdateStatus(InCommand, InCommand.Files, InCommand.ErrorMessages, States);

	return InCommand.bCommandSuccessful;
}

bool FGitMarkForAddWorker::UpdateStates() const
{
	return GitSourceControlUtils::UpdateCachedStates(States);
}

//////////////////////////////////////////////////////////////////////////

FName FGitDeleteWorker::GetName() const
{
	return "Delete";
}

bool FGitDeleteWorker::Execute(FGitSourceControlCommand& InCommand)
{
	//TODO: this could push on delete for assets that are source controlled, 
	//but this would also need a check-in dialog so cannot be done from the regular delete menu action
	//The best way is to add a specific context menu action for delete-push which will call both commands

	check(InCommand.Operation->GetName() == GetName());

	FGitSourceControlModule& GitSourceControl = FGitSourceControlModule::GetInstance();
	FGitSourceControlStatusFile& StatusFile = GitSourceControl.GetStatusFile();
	FGitSourceControlProvider& Provider = GitSourceControl.GetProvider();

	TArray<TSharedRef<ISourceControlState, ESPMode::ThreadSafe>> LocalStates;
	Provider.GetState(InCommand.Files, LocalStates, EStateCacheUsage::Use);

	for (auto& State : LocalStates)
	{
		auto& File = State->GetFilename();

		if (InCommand.bUseLocking && !((FGitSourceControlState*)&State.Get())->IsLockedByMe())
		{
			//We must lock the file to prevent someone else to acquire the lock before we check in the delete
			InCommand.bCommandSuccessful = GitSourceControlUtils::RunLockFiles(InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot, InCommand.Remote, { File }, InCommand.ErrorMessages);
			if (!InCommand.bCommandSuccessful)
				return false;
		}

		//Security, make file writeable
		GitSourceControlUtils::MakeWriteable(File);

		//Simply delete the files on disk
		//This can fail is the file has opened handles or is marked for read-only
		bool bDeleted = FPlatformFileManager::Get().GetPlatformFile().DeleteFile(*File);
		if (!bDeleted)
		{
			InCommand.ErrorMessages.Add(*FString::Printf(TEXT("Failed to delete file: %s"), *File));
		}
		else
		{
			auto FileState = StatusFile.GetState(File);
			if (FileState.CheckedOutRevision != "0")//Only mark checked-out files to deleted
			{
				FileState.State = EWorkingCopyState::Deleted;
				StatusFile.SetState(File, FileState, InCommand.PathToRepositoryRoot);
			}
		}

		InCommand.bCommandSuccessful &= InCommand.ErrorMessages.Num() == 0;

		if (!InCommand.bCommandSuccessful)
			return false;
	}

	// Delete status must be updated to reflect lock state
	GitSourceControlUtils::RunUpdateStatus(InCommand, InCommand.Files, InCommand.ErrorMessages, States);

	return InCommand.bCommandSuccessful;
}

bool FGitDeleteWorker::UpdateStates() const
{
	return GitSourceControlUtils::UpdateCachedStates(States);
}

//////////////////////////////////////////////////////////////////////////

FName FGitRevertWorker::GetName() const
{
	return "Revert";
}

bool FGitRevertWorker::Execute(FGitSourceControlCommand& InCommand)
{
	check(InCommand.Operation->GetName() == GetName());

	InCommand.bCommandSuccessful = true;

	//Assumes status is up to date

	//Get all local states
	FGitSourceControlModule& GitSourceControl = FGitSourceControlModule::GetInstance();
	FGitSourceControlStatusFile& StatusFile = GitSourceControl.GetStatusFile();

	FGitSourceControlProvider& Provider = GitSourceControl.GetProvider();

	TArray<FString> FilesToUnlock;
	FilesToUnlock.Reserve(InCommand.Files.Num());

	TArray<TSharedRef<ISourceControlState, ESPMode::ThreadSafe>> LocalStates;
	Provider.GetState(InCommand.Files, LocalStates, EStateCacheUsage::Use);
	for(const auto& State : LocalStates)
	{
		if(!State->CanRevert())
			continue;

		const FString& File = State->GetFilename();
		const auto& SavedState = StatusFile.GetState(File);
		
		bool bResult = GitSourceControlUtils::RunSyncFile(InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot, File, SavedState.CheckedOutRevision, InCommand.ErrorMessages, EWorkingCopyState::Unchanged);

		if (bResult && !State->IsCheckedOutOther())
			FilesToUnlock.Add(File);

		InCommand.bCommandSuccessful &= bResult;
	}

	if (InCommand.bUseLocking)
	{
		//Here we could revert a file that is also checked out by another, so we must pick the ones we have locked ourselves
		InCommand.bCommandSuccessful &= GitSourceControlUtils::RunUnlockFiles(InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot, InCommand.Remote, FilesToUnlock, InCommand.ErrorMessages);
	}

	// now update the status of our files 
	GitSourceControlUtils::RunUpdateStatus(InCommand, InCommand.Files, InCommand.ErrorMessages, States);

	return InCommand.bCommandSuccessful;
}

bool FGitRevertWorker::UpdateStates() const
{
	return GitSourceControlUtils::UpdateCachedStates(States);
}

//////////////////////////////////////////////////////////////////////////

FName FGitUpdateStatusWorker::GetName() const
{
	return "UpdateStatus";
}

bool FGitUpdateStatusWorker::Execute(FGitSourceControlCommand& InCommand)
{
	check(InCommand.Operation->GetName() == GetName());

	//must fetch before updating status
	//TODO Optimize for less agressive fetch as this is the slowest part of updating the status
	//Ideas: if fetch did not update the sha just update local status as remote status has not changed
	//Only fetch on timer expired or on meaningful operations (push/pull)
	InCommand.bCommandSuccessful = GitSourceControlUtils::RunFetch(InCommand);
	bConnected = InCommand.bCommandSuccessful;

	TSharedRef<FUpdateStatus, ESPMode::ThreadSafe> Operation = StaticCastSharedRef<FUpdateStatus>(InCommand.Operation);

	if(InCommand.Files.Num() > 0)
	{
		InCommand.bCommandSuccessful = GitSourceControlUtils::RunUpdateStatus(InCommand, InCommand.Files, InCommand.ErrorMessages, States);
		GitSourceControlUtils::RemoveRedundantErrors(InCommand, TEXT("' is outside repository"));
	}
	else if(Operation->ShouldCheckAllFiles() || Operation->ShouldGetOpenedOnly())
	{
		FString RepositoryRootArg = InCommand.PathToRepositoryRoot;
		if(!RepositoryRootArg.EndsWith("/"))
			RepositoryRootArg += '/';

		//Note: this will not update states properly as files that are newly unmodified will not appear and therefore their state will not be refreshed
		//If this becomes a problem, this method must pass all currently cached filenames to the update status routine

		InCommand.bCommandSuccessful = GitSourceControlUtils::RunUpdateStatus(InCommand, { RepositoryRootArg }, InCommand.ErrorMessages, States);
		GitSourceControlUtils::RemoveRedundantErrors(InCommand, TEXT("' is outside repository"));
	}

	if(Operation->ShouldUpdateHistory())
	{
		for(int32 Index = 0; Index < States.Num(); Index++)
		{
			const FString& File = States[Index].GetFilename();
			TGitSourceControlHistory History;

			GitSourceControlUtils::RunGetHistory(InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot, InCommand.GetRemoteBranch(), File, InCommand.ErrorMessages, History);
			
			Histories.Add(*File, History);
		}
	}

	// don't use the ShouldUpdateModifiedState() hint here as it is specific to Perforce: the above normal Git status has already told us this information (like Git and Mercurial)

	return InCommand.bCommandSuccessful;
}

bool FGitUpdateStatusWorker::UpdateStates() const
{
	bool bUpdated = GitSourceControlUtils::UpdateCachedStates(States);

	FGitSourceControlModule& GitSourceControl = FGitSourceControlModule::GetInstance();
	FGitSourceControlProvider& Provider = GitSourceControl.GetProvider();

	// add history, if any
	for(const auto& History : Histories)
	{
		TSharedRef<FGitSourceControlState, ESPMode::ThreadSafe> State = Provider.GetStateInternal(History.Key);
		State->History = History.Value;
		State->TimeStamp = FDateTime::Now();
		bUpdated = true;
	}

	return bUpdated;
}

bool FGitUpdateStatusWorker::IsConnected() const
{
	return bConnected;
}

//////////////////////////////////////////////////////////////////////////

FName FGitCopyWorker::GetName() const
{
	return "Copy";
}

bool FGitCopyWorker::Execute(FGitSourceControlCommand& InCommand)
{
	check(InCommand.Operation->GetName() == GetName());

	// This is called when "Duplicate" is called in the Content Browser
	// TODO : Test this behavior and code path
	// Probably we should enforce in the check-in command to check-in redirectors

	// Copy or Move operation on a single file : Git does not need an explicit copy nor move,
	// but after a Move the Editor create a redirector file with the old asset name that points to the new asset.
	// The redirector needs to be commited with the new asset to perform a real rename.
	// => the following is to "MarkForAdd" the redirector, but it still need to be committed by selecting the whole directory and "check-in"
	//InCommand.bCommandSuccessful = GitSourceControlUtils::RunCommand(TEXT("add"), InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot, TArray<FString>(), InCommand.Files, InCommand.InfoMessages, InCommand.ErrorMessages);

	return InCommand.bCommandSuccessful;
}

bool FGitCopyWorker::UpdateStates() const
{
	return false;
}

//////////////////////////////////////////////////////////////////////////

FName FGitResolveWorker::GetName() const
{
	return "Resolve";
}

bool FGitResolveWorker::Execute( class FGitSourceControlCommand& InCommand )
{
	//Note: this assumes resolve using mine, and keeps the file checked out
	check(InCommand.Operation->GetName() == GetName());

	InCommand.bCommandSuccessful = true;
	const FString RemoteSha = GitSourceControlUtils::GetCommitShaForBranch(InCommand.GetRemoteBranch(), InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot);
	if(RemoteSha.IsEmpty())
	{
		InCommand.bCommandSuccessful = false;
		return false;
	}

	//Security, make all files writeable
	GitSourceControlUtils::MakeWriteable(InCommand.Files);

	//Assumes status is up to date

	//Get all local states
	FGitSourceControlModule& GitSourceControl = FGitSourceControlModule::GetInstance();
	FGitSourceControlProvider& Provider = GitSourceControl.GetProvider();
	FGitSourceControlStatusFile& StatusFile = GitSourceControl.GetStatusFile();

	TArray<TSharedRef<ISourceControlState, ESPMode::ThreadSafe>> LocalStates;
	Provider.GetState(InCommand.Files, LocalStates, EStateCacheUsage::Use);
	for(const auto& State : LocalStates)
	{
		if(!State->IsConflicted())
			continue;

		const FString& File = State->GetFilename();
		const auto& FileState = StatusFile.GetState(File);
	
		auto NewFileState = FileState;
		NewFileState.State = EWorkingCopyState::CheckedOut;
		NewFileState.CheckedOutRevision = RemoteSha;
		StatusFile.SetState(File, NewFileState, InCommand.PathToRepositoryRoot, false);
	}

	if(!StatusFile.Save(InCommand.PathToRepositoryRoot))
	{
		InCommand.bCommandSuccessful = false;
		return false; //Error
	}

	// now update the status of our files
	GitSourceControlUtils::RunUpdateStatus(InCommand, InCommand.Files, InCommand.ErrorMessages, States);

	return InCommand.bCommandSuccessful;
}

bool FGitResolveWorker::UpdateStates() const
{
	return GitSourceControlUtils::UpdateCachedStates(States);
}

//////////////////////////////////////////////////////////////////////////

FName FGitSyncWorker::GetName() const
{
	return "Sync";
}

bool FGitSyncWorker::Execute(class FGitSourceControlCommand& InCommand)
{
	check(InCommand.Operation->GetName() == GetName());

	//Let's start by running fetch here
	InCommand.bCommandSuccessful = GitSourceControlUtils::RunFetch(InCommand);

	if(InCommand.Files.Num() == 1 && InCommand.Files[0] == InCommand.PathToRepositoryRoot)
	{
		//Base directory: get latest for all the files
		return GetLatest(InCommand);
	}
	else //command is called for directories and/or individual files, sync them
	{
		TArray<FString> FilesToSync;
		FilesToSync.Reserve(InCommand.Files.Num());

		for (const auto& File : InCommand.Files)
		{
			//Only include paths that belong to the selected git repository
			if (!File.StartsWith(InCommand.PathToRepositoryRoot))
			{
				continue;
			}

			FFileStatData FileInfo = FPlatformFileManager::Get().GetPlatformFile().GetStatData(*File);
			
			if (FileInfo.bIsDirectory)
			{
				//Add all the files in a directory if selected. 
				//Note: This will not get new files that may have been added on the remote
				//TODO: add support for syncing new files

				TArray<FString> FilesInDirectory;
				FPlatformFileManager::Get().GetPlatformFile().FindFiles(FilesInDirectory, *File, nullptr);
				FilesToSync.Append(FilesInDirectory);
			}
			else if (FileInfo.bIsValid)
			{
				FilesToSync.Add(File);
			}
		}

		//Security, make all files writeable
		GitSourceControlUtils::MakeWriteable(FilesToSync);

		FGitSourceControlModule& GitSourceControl = FGitSourceControlModule::GetInstance();
		FGitSourceControlProvider& Provider = GitSourceControl.GetProvider();

		TArray<TSharedRef<ISourceControlState, ESPMode::ThreadSafe>> LocalStates;
		Provider.GetState(FilesToSync, LocalStates, EStateCacheUsage::Use);
		for(const auto& State : LocalStates)
		{
			if(State->IsCurrent())
			{
				FilesToSync.RemoveSingleSwap(State->GetFilename());
			}
		}

		//Fail command if no files could really be synced
		if(FilesToSync.Num() == 0)
		{
			InCommand.bCommandSuccessful = false;
			return false;
		}

		const FString RemoteSha = GitSourceControlUtils::GetCommitShaForBranch(InCommand.GetRemoteBranch(), InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot);
		if(RemoteSha.IsEmpty())
		{
			InCommand.bCommandSuccessful = false;
			return false;
		}

		for(const auto& File : FilesToSync)
		{
			InCommand.bCommandSuccessful &= GitSourceControlUtils::RunSyncFile(InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot, File, RemoteSha, InCommand.ErrorMessages);
		}

		// now update the status of our files
		GitSourceControlUtils::RunUpdateStatus(InCommand, FilesToSync, InCommand.ErrorMessages, States);
		UpdatedFiles = FilesToSync;
	}

	return InCommand.bCommandSuccessful;
}

void FGitSyncWorker::Cleanup()
{
	FGitSourceControlModule& GitSourceControl = FGitSourceControlModule::GetInstance();
	FGitSourceControlStatusFile& StatusFile = GitSourceControl.GetStatusFile();

	StatusFile.RestoreCachedStates();
}

bool FGitSyncWorker::FoundRebaseConflict(const TArray<FString> Message) const
{
	for(const auto& Str : Message)
	{
		if(Str.Contains("CONFLICT"))
			return true;
	}
	return false;
}

bool FGitSyncWorker::ParsePullResults(class FGitSourceControlCommand& InCommand, const TArray<FString>& Results)
{
	//Note: we aren't using this because in cases of conflict, the output is completely different
	UpdatedFiles.Reset();
	for (const auto& ResultLine : Results)
	{
		int32 PipeIndex = ResultLine.Find("|");
		if (PipeIndex != INDEX_NONE) //File line
		{
			FString RelativeFilename = ResultLine.Left(PipeIndex);
			RelativeFilename.TrimStartAndEndInline();
			FPaths::MakePathRelativeTo(RelativeFilename, *InCommand.PathToRepositoryRoot);
			UpdatedFiles.Emplace(MoveTemp(RelativeFilename));
		}
		else
		{
			int32 FilesChangedIndex = ResultLine.Find("files changed");
			if (FilesChangedIndex != INDEX_NONE) //File line
			{
				int FilesChangedCount = FCString::Atoi(*ResultLine);
				if (FilesChangedCount == UpdatedFiles.Num())
				{
					return true;
				}
				else
				{
					InCommand.ErrorMessages.Add(*FString::Printf(TEXT("Failed to identify all updated files during pull, total count should be %d, found %d"), UpdatedFiles.Num(), FilesChangedCount));
				}
			}
		}
	}

	return false;
}

bool FGitSyncWorker::GetLatest(class FGitSourceControlCommand& InCommand)
{
	//Check index validity first
	GitIndexState IndexValid = GitSourceControlUtils::RunCheckIndexValid(InCommand);
	if(IndexValid != GitIndexState::Valid)
	{
		InCommand.bCommandSuccessful = false;
		return false;
	}
	
	const FString RemoteBranch = InCommand.GetRemoteBranch();
	const FString RemoteSha = GitSourceControlUtils::GetCommitShaForBranch(RemoteBranch, InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot);
	if(RemoteSha.IsEmpty())
	{
		InCommand.bCommandSuccessful = false;
		return false;
	}

	//Find merge base for local branch and remote
	const FString MergeBase = GitSourceControlUtils::GetMergeBase(InCommand.Branch, RemoteBranch, InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot);

	if(MergeBase.IsEmpty())
	{
		InCommand.ErrorMessages.Add(*FString::Printf(TEXT("Could not find merge-base for %s and %s"), *InCommand.Branch, *RemoteBranch));
		return false;
	}
	
	if(MergeBase == RemoteSha) //Nothing to sync, success
		return true;

	FGitSourceControlModule& GitSourceControl = FGitSourceControlModule::GetInstance();
	FGitSourceControlProvider& Provider = GitSourceControl.GetProvider();
	FGitSourceControlStatusFile& StatusFile = GitSourceControl.GetStatusFile();

	//Save states in case something goes wrong
	StatusFile.CacheStates();

	//check if we have any local changes
	bool bStash = false;

	{
		TArray<FString> StdOut;
		InCommand.bCommandSuccessful &= GitSourceControlUtils::RunCommand(TEXT("status --porcelain"), InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot, TArray<FString>(), TArray<FString>(), StdOut, InCommand.ErrorMessages);
		bStash = StdOut.Num() != 0;
	}

	if(InCommand.bCommandSuccessful && bStash)
	{
		TArray<FString> StdOut;
		InCommand.bCommandSuccessful &= GitSourceControlUtils::RunCommand(TEXT("add ."), InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot, TArray<FString>(), TArray<FString>(), StdOut, InCommand.ErrorMessages);

		InCommand.bCommandSuccessful &= GitSourceControlUtils::RunCommand(TEXT("stash save -u"), InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot, { GITCENTRAL_STASH }, TArray<FString>(), StdOut, InCommand.ErrorMessages);//error

		if(!InCommand.bCommandSuccessful)
		{
			GitSourceControlUtils::RunCommand(TEXT("reset"), InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot, TArray<FString>(), TArray<FString>(), StdOut, InCommand.ErrorMessages);
			StatusFile.ClearCache();
			return false;
		}
	}

	// List all the files that will be changed
	{
		TArray<FString> StdOut;
		TArray<FString> StdErr;

		TArray<FString> Parameters;
		Parameters.Add(MergeBase);
		Parameters.Add(RemoteBranch);
		//diff all files from the server but will only keep states that we are interested in
		//TODO: Maybe this would be better with a log and aggregating what happened as we can miss some add+delete cases with git diff
		//git log --name-status --pretty=format:"> %h %s" --reverse

		TMap<FString, FGitSourceControlState> RemoteStates;
		InCommand.bCommandSuccessful &= GitSourceControlUtils::RunCommand(TEXT("diff --name-status"), InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot, Parameters, TArray<FString>(), StdOut, StdErr);
		if (InCommand.bCommandSuccessful)
		{
			GitSourceControlUtils::ParseNameStatusResults(InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot, StdOut, RemoteStates);

			UpdatedFiles.Reserve(RemoteStates.Num());
			for (const auto& RemoteState : RemoteStates)
			{
				UpdatedFiles.Add(RemoteState.Value.GetFilename());
			}
		}
		else
		{
			InCommand.ErrorMessages.Append(StdErr);
			Cleanup();
			return false;
		}
	}

	//Perform pull
	{
		//Emulating the following command, because we need to track conflicted files
		//git pull --rebase --strategy="recursive" --strategy-option="theirs" origin master
		//Investigate --autostash option here, but we probably run into the same issue with silent conflicts
		TArray<FString> StdErr;
		TArray<FString> PullRebaseResult;
		{
			TArray<FString> Parameters;
			Parameters.Add(InCommand.Remote);
			Parameters.Add(InCommand.Branch);
			InCommand.bCommandSuccessful &= GitSourceControlUtils::RunCommand(TEXT("pull --rebase"), InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot, Parameters, TArray<FString>(), PullRebaseResult, StdErr);
		}

		while(!InCommand.bCommandSuccessful && FoundRebaseConflict(PullRebaseResult))
		{
			InCommand.bCommandSuccessful = true;
			PullRebaseResult.Reset();

			TArray<FString> StdOut;
			GitSourceControlUtils::RunCommand(TEXT("status --porcelain"), InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot, TArray<FString>(), TArray<FString>(), StdOut, InCommand.ErrorMessages);

			TMap<FString, FGitSourceControlState> StatusResult;
			GitSourceControlUtils::ParseStatusResults(InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot, TArray<FString>(), StdOut, StatusResult);

			for(auto It : StatusResult)
			{
				if(It.Value.IsConflicted())
				{
					const FString& File = It.Key;

					//Resolve the conflict (theirs here means accepting the version being merged into the remote = local version)
					InCommand.bCommandSuccessful &= GitSourceControlUtils::RunCommand(TEXT("checkout --theirs"), InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot, TArray<FString>(), { File }, StdOut, InCommand.ErrorMessages);

					TArray<TSharedRef<ISourceControlState, ESPMode::ThreadSafe>> LocalStates;
					Provider.GetState({ File }, LocalStates, EStateCacheUsage::Use);
					check(LocalStates.Num() == 1);
					if(LocalStates[0]->IsConflicted()) //Only keep conflict state if we hadn't resolve the conflict already
					{
						auto FileState = StatusFile.GetState(File);
						FileState.State = EWorkingCopyState::Conflicted;
						FileState.CheckedOutRevision = RemoteSha;
						StatusFile.SetState(File, FileState, InCommand.PathToRepositoryRoot, false);
					}

					UpdatedFiles.RemoveSingleSwap(File); //Conflicted files will not be changed
				}
			}

			InCommand.bCommandSuccessful &= GitSourceControlUtils::RunCommand(TEXT("add ."), InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot, TArray<FString>(), TArray<FString>(), StdOut, InCommand.ErrorMessages);

			StdErr.Reset();
			InCommand.bCommandSuccessful &= GitSourceControlUtils::RunCommand(TEXT("rebase --continue"), InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot, TArray<FString>(), TArray<FString>(), PullRebaseResult, StdErr);
		}

		//failure during rebase, abort rebase
		if(!InCommand.bCommandSuccessful)
		{
			InCommand.ErrorMessages.Append(StdErr);
			GitSourceControlUtils::RunCommand(TEXT("rebase --abort"), InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot, TArray<FString>(), TArray<FString>(), InCommand.InfoMessages, InCommand.ErrorMessages);
			if(bStash)
				GitSourceControlUtils::RunCommand(TEXT("stash drop"), InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot, TArray<FString>(), TArray<FString>(), InCommand.InfoMessages, InCommand.ErrorMessages);
			Cleanup();
			return false;
		}

		//Note: git checkout --theirs outputs an error when resolving a conflict: Updated 1 path from the index
		GitSourceControlUtils::RemoveRedundantErrors(InCommand, TEXT("path from the index"));
	}

	if(InCommand.bCommandSuccessful && bStash)
	{
		TArray<FString> StdOut;
		InCommand.bCommandSuccessful &= GitSourceControlUtils::RunCommand(TEXT("stash pop"), InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot, TArray<FString>(), TArray<FString>(), StdOut, InCommand.ErrorMessages);

		//Conflicts can theoretically be created here if the index was modified however we just did a stash so this in practice should not happen.
		//Conflict resolution here cannot be done through "checkout --theirs" but rather by doing "git add . && git reset"
		//For now we are not handling this case

		//git reset all into working directory
		InCommand.bCommandSuccessful &= GitSourceControlUtils::RunCommand(TEXT("reset"), InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot, TArray<FString>(), TArray<FString>(), StdOut, InCommand.ErrorMessages);
	}

	//Update all checked out revisions now that we are up to date
	const auto& SavedStatuses = StatusFile.GetAllStates();
	for(auto It : SavedStatuses)
	{
		auto FileState = It.Value;
		if(FileState.State == EWorkingCopyState::Deleted)
			FileState.State = EWorkingCopyState::Unknown;
		FileState.CheckedOutRevision = "0";
		StatusFile.SetState(It.Key, FileState, InCommand.PathToRepositoryRoot, false);
	}

	//Must mark conflicting files
	if(!StatusFile.Save(InCommand.PathToRepositoryRoot))
	{
		InCommand.bCommandSuccessful = false;
		Cleanup();
		return false;
	}

	// now update the status of our files and in particular the outdated files
	TArray<FSourceControlStateRef> FilesToRefresh = Provider.GetCachedStateByPredicate([](const FSourceControlStateRef& StateRef) {
		return !StateRef->IsCurrent() || StateRef->IsConflicted() || StateRef->IsCheckedOut();
	});

	TArray<FString> FilesParam;

	FString RepositoryRootArg = InCommand.PathToRepositoryRoot;
	if(!RepositoryRootArg.EndsWith("/"))
		RepositoryRootArg += '/';

	FilesParam.Add(RepositoryRootArg);

	for(auto& StateRef : FilesToRefresh)
	{
		FilesParam.Add(StateRef->GetFilename());
	}

	InCommand.bCommandSuccessful = GitSourceControlUtils::RunUpdateStatus(InCommand, FilesParam, InCommand.ErrorMessages, States);

	return true;
}

bool FGitSyncWorker::UpdateStates() const
{
	FGitSourceControlModule& GitSourceControl = FGitSourceControlModule::GetInstance();
	FGitSourceControlProvider& Provider = GitSourceControl.GetProvider();
	Provider.SetLastSyncOperationUpdatedFiles(UpdatedFiles);

	return GitSourceControlUtils::UpdateCachedStates(States);
}

//////////////////////////////////////////////////////////////////////////

FText FForceUnlock::GetInProgressString() const
{
	return LOCTEXT("SourceControl_ForceUnlock", "Force Unlocking files...");
}

FName FGitForceUnlockWorker::GetName() const
{
	return "ForceUnlock";
}

bool FGitForceUnlockWorker::Execute(class FGitSourceControlCommand& InCommand)
{
	check(InCommand.Operation->GetName() == GetName());

	//Assumes status is up to date

	FGitSourceControlModule& GitSourceControl = FGitSourceControlModule::GetInstance();
	FGitSourceControlProvider& Provider = GitSourceControl.GetProvider();

	TArray<TSharedRef<ISourceControlState, ESPMode::ThreadSafe>> LocalStates;
	Provider.GetState(InCommand.Files, LocalStates, EStateCacheUsage::Use);
	for (const auto& State : LocalStates)
	{
		if (!State->IsCheckedOutOther()) //Only unlocks files checked out by another
		{
			InCommand.Files.RemoveSingleSwap(State->GetFilename());
		}
	}

	InCommand.bCommandSuccessful = GitSourceControlUtils::RunUnlockFiles(InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot, InCommand.Remote, InCommand.Files, InCommand.ErrorMessages, true);

	// now update the status of our files
	GitSourceControlUtils::RunUpdateStatus(InCommand, InCommand.Files, InCommand.ErrorMessages, States);

	return InCommand.bCommandSuccessful;
}

bool FGitForceUnlockWorker::UpdateStates() const
{
	return GitSourceControlUtils::UpdateCachedStates(States);
}

//////////////////////////////////////////////////////////////////////////


FText FForceWriteable::GetInProgressString() const
{
	return LOCTEXT("SourceControl_ForceWriteable", "Force making files Writeable...");
}

FName FGitForceWriteableWorker::GetName() const
{
	return "ForceWriteable";
}

bool FGitForceWriteableWorker::Execute(class FGitSourceControlCommand& InCommand)
{
	check(InCommand.Operation->GetName() == GetName());

	//Assumes status is up to date

	//Get all local states
	FGitSourceControlModule& GitSourceControl = FGitSourceControlModule::GetInstance();
	FGitSourceControlProvider& Provider = GitSourceControl.GetProvider();
	FGitSourceControlStatusFile& StatusFile = GitSourceControl.GetStatusFile();

	//Security, make all files writeable
	GitSourceControlUtils::MakeWriteable(InCommand.Files);

	TArray<TSharedRef<ISourceControlState, ESPMode::ThreadSafe>> LocalStates;
	Provider.GetState(InCommand.Files, LocalStates, EStateCacheUsage::Use);
	for (const auto& State : LocalStates)
	{
		if (State->IsCheckedOutOther() && !State->IsCheckedOut() && !State->IsModified())
		{
			const FString& File = State->GetFilename();
			const auto& FileState = StatusFile.GetState(File);
			if (FileState.State == EWorkingCopyState::Unknown || FileState.State == EWorkingCopyState::Unchanged)
			{
				auto NewFileState = FileState;
				NewFileState.State = EWorkingCopyState::CheckedOut;
				StatusFile.SetState(File, NewFileState, InCommand.PathToRepositoryRoot, false);
			}
		}
	}

	if (!StatusFile.Save(InCommand.PathToRepositoryRoot))
	{
		InCommand.bCommandSuccessful = false;
		return false; //Error
	}

	// now update the status of our files
	InCommand.bCommandSuccessful = GitSourceControlUtils::RunUpdateStatus(InCommand, InCommand.Files, InCommand.ErrorMessages, States);

	return true;
}

bool FGitForceWriteableWorker::UpdateStates() const
{
	return GitSourceControlUtils::UpdateCachedStates(States);
}


#undef LOCTEXT_NAMESPACE
