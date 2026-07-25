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
	/**
	 * Scheme 4: multiplies RT coverage alpha.
	 * 0 = always use SceneIn (SceneColor); 1 = full RT where a>0.
	 * Final mix: lerp(SceneIn, RT.rgb, RT.a * BackfaceWeight).
	 */
	static constexpr const TCHAR* BackfaceWeightParam = TEXT("BackfaceWeight");

	/**
	 * For each mesh using M_PhoneixGlass master chain: ensure DMI and push
	 * GlassBackRT + BackfaceWeight so PhoneixGlass.usf dual-pass mix works.
	 */
	void ApplyBackfaceRTToGlassMaterials(UWorld* World, UTextureRenderTarget2D* GlassBackRT, float BackfaceWeight);
}
