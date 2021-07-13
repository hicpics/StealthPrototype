// Copyright (c) 2017-2020 Samuel Kahn (samuel@kahncode.com). All Rights Reserved.

#pragma once

#include "IGitSourceControlWorker.h"
#include "GitSourceControlState.h"
#include "GitSourceControlRevision.h"
#include "SourceControlOperationBase.h"

/** Called when first activated on a project, and then at project load time.
	Checks availability of the remote and specified branch */
class FGitConnectWorker : public IGitSourceControlWorker
{
public:
	virtual ~FGitConnectWorker() {}
	// IGitSourceControlWorker interface
	virtual FName GetName() const override;
	virtual bool Execute(class FGitSourceControlCommand& InCommand) override;
	virtual bool UpdateStates() const override;
	virtual bool IsConnected() const override;

private:
	bool bConnected;
};

/** Mark as checked out or lock (p4 check-out) a set of file to the local depot. */
class FGitCheckOutWorker : public IGitSourceControlWorker
{
public:
	virtual ~FGitCheckOutWorker() {}
	// IGitSourceControlWorker interface
	virtual FName GetName() const override;
	virtual bool Execute(class FGitSourceControlCommand& InCommand) override;
	virtual bool UpdateStates() const override;

private:
	/** Temporary states for results */
	TArray<FGitSourceControlState> States;
};

/** Commit (check-in) a set of file to the remote depot while keeping the rest of the workspace instact */
class FGitCheckInWorker : public IGitSourceControlWorker
{
public:
	virtual ~FGitCheckInWorker() {}
	// IGitSourceControlWorker interface
	virtual FName GetName() const override;
	virtual bool Execute(class FGitSourceControlCommand& InCommand) override;
	virtual bool UpdateStates() const override;

private:
	// Performs git cleanup
	void Cleanup(class FGitSourceControlCommand& InCommand, bool bShouldReset);

private:
	/** Temporary states for results */
	TArray<FGitSourceControlState> States;
};

/** Do nothing, update status, this is automatic */
class FGitMarkForAddWorker : public IGitSourceControlWorker
{
public:
	virtual ~FGitMarkForAddWorker() {}
	// IGitSourceControlWorker interface
	virtual FName GetName() const override;
	virtual bool Execute(class FGitSourceControlCommand& InCommand) override;
	virtual bool UpdateStates() const override;

private:
	/** Temporary states for results */
	TArray<FGitSourceControlState> States;
};

/** Delete a file and remove it from source control. */
class FGitDeleteWorker : public IGitSourceControlWorker
{
public:
	virtual ~FGitDeleteWorker() {}
	// IGitSourceControlWorker interface
	virtual FName GetName() const override;
	virtual bool Execute(class FGitSourceControlCommand& InCommand) override;
	virtual bool UpdateStates() const override;

private:
	/** Temporary states for results */
	TArray<FGitSourceControlState> States;
};

/** Revert any change to a file to its state on the local depot. */
class FGitRevertWorker : public IGitSourceControlWorker
{
public:
	virtual ~FGitRevertWorker() {}
	// IGitSourceControlWorker interface
	virtual FName GetName() const override;
	virtual bool Execute(class FGitSourceControlCommand& InCommand) override;
	virtual bool UpdateStates() const override;

private:
	/** Map of filenames to Git state */
	TArray<FGitSourceControlState> States;
};

/** Pull from remote while keeping local changes, effectively emulating perforce "Get Latest" */
class FGitSyncWorker : public IGitSourceControlWorker
{
public:
	virtual ~FGitSyncWorker() {}
	// IGitSourceControlWorker interface
	virtual FName GetName() const override;
	virtual bool Execute(class FGitSourceControlCommand& InCommand) override;
	virtual bool GetLatest(class FGitSourceControlCommand& InCommand);
	virtual bool UpdateStates() const override;

private:
	// Map of filenames to Git state
	TArray<FGitSourceControlState> States;
	TArray<FString> UpdatedFiles;

private:
	void Cleanup();
	bool FoundRebaseConflict(const TArray<FString> Message) const;
	bool ParsePullResults(class FGitSourceControlCommand& InCommand, const TArray<FString>& Results);
};

/** Get source control status of files on local working copy. */
class FGitUpdateStatusWorker : public IGitSourceControlWorker
{
public:
	virtual ~FGitUpdateStatusWorker() {}
	// IGitSourceControlWorker interface
	virtual FName GetName() const override;
	virtual bool Execute(class FGitSourceControlCommand& InCommand) override;
	virtual bool UpdateStates() const override;
	virtual bool IsConnected() const override;

private:
	/** Temporary states for results */
	TArray<FGitSourceControlState> States;

	/** Map of filenames to history */
	TMap<FString, TGitSourceControlHistory> Histories;

	bool bConnected;
};

/** Copy or Move operation on a single file */
class FGitCopyWorker : public IGitSourceControlWorker
{
public:
	virtual ~FGitCopyWorker() {}
	// IGitSourceControlWorker interface
	virtual FName GetName() const override;
	virtual bool Execute(class FGitSourceControlCommand& InCommand) override;
	virtual bool UpdateStates() const override;
};

/** Resovles the state by marking it in the status file */ 
class FGitResolveWorker : public IGitSourceControlWorker
{
public:
	virtual ~FGitResolveWorker() {}
	virtual FName GetName() const override;
	virtual bool Execute(class FGitSourceControlCommand& InCommand) override;
	virtual bool UpdateStates() const override;
	
private:
	/** Temporary states for results */
	TArray<FGitSourceControlState> States;
};

//////////////////////////////////////////////////////////////////////////
// End of standard operations and workers
//////////////////////////////////////////////////////////////////////////

/**
 * Operation to force unlock of locked files
 */
class FForceUnlock : public FSourceControlOperationBase
{
public:
	// ISourceControlOperation interface
	virtual FName GetName() const override
	{
		return "ForceUnlock";
	}

	virtual FText GetInProgressString() const override;
};

/** Resovles the state by marking it in the status file */
class FGitForceUnlockWorker : public IGitSourceControlWorker
{
public:
	virtual ~FGitForceUnlockWorker() {}
	virtual FName GetName() const override;
	virtual bool Execute(class FGitSourceControlCommand& InCommand) override;
	virtual bool UpdateStates() const override;

private:
	/** Temporary states for results */
	TArray<FGitSourceControlState> States;
};

/**
 * Operation to force files to be Writeable
 */
class FForceWriteable : public FSourceControlOperationBase
{
public:
	// ISourceControlOperation interface
	virtual FName GetName() const override
	{
		return "ForceWriteable";
	}

	virtual FText GetInProgressString() const override;
};

/** Resovles the state by marking it in the status file */
class FGitForceWriteableWorker : public IGitSourceControlWorker
{
public:
	virtual ~FGitForceWriteableWorker() {}
	virtual FName GetName() const override;
	virtual bool Execute(class FGitSourceControlCommand& InCommand) override;
	virtual bool UpdateStates() const override;

private:
	/** Temporary states for results */
	TArray<FGitSourceControlState> States;
};
