// Copyright Epic Games, Inc. All Rights Reserved.
// Owns SceneViewExtension + preview RenderTarget (method A).

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/EngineSubsystem.h"
#include "GlassDualPassSubsystem.generated.h"

class FGlassDualPassViewExtension;
class FTextureRenderTargetResource;
class UTextureRenderTarget2D;

/**
 * Engine subsystem for glass dual-pass (D1).
 * Preview RT: /Game/Phonix/RT_GlassBack — sample with M_GlassBackPreview on a plane.
 *
 * Console:
 *   r.GlassDualPass 0=off 1=backfaces (Step3) 2=magenta clear (Step2)
 *   r.GlassDualPass.MasterMaterial — master name (default M_PhoneixGlass)
 *
 * Glass selection: StaticMeshComponent material slots that use M_PhoneixGlass,
 * or any Material Instance whose parent chain reaches M_PhoneixGlass.
 */
UCLASS()
class UGlassDualPassSubsystem : public UEngineSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Preview target cleared each frame when r.GlassDualPass=1. */
	UFUNCTION(BlueprintCallable, Category = "GlassDualPass")
	UTextureRenderTarget2D* GetPreviewRenderTarget() const { return PreviewRT; }

	/** Create RT if needed (Game Thread). */
	UTextureRenderTarget2D* GetOrCreatePreviewRT();

	/** Resize preview RT (Game Thread). */
	void EnsurePreviewRTSize(int32 SizeX, int32 SizeY);

	/**
	 * Ensure GPU resource exists and return it (Game Thread).
	 * Call after GetOrCreate / EnsureSize; fixes "RHI texture null" skips.
	 */
	FTextureRenderTargetResource* GetPreviewRTResource();

private:
	TSharedPtr<FGlassDualPassViewExtension, ESPMode::ThreadSafe> ViewExtension;

	UPROPERTY()
	TObjectPtr<UTextureRenderTarget2D> PreviewRT;
};
