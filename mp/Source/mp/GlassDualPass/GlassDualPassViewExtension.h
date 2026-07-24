// Copyright Epic Games, Inc. All Rights Reserved.
// D1 glass dual-pass Step3: SkinCache GPU positions + static mesh backfaces → RT_GlassBack.

#pragma once

#include "SceneViewExtension.h"
#include "RenderGraphDefinitions.h"
#include "RHIResources.h"
#include "HAL/CriticalSection.h"

class FTextureRenderTargetResource;
class FSkeletalMeshObject;

enum class EGlassBackfaceSource : uint8
{
	/** Static mesh position VB. */
	StaticMesh,
	/** Skeletal: bind SkinCache skinned position buffer on RT (preferred). */
	SkeletalSkinCache,
	/** Skeletal: bind-pose position RHI if SkinCache unavailable. */
	SkeletalBindPose,
};

/**
 * Lightweight draw description (no per-vertex CPU arrays).
 * Skeletal SkinCache VB is resolved on the render thread from MeshObject.
 */
struct FGlassBackfaceDrawItem
{
	FMatrix44f LocalToWorld = FMatrix44f::Identity;

	/** Static / bind-pose position VB (also fallback if SkinCache miss). */
	FBufferRHIRef FallbackPositionBuffer;
	FBufferRHIRef IndexBuffer;

	/** Valid only until detach; used on render thread same frame. */
	const FSkeletalMeshObject* MeshObject = nullptr;

	/** LOD render-section index for FGPUSkinCache::GetPositionBuffer(..., SectionIndex). */
	int32 SkinSectionIndex = INDEX_NONE;

	uint32 NumVertices = 0;
	uint32 FirstIndex = 0;
	uint32 NumIndices = 0;
	/** Section.BaseVertexIndex — used with section-local SkinCache VB (may be negated at draw). */
	int32 BaseVertexIndex = 0;

	EGlassBackfaceSource Source = EGlassBackfaceSource::StaticMesh;
};

struct FGlassDualPassFramePayload
{
	TArray<FGlassBackfaceDrawItem> Draws;
	FTextureRenderTargetResource* PreviewRTResource = nullptr;
	int32 ViewFamilyId = 0;
};

class FGlassDualPassViewExtension final : public FSceneViewExtensionBase
{
public:
	FGlassDualPassViewExtension(const FAutoRegister& AutoRegister);

	virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override {}
	virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override {}
	virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override;

	virtual bool IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const override;

	virtual void PostRenderBasePassDeferred_RenderThread(
		FRDGBuilder& GraphBuilder,
		FSceneView& View,
		const FRenderTargetBindingSlots& RenderTargets,
		TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTextures) override;

private:
	void GatherInto(TArray<FGlassBackfaceDrawItem>& OutDraws, const FSceneViewFamily& ViewFamily);

	TSharedPtr<FGlassDualPassFramePayload, ESPMode::ThreadSafe> PublishedPayload;
	FCriticalSection GlassDataCS;
	int32 NextViewFamilyId = 1;
};
