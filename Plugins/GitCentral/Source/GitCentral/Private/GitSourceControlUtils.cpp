// Copyright (c) 2017-2020 Samuel Kahn (samuel@kahncode.com). All Rights Reserved.

#include "GitSourceControlUtils.h"
#include "GitSourceControlState.h"
#include "GitSourceControlModule.h"
#include "GitSourceControlCommand.h"
#include "Misc/EngineVersionComparison.h"

#if PLATFORM_LINUX
#include <sys/ioctl.h>
#elif PLATFORM_WINDOWS
//Note: This leaks symbols using unity builds.
#include "Windows/WindowsHWrapper.h"
//Prevent unity build compile error.
#undef GetUserName
#endif

//Inspired from InteractiveProcess.h
static FORCEINLINE bool CreatePipeWrite(void*& ReadPipe, void*& WritePipe)
{
#if PLATFORM_WINDOWS
	SECURITY_ATTRIBUTES Attr = { sizeof(SECURITY_ATTRIBUTES), NULL, true };

	if(!::CreatePipe(&ReadPipe, &WritePipe, &Attr, 0))
	{
		return false;
	}

	if(!::SetHandleInformation(WritePipe, HANDLE_FLAG_INHERIT, 0))
	{
		return false;
	}

	return true;
#else
	return FPlatformProcess::CreatePipe(ReadPipe, WritePipe);
#endif // PLATFORM_WINDOWS
}

namespace GitSourceControlConstants
{
	/** The maximum number of files we submit in a single Git command */
	const int32 MaxFilesPerBatch = 50;
}

FScopedTempFile::FScopedTempFile(const FText& InText)
{
	Filename = FPaths::CreateTempFilename(*FPaths::ProjectLogDir(), TEXT("Git-Temp"), TEXT(".txt"));
	if(!FFileHelper::SaveStringToFile(InText.ToString(), *Filename, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		GITCENTRAL_ERROR(TEXT("Failed to write to temp file: %s"), *Filename);
	}
}

FScopedTempFile::~FScopedTempFile()
{
	if(FPaths::FileExists(Filename))
	{
		if(!FPlatformFileManager::Get().GetPlatformFile().DeleteFile(*Filename))
		{
			GITCENTRAL_ERROR(TEXT("Failed to delete temp file: %s"), *Filename);
		}
	}
}

const FString& FScopedTempFile::GetFilename() const
{
	return Filename;
}


namespace GitSourceControlUtils
{

// Launch the Git command line process and extract its results & errors
static bool RunCommandInternalRaw(const FString& InCommand, const FString& InPathToGitBinary, const FString& InRepositoryRoot, const TArray<FString>& InParameters, const TArray<FString>& InFiles, FString& OutResults, FString& OutErrors)
{
	int32 ReturnCode = 0;
	FString FullCommand;
	FString LogableCommand; // short version of the command for logging purpose

	if(!InRepositoryRoot.IsEmpty())
	{
		//FString RepositoryRoot = InRepositoryRoot;

		// Detect a "migrate asset" scenario (a "git add" command is applied to files outside the current project)
		/*if ( (InFiles.Num() > 0) && (!InFiles[0].StartsWith(InRepositoryRoot)) )
		{
			// in this case, find the git repository (if any) of the destination Project
			FindRootDirectory(FPaths::GetPath(InFiles[0]), RepositoryRoot);
		}*/

		//Note: Using -C option to specify directory.
		//Other option --work-tree and --git-dir can fail on some commands, observed for instance with Stashing
		FullCommand = TEXT("-C \"");
		FullCommand += InRepositoryRoot;
		FullCommand += TEXT("\" ");

		// Specify the working copy (the root) of the git repository (before the command itself)
		/*FullCommand  = TEXT("--work-tree=\"");
		FullCommand += RepositoryRoot;
		// and the ".git" subdirectory in it (before the command itself)
		FullCommand += TEXT("\" --git-dir=\"");
		FullCommand += FPaths::Combine(*RepositoryRoot, TEXT(".git\" "));*/
	}
	// then the git command itself ("status", "log", "commit"...)
	LogableCommand += InCommand;

	// Append to the command all parameters, and then finally the files
	for(const auto& Parameter : InParameters)
	{
		LogableCommand += TEXT(" ");
		LogableCommand += Parameter;
	}
	for(const auto& File : InFiles)
	{
		LogableCommand += TEXT(" \"");
		LogableCommand += File;
		LogableCommand += TEXT("\"");
	}
	// Also, Git does not have a "--non-interactive" option, as it auto-detects when there are no connected standard input/output streams

	FullCommand += LogableCommand;

	GITCENTRAL_VERBOSE(TEXT("ExecProcess: 'git %s'"), *LogableCommand);
	
	FPlatformProcess::ExecProcess(*InPathToGitBinary, *FullCommand, &ReturnCode, &OutResults, &OutErrors);

	GITCENTRAL_VERBOSE(TEXT("ExecProcess: ReturnCode=%d OutResults='%s'"), ReturnCode, *OutResults);
	if (ReturnCode != 0)
	{
		GITCENTRAL_VERBOSE(TEXT("ExecProcess: ReturnCode=%d OutErrors='%s'"), ReturnCode, *OutErrors);
	}

	return ReturnCode == 0;
}

// Basic parsing or results & errors from the Git command line process
static bool RunCommandInternal(const FString& InCommand, const FString& InPathToGitBinary, const FString& InRepositoryRoot, const TArray<FString>& InParameters, const TArray<FString>& InFiles, TArray<FString>& OutResults, TArray<FString>& OutErrorMessages)
{
	bool bResult;
	FString Results;
	FString Errors;

	bResult = RunCommandInternalRaw(InCommand, InPathToGitBinary, InRepositoryRoot, InParameters, InFiles, Results, Errors);

	TArray<FString> AppendResults;
	Results.ParseIntoArray(AppendResults, TEXT("\n"), true);
	OutResults.Append(AppendResults);

	TArray<FString> AppendErrors;
	Errors.ParseIntoArray(AppendErrors, TEXT("\n"), true);
	OutErrorMessages.Append(AppendErrors);

	return bResult;
}

static FString GetAppDataLocalPath()
{
#if UE_VERSION_NEWER_THAN(4, 21, 0)
	return FPlatformMisc::GetEnvironmentVariable(TEXT("LOCALAPPDATA"));
#else
	TCHAR AppDataLocalPath[4096];
	FPlatformMisc::GetEnvironmentVariable(TEXT("LOCALAPPDATA"), AppDataLocalPath, ARRAY_COUNT(AppDataLocalPath));
	return AppDataLocalPath;
#endif
}

FString FindGitBinaryPath()
{
	FString GitBinaryPath;
	bool bFound = false;

#if PLATFORM_WINDOWS
	// 1) First of all, look into standard install directories
	// NOTE using only "git" (or "git.exe") relying on the "PATH" envvar does not always work as expected, depending on the installation:
	// If the PATH is set with "git/cmd" instead of "git/bin",
	// "git.exe" launch "git/cmd/git.exe" that redirect to "git/bin/git.exe" and ExecProcess() is unable to catch its outputs streams.
	// First check the 64-bit program files directory:
	GitBinaryPath = TEXT("C:/Program Files/Git/bin/git.exe");
	bFound = CheckGitAvailability(GitBinaryPath);
	if(!bFound)
	{
		// otherwise check the 32-bit program files directory.
		GitBinaryPath = TEXT("C:/Program Files (x86)/Git/bin/git.exe");
		bFound = CheckGitAvailability(GitBinaryPath);
	}

	FString AppDataLocalPath = GetAppDataLocalPath();

	if(!bFound)
	{
		// else the install dir for the current user: C:\Users\UserName\AppData\Local\Programs\Git\cmd
		GitBinaryPath = FString::Printf(TEXT("%s/Programs/Git/cmd/git.exe"), *AppDataLocalPath);
		bFound = CheckGitAvailability(GitBinaryPath);
	}

	// 2) Else, look for the version of Git bundled with SmartGit "Installer with JRE"
	if(!bFound)
	{
		GitBinaryPath = TEXT("C:/Program Files (x86)/SmartGit/bin/git.exe");
		bFound = CheckGitAvailability(GitBinaryPath);
	}

	// 3) Else, look for the local_git provided by SourceTree
	if(!bFound)
	{
		// C:\Users\UserName\AppData\Local\Atlassian\SourceTree\git_local\bin
		GitBinaryPath = FString::Printf(TEXT("%s/Atlassian/SourceTree/git_local/bin/git.exe"), *AppDataLocalPath);
		bFound = CheckGitAvailability(GitBinaryPath);
	}

	// 4) Else, look for the PortableGit provided by GitHub for Windows
	if(!bFound)
	{
		// The latest GitHub for windows adds its binaries into the local appdata directory:
		// C:\Users\UserName\AppData\Local\GitHub\PortableGit_c2ba306e536fdf878271f7fe636a147ff37326ad\bin
		FString SearchPath = FString::Printf(TEXT("%s/GitHub/PortableGit_*"), *AppDataLocalPath);
		TArray<FString> PortableGitFolders;
		IFileManager::Get().FindFiles(PortableGitFolders, *SearchPath, false, true);
		if(PortableGitFolders.Num() > 0)
		{
			// FindFiles just returns directory names, so we need to prepend the root path to get the full path.
			GitBinaryPath = FString::Printf(TEXT("%s/GitHub/%s/bin/git.exe"), *AppDataLocalPath, *(PortableGitFolders.Last())); // keep only the last PortableGit found
			bFound = CheckGitAvailability(GitBinaryPath);
		}
	}
#elif PLATFORM_MAC || PLATFORM_LINUX
	//See SubversionSourceControlUtils.cpp
	//TODO : implement this also for windows instead of the clever logic above "where git" should give us the path to git
	const TCHAR* Command[] = { TEXT("/usr/bin/which"), TEXT("git") };

	const bool bLaunchDetached = false;
	const bool bLaunchHidden = true;

	{
		// Attempt to detect a system wide version of the git command line tools
		void* ReadPipe = nullptr, *WritePipe = nullptr;
		FPlatformProcess::CreatePipe(ReadPipe, WritePipe);

		FProcHandle ProcHandle = FPlatformProcess::CreateProc(Command[0], Command[1], bLaunchDetached, bLaunchHidden, bLaunchHidden, NULL, 0, NULL, WritePipe);
		if(ProcHandle.IsValid())
		{
			FPlatformProcess::WaitForProc(ProcHandle);
			GitBinaryPath = FPlatformProcess::ReadPipe(ReadPipe);

			// Trim at the first \n
			int32 NewLine = INDEX_NONE;
			if(GitBinaryPath.FindChar('\n', NewLine))
			{
				GitBinaryPath = GitBinaryPath.Left(NewLine);
			}
		}
		FPlatformProcess::ClosePipe(ReadPipe, WritePipe);
		FPlatformProcess::CloseProc(ProcHandle);
	}

	bFound = !GitBinaryPath.IsEmpty() && FPaths::FileExists(GitBinaryPath) && CheckGitAvailability(GitBinaryPath);
#else
	#error Platform not tested
#endif

	if(bFound)
	{
		FPaths::MakePlatformFilename(GitBinaryPath);
	}
	else
	{
		// If we did not find a path to Git, set it empty
		GitBinaryPath.Empty();
	}

	return GitBinaryPath;
}

//TODO : Use this functionnality for enabling/disabling locking features
namespace GitCapabilities
{
	static bool bGitAvailable = false;
	static bool bGitLfsAvailable = false;
	static bool bSupportsLocking = false;
}

bool CheckGitAvailability(const FString& InPathToGitBinary)
{
	FString InfoMessages;
	FString ErrorMessages;
	GitCapabilities::bGitAvailable = RunCommandInternalRaw(TEXT("version"), InPathToGitBinary, FString(), TArray<FString>(), TArray<FString>(), InfoMessages, ErrorMessages);

	if(GitCapabilities::bGitAvailable && InfoMessages.Contains("git"))
	{
		InfoMessages.Empty();
		GitCapabilities::bGitLfsAvailable = RunCommandInternalRaw(TEXT("lfs version"), InPathToGitBinary, FString(), TArray<FString>(), TArray<FString>(), InfoMessages, ErrorMessages);
		if(GitCapabilities::bGitLfsAvailable &&InfoMessages.StartsWith("git-lfs/"))
		{
			//extract version number
			//2.0.0 is the first version that supports git lfs lock
			
			const float Version = TCString<TCHAR>::Atof(*InfoMessages.Mid(8));
			if (Version < 2.0)
			{
				GitCapabilities::bSupportsLocking = false;
				GITCENTRAL_ERROR(TEXT("git-lfs outdated version %f, update to latest to handle locking and ensure proper function"), Version);
			}
			else
			{
				GitCapabilities::bSupportsLocking = true;
			}

			//check if lfs is installed
			//Note that if not installed globally but only at a repository level this will fail, need to use --local
			InfoMessages.Empty();
			GitCapabilities::bGitLfsAvailable = RunCommandInternalRaw(TEXT("config filter.lfs.required"), InPathToGitBinary, FString(), TArray<FString>(), TArray<FString>(), InfoMessages, ErrorMessages);
			if(GitCapabilities::bGitLfsAvailable && InfoMessages.StartsWith("true"))
			{
				GitCapabilities::bGitAvailable = true;
			}
			else
			{
				GITCENTRAL_ERROR(TEXT("git-lfs is not installed, run 'git lfs install'"));
			}

		}
	}

	return GitCapabilities::bGitAvailable;
}

bool IsGitRepository(const FString &Directory)
{
	FString PathToGitSubdirectory = Directory / TEXT(".git");
	return IFileManager::Get().DirectoryExists(*PathToGitSubdirectory);
}

void TrimTrailingSlashes(FString& InOutPath)
{
	auto TrimTrailing = [](FString& Str, const TCHAR Char)
	{
		int32 Len = Str.Len();
		while(Len && Str[Len - 1] == Char)
		{
			Str = Str.LeftChop(1);
			Len = Str.Len();
		}
	};

	TrimTrailing(InOutPath, '\\');
	TrimTrailing(InOutPath, '/');
}

// Find the root of the Git repository, looking from the provided path and upward in its parent directories.
bool FindRootDirectory(const FString& InPath, FString& OutRepositoryRoot)
{
	bool bFound = false;
	FString PathToGitSubdirectory;
	OutRepositoryRoot = InPath;

	TrimTrailingSlashes(OutRepositoryRoot);

	while(!bFound && !OutRepositoryRoot.IsEmpty())
	{
		// Look for the ".git" subdirectory present at the root of every Git repository
		bFound = IsGitRepository(OutRepositoryRoot);

		if(!bFound)
		{
			int32 LastSlashIndex;
			if(OutRepositoryRoot.FindLastChar('/', LastSlashIndex))
			{
				OutRepositoryRoot = OutRepositoryRoot.Left(LastSlashIndex);
			}
			else
			{
				OutRepositoryRoot.Empty();
			}
		}
	}
	if (!bFound)
	{
		OutRepositoryRoot = InPath; // If not found, return the provided dir as best possible root.
	}
	return bFound;
}

void GetUserConfig(const FString& InPathToGitBinary, const FString& InRepositoryRoot, FString& OutUserName, FString& OutUserEmail)
{
	bool bResults;
	TArray<FString> InfoMessages;
	TArray<FString> ErrorMessages;
	TArray<FString> Parameters;
	Parameters.Add(TEXT("user.name"));
	bResults = RunCommandInternal(TEXT("config"), InPathToGitBinary, InRepositoryRoot, Parameters, TArray<FString>(), InfoMessages, ErrorMessages);
	if(bResults && InfoMessages.Num() > 0)
	{
		OutUserName = InfoMessages[0];
	}

	Parameters.Reset();
	Parameters.Add(TEXT("user.email"));
	InfoMessages.Reset();
	bResults &= RunCommandInternal(TEXT("config"), InPathToGitBinary, InRepositoryRoot, Parameters, TArray<FString>(), InfoMessages, ErrorMessages);
	if(bResults && InfoMessages.Num() > 0)
	{
		OutUserEmail = InfoMessages[0];
	}
}

void GetBranchName(const FString& InPathToGitBinary, const FString& InRepositoryRoot, FString& OutBranchName)
{
	bool bResults;
	TArray<FString> InfoMessages;
	TArray<FString> ErrorMessages;
	TArray<FString> Parameters;
	Parameters.Add(TEXT("--short"));
	Parameters.Add(TEXT("--quiet"));		// no error message while in detached HEAD
	Parameters.Add(TEXT("HEAD"));	
	bResults = RunCommandInternal(TEXT("symbolic-ref"), InPathToGitBinary, InRepositoryRoot, Parameters, TArray<FString>(), InfoMessages, ErrorMessages);
	if(bResults && InfoMessages.Num() > 0)
	{
		OutBranchName = InfoMessages[0];
	}
	else
	{
		Parameters.Reset();
		Parameters.Add(TEXT("-1"));
		Parameters.Add(TEXT("--format=\"%h\""));		// no error message while in detached HEAD
		bResults = RunCommandInternal(TEXT("log"), InPathToGitBinary, InRepositoryRoot, Parameters, TArray<FString>(), InfoMessages, ErrorMessages);
		if(bResults && InfoMessages.Num() > 0)
		{
			OutBranchName = "HEAD detached at ";
			OutBranchName += InfoMessages[0];
		}
	}
}

void GetRemoteNames(const FString& InPathToGitBinary, const FString& InRepositoryRoot, TArray<FString>& OutRemoteNames)
{
	bool bResults;
	TArray<FString> InfoMessages;
	TArray<FString> ErrorMessages;
	TArray<FString> Parameters;
	bResults = RunCommandInternal(TEXT("remote"), InPathToGitBinary, InRepositoryRoot, Parameters, TArray<FString>(), InfoMessages, ErrorMessages);
	if(bResults && InfoMessages.Num() > 0)
	{
		OutRemoteNames = InfoMessages;
	}
}

bool RunCommand(const FString& InCommand, const FString& InPathToGitBinary, const FString& InRepositoryRoot, const TArray<FString>& InParameters, const TArray<FString>& InFiles, TArray<FString>& OutResults, TArray<FString>& OutErrorMessages)
{
	bool bResult = true;

	if(InFiles.Num() > GitSourceControlConstants::MaxFilesPerBatch)
	{
		// Batch files up so we dont exceed command-line limits
		int32 FileCount = 0;
		while(FileCount < InFiles.Num())
		{
			TArray<FString> FilesInBatch;
			for(int32 FileIndex = 0; FileCount < InFiles.Num() && FileIndex < GitSourceControlConstants::MaxFilesPerBatch; FileIndex++, FileCount++)
			{
				FilesInBatch.Add(InFiles[FileCount]);
			}

			TArray<FString> BatchResults;
			TArray<FString> BatchErrors;
			bResult &= RunCommandInternal(InCommand, InPathToGitBinary, InRepositoryRoot, InParameters, FilesInBatch, BatchResults, BatchErrors);
			OutResults += BatchResults;
			OutErrorMessages += BatchErrors;
		}
	}
	else
	{
		bResult &= RunCommandInternal(InCommand, InPathToGitBinary, InRepositoryRoot, InParameters, InFiles, OutResults, OutErrorMessages);
	}

	return bResult;
}

// Run a Git "commit" command by batches
bool RunCommit(const FString& InPathToGitBinary, const FString& InRepositoryRoot, const TArray<FString>& InParameters, const TArray<FString>& InFiles, TArray<FString>& OutResults, TArray<FString>& OutErrorMessages)
{
	//Note : this is not currently called as commit is implemented differently
	bool bResult = true;

	if(InFiles.Num() > GitSourceControlConstants::MaxFilesPerBatch)
	{
		// Batch files up so we dont exceed command-line limits
		int32 FileCount = 0;
		{
			TArray<FString> FilesInBatch;
			for(int32 FileIndex = 0; FileIndex < GitSourceControlConstants::MaxFilesPerBatch; FileIndex++, FileCount++)
			{
				FilesInBatch.Add(InFiles[FileCount]);
			}
			// First batch is a simple "git commit" command with only the first files
			bResult &= RunCommandInternal(TEXT("commit"), InPathToGitBinary, InRepositoryRoot, InParameters, FilesInBatch, OutResults, OutErrorMessages);
		}
		
		TArray<FString> Parameters;
		for(const auto& Parameter : InParameters)
		{
			Parameters.Add(Parameter);
		}
		Parameters.Add(TEXT("--amend"));

		while (FileCount < InFiles.Num())
		{
			TArray<FString> FilesInBatch;
			for(int32 FileIndex = 0; FileCount < InFiles.Num() && FileIndex < GitSourceControlConstants::MaxFilesPerBatch; FileIndex++, FileCount++)
			{
				FilesInBatch.Add(InFiles[FileCount]);
			}
			// Next batches "amend" the commit with some more files
			TArray<FString> BatchResults;
			TArray<FString> BatchErrors;
			bResult &= RunCommandInternal(TEXT("commit"), InPathToGitBinary, InRepositoryRoot, Parameters, FilesInBatch, BatchResults, BatchErrors);
			OutResults += BatchResults;
			OutErrorMessages += BatchErrors;
		}
	}
	else
	{
		RunCommandInternal(TEXT("commit"), InPathToGitBinary, InRepositoryRoot, InParameters, InFiles, OutResults, OutErrorMessages);
	}

	return bResult;
}

/**
 * Extract and interpret the file state from the given Git status result.
 * @see http://git-scm.com/docs/git-status
 * ' ' = unchanged
 * 'M' = modified
 * 'A' = added
 * 'D' = deleted
 * 'R' = renamed
 * 'C' = copied
 * 'U' = updated but unmerged
 * '?' = unknown/untracked
 * '!' = ignored

*Example output with edge cases
*Note that files in directories that aren't known by source control aren't shown in git status --porcelain

*λ git status --porcelain
*A  "dir/file with spaces.txt"
*R  dir/file.txt -> dir/file_2.txt
*/

static void ParseStatusLine(const FString& InLine, const FString& InRepositoryRoot, TMap<FString, FGitSourceControlState>& OutStates)
{
	if(InLine.Len() < 2)
		return;

	// Extract the relative filename from the Git status result
	//TODO : if the filename has spaces, quotes need to be escaped
	FString RelativeFilename = InLine.RightChop(3);
	FString RelativeFilenameRenamed;

	// Note: this is not enough in case of a rename from -> to
	int32 RenameIndex = RelativeFilename.Find(" -> ");
	if(RenameIndex != INDEX_NONE)
	{
		RelativeFilenameRenamed = RelativeFilename.Mid(RenameIndex + 4);
		RelativeFilename = RelativeFilename.Left(RenameIndex);
	}

	if(RelativeFilename.IsEmpty())
		return;

	//Restore absolute path in the state
	FGitSourceControlState State(FPaths::Combine(InRepositoryRoot, RelativeFilename));

	TCHAR IndexState = InLine[0];
	TCHAR WCopyState = InLine[1];

	State.bStaged = IndexState != ' ';

	if((IndexState == 'U' || WCopyState == 'U')
		|| (IndexState == 'A' && WCopyState == 'A')
		|| (IndexState == 'D' && WCopyState == 'D'))
	{
		// "Unmerged" conflict cases are generally marked with a "U",
		// but there are also the special cases of both "A"dded, or both "D"eleted
		State.WorkingCopyState = EWorkingCopyState::Conflicted;
	}
	else if(IndexState == 'A')
	{
		State.WorkingCopyState = EWorkingCopyState::Added;
	}
	else if(IndexState == 'D' || WCopyState == 'D')
	{
		State.WorkingCopyState = EWorkingCopyState::Deleted;
	}
	else if(IndexState == 'M' || WCopyState == 'M')
	{
		State.WorkingCopyState = EWorkingCopyState::Modified;
	}
	else if(IndexState == '?' || WCopyState == '?')
	{
		State.WorkingCopyState = EWorkingCopyState::NotControlled;
		State.bStaged = false;
	}
	else if(IndexState == '!' || WCopyState == '!')
	{
		State.WorkingCopyState = EWorkingCopyState::Ignored;
	}
	else if(IndexState == 'R')
	{
		State.WorkingCopyState = EWorkingCopyState::Deleted;

		FGitSourceControlState RenamedState(FPaths::Combine(InRepositoryRoot, RelativeFilenameRenamed));
		RenamedState.WorkingCopyState = EWorkingCopyState::NotControlled;
		RenamedState.UpdateTimeStamp();
		if(!OutStates.Find(RenamedState.GetFilename()))
			OutStates.Add(RenamedState.GetFilename(), MoveTemp(RenamedState));
	}
	else if(IndexState == 'C')
	{
		//TODO: Test this, it is unhandled across the board so far
		State.WorkingCopyState = EWorkingCopyState::Unknown;
	}
	else
	{
		// unchanged never yield a status
		State.WorkingCopyState = EWorkingCopyState::Unknown;
	}

	State.UpdateTimeStamp();
	OutStates.Add(State.GetFilename(), MoveTemp(State));
};

/** Interpret the line from a --name-status command and return a FGitSourceControlState */
static void ParseNameStatusLine(const FString& InLine, const FString& InRepositoryRoot, TMap<FString, FGitSourceControlState>& OutStates)
{
	if(InLine.Len() < 2)
		return;

	// Extract the relative filename from the Git status result
	//TODO : if the filename has spaces, quotes need to be escaped
	FString RelativeFilename;
	FString RelativeFilenameRenamed;
	int32 TabIndex = INDEX_NONE;
	if(InLine.FindChar('\t', TabIndex))
	{
		RelativeFilename = InLine.Mid(TabIndex + 1);

		int32 LastTabIndex = INDEX_NONE;
		if(RelativeFilename.FindLastChar('\t', LastTabIndex))
		{
			RelativeFilenameRenamed = RelativeFilename.Mid(LastTabIndex + 1);
			RelativeFilename = RelativeFilename.Left(LastTabIndex);
		}
	}
	else
	{
		return;
	}
	
	//Restore absolute path in the state
	FGitSourceControlState State(FPaths::Combine(InRepositoryRoot, RelativeFilename));

	switch(InLine[0])
	{
	case TEXT(' '):
		State.WorkingCopyState = EWorkingCopyState::Unchanged;
		break;
	case TEXT('T'):
	case TEXT('M'):
		State.WorkingCopyState = EWorkingCopyState::Modified;
		break;
	case TEXT('A'):
		State.WorkingCopyState = EWorkingCopyState::Added;
		break;
	case TEXT('D'):
		State.WorkingCopyState = EWorkingCopyState::Deleted;
		break;
	case TEXT('R'):
	{
		State.WorkingCopyState = EWorkingCopyState::Deleted;

		FGitSourceControlState RenamedState(FPaths::Combine(InRepositoryRoot, RelativeFilenameRenamed));
		RenamedState.WorkingCopyState = EWorkingCopyState::Added;
		RenamedState.UpdateTimeStamp();
		if(!OutStates.Find(RenamedState.GetFilename()))
			OutStates.Add(RenamedState.GetFilename(), MoveTemp(RenamedState));
		break;
	}
	case TEXT('U'):
		State.WorkingCopyState = EWorkingCopyState::Conflicted;
		break;
	case TEXT('C'): //Copy is not handled but this means the file has not changed ?
	case TEXT('X'):
	case TEXT('B'):
	default:
		State.WorkingCopyState = EWorkingCopyState::Unknown;
	}

	State.UpdateTimeStamp();
	OutStates.Add(State.GetFilename(), MoveTemp(State));
};

/** Interpret the line from a git lfs locks command and return a FGitSourceControlState */
static void ParseLocksLine(const FString& InLine, const FString& InRepositoryRoot, const FString& LocalUserName, TMap<FString, FGitSourceControlState>& OutStates)
{
	if (InLine.Len() < 2)
		return;

	// Extract the relative filename from the Git status result
	// There are no quotes here even if the file path has spaces
	FString RelativeFilename;
	FString LockOwner;
	int LockId = -1;
	int32 TabIndex = INDEX_NONE;
	bool bParseSuccess = false;
	if (InLine.FindChar('\t', TabIndex))
	{
		RelativeFilename = InLine.Left(TabIndex);
		RelativeFilename.TrimEndInline(); //The line may contain spaces and not only tabs
		LockOwner = InLine.Mid(TabIndex + 1);

		int IdIndex = INDEX_NONE;
		if (LockOwner.FindLastChar(TCHAR(':'), IdIndex))
		{
			LockId = FCString::Atoi(&LockOwner.GetCharArray()[IdIndex + 1]);

			int32 SecondTabIndex = INDEX_NONE;
			if (LockOwner.FindChar('\t', SecondTabIndex))
			{
				LockOwner = LockOwner.Left(SecondTabIndex);
				LockOwner.TrimStartAndEndInline();
				bParseSuccess = true;
			}
		}
	}
	
	if(!bParseSuccess)
		return;

	//Restore absolute path in the state
	FGitSourceControlState State(FPaths::Combine(InRepositoryRoot, RelativeFilename));

	State.UserLocked = LockOwner;
	State.bLockedByOther = LockOwner != LocalUserName;
	State.LockId = LockId;

	OutStates.Add(State.GetFilename(), MoveTemp(State));
}

/** Parse the array of strings results of a 'git status' command
 *
 * Example git status results:
M  Content/Textures/T_Perlin_Noise_M.uasset
R  Content/Textures/T_Perlin_Noise_M.uasset -> Content/Textures/T_Perlin_Noise_M2.uasset
?? Content/Materials/M_Basic_Wall.uasset
!! BasicCode.sln
*/
void ParseStatusResults(const FString& InPathToGitBinary, const FString& InRepositoryRoot, const TArray<FString>& InFiles, const TArray<FString>& InResults, TMap<FString, FGitSourceControlState>& OutStates)
{
	// Iterate on results first
	for(const auto& Line : InResults)
	{
		ParseStatusLine(Line, InRepositoryRoot, OutStates);
	}

	// Iterate on all files explicitly listed in the command to check if we have a state for them
	for(const auto& File : InFiles)
	{
		auto FileInfo = FPlatformFileManager::Get().GetPlatformFile().GetStatData(*File);

		//Search for the file in statuses
		if(!FileInfo.bIsDirectory && OutStates.Find(File) == nullptr)
		{
			FGitSourceControlState FileState(File);

			// File not found in status
			if(FPaths::FileExists(File))
			{
				// usually means the file is unchanged, ignored should have been caught by git status
				FileState.WorkingCopyState = EWorkingCopyState::Unchanged;
			}
			else
			{
				// Newly created content is unknown for now, status will be correctly updated on save
				FileState.WorkingCopyState = EWorkingCopyState::Unknown;
			}

			FileState.UpdateTimeStamp();

			OutStates.Add(File, FileState);
		}
	}
}

/** Parse the array of strings results of a 'git diff --name-status' or 'git log --name-status' command
 *
 * Example results, for full syntax see git help diff, look for diff-filter
R100    dir/before_rename.txt     dir/after_rename.txt
A       dir/added_file.txt
M       modified_file.txt
*/
void ParseNameStatusResults(const FString& InPathToGitBinary, const FString& InRepositoryRoot, const TArray<FString>& InResults, TMap<FString, FGitSourceControlState>& OutStates)
{
	// Iterate on results first
	for(const auto& Line : InResults)
	{
		ParseNameStatusLine(Line, InRepositoryRoot, OutStates);
	}
}

/** Parse the array of strings results of a 'git lfs locks' command
 *
 * Example results from the documentation:
$ git lfs locks
images/bar.jpg  jane   ID:123
images/foo.jpg  alice  ID:456
*/
static void ParseLocksResults(const FString& InPathToGitBinary, const FString& InRepositoryRoot, const TArray<FString>& InResults, TMap<FString, FGitSourceControlState>& OutStates)
{
	auto& Module = FGitSourceControlModule::GetInstance();
	const FString& LockingUserName = Module.AccessSettings().GetLockingUsername();
	const FString& UserName = Module.GetProvider().GetUserName();
	// Iterate on results first
	for (const auto& Line : InResults)
	{
		ParseLocksLine(Line, InRepositoryRoot, LockingUserName.IsEmpty() ? UserName : LockingUserName, OutStates);
	}
}

// Returns the full commit SHA for logical name or empty string
FString GetCommitShaForBranch(const FString& InBranch, const FString& InPathToGitBinary, const FString& InRepositoryRoot)
{
	TArray<FString> StdOut;
	TArray<FString> StdErr;
	TArray<FString> Parameters;
	Parameters.Add(InBranch);
	bool bResult = RunCommand(TEXT("rev-parse --verify"), InPathToGitBinary, InRepositoryRoot, Parameters, TArray<FString>(), StdOut, StdErr);
	if(bResult && StdOut.Num() == 1)
		return StdOut[0];
	else
		return FString();
}

FString GetMergeBase(const FString& InCommit1, const FString& InCommit2, const FString& InPathToGitBinary, const FString& InRepositoryRoot)
{
	TArray<FString> StdOut;
	TArray<FString> StdErr;
	TArray<FString> Parameters;
	Parameters.Add(InCommit1);
	Parameters.Add(InCommit2);
	const bool bResult = RunCommand(TEXT("merge-base"), InPathToGitBinary, InRepositoryRoot, Parameters, TArray<FString>(), StdOut, StdErr);
	if(!bResult || StdOut.Num() == 0)
	{
		return FString();
	}
	else
	{
		return StdOut[0];
	}
}

// Run a Git "status" command to update status of given files.
bool RunUpdateStatus(FGitSourceControlCommand& InCommand, const TArray<FString>& InFiles, TArray<FString>& OutErrorMessages, TArray<FGitSourceControlState>& OutStates)
{
	const FString& InRepositoryRoot = InCommand.PathToRepositoryRoot;
	const FString& InPathToGitBinary = InCommand.PathToGitBinary;

	bool bIsDirUpdate = false;

	//files as parameters for most commands
	TArray<FString> FilesParam;
	FilesParam.Reserve(InFiles.Num());

	//files as parameters for diff commands
	TArray<FString> FilesToDiff;
	FilesToDiff.Reserve(InFiles.Num());

	for(const auto& File : InFiles)
	{
		//Only include paths that belong to the selected git repository
		if(!File.StartsWith(InRepositoryRoot))
		{
			continue;
		}

		FFileStatData FileInfo = FPlatformFileManager::Get().GetPlatformFile().GetStatData(*File);
		FilesParam.Add(File);
		if(FileInfo.bIsDirectory)
		{
			bIsDirUpdate = true;
		}
		else if(FileInfo.bIsValid)// git diff only accepted existing filesystem paths
		{
			FilesToDiff.Add(File);
		}
	}

	if(!FilesParam.Num())
		return false;

	FString MergeBase;
	const FString RemoteBranch = InCommand.GetRemoteBranch();

	//Find merge base for local branch and remote
	{
		MergeBase = GetMergeBase(InCommand.Branch, RemoteBranch, InPathToGitBinary, InRepositoryRoot);
		if(MergeBase.IsEmpty())
		{
			OutErrorMessages.Add(*FString::Printf(TEXT("Could not find merge-base for %s and %s"), *InCommand.Branch, *RemoteBranch));
			return false;
		}
	}

	//Strategy for status against remote server:
	//Find best merge ancestor
	//Log and aggregate all changes from ancestor to remote
	//Add all relevant files from remote to file list
	//Log and aggregate all changes from ancestor to head
	//Add untracked status on top
	//Add lfs lock status
	//Compute final status based on all of this

	TMap<FString, FGitSourceControlState> States;


	// Get all the remote diffs since merge-base
	// We do this first to add files which may not have a local status but have one remotely
	// Optimization : We could not run this command if we already know files are up to date
	const FString RemoteBranchSha = GetCommitShaForBranch(RemoteBranch, InPathToGitBinary, InRepositoryRoot);
	TMap<FString, FGitSourceControlState> RemoteStates;

	if(RemoteBranchSha != MergeBase)
	{
		TArray<FString> StdOut;
		TArray<FString> StdErr;
		TArray<FString> Parameters;
		Parameters.Add(MergeBase);
		Parameters.Add(RemoteBranch);
		//diff all files from the server but will only keep states that we are interested in
		//TODO: Maybe this would be better with a log and aggregating what happened as we can miss some add+delete cases with git diff
		//git log --name-status --pretty=format:"> %h %s" --reverse
		bool bRemoteStatusResult = RunCommand(TEXT("diff --name-status"), InPathToGitBinary, InRepositoryRoot, Parameters, TArray<FString>(), StdOut, StdErr);
		if(bRemoteStatusResult)
		{
			ParseNameStatusResults(InPathToGitBinary, InRepositoryRoot, StdOut, RemoteStates);

			if(bIsDirUpdate)
			{
				for(const auto& RemoteState : RemoteStates)
				{
					FilesParam.Add(RemoteState.Value.GetFilename());
				}
			}
		}
		else
		{
			return false;
		}
	}

	// Get all the locally commited diffs since merge-base
	const FString LocalBranchSha = GetCommitShaForBranch(InCommand.Branch, InPathToGitBinary, InRepositoryRoot);
	if((FilesToDiff.Num() || bIsDirUpdate) && LocalBranchSha != MergeBase)
	{
		//During directory updates we must check everything
		if(bIsDirUpdate)
			FilesToDiff.Reset();

		TArray<FString> StdOut;
		TArray<FString> StdErr;
		TArray<FString> Parameters;
		Parameters.Add(MergeBase);
		Parameters.Add(InCommand.Branch);
		bool bResult = RunCommand(TEXT("diff --name-status"), InPathToGitBinary, InRepositoryRoot, Parameters, FilesToDiff, StdOut, StdErr);
		if(bResult)
		{
			ParseNameStatusResults(InPathToGitBinary, InRepositoryRoot, StdOut, States);
		}
		else
		{
			return false;
		}
	}

	// Run regular git status to update local status
	{
		TArray<FString> Results;
		TArray<FString> ErrorMessages;
		TArray<FString> Parameters;
		//If it's a dir update we must fetch the status of all untracked files (-u) otherwise status only returns the directory to be untracked
		//Optimisation: Only run -u command with the directory parameters
		if(bIsDirUpdate)
			Parameters.Add(TEXT("-u"));

		//We no longer get the status of ignored files
		bool bResult = RunCommand(TEXT("status --porcelain"), InPathToGitBinary, InRepositoryRoot, Parameters, FilesParam, Results, ErrorMessages);
		OutErrorMessages.Append(ErrorMessages);
		if(bResult)
		{
			//Note: git status returns folders as well, when they are recently added and contain untracked files, with a not controlled status
			// Using -u allows to see the untracked files instead of the folders, but folders will still appear 
			TMap<FString, FGitSourceControlState> StatusStates;
			ParseStatusResults(InPathToGitBinary, InRepositoryRoot, FilesParam, Results, StatusStates);

			//Note: conflict here is not handled well, we assume normal operation will not generate local conflicts

			//Local states are added and combined to regular statuses
			for(auto& It : StatusStates)
			{
				FGitSourceControlState* StateResult = States.Find(It.Key);
				if(StateResult)
				{
					//Combine local state with more recent status state
					StateResult->CombineWithLocalState(It.Value);
				}
				else
				{
					States.Add(It.Key, It.Value);
				}
			}
		}
		else
		{
			return false;
		}
	}

	//Note: we are not parsing the status file for deleted files if they haven't changed on the server or locally they are not relevant for the status update
	//do not take into account checked out revision to create conflict on deletion because otherwise we can never submit the deletion!

	// Get all the saved states before applying remote diffs
	FGitSourceControlModule& GitSourceControl = FGitSourceControlModule::GetInstance();
	FGitSourceControlStatusFile& StatusFile = GitSourceControl.GetStatusFile();
	for(auto& It : States)
	{
		const FString& File = It.Value.GetFilename();
		const auto& SavedState = StatusFile.GetState(File);
		It.Value.CombineWithSavedState(SavedState, LocalBranchSha);

		//Check on disk for accurate deleted state no matter what are the conditions
		//Note: Fix for newly created assets, if something didn't have a status at all and doesn't exist on disk it's probably not deleted
		if(It.Value.WorkingCopyState != EWorkingCopyState::Unknown)
		{
			//Note: git status returns folders as well, when they are recently added and contain untracked files, with a not controlled status
			FFileStatData FileInfo = FPlatformFileManager::Get().GetPlatformFile().GetStatData(*File);
			if (!FileInfo.bIsValid)//File or folder doesn't exist
			{
				It.Value.WorkingCopyState = EWorkingCopyState::Deleted;
			}
		}
	}

	//Note: a file that has been added on remote, checked out locally, then deleted on remote will show as checked out instead of conflicted
	//This is a rare case for now not handled, as it is equivalent to adding a new file again.

	//Process remote states
	if(RemoteStates.Num())
	{
		for(auto& It : RemoteStates)
		{
			FGitSourceControlState* StateResult = States.Find(It.Key);
			if(!StateResult) //States have all been created in ParseStatusResults
				continue;

			//Note: if OldState == Deleted, we should not care about checked-out revision and always accept the conflict
			EWorkingCopyState::Type OldState = StateResult->WorkingCopyState;
			//Combine local state with "more recent" remote state
			StateResult->CombineWithRemoteState(It.Value);

			//Resolve outdated or conflict when applicable
			if(!StateResult->IsCurrent() && StateResult->CheckedOutRevision != "0")
			{
				if(StateResult->CheckedOutRevision == RemoteBranchSha)
				{
					StateResult->ResolveConflict(OldState);
				}
				else
				{
					TArray<FString> StdOut;
					TArray<FString> StdErr;
					//Get the latest revision at which the file was changed on remote
					const bool bResult = RunCommand(TEXT("log --pretty=format:\"%H\" -1 "), InPathToGitBinary, InRepositoryRoot, { RemoteBranch }, { It.Key }, StdOut, StdErr);
					if(bResult && StdOut.Num() == 1)
					{
						//Last changed revision must be an ancestor of CheckedOutReivision
						const FString& LastChangedRev = StdOut[0];
						if(GetMergeBase(LastChangedRev, StateResult->CheckedOutRevision, InPathToGitBinary, InRepositoryRoot) == LastChangedRev)
						{
							StateResult->ResolveConflict(OldState);
						}
					}
				}
			}
		}
	}

	//Process locks
	if (InCommand.bUseLocking)
	{
		TArray<FString> Results;
		TArray<FString> ErrorMessages;
		bool bResult = RunCommand(TEXT("lfs locks -r"), InPathToGitBinary, InRepositoryRoot, { InCommand.Remote }, TArray<FString>(), Results, ErrorMessages);
		OutErrorMessages.Append(ErrorMessages);
		if (bResult)
		{
			TMap<FString, FGitSourceControlState> LockStates;
			ParseLocksResults(InPathToGitBinary, InRepositoryRoot, Results, LockStates);

			//Combine Lock States
			for (auto& It : LockStates) //All locks state are locked, no need to test it here
			{
				FGitSourceControlState* StateResult = States.Find(It.Key);
				if (StateResult)
				{
					StateResult->CombineWithLockedState(It.Value);
				}
			}
		}
		else
		{
			return false;
		}
	}

	//Generate value array to return
	TArray<FGitSourceControlState> OutStatesIteration;
	States.GenerateValueArray(OutStatesIteration);
	OutStates.Append(OutStatesIteration);

	return true;
}

bool RunFetch(const FGitSourceControlCommand& InCommand)
{
	TArray<FString> StdOut;
	TArray<FString> StdErr;

	TArray<FString> Parameters;
	Parameters.Add("--quiet");
	Parameters.Add(InCommand.Remote);
	Parameters.Add(InCommand.Branch);

	return GitSourceControlUtils::RunCommand(TEXT("fetch"), InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot, Parameters, TArray<FString>(), StdOut, StdErr);
}

// Run a Git show command to dump the binary content of a revision into a file.
bool RunDumpToFile(const FString& InPathToGitBinary, const FString& InRepositoryRoot, const FString& InFile, const FString& InCommit, const FString& InDumpFileName)
{
	//If the file is tracked by LFS, we must fetch the real object and not the placeholder file
	bool bIsLFSTracked = false;

	{
		TArray<FString> StdOut;
		TArray<FString> StdErr;
		bool bSuccess = GitSourceControlUtils::RunCommand(TEXT("check-attr filter"), InPathToGitBinary, InRepositoryRoot, TArray<FString>(), { InFile }, StdOut, StdErr);
		if(bSuccess && StdOut.Num() == 1 && StdOut[0].EndsWith(TEXT("lfs")))
		{
			bIsLFSTracked = true;
		}
	}

	bool bResult = false;
	FString FullCommand;

	if(!InRepositoryRoot.IsEmpty())
	{
		FullCommand = TEXT("-C \"");
		FullCommand += InRepositoryRoot;
		FullCommand += TEXT("\" ");
	}

	// then the git command itself
	FullCommand += TEXT("show ");

	// Append to the command the parameter
	FullCommand += InCommit;
	FullCommand += ":";
	FullCommand += InFile;

	const bool bLaunchDetached = false;
	const bool bLaunchHidden = true;
	const bool bLaunchReallyHidden = bLaunchHidden;

	void* PipeRead = nullptr;
	void* PipeWrite = nullptr;

	verify(FPlatformProcess::CreatePipe(PipeRead, PipeWrite));

	FProcHandle ProcessHandle = FPlatformProcess::CreateProc(*InPathToGitBinary, *FullCommand, bLaunchDetached, bLaunchHidden, bLaunchReallyHidden, nullptr, 0, nullptr, PipeWrite);
	if(ProcessHandle.IsValid())
	{
		FPlatformProcess::Sleep(0.01);

		TArray<uint8> BinaryFileContent;
		{
			while(FPlatformProcess::IsProcRunning(ProcessHandle))
			{
				TArray<uint8> BinaryData;
				FPlatformProcess::ReadPipeToArray(PipeRead, BinaryData);
				if(BinaryData.Num() > 0)
				{
					BinaryFileContent.Append(MoveTemp(BinaryData));
				}
			}

			TArray<uint8> BinaryData;
			FPlatformProcess::ReadPipeToArray(PipeRead, BinaryData);
			if(BinaryData.Num() > 0)
			{
				BinaryFileContent.Append(MoveTemp(BinaryData));
			}
		}

		int32 ReturnCode = -1;
		FPlatformProcess::GetProcReturnCode(ProcessHandle, &ReturnCode);
		bResult = ReturnCode == 0;

		//pipe through lfs smudge to get the real binary file
		//Note: another approach is to use the "cat-file --filters" command, only available on newer than git 2.9.3
		if(bResult && bIsLFSTracked)
		{
			FString LfsSmudgeCommand;

			if(!InRepositoryRoot.IsEmpty())
			{
				LfsSmudgeCommand = TEXT("-C \"");
				LfsSmudgeCommand += InRepositoryRoot;
				LfsSmudgeCommand += TEXT("\" ");
			}

			LfsSmudgeCommand += TEXT("lfs smudge");

			// For writing to child process
			void* ReadPipeChild = nullptr;
			void* WritePipeParent = nullptr;
			verify(CreatePipeWrite(ReadPipeChild, WritePipeParent));

			// For reading from child process
			void* ReadPipeParent = nullptr;
			void* WritePipeChild = nullptr;
			verify(FPlatformProcess::CreatePipe(ReadPipeParent, WritePipeChild));

			FString Written;
			const FString LfsPointer = FString(BinaryFileContent.Num(), UTF8_TO_TCHAR(BinaryFileContent.GetData()));

			FProcHandle LFSProcessHandle = FPlatformProcess::CreateProc(*InPathToGitBinary, *LfsSmudgeCommand, bLaunchDetached, bLaunchHidden, bLaunchReallyHidden, nullptr, 0, nullptr, WritePipeChild, ReadPipeChild);
			if(LFSProcessHandle.IsValid())
			{
				FPlatformProcess::Sleep(0.01);

				FPlatformProcess::WritePipe(WritePipeParent, LfsPointer, &Written);

				BinaryFileContent.Reset();

				while(FPlatformProcess::IsProcRunning(LFSProcessHandle))
				{
					TArray<uint8> BinaryData;
					FPlatformProcess::ReadPipeToArray(ReadPipeParent, BinaryData);
					if(BinaryData.Num() > 0)
					{
						BinaryFileContent.Append(MoveTemp(BinaryData));
					}
				}

				TArray<uint8> BinaryData;
				FPlatformProcess::ReadPipeToArray(ReadPipeParent, BinaryData);
				if(BinaryData.Num() > 0)
				{
					BinaryFileContent.Append(MoveTemp(BinaryData));
				}
			}

			int32 LFSReturnCode = -1;
			FPlatformProcess::GetProcReturnCode(ProcessHandle, &LFSReturnCode);

			bResult = ReturnCode == 0;

			FPlatformProcess::ClosePipe(ReadPipeParent, WritePipeChild);
			FPlatformProcess::ClosePipe(ReadPipeChild, WritePipeParent);
			FPlatformProcess::CloseProc(LFSProcessHandle);
		}

		// Save buffer into temp file
		if(bResult)
		{
			if(FFileHelper::SaveArrayToFile(BinaryFileContent, *InDumpFileName))
			{
				GITCENTRAL_LOG(TEXT("Wrote '%s' (%do)"), *InDumpFileName, BinaryFileContent.Num());
			}
			else
			{
				GITCENTRAL_ERROR(TEXT("Could not write %s"), *InDumpFileName);
				bResult = false;
			}
		}
	}
	
	if(!bResult)
	{
		GITCENTRAL_ERROR(TEXT("Failed to get file revision: %s:%s"), *InFile, *InCommit);
	}

	FPlatformProcess::ClosePipe(PipeRead, PipeWrite);
	FPlatformProcess::CloseProc(ProcessHandle);

	return bResult;
}


/**
* Extract and interpret the file state from the given Git log --name-status.
* @see https://www.kernel.org/pub/software/scm/git/docs/git-log.html
* ' ' = unchanged
* 'M' = modified
* 'A' = added
* 'D' = deleted
* 'R' = renamed
* 'C' = copied
* 'T' = type changed
* 'U' = updated but unmerged
* 'X' = unknown
* 'B' = broken pairing
*/
static FString LogStatusToString(TCHAR InStatus)
{
	switch(InStatus)
	{
	case TEXT(' '):
		return FString("unchanged");
	case TEXT('M'):
		return FString("modified");
	case TEXT('A'):
		return FString("added");
	case TEXT('D'):
		return FString("deleted");
	case TEXT('R'):
		return FString("renamed");
	case TEXT('C'):
		return FString("copied");
	case TEXT('T'):
		return FString("type changed");
	case TEXT('U'):
		return FString("unmerged");
	case TEXT('X'):
		return FString("unknown");
	case TEXT('B'):
		return FString("broked pairing");
	}

	return FString();
}

/**
 * Parse the array of strings results of a 'git log' command
 *
 * Example git log results:
commit 97a4e7626681895e073aaefd68b8ac087db81b0b
Author: Sébastien Rombauts <sebastien.rombauts@gmail.com>
Date:   2014-2015-05-15 21:32:27 +0200

    Another commit used to test History

     - with many lines
     - some <xml>
     - and strange characteres $*+

M	Content/Blueprints/Blueprint_CeilingLight.uasset
R100	Content/Textures/T_Concrete_Poured_D.uasset Content/Textures/T_Concrete_Poured_D2.uasset

commit 355f0df26ebd3888adbb558fd42bb8bd3e565000
Author: Sébastien Rombauts <sebastien.rombauts@gmail.com>
Date:   2014-2015-05-12 11:28:14 +0200

    Testing git status, edit, and revert

A	Content/Blueprints/Blueprint_CeilingLight.uasset
C099	Content/Textures/T_Concrete_Poured_N.uasset Content/Textures/T_Concrete_Poured_N2.uasset
*/
static void ParseLogResults(const TArray<FString>& InResults, TGitSourceControlHistory& OutHistory)
{
	TSharedRef<FGitSourceControlRevision, ESPMode::ThreadSafe> SourceControlRevision = MakeShared<FGitSourceControlRevision, ESPMode::ThreadSafe>();
	for(const auto& Result : InResults)
	{
		if(Result.StartsWith(TEXT("commit "))) // Start of a new commit
		{
			// End of the previous commit
			if(SourceControlRevision->RevisionNumber != 0)
			{
				OutHistory.Add(SourceControlRevision);

				SourceControlRevision = MakeShared<FGitSourceControlRevision, ESPMode::ThreadSafe>();
			}
			SourceControlRevision->CommitId = Result.RightChop(7); // Full commit SHA1 hexadecimal string
			SourceControlRevision->ShortCommitId = SourceControlRevision->CommitId.Left(8); // Short revision ; first 8 hex characters (max that can hold a 32 bit integer)
			SourceControlRevision->RevisionNumber = FParse::HexNumber(*SourceControlRevision->ShortCommitId);
		}
		else if(Result.StartsWith(TEXT("Author: "))) // Author name & email
		{
			// Remove the 'email' part of the UserName
			FString UserNameEmail = Result.RightChop(8);
			int32 EmailIndex = 0;
			if(UserNameEmail.FindLastChar('<', EmailIndex))
			{
				SourceControlRevision->UserName = UserNameEmail.Left(EmailIndex - 1);
			}
		}
		else if(Result.StartsWith(TEXT("Date:   "))) // Commit date
		{
			FString Date = Result.RightChop(8);
			SourceControlRevision->Date = FDateTime::FromUnixTimestamp(FCString::Atoi(*Date));
		}
	//	else if(Result.IsEmpty()) // empty line before/after commit message has already been taken care by FString::ParseIntoArray()
		else if(Result.StartsWith(TEXT("    ")))  // Multi-lines commit message
		{
			SourceControlRevision->Description += Result.RightChop(4);
			SourceControlRevision->Description += TEXT("\n");
		}
		else // Name of the file, starting with an uppercase status letter ("A"/"M"...)
		{
			const TCHAR Status = Result[0];
			SourceControlRevision->Action = LogStatusToString(Status); // Readable action string ("Added", Modified"...) instead of "A"/"M"...
			// Take care of special case for Renamed/Copied file: extract the second filename after second tabulation
			int32 IdxTab;
			if(Result.FindLastChar('\t', IdxTab))
			{
				SourceControlRevision->Filename = Result.RightChop(IdxTab + 1); // relative filename
			}
		}
	}
	// End of the last commit
	if(SourceControlRevision->RevisionNumber != 0)
	{
		OutHistory.Add(SourceControlRevision);
	}
}


/**
 * Extract the SHA1 identifier and size of a blob (file) from a Git "ls-tree" command.
 *
 * Example output for the command git ls-tree --long 7fdaeb2 Content/Blueprints/BP_Test.uasset
100644 blob a14347dc3b589b78fb19ba62a7e3982f343718bc   70731	Content/Blueprints/BP_Test.uasset
*/
class FGitLsTreeParser
{
public:
	/** Parse the unmerge status: extract the base SHA1 identifier of the file */
	FGitLsTreeParser(const TArray<FString>& InResults)
	{
		const FString& FirstResult = InResults[0];
		FileHash = FirstResult.Mid(12, 40);
		int32 IdxTab;
		if(FirstResult.FindChar('\t', IdxTab))
		{
			const FString SizeString = FirstResult.Mid(53, IdxTab - 53);
			FileSize = FCString::Atoi(*SizeString);
		}
	}

	FString FileHash;	/// SHA1 Id of the file (warning: not the commit Id)
	int32	FileSize;	/// Size of the file (in bytes)
};

// Run a Git "log" command and parse it.
bool RunGetHistory(const FString& InPathToGitBinary, const FString& InRepositoryRoot, const FString& InBranch, const FString& InFile, TArray<FString>& OutErrorMessages, TGitSourceControlHistory& OutHistory)
{
	bool bResults;
	{
		TArray<FString> Results;
		TArray<FString> Parameters;
		Parameters.Add(TEXT("--max-count 100"));
		Parameters.Add(TEXT("--follow")); // follow file renames
		Parameters.Add(TEXT("--date=raw"));
		Parameters.Add(TEXT("--name-status")); // relative filename at this revision, preceded by a status character
		Parameters.Add(TEXT("--pretty=medium")); // make sure format matches expected in ParseLogResults
		Parameters.Add(InBranch);

		TArray<FString> Files;
		Files.Add(*InFile);

		bResults = RunCommand(TEXT("log"), InPathToGitBinary, InRepositoryRoot, Parameters, Files, Results, OutErrorMessages);
		if(bResults)
		{
			ParseLogResults(Results, OutHistory);
		}
	}

	//ls-tree is only useful for file size but it fails with this message, skipping this for now
	//[2017.03.18 - 17.25.02:003][732]LogSourceControl: ExecProcess: 'git ls-tree --long afd3ec36 "Test/some_asset.uasset"'
	//[2017.03.18 - 17.25.03:330][732]LogSourceControl : ExecProcess : ReturnCode = 128 OutResults = ''
	//[2017.03.18 - 17.25.03:330][732]LogSourceControl : ExecProcess : ReturnCode = 128 OutErrors = 'fatal: Cannot change to 'Test': No such file or directory
	/*
	for(auto& Revision : OutHistory)
	{
		// Get file (blob) sha1 id and size
		TArray<FString> Results;
		TArray<FString> Parameters;
		Parameters.Add(TEXT("--long")); // Show object size of blob (file) entries.
		Parameters.Add(Revision->GetRevision());
		TArray<FString> Files;
		Files.Add(*Revision->GetFilename());
		bResults &= RunCommand(TEXT("ls-tree"), InPathToGitBinary, InRepositoryRoot, Parameters, Files, Results, OutErrorMessages);
		if(bResults && Results.Num())
		{
			FGitLsTreeParser LsTree(Results);
			Revision->FileHash = LsTree.FileHash;
			Revision->FileSize = LsTree.FileSize;
		}
	}
	*/

	return bResults;
}

bool RunSyncFile(const FString& InPathToGitBinary, const FString& InRepositoryRoot, const FString& InFile, const FString& InRevision, TArray<FString>& OutErrorMessages, EWorkingCopyState::Type OverrideSavedState /*= EWorkingCopyState::Unknown*/)
{
	TArray<FString> Files;
	Files.Add(InFile);

	TArray<FString> StdOut;
	bool bResult = false;

	//Note: git checkout needs files that exist
	//untracked files will produce an error even when against head revision
	//files that have been deleted in the depot at the specified revision

	//git checkout will get the file at the correct revision but will leave it in the index
	{
		TArray<FString> Parameters;
		TArray<FString> StdErr;
		if(InRevision != "0")
			Parameters.Add(InRevision);
		else
			Parameters.Add("HEAD");
		Parameters.Add("-f --");
		bResult = RunCommand(TEXT("checkout"), InPathToGitBinary, InRepositoryRoot, Parameters, Files, StdOut, StdErr);

		//remove file from the index and place it into the working tree
		//In case of failure, in particular when "failed to unlink" i.e. failed to write the file, it will place the change in the index instead
		//Run git reset unconditionally in order for the command to proceed properly.
		bResult &= RunCommand(TEXT("reset"), InPathToGitBinary, InRepositoryRoot, TArray<FString>(), Files, StdOut, OutErrorMessages);
		
		if(!bResult && StdErr.Num() == 1 && StdErr[0].Contains("did not match any file(s) known to git")) // file does not exist at this revision
		{
			//Remove the file, this may also happen when reverting (syncing) locally added files
			bResult = !FPaths::FileExists(InFile) || FPlatformFileManager::Get().GetPlatformFile().DeleteFile(*InFile);
			if(!bResult)
			{
				OutErrorMessages.Add(*FString::Printf(TEXT("Failed to delete file: %s"), *InFile));
			}
			else
			{
				OverrideSavedState = EWorkingCopyState::Deleted;
			}
		}
		else
		{
			OutErrorMessages.Append(StdErr);
		}

	}
	
	if(bResult)
	{
		//update saved status' revision
		FGitSourceControlModule& GitSourceControl = FGitSourceControlModule::GetInstance();
		FGitSourceControlStatusFile& StatusFile = GitSourceControl.GetStatusFile();

		SavedState State = StatusFile.GetState(InFile);
		State.CheckedOutRevision = InRevision;
		if(OverrideSavedState != EWorkingCopyState::Unknown)
			State.State = OverrideSavedState;
		StatusFile.SetState(InFile, State, InRepositoryRoot);
	}

	return bResult;
}

bool RunLockFiles(const FString& InPathToGitBinary, const FString& InRepositoryRoot, const FString& InRemote, const TArray<FString>& InFiles, TArray<FString>& OutErrorMessages)
{
	FString FixedPathToRepoRoot(InRepositoryRoot);
	if (!FixedPathToRepoRoot.EndsWith("/"))
		FixedPathToRepoRoot += '/';

	bool bResult = true;
	TArray<FString> StdOut;
	for (const FString& File : InFiles)
	{
		StdOut.Reset();

		//lock and unlock are the only commands where absolute paths produce errors
		FString RelativePath(File);
		FPaths::MakePathRelativeTo(RelativePath, *FixedPathToRepoRoot);

		bResult &= RunCommand(TEXT("lfs lock -r"), InPathToGitBinary, InRepositoryRoot, { InRemote }, { RelativePath }, StdOut, OutErrorMessages);
	}

	return bResult;
}

bool RunUnlockFiles(const FString& InPathToGitBinary, const FString& InRepositoryRoot, const FString& InRemote, const TArray<FString>& InFiles, TArray<FString>& OutErrorMessages, bool bForce /*= false*/)
{
	FString FixedPathToRepoRoot(InRepositoryRoot);
	if (!FixedPathToRepoRoot.EndsWith("/"))
		FixedPathToRepoRoot += '/';

	TArray<FString> StdOut;
	TArray<FString> StdErr;
	TArray<FString> Parameters;
	Parameters.Add(InRemote);
	if (bForce)
		Parameters.Add(TEXT("--force"));

	bool bErrors = false;

	for(const FString& File : InFiles)
	{
		StdOut.Reset();
		StdErr.Reset();

		//lock and unlock are the only commands where absolute paths produce errors
		FString RelativePath(File);
		FPaths::MakePathRelativeTo(RelativePath, *FixedPathToRepoRoot);

		bool bResult = RunCommand(TEXT("lfs unlock -r"), InPathToGitBinary, InRepositoryRoot, Parameters, { RelativePath }, StdOut, StdErr);

		//Note: Output when the file is locked by other:
		//<file> is locked by <user>

		//Treat special case where file does not exist
		if (!bResult && !FPaths::FileExists(File))
		{
			//Error File not found (Os dependent error message)
			//Handling this using the lock id works but still outputs errors

			FGitSourceControlModule& GitSourceControl = FGitSourceControlModule::GetInstance();
			FGitSourceControlProvider& Provider = GitSourceControl.GetProvider();
			TSharedPtr<ISourceControlState, ESPMode::ThreadSafe> State = ((ISourceControlProvider*)&Provider)->GetState(File, EStateCacheUsage::Use);
			if(State)
			{
				FGitSourceControlState* GitState = (FGitSourceControlState*)State.Get();

				//Note this should only happen if the local state is deleted ?
				if (GitState->HasValidLockId() && !State->IsCheckedOutOther())//can only force unlock if locked by self
				{
					StdErr.Reset();

					TArray<FString> UnlockByIdParams = Parameters;
					UnlockByIdParams.Add(FString::Printf(TEXT("-i %d"), GitState->LockId));

					bResult = RunCommand(TEXT("lfs unlock -r"), InPathToGitBinary, InRepositoryRoot, UnlockByIdParams, TArray<FString>(), StdOut, StdErr);

					//This will error but the lock will have been successfully removed
					if (!bResult && StdErr.Num() == 1)
					{
						bResult = true;
						StdErr.Reset();
					}
				}
			}
		}

		//Other possible errors
		if (StdErr.Num() == 1)
		{
			if (StdErr[0].Contains(TEXT("Unable to get lock id")))
			{
				//Trying to unlock a file that is not locked will result in an error, such as: 
				//Unable to get lock id: lfs: no matching locks found

				//Just skip this error
				bResult = true;
				StdErr.Reset();
			}
			else if (StdErr[0].Contains(TEXT("Cannot unlock file with uncommitted changes")) && !bForce)
			{
				//Happens if newly added files were locked or files not at the head revision
				//We must use force unlock in this case
				FGitSourceControlModule& GitSourceControl = FGitSourceControlModule::GetInstance();
				FGitSourceControlProvider& Provider = GitSourceControl.GetProvider();
				TSharedPtr<ISourceControlState, ESPMode::ThreadSafe> State = ((ISourceControlProvider*)&Provider)->GetState(File, EStateCacheUsage::Use);
				check(State);
				if (!State->IsCheckedOutOther())//can only force unlock if locked by self
				{
					StdErr.Reset();
					bResult = RunUnlockFiles(InPathToGitBinary, InRepositoryRoot, InRemote, { File }, StdErr, true);
				}
			}
			else if (bResult && StdErr[0].Contains(TEXT("unlocking with uncommitted changes because --force")))
			{
				StdErr.Reset();
			}
		}

		if(StdErr.Num() > 0)
		{
			bErrors = true;
			OutErrorMessages.Append(StdErr);
		}
	}

	return !bErrors;
}

bool UpdateCachedStates(const TArray<FGitSourceControlState>& InStates)
{
	FGitSourceControlModule& GitSourceControl = FGitSourceControlModule::GetInstance();
	FGitSourceControlProvider& Provider = GitSourceControl.GetProvider();
	int NbStatesUpdated = 0;

	for(const auto& InState : InStates)
	{
		TSharedRef<FGitSourceControlState, ESPMode::ThreadSafe> State = Provider.GetStateInternal(InState.AbsoluteFilename);
		if(State.Get() != InState)
		{
			auto History = MoveTemp(State->History);
			*State = InState;
			State->TimeStamp = FDateTime::Now();
			State->History = MoveTemp(History);
			NbStatesUpdated++;
		}
	}

	return (NbStatesUpdated > 0);
}

/**
 * Helper struct for RemoveRedundantErrors()
 */
struct FRemoveRedundantErrors
{
	FRemoveRedundantErrors(const FString& InFilter)
		: Filter(InFilter)
	{
	}

	bool operator()(const FString& String) const
	{
		if(String.Contains(Filter))
		{
			return true;
		}

		return false;
	}

	/** The filter string we try to identify in the reported error */
	FString Filter;
};

void RemoveRedundantErrors(FGitSourceControlCommand& InCommand, const FString& InFilter)
{
	bool bFoundRedundantError = false;
	for(auto Iter(InCommand.ErrorMessages.CreateConstIterator()); Iter; Iter++)
	{
		if(Iter->Contains(InFilter))
		{
			InCommand.InfoMessages.Add(*Iter);
			bFoundRedundantError = true;
		}
	}

	InCommand.ErrorMessages.RemoveAll( FRemoveRedundantErrors(InFilter) );

	// if we have no error messages now, assume success!
	if(bFoundRedundantError && InCommand.ErrorMessages.Num() == 0 && !InCommand.bCommandSuccessful)
	{
		InCommand.bCommandSuccessful = true;
	}
}

GitIndexState RunCheckIndexValid(FGitSourceControlCommand& InCommand)
{
	//Run git status and check for conflicts
	TArray<FString> Results;
	TArray<FString> ErrorMessages;
	bool bResult = RunCommand(TEXT("status --porcelain"), InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot, TArray<FString>(), TArray<FString>(), Results, InCommand.ErrorMessages);
	if(bResult)
	{
		TMap<FString, FGitSourceControlState> StatusStates;
		ParseStatusResults(InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot, TArray<FString>(), Results, StatusStates);

		for(auto& It : StatusStates)
		{
			if(It.Value.IsConflicted())
			{
				return GitIndexState::MustResolveConflicts;
			}
			else if(It.Value.bStaged)
			{
				return GitIndexState::Invalid;
			}
		}
		return GitIndexState::Valid;
	}
	else
	{
		return GitIndexState::Invalid;
	}
}

void CleanupStatusFile(FGitSourceControlCommand& InCommand)
{
	FGitSourceControlModule& GitSourceControl = FGitSourceControlModule::GetInstance();
	FGitSourceControlStatusFile& StatusFile = GitSourceControl.GetStatusFile();

	//Save state in case something goes wrong
	StatusFile.CacheStates();

	//_Copy_ all the saved states as we will delete some during iteration
	const auto SavedStates = StatusFile.GetAllStates();

	if(SavedStates.Num() == 0)
		return;

	bool Success = true;

	FString MergeBase;
	const FString RemoteBranch = InCommand.GetRemoteBranch();

	//Find merge base for local branch and remote
	{
		MergeBase = GetMergeBase(InCommand.Branch, RemoteBranch, InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot);
		if(MergeBase.IsEmpty())
		{
			Success = false;
		}
	}

	//Get all diffs from the remote, relevant for deleted files
	const FString RemoteBranchSha = GetCommitShaForBranch(RemoteBranch, InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot);
	TMap<FString, FGitSourceControlState> RemoteStates;

	if(Success && RemoteBranchSha != MergeBase)
	{
		TArray<FString> Files;
		Files.Reserve(SavedStates.Num());

		for(auto It : SavedStates)
		{
			Files.Add(It.Key);
		}

		TArray<FString> StdOut;
		TArray<FString> StdErr;
		TArray<FString> Parameters;
		Parameters.Add(MergeBase);
		Parameters.Add(RemoteBranch);
		Success = RunCommand(TEXT("diff --name-status"), InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot, Parameters, Files, StdOut, StdErr);
		if(Success)
		{
			ParseNameStatusResults(InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot, StdOut, RemoteStates);
		}
	}

	if(Success)
	{
		TArray<FString> StdOut;
		TArray<FString> StdErr;

		for(auto It : SavedStates)
		{
			bool bClearState = false;

			if(It.Value.State == EWorkingCopyState::Deleted && !FPaths::FileExists(It.Key)) //Test if the deletion is still relevant
			{
				auto RemoteState = RemoteStates.Find(It.Key);

				//If the file is not present on remote, it is no longer relevant
				bClearState = RemoteState == nullptr;
				if(!bClearState)
				{
					//If the file has been deleted on remote, it is no longer relevant
					bClearState = RemoteState->WorkingCopyState == EWorkingCopyState::Deleted;
				}
			}
			else if(EWorkingCopyState::ToChar(It.Value.State) == '0' && It.Value.CheckedOutRevision != "0") // If the file is unchanged
			{
				//If CheckedOutRevision is the merge base or an ancestor, it is no longer relevant
				bClearState = It.Value.CheckedOutRevision == MergeBase;

				if(!bClearState)
				{
					bClearState = RunCommand(TEXT("merge-base --is-ancestor"),
						InCommand.PathToGitBinary, InCommand.PathToRepositoryRoot, { It.Value.CheckedOutRevision, MergeBase }, TArray<FString>(), StdOut, StdErr);
				}
			}

			if(bClearState)
				StatusFile.ClearState(It.Key, InCommand.PathToRepositoryRoot, true);
		}
	}

	if(Success)
		Success = StatusFile.Save(InCommand.PathToRepositoryRoot);

	if(!Success)
		StatusFile.RestoreCachedStates();
}


void MakeWriteable(const TArray<FString>& InFiles)
{
	for(const auto& File : InFiles)
	{
		MakeWriteable(File);
	}
}

void MakeWriteable(const FString& File)
{
	FFileStatData FileInfo = FPlatformFileManager::Get().GetPlatformFile().GetStatData(*File);
	if (FileInfo.bIsValid && !FileInfo.bIsDirectory && FileInfo.bIsReadOnly)
	{
		FPlatformFileManager::Get().GetPlatformFile().SetReadOnly(*File, false);
	}
}

}

