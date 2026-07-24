// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

/**
 * Game module: registers /Project -> <ProjectDir>/Shaders for Material Custom + global shaders.
 * LoadingPhase must be PostConfigInit so IMPLEMENT_GLOBAL_SHADER types register before shader lock.
 */
class FmpModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
