// Top-down controllable phoenix: Floating move + SpringArm camera + Enhanced Input WASD.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "PhoenixPawn.generated.h"

class USceneComponent;
class USkeletalMeshComponent;
class USpringArmComponent;
class UCameraComponent;
class UFloatingPawnMovement;
class UInputMappingContext;
class UInputAction;
class UAnimSequence;
struct FInputActionValue;

UCLASS(Blueprintable, meta = (DisplayName = "Phoenix Pawn (Top-Down)"))
class APhoenixPawn : public APawn
{
	GENERATED_BODY()

public:
	APhoenixPawn();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void PawnClientRestart() override;

protected:
	void Move(const FInputActionValue& Value);
	void EnsureDefaultInput();
	void ApplyMeshVisualDefaults();
	void ApplyMovementInput2D(FVector2D Axis2D);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Phoenix")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Phoenix")
	TObjectPtr<USkeletalMeshComponent> Mesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Phoenix")
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Phoenix")
	TObjectPtr<UCameraComponent> FollowCamera;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Phoenix")
	TObjectPtr<UFloatingPawnMovement> FloatingMovement;

	/** If unset, built at runtime with WASD -> Axis2D. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Phoenix|Input")
	TObjectPtr<UInputMappingContext> MappingContext;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Phoenix|Input")
	TObjectPtr<UInputAction> MoveAction;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Phoenix|Input")
	int32 MappingPriority = 0;

	/** Optional looped fly pose (e.g. AS_Idle_MainPose_flying). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Phoenix|Animation")
	TObjectPtr<UAnimSequence> IdleFlyAnimation;

	/** Yaw-only orient mesh toward move direction (camera arm stays world-aligned). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Phoenix|Movement")
	bool bOrientMeshToMovement = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Phoenix|Movement", meta = (ClampMin = "0.0"))
	float MeshTurnSpeed = 10.f;

	/**
	 * Added to movement-facing yaw when orienting the mesh.
	 * Phoenix VertData mesh faces -X relative to UE forward; 180 makes head match travel dir.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Phoenix|Movement")
	float MeshYawOffset = 180.f;

	/** Must match ScreenFluidActor.PhoenixStencilID for GBuffer inject. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Phoenix|Fluid", meta = (ClampMin = "0", ClampMax = "255"))
	int32 CustomDepthStencilValue = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Phoenix|Fluid")
	bool bEnableCustomDepth = true;

private:
	FVector LastMoveDirection = FVector::ForwardVector;
};
