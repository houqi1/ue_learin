// Copyright Epic Games, Inc. All Rights Reserved.
// Global shaders for glass dual-pass Step3 (static stream + SkinCache SRV).

#pragma once

#include "GlobalShader.h"
#include "ShaderParameterStruct.h"

class FGlassBackfaceVS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FGlassBackfaceVS);
	SHADER_USE_PARAMETER_STRUCT(FGlassBackfaceVS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FMatrix44f, LocalToWorld)
		SHADER_PARAMETER(FMatrix44f, ViewProjection)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

/** Skeletal: sample GPU SkinCache positions via Buffer<float> (FCachedGeometry). */
class FGlassBackfaceSkinCacheVS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FGlassBackfaceSkinCacheVS);
	SHADER_USE_PARAMETER_STRUCT(FGlassBackfaceSkinCacheVS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FMatrix44f, LocalToWorld)
		SHADER_PARAMETER(FMatrix44f, ViewProjection)
		SHADER_PARAMETER_SRV(Buffer<float>, PositionSRV)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

class FGlassBackfacePS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FGlassBackfacePS);
	SHADER_USE_PARAMETER_STRUCT(FGlassBackfacePS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FVector4f, DebugColor)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

FRHIVertexDeclaration* GetGlassBackfaceVertexDeclarationRHI();
