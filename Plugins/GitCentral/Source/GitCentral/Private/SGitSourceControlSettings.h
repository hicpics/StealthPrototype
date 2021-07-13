// Copyright (c) 2017-2020 Samuel Kahn (samuel@kahncode.com). All Rights Reserved.
#pragma once

#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

enum class ECheckBoxState : uint8;

class SGitSourceControlSettings : public SCompoundWidget
{
public:
	
	SLATE_BEGIN_ARGS(SGitSourceControlSettings) {}
	
	SLATE_END_ARGS()

public:

	void Construct(const FArguments& InArgs);

private:

	/** Delegate to get binary path from settings */
	FText GetBinaryPathText() const;

	/** Delegate to commit repository text to settings */
	void OnBinaryPathTextCommited(const FText& InText, ETextCommit::Type InCommitType) const;

	/** Delegate to commit repository root path to settings */
	void OnPathToRepositoryRootCommited(const FText& InText, ETextCommit::Type InCommitType) const;

	/** Delegate to commit branch settings */
	void OnBranchCommited(const FText& InText, ETextCommit::Type InCommitType) const;

	/** Delegate to commit remote settings */
	void OnRemoteCommited(const FText& InText, ETextCommit::Type InCommitType) const;

	/** Delegate to commit remote settings */
	void OnLockingUsernameCommited(const FText& InText, ETextCommit::Type InCommitType) const;

	void OnCheckAdmin(ECheckBoxState NewCheckedState);
	void OnCheckLocking(ECheckBoxState NewCheckedState);

	/** Delegate to get repository root, user name and email from provider */
	FText GetPathToRepositoryRoot() const;
	FText GetUserName() const;
	FText GetUserEmail() const;
	FText GetBranch() const;
	FText GetRemote() const;
	ECheckBoxState IsUsingLocking() const;
	ECheckBoxState IsAdmin() const;
	FText GetLockingUsername() const;

	EVisibility CanInitializeGitRepository() const;
	FReply OnClickedInitializeGitRepository();

	void OnCheckedCreateGitIgnore(ECheckBoxState NewCheckedState);
	bool bAutoCreateGitIgnore;

	void OnCheckedInitialCommit(ECheckBoxState NewCheckedState);
	bool bAutoInitialCommit;
	void OnInitialCommitMessageCommited(const FText& InText, ETextCommit::Type InCommitType);
	FText GetInitialCommitMessage() const;
	FText InitialCommitMessage;
};