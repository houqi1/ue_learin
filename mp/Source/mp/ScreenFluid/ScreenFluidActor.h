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

	// --- Inject (Stam interact → u0/v0/dens0; only while pointer moves) ---
	/** Brush radius in UV. HTML demo ~ domain/20 → ~0.05 on unit square. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ScreenFluid|Inject", meta = (ClampMin = "0.001"))
	float ClickRadius = 0.05f;

	/**
	 * Stam-style velocity source multiplier (HTML vMult=50).
	 * InjectForce = (dx_cells, dy_cells) * VelocityMult  written to User.InjectForce;
	 * add_source does u += dt * InjectForce * brush.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ScreenFluid|Inject", meta = (ClampMin = "0.0"))
	float VelocityMult = 50.f;

	/** Density source amount (HTML dye strength; single channel). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ScreenFluid|Inject", meta = (ClampMin = "0.0"))
	float DensityAmount = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ScreenFluid|Inject")
	bool bRespondToLeftMouse = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ScreenFluid|Inject")
	bool bRespondToSpaceBar = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ScreenFluid|Inject")
	bool bContinuousWhileHeld = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ScreenFluid|Inject")
	bool bForceShowMouseCursor = true;

	/**
	 * Minimum |delta UV| to count as moving (else InjectPulse=0, no force).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ScreenFluid|Inject", meta = (ClampMin = "0.0"))
	float MinMoveUV = 0.0005f;

	// --- Stam solver params (pushed to Niagara User.*) ---
	/** Time step. Classic demo often 0.1; realtime try 1/60. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ScreenFluid|Solver", meta = (ClampMin = "0.0001"))
	float SimDt = 0.0166667f;

	/** Must match Grid2D SetNumCells / RT size. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ScreenFluid|Solver", meta = (ClampMin = "16", ClampMax = "2048"))
	int32 GridN = 512;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ScreenFluid|Solver", meta = (ClampMin = "0.0"))
	float Viscosity = 0.0001f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ScreenFluid|Solver", meta = (ClampMin = "0.0"))
	float Diffusion = 0.0001f;

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
	/** Stam u0/v0 for this frame (grid-cell delta * VelocityMult); zero if still. */
	FVector2D PendingInjectForce = FVector2D::ZeroVector;
	float PendingDensitySrc = 0.f;
	FVector2D LastMouseUV = FVector2D(0.5f, 0.5f);
	/** Previous frame UV while button held (for delta). */
	FVector2D PrevHeldMouseUV = FVector2D(0.5f, 0.5f);
	bool bHasPrevHeldMouseUV = false;
	float LogTimer = 0.f;
	float VerifyLogCooldown = 0.f;
	float PrevInjectPulse = 0.f;
	double LastNiagaraRestartTime = -1000.0;
};
