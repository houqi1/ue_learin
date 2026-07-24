// Copyright Epic Games, Inc. All Rights Reserved.
// Global shaders: glass backface (static + SkinCache) with bake-map shading.

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
		SHADER_PARAMETER(FMatrix44f, LocalToWorldInverseTranspose)
		SHADER_PARAMETER(FMatrix44f, ViewProjection)
		SHADER_PARAMETER_SRV(Buffer<float4>, TangentSRV)
		SHADER_PARAMETER_SRV(Buffer<float2>, UVSRV)
		SHADER_PARAMETER(uint32, NumTexCoords)
		SHADER_PARAMETER(uint32, TangentFormat)
		SHADER_PARAMETER(uint32, bHasTangents)
		SHADER_PARAMETER(uint32, bHasUVs)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

class FGlassBackfaceSkinCacheVS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FGlassBackfaceSkinCacheVS);
	SHADER_USE_PARAMETER_STRUCT(FGlassBackfaceSkinCacheVS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FMatrix44f, LocalToWorld)
		SHADER_PARAMETER(FMatrix44f, LocalToWorldInverseTranspose)
		SHADER_PARAMETER(FMatrix44f, ViewProjection)
		SHADER_PARAMETER_SRV(Buffer<float>, PositionSRV)
		SHADER_PARAMETER_SRV(Buffer<float4>, TangentSRV)
		SHADER_PARAMETER_SRV(Buffer<float2>, UVSRV)
		SHADER_PARAMETER(uint32, NumTexCoords)
		SHADER_PARAMETER(uint32, TangentFormat)
		SHADER_PARAMETER(uint32, bHasTangents)
		SHADER_PARAMETER(uint32, bHasUVs)
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
		SHADER_PARAMETER(FMatrix44f, ViewProjection)
		SHADER_PARAMETER(FVector3f, CameraWorldPos)
		// SceneColor buffer mapping (UE ViewportUV → BufferUV)
		SHADER_PARAMETER(FVector2f, SceneViewRectMin)   // pixels
		SHADER_PARAMETER(FVector2f, SceneViewSize)      // pixels
		SHADER_PARAMETER(FVector2f, SceneBufferInvSize) // 1 / buffer extent
		SHADER_PARAMETER(float, SceneEdgeSoftness)      // soft edge width in viewport UV
		SHADER_PARAMETER(float, IorStart)
		SHADER_PARAMETER(float, UseTransmittance)
		SHADER_PARAMETER(float, EnvRefraction)
		SHADER_PARAMETER(float, FringeCurve)
		SHADER_PARAMETER(float, FringeMix)
		SHADER_PARAMETER(FVector3f, FringeColor)
		SHADER_PARAMETER(float, RefractionIridescence)
		SHADER_PARAMETER(float, DistScale)
		SHADER_PARAMETER_TEXTURE(Texture2D, DataATexture)
		SHADER_PARAMETER_TEXTURE(Texture2D, DataBTexture)
		SHADER_PARAMETER_TEXTURE(TextureCube, EnvMapTexture)
		SHADER_PARAMETER_TEXTURE(Texture2D, ColorsMapTexture)
		SHADER_PARAMETER_TEXTURE(Texture2D, SceneColorTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, DataSampler)
		SHADER_PARAMETER_SAMPLER(SamplerState, EnvSampler)
		SHADER_PARAMETER_SAMPLER(SamplerState, ColorsSampler)
		SHADER_PARAMETER_SAMPLER(SamplerState, SceneSampler)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

FRHIVertexDeclaration* GetGlassBackfaceVertexDeclarationRHI();
