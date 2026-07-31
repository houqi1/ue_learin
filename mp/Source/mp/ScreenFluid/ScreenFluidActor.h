// Niagara Grid2D fluid host: inject (mouse or Phoenix GBuffer) → NS stages → RT → PP warp.
// Solver lives in Niagara (Grid2D Collection + Simulation Stages + Export to RT).

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

	// --- Inject mode ---
	/**
	 * 0 = mouse brush (ClickUV / InjectForce / InjectPulse).
	 * 1 = Phoenix GBuffer (Stencil mask + reverse Scene Velocity) inside Niagara Inject stage.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ScreenFluid|Inject", meta = (ClampMin = "0", ClampMax = "1"))
	int32 InjectMode = 1;

	// --- Mouse inject (Mode 0) ---
	/** Brush radius in UV. HTML demo ~ domain/20 → ~0.05 on unit square. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ScreenFluid|Inject|Mouse", meta = (ClampMin = "0.001"))
	float ClickRadius = 0.05f;

	/**
	 * Stam-style velocity source multiplier (HTML vMult=50).
	 * InjectForce = (dx_cells, dy_cells) * VelocityMult.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ScreenFluid|Inject|Mouse", meta = (ClampMin = "0.0"))
	float VelocityMult = 50.f;

	/** Density source amount for mouse brush / also default InjectDensity for Phoenix. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ScreenFluid|Inject", meta = (ClampMin = "0.0"))
	float DensityAmount = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ScreenFluid|Inject|Mouse")
	bool bRespondToLeftMouse = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ScreenFluid|Inject|Mouse")
	bool bRespondToSpaceBar = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ScreenFluid|Inject|Mouse")
	bool bContinuousWhileHeld = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ScreenFluid|Inject|Mouse")
	bool bForceShowMouseCursor = true;

	/** Minimum |delta UV| to count as moving (else InjectPulse=0, no force). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ScreenFluid|Inject|Mouse", meta = (ClampMin = "0.0"))
	float MinMoveUV = 0.0005f;

	// --- Phoenix GBuffer inject (Mode 1) ---
	/** Must match mesh CustomDepth Stencil Value. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ScreenFluid|Inject|Phoenix", meta = (ClampMin = "0", ClampMax = "255"))
	int32 PhoenixStencilID = 1;

	/** Multiplier on -SceneVelocity before adding to fluid velocity. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ScreenFluid|Inject|Phoenix", meta = (ClampMin = "0.0"))
	float InjectStrength = 1.f;

	/** Convert engine motion vectors into fluid grid force units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ScreenFluid|Inject|Phoenix", meta = (ClampMin = "0.0"))
	float VelocityToFluid = 1.f;

	/** Ignore inject when |scene velocity| is below this (stops idle noise). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ScreenFluid|Inject|Phoenix", meta = (ClampMin = "0.0"))
	float MinSpeedUV = 0.0005f;

	/** If true, flips scene velocity Y before reverse inject (UV vs NDC mismatch). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ScreenFluid|Inject|Phoenix")
	bool bVelocityYFlip = false;

	/**
	 * Extra body inject along phoenix behind (UV unit dir from pawn facing).
	 * Force = BehindDir * BehindStrength on stencil mask; stacked on reverse SceneVel.
	 * Own category so it is easy to find after cold rebuild (not Live Coding only).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ScreenFluid|Phoenix Behind",
		meta = (ClampMin = "0.0", DisplayName = "Behind Strength"))
	float BehindStrength = 30.f;

	/**
	 * Optional explicit phoenix. If null, uses player pawn cast to APhoenixPawn,
	 * then first APhoenixPawn in world.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ScreenFluid|Phoenix Behind",
		meta = (DisplayName = "Phoenix Actor Override"))
	TObjectPtr<AActor> PhoenixActorOverride;

	// --- Stam solver params (pushed to Niagara User.*) ---
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
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ScreenFluid|Display", meta = (ClampMin = "0.0"))
	float MaxUVOffset = 0.12f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ScreenFluid|Display", meta = (ClampMin = "0.0", ClampMax = "0.5"))
	float Chromatic = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ScreenFluid|Display", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DistortIntensity = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ScreenFluid|Display")
	float PostProcessPriority = 10.f;

	/**
	 * When true, post-process material parameter bShowRT=1:
	 * fullscreen displays the Velocity RT (no UV warp).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ScreenFluid|Display")
	bool bShowDebugVelocity = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ScreenFluid|Debug")
	bool bLogInput = true;

	/**
	 * After SetVariable, read User.ClickUV / InjectPulse / InjectDir (and InjectForce alias)
	 * back from the Niagara component. PASS requires InjectDir match (primary for SF_Inject).
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
	void EnsureNiagaraRunning(bool bLogIfFail);
	void PushNiagaraParams();
	void VerifyNiagaraClickParams(bool bForceLog);
	void TickInput();
	bool GetMouseScreenUV(FVector2D& OutUV) const;

	/** Set float User param with both User.X and short X names. */
	void SetNiagaraFloat(FName UserName, FName ShortName, float Value);
	void SetNiagaraVec2(FName UserName, FName ShortName, FVector2D Value);

	/** Project phoenix behind (world) → UV unit direction for Mode 1 step (2). */
	void UpdatePhoenixBehindDirUV();

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> DistortMID;

	bool bMouseWasDown = false;
	bool bSpaceWasDown = false;
	float InjectPulse = 0.f;
	FVector2D PendingInjectUV = FVector2D(0.5f, 0.5f);
	/** Stam u0/v0 for this frame (grid-cell delta * VelocityMult); zero if still. */
	FVector2D PendingInjectForce = FVector2D::ZeroVector;
	float PendingDensitySrc = 0.f;
	/** Mode 1: UV-space unit vector toward phoenix back (from mesh facing). */
	FVector2D PendingBehindDirUV = FVector2D::ZeroVector;
	FVector2D LastMouseUV = FVector2D(0.5f, 0.5f);
	FVector2D PrevHeldMouseUV = FVector2D(0.5f, 0.5f);
	bool bHasPrevHeldMouseUV = false;
	float LogTimer = 0.f;
	float VerifyLogCooldown = 0.f;
	float PrevInjectPulse = 0.f;
	double LastNiagaraRestartTime = -1000.0;
};
