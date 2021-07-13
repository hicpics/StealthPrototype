// Copyright (c) 2017-2020 Samuel Kahn (samuel@kahncode.com). All Rights Reserved.

#pragma once

#include "GitSourceControlState.h"
#include "GitSourceControlRevision.h"

class FGitSourceControlCommand;

/**
 * Helper struct for maintaining temporary files for passing to commands
 */
class FScopedTempFile
{
public:

	/** Constructor - open & write string to temp file */
	FScopedTempFile(const FText& InText);

	/** Destructor - delete temp file */
	~FScopedTempFile();

	/** Get the filename of this temp file - empty if it failed to be created */
	const FString& GetFilename() const;

private:
	/** The filename we are writing to */
	FString Filename;
};

enum GitIndexState
{
	Invalid = 0,
	Valid,
	MustResolveConflicts
};

namespace GitSourceControlUtils
{

/**
 * Find the path to the Git binary, looking into a few places (standalone Git install, and other common tools embedding Git)
 * @returns the path to the Git binary if found, or an empty string.
 */
FString FindGitBinaryPath();

/**
 * Run a Git "version" command to check the availability of the binary.
 * @param InPathToGitBinary		The path to the Git binary
 * @returns true if the command succeeded and returned no errors
 */
bool CheckGitAvailability(const FString& InPathToGitBinary);

/**
 * Find the root of the Git repository, looking from the provided path and upward in its parent directories
 * @param InPath				The path to the Game Directory (or any path or file in any git repository)
 * @param OutRepositoryRoot		The path to the root directory of the Git repository if found, else the path to the GameDir
 * @returns true if the command succeeded and returned no errors
 */
bool FindRootDirectory(const FString& InPath, FString& OutRepositoryRoot);

/**
 * Tests if a directory is a git repository, i.e. if ".git" subdirectory exists
 * @param Directory				Directory to test
 * @returns true if Directory is a git repository
 */
bool IsGitRepository(const FString &Directory);

/**
 * Trims slashes and backslashes at the end of a path
 * @param InOutPath				Path to trim
 */
void TrimTrailingSlashes(FString& InOutPath);

/**
 * Get Git config user.name & user.email
 * @param	InPathToGitBinary	The path to the Git binary
 * @param	InRepositoryRoot	The Git repository from where to run the command - usually the Game directory (can be empty)
 * @param	OutUserName			Name of the Git user configured for this repository (or globaly)
 * @param	OutEmailName		E-mail of the Git user configured for this repository (or globaly)
 */
void GetUserConfig(const FString& InPathToGitBinary, const FString& InRepositoryRoot, FString& OutUserName, FString& OutUserEmail);

/**
 * Get Git current checked-out branch
 * @param	InPathToGitBinary	The path to the Git binary
 * @param	InRepositoryRoot	The Git repository from where to run the command - usually the Game directory (can be empty)
 * @param	OutBranchName		Name of the current checked-out branch (if any, ie. not in detached HEAD)
 */
void GetBranchName(const FString& InPathToGitBinary, const FString& InRepositoryRoot, FString& OutBranchName);

/**
 * Get all git remotes
 * @param	InPathToGitBinary	The path to the Git binary
 * @param	InRepositoryRoot	The Git repository from where to run the command - usually the Game directory (can be empty)
 * @param	OutRemoteNames		List of all remotes of this git repository
 */
void GetRemoteNames(const FString& InPathToGitBinary, const FString& InRepositoryRoot, TArray<FString>& OutRemoteNames);

/**
 * Run a Git command - output is a string TArray.
 *
 * @param	InCommand			The Git command - e.g. commit
 * @param	InPathToGitBinary	The path to the Git binary
 * @param	InRepositoryRoot	The Git repository from where to run the command - usually the Game directory (can be empty)
 * @param	InParameters		The parameters to the Git command
 * @param	InFiles				The files to be operated on
 * @param	OutResults			The results (from StdOut) as an array per-line
 * @param	OutErrorMessages	Any errors (from StdErr) as an array per-line
 * @returns true if the command succeeded and returned no errors
 */
bool RunCommand(const FString& InCommand, const FString& InPathToGitBinary, const FString& InRepositoryRoot, const TArray<FString>& InParameters, const TArray<FString>& InFiles, TArray<FString>& OutResults, TArray<FString>& OutErrorMessages);

/**
 * Run a Git "commit" command by batches.
 *
 * @param	InPathToGitBinary	The path to the Git binary
 * @param	InRepositoryRoot	The Git repository from where to run the command - usually the Game directory (can be empty)
 * @param	InParameter			The parameters to the Git commit command
 * @param	InFiles				The files to be operated on
 * @param	OutErrorMessages	Any errors (from StdErr) as an array per-line
 * @returns true if the command succeeded and returned no errors
 */
bool RunCommit(const FString& InPathToGitBinary, const FString& InRepositoryRoot, const TArray<FString>& InParameters, const TArray<FString>& InFiles, TArray<FString>& OutResults, TArray<FString>& OutErrorMessages);

/**
 * Run a series of commands to determine status of requested files against fetch head. Fetch must have been called prior.
 *
 * @param	InPathToGitBinary	The path to the Git binary
 * @param	InRepositoryRoot	The Git repository from where to run the command - usually the Game directory (can be empty)
 * @param	InFiles				The files to be operated on
 * @param	OutErrorMessages	Any errors (from StdErr) as an array per-line
 * @returns true if the command succeeded and returned no errors
 */
bool RunUpdateStatus(FGitSourceControlCommand& InCommand, const TArray<FString>& InFiles, TArray<FString>& OutErrorMessages, TArray<FGitSourceControlState>& OutStates);

//Internals
void ParseStatusResults(const FString& InPathToGitBinary, const FString& InRepositoryRoot, const TArray<FString>& InFiles, const TArray<FString>& InResults, TMap<FString, FGitSourceControlState>& OutStates);
void ParseNameStatusResults(const FString& InPathToGitBinary, const FString& InRepositoryRoot, const TArray<FString>& InResults, TMap<FString, FGitSourceControlState>& OutStates);


/**
 * Run a Git fetch"status" command and parse it.
 *
 * @param	InCommand	The source control command (which contains all necessary parameters)
 * @returns true if the command succeeded and returned no errors
 */
bool RunFetch(const FGitSourceControlCommand& InCommand);

/**
 * Run a Git "show" command to dump the binary content of a revision into a file. Will use git lfs smudge if the file is tracked by git lfs.
 *
 * @param	InPathToGitBinary	The path to the Git binary
 * @param	InRepositoryRoot	The Git repository from where to run the command - usually the Game directory (can be empty)
 * @param	InFile				The file to get
 * @param	InCommit			The commit to get the file at
 * @param	InDumpFileName		The temporary file to dump the revision
 * @returns true if the command succeeded and returned no errors
*/
bool RunDumpToFile(const FString& InPathToGitBinary, const FString& InRepositoryRoot, const FString& InFile, const FString& InCommit, const FString& InDumpFileName);

/**
 * Run a Git "log" command and parse it.
 *
 * @param	InPathToGitBinary	The path to the Git binary
 * @param	InRepositoryRoot	The Git repository from where to run the command - usually the Game directory (can be empty)
 * @param	InBranch			The branch history to log
 * @param	InFile				The file to be operated on
 * @param	OutErrorMessages	Any errors (from StdErr) as an array per-line
 * @param	OutHistory			The history of the file
 */
bool RunGetHistory(const FString& InPathToGitBinary, const FString& InRepositoryRoot, const FString& InBranch, const FString& InFile, TArray<FString>& OutErrorMessages, TGitSourceControlHistory& OutHistory);

/**
 * Will checkout the file at the specified revision and update StatusFile accordingly
 *
 * @param	InPathToGitBinary	The path to the Git binary
 * @param	InRepositoryRoot	The Git repository from where to run the command - usually the Game directory (can be empty)
 * @param	InFile				The file to be operated on
 * @param	InRevision			The SHA of the revision to checkout the file at. Use "0" for HEAD revision. Do not use with logical names!
 * @param	OutErrorMessages	Any errors (from StdErr) as an array per-line
 * @param	OverrideSavedState	Override the state in the saved file
 */
bool RunSyncFile(const FString& InPathToGitBinary, const FString& InRepositoryRoot, const FString& InFile, const FString& InRevision, TArray<FString>& OutErrorMessages, EWorkingCopyState::Type OverrideSavedState = EWorkingCopyState::Unknown);

/**
 * Will lock the files
 *
 * @param	InPathToGitBinary	The path to the Git binary
 * @param	InRepositoryRoot	The Git repository from where to run the command - usually the Game directory (can be empty)
 * @param	InFiles				The files to be operated on
 * @param	OutErrorMessages	Any errors (from StdErr) as an array per-line
 */
bool RunLockFiles(const FString& InPathToGitBinary, const FString& InRepositoryRoot, const FString& InRemote, const TArray<FString>& InFiles, TArray<FString>& OutErrorMessages);

/**
 * Will unlock the files
 *
 * @param	InPathToGitBinary	The path to the Git binary
 * @param	InRepositoryRoot	The Git repository from where to run the command - usually the Game directory (can be empty)
 * @param	InFiles				The files to be operated on
 * @param	OutErrorMessages	Any errors (from StdErr) as an array per-line
 */
bool RunUnlockFiles(const FString& InPathToGitBinary, const FString& InRepositoryRoot, const FString& InRemote, const TArray<FString>& InFiles, TArray<FString>& OutErrorMessages, bool bForce = false);


/**
 * Helper function for various commands to update cached states.
 * @returns true if any states were updated
 */
bool UpdateCachedStates(const TArray<FGitSourceControlState>& InStates);

/** 
 * Remove redundant errors (that contain a particular string) and also
 * update the commands success status if all errors were removed.
 */
void RemoveRedundantErrors(FGitSourceControlCommand& InCommand, const FString& InFilter);


/**
 * Checks that the index is in a valid state before calling potentially destructive commands
 * For the plugin to work the index must always be empty
 * Unless in case of merge conflicts where the index and working directory will contain the pending merge states
 *
 * @param	InCommand	The source control command (which contains all necessary parameters)
 * @returns GitIndexState describes the state of the index from the point of view of executing commands, if conflict resolution is needed another command must be issued, 
 * if index is invalid the user must restore the index to be valid himself as this is a result of user tampering
 */
GitIndexState RunCheckIndexValid(FGitSourceControlCommand& InCommand);


/**
 * Returns a full SHA identifier for a logical commit or branch name
 *
 * @param	InBranch	The branch or commit name
 * @returns FString		The SHA of this branch/commit
 */
FString GetCommitShaForBranch(const FString& InBranch, const FString& InPathToGitBinary, const FString& InRepositoryRoot);

/**
 * Returns the best common ancestor for a potential merge
 *
 * @returns FString		The SHA of the selected ancestor
 */
FString GetMergeBase(const FString& InCommit1, const FString& InCommit2, const FString& InPathToGitBinary, const FString& InRepositoryRoot);

/**
 * Performs a cleanup of the status file by removing irrelevant or outdated entries
 */
void CleanupStatusFile(FGitSourceControlCommand& InCommand);


/**
 * Marks all the files writeable, no file should ever be read-only in git central
 */
void MakeWriteable(const TArray<FString>& InFiles);
void MakeWriteable(const FString& InFile);

}
