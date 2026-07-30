// Runtime post-process host: displays engine SceneTexture Velocity (motion vectors).
// Pair with material /Game/FX/Debug/M_PP_DebugMotionVector (create via Tools script or console).

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DebugMotionVectorView.generated.h"

class UPostProcessComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;

UCLASS(Blueprintable, meta = (DisplayName = "Debug Motion Vector View"))
class ADebugMotionVectorView : public AActor
{
	GENERATED_BODY()

public:
	ADebugMotionVectorView();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	/** Enable/disable the fullscreen MV overlay at runtime. */
	UFUNCTION(BlueprintCallable, Category = "DebugMotionVector")
	void SetOverlayEnabled(bool bEnabled);

	UFUNCTION(BlueprintCallable, Category = "DebugMotionVector")
	void SetExposure(float InExposure);

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DebugMotionVector")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "DebugMotionVector")
	TObjectPtr<UPostProcessComponent> PostProcess;

	/** Parent material (Post Process domain). Created by Tools/create_pp_debug_motion_vector.py */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DebugMotionVector")
	TObjectPtr<UMaterialInterface> MotionVectorMaterial;

	/** Amplify Velocity.rg before bias to mid-gray (0.5). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DebugMotionVector", meta = (ClampMin = "0.0"))
	float Exposure = 30.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DebugMotionVector")
	float PostProcessPriority = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DebugMotionVector")
	bool bEnabledOnBeginPlay = true;

private:
	void EnsureMaterial();
	void ApplyBlendable();
	void RemoveBlendable();
	void PushParams();

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> MID;
};
