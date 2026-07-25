// Copyright Epic Games, Inc. All Rights Reserved.
// Glass dual-pass: Material Shader (M_PhoneixGlass_Back) draws backfaces → RT_GlassBack.

#pragma once

#include "SceneViewExtension.h"
#include "RenderGraphDefinitions.h"
#include "RHIResources.h"
#include "HAL/CriticalSection.h"

class FTextureRenderTargetResource;
class FPrimitiveSceneProxy;
class FMaterialRenderProxy;

/** One glass primitive to draw with the backface material override. */
struct FGlassMaterialProxyItem
{
	/** Valid for the frame gathered in BeginRenderViewFamily. */
	FPrimitiveSceneProxy* Proxy = nullptr;
};

struct FGlassDualPassFramePayload
{
	TArray<FGlassMaterialProxyItem> Proxies;

	FTextureRenderTargetResource* PreviewRTResource = nullptr;
	/** Lit SceneColor copy RT for material SceneColorTexture param. */
	FTextureRenderTargetResource* SceneColorCopyResource = nullptr;
	int32 ViewFamilyId = 0;

	/** Render-thread material proxy for M_PhoneixGlass_Back MID (override). */
	const FMaterialRenderProxy* BackfaceMaterialProxy = nullptr;
};

class FGlassDualPassViewExtension final : public FSceneViewExtensionBase
{
public:
	FGlassDualPassViewExtension(const FAutoRegister& AutoRegister);

	virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override {}
	virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override {}
	virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override;

	virtual bool IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const override;

	/**
	 * Sole draw path: after deferred lighting (lit SceneColor available).
	 * Draws glass meshes with M_PhoneixGlass_Back mesh material shaders → RT_GlassBack.
	 */
	virtual void PrePostProcessPass_RenderThread(
		FRDGBuilder& GraphBuilder,
		const FSceneView& View,
		const FPostProcessingInputs& Inputs) override;

private:
	void GatherGlassProxies(TArray<FGlassMaterialProxyItem>& OutProxies, const FSceneViewFamily& ViewFamily);

	void ExecuteGlassBackfaceMaterialPass(
		FRDGBuilder& GraphBuilder,
		const FSceneView& View,
		FRDGTextureRef SceneColorRDG,
		const TCHAR* PassTag);

	TSharedPtr<FGlassDualPassFramePayload, ESPMode::ThreadSafe> PublishedPayload;
	FCriticalSection GlassDataCS;
	int32 NextViewFamilyId = 1;
};
