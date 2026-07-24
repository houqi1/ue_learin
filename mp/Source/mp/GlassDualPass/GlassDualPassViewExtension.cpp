// Copyright Epic Games, Inc. All Rights Reserved.

#include "GlassDualPassViewExtension.h"

#include "GlassDualPassBackfaceShaders.h"
#include "GlassDualPassLogs.h"
#include "GlassDualPassSubsystem.h"

#include "Components/MeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkinnedMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/TextureRenderTarget.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "GlobalShader.h"
#include "CachedGeometry.h"
#include "CommonRenderResources.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInterface.h"
#include "PipelineStateCache.h"
#include "PooledRenderTarget.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "RHI.h"
#include "RHICommandList.h"
#include "RHIResources.h"
#include "RHIStaticStates.h"
#include "SceneTexturesConfig.h"
#include "SceneView.h"
#include "ShaderParameterMacros.h"
#include "ShaderParameterStruct.h"
#include "SkeletalRenderPublic.h"
#include "StaticMeshResources.h"
#include "TextureResource.h"

#include "HAL/IConsoleManager.h"
#include "Engine/Engine.h"

/**
 * r.GlassDualPass
 * 0: disabled
 * 1: clear RT_GlassBack to black, then draw backfaces of meshes using Phoneix/Phoenix glass materials (Step3)
 * 2: clear RT_GlassBack to debug magenta only (Step2 legacy)
 */
static TAutoConsoleVariable<int32> CVarGlassDualPass(
	TEXT("r.GlassDualPass"),
	1,
	TEXT("D1 glass dual-pass.\n")
	TEXT("0: disabled\n")
	TEXT("1: after BasePass, clear RT_GlassBack + draw backfaces of meshes using M_PhoneixGlass / its MIs (Step3)\n")
	TEXT("2: clear RT_GlassBack to debug magenta only (Step2)"),
	ECVF_RenderThreadSafe);

/**
 * Master material asset name for dual-pass glass.
 * Any slot using this material, or a Material Instance whose parent chain reaches it, qualifies.
 * Default: M_PhoneixGlass
 */
static TAutoConsoleVariable<FString> CVarGlassDualPassMasterMaterial(
	TEXT("r.GlassDualPass.MasterMaterial"),
	TEXT("M_PhoneixGlass"),
	TEXT("Asset name of the glass master material. Meshes using it or its MI chain are dual-pass glass."),
	ECVF_Default);

/** Solid color written for glass backfaces (linear). Default cyan for debug. */
static TAutoConsoleVariable<FString> CVarGlassDualPassBackColor(
	TEXT("r.GlassDualPass.BackColor"),
	TEXT("0,1,1,1"),
	TEXT("RGBA linear debug color for glass backfaces in RT_GlassBack (comma-separated)."),
	ECVF_Default);

/**
 * Skeletal path:
 * 0 = skip skeletal meshes
 * 1 = GPU SkinCache position buffer (preferred) + bind-pose fallback
 * 2 = bind-pose RHI only (no SkinCache)
 */
static TAutoConsoleVariable<int32> CVarGlassDualPassSkeletalMode(
	TEXT("r.GlassDualPass.SkeletalMode"),
	1,
	TEXT("0=no skeletal, 1=GPU SkinCache positions, 2=bind-pose only."),
	ECVF_Default);

static const FLinearColor GGlassBackDebugMagenta(1.f, 0.f, 1.f, 1.f);
static const FLinearColor GGlassBackClearBlack(0.f, 0.f, 0.f, 0.f);

/** Skip thumbnails / captures that thrash gather and cause multi-family races. */
static bool ShouldGatherForViewFamily(const FSceneViewFamily& ViewFamily)
{
	if (ViewFamily.Views.Num() == 0 || ViewFamily.Views[0] == nullptr)
	{
		return false;
	}
	const FSceneView& V = *ViewFamily.Views[0];
	if (V.bIsSceneCapture || V.bIsReflectionCapture || V.bIsPlanarReflection || V.bIsVirtualTexture)
	{
		return false;
	}
	return true;
}

BEGIN_SHADER_PARAMETER_STRUCT(FGlassBackfacePassParameters, )
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

static FLinearColor ParseBackColorCVar()
{
	const FString S = CVarGlassDualPassBackColor.GetValueOnAnyThread();
	TArray<FString> Parts;
	S.ParseIntoArray(Parts, TEXT(","), true);
	if (Parts.Num() >= 3)
	{
		const float R = FCString::Atof(*Parts[0]);
		const float G = FCString::Atof(*Parts[1]);
		const float B = FCString::Atof(*Parts[2]);
		const float A = Parts.Num() >= 4 ? FCString::Atof(*Parts[3]) : 1.f;
		return FLinearColor(R, G, B, A);
	}
	return FLinearColor(0.f, 1.f, 1.f, 1.f);
}

/**
 * True if Material is exactly M_PhoneixGlass, or a Material Instance whose parent chain
 * eventually reaches M_PhoneixGlass (any depth of MI nesting).
 */
static bool MaterialIsGlassDualPass(const UMaterialInterface* Material)
{
	if (!IsValid(Material))
	{
		return false;
	}

	const FString MasterName = CVarGlassDualPassMasterMaterial.GetValueOnGameThread().TrimStartAndEnd();
	if (MasterName.IsEmpty())
	{
		return false;
	}

	// Walk Material Instance → Parent → … → base Material.
	const UMaterialInterface* Current = Material;
	while (IsValid(Current))
	{
		// Exact asset name match only (e.g. "M_PhoneixGlass"), not substring.
		if (Current->GetName().Equals(MasterName, ESearchCase::IgnoreCase))
		{
			return true;
		}

		if (const UMaterialInstance* MI = Cast<UMaterialInstance>(Current))
		{
			Current = MI->Parent;
			continue;
		}
		break;
	}

	return false;
}

/** Component qualifies if any material slot uses M_PhoneixGlass or an instance of it. */
static bool ComponentIsGlassDualPass(const UMeshComponent* MeshComp)
{
	if (!IsValid(MeshComp))
	{
		return false;
	}

	const int32 NumMaterials = MeshComp->GetNumMaterials();
	for (int32 Index = 0; Index < NumMaterials; ++Index)
	{
		if (MaterialIsGlassDualPass(MeshComp->GetMaterial(Index)))
		{
			return true;
		}
	}
	return false;
}

static bool MaterialSlotIsGlass(const UMeshComponent* MeshComp, int32 MaterialIndex)
{
	return IsValid(MeshComp) && MaterialIsGlassDualPass(MeshComp->GetMaterial(MaterialIndex));
}

static bool TryBuildDrawItem_Static(const UStaticMeshComponent* SMC, FGlassBackfaceDrawItem& OutItem)
{
	UStaticMesh* Mesh = SMC->GetStaticMesh();
	if (!IsValid(Mesh))
	{
		return false;
	}

	const FStaticMeshRenderData* RenderData = Mesh->GetRenderData();
	if (!RenderData || RenderData->LODResources.Num() == 0)
	{
		return false;
	}

	const FStaticMeshLODResources& LOD = RenderData->LODResources[0];
	FRHIBuffer* PositionRHI = LOD.VertexBuffers.PositionVertexBuffer.VertexBufferRHI.GetReference();
	FRHIBuffer* IndexRHI = LOD.IndexBuffer.IndexBufferRHI.GetReference();
	if (!PositionRHI || !IndexRHI)
	{
		return false;
	}

	const uint32 NumVertices = LOD.VertexBuffers.PositionVertexBuffer.GetNumVertices();
	const uint32 NumIndices = static_cast<uint32>(LOD.IndexBuffer.GetNumIndices());
	if (NumVertices == 0 || NumIndices < 3)
	{
		return false;
	}

	bool bAnyGlassSection = false;
	if (LOD.Sections.Num() > 0)
	{
		for (int32 SecIdx = 0; SecIdx < LOD.Sections.Num(); ++SecIdx)
		{
			if (MaterialSlotIsGlass(SMC, LOD.Sections[SecIdx].MaterialIndex))
			{
				bAnyGlassSection = true;
				break;
			}
		}
		if (!bAnyGlassSection)
		{
			return false;
		}
	}

	OutItem.LocalToWorld = FMatrix44f(SMC->GetComponentTransform().ToMatrixWithScale());
	OutItem.FallbackPositionBuffer = PositionRHI;
	OutItem.IndexBuffer = IndexRHI;
	OutItem.MeshObject = nullptr;
	OutItem.SkinSectionIndex = INDEX_NONE;
	OutItem.NumVertices = NumVertices;
	OutItem.FirstIndex = 0;
	OutItem.NumIndices = NumIndices;
	OutItem.BaseVertexIndex = 0;
	OutItem.Source = EGlassBackfaceSource::StaticMesh;
	return true;
}

/**
 * Skeletal: record MeshObject + section for SkinCache bind on RT.
 * No CPU vertex arrays — engine GPU SkinCache already wrote skinned positions.
 */
static void AppendSkeletalGlassDraws(USkeletalMeshComponent* SKC, TArray<FGlassBackfaceDrawItem>& OutDraws)
{
	if (!IsValid(SKC) || !SKC->GetSkeletalMeshAsset())
	{
		return;
	}

	const int32 SkeletalMode = CVarGlassDualPassSkeletalMode.GetValueOnGameThread();
	if (SkeletalMode == 0)
	{
		return;
	}

	FSkeletalMeshRenderData* RenderData = SKC->GetSkeletalMeshRenderData();
	if (!RenderData || RenderData->LODRenderData.Num() == 0)
	{
		return;
	}

	const FSkeletalMeshObject* MeshObject = SKC->GetMeshObject();
	// Align with GetCachedGeometry which uses MeshObject->GetLOD(), not PredictedLOD alone.
	int32 LODIndex = SKC->GetPredictedLODLevel();
	if (MeshObject)
	{
		LODIndex = MeshObject->GetLOD();
	}
	LODIndex = FMath::Clamp(LODIndex, 0, RenderData->LODRenderData.Num() - 1);
	FSkeletalMeshLODRenderData& LOD = RenderData->LODRenderData[LODIndex];

	const FRawStaticIndexBuffer16or32Interface* IndexBuffer = LOD.MultiSizeIndexContainer.GetIndexBuffer();
	if (!IndexBuffer)
	{
		return;
	}

	FRHIBuffer* IndexRHI = IndexBuffer->IndexBufferRHI.GetReference();
	FRHIBuffer* BindPosePosRHI = LOD.StaticVertexBuffers.PositionVertexBuffer.VertexBufferRHI.GetReference();
	if (!IndexRHI)
	{
		return;
	}

	const bool bWantSkinCache = (SkeletalMode == 1) && MeshObject && MeshObject->IsGPUSkinMesh();

	const FMatrix44f LocalToWorld(SKC->GetComponentTransform().ToMatrixWithScale());

	// One draw item per glass material section (SkinCache is section-indexed).
	for (int32 SecIdx = 0; SecIdx < LOD.RenderSections.Num(); ++SecIdx)
	{
		const FSkelMeshRenderSection& Section = LOD.RenderSections[SecIdx];
		if (Section.NumTriangles == 0 || !MaterialSlotIsGlass(SKC, Section.MaterialIndex))
		{
			continue;
		}

		FGlassBackfaceDrawItem Item;
		Item.LocalToWorld = LocalToWorld;
		Item.IndexBuffer = IndexRHI;
		Item.FallbackPositionBuffer = BindPosePosRHI;
		Item.FirstIndex = Section.BaseIndex;
		Item.NumIndices = Section.NumTriangles * 3;
		Item.NumVertices = Section.NumVertices;
		Item.BaseVertexIndex = static_cast<int32>(Section.BaseVertexIndex);
		Item.SkinSectionIndex = SecIdx;
		Item.MeshObject = bWantSkinCache ? MeshObject : nullptr;
		Item.Source = bWantSkinCache ? EGlassBackfaceSource::SkeletalSkinCache : EGlassBackfaceSource::SkeletalBindPose;

		if (Item.NumIndices >= 3 && (Item.FallbackPositionBuffer || Item.MeshObject))
		{
			OutDraws.Add(MoveTemp(Item));
		}
	}
}

FGlassDualPassViewExtension::FGlassDualPassViewExtension(const FAutoRegister& AutoRegister)
	: FSceneViewExtensionBase(AutoRegister)
{
	UE_LOG(LogGlassDualPass, Log, TEXT("FGlassDualPassViewExtension registered"));
}

bool FGlassDualPassViewExtension::IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const
{
	return CVarGlassDualPass.GetValueOnAnyThread() != 0;
}

void FGlassDualPassViewExtension::GatherInto(TArray<FGlassBackfaceDrawItem>& OutDraws, const FSceneViewFamily& ViewFamily)
{
	OutDraws.Reset();

	UWorld* World = ViewFamily.Scene ? ViewFamily.Scene->GetWorld() : nullptr;
	if (!World)
	{
		return;
	}

	int32 NumStatic = 0;
	int32 NumSkeletal = 0;

	for (TActorIterator<AActor> ActorIt(World); ActorIt; ++ActorIt)
	{
		AActor* Actor = *ActorIt;
		if (!IsValid(Actor))
		{
			continue;
		}

		TInlineComponentArray<UStaticMeshComponent*> SMCs;
		Actor->GetComponents(SMCs);
		for (UStaticMeshComponent* SMC : SMCs)
		{
			if (!IsValid(SMC) || !SMC->IsRegistered() || !SMC->IsVisible())
			{
				continue;
			}
			if (!ComponentIsGlassDualPass(SMC))
			{
				continue;
			}

			FGlassBackfaceDrawItem Item;
			if (TryBuildDrawItem_Static(SMC, Item))
			{
				OutDraws.Add(MoveTemp(Item));
				++NumStatic;
			}
		}

		TInlineComponentArray<USkeletalMeshComponent*> SKCs;
		Actor->GetComponents(SKCs);
		for (USkeletalMeshComponent* SKC : SKCs)
		{
			if (!IsValid(SKC) || !SKC->IsRegistered() || !SKC->IsVisible())
			{
				continue;
			}
			if (!ComponentIsGlassDualPass(SKC))
			{
				continue;
			}

			const int32 Before = OutDraws.Num();
			AppendSkeletalGlassDraws(SKC, OutDraws);
			if (OutDraws.Num() > Before)
			{
				++NumSkeletal;
			}
		}
	}

	static double LastGatherLogSeconds = 0.0;
	const double Now = FPlatformTime::Seconds();
	if (Now - LastGatherLogSeconds > 2.0)
	{
		LastGatherLogSeconds = Now;
		UE_LOG(LogGlassDualPass, Log,
			TEXT("Glass gather: %d draw(s) [static=%d skeletal=%d] M_PhoneixGlass / MIs"),
			OutDraws.Num(), NumStatic, NumSkeletal);
	}
}

void FGlassDualPassViewExtension::BeginRenderViewFamily(FSceneViewFamily& InViewFamily)
{
	// Epic: game thread owns simulation data; never share mutable TArray with render thread.
	// Publish a SharedPtr payload so RT holds a stable snapshot without ConstructItems race.

	if (CVarGlassDualPass.GetValueOnGameThread() == 0)
	{
		return;
	}

	if (!ShouldGatherForViewFamily(InViewFamily))
	{
		return;
	}

	if (!GEngine)
	{
		return;
	}

	UGlassDualPassSubsystem* Sys = GEngine->GetEngineSubsystem<UGlassDualPassSubsystem>();
	if (!Sys)
	{
		return;
	}

	UTextureRenderTarget2D* PreviewRT = Sys->GetOrCreatePreviewRT();
	if (!IsValid(PreviewRT))
	{
		return;
	}

	if (InViewFamily.Views.Num() > 0 && InViewFamily.Views[0])
	{
		const FIntRect& ViewRect = InViewFamily.Views[0]->UnconstrainedViewRect;
		Sys->EnsurePreviewRTSize(FMath::Max(ViewRect.Width(), 1), FMath::Max(ViewRect.Height(), 1));
	}

	FTextureRenderTargetResource* RTResource = Sys->GetPreviewRTResource();
	if (!RTResource)
	{
		static double LastWarn = 0.0;
		const double Now = FPlatformTime::Seconds();
		if (Now - LastWarn > 2.0)
		{
			LastWarn = Now;
			UE_LOG(LogGlassDualPass, Warning,
				TEXT("Preview RT resource null (RT=%s %dx%d)"),
				*GetNameSafe(PreviewRT), PreviewRT->SizeX, PreviewRT->SizeY);
		}
		return;
	}

	const int32 Mode = CVarGlassDualPass.GetValueOnGameThread();
	TSharedPtr<FGlassDualPassFramePayload, ESPMode::ThreadSafe> Payload =
		MakeShared<FGlassDualPassFramePayload, ESPMode::ThreadSafe>();
	Payload->PreviewRTResource = RTResource;
	Payload->ViewFamilyId = NextViewFamilyId++;

	if (Mode == 1)
	{
		GatherInto(Payload->Draws, InViewFamily);
	}
	// Mode 2: empty Draws → clear magenta only on RT.

	{
		FScopeLock Lock(&GlassDataCS);
		PublishedPayload = Payload;
	}
}

void FGlassDualPassViewExtension::PostRenderBasePassDeferred_RenderThread(
	FRDGBuilder& GraphBuilder,
	FSceneView& View,
	const FRenderTargetBindingSlots& RenderTargets,
	TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTextures)
{
	(void)RenderTargets;
	(void)SceneTextures;

	const int32 Mode = CVarGlassDualPass.GetValueOnRenderThread();
	if (Mode == 0)
	{
		return;
	}

	// Only drive the dual-pass RT from the primary view of a family (avoid N× clear/draw + races).
	if (View.Family && View.Family->Views.Num() > 0 && View.Family->Views[0] != &View)
	{
		return;
	}
	if (View.bIsSceneCapture || View.bIsReflectionCapture || View.bIsPlanarReflection || View.bIsVirtualTexture)
	{
		return;
	}

	// Snapshot SharedPtr under lock — NO deep copy of DynamicPositions (that was the AV/OOM site).
	TSharedPtr<FGlassDualPassFramePayload, ESPMode::ThreadSafe> Payload;
	{
		FScopeLock Lock(&GlassDataCS);
		Payload = PublishedPayload;
	}
	if (!Payload.IsValid() || !Payload->PreviewRTResource)
	{
		return;
	}

	FRHITexture* TextureRHI = Payload->PreviewRTResource->GetRenderTargetTexture();
	if (!TextureRHI)
	{
		TextureRHI = Payload->PreviewRTResource->TextureRHI.GetReference();
	}
	if (!TextureRHI)
	{
		return;
	}

	const FIntRect& ViewRect = View.UnconstrainedViewRect;
	const FRDGTextureRef GlassBackRDG = GraphBuilder.RegisterExternalTexture(
		CreateRenderTarget(TextureRHI, TEXT("GlassBackPreviewRT")));

	const FLinearColor ClearColor = (Mode == 2) ? GGlassBackDebugMagenta : GGlassBackClearBlack;
	AddClearRenderTargetPass(GraphBuilder, GlassBackRDG, ClearColor);

	if (Mode == 2)
	{
		return;
	}

	if (Payload->Draws.Num() == 0)
	{
		return;
	}

	const FMatrix44f ViewProjection44(View.ViewMatrices.GetWorldToClip());
	const FLinearColor BackColor = ParseBackColorCVar();
	const FIntPoint TextureExtent(TextureRHI->GetSizeX(), TextureRHI->GetSizeY());

	// Resolve GPU SkinCache positions via public virtual GetCachedGeometry.
	struct FResolvedGlassDraw
	{
		FMatrix44f LocalToWorld;
		FBufferRHIRef StaticPositionBuffer; // ATTRIBUTE0 path
		FShaderResourceViewRHIRef PositionSRV; // SkinCache path (absolute vertex indices)
		FBufferRHIRef IndexBuffer;
		uint32 NumVertices = 0; // must cover max absolute index used (full mesh for skeletal)
		uint32 FirstIndex = 0;
		uint32 NumIndices = 0;
		bool bUseSkinCacheSRV = false;
	};

	// Cache GetCachedGeometry per MeshObject (one call fills all sections).
	TMap<const FSkeletalMeshObject*, FCachedGeometry> CachedGeomByMesh;
	TArray<FResolvedGlassDraw> Resolved;
	Resolved.Reserve(Payload->Draws.Num());
	int32 NumSkinCacheHits = 0;
	int32 NumFallbacks = 0;

	for (const FGlassBackfaceDrawItem& Item : Payload->Draws)
	{
		if (!Item.IndexBuffer || Item.NumIndices < 3 || Item.NumVertices == 0)
		{
			continue;
		}

		FResolvedGlassDraw R;
		R.LocalToWorld = Item.LocalToWorld;
		R.IndexBuffer = Item.IndexBuffer;
		R.NumVertices = Item.NumVertices;
		R.FirstIndex = Item.FirstIndex;
		R.NumIndices = Item.NumIndices;
		R.StaticPositionBuffer = Item.FallbackPositionBuffer;
		R.bUseSkinCacheSRV = false;

		if (Item.Source == EGlassBackfaceSource::SkeletalSkinCache
			&& Item.MeshObject
			&& Item.SkinSectionIndex != INDEX_NONE)
		{
			const FCachedGeometry* Geom = CachedGeomByMesh.Find(Item.MeshObject);
			if (!Geom)
			{
				FCachedGeometry NewGeom;
				if (Item.MeshObject->GetCachedGeometry(GraphBuilder, NewGeom))
				{
					Geom = &CachedGeomByMesh.Add(Item.MeshObject, MoveTemp(NewGeom));
				}
			}

			if (Geom && Geom->Sections.IsValidIndex(Item.SkinSectionIndex))
			{
				const FCachedGeometry::Section& Sec = Geom->Sections[Item.SkinSectionIndex];
				if (Sec.PositionBuffer && Sec.NumVertices > 0)
				{
					R.PositionSRV = Sec.PositionBuffer;
					// SkinCache stores positions at absolute indices (SkinCacheStart = BaseVertexIndex).
					// Indexed draw uses absolute indices → VS must sample with SV_VertexID (no subtract).
					// NumVertices must cover the full mesh range, not only this section's local count.
					const uint32 TotalVerts = Sec.TotalVertexCount > 0
						? Sec.TotalVertexCount
						: (Sec.VertexBaseIndex + Sec.NumVertices);
					R.NumVertices = FMath::Max(TotalVerts, Sec.VertexBaseIndex + Sec.NumVertices);
					R.FirstIndex = Sec.IndexBaseIndex;
					R.NumIndices = Sec.NumPrimitives * 3;
					R.bUseSkinCacheSRV = true;
					++NumSkinCacheHits;
				}
			}
		}

		if (!R.bUseSkinCacheSRV)
		{
			if (!R.StaticPositionBuffer)
			{
				continue;
			}
			// Bind-pose fallback also uses absolute indices for skeletal sections.
			if (Item.Source != EGlassBackfaceSource::StaticMesh)
			{
				R.NumVertices = FMath::Max(R.NumVertices, static_cast<uint32>(Item.BaseVertexIndex) + Item.NumVertices);
				++NumFallbacks;
			}
		}

		Resolved.Add(MoveTemp(R));
	}

	UE_LOG(LogGlassDualPass, Verbose,
		TEXT("Glass draw resolve: %d items (SkinCacheSRV=%d BindPoseFallback=%d)"),
		Resolved.Num(), NumSkinCacheHits, NumFallbacks);

	if (Resolved.Num() == 0)
	{
		return;
	}

	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(View.GetFeatureLevel());
	TShaderMapRef<FGlassBackfaceVS> VertexShader(ShaderMap);
	TShaderMapRef<FGlassBackfaceSkinCacheVS> SkinCacheVS(ShaderMap);
	TShaderMapRef<FGlassBackfacePS> PixelShader(ShaderMap);

	FGlassBackfacePassParameters* PassParameters = GraphBuilder.AllocParameters<FGlassBackfacePassParameters>();
	PassParameters->RenderTargets[0] = FRenderTargetBinding(GlassBackRDG, ERenderTargetLoadAction::ELoad);

	const int32 DrawCount = Resolved.Num();
	GraphBuilder.AddPass(
		RDG_EVENT_NAME("GlassDualPass_Backfaces(%d)", DrawCount),
		PassParameters,
		ERDGPassFlags::Raster,
		[Resolved = MoveTemp(Resolved), ViewProjection44, BackColor, ViewRect, TextureExtent, VertexShader, SkinCacheVS, PixelShader](FRHICommandList& RHICmdList)
		{
			RHICmdList.SetViewport(
				static_cast<float>(ViewRect.Min.X),
				static_cast<float>(ViewRect.Min.Y),
				0.0f,
				static_cast<float>(ViewRect.Max.X),
				static_cast<float>(ViewRect.Max.Y),
				1.0f);

			auto SetCommonRaster = [&](FRHIVertexDeclaration* VertDecl, FRHIVertexShader* VS)
			{
				FGraphicsPipelineStateInitializer GraphicsPSOInit;
				RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
				GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = VertDecl;
				GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VS;
				GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
				GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_CCW>::GetRHI();
				GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
				GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
				GraphicsPSOInit.PrimitiveType = PT_TriangleList;
				SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, 0);

				FGlassBackfacePS::FParameters PSParams;
				PSParams.DebugColor = FVector4f(BackColor.R, BackColor.G, BackColor.B, BackColor.A);
				SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), PSParams);
			};

			int32 Drawn = 0;
			for (const FResolvedGlassDraw& Item : Resolved)
			{
				if (!Item.IndexBuffer || Item.NumIndices < 3)
				{
					continue;
				}

				if (Item.bUseSkinCacheSRV && Item.PositionSRV)
				{
					// No vertex streams — SV_VertexID + SkinCache Position SRV (engine SkinCache layout).
					SetCommonRaster(GEmptyVertexDeclaration.VertexDeclarationRHI, SkinCacheVS.GetVertexShader());

					FGlassBackfaceSkinCacheVS::FParameters VSParams;
					VSParams.LocalToWorld = Item.LocalToWorld;
					VSParams.ViewProjection = ViewProjection44;
					VSParams.PositionSRV = Item.PositionSRV;
					SetShaderParameters(RHICmdList, SkinCacheVS, SkinCacheVS.GetVertexShader(), VSParams);

					RHICmdList.DrawIndexedPrimitive(
						Item.IndexBuffer,
						/*BaseVertexIndex*/ 0,
						/*FirstInstance*/ 0,
						Item.NumVertices,
						Item.FirstIndex,
						Item.NumIndices / 3,
						1);
					++Drawn;
				}
				else if (Item.StaticPositionBuffer)
				{
					SetCommonRaster(GetGlassBackfaceVertexDeclarationRHI(), VertexShader.GetVertexShader());

					FGlassBackfaceVS::FParameters VSParams;
					VSParams.LocalToWorld = Item.LocalToWorld;
					VSParams.ViewProjection = ViewProjection44;
					SetShaderParameters(RHICmdList, VertexShader, VertexShader.GetVertexShader(), VSParams);

					RHICmdList.SetStreamSource(0, Item.StaticPositionBuffer, 0);
					RHICmdList.DrawIndexedPrimitive(
						Item.IndexBuffer,
						0, 0, Item.NumVertices, Item.FirstIndex, Item.NumIndices / 3, 1);
					++Drawn;
				}
			}

			UE_LOG(LogGlassDualPass, Verbose,
				TEXT("Drew %d/%d glass backfaces RT=%dx%d"),
				Drawn, Resolved.Num(), TextureExtent.X, TextureExtent.Y);
		});
}
