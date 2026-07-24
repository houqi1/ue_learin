// Copyright Epic Games, Inc. All Rights Reserved.
// D1 glass dual-pass: SkinCache + static backfaces → RT_GlassBack.

#pragma once

#include "SceneViewExtension.h"
#include "RenderGraphDefinitions.h"
#include "RHIResources.h"
#include "HAL/CriticalSection.h"

class FTextureRenderTargetResource;
class FSkeletalMeshObject;

enum class EGlassBackfaceSource : uint8
{
	StaticMesh,
	SkeletalSkinCache,
	SkeletalBindPose,
};

struct FGlassBackfaceDrawItem
{
	FMatrix44f LocalToWorld = FMatrix44f::Identity;

	FBufferRHIRef FallbackPositionBuffer;
	FBufferRHIRef IndexBuffer;

	/** UV buffer (channel stride = NumTexCoords). Bake data on UV1. */
	FShaderResourceViewRHIRef UVSRV;
	uint32 NumTexCoords = 1;

	/**
	 * Tangent buffer for model normals (static / bind-pose).
	 * Layout matches engine StaticMeshVertexBuffer: [TangentX, TangentZ] pairs → TangentFormat=0.
	 * SkinCache path overrides from GetCachedGeometry on RT.
	 */
	FShaderResourceViewRHIRef TangentSRV;
	uint32 TangentFormat = 0; // 0 = interleaved X/Z (normal at *2+1)

	const FSkeletalMeshObject* MeshObject = nullptr;
	int32 SkinSectionIndex = INDEX_NONE;

	uint32 NumVertices = 0;
	uint32 FirstIndex = 0;
	uint32 NumIndices = 0;
	int32 BaseVertexIndex = 0;

	EGlassBackfaceSource Source = EGlassBackfaceSource::StaticMesh;
};

struct FGlassDualPassFramePayload
{
	TArray<FGlassBackfaceDrawItem> Draws;
	FTextureRenderTargetResource* PreviewRTResource = nullptr;
	int32 ViewFamilyId = 0;

	FTextureRHIRef DataA;
	FTextureRHIRef DataB;
	FTextureRHIRef EnvMap; // TextureCube RHI
	FTextureRHIRef ColorsMap;
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
	 * Not PostRenderBasePass — that stage is pre-lighting GBuffer/SceneColor.
	 */
	virtual void PrePostProcessPass_RenderThread(
		FRDGBuilder& GraphBuilder,
		const FSceneView& View,
		const FPostProcessingInputs& Inputs) override;

private:
	void GatherInto(TArray<FGlassBackfaceDrawItem>& OutDraws, const FSceneViewFamily& ViewFamily);

	/** Clear RT + optional shaded backface draws. SceneColorRDG may be null (uses black). */
	void ExecuteGlassBackfacePass(
		FRDGBuilder& GraphBuilder,
		const FSceneView& View,
		FRDGTextureRef SceneColorRDG,
		bool bCopySceneColor,
		const TCHAR* PassTag);

	TSharedPtr<FGlassDualPassFramePayload, ESPMode::ThreadSafe> PublishedPayload;
	FCriticalSection GlassDataCS;
	int32 NextViewFamilyId = 1;
};
