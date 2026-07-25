// Copyright Epic Games, Inc. All Rights Reserved.
// Front glass materials sample RT_GlassBack (dual-pass closed loop).

#pragma once

#include "CoreMinimal.h"

class UWorld;
class UTextureRenderTarget2D;
class UMeshComponent;

namespace GlassDualPassFront
{
	/** Texture param on front M_PhoneixGlass / MIs (Custom or TextureSample). */
	static constexpr const TCHAR* GlassBackRTParam = TEXT("GlassBackRT");
	/** 0 = ignore back RT; 1 = full replace SceneIn with backface. */
	static constexpr const TCHAR* BackfaceWeightParam = TEXT("BackfaceWeight");

	/**
	 * For each mesh using M_PhoneixGlass master chain: ensure DMI and push
	 * GlassBackRT + BackfaceWeight so PhoneixGlass.usf dual-pass mix works.
	 */
	void ApplyBackfaceRTToGlassMaterials(UWorld* World, UTextureRenderTarget2D* GlassBackRT, float BackfaceWeight);
}
