// Copyright (c) 2017-2020 Samuel Kahn (samuel@kahncode.com). All Rights Reserved.

#pragma once

class FGitSourceControlSettings
{
public:
	/** Get the User Defined Git Binary Path */
	const FString& GetBinaryPath() const;

	/** Get the User Defined Root Path */
	const FString& GetRootPath() const;

	/** Get the User Defined Branch */
	const FString& GetBranch() const;

	/** Get the User Defined Remote */
	const FString& GetRemote() const;

	/** Set the Git Binary Path */
	void SetBinaryPath(const FString& InString);

	/** Set the Git Binary Path */
	void SetRootPath(const FString& InString);

	void SetBranch(const FString& InString);
	void SetRemote(const FString& InString);

	void SetUseLocking(bool UseLocking);
	bool IsUsingLocking() const;

	void SetLockingUsername(const FString& InString);
	const FString& GetLockingUsername();

	void SetIsAdmin(bool Admin);
	bool IsAdmin() const;

	/** Load settings from ini file */
	void LoadSettings();

	/** Save settings to ini file */
	void SaveSettings() const;

private:
	/** A critical section for settings access */
	mutable FCriticalSection CriticalSection;

	/** Git binary path */
	FString BinaryPath;

	/** Git root path */
	FString RootPath;

	/** Branch to use */
	FString Branch;

	/** Remote to use */
	FString Remote;

	/** Whether we use file locking */
	bool bUseLocking = true;

	/** Optional username override for locks */
	FString LockingUsername;

	/** Whether the user has administrator access to the remote repository */
	bool bIsAdmin = false;
};