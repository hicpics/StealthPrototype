// Copyright (c) 2017-2020 Samuel Kahn (samuel@kahncode.com). All Rights Reserved.

#include "SGitSourceControlSettings.h"
#include "GitSourceControlModule.h"
#include "GitSourceControlUtils.h"

#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Text/STextBlock.h"
#include "EditorStyleSet.h"


#define LOCTEXT_NAMESPACE "SGitSourceControlSettings"

void SGitSourceControlSettings::Construct(const FArguments& InArgs)
{
	FSlateFontInfo Font = FEditorStyle::GetFontStyle(TEXT("SourceControl.LoginWindow.Font"));

	bAutoCreateGitIgnore = true;
	bAutoInitialCommit = true;

	InitialCommitMessage = LOCTEXT("InitialCommitMessage", "Initial commit");

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage( FEditorStyle::GetBrush("DetailsView.CategoryBottom"))
		.Padding(FMargin(0.0f, 3.0f, 0.0f, 0.0f))
		[
			SNew(SVerticalBox)
			+SVerticalBox::Slot()
			.AutoHeight()
			.Padding(2.0f)
			.VAlign(VAlign_Center)
			[
				SNew(SHorizontalBox)
				+SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("BinaryPathLabel", "Git Path"))
					.ToolTipText(LOCTEXT("BinaryPathLabel_Tooltip", "Path to Git binary"))
					.Font(Font)
				]
				+SHorizontalBox::Slot()
				.FillWidth(2.0f)
				[
					SNew(SEditableTextBox)
					.Text(this, &SGitSourceControlSettings::GetBinaryPathText)
					.ToolTipText(LOCTEXT("BinaryPathLabel_Tooltip", "Path to Git binary"))
					.OnTextCommitted(this, &SGitSourceControlSettings::OnBinaryPathTextCommited)
					.Font(Font)
				]
			]
			+SVerticalBox::Slot()
			.AutoHeight()
			.Padding(2.0f)
			.VAlign(VAlign_Center)
			[
				SNew(SHorizontalBox)
				+SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("RepositoryRootLabel", "Root of the repository"))
					.ToolTipText(LOCTEXT("RepositoryRootLabel_Tooltip", "Path to the root of the Git repository"))
					.Font(Font)
				]
				+SHorizontalBox::Slot()
				.FillWidth(2.0f)
				[
					SNew(SEditableTextBox)
					.Text(this, &SGitSourceControlSettings::GetPathToRepositoryRoot)
					.ToolTipText(LOCTEXT("RepositoryRootLabel_Tooltip", "Path to the root of the Git repository"))
					.OnTextCommitted(this, &SGitSourceControlSettings::OnPathToRepositoryRootCommited)
					.Font(Font)
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(2.0f)
			.VAlign(VAlign_Center)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("BranchLabel", "Branch"))
					.ToolTipText(LOCTEXT("BranchLabel_Tooltip", "Active branch to use"))
					.Font(Font)
				]
				+ SHorizontalBox::Slot()
				.FillWidth(2.0f)
				[
					SNew(SEditableTextBox)
					.Text(this, &SGitSourceControlSettings::GetBranch)
					.ToolTipText(LOCTEXT("BranchLabel_Tooltip", "Active branch to use"))
					.OnTextCommitted(this, &SGitSourceControlSettings::OnBranchCommited)
					.Font(Font)
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(2.0f)
			.VAlign(VAlign_Center)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("RemoteLabel", "Remote"))
					.ToolTipText(LOCTEXT("RemoteLabel_Tooltip", "Name of the remote to use as centralized server"))
					.Font(Font)
				]
				+ SHorizontalBox::Slot()
				.FillWidth(2.0f)
				[
					SNew(SEditableTextBox)
					.Text(this, &SGitSourceControlSettings::GetRemote)
					.ToolTipText(LOCTEXT("RemoteLabel_Tooltip", "Name of the remote to use as centralized server"))
					.OnTextCommitted(this, &SGitSourceControlSettings::OnRemoteCommited)
					.Font(Font)
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(2.0f)
			.VAlign(VAlign_Center)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("UseLockingLabel", "Use Locking"))
					.ToolTipText(LOCTEXT("UseLockingLabel_Tooltip", "Use the lock feature of Git LFS"))
					.Font(Font)
				]
				+ SHorizontalBox::Slot()
				.FillWidth(2.0f)
				[
					SNew(SCheckBox)
					.IsChecked(this, &SGitSourceControlSettings::IsUsingLocking)
					.OnCheckStateChanged(this, &SGitSourceControlSettings::OnCheckLocking)
					.ToolTipText(LOCTEXT("UseLockingLabel_Tooltip", "Use the lock feature of Git LFS"))
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(2.0f)
			.VAlign(VAlign_Center)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("IsAdminLabel", "Admin Access"))
					.ToolTipText(LOCTEXT("IsAdminLabel_Tooltip", "Allows the use of admin commands, requires admin access to the remote repository"))
					.Font(Font)
				]
				+ SHorizontalBox::Slot()
				.FillWidth(2.0f)
				[
					SNew(SCheckBox)
					.IsChecked(this, &SGitSourceControlSettings::IsAdmin)
					.OnCheckStateChanged(this, &SGitSourceControlSettings::OnCheckAdmin)
					.ToolTipText(LOCTEXT("IsAdminLabel_Tooltip", "Allows the use of admin commands, requires admin access to the remote repository"))
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(2.0f)
			.VAlign(VAlign_Center)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("LockingUsernameLabel", "Locks username (optional)"))
					.ToolTipText(LOCTEXT("LockingUsernameLabel_Tooltip", "Fill this if your git username (git config user.name) does not match the locks username (git lfs locks). Input the locks username here to correctly detect the files you have locked"))
					.Font(Font)
				]
				+ SHorizontalBox::Slot()
				.FillWidth(2.0f)
				[
					SNew(SEditableTextBox)
					.Text(this, &SGitSourceControlSettings::GetLockingUsername)
					.ToolTipText(LOCTEXT("LockingUsernameLabel_Tooltip", "Fill this if your git username (git config user.name) does not match the locks username (git lfs locks). Input the locks username here to correctly detect the files you have locked"))
					.OnTextCommitted(this, &SGitSourceControlSettings::OnLockingUsernameCommited)
					.Font(Font)
				]
			]
			+SVerticalBox::Slot()
			.FillHeight(1.0f)
			.Padding(2.0f)
			.VAlign(VAlign_Center)
			[
				SNew(SHorizontalBox)
				+SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("GitUserName", "User Name"))
					.ToolTipText(LOCTEXT("GitUserName_Tooltip", "User name configured for the Git repository"))
					.Font(Font)
				]
				+SHorizontalBox::Slot()
				.FillWidth(2.0f)
				[
					SNew(STextBlock)
					.Text(this, &SGitSourceControlSettings::GetUserName)
					.ToolTipText(LOCTEXT("GitUserName_Tooltip", "User name configured for the Git repository"))
					.Font(Font)
				]
			]
			+SVerticalBox::Slot()
			.FillHeight(1.0f)
			.Padding(2.0f)
			.VAlign(VAlign_Center)
			[
				SNew(SHorizontalBox)
				+SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("GitUserEmail", "E-Mail"))
					.ToolTipText(LOCTEXT("GitUserEmail_Tooltip", "User e-mail configured for the Git repository"))
					.Font(Font)
				]
				+SHorizontalBox::Slot()
				.FillWidth(2.0f)
				[
					SNew(STextBlock)
					.Text(this, &SGitSourceControlSettings::GetUserEmail)
					.ToolTipText(LOCTEXT("GitUserEmail_Tooltip", "User e-mail configured for the Git repository"))
					.Font(Font)
				]
			]
			/*+SVerticalBox::Slot()
			.AutoHeight()
			.Padding(2.0f)
			.VAlign(VAlign_Center)
			[
				SNew(SSeparator)
				.Visibility(this, &SGitSourceControlSettings::CanInitializeGitRepository)
			]*/
			//TODO : handle git repository initialization
		]
	];
}

FText SGitSourceControlSettings::GetBinaryPathText() const
{
	FGitSourceControlModule& GitSourceControl = FGitSourceControlModule::GetInstance();
	return FText::FromString(GitSourceControl.AccessSettings().GetBinaryPath());
}

void SGitSourceControlSettings::OnBinaryPathTextCommited(const FText& InText, ETextCommit::Type InCommitType) const
{
	FGitSourceControlModule& GitSourceControl = FGitSourceControlModule::GetInstance();

	if(InText.IsEmpty())
	{
		GitSourceControl.AccessSettings().SetBinaryPath(GitSourceControlUtils::FindGitBinaryPath());
	}
	else
	{
		GitSourceControl.AccessSettings().SetBinaryPath(InText.ToString());
	}

	GitSourceControl.SaveSettings();
}

void SGitSourceControlSettings::OnPathToRepositoryRootCommited(const FText& InText, ETextCommit::Type InCommitType) const
{
	FGitSourceControlModule& GitSourceControl = FGitSourceControlModule::GetInstance();
	GitSourceControl.AccessSettings().SetRootPath(InText.ToString());
	GitSourceControl.SaveSettings();
	if(InText.IsEmpty())
		GitSourceControl.AccessSettings().SetRootPath(GitSourceControl.GetProvider().GetPathToRepositoryRoot());
}

void SGitSourceControlSettings::OnBranchCommited(const FText& InText, ETextCommit::Type InCommitType) const
{
	//TODO : if branch exists it will require a reload of all packages (or restart of the editor?)
	FGitSourceControlModule& GitSourceControl = FGitSourceControlModule::GetInstance();

	GitSourceControl.AccessSettings().SetBranch(InText.ToString());
	GitSourceControl.SaveSettings();
	if(InText.IsEmpty())
		GitSourceControl.AccessSettings().SetBranch(GitSourceControl.GetProvider().GetBranch());
}

void SGitSourceControlSettings::OnRemoteCommited(const FText& InText, ETextCommit::Type InCommitType) const
{
	FGitSourceControlModule& GitSourceControl = FGitSourceControlModule::GetInstance();
	GitSourceControl.AccessSettings().SetRemote(InText.ToString());
	GitSourceControl.SaveSettings();
	if(InText.IsEmpty())
		GitSourceControl.AccessSettings().SetRemote(GitSourceControl.GetProvider().GetRemote());
}

void SGitSourceControlSettings::OnLockingUsernameCommited(const FText& InText, ETextCommit::Type InCommitType) const
{
	FGitSourceControlModule& GitSourceControl = FGitSourceControlModule::GetInstance();

	GitSourceControl.AccessSettings().SetLockingUsername(InText.ToString());
	GitSourceControl.SaveSettings();
}

void SGitSourceControlSettings::OnCheckAdmin(ECheckBoxState NewCheckedState)
{
	FGitSourceControlModule& GitSourceControl = FGitSourceControlModule::GetInstance();
	GitSourceControl.AccessSettings().SetIsAdmin(NewCheckedState == ECheckBoxState::Checked);
	GitSourceControl.SaveSettings();
}

void SGitSourceControlSettings::OnCheckLocking(ECheckBoxState NewCheckedState)
{
	FGitSourceControlModule& GitSourceControl = FGitSourceControlModule::GetInstance();
	GitSourceControl.AccessSettings().SetUseLocking(NewCheckedState == ECheckBoxState::Checked);
	GitSourceControl.SaveSettings();
}

FText SGitSourceControlSettings::GetPathToRepositoryRoot() const
{
	FGitSourceControlModule& GitSourceControl = FGitSourceControlModule::GetInstance();
	if(GitSourceControl.AccessSettings().GetRootPath().IsEmpty())
		return FText::FromString(GitSourceControl.GetProvider().GetPathToRepositoryRoot());
	else
		return FText::FromString(GitSourceControl.AccessSettings().GetRootPath());
}

FText SGitSourceControlSettings::GetUserName() const
{
	FGitSourceControlModule& GitSourceControl = FGitSourceControlModule::GetInstance();
	return FText::FromString(GitSourceControl.GetProvider().GetUserName());
}

FText SGitSourceControlSettings::GetUserEmail() const
{
	FGitSourceControlModule& GitSourceControl = FGitSourceControlModule::GetInstance();
	return FText::FromString(GitSourceControl.GetProvider().GetUserEmail());
}

FText SGitSourceControlSettings::GetBranch() const
{
	FGitSourceControlModule& GitSourceControl = FGitSourceControlModule::GetInstance();
	if(GitSourceControl.AccessSettings().GetBranch().IsEmpty())
		return FText::FromString(GitSourceControl.GetProvider().GetBranch());
	else
		return FText::FromString(GitSourceControl.AccessSettings().GetBranch());
}

FText SGitSourceControlSettings::GetRemote() const
{
	FGitSourceControlModule& GitSourceControl = FGitSourceControlModule::GetInstance();
	if(GitSourceControl.AccessSettings().GetRemote().IsEmpty())
		return FText::FromString(GitSourceControl.GetProvider().GetRemote());
	else
		return FText::FromString(GitSourceControl.AccessSettings().GetRemote());
}

ECheckBoxState SGitSourceControlSettings::IsUsingLocking() const
{
	FGitSourceControlModule& GitSourceControl = FGitSourceControlModule::GetInstance();
	return GitSourceControl.AccessSettings().IsUsingLocking() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

ECheckBoxState SGitSourceControlSettings::IsAdmin() const
{
	FGitSourceControlModule& GitSourceControl = FGitSourceControlModule::GetInstance();
	return GitSourceControl.AccessSettings().IsAdmin() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

FText SGitSourceControlSettings::GetLockingUsername() const
{
	FGitSourceControlModule& GitSourceControl = FGitSourceControlModule::GetInstance();
	return FText::FromString(GitSourceControl.AccessSettings().GetLockingUsername());
}

EVisibility SGitSourceControlSettings::CanInitializeGitRepository() const
{
	FGitSourceControlModule& GitSourceControl = FGitSourceControlModule::GetInstance();
	const bool bGitAvailable = GitSourceControl.GetProvider().IsGitAvailable();
	const bool bGitRepositoryFound = GitSourceControl.GetProvider().IsEnabled();
	return (bGitAvailable && !bGitRepositoryFound) ? EVisibility::Visible : EVisibility::Collapsed;
}

FReply SGitSourceControlSettings::OnClickedInitializeGitRepository()
{
	FGitSourceControlModule& GitSourceControl = FGitSourceControlModule::GetInstance();
	TArray<FString> InfoMessages;
	TArray<FString> ErrorMessages;
	const FString& PathToGitBinary = GitSourceControl.AccessSettings().GetBinaryPath();
	const FString PathToGameDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	GitSourceControlUtils::RunCommand(TEXT("init"), PathToGitBinary, PathToGameDir, TArray<FString>(), TArray<FString>(), InfoMessages, ErrorMessages);
	// Check the new repository status to enable connection
	GitSourceControl.GetProvider().CheckGitAvailability();
	if(GitSourceControl.GetProvider().IsEnabled())
	{
		TArray<FString> ProjectFiles;
		ProjectFiles.Add(FPaths::GetCleanFilename(FPaths::GetProjectFilePath()));
		ProjectFiles.Add(FPaths::GetCleanFilename(FPaths::ProjectConfigDir()));
		ProjectFiles.Add(FPaths::GetCleanFilename(FPaths::ProjectContentDir()));
		if(FPaths::DirectoryExists(FPaths::GameSourceDir()))
		{
			ProjectFiles.Add(FPaths::GetCleanFilename(FPaths::GameSourceDir()));
		}
		if(bAutoCreateGitIgnore)
		{
			// Create a standard ".gitignore" file with common patterns for a typical Blueprint & C++ project
			const FString Filename = FPaths::Combine(*PathToGameDir, TEXT(".gitignore"));
			const FString GitIgnoreContent = TEXT("Binaries\nDerivedDataCache\nIntermediate\nSaved\n*.VC.db\n*.opensdf\n*.opendb\n*.sdf\n*.sln\n*.suo\n*.xcodeproj\n*.xcworkspace");
			if(FFileHelper::SaveStringToFile(GitIgnoreContent, *Filename, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
			{
				ProjectFiles.Add(TEXT(".gitignore"));
			}
		}
		// Add .uproject, Config/, Content/ and Source/ files (and .gitignore if any)
		GitSourceControlUtils::RunCommand(TEXT("add"), PathToGitBinary, PathToGameDir, TArray<FString>(), ProjectFiles, InfoMessages, ErrorMessages);
		if(bAutoInitialCommit)
		{
			// optionnal initial git commit with custom message
			TArray<FString> Parameters;
			FString ParamCommitMsg = TEXT("--message=\"");
			ParamCommitMsg += InitialCommitMessage.ToString();
			ParamCommitMsg += TEXT("\"");
			Parameters.Add(ParamCommitMsg);
			GitSourceControlUtils::RunCommit(PathToGitBinary, PathToGameDir, Parameters, TArray<FString>(), InfoMessages, ErrorMessages);
		}
	}
	return FReply::Handled();
}

void SGitSourceControlSettings::OnCheckedCreateGitIgnore(ECheckBoxState NewCheckedState)
{
	bAutoCreateGitIgnore = (NewCheckedState == ECheckBoxState::Checked);
}

void SGitSourceControlSettings::OnCheckedInitialCommit(ECheckBoxState NewCheckedState)
{
	bAutoInitialCommit = (NewCheckedState == ECheckBoxState::Checked);
}

void SGitSourceControlSettings::OnInitialCommitMessageCommited(const FText& InText, ETextCommit::Type InCommitType)
{
	InitialCommitMessage = InText;
}

FText SGitSourceControlSettings::GetInitialCommitMessage() const
{
	return InitialCommitMessage;
}

#undef LOCTEXT_NAMESPACE
