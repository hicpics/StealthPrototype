// Copyright (c) 2017-2020 Samuel Kahn (samuel@kahncode.com). All Rights Reserved.

#include "GitSourceControlModule.h"
#include "Modules/ModuleManager.h"
#include "ISourceControlModule.h"
#include "GitSourceControlSettings.h"
#include "GitSourceControlOperations.h"
#include "Runtime/Core/Public/Features/IModularFeatures.h"

#define LOCTEXT_NAMESPACE "GitCentral"

template<typename Type>
static TSharedRef<IGitSourceControlWorker, ESPMode::ThreadSafe> CreateWorker()
{
	return MakeShared<Type, ESPMode::ThreadSafe>();
}

void FGitSourceControlModule::StartupModule()
{
	// Register our operations
	GitSourceControlProvider.RegisterWorker("Connect", FGetGitSourceControlWorker::CreateStatic(&CreateWorker<FGitConnectWorker>));
	GitSourceControlProvider.RegisterWorker("CheckOut", FGetGitSourceControlWorker::CreateStatic(&CreateWorker<FGitCheckOutWorker>));
	GitSourceControlProvider.RegisterWorker("UpdateStatus", FGetGitSourceControlWorker::CreateStatic(&CreateWorker<FGitUpdateStatusWorker>));
	GitSourceControlProvider.RegisterWorker("MarkForAdd", FGetGitSourceControlWorker::CreateStatic(&CreateWorker<FGitMarkForAddWorker>));
	GitSourceControlProvider.RegisterWorker("Delete", FGetGitSourceControlWorker::CreateStatic(&CreateWorker<FGitDeleteWorker>));
	GitSourceControlProvider.RegisterWorker("Revert", FGetGitSourceControlWorker::CreateStatic(&CreateWorker<FGitRevertWorker>));
	GitSourceControlProvider.RegisterWorker("Sync", FGetGitSourceControlWorker::CreateStatic(&CreateWorker<FGitSyncWorker>));
	GitSourceControlProvider.RegisterWorker("CheckIn", FGetGitSourceControlWorker::CreateStatic(&CreateWorker<FGitCheckInWorker>));
	GitSourceControlProvider.RegisterWorker("Copy", FGetGitSourceControlWorker::CreateStatic(&CreateWorker<FGitCopyWorker>));
	GitSourceControlProvider.RegisterWorker("Resolve", FGetGitSourceControlWorker::CreateStatic(&CreateWorker<FGitResolveWorker>));
	GitSourceControlProvider.RegisterWorker("ForceUnlock", FGetGitSourceControlWorker::CreateStatic(&CreateWorker<FGitForceUnlockWorker>));
	GitSourceControlProvider.RegisterWorker("ForceWriteable", FGetGitSourceControlWorker::CreateStatic(&CreateWorker<FGitForceWriteableWorker>));

	// load our settings
	GitSourceControlSettings.LoadSettings();

	// Bind our source control provider to the editor
	IModularFeatures::Get().RegisterModularFeature("SourceControl", &GitSourceControlProvider);
}

void FGitSourceControlModule::ShutdownModule()
{
	// shut down the provider, as this module is going away
	GitSourceControlProvider.Close();

	// unbind provider from editor
	IModularFeatures::Get().UnregisterModularFeature("SourceControl", &GitSourceControlProvider);
}

void FGitSourceControlModule::SaveSettings()
{
	if (FApp::IsUnattended() || IsRunningCommandlet())
	{
		return;
	}

	GitSourceControlSettings.SaveSettings();
}

void FGitSourceControlModule::RegisterMenuExtensions()
{
	GitSourceControlMenuExtensions.Register();
}

void FGitSourceControlModule::UnregisterMenuExtensions()
{
	GitSourceControlMenuExtensions.Unregister();
}

IMPLEMENT_MODULE(FGitSourceControlModule, GitCentral);

#undef LOCTEXT_NAMESPACE
