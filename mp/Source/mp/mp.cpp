// Copyright Epic Games, Inc. All Rights Reserved.

#include "mp.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/Paths.h"
#include "ShaderCore.h"

DEFINE_LOG_CATEGORY_STATIC(LogMpShaders, Log, All);

void FmpModule::StartupModule()
{
	// Step 1: map project Shaders/ to virtual path for Material Custom #include
	// Usage later: #include "/Project/YourShader.usf"
	const FString ShaderDir = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectDir(), TEXT("Shaders")));

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (!PlatformFile.DirectoryExists(*ShaderDir))
	{
		PlatformFile.CreateDirectoryTree(*ShaderDir);
	}

	AddShaderSourceDirectoryMapping(TEXT("/Project"), ShaderDir);

	UE_LOG(LogMpShaders, Log, TEXT("Shader source mapped: /Project -> %s"), *ShaderDir);
}

void FmpModule::ShutdownModule()
{
}

IMPLEMENT_PRIMARY_GAME_MODULE(FmpModule, mp, "mp");
