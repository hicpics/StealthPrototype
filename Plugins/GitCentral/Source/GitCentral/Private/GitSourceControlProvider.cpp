// Copyright (c) 2017-2020 Samuel Kahn (samuel@kahncode.com). All Rights Reserved.

#include "GitSourceControlProvider.h"
#include "GitSourceControlCommand.h"
#include "ISourceControlModule.h"
#include "GitSourceControlModule.h"
#include "GitSourceControlSettings.h"
#include "GitSourceControlOperations.h"
#include "GitSourceControlUtils.h"
#include "SGitSourceControlSettings.h"
#include "ScopedSourceControlProgress.h"
#include "SourceControlHelpers.h"
#include "SourceControlOperations.h"

#define LOCTEXT_NAMESPACE "GitCentral"

static FName ProviderName("GitCentral");

void FGitSourceControlProvider::Init(bool bForceConnection)
{
	CheckGitAvailability();

	// bForceConnection: not used anymore

	FGitSourceControlModule::GetInstance().RegisterMenuExtensions();
}

bool FGitSourceControlProvider::CheckGitAvailability()
{
	const FString OldRepositoryRootPath = PathToRepositoryRoot;
	const bool bWasGitAvailable = bGitRepositoryFound;

	//Reset connected flag, will be set by the next Connect or Update
	bConnected = false;
	bGitAvailable = false;

	FGitSourceControlModule& GitSourceControl = FGitSourceControlModule::GetInstance();
	const FString& PathToGitBinary = GitSourceControl.AccessSettings().GetBinaryPath();
	if(!PathToGitBinary.IsEmpty())
	{
		bGitAvailable = GitSourceControlUtils::CheckGitAvailability(PathToGitBinary);
		if(bGitAvailable)
		{
			bool bRepositoryFound = false;

			// First test user supplied directory
			FString UserRoot = GitSourceControl.AccessSettings().GetRootPath();
			GitSourceControlUtils::TrimTrailingSlashes(UserRoot);
			if(!UserRoot.IsEmpty())
			{
				bRepositoryFound = GitSourceControlUtils::IsGitRepository(UserRoot);
				PathToRepositoryRoot = UserRoot;
			}
			else
			{
				// Find the path to the root Git directory (if any)
				// Start in content folder to catch nested root setup
				// Root can be changed in UI to enforce working in a particular git repository

				const FString PathToGameDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir());
				bRepositoryFound = GitSourceControlUtils::FindRootDirectory(PathToGameDir, PathToRepositoryRoot);
			}

			if (bRepositoryFound)
			{
				// Get user name & email (of the repository, else from the global Git config)
				GitSourceControlUtils::GetUserConfig(PathToGitBinary, PathToRepositoryRoot, UserName, UserEmail);

				// Get active branch name from repository
				GitSourceControlUtils::GetBranchName(PathToGitBinary, PathToRepositoryRoot, BranchName);

				const FString& UserBranch = GitSourceControl.AccessSettings().GetBranch();
				if(!UserBranch.IsEmpty() && UserBranch != BranchName)
				{
					GITCENTRAL_ERROR(TEXT("Unexpected active branch (%s). Close the editor and switch to the correct branch (git checkout %s)"),
						*BranchName, *UserBranch);
				
					bGitRepositoryFound = false;
				}
				else
				{
					bGitRepositoryFound = true;
				}

				// List and test remote parameter
				if(bGitRepositoryFound)
				{
					const FString& UserRemote = GitSourceControl.AccessSettings().GetRemote();
					if(UserRemote.IsEmpty())
						RemoteName = "origin";
					else
						RemoteName = UserRemote;

					TArray<FString> RemoteNames;
					GitSourceControlUtils::GetRemoteNames(PathToGitBinary, PathToRepositoryRoot, RemoteNames);

					if(!RemoteNames.Contains(RemoteName))
					{
						GITCENTRAL_ERROR(TEXT("Remote was not found (%s)"), *RemoteName);
						bGitRepositoryFound = false;

						if(RemoteNames.Contains("origin"))
						{
							RemoteName = "origin";
						}
						else if(RemoteNames.Num() > 0)
						{
							RemoteName = RemoteNames[0];
						}
					}
				}
			}
			else
			{
				GITCENTRAL_ERROR(TEXT("'%s' is not part of a Git repository"), *FPaths::ProjectDir());
				bGitRepositoryFound = false;
			}
		}
	}
	

	if(!bGitAvailable)
	{
		GITCENTRAL_ERROR(TEXT("Git is not available at '%s'"), *PathToGitBinary);
		bGitAvailable = false;
	}

	if(PathToRepositoryRoot != OldRepositoryRootPath || bWasGitAvailable != bGitAvailable)
	{
		GITCENTRAL_LOG(TEXT("GitCentral status: git found? %s, repository: %s, reloaded cache file")
			, bGitAvailable ? TEXT("true") : TEXT("false"), bGitRepositoryFound ? *PathToRepositoryRoot : TEXT("not found"));
	}

	return bGitAvailable;
}

void FGitSourceControlProvider::ClearCache()
{
	StateCache.Empty();
	bForceBroadcastUpdateNextTick = true;
}

void FGitSourceControlProvider::Close()
{
	ClearCache();
	FGitSourceControlModule::GetInstance().UnregisterMenuExtensions();
}

TSharedRef<FGitSourceControlState, ESPMode::ThreadSafe> FGitSourceControlProvider::GetStateInternal(const FString& Filename)
{
	TSharedRef<FGitSourceControlState, ESPMode::ThreadSafe>* State = StateCache.Find(Filename);
	if(State != NULL)
	{
		// found cached item
		return (*State);
	}
	else
	{
		// cache an unknown state for this item
		TSharedRef<FGitSourceControlState, ESPMode::ThreadSafe> NewState = MakeShared<FGitSourceControlState, ESPMode::ThreadSafe>(Filename);
		StateCache.Add(Filename, NewState);
		return NewState;
	}
}

FText FGitSourceControlProvider::GetStatusText() const
{
	FFormatNamedArguments Args;
	Args.Add( TEXT("RepositoryName"), FText::FromString(PathToRepositoryRoot) );
	Args.Add( TEXT("BranchName"), FText::FromString(BranchName) );
	Args.Add( TEXT("RemoteName"), FText::FromString(RemoteName) );
	Args.Add( TEXT("UserName"), FText::FromString(UserName) );
	Args.Add( TEXT("UserEmail"), FText::FromString(UserEmail) );

	return FText::Format( NSLOCTEXT("Status", "Provider: Git\nEnabledLabel", "Repository: {RepositoryName}\nBranch: {BranchName}\nRemote: {RemoteName}\nUser: {UserName}\nE-mail: {UserEmail}"), Args );
}

/** Quick check if source control is enabled */
bool FGitSourceControlProvider::IsEnabled() const
{
	return bGitRepositoryFound && bGitAvailable;
}

/** Quick check if source control is available for use (useful for server-based providers) */
bool FGitSourceControlProvider::IsAvailable() const
{
	return bConnected;
}

const FName& FGitSourceControlProvider::GetName(void) const
{
	return ProviderName;
}

ECommandResult::Type FGitSourceControlProvider::GetState( const TArray<FString>& InFiles, TArray< TSharedRef<ISourceControlState, ESPMode::ThreadSafe> >& OutState, EStateCacheUsage::Type InStateCacheUsage )
{
	if(!IsEnabled())
	{
		return ECommandResult::Failed;
	}

	TArray<FString> AbsoluteFiles = USourceControlHelpers::AbsoluteFilenames(InFiles);

	//Note that alike the other source control providers, if the state is not in cache it will not attept to fetch it
	if(InStateCacheUsage == EStateCacheUsage::ForceUpdate)
	{
		Execute(ISourceControlOperation::Create<FUpdateStatus>(), AbsoluteFiles);
	}

	for(const auto& AbsoluteFile : AbsoluteFiles)
	{
		OutState.Add(GetStateInternal(*AbsoluteFile));
	}

	return ECommandResult::Succeeded;
}

TArray<FSourceControlStateRef> FGitSourceControlProvider::GetCachedStateByPredicate(TFunctionRef<bool(const FSourceControlStateRef&)> Predicate) const
{
	TArray<FSourceControlStateRef> Result;
	for(const auto& CacheItem : StateCache)
	{
		FSourceControlStateRef State = CacheItem.Value;
		if(Predicate(State))
		{
			Result.Add(State);
		}
	}
	return Result;
}

bool FGitSourceControlProvider::RemoveFileFromCache(const FString& Filename)
{
	return StateCache.Remove(Filename) > 0;
}

FDelegateHandle FGitSourceControlProvider::RegisterSourceControlStateChanged_Handle( const FSourceControlStateChanged::FDelegate& SourceControlStateChanged )
{
	return OnSourceControlStateChanged.Add( SourceControlStateChanged );
}

void FGitSourceControlProvider::UnregisterSourceControlStateChanged_Handle( FDelegateHandle Handle )
{
	OnSourceControlStateChanged.Remove( Handle );
}

ECommandResult::Type FGitSourceControlProvider::Execute( const TSharedRef<ISourceControlOperation, ESPMode::ThreadSafe>& InOperation, const TArray<FString>& InFiles, EConcurrency::Type InConcurrency, const FSourceControlOperationComplete& InOperationCompleteDelegate )
{
	if(!IsEnabled())
	{
		return ECommandResult::Failed;
	}

	//Fully log all issued commands
	if(UE_LOG_ACTIVE(LogSourceControl, Verbose))
	{
		FString Files;
		for (const auto& File : InFiles)
		{
			Files += File;
			Files += TEXT("\n");
		}
		GITCENTRAL_VERBOSE(TEXT("FGitSourceControlProvider::Execute: %s, Files/Params: %d\n%s"), *InOperation->GetName().ToString(), InFiles.Num(), *Files);
	}

	TArray<FString> AbsoluteFiles = USourceControlHelpers::AbsoluteFilenames(InFiles);

	// Query to see if we allow this operation
	TSharedPtr<IGitSourceControlWorker, ESPMode::ThreadSafe> Worker = CreateWorker(InOperation->GetName());
	if(!Worker.IsValid())
	{
		// this operation is unsupported by this source control provider
		FFormatNamedArguments Arguments;
		Arguments.Add( TEXT("OperationName"), FText::FromName(InOperation->GetName()) );
		Arguments.Add( TEXT("ProviderName"), FText::FromName(GetName()) );
		FMessageLog("SourceControl").Error(FText::Format(LOCTEXT("UnsupportedOperation", "Operation '{OperationName}' not supported by source control provider '{ProviderName}'"), Arguments));
		return ECommandResult::Failed;
	}

	FGitSourceControlCommand* Command = new FGitSourceControlCommand(InOperation, Worker.ToSharedRef());
	Command->Files = AbsoluteFiles;
	Command->OperationCompleteDelegate = InOperationCompleteDelegate;

	// fire off operation
	if(InConcurrency == EConcurrency::Synchronous)
	{
		Command->bAutoDelete = false;
		return ExecuteSynchronousCommand(*Command, InOperation->GetInProgressString());
	}
	else
	{
		Command->bAutoDelete = true;
		return IssueCommand(*Command);
	}
}

bool FGitSourceControlProvider::CanCancelOperation( const TSharedRef<ISourceControlOperation, ESPMode::ThreadSafe>& InOperation ) const
{
	return false;
}

void FGitSourceControlProvider::CancelOperation( const TSharedRef<ISourceControlOperation, ESPMode::ThreadSafe>& InOperation )
{
}

bool FGitSourceControlProvider::UsesLocalReadOnlyState() const
{
	return false;
}

bool FGitSourceControlProvider::UsesChangelists() const
{
	return false;
}

bool FGitSourceControlProvider::UsesCheckout() const
{
	return true;
}

TSharedPtr<IGitSourceControlWorker, ESPMode::ThreadSafe> FGitSourceControlProvider::CreateWorker(const FName& InOperationName) const
{
	const FGetGitSourceControlWorker* Operation = WorkersMap.Find(InOperationName);
	if(Operation != nullptr)
	{
		return Operation->Execute();
	}
		
	return nullptr;
}

void FGitSourceControlProvider::RegisterWorker( const FName& InName, const FGetGitSourceControlWorker& InDelegate )
{
	WorkersMap.Add( InName, InDelegate );
}

void FGitSourceControlProvider::OutputCommandMessages(const FGitSourceControlCommand& InCommand) const
{
	FMessageLog SourceControlLog("SourceControl");

	for(int32 ErrorIndex = 0; ErrorIndex < InCommand.ErrorMessages.Num(); ++ErrorIndex)
	{
		SourceControlLog.Error(FText::FromString(InCommand.ErrorMessages[ErrorIndex]));
	}

	for(int32 InfoIndex = 0; InfoIndex < InCommand.InfoMessages.Num(); ++InfoIndex)
	{
		SourceControlLog.Info(FText::FromString(InCommand.InfoMessages[InfoIndex]));
	}
}

void FGitSourceControlProvider::Tick()
{	
	bool bStatesUpdated = false;
	for(int32 CommandIndex = 0; CommandIndex < CommandQueue.Num(); ++CommandIndex)
	{
		FGitSourceControlCommand& Command = *CommandQueue[CommandIndex];
		if(Command.bExecuteProcessed)
		{
			// Remove command from the queue
			CommandQueue.RemoveAt(CommandIndex);

			// let command update the states of any files
			bStatesUpdated |= Command.Worker->UpdateStates();

			if(Command.Worker->IsConnected())
				bConnected = true;

			// dump any messages to output log
			OutputCommandMessages(Command);

			// run the completion delegate callback if we have one bound
			ECommandResult::Type Result = Command.bCommandSuccessful ? ECommandResult::Succeeded : ECommandResult::Failed;

			GITCENTRAL_VERBOSE(TEXT("FGitSourceControlProvider::CommandFinished: %s, Success: %s"), *Command.Operation->GetName().ToString(), Command.bCommandSuccessful ? TEXT("true") : TEXT("false"));

			Command.OperationCompleteDelegate.ExecuteIfBound(Command.Operation, Result);

			// commands that are left in the array during a tick need to be deleted
			if(Command.bAutoDelete)
			{
				// Only delete commands that are not running 'synchronously'
				delete &Command;
			}
			
			// only do one command per tick loop, as we dont want concurrent modification 
			// of the command queue (which can happen in the completion delegate)
			break;
		}
	}

	if(bStatesUpdated || bForceBroadcastUpdateNextTick)
	{
		OnSourceControlStateChanged.Broadcast();
		bForceBroadcastUpdateNextTick = false;
	}
}

TArray< TSharedRef<ISourceControlLabel> > FGitSourceControlProvider::GetLabels( const FString& InMatchingSpec ) const
{
	TArray< TSharedRef<ISourceControlLabel> > Tags;

	// TODO SRombauts : tags
	if (bGitRepositoryFound)
	{
		FGitSourceControlModule& GitSourceControl = FGitSourceControlModule::GetInstance();
		const FString& PathToGitBinary = GitSourceControl.AccessSettings().GetBinaryPath();
		TArray<FString> InfoMessages;
		TArray<FString> ErrorMessages;
		TArray<FString> Parameters;
		Parameters.Add(TEXT("--list"));
		Parameters.Add(InMatchingSpec);
		GitSourceControlUtils::RunCommand(TEXT("tag"), PathToGitBinary, PathToRepositoryRoot, Parameters, TArray<FString>(), InfoMessages, ErrorMessages);
	}

	return Tags;
}

#if SOURCE_CONTROL_WITH_SLATE
TSharedRef<class SWidget> FGitSourceControlProvider::MakeSettingsWidget() const
{
	return SNew(SGitSourceControlSettings);
}
#endif

ECommandResult::Type FGitSourceControlProvider::ExecuteSynchronousCommand(FGitSourceControlCommand& InCommand, const FText& Task)
{
	ECommandResult::Type Result = ECommandResult::Failed;

	// Display the progress dialog if a string was provided
	{
		FScopedSourceControlProgress Progress(Task);

		// Issue the command asynchronously...
		IssueCommand( InCommand );

		// ... then wait for its completion (thus making it synchrounous)
		while(!InCommand.bExecuteProcessed)
		{
			// Tick the command queue and update progress.
			Tick();
			
			Progress.Tick();

			// Sleep for a bit so we don't busy-wait so much.
			FPlatformProcess::Sleep(0.01f);
		}
	
		// always do one more Tick() to make sure the command queue is cleaned up.
		Tick();

		if(InCommand.bCommandSuccessful)
		{
			Result = ECommandResult::Succeeded;
		}
	}

	// Delete the command now (asynchronous commands are deleted in the Tick() method)
	check(!InCommand.bAutoDelete);

	// ensure commands that are not auto deleted do not end up in the command queue
	if ( CommandQueue.Contains( &InCommand ) ) 
	{
		CommandQueue.Remove( &InCommand );
	}
	delete &InCommand;

	return Result;
}

ECommandResult::Type FGitSourceControlProvider::IssueCommand(FGitSourceControlCommand& InCommand)
{
	if(GThreadPool != nullptr)
	{
		// Queue this to our worker thread(s) for resolving
		GThreadPool->AddQueuedWork(&InCommand);
		CommandQueue.Add(&InCommand);
		return ECommandResult::Succeeded;
	}
	else
	{
		return ECommandResult::Failed;
	}
}
#undef LOCTEXT_NAMESPACE
