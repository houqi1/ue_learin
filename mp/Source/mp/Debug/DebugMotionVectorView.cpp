#include "DebugMotionVectorView.h"

#include "Camera/CameraComponent.h"
#include "Components/PostProcessComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

DEFINE_LOG_CATEGORY_STATIC(LogDebugMV, Log, All);

namespace DebugMV
{
	static const TCHAR* DefaultMatPath =
		TEXT("/Game/FX/Debug/M_PP_DebugMotionVector.M_PP_DebugMotionVector");
	static const FName ExposureParam(TEXT("Exposure"));
}

ADebugMotionVectorView::ADebugMotionVectorView()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(SceneRoot);

	PostProcess = CreateDefaultSubobject<UPostProcessComponent>(TEXT("PostProcess"));
	PostProcess->SetupAttachment(SceneRoot);
	PostProcess->bUnbound = true;
	PostProcess->bEnabled = true;
	PostProcess->Priority = 100.f;
	PostProcess->BlendWeight = 1.f;
}

void ADebugMotionVectorView::BeginPlay()
{
	Super::BeginPlay();
	EnsureMaterial();
	if (bEnabledOnBeginPlay)
	{
		ApplyBlendable();
		PushParams();
	}
	else if (PostProcess)
	{
		PostProcess->bEnabled = false;
	}

	UE_LOG(LogDebugMV, Warning,
		TEXT("DebugMotionVectorView ready | Mat=%s MID=%s Enabled=%d Exposure=%.1f"),
		*GetNameSafe(MotionVectorMaterial),
		MID ? TEXT("ok") : TEXT("NULL"),
		bEnabledOnBeginPlay ? 1 : 0,
		Exposure);
}

void ADebugMotionVectorView::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	RemoveBlendable();
	Super::EndPlay(EndPlayReason);
}

void ADebugMotionVectorView::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (PostProcess && PostProcess->bEnabled)
	{
		PushParams();
		// Keep blendable registered (some systems clear WeightedBlendables).
		if (MID && PostProcess->Settings.WeightedBlendables.Array.Num() == 0)
		{
			ApplyBlendable();
		}
	}
}

void ADebugMotionVectorView::SetOverlayEnabled(bool bEnabled)
{
	if (!PostProcess)
	{
		return;
	}
	if (bEnabled)
	{
		EnsureMaterial();
		ApplyBlendable();
		PushParams();
		PostProcess->bEnabled = true;
	}
	else
	{
		RemoveBlendable();
		PostProcess->bEnabled = false;
	}
}

void ADebugMotionVectorView::SetExposure(float InExposure)
{
	Exposure = FMath::Max(0.f, InExposure);
	PushParams();
}

void ADebugMotionVectorView::EnsureMaterial()
{
	if (!MotionVectorMaterial)
	{
		MotionVectorMaterial = LoadObject<UMaterialInterface>(nullptr, DebugMV::DefaultMatPath);
	}
	if (!MotionVectorMaterial)
	{
		UE_LOG(LogDebugMV, Error,
			TEXT("Missing material %s — run Tools/create_pp_debug_motion_vector.py in Editor"),
			DebugMV::DefaultMatPath);
		return;
	}
	if (!MID)
	{
		MID = UMaterialInstanceDynamic::Create(MotionVectorMaterial, this);
	}
}

void ADebugMotionVectorView::ApplyBlendable()
{
	if (!PostProcess || !MID)
	{
		return;
	}
	PostProcess->Settings.WeightedBlendables.Array.Reset();
	PostProcess->Settings.WeightedBlendables.Array.Add(FWeightedBlendable(1.f, MID));
	PostProcess->Priority = PostProcessPriority;
	PostProcess->BlendWeight = 1.f;
	PostProcess->bUnbound = true;
	PostProcess->bEnabled = true;

	// Also stamp onto player camera if present (same pattern as ScreenFluid).
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
	{
		AActor* ViewTarget = PC->GetViewTarget() ? PC->GetViewTarget() : static_cast<AActor*>(PC->GetPawn());
		if (ViewTarget)
		{
			TArray<UCameraComponent*> Cams;
			ViewTarget->GetComponents<UCameraComponent>(Cams);
			for (UCameraComponent* Cam : Cams)
			{
				if (!Cam)
				{
					continue;
				}
				Cam->PostProcessSettings.WeightedBlendables.Array.RemoveAll([this](const FWeightedBlendable& B)
				{
					return B.Object == MID;
				});
				Cam->PostProcessSettings.WeightedBlendables.Array.Add(FWeightedBlendable(1.f, MID));
				Cam->PostProcessBlendWeight = 1.f;
			}
		}
	}
}

void ADebugMotionVectorView::RemoveBlendable()
{
	if (PostProcess)
	{
		PostProcess->Settings.WeightedBlendables.Array.Reset();
		PostProcess->bEnabled = false;
	}
}

void ADebugMotionVectorView::PushParams()
{
	if (MID)
	{
		MID->SetScalarParameterValue(DebugMV::ExposureParam, Exposure);
	}
}
