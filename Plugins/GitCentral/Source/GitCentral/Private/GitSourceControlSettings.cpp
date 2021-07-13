// Copyright (c) 2017-2020 Samuel Kahn (samuel@kahncode.com). All Rights Reserved.

#include "GitSourceControlSettings.h"
#include "GitSourceControlModule.h"
#include "GitSourceControlProvider.h"
#include "GitSourceControlUtils.h"
#include "SourceControlHelpers.h"

namespace GitSettingsConstants
{

/** The section of the ini file we load our settings from */
static const FString SettingsSection = TEXT("GitCentral.Settings");

}

const FString& FGitSourceControlSettings::GetBinaryPath() const
{
	FScopeLock ScopeLock(&CriticalSection);
	return BinaryPath;
}

const FString& FGitSourceControlSettings::GetRootPath() const
{
	FScopeLock ScopeLock(&CriticalSection);
	return RootPath;
}

const FString& FGitSourceControlSettings::GetBranch() const
{
	FScopeLock ScopeLock(&CriticalSection);
	return Branch;
}

const FString& FGitSourceControlSettings::GetRemote() const
{
	FScopeLock ScopeLock(&CriticalSection);
	return Remote;
}

void FGitSourceControlSettings::SetBinaryPath(const FString& InString)
{
	FScopeLock ScopeLock(&CriticalSection);
	BinaryPath = InString;
	BinaryPath.TrimStartAndEndInline();
}

void FGitSourceControlSettings::SetRootPath(const FString& InString)
{
	FScopeLock ScopeLock(&CriticalSection);
	RootPath = InString;
	RootPath.TrimStartAndEndInline();
}

void FGitSourceControlSettings::SetBranch(const FString& InString)
{
	FScopeLock ScopeLock(&CriticalSection);
	Branch = InString;
	Branch.TrimStartAndEndInline();
}

void FGitSourceControlSettings::SetRemote(const FString& InString)
{
	FScopeLock ScopeLock(&CriticalSection);
	Remote = InString;
	Remote.TrimStartAndEndInline();
}

void FGitSourceControlSettings::SetUseLocking(bool UseLocking)
{
	FScopeLock ScopeLock(&CriticalSection);
	bUseLocking = UseLocking;
}

bool FGitSourceControlSettings::IsUsingLocking() const
{
	FScopeLock ScopeLock(&CriticalSection);
	return bUseLocking;
}

void FGitSourceControlSettings::SetLockingUsername(const FString& InString)
{
	FScopeLock ScopeLock(&CriticalSection);
	LockingUsername = InString;
	LockingUsername.TrimStartAndEndInline();
}

const FString& FGitSourceControlSettings::GetLockingUsername()
{
	FScopeLock ScopeLock(&CriticalSection);
	return LockingUsername;
}

void FGitSourceControlSettings::SetIsAdmin(bool Admin)
{
	FScopeLock ScopeLock(&CriticalSection);
	bIsAdmin = Admin;
}

bool FGitSourceControlSettings::IsAdmin() const
{
	FScopeLock ScopeLock(&CriticalSection);
	return bIsAdmin;
}

// This is called at startup nearly before anything else in our module: BinaryPath will then be used by the provider
void FGitSourceControlSettings::LoadSettings()
{
	FScopeLock ScopeLock(&CriticalSection);
	const FString& IniFile = USourceControlHelpers::GetSettingsIni();
	bool bLoaded = GConfig->GetString(*GitSettingsConstants::SettingsSection, TEXT("BinaryPath"), BinaryPath, IniFile);
	if(!bLoaded || BinaryPath.IsEmpty())
	{
		BinaryPath = GitSourceControlUtils::FindGitBinaryPath();
	}

	bLoaded = GConfig->GetString(*GitSettingsConstants::SettingsSection, TEXT("RootPath"), RootPath, IniFile);
	bLoaded = GConfig->GetString(*GitSettingsConstants::SettingsSection, TEXT("Branch"), Branch, IniFile);
	bLoaded = GConfig->GetString(*GitSettingsConstants::SettingsSection, TEXT("Remote"), Remote, IniFile);
	bLoaded = GConfig->GetBool(*GitSettingsConstants::SettingsSection, TEXT("IsAdmin"), bIsAdmin, IniFile);
	bLoaded = GConfig->GetBool(*GitSettingsConstants::SettingsSection, TEXT("UseLocking"), bUseLocking, IniFile);
	bLoaded = GConfig->GetString(*GitSettingsConstants::SettingsSection, TEXT("LockingUsername"), LockingUsername, IniFile);
}

void FGitSourceControlSettings::SaveSettings() const
{
	FScopeLock ScopeLock(&CriticalSection);

	// Reset git provider to validate every change
	FGitSourceControlModule& GitSourceControl = FGitSourceControlModule::GetInstance();
	if (GitSourceControl.GetProvider().CheckGitAvailability())
	{
		const FString& IniFile = USourceControlHelpers::GetSettingsIni();
		GConfig->SetString(*GitSettingsConstants::SettingsSection, TEXT("BinaryPath"), *BinaryPath, IniFile);
		GConfig->SetString(*GitSettingsConstants::SettingsSection, TEXT("RootPath"), *RootPath, IniFile);
		GConfig->SetString(*GitSettingsConstants::SettingsSection, TEXT("Branch"), *Branch, IniFile);
		GConfig->SetString(*GitSettingsConstants::SettingsSection, TEXT("Remote"), *Remote, IniFile);
		GConfig->SetBool(*GitSettingsConstants::SettingsSection, TEXT("IsAdmin"), bIsAdmin, IniFile);
		GConfig->SetBool(*GitSettingsConstants::SettingsSection, TEXT("UseLocking"), bUseLocking, IniFile);
		GConfig->SetString(*GitSettingsConstants::SettingsSection, TEXT("LockingUsername"), *LockingUsername, IniFile);
	}
}