// Copyright Epic Games, Inc. All Rights Reserved.
// Material Shader backface pass: M_PhoneixGlass_Back → RT_GlassBack (no Global PS).

#include "GlassDualPassViewExtension.h"

#include "GlassDualPassFront.h"
#include "GlassDualPassLogs.h"
#include "GlassDualPassMaterial.h"
#include "GlassDualPassMaterialMesh.h"
#include "GlassDualPassSubsystem.h"

#include "Components/MeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FXRenderingUtils.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "InstanceCulling/InstanceCullingContext.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "MeshPassProcessor.inl"
#include "PostProcess/PostProcessInputs.h"
#include "PrimitiveSceneInfo.h"
#include "PrimitiveSceneProxy.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "ScenePrivate.h"
#include "SceneRendering.h"
#include "SceneView.h"
#include "SimpleMeshDrawCommandPass.h"
#include "TextureResource.h"

/**
 * r.GlassDualPass
 * 0: disabled
 * 1: clear RT_GlassBack + material-shader backfaces
 * 2: clear RT_GlassBack to debug magenta only
 */
static TAutoConsoleVariable<int32> CVarGlassDualPass(
	TEXT("r.GlassDualPass"),
	1,
	TEXT("Glass dual-pass.\n")
	TEXT("0: disabled\n")
	TEXT("1: material shader backfaces (M_PhoneixGlass_Back) → RT_GlassBack\n")
	TEXT("2: clear RT_GlassBack to debug magenta only"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<FString> CVarGlassDualPassMasterMaterial(
	TEXT("r.GlassDualPass.MasterMaterial"),
	TEXT("M_PhoneixGlass"),
	TEXT("Front glass master material name (mesh filter)."),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarGlassMaxRTSize(
	TEXT("r.GlassDualPass.MaxRTSize"),
	1280,
	TEXT("Max dimension for RT_GlassBack (VRAM cap)."),
	ECVF_Default);

// Fallback only when M_PhoneixGlass_Back is missing (effect params live on material).
static TAutoConsoleVariable<float> CVarGlassIor(TEXT("r.GlassDualPass.IOR"), 1.45f, TEXT("Fallback IOR if backface material missing."), ECVF_RenderThreadSafe);
static TAutoConsoleVariable<float> CVarGlassUseTransmittance(TEXT("r.GlassDualPass.UseTransmittance"), 1.f, TEXT("Fallback transmittance if material missing."), ECVF_RenderThreadSafe);
static TAutoConsoleVariable<float> CVarGlassEnvRefraction(TEXT("r.GlassDualPass.EnvRefraction"), 0.35f, TEXT("Fallback env refraction if material missing."), ECVF_RenderThreadSafe);
static TAutoConsoleVariable<float> CVarGlassFringeCurve(TEXT("r.GlassDualPass.FringeCurve"), 2.f, TEXT("Fallback fringe curve if material missing."), ECVF_RenderThreadSafe);
static TAutoConsoleVariable<float> CVarGlassFringeMix(TEXT("r.GlassDualPass.FringeMix"), 0.25f, TEXT("Fallback fringe mix if material missing."), ECVF_RenderThreadSafe);
static TAutoConsoleVariable<float> CVarGlassRefractionIridescence(TEXT("r.GlassDualPass.RefractionIridescence"), 0.85f, TEXT("Fallback iridescence if material missing."), ECVF_RenderThreadSafe);
static TAutoConsoleVariable<float> CVarGlassDistScale(TEXT("r.GlassDualPass.DistScale"), 1.f, TEXT("Fallback DistScale if material missing."), ECVF_RenderThreadSafe);
static TAutoConsoleVariable<float> CVarGlassFrontWeight(
	TEXT("r.GlassDualPass.FrontWeight"),
	0.65f,
	TEXT("Front Scheme4 weight: multiplies RT coverage alpha. Mix = lerp(SceneIn, RT.rgb, RT.a * weight). 0=SceneColor only, 1=full dual-pass."),
	ECVF_Default);

static const FLinearColor GGlassBackDebugMagenta(1.f, 0.f, 1.f, 1.f);
static const FLinearColor GGlassBackClearBlack(0.f, 0.f, 0.f, 0.f);

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

/**
 * Mesh material shaders require static UBs at fixed slots (View, Scene, InstanceCulling).
 * RDG binds them from this pass parameter struct — all three must be non-null.
 * Do NOT use AddSimpleMeshPass with null InstanceCullingManager: BuildRenderingCommands
 * Memzeros params and leaves Scene=null when the manager is missing.
 */
BEGIN_SHADER_PARAMETER_STRUCT(FGlassMatBackfacePassParameters, )
	SHADER_PARAMETER_STRUCT_INCLUDE(FViewShaderParameters, View)
	SHADER_PARAMETER_STRUCT_INCLUDE(FInstanceCullingDrawParams, InstanceCullingDrawParams)
	// RDG deps: ensure lit SceneColor is copied into the UTexture RT before mesh samples it.
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneColorTexture)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneColorCopyForMaterial)
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

FGlassDualPassViewExtension::FGlassDualPassViewExtension(const FAutoRegister& AutoRegister)
	: FSceneViewExtensionBase(AutoRegister)
{
	UE_LOG(LogGlassDualPass, Log,
		TEXT("FGlassDualPassViewExtension registered — Material Shader backface (M_PhoneixGlass_Back) @ PrePostProcess"));
}

bool FGlassDualPassViewExtension::IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const
{
	return CVarGlassDualPass.GetValueOnAnyThread() != 0;
}

void FGlassDualPassViewExtension::GatherGlassProxies(TArray<FGlassMaterialProxyItem>& OutProxies, const FSceneViewFamily& ViewFamily)
{
	OutProxies.Reset();

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
			if (FPrimitiveSceneProxy* Proxy = SMC->SceneProxy)
			{
				OutProxies.Add({Proxy});
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
			if (FPrimitiveSceneProxy* Proxy = SKC->SceneProxy)
			{
				OutProxies.Add({Proxy});
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
			TEXT("Glass material gather: %d proxy(s) [static=%d skeletal=%d]"),
			OutProxies.Num(), NumStatic, NumSkeletal);
	}
}

void FGlassDualPassViewExtension::BeginRenderViewFamily(FSceneViewFamily& InViewFamily)
{
	if (CVarGlassDualPass.GetValueOnGameThread() == 0)
	{
		return;
	}

	if (!ShouldGatherForViewFamily(InViewFamily) || !GEngine)
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
		return;
	}

	const int32 Mode = CVarGlassDualPass.GetValueOnGameThread();
	TSharedPtr<FGlassDualPassFramePayload, ESPMode::ThreadSafe> Payload =
		MakeShared<FGlassDualPassFramePayload, ESPMode::ThreadSafe>();
	Payload->PreviewRTResource = RTResource;
	Payload->ViewFamilyId = NextViewFamilyId++;

	Sys->EnsureShadingTextures();
	Sys->EnsureBackfaceMaterial();

	// SceneColor copy size for material SceneColorTexture
	int32 CopyW = 1280;
	int32 CopyH = 720;
	const int32 MaxDim = FMath::Max(64, CVarGlassMaxRTSize.GetValueOnGameThread());
	if (InViewFamily.RenderTarget)
	{
		const FIntPoint RTSize = InViewFamily.RenderTarget->GetSizeXY();
		CopyW = FMath::Max(RTSize.X, 1);
		CopyH = FMath::Max(RTSize.Y, 1);
	}
	else if (InViewFamily.Views.Num() > 0 && InViewFamily.Views[0])
	{
		const FIntRect& VR = InViewFamily.Views[0]->UnconstrainedViewRect;
		CopyW = FMath::Max(VR.Width(), 1);
		CopyH = FMath::Max(VR.Height(), 1);
	}
	if (CopyW > MaxDim || CopyH > MaxDim)
	{
		const float Scale = static_cast<float>(MaxDim) / static_cast<float>(FMath::Max(CopyW, CopyH));
		CopyW = FMath::Max(8, FMath::RoundToInt(CopyW * Scale));
		CopyH = FMath::Max(8, FMath::RoundToInt(CopyH * Scale));
	}
	UTextureRenderTarget2D* SceneCopy = Sys->GetOrCreateSceneColorCopyRT(CopyW, CopyH);

	// 1) Sync MIC → DMI first (artist knobs). MUST run before SceneColor inject —
	//    CopyParameterOverrides would wipe SceneColorTexture if done after.
	if (UMaterialInstanceDynamic* MID = Sys->GetBackfaceMaterialMID())
	{
		if (UMaterialInstance* ParentMI = Cast<UMaterialInstance>(Sys->GetBackfaceMaterial()))
		{
			if (!Cast<UMaterialInstanceDynamic>(ParentMI))
			{
				MID->CopyParameterOverrides(ParentMI);
			}
		}
	}

	// 2) Runtime inject last: SceneColor RT + view metrics / ViewProjection (refraction UV).
	if (InViewFamily.Views.Num() > 0 && InViewFamily.Views[0])
	{
		const FSceneView& View0 = *InViewFamily.Views[0];
		// Match Global-path style: raw view rect + buffer size for ViewportUV→BufferUV.
		const FIntRect ViewRect = View0.UnconstrainedViewRect;
		const FVector2f RectMin(static_cast<float>(ViewRect.Min.X), static_cast<float>(ViewRect.Min.Y));
		const FVector2f ViewSize(
			static_cast<float>(FMath::Max(ViewRect.Width(), 1)),
			static_cast<float>(FMath::Max(ViewRect.Height(), 1)));
		// Buffer inv must match SceneCopy extent (where we blit lit SceneColor).
		const FVector2f BufInv(
			1.f / static_cast<float>(FMath::Max(CopyW, 1)),
			1.f / static_cast<float>(FMath::Max(CopyH, 1)));
		const FMatrix44f VP(View0.ViewMatrices.GetWorldToClip());
		const FVector3f Cam(View0.ViewMatrices.GetViewOrigin());

		GlassDualPassMaterial::FShadingParams Shade;
		if (IsValid(Sys->GetBackfaceMaterial()))
		{
			GlassDualPassMaterial::ReadShadingParamsFromMaterial(Sys->GetBackfaceMaterial(), Shade);
		}
		const float EdgeSoft = Shade.bFromMaterial ? Shade.SceneEdgeSoftness : 0.02f;

		Sys->InjectRuntimeIntoBackfaceMID(SceneCopy, RectMin, ViewSize, BufInv, EdgeSoft, VP, Cam);
	}

	// Mesh override uses DMI proxy (has SceneColor after inject).
	if (UMaterialInstanceDynamic* MID = Sys->GetBackfaceMaterialMID())
	{
		Payload->BackfaceMaterialProxy = MID->GetRenderProxy();
	}
	else if (UMaterialInterface* Mat = Sys->GetBackfaceMaterial())
	{
		Payload->BackfaceMaterialProxy = Mat->GetRenderProxy();
	}

	Payload->SceneColorCopyResource = IsValid(SceneCopy) ? SceneCopy->GameThread_GetRenderTargetResource() : nullptr;

	if (Mode == 1)
	{
		GatherGlassProxies(Payload->Proxies, InViewFamily);
	}

	// Front closed loop
	if (Mode == 1 && InViewFamily.Scene)
	{
		if (UWorld* World = InViewFamily.Scene->GetWorld())
		{
			Sys->ApplyFrontGlassBindings(World, CVarGlassFrontWeight.GetValueOnGameThread());
		}
	}

	{
		FScopeLock Lock(&GlassDataCS);
		PublishedPayload = Payload;
	}
}

void FGlassDualPassViewExtension::ExecuteGlassBackfaceMaterialPass(
	FRDGBuilder& GraphBuilder,
	const FSceneView& View,
	FRDGTextureRef SceneColorRDG,
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

	const FLinearColor ClearColor = (Mode == 2) ? GGlassBackDebugMagenta : GGlassBackClearBlack;
	AddClearRenderTargetPass(GraphBuilder, GlassBackRDG, ClearColor);

	if (Mode == 2)
	{
		return;
	}

	if (!Payload->BackfaceMaterialProxy)
	{
		static double LastNoMat = 0.0;
		const double Now = FPlatformTime::Seconds();
		if (Now - LastNoMat > 2.0)
		{
			LastNoMat = Now;
			UE_LOG(LogGlassDualPass, Warning,
				TEXT("[%s] No M_PhoneixGlass_Back material proxy — RT cleared only. Create/reload material."),
				PassTag);
		}
		return;
	}

	if (Payload->Proxies.Num() == 0)
	{
		static double LastEmpty = 0.0;
		const double Now = FPlatformTime::Seconds();
		if (Now - LastEmpty > 2.0)
		{
			LastEmpty = Now;
			UE_LOG(LogGlassDualPass, Log, TEXT("[%s] Mode1 clear black, 0 glass proxies"), PassTag);
		}
		return;
	}

	// Lit SceneColor (copy-before-sample) → UTexture RT bound as material SceneColorTexture.
	FRDGTextureRef SceneColorSample = nullptr;
	if (SceneColorRDG)
	{
		SceneColorSample = GraphBuilder.CreateTexture(SceneColorRDG->Desc, TEXT("GlassSceneColorCopy"));
		AddCopyTexturePass(GraphBuilder, SceneColorRDG, SceneColorSample);
	}
	if (!SceneColorSample)
	{
		const FRDGTextureDesc BlackDesc = FRDGTextureDesc::Create2D(
			FIntPoint(8, 8), PF_FloatRGBA, FClearValueBinding::Black,
			TexCreate_ShaderResource | TexCreate_RenderTargetable);
		SceneColorSample = GraphBuilder.CreateTexture(BlackDesc, TEXT("GlassBlackSceneFallback"));
		AddClearRenderTargetPass(GraphBuilder, SceneColorSample, FLinearColor::Black);
	}

	// Blit into the same UTextureRenderTarget the DMI samples as SceneColorTexture.
	// Mesh pass must list this RDG texture as a read dependency so the copy runs first.
	FRDGTextureRef SceneCopyForMaterialRDG = nullptr;
	if (Payload->SceneColorCopyResource && SceneColorSample)
	{
		if (FRHITexture* CopyRHI = Payload->SceneColorCopyResource->GetRenderTargetTexture())
		{
			SceneCopyForMaterialRDG = GraphBuilder.RegisterExternalTexture(
				CreateRenderTarget(CopyRHI, TEXT("GlassSceneColorCopyRT")));
			if (SceneCopyForMaterialRDG)
			{
				const FIntPoint SrcExt = SceneColorSample->Desc.Extent;
				const FIntPoint DstExt = SceneCopyForMaterialRDG->Desc.Extent;
				// Prefer full same-size copy; else top-left subrect (capped MaxRTSize).
				if (SrcExt == DstExt)
				{
					AddCopyTexturePass(GraphBuilder, SceneColorSample, SceneCopyForMaterialRDG);
				}
				else
				{
					const FIntPoint CopySize(FMath::Min(SrcExt.X, DstExt.X), FMath::Min(SrcExt.Y, DstExt.Y));
					if (CopySize.X > 0 && CopySize.Y > 0)
					{
						AddCopyTexturePass(
							GraphBuilder, SceneColorSample, SceneCopyForMaterialRDG,
							FIntPoint::ZeroValue, FIntPoint::ZeroValue, CopySize);
					}
				}
			}
		}
	}
	if (!SceneCopyForMaterialRDG)
	{
		// Keep a valid RDG texture for pass params even if external RT missing.
		SceneCopyForMaterialRDG = SceneColorSample;
	}

	const FScene* Scene = View.Family && View.Family->Scene
		? View.Family->Scene->GetRenderScene()
		: nullptr;

	// Build proxy set for dynamic mesh filter
	TSet<const FPrimitiveSceneProxy*> ProxySet;
	ProxySet.Reserve(Payload->Proxies.Num());
	for (const FGlassMaterialProxyItem& Item : Payload->Proxies)
	{
		if (Item.Proxy)
		{
			ProxySet.Add(Item.Proxy);
		}
	}

	const FMaterialRenderProxy* OverrideProxy = Payload->BackfaceMaterialProxy;
	const FIntPoint TextureExtent(TextureRHI->GetSizeXY());
	const FIntRect PassViewport(0, 0, TextureExtent.X, TextureExtent.Y);

	// Prefer FViewInfo for DynamicMeshElements + SimpleMeshPass requirements
	if (!View.bIsViewInfo)
	{
		UE_LOG(LogGlassDualPass, Warning, TEXT("[%s] View is not FViewInfo — skip material mesh pass"), PassTag);
		return;
	}
	const FViewInfo& ViewInfo = static_cast<const FViewInfo&>(View);

	if (!Scene)
	{
		UE_LOG(LogGlassDualPass, Warning, TEXT("[%s] No FScene — skip material mesh pass"), PassTag);
		return;
	}

	FGlassMatBackfacePassParameters* PassParameters = GraphBuilder.AllocParameters<FGlassMatBackfacePassParameters>();
	PassParameters->SceneColorTexture = SceneColorSample;
	// Forces RDG to complete SceneColor→SceneCopy blit before mesh material samples the UTexture RT.
	PassParameters->SceneColorCopyForMaterial = SceneCopyForMaterialRDG;
	PassParameters->RenderTargets[0] = FRenderTargetBinding(GlassBackRDG, ERenderTargetLoadAction::ELoad);

	// --- Required static uniform buffers (must all be valid) ---
	// View / ResolvedView (slot View)
	PassParameters->View.View = ViewInfo.ViewUniformBuffer;
	PassParameters->View.InstancedView = ViewInfo.GetInstancedViewUniformBuffer();
	// Scene UB (slot Scene) — from the active scene renderer
	PassParameters->InstanceCullingDrawParams.Scene = ViewInfo.GetSceneUniforms().GetBuffer(GraphBuilder);
	// InstanceCulling UB (slot InstanceCulling) — dummy is enough when not using GPU instance cull
	PassParameters->InstanceCullingDrawParams.InstanceCulling =
		FInstanceCullingContext::CreateDummyInstanceCullingUniformBuffer(GraphBuilder);

	if (!PassParameters->View.View
		|| !PassParameters->InstanceCullingDrawParams.Scene
		|| !PassParameters->InstanceCullingDrawParams.InstanceCulling)
	{
		UE_LOG(LogGlassDualPass, Error,
			TEXT("[%s] Missing static UB (View/Scene/InstanceCulling) — skip material mesh pass"), PassTag);
		return;
	}

	FMeshPassProcessorRenderState DrawRenderState;
	DrawRenderState.SetBlendState(TStaticBlendState<>::GetRHI());
	DrawRenderState.SetDepthStencilState(TStaticDepthStencilState<false, CF_Always>::GetRHI());

	const int32 ProxyCount = Payload->Proxies.Num();
	const ERHIFeatureLevel::Type FeatureLevel = View.GetFeatureLevel();

	TArray<FPrimitiveSceneProxy*> ProxiesCopy;
	ProxiesCopy.Reserve(Payload->Proxies.Num());
	for (const FGlassMaterialProxyItem& Item : Payload->Proxies)
	{
		if (Item.Proxy)
		{
			ProxiesCopy.Add(Item.Proxy);
		}
	}

	// AddDrawDynamicMeshPass: does NOT wipe our UBs (unlike AddSimpleMeshPass + null manager).
	AddDrawDynamicMeshPass(
		GraphBuilder,
		RDG_EVENT_NAME("GlassDualPass_MaterialBackface(%d)", ProxyCount),
		PassParameters,
		View,
		PassViewport,
		[
			Scene,
			FeatureLevel,
			DrawRenderState,
			OverrideProxy,
			ProxiesCopy = MoveTemp(ProxiesCopy),
			&ViewInfo,
			ProxySet = MoveTemp(ProxySet),
			PassTag
		](FDynamicPassMeshDrawListContext* DynamicMeshPassContext)
		{
			FGlassMatBackfaceMeshProcessor Processor(
				Scene,
				FeatureLevel,
				&ViewInfo,
				DrawRenderState,
				DynamicMeshPassContext,
				OverrideProxy);

			const uint64 DefaultMask = ~0ull;
			int32 Added = 0;

			// 1) Static mesh batches from primitive scene info
			for (FPrimitiveSceneProxy* Proxy : ProxiesCopy)
			{
				if (!Proxy)
				{
					continue;
				}
				FPrimitiveSceneInfo* Info = Proxy->GetPrimitiveSceneInfo();
				if (!Info)
				{
					continue;
				}
				for (int32 MeshId = 0; MeshId < Info->StaticMeshes.Num(); ++MeshId)
				{
					const FStaticMeshBatch& StaticBatch = Info->StaticMeshes[MeshId];
					const FMeshBatch& MeshBatch = StaticBatch;
					if (MeshBatch.bUseForMaterial && MeshBatch.VertexFactory)
					{
						Processor.AddMeshBatch(MeshBatch, DefaultMask, Proxy, MeshId);
						++Added;
					}
				}
			}

			// 2) Dynamic elements (skeletal / movable) already gathered for this view
			for (int32 MeshIndex = 0; MeshIndex < ViewInfo.DynamicMeshElements.Num(); ++MeshIndex)
			{
				const FMeshBatchAndRelevance& MeshAndRel = ViewInfo.DynamicMeshElements[MeshIndex];
				if (!MeshAndRel.Mesh || !MeshAndRel.PrimitiveSceneProxy)
				{
					continue;
				}
				if (!ProxySet.Contains(MeshAndRel.PrimitiveSceneProxy))
				{
					continue;
				}
				Processor.AddMeshBatch(*MeshAndRel.Mesh, DefaultMask, MeshAndRel.PrimitiveSceneProxy);
				++Added;
			}

			static double LastDrawLog = 0.0;
			const double Now = FPlatformTime::Seconds();
			if (Now - LastDrawLog > 2.0)
			{
				LastDrawLog = Now;
				UE_LOG(LogGlassDualPass, Log,
					TEXT("[%s] Material mesh pass setup: %d batch add(s), %d proxies"),
					PassTag, Added, ProxiesCopy.Num());
			}
		},
		/*bForceStereoInstancingOff=*/true,
		/*bForceParallelSetupOff=*/true);
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

	FRDGTextureRef SceneColor = nullptr;
	if (Inputs.SceneTextures)
	{
		SceneColor = (*Inputs.SceneTextures)->SceneColorTexture;
	}

	ExecuteGlassBackfaceMaterialPass(GraphBuilder, View, SceneColor, TEXT("PrePostProcess"));
}
