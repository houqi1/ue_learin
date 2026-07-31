#include "PhoenixPawn.h"

#include "Camera/CameraComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/FloatingPawnMovement.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "Animation/AnimSequence.h"
#include "Engine/LocalPlayer.h"
#include "Engine/SkeletalMesh.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogPhoenixPawn, Log, All);

APhoenixPawn::APhoenixPawn()
{
	PrimaryActorTick.bCanEverTick = true;

	// Neutral root: move this; mesh can yaw independently; camera boom stays world-aligned.
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	Mesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(SceneRoot);
	Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Mesh->SetCollisionObjectType(ECC_Pawn);
	Mesh->SetCollisionResponseToAllChannels(ECR_Block);
	Mesh->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	Mesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	Mesh->SetSimulatePhysics(false);
	// VertData mesh forward is opposite UE +X; offset so nose matches movement / camera-forward.
	Mesh->SetRelativeRotation(FRotator(0.f, 180.f, 0.f));

	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(SceneRoot);
	CameraBoom->TargetArmLength = 1400.f;
	// Fixed oblique top-down (world-aligned via absolute rotation each tick if needed).
	CameraBoom->SetRelativeRotation(FRotator(-65.f, 0.f, 0.f));
	CameraBoom->bDoCollisionTest = false;
	CameraBoom->bUsePawnControlRotation = false;
	CameraBoom->bInheritPitch = false;
	CameraBoom->bInheritYaw = false;
	CameraBoom->bInheritRoll = false;
	CameraBoom->bEnableCameraLag = false;
	// Keep boom orientation in world space so actor yaw (if any) does not swing the camera.
	CameraBoom->SetUsingAbsoluteRotation(true);

	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;
	FollowCamera->FieldOfView = 80.f;

	FloatingMovement = CreateDefaultSubobject<UFloatingPawnMovement>(TEXT("FloatingMovement"));
	FloatingMovement->UpdatedComponent = SceneRoot;
	FloatingMovement->MaxSpeed = 900.f;
	FloatingMovement->Acceleration = 4000.f;
	FloatingMovement->Deceleration = 4000.f;
	FloatingMovement->TurningBoost = 8.f;

	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	AutoPossessPlayer = EAutoReceiveInput::Player0;

	// Mesh only — do NOT ConstructorHelpers-load AnimSequences here.
	// CDO is created during editor/module startup; a broken AnimSequence
	// (missing data model) will assert in UAnimSequenceBase::PostLoad and
	// hard-crash the editor before the project finishes opening.
	static ConstructorHelpers::FObjectFinder<USkeletalMesh> MeshAsset(
		TEXT("/Game/Phonix/SK_Phoenix_VertData/SK_Phoenix_VertData.SK_Phoenix_VertData"));
	if (MeshAsset.Succeeded())
	{
		Mesh->SetSkeletalMesh(MeshAsset.Object);
	}

	// IdleFlyAnimation stays null by default. Assign a healthy AnimSequence
	// on the instance/BP later (several project AS_* assets are corrupt on UE5.8).
}

void APhoenixPawn::ApplyMeshVisualDefaults()
{
	if (!Mesh)
	{
		return;
	}

	Mesh->SetRenderCustomDepth(bEnableCustomDepth);
	Mesh->SetCustomDepthStencilValue(CustomDepthStencilValue);

	if (IdleFlyAnimation)
	{
		Mesh->PlayAnimation(IdleFlyAnimation, true);
	}
}

void APhoenixPawn::BeginPlay()
{
	Super::BeginPlay();
	EnsureDefaultInput();
	ApplyMeshVisualDefaults();

	if (FloatingMovement && SceneRoot)
	{
		FloatingMovement->SetUpdatedComponent(SceneRoot);
	}

	// Absolute rotation: set world pitch once for top-down boom.
	if (CameraBoom)
	{
		CameraBoom->SetWorldRotation(FRotator(-65.f, 0.f, 0.f));
	}

	UE_LOG(LogPhoenixPawn, Log,
		TEXT("PhoenixPawn ready | Mesh=%s ArmLen=%.0f Stencil=%d AutoPossess=%d"),
		Mesh && Mesh->GetSkeletalMeshAsset() ? *Mesh->GetSkeletalMeshAsset()->GetName() : TEXT("None"),
		CameraBoom ? CameraBoom->TargetArmLength : 0.f,
		CustomDepthStencilValue,
		static_cast<int32>(AutoPossessPlayer));
}

void APhoenixPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// Keep camera boom world orientation fixed (oblique top-down).
	if (CameraBoom)
	{
		CameraBoom->SetWorldRotation(FRotator(-65.f, 0.f, 0.f));
	}

	if (!bOrientMeshToMovement || !Mesh || LastMoveDirection.IsNearlyZero())
	{
		return;
	}

	const FRotator Current = Mesh->GetRelativeRotation();
	const float TargetYaw = LastMoveDirection.Rotation().Yaw + MeshYawOffset;
	const FRotator Target(Current.Pitch, TargetYaw, Current.Roll);
	const FRotator NewRot = FMath::RInterpTo(Current, Target, DeltaSeconds, MeshTurnSpeed);
	Mesh->SetRelativeRotation(NewRot);
}

FVector APhoenixPawn::GetFacingWorldDirection() const
{
	// Fluid "face" axis = mesh local +Y (UE Right). Behind inject uses local -Y.
	if (Mesh)
	{
		const FVector MeshY = Mesh->GetRightVector();
		if (!MeshY.IsNearlyZero())
		{
			return MeshY.GetSafeNormal();
		}
	}
	if (!LastMoveDirection.IsNearlyZero())
	{
		return LastMoveDirection.GetSafeNormal();
	}
	return GetActorRightVector().GetSafeNormal();
}

FVector APhoenixPawn::GetBehindWorldDirection() const
{
	// Mesh local -Y (world): opposite of GetRightVector().
	return -GetFacingWorldDirection();
}

void APhoenixPawn::EnsureDefaultInput()
{
	if (!MoveAction)
	{
		MoveAction = NewObject<UInputAction>(this, TEXT("IA_PhoenixMove"), RF_Transient);
		MoveAction->ValueType = EInputActionValueType::Axis2D;
	}

	if (!MappingContext)
	{
		MappingContext = NewObject<UInputMappingContext>(this, TEXT("IMC_Phoenix"), RF_Transient);

		MappingContext->MapKey(MoveAction, EKeys::D);

		{
			FEnhancedActionKeyMapping& Map = MappingContext->MapKey(MoveAction, EKeys::A);
			UInputModifierNegate* Negate = NewObject<UInputModifierNegate>(MappingContext);
			Map.Modifiers.Add(Negate);
		}

		{
			FEnhancedActionKeyMapping& Map = MappingContext->MapKey(MoveAction, EKeys::W);
			UInputModifierSwizzleAxis* Swizzle = NewObject<UInputModifierSwizzleAxis>(MappingContext);
			Swizzle->Order = EInputAxisSwizzle::YXZ;
			Map.Modifiers.Add(Swizzle);
		}

		{
			FEnhancedActionKeyMapping& Map = MappingContext->MapKey(MoveAction, EKeys::S);
			UInputModifierSwizzleAxis* Swizzle = NewObject<UInputModifierSwizzleAxis>(MappingContext);
			Swizzle->Order = EInputAxisSwizzle::YXZ;
			Map.Modifiers.Add(Swizzle);
			UInputModifierNegate* Negate = NewObject<UInputModifierNegate>(MappingContext);
			Map.Modifiers.Add(Negate);
		}

		MappingContext->MapKey(MoveAction, EKeys::Right);
		{
			FEnhancedActionKeyMapping& Map = MappingContext->MapKey(MoveAction, EKeys::Left);
			UInputModifierNegate* Negate = NewObject<UInputModifierNegate>(MappingContext);
			Map.Modifiers.Add(Negate);
		}
		{
			FEnhancedActionKeyMapping& Map = MappingContext->MapKey(MoveAction, EKeys::Up);
			UInputModifierSwizzleAxis* Swizzle = NewObject<UInputModifierSwizzleAxis>(MappingContext);
			Swizzle->Order = EInputAxisSwizzle::YXZ;
			Map.Modifiers.Add(Swizzle);
		}
		{
			FEnhancedActionKeyMapping& Map = MappingContext->MapKey(MoveAction, EKeys::Down);
			UInputModifierSwizzleAxis* Swizzle = NewObject<UInputModifierSwizzleAxis>(MappingContext);
			Swizzle->Order = EInputAxisSwizzle::YXZ;
			Map.Modifiers.Add(Swizzle);
			UInputModifierNegate* Negate = NewObject<UInputModifierNegate>(MappingContext);
			Map.Modifiers.Add(Negate);
		}
	}
}

void APhoenixPawn::PawnClientRestart()
{
	Super::PawnClientRestart();

	EnsureDefaultInput();

	if (const APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
				ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
		{
			if (MappingContext)
			{
				Subsystem->AddMappingContext(MappingContext, MappingPriority);
			}
		}
	}
}

void APhoenixPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	EnsureDefaultInput();

	if (UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		if (MoveAction)
		{
			EIC->BindAction(MoveAction, ETriggerEvent::Triggered, this, &APhoenixPawn::Move);
			EIC->BindAction(MoveAction, ETriggerEvent::Completed, this, &APhoenixPawn::Move);
		}
	}
	else
	{
		UE_LOG(LogPhoenixPawn, Warning, TEXT("Input component is not EnhancedInputComponent — WASD will not bind."));
	}
}

void APhoenixPawn::Move(const FInputActionValue& Value)
{
	ApplyMovementInput2D(Value.Get<FVector2D>());
}

void APhoenixPawn::ApplyMovementInput2D(FVector2D Axis2D)
{
	if (Axis2D.IsNearlyZero())
	{
		return;
	}

	// Camera yaw only (oblique top-down): W = into screen on ground plane.
	const FRotator CamRot = FollowCamera ? FollowCamera->GetComponentRotation() : FRotator::ZeroRotator;
	const FRotationMatrix CamMat(FRotator(0.f, CamRot.Yaw, 0.f));
	const FVector Forward = CamMat.GetUnitAxis(EAxis::X);
	const FVector Right = CamMat.GetUnitAxis(EAxis::Y);

	const FVector WorldDir = (Forward * Axis2D.Y + Right * Axis2D.X).GetSafeNormal();
	if (WorldDir.IsNearlyZero())
	{
		return;
	}

	LastMoveDirection = WorldDir;
	AddMovementInput(WorldDir, 1.f);
}
