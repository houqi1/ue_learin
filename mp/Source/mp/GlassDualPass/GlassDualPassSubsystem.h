// Copyright Epic Games, Inc. All Rights Reserved.
// Owns SceneViewExtension + preview RT + bake/env textures for backface shading.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/EngineSubsystem.h"
#include "GlassDualPassMaterial.h"
#include "GlassDualPassSubsystem.generated.h"

class FGlassDualPassViewExtension;
class FTextureRenderTargetResource;
class UTextureRenderTarget2D;
class UTexture2D;
class UTextureCube;
class UMaterialInterface;
class UMaterialInstanceDynamic;

/**
 * Engine subsystem for glass dual-pass (D1).
 * Preview RT: /Game/Phonix/RT_GlassBack
 * Effect control: M_PhoneixGlass_Back material parameters (editor).
 * Geometry RT draw: Global VS/PS + shared Lib (SkinCache).
 */
UCLASS()
class UGlassDualPassSubsystem : public UEngineSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintCallable, Category = "GlassDualPass")
	UTextureRenderTarget2D* GetPreviewRenderTarget() const { return PreviewRT; }

	/** Backface Unlit material (Custom → GlassDualPassBackfaceCustom.usf). */
	UFUNCTION(BlueprintCallable, Category = "GlassDualPass")
	UMaterialInterface* GetBackfaceMaterial() const { return BackfaceMaterial; }

	UFUNCTION(BlueprintCallable, Category = "GlassDualPass")
	UMaterialInstanceDynamic* GetBackfaceMaterialMID() const { return BackfaceMID; }

	UTextureRenderTarget2D* GetOrCreatePreviewRT();
	void EnsurePreviewRTSize(int32 SizeX, int32 SizeY);
	FTextureRenderTargetResource* GetPreviewRTResource();

	/** Lazy-load bake/env/LUT textures as fallbacks when material params are empty. */
	void EnsureShadingTextures();

	/** Load M_PhoneixGlass_Back + runtime MID (safe; no auto-create at init). */
	void EnsureBackfaceMaterial();

	/** Force another load attempt (e.g. after authoring the asset in-session). */
	void ResetBackfaceMaterialLoadAttempt();

	/**
	 * Read effect params from M_PhoneixGlass_Back (material authority).
	 * Fills fallbacks for empty texture pins from subsystem cache.
	 * Game thread only.
	 */
	GlassDualPassMaterial::FShadingParams GatherShadingParamsFromMaterial();

	/**
	 * Inject only runtime SceneColor + view metrics into MID (never overwrites effect params).
	 */
	void InjectRuntimeIntoBackfaceMID(
		UTexture* SceneColor,
		const FVector2f& SceneViewRectMin,
		const FVector2f& SceneViewSize,
		const FVector2f& SceneBufferInvSize,
		float SceneEdgeSoftness,
		const FMatrix44f& ViewProjection,
		const FVector3f& CameraWorldPos);

	/** Lit SceneColor copy for backface material Custom (optional). */
	UTextureRenderTarget2D* GetOrCreateSceneColorCopyRT(int32 SizeX, int32 SizeY);
	UTextureRenderTarget2D* GetSceneColorCopyRT() const { return SceneColorCopyRT; }

	/**
	 * Push RT_GlassBack + BackfaceWeight onto front glass DMIs (closed dual-pass loop).
	 * Front Custom should #include PhoneixGlassDualFront.usf and sample GlassBackRT.
	 */
	void ApplyFrontGlassBindings(UWorld* World, float BackfaceWeight);

	UTexture2D* GetDataA() const { return DataA; }
	UTexture2D* GetDataB() const { return DataB; }
	UTextureCube* GetEnvMap() const { return EnvMap; }
	UTexture2D* GetColorsMap() const { return ColorsMap; }

private:
	void OnPostEngineInit();

	TSharedPtr<FGlassDualPassViewExtension, ESPMode::ThreadSafe> ViewExtension;
	FDelegateHandle PostEngineInitHandle;

	UPROPERTY()
	TObjectPtr<UTextureRenderTarget2D> PreviewRT;

	/** Game-thread readable copy of lit SceneColor for material MID. */
	UPROPERTY()
	TObjectPtr<UTextureRenderTarget2D> SceneColorCopyRT;

	UPROPERTY()
	TObjectPtr<UTexture2D> DataA;

	UPROPERTY()
	TObjectPtr<UTexture2D> DataB;

	/** HDR studio environment — asset is TextureCube, not Texture2D. */
	UPROPERTY()
	TObjectPtr<UTextureCube> EnvMap;

	UPROPERTY()
	TObjectPtr<UTexture2D> ColorsMap;

	/** Shell material: Custom #include GlassDualPassBackfaceCustom.usf */
	UPROPERTY()
	TObjectPtr<UMaterialInterface> BackfaceMaterial;

	/** Runtime instance — only SceneColor / view vectors pushed (not effect params). */
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> BackfaceMID;

	/** Avoid per-frame LoadObject spam when an asset is missing / wrong type. */
	bool bTriedLoadDataA = false;
	bool bTriedLoadDataB = false;
	bool bTriedLoadEnvMap = false;
	bool bTriedLoadColorsMap = false;
	bool bTriedBackfaceMaterial = false;
};
