// Copyright (c) 2017-2018 Samuel Kahn (samuel@kahncode.com). All Rights Reserved.

#pragma once

#include "Runtime/AssetRegistry/Public/AssetData.h"
#include "Editor/LevelEditor/Public/LevelEditor.h"
#include "Editor/ContentBrowser/Public/ContentBrowserDelegates.h"
#include "ISourceControlOperation.h"
#include "ISourceControlProvider.h"

class UPackage;

class FGitSourceControlMenuExtensions
{
public:
	FGitSourceControlMenuExtensions();
	~FGitSourceControlMenuExtensions() {}

	void Register();
	void Unregister();

private:
	TSharedRef<FExtender> GetAssetContextMenuExtender(const TArray<FAssetData>& SelectedAssets);
	TSharedRef<FExtender> GetSourceControlMenuExtender(const TSharedRef<FUICommandList>);

	bool IsSourceControlConnected();

	void AddSourceControlMenuExtension(FMenuBuilder& MenuBuilder);
	void AddAssetContextMenuExtension(FMenuBuilder& MenuBuilder);

	void OnResolveAllConflicts();
	void OnGetLatest();
	void OnResolveYours();
	void OnResolveTheirs();
	void ExecuteResolveYours(const TArray<FString>& PackageNames);
	void ExecuteResolveTheirs(const TArray<FString>& PackageNames);
	void OnLock();
	void OnForceUnlock();
	void OnForceWriteable();

	//Resolve All UI callbacks
	/** Callback for OnResolveAllConflicts(), continues to bring up UI once source control operations are complete */
	void ChoosePackagesToResolveCallback(const FSourceControlOperationRef& InOperation, ECommandResult::Type InResult);
	/** Called when the process has completed and we have packages to resolve */
	void ChoosePackagesToResolveCompleted(const TArray<UPackage*>& LoadedPackages, const TArray<FString>& PackageNames);
	/** Delegate called when the user has decided to cancel the resolve process */
	void ChoosePackagesToResolveCancelled(FSourceControlOperationRef InOperation);
	/** Show the dialog and perform resolve on success */
	void PromptForResolve(const TArray<FString>& InPackageNames);

	//Returns filenames to run the command on
	TArray<UPackage*> PreparePackagesForReload(const TArray<FString>& PackageNames);
	bool ReloadPackages(const TArray<FString>& PackageFilenamesToReload);
	bool ReloadPackages(TArray<UPackage*>& PackagesToReload);

	FLevelEditorModule::FLevelEditorMenuExtender SourceControlMenuDelegate;
	FContentBrowserMenuExtender_SelectedAssets AssetContextMenuDelegate;

	/** The notification in place while we choose packages to resolve */
	TWeakPtr<class SNotificationItem> ChoosePackagesToResolveNotification;

	TArray<FAssetData> SavedSelectedAssets;
	bool bRegistered;
};