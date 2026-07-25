// Copyright Epic Games, Inc. All Rights Reserved.
// Mesh Material Shader path: draw M_PhoneixGlass_Back to RT_GlassBack (no Global PS).

#pragma once

#include "CoreMinimal.h"
#include "MeshMaterialShader.h"
#include "MeshPassProcessor.h"
#include "RHIDefinitions.h"

class FScene;
class FPrimitiveSceneProxy;
class FMaterialRenderProxy;

/** Mesh material VS for glass backface → RT. */
class FGlassMatBackfaceVS : public FMeshMaterialShader
{
	DECLARE_SHADER_TYPE(FGlassMatBackfaceVS, MeshMaterial);

public:
	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters);

	FGlassMatBackfaceVS() = default;
	FGlassMatBackfaceVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FMeshMaterialShader(Initializer)
	{
	}
};

/** Mesh material PS: material Emissive (Custom / Lib) → SV_Target0. */
class FGlassMatBackfacePS : public FMeshMaterialShader
{
	DECLARE_SHADER_TYPE(FGlassMatBackfacePS, MeshMaterial);

public:
	static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters);

	FGlassMatBackfacePS() = default;
	FGlassMatBackfacePS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FMeshMaterialShader(Initializer)
	{
	}
};

/**
 * Dynamic mesh pass processor: force OverrideMaterialProxy (M_PhoneixGlass_Back MID),
 * draw back faces (CM_CCW), no depth test — same intent as former Global backface pass.
 */
class FGlassMatBackfaceMeshProcessor : public FMeshPassProcessor
{
public:
	FGlassMatBackfaceMeshProcessor(
		const FScene* InScene,
		ERHIFeatureLevel::Type InFeatureLevel,
		const FSceneView* InViewIfDynamicMeshCommand,
		const FMeshPassProcessorRenderState& InDrawRenderState,
		FMeshPassDrawListContext* InDrawListContext,
		const FMaterialRenderProxy* InOverrideMaterialProxy);

	virtual void AddMeshBatch(
		const FMeshBatch& RESTRICT MeshBatch,
		uint64 BatchElementMask,
		const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy,
		int32 StaticMeshId = -1) override;

private:
	bool Process(
		const FMeshBatch& MeshBatch,
		uint64 BatchElementMask,
		int32 StaticMeshId,
		const FPrimitiveSceneProxy* PrimitiveSceneProxy,
		const FMaterialRenderProxy& MaterialRenderProxy,
		const FMaterial& MaterialResource,
		ERasterizerFillMode MeshFillMode,
		ERasterizerCullMode MeshCullMode);

	FMeshPassProcessorRenderState PassDrawRenderState;
	const FMaterialRenderProxy* OverrideMaterialProxy = nullptr;
};
