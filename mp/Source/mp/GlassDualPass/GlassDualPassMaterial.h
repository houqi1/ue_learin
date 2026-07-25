// Copyright Epic Games, Inc. All Rights Reserved.
// Backface material shell: M_PhoneixGlass_Back → Custom → GlassDualPassBackfaceCustom.usf
// Effect control surface: edit material parameters in the editor; runtime reads them into the Global backface PS.

#pragma once

#include "CoreMinimal.h"

class UMaterial;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UTexture;
class UTexture2D;
class UTextureCube;

/**
 * Loads the dual-pass backface material / material instance.
 * Shading body: Custom → GlassDualPassBackfaceCustom.usf → Lib.
 *
 * Authority model:
 *   Effect params → MI (preferred) or master material (editor UI)
 *   Runtime-only (SceneColor RT, ViewProjection, view rect) → DMI each frame
 *   CVars r.GlassDualPass.IOR etc. → fallback only when material is missing
 *
 * Path CVar: r.GlassDualPass.BackfaceMaterial
 */
namespace GlassDualPassMaterial
{
	/** Master material (parent of the instance). */
	static constexpr const TCHAR* BackfaceMaterialPath =
		TEXT("/Game/Phonix/Material/M_PhoneixGlass_Back.M_PhoneixGlass_Back");

	/** Default content MI used for rendering (override via r.GlassDualPass.BackfaceMaterial). */
	static constexpr const TCHAR* BackfaceMaterialInstancePath =
		TEXT("/Game/Phonix/Material/M_PhoneixGlass_Back_Inst.M_PhoneixGlass_Back_Inst");

	/** Effect parameters read from MI / material (game thread). */
	struct FShadingParams
	{
		float IorStart = 1.45f;
		float UseTransmittance = 1.f;
		float EnvRefraction = 0.35f;
		float FringeCurve = 2.f;
		float FringeMix = 0.25f;
		float RefractionIridescence = 0.85f;
		float DistScale = 1.f;
		float SceneEdgeSoftness = 0.02f;
		FLinearColor FringeColor = FLinearColor(0.55f, 0.75f, 1.f, 1.f);

		/** From material TextureObjectParameters (may be null → use subsystem fallbacks). */
		TObjectPtr<UTexture2D> DataA = nullptr;
		TObjectPtr<UTexture2D> DataB = nullptr;
		TObjectPtr<UTextureCube> EnvMap = nullptr;
		TObjectPtr<UTexture2D> ColorsMap = nullptr;

		bool bFromMaterial = false;
	};

	/**
	 * Load backface material interface (MI first, then master).
	 * Path from r.GlassDualPass.BackfaceMaterial, else BackfaceMaterialInstancePath,
	 * else BackfaceMaterialPath.
	 */
	UMaterialInterface* LoadOrCreateBackfaceMaterial();

	/** Soft path currently used for load (after CVar resolution). */
	FString GetConfiguredBackfaceMaterialPath();

	/** Editor: NewObject create master if missing (no MaterialEditingLibrary — no startup crash). */
	UMaterialInterface* CreateBackfaceMaterialIfMissing();

	/**
	 * Build a DMI from the configured MI / master (for SceneColor + view inject).
	 */
	UMaterialInstanceDynamic* CreateBackfaceMID(UObject* Outer);

	/**
	 * Read effect parameters from the given interface (MIC/MID/master values as resolved).
	 * Does not skip MI overrides — pass the MIC to use instance params.
	 */
	bool ReadShadingParamsFromMaterial(UMaterialInterface* Mat, FShadingParams& Out);

	/** CVar / hard-coded fallbacks when material is missing. */
	void FillShadingParamsFromCVars(FShadingParams& Out);

	/**
	 * Runtime-only inject into MID (does NOT write IOR/fringe/bake textures).
	 * SceneColor RT + view metrics so Custom preview can sample live scene if opened.
	 */
	void InjectRuntimeViewIntoMID(
		UMaterialInstanceDynamic* MID,
		UTexture* SceneColor,
		float SceneEdgeSoftness,
		const FVector2f& SceneViewRectMin,
		const FVector2f& SceneViewSize,
		const FVector2f& SceneBufferInvSize,
		const FMatrix44f& ViewProjection,
		const FVector3f& CameraWorldPos);
}
