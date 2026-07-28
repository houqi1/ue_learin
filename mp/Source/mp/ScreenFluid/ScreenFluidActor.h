// Niagara Grid2D fluid host: mouse inject → NS (Grid2D stages) → RT → PostProcess UV warp.
// Solver lives entirely in Niagara (Grid2D Collection + Simulation Stages + Export to RT).

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ScreenFluidActor.generated.h"

class UTextureRenderTarget2D;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UPostProcessComponent;
class UNiagaraComponent;
class UNiagaraSystem;

UCLASS(Blueprintable, meta = (DisplayName = "Screen Fluid Niagara Grid2D"))
class AScreenFluidActor : public AActor
{
	GENERATED_BODY()

public:
	AScreenFluidActor();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	UFUNCTION(BlueprintCallable, Category = "ScreenFluid")
	void InjectAtScreenUV(FVector2D ScreenUV, float StrengthOverride = -1.f, float RadiusOverride = -1.f);

	UFUNCTION(BlueprintPure, Category = "ScreenFluid")
	UTextureRenderTarget2D* GetVelocityRenderTarget() const { return VelocityRT; }

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ScreenFluid")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ScreenFluid")
	TObjectPtr<UPostProcessComponent> PostProcess;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ScreenFluid")
	TObjectPtr<UNiagaraComponent> NiagaraFluid;

	/** Niagara system with Grid2D stages + export velocity to RT. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ScreenFluid|Niagara")
	TObjectPtr<UNiagaraSystem> FluidSystem;

	/** Velocity field RT written by Niagara Export/SetRT stage (RGBA16f). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ScreenFluid|Niagara")
	TObjectPtr<UTextureRenderTarget2D> VelocityRT;

	/** Post process material sampling VelocityRT (param: VelocityField). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ScreenFluid|Materials")
	TObjectPtr<UMaterialInterface> DistortMaterial;

	// --- Inject ---
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ScreenFluid|Inject")
	float ClickStrength = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ScreenFluid|Inject", meta = (ClampMin = "0.001"))
	float ClickRadius = 0.08f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ScreenFluid|Inject")
	bool bRespondToLeftMouse = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ScreenFluid|Inject")
	bool bRespondToSpaceBar = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ScreenFluid|Inject")
	bool bContinuousWhileHeld = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ScreenFluid|Inject")
	bool bForceShowMouseCursor = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ScreenFluid|Inject")
	float PulseDecayPerSecond = 4.0f;

	/**
	 * Minimum mouse move in screen UV per frame to count as "moving".
	 * Below this: no inject (InjectPulse=0).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ScreenFluid|Inject", meta = (ClampMin = "0.0"))
	float MinMoveUV = 0.0008f;

	/**
	 * Scales |delta UV| into InjectPulse / strength (pulse ≈ saturate(move * MoveStrengthScale)).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ScreenFluid|Inject", meta = (ClampMin = "0.0"))
	float MoveStrengthScale = 80.f;

	// --- Display ---
	/** UV warp scale. Temporarily large so solid/non-zero RT is obvious in PIE. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ScreenFluid|Display", meta = (ClampMin = "0.0"))
	float MaxUVOffset = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ScreenFluid|Display", meta = (ClampMin = "0.0", ClampMax = "0.5"))
	float Chromatic = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ScreenFluid|Display", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DistortIntensity = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ScreenFluid|Display")
	float PostProcessPriority = 10.f;

	/**
	 * When true, post-process material parameter bShowRT=1:
	 * fullscreen displays the Velocity RT (no UV warp) so you can verify RT content.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ScreenFluid|Display")
	bool bShowDebugVelocity = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ScreenFluid|Debug")
	bool bLogInput = true;

	/**
	 * After SetVariable, read User.ClickUV / InjectPulse back from the Niagara component
	 * and log whether they match what we wrote (proves the click position entered the system).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ScreenFluid|Debug")
	bool bVerifyClickParams = true;

	/** Create RT at runtime if VelocityRT asset not assigned. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ScreenFluid|Niagara", meta = (ClampMin = "64", ClampMax = "1024"))
	int32 RuntimeRTSize = 512;

private:
	void EnsureVelocityRT();
	void EnsureMaterialInstances();
	void ApplyPostProcessBlendable();
	void RemovePostProcessBlendable();
	void SetupPlayerInputHelpers();
	void ActivateNiagara();
	/** Reset/Activate if inactive or complete; rate-limited diagnostics. */
	void EnsureNiagaraRunning(bool bLogIfFail);
	void PushNiagaraParams();
	/** Read-back User.ClickUV / pulse after SetVariable; log PASS/FAIL. */
	void VerifyNiagaraClickParams(bool bForceLog);
	void TickInput();
	bool GetMouseScreenUV(FVector2D& OutUV) const;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> DistortMID;

	bool bMouseWasDown = false;
	bool bSpaceWasDown = false;
	float InjectPulse = 0.f;
	FVector2D PendingInjectUV = FVector2D(0.5f, 0.5f);
	/** Unit direction of mouse motion in UV (this frame); zero when not moving. */
	FVector2D PendingInjectDir = FVector2D::ZeroVector;
	FVector2D LastMouseUV = FVector2D(0.5f, 0.5f);
	/** Previous frame UV while button held (for delta). */
	FVector2D PrevHeldMouseUV = FVector2D(0.5f, 0.5f);
	bool bHasPrevHeldMouseUV = false;
	float LogTimer = 0.f;
	float VerifyLogCooldown = 0.f;
	float PrevInjectPulse = 0.f;
	double LastNiagaraRestartTime = -1000.0;
};
