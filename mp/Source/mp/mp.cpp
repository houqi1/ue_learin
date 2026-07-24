// Copyright Epic Games, Inc. All Rights Reserved.

#include "mp.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/Paths.h"
#include "ShaderCore.h"

DEFINE_LOG_CATEGORY_STATIC(LogMpShaders, Log, All);

void FmpModule::StartupModule()
{
	// Map project Shaders/ for Material Custom #include "/Project/....usf"
	// AddShaderSourceDirectoryMapping asserts if the virtual path is registered twice
	// (module reload / Live Coding / another system already mapping /Project).
	static const FString VirtualShaderDir(TEXT("/Project"));
	const FString ShaderDir = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectDir(), TEXT("Shaders")));

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (!PlatformFile.DirectoryExists(*ShaderDir))
	{
		PlatformFile.CreateDirectoryTree(*ShaderDir);
	}

	const TMap<FString, FString>& ExistingMappings = AllShaderSourceDirectoryMappings();
	if (const FString* ExistingRealDir = ExistingMappings.Find(VirtualShaderDir))
	{
		UE_LOG(LogMpShaders, Log,
			TEXT("Shader mapping /Project already exists -> %s (skip AddShaderSourceDirectoryMapping)"),
			**ExistingRealDir);
		return;
	}

	AddShaderSourceDirectoryMapping(VirtualShaderDir, ShaderDir);
	UE_LOG(LogMpShaders, Log, TEXT("Shader source mapped: /Project -> %s"), *ShaderDir);
}

void FmpModule::ShutdownModule()
{
}

IMPLEMENT_PRIMARY_GAME_MODULE(FmpModule, mp, "mp");
