// Copyright (c) 2017-2020 Samuel Kahn (samuel@kahncode.com). All Rights Reserved.

using System;
using System.IO;
using UnrealBuildTool;

public class GitCentral : ModuleRules
{
	public GitCentral(ReadOnlyTargetRules Target)
        : base(Target)
    {
        Console.WriteLine("GitCentral.Build.cs: {0} {1} {2} {3}", Target.Name, Target.Type.ToString(), Target.Configuration.ToString(), Target.Platform.ToString());

        //RunUAT BuildPlugin always builds with NoSharedPCH and IWYU anyway
        PCHUsage = ModuleRules.PCHUsageMode.NoSharedPCHs;
        PrivatePCHHeaderFile = "Private/GitSourceControlPrivatePCH.h";
        bEnforceIWYU = true;

#if UE_4_24_OR_LATER
        bLegacyPublicIncludePaths = false;
        ShadowVariableWarningLevel = WarningLevel.Error;
#endif
        

    PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"Slate",
				"SlateCore",
				"EditorStyle",
				"SourceControl",
				"InputCore",
                "UnrealEd",
                "CoreUObject",
                "Engine",
                "Json"
			}
		);
	}
}
