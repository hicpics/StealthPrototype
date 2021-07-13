// Copyright (c) 2017-2020 Samuel Kahn (samuel@kahncode.com). All Rights Reserved.

#include "GitSourceControlCommand.h"
#include "GitSourceControlModule.h"
#include "GitSourceControlProvider.h"
#include "IGitSourceControlWorker.h"
#include "SGitSourceControlSettings.h"

FGitSourceControlCommand::FGitSourceControlCommand(const TSharedRef<class ISourceControlOperation, ESPMode::ThreadSafe>& InOperation, const TSharedRef<class IGitSourceControlWorker, ESPMode::ThreadSafe>& InWorker, const FSourceControlOperationComplete& InOperationCompleteDelegate)
	: Operation(InOperation)
	, Worker(InWorker)
	, OperationCompleteDelegate(InOperationCompleteDelegate)
	, bExecuteProcessed(0)
	, bCommandSuccessful(false)
	, bAutoDelete(true)
	, Concurrency(EConcurrency::Synchronous)
{
	// grab the providers settings here, so we don't access them once the worker thread is launched
	check(IsInGameThread());
	FGitSourceControlModule& GitSourceControl = FGitSourceControlModule::GetInstance();
	PathToGitBinary = GitSourceControl.AccessSettings().GetBinaryPath();
	PathToRepositoryRoot = GitSourceControl.GetProvider().GetPathToRepositoryRoot();
	Branch = GitSourceControl.GetProvider().GetBranch();
	Remote = GitSourceControl.GetProvider().GetRemote();
	bUseLocking = GitSourceControl.AccessSettings().IsUsingLocking();
}

bool FGitSourceControlCommand::DoWork()
{
	bCommandSuccessful = Worker->Execute(*this);
	FPlatformAtomics::InterlockedExchange(&bExecuteProcessed, 1);

	return bCommandSuccessful;
}

void FGitSourceControlCommand::Abandon()
{
	FPlatformAtomics::InterlockedExchange(&bExecuteProcessed, 1);
}

void FGitSourceControlCommand::DoThreadedWork()
{
	Concurrency = EConcurrency::Asynchronous;
	DoWork();
}
