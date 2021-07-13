// Copyright (c) 2017-2018 Samuel Kahn (samuel@kahncode.com). All Rights Reserved.

#pragma once

#include "Modules/ModuleInterface.h"
#include "GitSourceControlSettings.h"
#include "GitSourceControlProvider.h"
#include "GitSourceControlStatusFile.h"
#include "GitSourceControlMenuExtensions.h"

/** See README.txt for full documentation
 */
class FGitSourceControlModule : public IModuleInterface
{
public:
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	static FGitSourceControlModule& GetInstance()
	{
		return FModuleManager::Get().GetModuleChecked<FGitSourceControlModule>("GitCentral");
	}

	static bool IsLoaded()
	{
		return FModuleManager::Get().IsModuleLoaded("GitCentral");
	}

	/** Access the Git source control settings */
	FGitSourceControlSettings& AccessSettings()
	{
		return GitSourceControlSettings;
	}

	/** Save the Git source control settings */
	void SaveSettings();

	/** Access the Git source control provider */
	FGitSourceControlProvider& GetProvider()
	{
		return GitSourceControlProvider;
	}

	/** Access the Git source control saved statuses */
	FGitSourceControlStatusFile& GetStatusFile()
	{
		return GitSourceControlStatusFile;
	}

	void RegisterMenuExtensions();
	void UnregisterMenuExtensions();

private:

	/** The Git source control provider */
	FGitSourceControlProvider GitSourceControlProvider;

	/** The settings for Git source control */
	FGitSourceControlSettings GitSourceControlSettings;

	/** The status file necessary for GitCentral */
	FGitSourceControlStatusFile GitSourceControlStatusFile;

	/** The menu extensions manager for GitCentral */
	FGitSourceControlMenuExtensions GitSourceControlMenuExtensions;
};