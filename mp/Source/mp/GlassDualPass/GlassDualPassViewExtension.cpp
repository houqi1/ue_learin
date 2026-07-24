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
#include "Engine/Texture2D.h"
#include "Engine/TextureCube.h"
#include "GlobalRenderResources.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInterface.h"
#include "PipelineStateCache.h"
#include "PooledRenderTarget.h"
#include "PostProcess/PostProcessInputs.h"
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
#include "FXRenderingUtils.h"

/**
 * r.GlassDualPass
 * 0: disabled
 * 1: clear RT_GlassBack + draw shaded backfaces (BasePass + lit upgrade @ PrePostProcess)
 * 2: clear RT_GlassBack to debug magenta only
 */
static TAutoConsoleVariable<int32> CVarGlassDualPass(
	TEXT("r.GlassDualPass"),
	1,
	TEXT("D1 glass dual-pass.\n")
	TEXT("0: disabled\n")
	TEXT("1: clear RT_GlassBack + draw backfaces (M_PhoneixGlass / MIs)\n")
	TEXT("2: clear RT_GlassBack to debug magenta only"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<FString> CVarGlassDualPassMasterMaterial(
	TEXT("r.GlassDualPass.MasterMaterial"),
	TEXT("M_PhoneixGlass"),
	TEXT("Asset name of the glass master material."),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarGlassDualPassSkeletalMode(
	TEXT("r.GlassDualPass.SkeletalMode"),
	1,
	TEXT("0=no skeletal, 1=GPU SkinCache positions, 2=bind-pose only."),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarGlassMaxRTSize(
	TEXT("r.GlassDualPass.MaxRTSize"),
	1280,
	TEXT("Max dimension for RT_GlassBack (VRAM cap)."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarGlassIor(TEXT("r.GlassDualPass.IOR"), 1.45f, TEXT("Backface IOR."), ECVF_RenderThreadSafe);
static TAutoConsoleVariable<float> CVarGlassUseTransmittance(TEXT("r.GlassDualPass.UseTransmittance"), 1.f, TEXT("Fresnel transmittance scale."), ECVF_RenderThreadSafe);
static TAutoConsoleVariable<float> CVarGlassEnvRefraction(TEXT("r.GlassDualPass.EnvRefraction"), 0.35f, TEXT("Env cubemap add."), ECVF_RenderThreadSafe);
static TAutoConsoleVariable<float> CVarGlassFringeCurve(TEXT("r.GlassDualPass.FringeCurve"), 2.f, TEXT("Fringe falloff."), ECVF_RenderThreadSafe);
static TAutoConsoleVariable<float> CVarGlassFringeMix(TEXT("r.GlassDualPass.FringeMix"), 0.25f, TEXT("Fringe mix."), ECVF_RenderThreadSafe);
static TAutoConsoleVariable<float> CVarGlassRefractionIridescence(TEXT("r.GlassDualPass.RefractionIridescence"), 0.85f, TEXT("colorsMap mix."), ECVF_RenderThreadSafe);
static TAutoConsoleVariable<float> CVarGlassDistScale(TEXT("r.GlassDualPass.DistScale"), 1.f, TEXT("Baked Dist scale."), ECVF_RenderThreadSafe);

static const FLinearColor GGlassBackDebugMagenta(1.f, 0.f, 1.f, 1.f);
static const FLinearColor GGlassBackClearBlack(0.f, 0.f, 0.f, 0.f);
static const FVector3f GGlassFringeColor(0.55f, 0.75f, 1.0f);

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

static bool IsPrimaryNonCaptureView(const FSceneView& View)
{
	if (View.Family && View.Family->Views.Num() > 0 && View.Family->Views[0] != &View)
	{
		return false;
	}
	if (View.bIsSceneCapture || View.bIsReflectionCapture || View.bIsPlanarReflection || View.bIsVirtualTexture)
	{
		return false;
	}
	return true;
}

BEGIN_SHADER_PARAMETER_STRUCT(FGlassBackfacePassParameters, )
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneColorTexture)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

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

	const UMaterialInterface* Current = Material;
	while (IsValid(Current))
	{
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

	if (LOD.Sections.Num() > 0)
	{
		bool bAnyGlassSection = false;
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
	OutItem.UVSRV = LOD.VertexBuffers.StaticMeshVertexBuffer.GetTexCoordsSRV();
	OutItem.NumTexCoords = LOD.VertexBuffers.StaticMeshVertexBuffer.GetNumTexCoords();
	// Model normals: TangentZ in interleaved [Tx, Tz] pairs (TangentFormat=0).
	OutItem.TangentSRV = LOD.VertexBuffers.StaticMeshVertexBuffer.GetTangentsSRV();
	OutItem.TangentFormat = 0;
	OutItem.MeshObject = nullptr;
	OutItem.SkinSectionIndex = INDEX_NONE;
	OutItem.NumVertices = NumVertices;
	OutItem.FirstIndex = 0;
	OutItem.NumIndices = NumIndices;
	OutItem.BaseVertexIndex = 0;
	OutItem.Source = EGlassBackfaceSource::StaticMesh;
	return true;
}

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
	const FShaderResourceViewRHIRef UVSRV = LOD.StaticVertexBuffers.StaticMeshVertexBuffer.GetTexCoordsSRV();
	const FShaderResourceViewRHIRef TangentSRV = LOD.StaticVertexBuffers.StaticMeshVertexBuffer.GetTangentsSRV();
	const uint32 NumTexCoords = LOD.StaticVertexBuffers.StaticMeshVertexBuffer.GetNumTexCoords();

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
		Item.UVSRV = UVSRV;
		Item.NumTexCoords = NumTexCoords;
		Item.TangentSRV = TangentSRV;
		Item.TangentFormat = 0;
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
	UE_LOG(LogGlassDualPass, Log, TEXT("FGlassDualPassViewExtension registered (draw only @ PrePostProcess / post-lighting)"));
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
			TEXT("Glass gather: %d draw(s) [static=%d skeletal=%d]"),
			OutDraws.Num(), NumStatic, NumSkeletal);
	}
}

void FGlassDualPassViewExtension::BeginRenderViewFamily(FSceneViewFamily& InViewFamily)
{
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

	// Never leave Step2 magenta as the asset clear color.
	if (PreviewRT->ClearColor != GGlassBackClearBlack)
	{
		PreviewRT->ClearColor = GGlassBackClearBlack;
	}

	if (InViewFamily.Views.Num() > 0 && InViewFamily.Views[0])
	{
		const FIntRect& ViewRect = InViewFamily.Views[0]->UnconstrainedViewRect;
		const int32 MaxDim = FMath::Max(64, CVarGlassMaxRTSize.GetValueOnGameThread());
		int32 W = FMath::Max(ViewRect.Width(), 1);
		int32 H = FMath::Max(ViewRect.Height(), 1);
		if (W > MaxDim || H > MaxDim)
		{
			const float Scale = static_cast<float>(MaxDim) / static_cast<float>(FMath::Max(W, H));
			W = FMath::Max(8, FMath::RoundToInt(W * Scale));
			H = FMath::Max(8, FMath::RoundToInt(H * Scale));
		}
		Sys->EnsurePreviewRTSize(W, H);
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

	Sys->EnsureShadingTextures();
	auto GrabTex = [](UTexture* T) -> FTextureRHIRef
	{
		if (!IsValid(T) || !T->GetResource())
		{
			return nullptr;
		}
		return T->GetResource()->TextureRHI;
	};
	Payload->DataA = GrabTex(Sys->GetDataA());
	Payload->DataB = GrabTex(Sys->GetDataB());
	Payload->EnvMap = GrabTex(Sys->GetEnvMap());
	Payload->ColorsMap = GrabTex(Sys->GetColorsMap());

	if (Mode == 1)
	{
		GatherInto(Payload->Draws, InViewFamily);
	}

	{
		FScopeLock Lock(&GlassDataCS);
		PublishedPayload = Payload;
	}
}

void FGlassDualPassViewExtension::ExecuteGlassBackfacePass(
	FRDGBuilder& GraphBuilder,
	const FSceneView& View,
	FRDGTextureRef SceneColorRDG,
	bool bCopySceneColor,
	const TCHAR* PassTag)
{
	const int32 Mode = CVarGlassDualPass.GetValueOnRenderThread();
	if (Mode == 0)
	{
		return;
	}

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

	const FRDGTextureRef GlassBackRDG = GraphBuilder.RegisterExternalTexture(
		CreateRenderTarget(TextureRHI, TEXT("GlassBackPreviewRT")));

	// Mode 2 = magenta debug; Mode 1 = black then draw.
	const FLinearColor ClearColor = (Mode == 2) ? GGlassBackDebugMagenta : GGlassBackClearBlack;
	AddClearRenderTargetPass(GraphBuilder, GlassBackRDG, ClearColor);

	if (Mode == 2)
	{
		return;
	}

	if (Payload->Draws.Num() == 0)
	{
		static double LastEmpty = 0.0;
		const double Now = FPlatformTime::Seconds();
		if (Now - LastEmpty > 2.0)
		{
			LastEmpty = Now;
			UE_LOG(LogGlassDualPass, Log, TEXT("[%s] Mode1 clear black, 0 draws (no M_PhoneixGlass mesh?)"), PassTag);
		}
		return;
	}

	// Resolve SceneColor for refraction (may be null → black).
	FRDGTextureRef SceneColorSample = nullptr;
	if (SceneColorRDG)
	{
		if (bCopySceneColor)
		{
			SceneColorSample = GraphBuilder.CreateTexture(SceneColorRDG->Desc, TEXT("GlassSceneColorCopy"));
			AddCopyTexturePass(GraphBuilder, SceneColorRDG, SceneColorSample);
		}
		else
		{
			SceneColorSample = SceneColorRDG;
		}
	}

	// Fallback: tiny black RDG texture if no SceneColor is available.
	if (!SceneColorSample)
	{
		const FRDGTextureDesc BlackDesc = FRDGTextureDesc::Create2D(
			FIntPoint(8, 8),
			PF_FloatRGBA,
			FClearValueBinding::Black,
			TexCreate_ShaderResource | TexCreate_RenderTargetable);
		SceneColorSample = GraphBuilder.CreateTexture(BlackDesc, TEXT("GlassBlackSceneFallback"));
		AddClearRenderTargetPass(GraphBuilder, SceneColorSample, FLinearColor::Black);
	}

	const FMatrix44f ViewProjection44(View.ViewMatrices.GetWorldToClip());
	const FIntPoint TextureExtent(TextureRHI->GetSizeX(), TextureRHI->GetSizeY());

	struct FResolvedGlassDraw
	{
		FMatrix44f LocalToWorld;
		FMatrix44f LocalToWorldInverseTranspose;
		FBufferRHIRef StaticPositionBuffer;
		FShaderResourceViewRHIRef PositionSRV;
		FShaderResourceViewRHIRef TangentSRV; // model normals (static / SkinCache / bind-pose)
		FShaderResourceViewRHIRef UVSRV;
		FBufferRHIRef IndexBuffer;
		uint32 NumVertices = 0;
		uint32 FirstIndex = 0;
		uint32 NumIndices = 0;
		uint32 NumTexCoords = 1;
		uint32 TangentFormat = 0;
		uint32 bHasTangents = 0;
		uint32 bHasUVs = 0;
		bool bUseSkinCacheSRV = false;
	};

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
		R.LocalToWorldInverseTranspose = Item.LocalToWorld.Inverse().GetTransposed();
		R.IndexBuffer = Item.IndexBuffer;
		R.NumVertices = Item.NumVertices;
		R.FirstIndex = Item.FirstIndex;
		R.NumIndices = Item.NumIndices;
		R.StaticPositionBuffer = Item.FallbackPositionBuffer;
		R.UVSRV = Item.UVSRV;
		R.NumTexCoords = FMath::Max(1u, Item.NumTexCoords);
		R.bHasUVs = Item.UVSRV.IsValid() ? 1u : 0u;
		R.TangentSRV = Item.TangentSRV;
		R.TangentFormat = Item.TangentFormat;
		R.bHasTangents = Item.TangentSRV.IsValid() ? 1u : 0u;
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
					R.TangentSRV = Sec.TangentBuffer;
					R.bHasTangents = Sec.TangentBuffer ? 1u : 0u;
					R.TangentFormat = static_cast<uint32>(Sec.TangentFormat);
					if (Sec.UVsBuffer)
					{
						R.UVSRV = Sec.UVsBuffer;
						R.NumTexCoords = FMath::Max(1u, Sec.UVsChannelCount);
						R.bHasUVs = 1u;
					}
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
			if (Item.Source != EGlassBackfaceSource::StaticMesh)
			{
				R.NumVertices = FMath::Max(R.NumVertices, static_cast<uint32>(Item.BaseVertexIndex) + Item.NumVertices);
				++NumFallbacks;
			}
		}

		Resolved.Add(MoveTemp(R));
	}

	if (Resolved.Num() == 0)
	{
		static double LastResolveEmpty = 0.0;
		const double Now = FPlatformTime::Seconds();
		if (Now - LastResolveEmpty > 2.0)
		{
			LastResolveEmpty = Now;
			UE_LOG(LogGlassDualPass, Warning,
				TEXT("[%s] %d payload draws → 0 resolved (SkinCache/buffers)"), PassTag, Payload->Draws.Num());
		}
		return;
	}

	FRHITexture* BlackTex = GBlackTexture ? GBlackTexture->TextureRHI.GetReference() : nullptr;
	FRHITexture* BlackCube = GBlackTextureCube ? GBlackTextureCube->TextureRHI.GetReference() : BlackTex;
	// DataA/B: shader currently uses mid-range constants (bake sample disabled). Still bind fallbacks.
	FRHITexture* DataARHI = Payload->DataA.IsValid() ? Payload->DataA.GetReference() : BlackTex;
	FRHITexture* DataBRHI = Payload->DataB.IsValid() ? Payload->DataB.GetReference() : BlackTex;
	FRHITexture* EnvRHI = Payload->EnvMap.IsValid() ? Payload->EnvMap.GetReference() : BlackCube;
	FRHITexture* ColorsRHI = Payload->ColorsMap.IsValid() ? Payload->ColorsMap.GetReference() : BlackTex;
	if (!EnvRHI || !ColorsRHI)
	{
		UE_LOG(LogGlassDualPass, Warning, TEXT("[%s] Missing env/colors RHI; skip"), PassTag);
		return;
	}
	if (!DataARHI)
	{
		DataARHI = BlackTex;
	}
	if (!DataBRHI)
	{
		DataBRHI = BlackTex;
	}

	const FVector3f CameraWorld(View.ViewMatrices.GetViewOrigin());
	const float Ior = CVarGlassIor.GetValueOnRenderThread();
	const float UseTrans = CVarGlassUseTransmittance.GetValueOnRenderThread();
	const float EnvRefr = CVarGlassEnvRefraction.GetValueOnRenderThread();
	const float FringeCurve = CVarGlassFringeCurve.GetValueOnRenderThread();
	const float FringeMix = CVarGlassFringeMix.GetValueOnRenderThread();
	const float IridW = CVarGlassRefractionIridescence.GetValueOnRenderThread();
	const float DistScale = CVarGlassDistScale.GetValueOnRenderThread();

	// SceneColor buffer mapping — same as UE ViewportUVToBufferUV.
	const FIntRect SceneViewRect = UE::FXRenderingUtils::GetRawViewRectUnsafe(View);
	const FIntPoint SceneBufferExtent = SceneColorSample->Desc.Extent;
	const FVector2f SceneViewRectMinF(
		static_cast<float>(SceneViewRect.Min.X),
		static_cast<float>(SceneViewRect.Min.Y));
	const FVector2f SceneViewSizeF(
		static_cast<float>(FMath::Max(SceneViewRect.Width(), 1)),
		static_cast<float>(FMath::Max(SceneViewRect.Height(), 1)));
	const FVector2f SceneBufferInvSizeF(
		1.0f / static_cast<float>(FMath::Max(SceneBufferExtent.X, 1)),
		1.0f / static_cast<float>(FMath::Max(SceneBufferExtent.Y, 1)));
	// Soft edge ~2% of viewport (no hard UV saturate banding).
	const float SceneEdgeSoftness = 0.02f;

	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(View.GetFeatureLevel());
	TShaderMapRef<FGlassBackfaceVS> VertexShader(ShaderMap);
	TShaderMapRef<FGlassBackfaceSkinCacheVS> SkinCacheVS(ShaderMap);
	TShaderMapRef<FGlassBackfacePS> PixelShader(ShaderMap);

	if (!VertexShader.IsValid() || !SkinCacheVS.IsValid() || !PixelShader.IsValid())
	{
		UE_LOG(LogGlassDualPass, Error, TEXT("[%s] Global shaders not valid (compile failed?)"), PassTag);
		return;
	}

	FGlassBackfacePassParameters* PassParameters = GraphBuilder.AllocParameters<FGlassBackfacePassParameters>();
	PassParameters->SceneColorTexture = SceneColorSample;
	PassParameters->RenderTargets[0] = FRenderTargetBinding(GlassBackRDG, ERenderTargetLoadAction::ELoad);

	FRHIShaderResourceView* NullSRV = GNullVertexBuffer.VertexBufferSRV.GetReference();
	const int32 DrawCount = Resolved.Num();

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("GlassDualPass_%s(%d)", PassTag, DrawCount),
		PassParameters,
		ERDGPassFlags::Raster,
		[Resolved = MoveTemp(Resolved), ViewProjection44, TextureExtent, VertexShader, SkinCacheVS, PixelShader,
			SceneColorSample, DataARHI, DataBRHI, EnvRHI, ColorsRHI, CameraWorld, Ior, UseTrans, EnvRefr,
			FringeCurve, FringeMix, IridW, DistScale, NullSRV, PassTag,
			SceneViewRectMinF, SceneViewSizeF, SceneBufferInvSizeF, SceneEdgeSoftness](FRHICommandList& RHICmdList)
		{
			// RT is a scaled capture of the same NDC as ViewProjection; full RT = full view.
			RHICmdList.SetViewport(
				0.0f, 0.0f, 0.0f,
				static_cast<float>(TextureExtent.X), static_cast<float>(TextureExtent.Y), 1.0f);

			FRHITexture* SceneColorRHI = SceneColorSample->GetRHI();
			if (!SceneColorRHI)
			{
				return;
			}

			auto FillPS = [&](FGlassBackfacePS::FParameters& PSParams)
			{
				PSParams.ViewProjection = ViewProjection44;
				PSParams.CameraWorldPos = CameraWorld;
				PSParams.SceneViewRectMin = SceneViewRectMinF;
				PSParams.SceneViewSize = SceneViewSizeF;
				PSParams.SceneBufferInvSize = SceneBufferInvSizeF;
				PSParams.SceneEdgeSoftness = SceneEdgeSoftness;
				PSParams.IorStart = Ior;
				PSParams.UseTransmittance = UseTrans;
				PSParams.EnvRefraction = EnvRefr;
				PSParams.FringeCurve = FringeCurve;
				PSParams.FringeMix = FringeMix;
				PSParams.FringeColor = GGlassFringeColor;
				PSParams.RefractionIridescence = IridW;
				PSParams.DistScale = DistScale;
				PSParams.DataATexture = DataARHI;
				PSParams.DataBTexture = DataBRHI;
				PSParams.EnvMapTexture = EnvRHI;
				PSParams.ColorsMapTexture = ColorsRHI;
				PSParams.SceneColorTexture = SceneColorRHI;
				PSParams.DataSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
				PSParams.EnvSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
				PSParams.ColorsSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
				// Border-safe: out-of-range UV handled by EdgeMask (not hard clamp into edge texels).
				PSParams.SceneSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
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
					FGraphicsPipelineStateInitializer GraphicsPSOInit;
					RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
					GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GEmptyVertexDeclaration.VertexDeclarationRHI;
					GraphicsPSOInit.BoundShaderState.VertexShaderRHI = SkinCacheVS.GetVertexShader();
					GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
					GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_CCW>::GetRHI();
					GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
					GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
					GraphicsPSOInit.PrimitiveType = PT_TriangleList;
					SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, 0);

					FGlassBackfaceSkinCacheVS::FParameters VSParams;
					VSParams.LocalToWorld = Item.LocalToWorld;
					VSParams.LocalToWorldInverseTranspose = Item.LocalToWorldInverseTranspose;
					VSParams.ViewProjection = ViewProjection44;
					VSParams.PositionSRV = Item.PositionSRV;
					VSParams.TangentSRV = Item.TangentSRV.IsValid() ? Item.TangentSRV.GetReference() : NullSRV;
					VSParams.UVSRV = Item.UVSRV.IsValid() ? Item.UVSRV.GetReference() : NullSRV;
					VSParams.NumTexCoords = Item.NumTexCoords;
					VSParams.TangentFormat = Item.TangentFormat;
					VSParams.bHasTangents = Item.bHasTangents && Item.TangentSRV.IsValid() ? 1u : 0u;
					VSParams.bHasUVs = Item.bHasUVs && Item.UVSRV.IsValid() ? 1u : 0u;
					if (!VSParams.TangentSRV || !VSParams.UVSRV)
					{
						continue;
					}
					SetShaderParameters(RHICmdList, SkinCacheVS, SkinCacheVS.GetVertexShader(), VSParams);

					FGlassBackfacePS::FParameters PSParams;
					FillPS(PSParams);
					SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), PSParams);

					RHICmdList.DrawIndexedPrimitive(
						Item.IndexBuffer, 0, 0, Item.NumVertices, Item.FirstIndex, Item.NumIndices / 3, 1);
					++Drawn;
				}
				else if (Item.StaticPositionBuffer)
				{
					FGraphicsPipelineStateInitializer GraphicsPSOInit;
					RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
					GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GetGlassBackfaceVertexDeclarationRHI();
					GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
					GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
					GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_CCW>::GetRHI();
					GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
					GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
					GraphicsPSOInit.PrimitiveType = PT_TriangleList;
					SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, 0);

					FGlassBackfaceVS::FParameters VSParams;
					VSParams.LocalToWorld = Item.LocalToWorld;
					VSParams.LocalToWorldInverseTranspose = Item.LocalToWorldInverseTranspose;
					VSParams.ViewProjection = ViewProjection44;
					VSParams.TangentSRV = Item.TangentSRV.IsValid() ? Item.TangentSRV.GetReference() : NullSRV;
					VSParams.UVSRV = Item.UVSRV.IsValid() ? Item.UVSRV.GetReference() : NullSRV;
					VSParams.NumTexCoords = Item.NumTexCoords;
					VSParams.TangentFormat = Item.TangentFormat;
					VSParams.bHasTangents = Item.bHasTangents && Item.TangentSRV.IsValid() ? 1u : 0u;
					VSParams.bHasUVs = Item.bHasUVs && Item.UVSRV.IsValid() ? 1u : 0u;
					if (!VSParams.UVSRV || !VSParams.TangentSRV)
					{
						continue;
					}
					SetShaderParameters(RHICmdList, VertexShader, VertexShader.GetVertexShader(), VSParams);

					FGlassBackfacePS::FParameters PSParams;
					FillPS(PSParams);
					SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), PSParams);

					RHICmdList.SetStreamSource(0, Item.StaticPositionBuffer, 0);
					RHICmdList.DrawIndexedPrimitive(
						Item.IndexBuffer, 0, 0, Item.NumVertices, Item.FirstIndex, Item.NumIndices / 3, 1);
					++Drawn;
				}
			}

			static double LastDrawLog = 0.0;
			const double Now = FPlatformTime::Seconds();
			if (Now - LastDrawLog > 2.0)
			{
				LastDrawLog = Now;
				UE_LOG(LogGlassDualPass, Log,
					TEXT("[%s] Drew %d/%d backfaces → RT %dx%d"),
					PassTag, Drawn, Resolved.Num(), TextureExtent.X, TextureExtent.Y);
			}
		});
}

void FGlassDualPassViewExtension::PrePostProcessPass_RenderThread(
	FRDGBuilder& GraphBuilder,
	const FSceneView& View,
	const FPostProcessingInputs& Inputs)
{
	if (!IsPrimaryNonCaptureView(View))
	{
		return;
	}

	// Only path: after deferred lighting — SceneColor is lit.
	FRDGTextureRef SceneColor = nullptr;
	if (Inputs.SceneTextures)
	{
		SceneColor = (*Inputs.SceneTextures)->SceneColorTexture;
	}

	ExecuteGlassBackfacePass(GraphBuilder, View, SceneColor, /*bCopySceneColor=*/true, TEXT("PrePostProcess"));
}
