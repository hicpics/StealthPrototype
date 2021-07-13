// Copyright (c) 2017-2020 Samuel Kahn (samuel@kahncode.com). All Rights Reserved.

#pragma once

#include "ISourceControlState.h"
#include "GitSourceControlRevision.h"
#include "Dom/JsonObject.h"

#include "GitSourceControlState.generated.h"

UENUM()
namespace EWorkingCopyState
{
	enum Type
	{
		Unknown, // Can appear in saved file
		Unchanged, // Can appear in saved file
		Added,
		Deleted, // Can appear in saved file
		Modified, // Modified implies checked-out (or local only)
		CheckedOut, // Only set when a file is checked out or locked but locally unchanged. Can appear in saved file
		Conflicted, // Can appear in saved file
		ForcedWriteable, // State when a file is locked by another yet checked out or modified locally. Only possible action is revert.
		NotControlled,
		Ignored,

		//These states only occur after processing remote state
		Outdated, // File has a new remote revision or has been deleted on the remote
		Missing, // File has been added on remote branch
	};
}

namespace EWorkingCopyState
{
	TCHAR ToChar(Type State);
	Type FromChar(TCHAR State);
}

/** Possible saved states
* 0: Unknown. CheckOut -> 1, Sync -> 3
* 1: "Checked out at local revision" (CheckOut / "0"). Revert -> 0, Resolve -> 2
* 2: "Checked out at remote revision" (CheckOut / rev). Revert -> 3, GetLatest -> 1, Check-In -> 4
* 3: "Unchanged at remote revision" (Unknown / rev). CheckOut-> 2, GetLatest -> 0
* 4: "Just commited" (Unknown / commit rev). GetLatest -> 0, CheckOut -> 2
* 5: "Deleted from remote revision" (Deleted / rev). Get here from any revert or sync to remote revision with remote file deleted
* 6: "Conflicted during get latest" (Conflicted / "0"). Resolve -> 1
*/
struct SavedState
{
	SavedState() : State(EWorkingCopyState::Unknown), CheckedOutRevision("0") {}

	bool IsValid() const { return EWorkingCopyState::ToChar(State) != '0' || CheckedOutRevision != "0"; }

	EWorkingCopyState::Type State = EWorkingCopyState::Unknown;
	FString CheckedOutRevision;

	//Serialization
	bool FromJson(const TSharedPtr<FJsonObject>& Json);
	TSharedPtr<FJsonObject> ToJSon() const;
};

//TODO: save all 3 states for easier debugging and more fine tuned behaviors
class FGitSourceControlState : public ISourceControlState, public TSharedFromThis<FGitSourceControlState, ESPMode::ThreadSafe>
{
public:
	FGitSourceControlState( const FString& InLocalFilename )
		: AbsoluteFilename(InLocalFilename)
		, WorkingCopyState(EWorkingCopyState::Unknown)
		, RemoteState(EWorkingCopyState::Unknown)
		, TimeStamp(0)
		, CheckedOutRevision("0")
		, bLockedByOther(false)
		, bOutdated(false)
		, bStaged(false)
	{
	}

	FGitSourceControlState()
		: AbsoluteFilename("")
		, WorkingCopyState(EWorkingCopyState::Unknown)
		, RemoteState(EWorkingCopyState::Unknown)
		, TimeStamp(0)
		, CheckedOutRevision("0")
		, bLockedByOther(false)
		, bOutdated(false)
		, bStaged(false)
	{
	}

	bool IsValid() const { return !AbsoluteFilename.IsEmpty() && WorkingCopyState != EWorkingCopyState::Unknown; }
	void UpdateTimeStamp() { TimeStamp = FDateTime::Now(); }

	//Final status is computed by combining in this order
	//- Local state of the commits from the merge ancestor to the local branch head
	//- State from git status
	//- Saved State
	//- State from the merge ancestor to the remote head

	/** Combines a state with another state from the local branch, only keeping most meaningful action
	* This is useful when aggregating statuses and diffs to determine final state against merge ancestor
	* Examples: if a file has been both Added and Modified locally, the final state will still be Added,
	*
	* @param	Other				State to combine with this one
	*/
	void CombineWithLocalState(const FGitSourceControlState& Other);

	/** Combines a local state with a saved state from the file to add our custom semantics to the state
	*
	* @param	SavedState				SavedState from the file
	*/
	void CombineWithSavedState(const SavedState& SavedState, const FString& LocalBranchSha);

	/** Combines a local state with a state from the remote branch, only keeping most meaningful action
	* This is useful when aggregating statuses and diffs to determine final state against merge ancestor
	* Examples: if a file is Modified locally and Modified on the remote, the final state will be Outdated.
	*
	* @param	Other				State to combine with this one
	*/
	void CombineWithRemoteState(const FGitSourceControlState& Other);

	/** Combines a state with valid local and remote state with information from git lfs locks
	* This is the final step of computing the remote state, the most useful state for the user is preserved
	* Examples: if a file is locked by another user and also outdated, the final state will be Outdated
	* as the first step for the user will be to sync the file before having to deal with locks
	*
	* @param	Other				State to combine with this one
	*/
	void CombineWithLockedState(const FGitSourceControlState& Other);

	void ResolveConflict(EWorkingCopyState::Type OldState);

	bool operator==(const FGitSourceControlState& Other) const;
	bool operator!=(const FGitSourceControlState& Other) const;

	/** ISourceControlState interface */
	virtual int32 GetHistorySize() const override;
	virtual TSharedPtr<class ISourceControlRevision, ESPMode::ThreadSafe> GetHistoryItem(int32 HistoryIndex) const override;
	virtual TSharedPtr<class ISourceControlRevision, ESPMode::ThreadSafe> FindHistoryRevision(int32 RevisionNumber) const override;
	virtual TSharedPtr<class ISourceControlRevision, ESPMode::ThreadSafe> FindHistoryRevision(const FString& InRevision) const override;
	virtual TSharedPtr<class ISourceControlRevision, ESPMode::ThreadSafe> GetBaseRevForMerge() const override;
	virtual FName GetIconName() const override;
	virtual FName GetSmallIconName() const override;
	virtual FText GetDisplayName() const override;
	virtual FText GetDisplayTooltip() const override;
	virtual const FString& GetFilename() const override;
	virtual const FDateTime& GetTimeStamp() const override;
	virtual bool CanCheckIn() const override;
	virtual bool CanRevert() const override;
	virtual bool CanCheckout() const override;
	virtual bool IsCheckedOut() const override;
	virtual bool IsCheckedOutOther(FString* Who = nullptr) const override;
	virtual bool IsCheckedOutInOtherBranch(const FString& CurrentBranch = FString()) const override { return false; }
	virtual bool IsModifiedInOtherBranch(const FString& CurrentBranch = FString()) const override { return false; }
	virtual bool IsCheckedOutOrModifiedInOtherBranch(const FString& CurrentBranch = FString()) const override { return IsCheckedOutInOtherBranch(CurrentBranch) || IsModifiedInOtherBranch(CurrentBranch); }
	virtual TArray<FString> GetCheckedOutBranches() const override { return TArray<FString>(); }
	virtual FString GetOtherUserBranchCheckedOuts() const override { return FString(); }
	virtual bool GetOtherBranchHeadModification(FString& HeadBranchOut, FString& ActionOut, int32& HeadChangeListOut) const override { return false; }
	virtual bool IsCurrent() const override;
	virtual bool IsSourceControlled() const override;
	virtual bool IsAdded() const override;
	virtual bool IsDeleted() const override;
	virtual bool IsIgnored() const override;
	virtual bool CanEdit() const override;
	virtual bool IsUnknown() const override;
	virtual bool IsModified() const override;
	virtual bool CanAdd() const override;
	virtual bool CanDelete() const override;
	virtual bool IsConflicted() const override;

	bool IsLockedByMe() const;
	bool CanLock() const;
	bool CanUnlock() const;
	bool HasValidLockId() const;

	//Fixes lock if we should be in a locked state otherwise but aren't
	bool CanFixLock() const;

	//Force Writeable when file is locked by another and not checked out locally yet
	bool CanForceWriteable() const;

	void DebugPrint() const;
	
public:
	/** History of the item, if any */
	TGitSourceControlHistory History;

	/** Filename on disk */
	FString AbsoluteFilename;

	/** State of the working copy */
	EWorkingCopyState::Type WorkingCopyState;

	/** State of the file on the remote */
	EWorkingCopyState::Type RemoteState;

	/** The timestamp of the last update */
	FDateTime TimeStamp;

	/** The base revision we are considering for this file */
	FString CheckedOutRevision;

	/** If another user has this file locked, this contains their name(s). Multiple users are comma-delimited */
	FString UserLocked;

	/** Id of the lock, in case we need it to unlock files by id */
	int LockId = -1;

	/** Whether there are changes on the remote that were not pulled */
	bool bLockedByOther;

	/** Whether there are changes on the remote that were not pulled */
	bool bOutdated;

	/** Whether this file is staged */
	bool bStaged;
};
