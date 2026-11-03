#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ClimbComponent.generated.h"

class ACharacter;
class UCharacterMovementComponent;

// Breath-of-the-Wild-style free climbing for the player: any steep surface can be climbed,
// draining stamina; jump while climbing is a climb-jump; reaching a ledge mantles up; running
// out of stamina lets go.
UCLASS(ClassGroup = (Soulslike), meta = (BlueprintSpawnableComponent))
class UNREALSOULSLIKE_API UClimbComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UClimbComponent();

	UFUNCTION(BlueprintPure, Category = "Climb")
	bool IsClimbing() const { return bClimbing; }

	// Jump input: a climb-jump while climbing, otherwise the character's normal jump.
	void HandleJumpPressed();

	// Move input while climbing, in wall space: X = sideways, Y = up/down.
	void AddClimbInput(const FVector2D& Input);

	// Let go of the wall (dodge input while climbing).
	void Release();

	// Animation the owner should play while climbing (key into CharacterAssets.json "anims");
	// NAME_None when the component isn't overriding the owner's animation.
	FName GetAnimOverride(bool& bOutLoop, float& OutDuration) const;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Climb")
	float ClimbSpeed = 180.f;

	// Surfaces whose normal Z is below this are climbable (walkable floors are above it).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Climb")
	float MaxClimbableNormalZ = 0.7f;

	// Holding input into a wall this long while walking starts a climb (falling grabs at once).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Climb")
	float EnterDelay = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Climb|Stamina")
	float StaminaDrainMoving = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Climb|Stamina")
	float StaminaDrainStill = 4.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Climb|Stamina")
	float ClimbJumpStaminaCost = 20.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Climb")
	float ClimbJumpDistance = 250.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Climb")
	float ClimbJumpDuration = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Climb")
	float MantleDuration = 0.4f;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(Replicated)
	bool bClimbing = false;

	UFUNCTION(Server, Reliable)
	void ServerSetClimbing(bool bNewClimbing, FVector_NetQuantizeNormal Normal);

	UFUNCTION(Server, Unreliable)
	void ServerSetFacing(float Yaw);

	UFUNCTION(Server, Reliable)
	void ServerClimbJump();

	// The server replays the owner's moves, so it must cap them at the same fly speed.
	UFUNCTION(Server, Reliable)
	void ServerSetFlySpeed(float Speed);

	UFUNCTION(Client, Reliable)
	void ClientForceRelease();

private:
	ACharacter* GetCharacter() const;
	UCharacterMovementComponent* GetMovement() const;
	bool IsClimbableNormal(const FVector& Normal) const;
	bool TraceWall(const FVector& Start, const FVector& Direction, float Distance, FHitResult& OutHit) const;
	void TryEnter(float DeltaTime);
	void TickClimbing(float DeltaTime);
	void TickMantle(float DeltaTime);
	void StartClimbing(const FHitResult& Hit);
	void StartMantle(const FVector& Target);
	void ExitClimbing(bool bToWalking);
	void FaceWall();
	void SetFlySpeed(float Speed);
	void DrainStamina(float DeltaTime);

	FVector WallNormal = FVector::ForwardVector;
	FVector2D PendingInput = FVector2D::ZeroVector;
	FVector ClimbJumpDirection = FVector::ZeroVector;
	FVector MantleTarget = FVector::ZeroVector;
	float EnterTimer = 0.f;
	float ClimbJumpTimer = 0.f;
	float MantleTimer = 0.f;
	float ReleaseCooldown = 0.f;
	float LastSentYaw = 0.f;
	float DefaultMaxFlySpeed = 600.f;
	float DefaultBrakingFlying = 0.f;
	bool bMantling = false;
	bool bMantleRising = true;
	bool bDefaultOrientToMovement = true;
};
