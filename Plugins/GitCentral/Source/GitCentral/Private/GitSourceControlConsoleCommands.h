#pragma once

#include "CoreMinimal.h"

/**
 * Git source control editor commands
 */
class GitSourceControlConsoleCommands
{
public:
	static void PrintStatus(const TArray<FString>& Args);
	static void PrintStatusCache();
};
