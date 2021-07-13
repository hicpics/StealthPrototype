// Copyright (c) 2017-2020 Samuel Kahn (samuel@kahncode.com). All Rights Reserved.

#pragma once

#include "GitSourceControlState.h"

/** FGitSourceControlStatusFile: access GitCentral saved status file
* The file store extra information about file status according to past operations
*
* Git does not enable us to know which revision a file was individually checked out at,
* therefore we use a file to emulate this behavior
*
* Losing this file will have the primary effect of forgetting if individual files were updated
* and treat each modification as if it was made from the current HEAD, possibly creating conflicts.
* Likewise it may consider local changes to be up-to-date modifications where in reality they
* should have been conflicting.
*
* We can recover from that state in normal workflow assuming the user is careful, but it is undesirable.
*/
class FGitSourceControlStatusFile
{
public:
	FGitSourceControlStatusFile();
	~FGitSourceControlStatusFile();

	bool Save(const FString& PathToRepositoryRoot, bool bForce = false);
	bool Load(const FString& PathToRepositoryRoot);

	//Saves the current states so they can be restored in case of failure, saving or loading will clear the cache
	void CacheStates();
	void RestoreCachedStates();

	void ClearCache();
	void ClearSavedStates();

	const SavedState& GetState(const FString& InFilePath) const;
	bool SetState(const FString& InFilePath, const SavedState& InState, const FString& PathToRepositoryRoot, bool bSave = true);
	bool ClearState(const FString& InFilePath, const FString& PathToRepositoryRoot, bool bSave = true);

	const TMap<FString, SavedState>& GetAllStates() const { return SavedStates; }

private:

	bool LoadStatusFile(const FString& PathToRepositoryRoot, const FString& StatusFilePath);

	bool bDirty;

	//Path is stored absolute
	TMap<FString, SavedState> SavedStates;
	TMap<FString, SavedState> CachedStates;
	TSharedPtr<FJsonObject> LoadedData;
};