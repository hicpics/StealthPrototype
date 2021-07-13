#include "GitSourceControlConsoleCommands.h"

#include "GitSourceControlModule.h"
#include "GitSourceControlState.h"

namespace Private_GitSourceControlCommands
{
	// Auto-registered console commands:
	// No re-register on hot reload, and unregistered only once on editor shutdown.

	//TODO: Add these potentially useful commands
	/*static FAutoConsoleCommand g_cmdExecute(TEXT("gitcentral.ExecuteCommand"),
		TEXT("Executes a source control command. See the whole list using gitcentral.Help")
		TEXT("gitcentral.ExecuteCommand <command_name> [Paths...]"),
		FConsoleCommandWithArgsDelegate::CreateStatic(&GitSourceControlConsoleCommands::ExecuteCommand), ECVF_Cheat);

	static FAutoConsoleCommand g_cmdHelp(TEXT("gitcentral.Help"),
		TEXT("Prints all available commands from GitCentral"),
		FConsoleCommandDelegate::CreateStatic(&GitSourceControlConsoleCommands::Help), ECVF_Cheat);*/

	static FAutoConsoleCommand g_cmdPrintStatus(TEXT("gitcentral.PrintStatus"),
		TEXT("Prints the internal status of a file path or directory, useful for debugging")
		TEXT("gitcentral.PrintStatus [Paths...]"),
		FConsoleCommandWithArgsDelegate::CreateStatic(&GitSourceControlConsoleCommands::PrintStatus), ECVF_Cheat);

	static FAutoConsoleCommand g_cmdPrintStatusCache(TEXT("gitcentral.PrintStatusCache"),
		TEXT("Prints the internal status of all known files"),
		FConsoleCommandDelegate::CreateStatic(&GitSourceControlConsoleCommands::PrintStatusCache), ECVF_Cheat);

} // namespace Private_DatabaseCommands

void GitSourceControlConsoleCommands::PrintStatus(const TArray<FString>& Args)
{
	if (Args.Num() == 0)
	{
		GITCENTRAL_ERROR(TEXT("PrintStatus: Must provide paths to print status for"));
	}

	FGitSourceControlModule& Module = FGitSourceControlModule::GetInstance();
	FGitSourceControlProvider& Provider = Module.GetProvider();

	for (const auto& Arg : Args)
	{
		const auto State = Provider.GetStateInternal(Arg);
		State->DebugPrint();
	}
}

void GitSourceControlConsoleCommands::PrintStatusCache()
{
	FGitSourceControlModule& Module = FGitSourceControlModule::GetInstance();
	FGitSourceControlProvider& Provider = Module.GetProvider();

	const auto& StateCache = Provider.GetAllStatesInternal();
	for (const auto& State : StateCache)
	{
		State.Value->DebugPrint();
	}
}
