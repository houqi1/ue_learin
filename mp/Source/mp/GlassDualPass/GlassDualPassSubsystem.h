// Copyright Epic Games, Inc. All Rights Reserved.
// Owns SceneViewExtension + preview RT + bake/env textures for backface shading.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/EngineSubsystem.h"
#include "GlassDualPassSubsystem.generated.h"

class FGlassDualPassViewExtension;
class FTextureRenderTargetResource;
class UTextureRenderTarget2D;
class UTexture2D;
class UTextureCube;

/**
 * Engine subsystem for glass dual-pass (D1).
 * Preview RT: /Game/Phonix/RT_GlassBack
 * Backface shading samples T_Phoenix_DataA/B, colorsMap, env cube, lit SceneColor.
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

	UTextureRenderTarget2D* GetOrCreatePreviewRT();
	void EnsurePreviewRTSize(int32 SizeX, int32 SizeY);
	FTextureRenderTargetResource* GetPreviewRTResource();

	/** Lazy-load bake/env/LUT textures used by backface global shader (each path tried at most once). */
	void EnsureShadingTextures();

	UTexture2D* GetDataA() const { return DataA; }
	UTexture2D* GetDataB() const { return DataB; }
	UTextureCube* GetEnvMap() const { return EnvMap; }
	UTexture2D* GetColorsMap() const { return ColorsMap; }

private:
	TSharedPtr<FGlassDualPassViewExtension, ESPMode::ThreadSafe> ViewExtension;

	UPROPERTY()
	TObjectPtr<UTextureRenderTarget2D> PreviewRT;

	UPROPERTY()
	TObjectPtr<UTexture2D> DataA;

	UPROPERTY()
	TObjectPtr<UTexture2D> DataB;

	/** HDR studio environment — asset is TextureCube, not Texture2D. */
	UPROPERTY()
	TObjectPtr<UTextureCube> EnvMap;

	UPROPERTY()
	TObjectPtr<UTexture2D> ColorsMap;

	/** Avoid per-frame LoadObject spam when an asset is missing / wrong type. */
	bool bTriedLoadDataA = false;
	bool bTriedLoadDataB = false;
	bool bTriedLoadEnvMap = false;
	bool bTriedLoadColorsMap = false;
};
