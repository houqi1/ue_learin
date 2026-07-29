#include "ScreenFluidActor.h"

#include "Camera/CameraComponent.h"
#include "Components/PostProcessComponent.h"
#include "Engine/LocalPlayer.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogScreenFluid, Log, All);

// User parameters expected on NS_ScreenFluid_Grid2D (must match Niagara system).
// Niagara accepts both "User.X" and "X"; set both so DI UserParam binding resolves.
namespace SFUser
{
	static const FName ClickUV(TEXT("User.ClickUV"));
	static const FName ClickUVShort(TEXT("ClickUV"));
	static const FName ClickRadius(TEXT("User.ClickRadius"));
	static const FName ClickRadiusShort(TEXT("ClickRadius"));
	static const FName InjectPulse(TEXT("User.InjectPulse"));
	static const FName InjectPulseShort(TEXT("InjectPulse"));
	static const FName InjectForce(TEXT("User.InjectForce"));
	static const FName InjectForceShort(TEXT("InjectForce"));
	static const FName DensitySrc(TEXT("User.DensitySrc"));
	static const FName DensitySrcShort(TEXT("DensitySrc"));
	static const FName Dt(TEXT("User.Dt"));
	static const FName DtShort(TEXT("Dt"));
	static const FName GridN(TEXT("User.GridN"));
	static const FName GridNShort(TEXT("GridN"));
	static const FName Viscosity(TEXT("User.Viscosity"));
	static const FName ViscosityShort(TEXT("Viscosity"));
	static const FName Diffusion(TEXT("User.Diffusion"));
	static const FName DiffusionShort(TEXT("Diffusion"));
	static const FName VelocityRT(TEXT("User.VelocityRT"));
	static const FName VelocityRTShort(TEXT("VelocityRT"));
}

namespace SFMat
{
	static const FName VelocityField(TEXT("VelocityField"));
	static const FName MaxOffset(TEXT("MaxOffset"));
	static const FName Chromatic(TEXT("Chromatic"));
	static const FName Intensity(TEXT("Intensity"));
	static const FName bDebugVelocity(TEXT("bDebugVelocity")); // legacy alias
	static const FName bShowRT(TEXT("bShowRT"));             // 1 = fullscreen show Velocity RT
}

AScreenFluidActor::AScreenFluidActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(SceneRoot);

	PostProcess = CreateDefaultSubobject<UPostProcessComponent>(TEXT("PostProcess"));
	PostProcess->SetupAttachment(SceneRoot);
	PostProcess->bUnbound = true;
	PostProcess->bEnabled = true;
	PostProcess->Priority = 10.f;
	PostProcess->BlendWeight = 1.f;

	NiagaraFluid = CreateDefaultSubobject<UNiagaraComponent>(TEXT("NiagaraFluid"));
	NiagaraFluid->SetupAttachment(SceneRoot);
	NiagaraFluid->SetAutoActivate(false);
	NiagaraFluid->SetAbsolute(true, true, true);
}

void AScreenFluidActor::BeginPlay()
{
	Super::BeginPlay();

	if (!FluidSystem)
	{
		FluidSystem = LoadObject<UNiagaraSystem>(
			nullptr, TEXT("/Game/FX/ScreenFluid/NS_ScreenFluid_Grid2D.NS_ScreenFluid_Grid2D"));
	}
	if (!DistortMaterial)
	{
		DistortMaterial = LoadObject<UMaterialInterface>(
			nullptr, TEXT("/Game/FX/ScreenFluid/M_PP_ScreenFluidDistort.M_PP_ScreenFluidDistort"));
	}
	if (!VelocityRT)
	{
		VelocityRT = LoadObject<UTextureRenderTarget2D>(
			nullptr, TEXT("/Game/FX/ScreenFluid/RT_ScreenFluidVelocity.RT_ScreenFluidVelocity"));
	}

	EnsureVelocityRT();
	EnsureMaterialInstances();
	ApplyPostProcessBlendable();
	SetupPlayerInputHelpers();
	ActivateNiagara();
	// No seed inject: density only when pointer moves while held.
	PushNiagaraParams();

	UE_LOG(LogScreenFluid, Warning,
		TEXT("ScreenFluid NIAGARA Grid2D host ready | System=%s RT=%s Distort=%s NiagaraActive=%d"),
		*GetNameSafe(FluidSystem),
		*GetNameSafe(VelocityRT),
		DistortMID ? TEXT("ok") : TEXT("NULL"),
		(NiagaraFluid && NiagaraFluid->IsActive()) ? 1 : 0);
}

void AScreenFluidActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (NiagaraFluid)
	{
		NiagaraFluid->DeactivateImmediate();
	}
	RemovePostProcessBlendable();
	Super::EndPlay(EndPlayReason);
}

void AScreenFluidActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	TickInput();

	// Keep Grid2D sim alive. If GPU script failed (coord uninitialized etc.),
	// IsActive stays false no matter how often we Activate — log that clearly.
	if (NiagaraFluid && FluidSystem)
	{
		if (!NiagaraFluid->GetAsset())
		{
			NiagaraFluid->SetAsset(FluidSystem);
		}
		EnsureNiagaraRunning(/*bLogIfFail=*/true);
	}

	PushNiagaraParams();

	// When injecting (or periodically), confirm ClickUV landed in Niagara user params.
	if (bVerifyClickParams)
	{
		const bool bInjecting = InjectPulse > 0.05f;
		const bool bPulseEdge = InjectPulse > 0.5f && PrevInjectPulse <= 0.5f;
		VerifyLogCooldown += DeltaSeconds;
		LogTimer += DeltaSeconds;

		const bool bTimeForPeriodic = LogTimer > 1.5f;
		const bool bTimeForInjectLog = bInjecting && (bPulseEdge || VerifyLogCooldown > 0.35f);
		if (bTimeForPeriodic || bTimeForInjectLog)
		{
			if (bTimeForPeriodic)
			{
				LogTimer = 0.f;
			}
			if (bTimeForInjectLog)
			{
				VerifyLogCooldown = 0.f;
			}
			VerifyNiagaraClickParams(/*bForceLog=*/bInjecting || bPulseEdge);
		}
	}
	else if (bLogInput)
	{
		LogTimer += DeltaSeconds;
		if (LogTimer > 1.5f)
		{
			LogTimer = 0.f;
			FVector2D UV;
			const bool bHas = GetMouseScreenUV(UV);
			UE_LOG(LogScreenFluid, Log,
				TEXT("Niagara host: mouse=%s uv=(%.2f,%.2f) pulse=%.2f active=%d"),
				bHas ? TEXT("yes") : TEXT("NO"), UV.X, UV.Y, InjectPulse,
				(NiagaraFluid && NiagaraFluid->IsActive()) ? 1 : 0);
		}
	}
	PrevInjectPulse = InjectPulse;

	if (DistortMID && VelocityRT)
	{
		DistortMID->SetTextureParameterValue(SFMat::VelocityField, VelocityRT);
		DistortMID->SetScalarParameterValue(SFMat::MaxOffset, MaxUVOffset);
		DistortMID->SetScalarParameterValue(SFMat::Chromatic, Chromatic);
		DistortMID->SetScalarParameterValue(SFMat::Intensity, DistortIntensity);
		// bShowRT: when true, PP draws VelocityField RT directly (debug view).
		DistortMID->SetScalarParameterValue(SFMat::bShowRT, bShowDebugVelocity ? 1.f : 0.f);
		DistortMID->SetScalarParameterValue(SFMat::bDebugVelocity, bShowDebugVelocity ? 1.f : 0.f);
	}

	if (PostProcess)
	{
		PostProcess->bEnabled = true;
		PostProcess->bUnbound = true;
		if (PostProcess->Settings.WeightedBlendables.Array.Num() == 0 && DistortMID)
		{
			ApplyPostProcessBlendable();
		}
	}
}

void AScreenFluidActor::InjectAtScreenUV(FVector2D ScreenUV, float StrengthOverride, float RadiusOverride)
{
	// Manual inject: place brush at UV with +X force (BP/debug).
	PendingInjectUV.X = FMath::Clamp(ScreenUV.X, 0.f, 1.f);
	PendingInjectUV.Y = FMath::Clamp(ScreenUV.Y, 0.f, 1.f);
	const float Mult = StrengthOverride >= 0.f ? StrengthOverride : VelocityMult;
	PendingInjectForce = FVector2D(Mult, 0.f);
	PendingDensitySrc = DensityAmount;
	InjectPulse = 1.f;
	if (RadiusOverride >= 0.f)
	{
		ClickRadius = RadiusOverride;
	}
	UE_LOG(LogScreenFluid, Log, TEXT("Inject UV=(%.3f,%.3f) force=(%.2f,%.2f) dens=%.1f"),
		PendingInjectUV.X, PendingInjectUV.Y, PendingInjectForce.X, PendingInjectForce.Y, PendingDensitySrc);
}

void AScreenFluidActor::EnsureVelocityRT()
{
	// Prefer solver GridN so RT / SetNumCells / User.GridN stay consistent.
	const int32 Want = FMath::Clamp(GridN > 0 ? GridN : RuntimeRTSize, 64, 2048);

	if (!VelocityRT)
	{
		VelocityRT = NewObject<UTextureRenderTarget2D>(this, TEXT("SF_VelocityRT_Runtime"));
		VelocityRT->ClearColor = FLinearColor(0, 0, 0, 0);
		VelocityRT->bAutoGenerateMips = false;
		VelocityRT->RenderTargetFormat = RTF_RGBA16f;
		VelocityRT->InitCustomFormat(Want, Want, PF_FloatRGBA, false);
		VelocityRT->UpdateResourceImmediate(true);
		UE_LOG(LogScreenFluid, Log, TEXT("Created runtime VelocityRT %dx%d"), Want, Want);
		return;
	}

	// Niagara RenderTarget2D DI can rewrite the user RT to DI.Size (e.g. 51x3) when
	// Inherit is off or User binding fails. Force correct size before Activate.
	const bool bBadSize = (VelocityRT->SizeX != Want) || (VelocityRT->SizeY != Want);
	const bool bBadFmt =
		VelocityRT->RenderTargetFormat != RTF_RGBA16f &&
		VelocityRT->GetFormat() != PF_FloatRGBA;
	if (bBadSize || bBadFmt || !VelocityRT->GetResource())
	{
		UE_LOG(LogScreenFluid, Warning,
			TEXT("VelocityRT was %dx%d fmt=%d — forcing %dx%d RGBA16f (Niagara DI may have resized it)"),
			VelocityRT->SizeX, VelocityRT->SizeY, (int32)VelocityRT->RenderTargetFormat, Want, Want);
		VelocityRT->bAutoGenerateMips = false;
		VelocityRT->bCanCreateUAV = true;
		VelocityRT->RenderTargetFormat = RTF_RGBA16f;
		VelocityRT->ClearColor = FLinearColor(0, 0, 0, 0);
		VelocityRT->InitCustomFormat(Want, Want, PF_FloatRGBA, false);
		VelocityRT->UpdateResourceImmediate(true);
	}
}

void AScreenFluidActor::EnsureMaterialInstances()
{
	if (DistortMaterial && !DistortMID)
	{
		DistortMID = UMaterialInstanceDynamic::Create(DistortMaterial, this);
	}
}

void AScreenFluidActor::ApplyPostProcessBlendable()
{
	if (!PostProcess || !DistortMID)
	{
		return;
	}
	PostProcess->Settings.WeightedBlendables.Array.Reset();
	PostProcess->Settings.WeightedBlendables.Array.Add(FWeightedBlendable(1.f, DistortMID));
	PostProcess->Priority = PostProcessPriority;
	PostProcess->BlendWeight = 1.f;
	PostProcess->bUnbound = true;
	PostProcess->bEnabled = true;

	if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
	{
		AActor* ViewTarget = PC->GetViewTarget() ? PC->GetViewTarget() : static_cast<AActor*>(PC->GetPawn());
		if (ViewTarget)
		{
			TArray<UCameraComponent*> Cams;
			ViewTarget->GetComponents<UCameraComponent>(Cams);
			for (UCameraComponent* Cam : Cams)
			{
				if (!Cam) continue;
				Cam->PostProcessSettings.WeightedBlendables.Array.RemoveAll([this](const FWeightedBlendable& B)
				{
					return B.Object == DistortMID;
				});
				Cam->PostProcessSettings.WeightedBlendables.Array.Add(FWeightedBlendable(1.f, DistortMID));
				Cam->PostProcessBlendWeight = 1.f;
			}
		}
	}
}

void AScreenFluidActor::RemovePostProcessBlendable()
{
	if (PostProcess)
	{
		PostProcess->Settings.WeightedBlendables.Array.Reset();
		PostProcess->bEnabled = false;
	}
}

void AScreenFluidActor::SetupPlayerInputHelpers()
{
	APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
	if (!PC || !bForceShowMouseCursor)
	{
		return;
	}
	PC->bShowMouseCursor = true;
	PC->bEnableClickEvents = true;
	FInputModeGameAndUI Mode;
	Mode.SetHideCursorDuringCapture(false);
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	PC->SetInputMode(Mode);
}

void AScreenFluidActor::ActivateNiagara()
{
	if (!NiagaraFluid)
	{
		UE_LOG(LogScreenFluid, Error, TEXT("NiagaraFluid component missing"));
		return;
	}
	if (!FluidSystem)
	{
		UE_LOG(LogScreenFluid, Error,
			TEXT("FluidSystem not set — assign NS_ScreenFluid_Grid2D (Niagara Grid2D gas + export stages)"));
		return;
	}

	// Component setup recommended for continuous GPU/Grid2D hosts
	NiagaraFluid->SetAutoActivate(false);
	NiagaraFluid->SetAutoDestroy(false);
	NiagaraFluid->SetAllowScalability(false);
	NiagaraFluid->SetForceSolo(true);
	NiagaraFluid->SetVisibility(true);
	NiagaraFluid->SetHiddenInGame(false);
	NiagaraFluid->SetAsset(FluidSystem);

	// Large fixed bounds so GPU sim is not culled (local space box)
	const FBox Fixed(FVector(-50000.f), FVector(50000.f));
	NiagaraFluid->SetSystemFixedBounds(Fixed);

	// Force RT size BEFORE Niagara starts
	EnsureVelocityRT();
	if (VelocityRT)
	{
		NiagaraFluid->SetVariableTextureRenderTarget(SFUser::VelocityRT, VelocityRT);
		NiagaraFluid->SetVariableTextureRenderTarget(SFUser::VelocityRTShort, VelocityRT);
	}

	// Params first, then start (and re-start if Complete)
	PushNiagaraParams();
	NiagaraFluid->ResetSystem();
	NiagaraFluid->ActivateSystem(true);
	NiagaraFluid->SetPaused(false);

	// Re-assert RT after first DI tick can resize
	EnsureVelocityRT();
	if (VelocityRT)
	{
		NiagaraFluid->SetVariableTextureRenderTarget(SFUser::VelocityRT, VelocityRT);
		NiagaraFluid->SetVariableTextureRenderTarget(SFUser::VelocityRTShort, VelocityRT);
	}

	const bool bActive = NiagaraFluid->IsActive();
	const bool bComplete = NiagaraFluid->IsComplete();
	UE_LOG(LogScreenFluid, Warning,
		TEXT("ActivateNiagara done | active=%d complete=%d asset=%s (if active=0 after this, check LogNiagara for ParticleGPUComputeScript / coord uninitialized)"),
		bActive ? 1 : 0, bComplete ? 1 : 0, *GetNameSafe(FluidSystem));
}

void AScreenFluidActor::EnsureNiagaraRunning(bool bLogIfFail)
{
	if (!NiagaraFluid || !FluidSystem)
	{
		return;
	}
	if (!NiagaraFluid->GetAsset())
	{
		NiagaraFluid->SetAsset(FluidSystem);
	}

	// Complete systems (loop once / no keep-alive) need Reset+Activate
	const bool bNeedStart = !NiagaraFluid->IsActive() || NiagaraFluid->IsComplete();
	if (!bNeedStart)
	{
		return;
	}

	// Avoid thrashing every frame if GPU script is broken — only retry every 2s
	const double Now = FPlatformTime::Seconds();
	if (Now - LastNiagaraRestartTime < 2.0)
	{
		return;
	}
	LastNiagaraRestartTime = Now;

	NiagaraFluid->SetAllowScalability(false);
	NiagaraFluid->SetForceSolo(true);
	NiagaraFluid->SetPaused(false);
	NiagaraFluid->ResetSystem();
	NiagaraFluid->ActivateSystem(true);

	const bool bActive = NiagaraFluid->IsActive();
	const bool bComplete = NiagaraFluid->IsComplete();
	if (bLogIfFail && !bActive)
	{
		if (bComplete)
		{
			// complete=1: lifecycle finished (Loop Once / empty emitter finished), not "didn't Activate".
			// Continuous Grid2D fluid needs System + Emitter State → Loop Behavior = Infinite.
			UE_LOG(LogScreenFluid, Error,
				TEXT("Niagara inactive | complete=1 (system finished same frame) | "
				     "Fix NS: System State and Emitter State → Loop Behavior = Infinite "
				     "(Empty GPU Grid2D with Loop Once completes immediately → active stays 0). "
				     "If LogNiagara still has ParticleGPUComputeScript / coord uninitialized, fix Sample UV first."));
		}
		else
		{
			UE_LOG(LogScreenFluid, Error,
				TEXT("Niagara inactive | complete=0 | often GPU script invalid: LogNiagara ParticleGPUComputeScript + coord uninitialized "
				     "(Sample Previous UV). C++ Activate cannot force active if GPU script failed."));
		}
	}
	else if (bLogIfFail && bActive)
	{
		UE_LOG(LogScreenFluid, Warning, TEXT("Niagara re-activated successfully | complete=%d"), bComplete ? 1 : 0);
	}
}

void AScreenFluidActor::PushNiagaraParams()
{
	if (!NiagaraFluid || !NiagaraFluid->GetAsset())
	{
		return;
	}

	const float N = static_cast<float>(FMath::Max(GridN, 16));
	NiagaraFluid->SetVariableVec2(SFUser::ClickUV, PendingInjectUV);
	NiagaraFluid->SetVariableVec2(SFUser::ClickUVShort, PendingInjectUV);
	NiagaraFluid->SetVariableVec2(SFUser::InjectForce, PendingInjectForce);
	NiagaraFluid->SetVariableVec2(SFUser::InjectForceShort, PendingInjectForce);
	NiagaraFluid->SetVariableFloat(SFUser::DensitySrc, PendingDensitySrc);
	NiagaraFluid->SetVariableFloat(SFUser::DensitySrcShort, PendingDensitySrc);
	NiagaraFluid->SetVariableFloat(SFUser::ClickRadius, ClickRadius);
	NiagaraFluid->SetVariableFloat(SFUser::ClickRadiusShort, ClickRadius);
	NiagaraFluid->SetVariableFloat(SFUser::InjectPulse, InjectPulse);
	NiagaraFluid->SetVariableFloat(SFUser::InjectPulseShort, InjectPulse);
	NiagaraFluid->SetVariableFloat(SFUser::Dt, SimDt);
	NiagaraFluid->SetVariableFloat(SFUser::DtShort, SimDt);
	NiagaraFluid->SetVariableFloat(SFUser::GridN, N);
	NiagaraFluid->SetVariableFloat(SFUser::GridNShort, N);
	NiagaraFluid->SetVariableFloat(SFUser::Viscosity, Viscosity);
	NiagaraFluid->SetVariableFloat(SFUser::ViscosityShort, Viscosity);
	NiagaraFluid->SetVariableFloat(SFUser::Diffusion, Diffusion);
	NiagaraFluid->SetVariableFloat(SFUser::DiffusionShort, Diffusion);
	if (VelocityRT)
	{
		NiagaraFluid->SetVariableTextureRenderTarget(SFUser::VelocityRT, VelocityRT);
		NiagaraFluid->SetVariableTextureRenderTarget(SFUser::VelocityRTShort, VelocityRT);
	}
	else if (bLogInput)
	{
		UE_LOG(LogScreenFluid, Error,
			TEXT("VelocityRT is NULL — Niagara Inherit UserParam will log 'RenderTarget UserParam is required but invalid' every tick"));
	}
}

void AScreenFluidActor::VerifyNiagaraClickParams(bool bForceLog)
{
	if (!NiagaraFluid || !NiagaraFluid->GetAsset())
	{
		if (bForceLog)
		{
			UE_LOG(LogScreenFluid, Error,
				TEXT("ClickUV check FAIL: Niagara component or asset missing (cannot push User.ClickUV)"));
		}
		return;
	}

	const FVector2D ExpectedUV = PendingInjectUV;
	const float ExpectedPulse = InjectPulse;
	const FVector2D ExpectedForce = PendingInjectForce;

	bool bUvValid = false;
	bool bPulseValid = false;
	bool bForceValid = false;
	FVector2D ReadUV = FVector2D::ZeroVector;
	float ReadPulse = 0.f;
	FVector2D ReadForce = FVector2D::ZeroVector;

	ReadUV = NiagaraFluid->GetVariableVec2(SFUser::ClickUV, bUvValid);
	if (!bUvValid)
	{
		ReadUV = NiagaraFluid->GetVariableVec2(SFUser::ClickUVShort, bUvValid);
	}
	ReadPulse = NiagaraFluid->GetVariableFloat(SFUser::InjectPulse, bPulseValid);
	if (!bPulseValid)
	{
		ReadPulse = NiagaraFluid->GetVariableFloat(SFUser::InjectPulseShort, bPulseValid);
	}
	ReadForce = NiagaraFluid->GetVariableVec2(SFUser::InjectForce, bForceValid);
	if (!bForceValid)
	{
		ReadForce = NiagaraFluid->GetVariableVec2(SFUser::InjectForceShort, bForceValid);
	}

	const float UvErr = FVector2D::Distance(ReadUV, ExpectedUV);
	const bool bUvMatch = bUvValid && UvErr < 1.e-3f;
	const bool bPulseMatch = bPulseValid && FMath::IsNearlyEqual(ReadPulse, ExpectedPulse, 1.e-3f);
	const bool bAllOk = bUvMatch && bPulseMatch;

	if (!bForceLog && bAllOk && !bLogInput)
	{
		return;
	}

	if (bAllOk)
	{
		UE_LOG(LogScreenFluid, Warning,
			TEXT("ClickUV check PASS | UV=(%.4f,%.4f) pulse=%.3f force=(%.2f,%.2f) | read UV=(%.4f,%.4f) forceOk=%d | active=%d"),
			ExpectedUV.X, ExpectedUV.Y, ExpectedPulse, ExpectedForce.X, ExpectedForce.Y,
			ReadUV.X, ReadUV.Y, bForceValid ? 1 : 0,
			NiagaraFluid->IsActive() ? 1 : 0);
	}
	else
	{
		UE_LOG(LogScreenFluid, Error,
			TEXT("ClickUV check FAIL | wrote UV=(%.4f,%.4f) pulse=%.3f | read UV valid=%d (%.4f,%.4f) err=%.5f | pulse valid=%d (%.3f) | active=%d"),
			ExpectedUV.X, ExpectedUV.Y, ExpectedPulse,
			bUvValid ? 1 : 0, ReadUV.X, ReadUV.Y, UvErr,
			bPulseValid ? 1 : 0, ReadPulse,
			NiagaraFluid->IsActive() ? 1 : 0);
	}
}

void AScreenFluidActor::TickInput()
{
	APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
	if (!PC)
	{
		return;
	}

	FVector2D UV;
	const bool bHasUV = GetMouseScreenUV(UV);
	if (bHasUV)
	{
		LastMouseUV = UV;
	}

	// Stam HTML interact: only while dragging; u0 = dx_cells * vMult, dens0 = amount.
	// Default: clear sources this frame (clearInputs equivalent when not moving).
	PendingInjectForce = FVector2D::ZeroVector;
	PendingDensitySrc = 0.f;
	InjectPulse = 0.f;

	const bool bLeftDown = bRespondToLeftMouse && PC->IsInputKeyDown(EKeys::LeftMouseButton);
	const bool bSpaceDown = bRespondToSpaceBar && PC->IsInputKeyDown(EKeys::SpaceBar);
	const bool bHeld = bLeftDown || bSpaceDown;

	if (bHeld && bHasUV)
	{
		PendingInjectUV = UV;

		if (bHasPrevHeldMouseUV)
		{
			const FVector2D DeltaUV = UV - PrevHeldMouseUV;
			const float MoveLenUV = DeltaUV.Size();
			if (MoveLenUV >= MinMoveUV)
			{
				// dx,dy in grid cells (HTML getMousePos maps to 1..N space)
				const float N = static_cast<float>(FMath::Max(GridN, 16));
				const FVector2D DeltaCells = DeltaUV * N;
				PendingInjectForce = DeltaCells * VelocityMult;
				PendingDensitySrc = DensityAmount;
				InjectPulse = 1.f;
			}
		}

		PrevHeldMouseUV = UV;
		bHasPrevHeldMouseUV = true;
	}
	else
	{
		bHasPrevHeldMouseUV = false;
	}

	bMouseWasDown = bLeftDown;
	bSpaceWasDown = bSpaceDown;
}

bool AScreenFluidActor::GetMouseScreenUV(FVector2D& OutUV) const
{
	APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
	if (!PC)
	{
		return false;
	}

	float MouseX = 0.f, MouseY = 0.f;
	bool bGot = PC->GetMousePosition(MouseX, MouseY);
	if (!bGot)
	{
		if (ULocalPlayer* LP = PC->GetLocalPlayer())
		{
			if (LP->ViewportClient)
			{
				FVector2D MP;
				if (LP->ViewportClient->GetMousePosition(MP))
				{
					MouseX = MP.X;
					MouseY = MP.Y;
					bGot = true;
				}
			}
		}
	}
	if (!bGot)
	{
		return false;
	}

	int32 SizeX = 0, SizeY = 0;
	PC->GetViewportSize(SizeX, SizeY);
	if (SizeX <= 0 || SizeY <= 0)
	{
		return false;
	}

	OutUV.X = FMath::Clamp(MouseX / float(SizeX), 0.f, 1.f);
	OutUV.Y = FMath::Clamp(MouseY / float(SizeY), 0.f, 1.f);
	return true;
}
