#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SoulsWallRun.generated.h"

class ASoulsKnight;
class UAnimMontage;
class UAnimSequenceBase;

// UNR-T77. Titanfall-style wall running for the player knight. Jump toward or alongside a near-vertical
// wall while moving fast enough (sprint helps) and the knight sticks to it and runs along it for
// MinDuration..MaxDuration seconds (longer the faster it came in) under reduced gravity, body tilted
// away from the wall and the camera rolled. A jumps off the wall at an angle (chain between facing
// walls); letting go of the stick, turning away from the wall, running off its end or the timer
// running out drops off. Touching the ground (or water) resets everything.
//
// Ticks before the character movement component, so the velocity it sets each frame is the one the
// movement integrates. The knight routes A through TryWallJump() first (see JumpPressed).
// -WallRunDemo drives it with simulated input on the island's wall-run course (tools/build_island.py).
UCLASS(ClassGroup = (Soulslike), meta = (BlueprintSpawnableComponent))
class UNREALSOULSLIKE_API USoulsWallRunComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USoulsWallRunComponent();

	UFUNCTION(BlueprintPure, Category = "WallRun")
	bool IsWallRunning() const { return bWallRunning; }

	// The wall's outward normal while running (zero otherwise).
	FVector GetWallNormal() const { return bWallRunning ? WallNormal : FVector::ZeroVector; }
	FVector GetRunDirection() const { return bWallRunning ? RunDirection : FVector::ZeroVector; }
	// -1 = wall on the knight's left, +1 = on its right, 0 = not running.
	float GetWallSide() const { return bWallRunning ? WallSide : 0.f; }

	// A pressed: on a wall, kick off it (returns true: the press is used up). Otherwise false.
	bool TryWallJump();

	// Drop off the wall now (hit, grappled, melted, ...). bPushOff nudges the knight clear of it.
	void StopWallRun(bool bPushOff);

	// ---- Tuning ----
	// Slowest along-the-wall speed that still sticks (run is 520, sprint 760).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WallRun")
	float MinEntrySpeed = 430.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WallRun")
	float MaxRunSpeed = 980.f;

	// Run time at MinEntrySpeed and at sprint speed (and above).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WallRun")
	float MinDuration = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WallRun")
	float MaxDuration = 2.0f;

	// The reduced gravity on the wall (cm/s^2) and the fastest it lets you slide down.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WallRun")
	float WallGravity = 300.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WallRun")
	float MaxSlideSpeed = 380.f;

	// The most upward speed carried onto the wall from the jump.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WallRun")
	float MaxEntryRise = 320.f;

	// How far past the capsule's side a wall is looked for.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WallRun")
	float WallReach = 75.f;

	// Feet at least this far above the ground before a wall run can start.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WallRun")
	float MinFloorGap = 25.f;

	// Wall jump: out from the wall, up, and on along the run (scaled by the run speed).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WallRun")
	float JumpOffSpeed = 680.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WallRun")
	float JumpUpSpeed = 620.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WallRun")
	float JumpCarry = 0.85f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WallRun")
	float BodyTilt = 24.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WallRun")
	float CameraRoll = 11.f;

	// Optional override for the running loop; by default the knight's own jog-forward clip is found
	// (the Ranger's retarget, or Epic's mannequin clip), whichever matches the body's skeleton.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WallRun")
	TObjectPtr<UAnimSequenceBase> RunLoop;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual void BeginPlay() override;

private:
	bool CanStart() const;
	// A runnable wall next to the knight given its velocity: outward normal, contact point, actor.
	bool FindWall(const FVector& Velocity, FHitResult& OutHit, FVector& OutAlong) const;
	bool IsRunnableHit(const FHitResult& Hit) const;
	void StartWallRun(const FHitResult& Hit, const FVector& Along);
	void TickWallRun(float DeltaTime);
	void ApplyLean(float DeltaTime, bool bOnWall);
	void PlayRunLoop();
	void StopRunLoop();
	UAnimSequenceBase* FindRunLoop() const;
	void PlayStep();

	// -WallRunDemo: the course, a sprint, onto the left wall, along it, across to the right one, down.
	void RunWallRunDemo();

	UPROPERTY(Transient)
	TObjectPtr<ASoulsKnight> Knight;

	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> RunMontage;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequenceBase> CachedRunLoop;

	bool bWallRunning = false;
	bool bRunLoopSearched = false;
	bool bSavedOrientToMovement = true;
	float SavedGravityScale = 1.f;
	FVector WallNormal = FVector::ZeroVector;
	FVector RunDirection = FVector::ForwardVector;
	float WallSide = 0.f;
	float RunSpeed = 0.f;
	float RunElapsed = 0.f;
	float RunDuration = 0.f;
	float StickIdle = 0.f;
	float StepDistance = 0.f;
	float LastEndTime = -100.f;
	TWeakObjectPtr<AActor> WallActor;
	// The wall last run on (and its normal): not run on again until the ground or another wall resets it.
	TWeakObjectPtr<AActor> LastWallActor;
	FVector LastWallNormal = FVector::ZeroVector;
	// Mesh and camera at rest, and the current lean (degrees, + = away from the wall) and roll.
	FVector BaseMeshLocation = FVector::ZeroVector;
	FQuat BaseMeshRotation = FQuat::Identity;
	bool bLeanApplied = false;
	float Lean = 0.f;
	FVector LeanAwayFrom = FVector::ZeroVector;
	float Roll = 0.f;
	int32 WallRunsThisAir = 0;
};
