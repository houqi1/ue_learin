// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class mp : ModuleRules
{
	public mp(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
	
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"RenderCore",
			"RHI",
			"Renderer"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });

		// FPostProcessingInputs / lit SceneColor access for glass dual-pass PrePostProcess.
		PrivateIncludePaths.Add(Path.Combine(EngineDirectory, "Source/Runtime/Renderer/Internal"));
	}
}
