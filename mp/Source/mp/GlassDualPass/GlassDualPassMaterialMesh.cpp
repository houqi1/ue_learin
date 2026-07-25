// Copyright Epic Games, Inc. All Rights Reserved.

#include "GlassDualPassMaterialMesh.h"

#include "DataDrivenShaderPlatformInfo.h"
#include "MaterialDomain.h"
#include "MaterialShared.h"
#include "Materials/Material.h"
#include "MeshMaterialShader.h"
#include "MeshPassProcessor.inl"
#include "ScenePrivate.h"
#include "ShaderBaseClasses.h"

// -----------------------------------------------------------------------------
// Shaders
// -----------------------------------------------------------------------------

bool FGlassMatBackfaceVS::ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
{
	// Only Unlit surface materials (M_PhoneixGlass_Back). Do NOT use bIsSpecialEngineMaterial
	// — that forces compile for WorldGridMaterial / default mats and explodes permutation count.
	const bool bUnlit = Parameters.MaterialParameters.ShadingModels.HasShadingModel(MSM_Unlit);
	const bool bSurface = Parameters.MaterialParameters.MaterialDomain == MD_Surface;
	return bUnlit && bSurface && !Parameters.MaterialParameters.bIsSpecialEngineMaterial;
}

IMPLEMENT_MATERIAL_SHADER_TYPE(, FGlassMatBackfaceVS, TEXT("/Project/GlassDualPassMaterialBackface.usf"), TEXT("MainVS"), SF_Vertex);

bool FGlassMatBackfacePS::ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
{
	const bool bUnlit = Parameters.MaterialParameters.ShadingModels.HasShadingModel(MSM_Unlit);
	const bool bSurface = Parameters.MaterialParameters.MaterialDomain == MD_Surface;
	return bUnlit && bSurface
		&& !Parameters.MaterialParameters.bIsSpecialEngineMaterial
		&& !Parameters.VertexFactoryType->SupportsNaniteRendering();
}

IMPLEMENT_MATERIAL_SHADER_TYPE(, FGlassMatBackfacePS, TEXT("/Project/GlassDualPassMaterialBackface.usf"), TEXT("MainPS"), SF_Pixel);

// -----------------------------------------------------------------------------
// Mesh processor
// -----------------------------------------------------------------------------

static bool GetGlassMatBackfaceShaders(
	const FMaterial& Material,
	const FVertexFactoryType* VertexFactoryType,
	TShaderRef<FGlassMatBackfaceVS>& OutVS,
	TShaderRef<FGlassMatBackfacePS>& OutPS)
{
	FMaterialShaderTypes ShaderTypes;
	ShaderTypes.AddShaderType<FGlassMatBackfaceVS>();
	ShaderTypes.AddShaderType<FGlassMatBackfacePS>();

	FMaterialShaders Shaders;
	if (!Material.TryGetShaders(ShaderTypes, VertexFactoryType, Shaders))
	{
		return false;
	}
	Shaders.TryGetVertexShader(OutVS);
	Shaders.TryGetPixelShader(OutPS);
	return OutVS.IsValid() && OutPS.IsValid();
}

FGlassMatBackfaceMeshProcessor::FGlassMatBackfaceMeshProcessor(
	const FScene* InScene,
	ERHIFeatureLevel::Type InFeatureLevel,
	const FSceneView* InViewIfDynamicMeshCommand,
	const FMeshPassProcessorRenderState& InDrawRenderState,
	FMeshPassDrawListContext* InDrawListContext,
	const FMaterialRenderProxy* InOverrideMaterialProxy)
	: FMeshPassProcessor(EMeshPass::TranslucencyAll, InScene, InFeatureLevel, InViewIfDynamicMeshCommand, InDrawListContext)
	, PassDrawRenderState(InDrawRenderState)
	, OverrideMaterialProxy(InOverrideMaterialProxy)
{
}

void FGlassMatBackfaceMeshProcessor::AddMeshBatch(
	const FMeshBatch& RESTRICT MeshBatch,
	uint64 BatchElementMask,
	const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy,
	int32 StaticMeshId)
{
	if (!MeshBatch.bUseForMaterial || !MeshBatch.VertexFactory)
	{
		return;
	}

	const FMaterialRenderProxy* MaterialRenderProxy = OverrideMaterialProxy
		? OverrideMaterialProxy
		: MeshBatch.MaterialRenderProxy;

	while (MaterialRenderProxy)
	{
		const FMaterial* Material = MaterialRenderProxy->GetMaterialNoFallback(FeatureLevel);
		if (Material && Material->GetRenderingThreadShaderMap())
		{
			// Force back-face only (same as former Global CM_CCW).
			const ERasterizerFillMode FillMode = FM_Solid;
			const ERasterizerCullMode CullMode = CM_CCW;

			if (Process(MeshBatch, BatchElementMask, StaticMeshId, PrimitiveSceneProxy, *MaterialRenderProxy, *Material, FillMode, CullMode))
			{
				break;
			}
		}
		MaterialRenderProxy = MaterialRenderProxy->GetFallback(FeatureLevel);
	}
}

bool FGlassMatBackfaceMeshProcessor::Process(
	const FMeshBatch& MeshBatch,
	uint64 BatchElementMask,
	int32 StaticMeshId,
	const FPrimitiveSceneProxy* PrimitiveSceneProxy,
	const FMaterialRenderProxy& MaterialRenderProxy,
	const FMaterial& MaterialResource,
	ERasterizerFillMode MeshFillMode,
	ERasterizerCullMode MeshCullMode)
{
	TMeshProcessorShaders<FGlassMatBackfaceVS, FGlassMatBackfacePS> PassShaders;
	if (!GetGlassMatBackfaceShaders(
		MaterialResource,
		MeshBatch.VertexFactory->GetType(),
		PassShaders.VertexShader,
		PassShaders.PixelShader))
	{
		return false;
	}

	FMeshMaterialShaderElementData ShaderElementData;
	ShaderElementData.InitializeMeshMaterialData(ViewIfDynamicMeshCommand, PrimitiveSceneProxy, MeshBatch, StaticMeshId, false);

	const FMeshDrawCommandSortKey SortKey = CalculateMeshStaticSortKey(
		static_cast<const FMeshMaterialShader*>(PassShaders.VertexShader.GetShader()),
		static_cast<const FMeshMaterialShader*>(PassShaders.PixelShader.GetShader()));

	BuildMeshDrawCommands(
		MeshBatch,
		BatchElementMask,
		PrimitiveSceneProxy,
		MaterialRenderProxy,
		MaterialResource,
		PassDrawRenderState,
		PassShaders,
		MeshFillMode,
		MeshCullMode,
		SortKey,
		EMeshPassFeatures::Default,
		ShaderElementData);

	return true;
}
