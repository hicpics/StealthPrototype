// Copyright (c) 2017-2020 Samuel Kahn (samuel@kahncode.com). All Rights Reserved.

#include "GitSourceControlMenuExtensions.h"

#include "SGitSourceControlResolveWidget.h"
#include "GitSourceControlModule.h"
#include "GitSourceControlOperations.h"

#include "UObject/UObjectGlobals.h"
#include "ContentBrowserModule.h"
#include "SourceControlHelpers.h"
#include "PackageTools.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Logging/MessageLog.h"
#include "Settings/EditorExperimentalSettings.h"
#include "FileHelpers.h"
#include "UnrealEdMisc.h"
#include "Dialogs/Dialogs.h"
#include "SourceControlOperations.h"
#include "EditorStyle/Public/EditorStyleSet.h"
#include "Misc/MessageDialog.h"

#define LOCTEXT_NAMESPACE "GitCentral"

FGitSourceControlMenuExtensions::FGitSourceControlMenuExtensions()
{
	bRegistered = false;
	SourceControlMenuDelegate = FLevelEditorModule::FLevelEditorMenuExtender::CreateRaw(this, &FGitSourceControlMenuExtensions::GetSourceControlMenuExtender);
	AssetContextMenuDelegate = FContentBrowserMenuExtender_SelectedAssets::CreateRaw(this, &FGitSourceControlMenuExtensions::GetAssetContextMenuExtender);
}

void FGitSourceControlMenuExtensions::Register()
{
	if(bRegistered)
		return;

	//Register our menu extenders for source control menu
	FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
	LevelEditorModule.GetAllLevelEditorToolbarSourceControlMenuExtenders().Add(SourceControlMenuDelegate);

	//Register our menu extenders for asset context menu
	FContentBrowserModule& ContentBrowserModule = FModuleManager::GetModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
	ContentBrowserModule.GetAllAssetViewContextMenuExtenders().Add(AssetContextMenuDelegate);

	bRegistered = true;
}

void FGitSourceControlMenuExtensions::Unregister()
{
	if(!bRegistered)
		return;

	FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
	LevelEditorModule.GetAllLevelEditorToolbarSourceControlMenuExtenders().RemoveAll(
		[&](const FLevelEditorModule::FLevelEditorMenuExtender& In) { return In.GetHandle() == SourceControlMenuDelegate.GetHandle(); });

	FContentBrowserModule& ContentBrowserModule = FModuleManager::GetModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
	ContentBrowserModule.GetAllAssetViewContextMenuExtenders().RemoveAll(
		[&](const FContentBrowserMenuExtender_SelectedAssets& In) { return In.GetHandle() == AssetContextMenuDelegate.GetHandle(); });

	bRegistered = false;
}

TSharedRef<FExtender> FGitSourceControlMenuExtensions::GetAssetContextMenuExtender(const TArray<FAssetData>& SelectedAssets)
{
	SavedSelectedAssets = SelectedAssets;

	TSharedRef<FExtender> SourceControlMenuExtender = MakeShared<FExtender>();
	SourceControlMenuExtender->AddMenuExtension("AssetSourceControlActions", EExtensionHook::First, NULL, FMenuExtensionDelegate::CreateRaw(this, &FGitSourceControlMenuExtensions::AddAssetContextMenuExtension));
	return SourceControlMenuExtender;
}

TSharedRef<FExtender> FGitSourceControlMenuExtensions::GetSourceControlMenuExtender(const TSharedRef<FUICommandList>)
{
	TSharedRef<FExtender> AssetContextMenuExtender = MakeShared<FExtender>();
	AssetContextMenuExtender->AddMenuExtension("SourceControlActions", EExtensionHook::After, NULL, FMenuExtensionDelegate::CreateRaw(this, &FGitSourceControlMenuExtensions::AddSourceControlMenuExtension));
	return AssetContextMenuExtender;
}

void FGitSourceControlMenuExtensions::AddSourceControlMenuExtension(FMenuBuilder& MenuBuilder)
{
	//Resolve all conflicts
	MenuBuilder.AddMenuEntry(
		LOCTEXT("GitCentralResolveAllLabel", "Resolve Conflicts..."),
		LOCTEXT("GitCentralResolveAllTooltip", "Opens a dialog for resolving all conflicts."),
		FSlateIcon(FEditorStyle::GetStyleSetName(), "Subversion.NotInDepot_Small"),
		FUIAction(
			FExecuteAction::CreateRaw(this, &FGitSourceControlMenuExtensions::OnResolveAllConflicts),
			FCanExecuteAction::CreateRaw(this, &FGitSourceControlMenuExtensions::IsSourceControlConnected)
		)
	);

	//Get latest
	MenuBuilder.AddMenuEntry(
		LOCTEXT("GitCentralGetLatestLabel", "Get Latest..."),
		LOCTEXT("GitCentralGetLatestTooltip", "Update all unchanged files to the latest version."),
		FSlateIcon(FEditorStyle::GetStyleSetName(), "SourceControl.Actions.Sync"),
		FUIAction(
			FExecuteAction::CreateRaw(this, &FGitSourceControlMenuExtensions::OnGetLatest),
			FCanExecuteAction::CreateRaw(this, &FGitSourceControlMenuExtensions::IsSourceControlConnected)
		)
	);
}

void FGitSourceControlMenuExtensions::AddAssetContextMenuExtension(FMenuBuilder& MenuBuilder)
{
	//TODO : add revert option for only checked out file but not modified
	//TODO : add a sync menu for out of date, it will make things nicer and more obvious

	//Note: The regular asset context menu shows an option available even if only one asset in the selection has the option
	//The operations must filter which files they can operate on afterwards
	bool bAssetsConflicted = false;
	bool bAssetsLockedByOther = false;
	bool bCanForceWriteable = false;
	bool bAssetsCanBeLocked = false;

	for(const auto& AssetData : SavedSelectedAssets)
	{
		FSourceControlStatePtr SourceControlState = ISourceControlModule::Get().GetProvider().GetState(USourceControlHelpers::PackageFilename(AssetData.PackageName.ToString()), EStateCacheUsage::Use);
		if(SourceControlState.IsValid())
		{
			FGitSourceControlState* GitState = (FGitSourceControlState*)SourceControlState.Get();

			if(GitState->IsConflicted())
				bAssetsConflicted = true;

			if (GitState->IsCheckedOutOther())
				bAssetsLockedByOther = true;

			if (GitState->CanForceWriteable())
				bCanForceWriteable = true;

			if (GitState->CanFixLock())
				bAssetsCanBeLocked = true;

			if (bAssetsConflicted && bAssetsLockedByOther && bAssetsCanBeLocked)
				break;
		}
	}

	if (bAssetsConflicted)
	{
		//Resolve conflicts submenu
		MenuBuilder.AddSubMenu(
			LOCTEXT("GitCentralResolveSubMenuLabel", "Resolve Conflicts"),
			LOCTEXT("GitCentralResolveSubMenuTooltip", "Resolve Conflicts actions."),
			FNewMenuDelegate::CreateLambda([&](FMenuBuilder& MenuBuilder)
		{
			MenuBuilder.AddMenuEntry(
				LOCTEXT("GitCentralResolveMineLabel", "Resolve Using Yours"),
				LOCTEXT("GitCentralResolveMineTooltip", "Resolves the conflict by keeping your local changes."),
				FSlateIcon(FEditorStyle::GetStyleSetName(), "Subversion.CheckedOut_Small"),
				FUIAction(
					FExecuteAction::CreateRaw(this, &FGitSourceControlMenuExtensions::OnResolveYours),
					FCanExecuteAction()
				)
			);

			MenuBuilder.AddMenuEntry(
				LOCTEXT("GitCentralResolveTheirsLabel", "Resolve Using Theirs"),
				LOCTEXT("GitCentralResolveTheirsTooltip", "Resolves the conflict by reverting your changes and accepting the latest version."),
				FSlateIcon(FEditorStyle::GetStyleSetName(), "SourceControl.Actions.Sync"),
				FUIAction(
					FExecuteAction::CreateRaw(this, &FGitSourceControlMenuExtensions::OnResolveTheirs),
					FCanExecuteAction()
				)
			);
		}),
			FUIAction(),
			NAME_None,
			EUserInterfaceActionType::Button,
			false,
			FSlateIcon(FEditorStyle::GetStyleSetName(), "Subversion.NotInDepot_Small")
			);
	}

	FGitSourceControlModule& GitSourceControl = FGitSourceControlModule::GetInstance();
	const bool bIsAdmin = GitSourceControl.AccessSettings().IsAdmin();
	const bool bUseLocking = GitSourceControl.AccessSettings().IsUsingLocking();

	if (bUseLocking)
	{
		if (bAssetsCanBeLocked)
		{
			MenuBuilder.AddMenuEntry(
				LOCTEXT("GitCentralLockLabel", "Lock"),
				LOCTEXT("GitCentralLockTooltip", "Locks the files."),
				FSlateIcon(FEditorStyle::GetStyleSetName(), "PropertyWindow.Locked"),
				FUIAction(
					FExecuteAction::CreateRaw(this, &FGitSourceControlMenuExtensions::OnLock),
					FCanExecuteAction()
				)
			);
		}

		if (bCanForceWriteable)
		{
			MenuBuilder.AddMenuEntry(
				LOCTEXT("GitCentralForceWriteableLabel", "Force Writeable"),
				LOCTEXT("GitCentralForceWriteableTooltip", "Forces the file to be Writeable locally. You will not be able to submit the file while another user has the lock."),
				FSlateIcon(FEditorStyle::GetStyleSetName(), "Perforce.CheckedOutByOtherUserOtherBranch"),
				FUIAction(
					FExecuteAction::CreateRaw(this, &FGitSourceControlMenuExtensions::OnForceWriteable),
					FCanExecuteAction()
				)
			);
		}

		if (bIsAdmin && bAssetsLockedByOther)
		{
			MenuBuilder.AddMenuEntry(
				LOCTEXT("GitCentralForceUnlockLabel", "Force Unlock"),
				LOCTEXT("GitCentralForceUnlockTooltip", "Forces unlocking of the file. May require administrator permissions."),
				FSlateIcon(FEditorStyle::GetStyleSetName(), "PropertyWindow.Unlocked"),
				FUIAction(
					FExecuteAction::CreateRaw(this, &FGitSourceControlMenuExtensions::OnForceUnlock),
					FCanExecuteAction()
				)
			);
		}
	}
}

void FGitSourceControlMenuExtensions::OnResolveAllConflicts()
{
	//Inspired from FSourceControlWindows::ChoosePackagesToCheckIn
	if(ISourceControlModule::Get().IsEnabled())
	{
		if(ISourceControlModule::Get().GetProvider().IsAvailable())
		{
			TArray<FString> Filenames;
			Filenames.Add(FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir()));

			ISourceControlProvider& SourceControlProvider = ISourceControlModule::Get().GetProvider();
			FSourceControlOperationRef Operation = ISourceControlOperation::Create<FUpdateStatus>();
			StaticCastSharedRef<FUpdateStatus>(Operation)->SetCheckingAllFiles(false);
			SourceControlProvider.Execute(Operation, Filenames, EConcurrency::Asynchronous, FSourceControlOperationComplete::CreateRaw(this, &FGitSourceControlMenuExtensions::ChoosePackagesToResolveCallback));

			if(ChoosePackagesToResolveNotification.IsValid())
			{
				ChoosePackagesToResolveNotification.Pin()->ExpireAndFadeout();
			}

			FNotificationInfo Info(LOCTEXT("ChooseAssetsToCheckInIndicator", "Checking for assets to resolve..."));
			Info.bFireAndForget = false;
			Info.ExpireDuration = 0.0f;
			Info.FadeOutDuration = 1.0f;

			if(SourceControlProvider.CanCancelOperation(Operation))
			{
				Info.ButtonDetails.Add(FNotificationButtonInfo(
					LOCTEXT("ChoosePackagesToCheckIn_CancelButton", "Cancel"),
					LOCTEXT("ChoosePackagesToCheckIn_CancelButtonTooltip", "Cancel the resolve in operation."),
					FSimpleDelegate::CreateRaw(this, &FGitSourceControlMenuExtensions::ChoosePackagesToResolveCancelled, Operation)
				));
			}

			ChoosePackagesToResolveNotification = FSlateNotificationManager::Get().AddNotification(Info);

			if(ChoosePackagesToResolveNotification.IsValid())
			{
				ChoosePackagesToResolveNotification.Pin()->SetCompletionState(SNotificationItem::CS_Pending);
			}
		}
		else
		{
			FMessageLog EditorErrors("EditorErrors");
			EditorErrors.Warning(LOCTEXT("NoSCCConnection", "No connection to source control available!"))
				->AddToken(FDocumentationToken::Create(TEXT("Engine/UI/SourceControl")));
			EditorErrors.Notify();
		}
	}
}

void FGitSourceControlMenuExtensions::ChoosePackagesToResolveCallback(const FSourceControlOperationRef& InOperation, ECommandResult::Type InResult)
{
	if(ChoosePackagesToResolveNotification.IsValid())
	{
		ChoosePackagesToResolveNotification.Pin()->ExpireAndFadeout();
	}
	ChoosePackagesToResolveNotification.Reset();

	if(InResult == ECommandResult::Succeeded)
	{
		// Get a list of all the checked out packages
		TArray<FString> PackageNames;
		TArray<UPackage*> LoadedPackages;
		
		//FEditorFileUtils::FindAllSubmittablePackageFiles(PackageStates, true);
		ISourceControlProvider& SourceControlProvider = ISourceControlModule::Get().GetProvider();

		// Get file status of packages and config
		TArray<FSourceControlStateRef> ConflictingStates = SourceControlProvider.GetCachedStateByPredicate([](const FSourceControlStateRef& StateRef) {
			return StateRef->IsConflicted();
		});

		for(const auto& State : ConflictingStates)
		{
			PackageNames.Add(FPackageName::FilenameToLongPackageName(State->GetFilename()));
		}

		for(const auto &PackageName : PackageNames)
		{
			UPackage* Package = FindPackage(nullptr, *PackageName);
			if(Package != nullptr)
			{
				LoadedPackages.Add(Package);
			}
		}

		ChoosePackagesToResolveCompleted(LoadedPackages, PackageNames);
	}
	else if(InResult == ECommandResult::Failed)
	{
		FMessageLog EditorErrors("EditorErrors");
		EditorErrors.Warning(LOCTEXT("CheckInOperationFailed", "Failed checking source control status!"));
		EditorErrors.Notify();
	}
}

void FGitSourceControlMenuExtensions::ChoosePackagesToResolveCompleted(const TArray<UPackage*>& LoadedPackages, const TArray<FString>& PackageNames)
{
	if(ChoosePackagesToResolveNotification.IsValid())
	{
		ChoosePackagesToResolveNotification.Pin()->ExpireAndFadeout();
	}
	ChoosePackagesToResolveNotification.Reset();

	// Prompt the user to ask if they would like to first save any dirty packages they are trying to check-in
	const FEditorFileUtils::EPromptReturnCode UserResponse = FEditorFileUtils::PromptForCheckoutAndSave(LoadedPackages, true, true);

	// If the user elected to save dirty packages, but one or more of the packages failed to save properly OR if the user
	// canceled out of the prompt, don't follow through on the check-in process
	const bool bShouldProceed = (UserResponse == FEditorFileUtils::EPromptReturnCode::PR_Success || UserResponse == FEditorFileUtils::EPromptReturnCode::PR_Declined);
	if(bShouldProceed)
	{
		PromptForResolve(PackageNames);
	}
	else
	{
		// If a failure occurred, alert the user that the check-in was aborted. This warning shouldn't be necessary if the user cancelled
		// from the dialog, because they obviously intended to cancel the whole operation.
		if(UserResponse == FEditorFileUtils::EPromptReturnCode::PR_Failure)
		{
			FMessageDialog::Open(EAppMsgType::Ok, NSLOCTEXT("GitCentral", "GitCentral_Resolve_Aborted", "Resolve aborted as a result of save failure."));
		}
	}
}

void FGitSourceControlMenuExtensions::ChoosePackagesToResolveCancelled(FSourceControlOperationRef InOperation)
{
	ISourceControlProvider& SourceControlProvider = ISourceControlModule::Get().GetProvider();
	SourceControlProvider.CancelOperation(InOperation);

	if(ChoosePackagesToResolveNotification.IsValid())
	{
		ChoosePackagesToResolveNotification.Pin()->ExpireAndFadeout();
	}
	ChoosePackagesToResolveNotification.Reset();
}

void FGitSourceControlMenuExtensions::PromptForResolve(const TArray<FString>& InPackageNames)
{
	ISourceControlProvider& SourceControlProvider = ISourceControlModule::Get().GetProvider();

	bool bCheckInSuccess = true;

	// Get filenames for packages and config to be resolved
	const TArray<FString> AllFiles = USourceControlHelpers::PackageFilenames(InPackageNames);

	// Get file status of packages and config
	TArray<FSourceControlStateRef> States;
	SourceControlProvider.GetState(AllFiles, States, EStateCacheUsage::Use);

	if(States.Num())
	{
		TSharedRef<SWindow> NewWindow = SNew(SWindow)
			.Title(NSLOCTEXT("SourceControl.ResolveWindow", "Title", "Resolve Files"))
			.SizingRule(ESizingRule::UserSized)
			.ClientSize(FVector2D(600, 400))
			.SupportsMaximize(true)
			.SupportsMinimize(false);

		TSharedRef<SSourceControlResolveWidget> SourceControlWidget =
			SNew(SSourceControlResolveWidget)
			.ParentWindow(NewWindow)
			.Items(States);

		NewWindow->SetContent(
			SourceControlWidget
		);

		FSlateApplication::Get().AddModalWindow(NewWindow, NULL);

		if(SourceControlWidget->GetResult() == EResolveResults::RESOLVE_ACCEPTED)
		{
			{
				TArray<FString> FilenamesYours;
				SourceControlWidget->GetFilenamesForResolveYours(FilenamesYours);
				if(FilenamesYours.Num())
				{
					TArray<FString> PackageNames;
					for(const auto& File : FilenamesYours)
					{
						PackageNames.Add(FPackageName::FilenameToLongPackageName(File));
					}

					ExecuteResolveYours(PackageNames);
				}
			}

			{
				TArray<FString> FilenamesTheirs;
				SourceControlWidget->GetFilenamesForResolveTheirs(FilenamesTheirs);
				if(FilenamesTheirs.Num())
				{
					TArray<FString> PackageNames;
					for(const auto& File : FilenamesTheirs)
					{
						PackageNames.Add(FPackageName::FilenameToLongPackageName(File));
					}

					ExecuteResolveTheirs(PackageNames);
				}
			}
		}
	}
	else
	{
		FMessageLog EditorErrors("EditorErrors");
		EditorErrors.Warning(LOCTEXT("NoAssetsToResolve", "No assets to Resolve !"));
		EditorErrors.Notify();
	}
}

void FGitSourceControlMenuExtensions::OnResolveYours()
{
	TArray<FString> PackageNames;
	for(const auto& Asset : SavedSelectedAssets)
	{
		UPackage* Package = Asset.GetPackage();
		PackageNames.Add(Package->GetName());
	}

	ExecuteResolveYours(PackageNames);
}

void FGitSourceControlMenuExtensions::ExecuteResolveYours(const TArray<FString>& PackageNames)
{
	const TArray<FString> PackageFilenames = USourceControlHelpers::PackageFilenames(PackageNames);

	//Here just call the resolve command which will accept ours
	ISourceControlProvider& SCCProvider = ISourceControlModule::Get().GetProvider();
	SCCProvider.Execute(ISourceControlOperation::Create<FResolve>(), PackageFilenames);

	//No need to reload packages as nothing has changed
	//Deleted packages will also work properly
}

void FGitSourceControlMenuExtensions::OnResolveTheirs()
{
	TArray<FString> PackageNames;
	for(const auto& Asset : SavedSelectedAssets)
	{
		UPackage* Package = Asset.GetPackage();
		PackageNames.Add(Package->GetName());
	}

	ExecuteResolveTheirs(PackageNames);
}

void FGitSourceControlMenuExtensions::ExecuteResolveTheirs(const TArray<FString>& PackageNames)
{
	//This should call ContentBrowserUtils::SyncPackagesFromSourceControl but it is private so let's emulate it ourselves

	//TODO: handle dependencies here!
	// Warn about any packages that are being synced without also getting the newest version of their dependencies...
	/*TArray<FString> PackageNamesToSync = PackageNames;
	{
		TArray<FString> OutOfDateDependencies;
		GetOutOfDatePackageDependencies(PackageNamesToSync, OutOfDateDependencies);

		TArray<FString> ExtraPackagesToSync;
		ShowSyncDependenciesDialog(OutOfDateDependencies, ExtraPackagesToSync);

		PackageNamesToSync.Append(ExtraPackagesToSync);
	}*/

	const TArray<FString> PackageFilenames = SourceControlHelpers::PackageFilenames(PackageNames);
	TArray<UPackage*> PackagesToReload = PreparePackagesForReload(PackageNames);

	if(PackageFilenames.Num())
	{
		ISourceControlProvider& SCCProvider = ISourceControlModule::Get().GetProvider();
		SCCProvider.Execute(ISourceControlOperation::Create<FSync>(), PackageFilenames);
	}

	ReloadPackages(PackagesToReload);
}

bool FGitSourceControlMenuExtensions::IsSourceControlConnected()
{
	const auto& Provider = ISourceControlModule::Get().GetProvider();
	return Provider.IsEnabled() && Provider.IsAvailable();
}

TArray<UPackage*> FGitSourceControlMenuExtensions::PreparePackagesForReload(const TArray<FString>& PackageNames)
{
	//Inspired from ContentBrowserUtils::SyncPackagesFromSourceControl and SyncPathsFromSourceControl 
	//also :https://github.com/SRombauts/UE4PlasticPlugin/blob/master/Source/PlasticSourceControl/Private/PlasticSourceControlMenu.cpp#L60

	TArray<UPackage*> LoadedPackages;
	//Inspired from ContentBrowserUtils.cpp and PackageRestore.cpp
	if (PackageNames.Num() == 0)
		return LoadedPackages;


	// Form a list of loaded packages to reload...
	LoadedPackages.Reserve(PackageNames.Num());
	for (const FString& PackageName : PackageNames)
	{
		UPackage* Package = FindPackage(nullptr, *PackageName);
		if (Package)
		{
			LoadedPackages.Emplace(Package);

			// Detach the linkers of any loaded packages so that SCC can overwrite the files...
			if (!Package->IsFullyLoaded())
			{
				FlushAsyncLoading();
				Package->FullyLoad();
			}
			ResetLoaders(Package);
		}
	}

	return LoadedPackages;
}

bool FGitSourceControlMenuExtensions::ReloadPackages(const TArray<FString>& PackageFilenamesToReload)
{
	TArray<UPackage*> PackagesToReload;
	PackagesToReload.Reserve(PackageFilenamesToReload.Num());
	for (const FString& PackageFilename : PackageFilenamesToReload)
	{
		FString PackageName;
		FString FailureReason;
		if (FPackageName::TryConvertFilenameToLongPackageName(PackageFilename, PackageName, &FailureReason))
		{
			UPackage* Package = FindPackage(nullptr, *PackageName);
			if (Package)
			{
				PackagesToReload.Emplace(Package);
			}
		}
		else
		{
			GITCENTRAL_ERROR(TEXT("%s"), *FailureReason);
		}
	}

	return ReloadPackages(PackagesToReload);
}

bool FGitSourceControlMenuExtensions::ReloadPackages(TArray<UPackage*>& PackagesToReload)
{
	if (PackagesToReload.Num() == 0)
		return true;

	// Some packages may have been deleted, so we need to unload those rather than re-load them...
	TArray<UPackage*> PackagesToUnload;
	PackagesToReload.RemoveAll([&](UPackage* InPackage) -> bool
	{
		const FString PackageExtension = InPackage->ContainsMap() ? FPackageName::GetMapPackageExtension() : FPackageName::GetAssetPackageExtension();
		const FString PackageFilename = FPackageName::LongPackageNameToFilename(InPackage->GetName(), PackageExtension);
		if (!FPaths::FileExists(PackageFilename))
		{
			PackagesToUnload.Emplace(InPackage);
			return true; // remove package
		}
		return false; // keep package
	});

	// Hot-reload the new packages...
	UPackageTools::ReloadPackages(PackagesToReload);

	// Unload any deleted packages...
	UPackageTools::UnloadPackages(PackagesToUnload);

	return true;
}

void FGitSourceControlMenuExtensions::OnGetLatest()
{
	//This should call ContentBrowserUtils::SyncPackagesFromSourceControl but it is private so let's emulate it ourselves

	//Prompt to save or discard all packages
	bool bOkToExit = false;
	bool bHadPackagesToSave = false;
	{
		const bool bPromptUserToSave = true;
		const bool bSaveMapPackages = true;
		const bool bSaveContentPackages = true;
		const bool bFastSave = false;
		const bool bNotifyNoPackagesSaved = false;
		const bool bCanBeDeclined = true; //If the user clicks "don't save" this will continue and lose their changes
		bOkToExit = FEditorFileUtils::SaveDirtyPackages(bPromptUserToSave, bSaveMapPackages, bSaveContentPackages, bFastSave, bNotifyNoPackagesSaved, bCanBeDeclined, &bHadPackagesToSave);
	}

	//bOkToExit can be true if the user selects to not save an asset by unchecking it and clicking "save"
	//Check again that all packages were saved properly
	if(bOkToExit)
	{
		TArray<UPackage*> DirtyPackages;
		FEditorFileUtils::GetDirtyWorldPackages(DirtyPackages);
		FEditorFileUtils::GetDirtyContentPackages(DirtyPackages);
		bOkToExit = DirtyPackages.Num() == 0;
	}

	if(!bOkToExit)
	{
		FMessageLog EditorErrors("EditorErrors");
		EditorErrors.Warning(LOCTEXT("GetLatestUnsaved", "Save All Assets before attempting to Get Latest !"));
		EditorErrors.Notify();
		return;
	}

	//TODO: we could check if there is update available before even attempting to get latest

	FGitSourceControlModule& Module = FGitSourceControlModule::GetInstance();
	FGitSourceControlProvider& Provider = Module.GetProvider();

	//Find all packages in repository
	TArray<FString> PackageRelativePaths;
	FPackageName::FindPackagesInDirectory(PackageRelativePaths, *Provider.GetPathToRepositoryRoot());

	TArray<FString> PackageNames;
	PackageNames.Reserve(PackageRelativePaths.Num());
	for (const auto& Path : PackageRelativePaths)
	{
		FString PackageName;
		FString FailureReason;
		if (FPackageName::TryConvertFilenameToLongPackageName(Path, PackageName, &FailureReason))
		{
			PackageNames.Add(PackageName);
		}
		else
		{
			GITCENTRAL_ERROR(TEXT("%s"), *FailureReason);
		}
	}

	//Note: don't worry about dependencies as we are syncing everything anyway

	TArray<UPackage*> PackagesToReload = PreparePackagesForReload(PackageNames);

	//get latest
	auto Result = Provider.Execute(ISourceControlOperation::Create<FSync>(), { Provider.GetPathToRepositoryRoot() }, EConcurrency::Synchronous);
	if (Result == ECommandResult::Succeeded)
	{
		//Note: Reloading assets takes an extremely long time on large projects
		//Options are given to the user to skip this step, but they must be warned of the consequences.
		//If the reload strategy doesn't work, we can revert back to the original editor restart strategy²
		//FUnrealEdMisc::Get().RestartEditor(false); //Note: If we have unsaved packages this will prompt for save again

		if(Provider.GetLastSyncOperationUpdatedFiles().Num() > 0) //Files were synced
		{
			FSuppressableWarningDialog::FSetupInfo Info(
				LOCTEXT("GetLatest_ReloadAssetsText", "Reload updated assets? \nAssets have been updated, you must reload to see the update in the editor.\n\nWARNING: If you do not reload, it is strongly recommended to restart the editor, else you may overwrite updated assets without noticing."),
				LOCTEXT("GetLatest_ReloadAssetsTitle", "Reload after Getting Latest"),
				TEXT("GetLatest_ReloadAssetsEditorWarning"));
			Info.ConfirmText = LOCTEXT("Yes", "Yes");
			Info.CancelText = LOCTEXT("No", "No");
			Info.bDefaultToSuppressInTheFuture = false;

			//Must provide full path here, not necessary for now
			//Info.IniSettingFileName = TEXT("SourceControlSettings");

			FSuppressableWarningDialog ReloadAssetsWarningDialog(Info);
			auto ConfirmRestart = ReloadAssetsWarningDialog.ShowModal();
			if (ConfirmRestart == FSuppressableWarningDialog::EResult::Confirm || ConfirmRestart == FSuppressableWarningDialog::EResult::Suppressed)
			{
				//Try to reload the packages if not restarting the editor
				ReloadPackages(Provider.GetLastSyncOperationUpdatedFiles());
			}
		}
	}
	else
	{
		FMessageLog EditorErrors("EditorErrors");
		EditorErrors.Warning(LOCTEXT("GetLatestFailed", "Failed to Get Latest, check the logs for errors !"));
		EditorErrors.Notify();
	}
}

void FGitSourceControlMenuExtensions::OnLock()
{
	TArray<FString> PackageNames;
	for (const auto& Asset : SavedSelectedAssets)
	{
		UPackage* Package = Asset.GetPackage();
		PackageNames.Add(Package->GetName());
	}

	const TArray<FString> PackageFilenames = USourceControlHelpers::PackageFilenames(PackageNames);

	ISourceControlProvider& SCCProvider = ISourceControlModule::Get().GetProvider();
	SCCProvider.Execute(ISourceControlOperation::Create<FCheckOut>(), PackageFilenames);
}

void FGitSourceControlMenuExtensions::OnForceUnlock()
{
	TArray<FString> PackageNames;
	for (const auto& Asset : SavedSelectedAssets)
	{
		UPackage* Package = Asset.GetPackage();
		PackageNames.Add(Package->GetName());
	}

	const TArray<FString> PackageFilenames = USourceControlHelpers::PackageFilenames(PackageNames);

	ISourceControlProvider& SCCProvider = ISourceControlModule::Get().GetProvider();
	SCCProvider.Execute(ISourceControlOperation::Create<FForceUnlock>(), PackageFilenames);
}

void FGitSourceControlMenuExtensions::OnForceWriteable()
{
	TArray<FString> PackageNames;
	for (const auto& Asset : SavedSelectedAssets)
	{
		UPackage* Package = Asset.GetPackage();
		PackageNames.Add(Package->GetName());
	}

	const TArray<FString> PackageFilenames = USourceControlHelpers::PackageFilenames(PackageNames);

	ISourceControlProvider& SCCProvider = ISourceControlModule::Get().GetProvider();
	SCCProvider.Execute(ISourceControlOperation::Create<FForceWriteable>(), PackageFilenames);
}

#undef LOCTEXT_NAMESPACE