// UNR-T77: Titanfall-style wall running for the player knight. See SoulsWallRun.h.
#include "SoulsWallRun.h"
#include "SoulsKnight.h"
#include "UnrealSoulslike.h"
#include "Components/ShadowMergeComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequenceBase.h"
#include "Camera/CameraComponent.h"
#include "CollisionQueryParams.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/Skeleton.h"
#include "Engine/TargetPoint.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputCoreTypes.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "TimerManager.h"

namespace
{
	constexpr float WallRunInPush = 80.f;          // cm/s pressed into the wall, so the capsule stays on it
	constexpr float WallRunDropPush = 180.f;       // cm/s off the wall when dropping off
	constexpr float WallRunReattachDelay = 0.18f;  // no new wall run this soon after one ends
	constexpr float WallRunStickGrace = 0.15f;     // the stick may be let go this long before dropping off
	constexpr float WallRunStepLength = 175.f;     // cm between footsteps on the wall
	constexpr float WallRunSprintSpeed = 760.f;    // the knight's sprint (SoulsKnight.cpp KSprintSpeed)
	const TCHAR* const WallRunLoopPaths[] = {
		TEXT("/Game/Souls/Bestiary/Ranger/Anims/Ranger_MF_Unarmed_Jog_Fwd.Ranger_MF_Unarmed_Jog_Fwd"),
		TEXT("/Game/Characters/Mannequins/Anims/Unarmed/Jog/MF_Unarmed_Jog_Fwd.MF_Unarmed_Jog_Fwd"),
	};
}

USoulsWallRunComponent::USoulsWallRunComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void USoulsWallRunComponent::BeginPlay()
{
	Super::BeginPlay();
	Knight = Cast<ASoulsKnight>(GetOwner());
	if (!Knight)
	{
		return;
	}
	// Tick first: the velocity set here is the one the movement integrates this frame.
	if (UCharacterMovementComponent* Movement = Knight->GetCharacterMovement())
	{
		Movement->AddTickPrerequisiteComponent(this);
	}
	if (Knight->Team == 0 && FParse::Param(FCommandLine::Get(), TEXT("WallRunDemo")))
	{
		RunWallRunDemo();
	}
}

// ---------------------------------------------------------------------------------------------
// Finding a wall
// ---------------------------------------------------------------------------------------------

bool USoulsWallRunComponent::CanStart() const
{
	const UCharacterMovementComponent* Movement = Knight->GetCharacterMovement();
	if (!Knight->IsPlayerControlled() || Knight->IsDead() || Knight->IsSwimming() || Knight->IsBusy()
		|| Knight->GetGrappleMode() != EGrappleMode::None || (Knight->ShadowMelt && Knight->ShadowMelt->IsMerged())
		|| Knight->GetAttachParentActor() || Knight->IsHidden() || Movement->MovementMode != MOVE_Falling)
	{
		return false;
	}
	if (GetWorld()->GetTimeSeconds() - LastEndTime < WallRunReattachDelay)
	{
		return false;
	}
	// The stick must be held, and a hard fall can't be caught on a wall.
	if (Movement->GetCurrentAcceleration().IsNearlyZero() || Movement->Velocity.Z < -700.f
		|| Movement->Velocity.Size2D() < MinEntrySpeed * 0.8f)
	{
		return false;
	}
	// Off the ground: not just a step up a kerb.
	const float Half = Knight->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const FVector Feet = Knight->GetActorLocation() - FVector(0.f, 0.f, Half);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(WallRunFloor), false, Knight);
	FHitResult Floor;
	return !GetWorld()->LineTraceSingleByChannel(Floor, Feet, Feet - FVector(0.f, 0.f, MinFloorGap), ECC_Visibility, Params);
}

bool USoulsWallRunComponent::IsRunnableHit(const FHitResult& Hit) const
{
	if (!Hit.bBlockingHit || Hit.bStartPenetrating)
	{
		return false;
	}
	// Near vertical: not the ground, not a ceiling, not a steep bank.
	if (FMath::Abs(Hit.ImpactNormal.Z) > 0.3f)
	{
		return false;
	}
	// Not an enemy (or any other body), nor anything loose.
	if (const AActor* HitActor = Hit.GetActor(); HitActor && HitActor->IsA<APawn>())
	{
		return false;
	}
	if (const UPrimitiveComponent* HitComponent = Hit.GetComponent(); HitComponent && HitComponent->IsSimulatingPhysics())
	{
		return false;
	}
	return true;
}

bool USoulsWallRunComponent::FindWall(const FVector& Velocity, FHitResult& OutHit, FVector& OutAlong) const
{
	const FVector Flat(Velocity.X, Velocity.Y, 0.f);
	FVector Fwd = Flat.GetSafeNormal();
	if (Fwd.IsNearlyZero())
	{
		Fwd = Knight->GetActorForwardVector().GetSafeNormal2D();
	}
	const FVector Right = FVector::CrossProduct(FVector::UpVector, Fwd);
	const UCapsuleComponent* Capsule = Knight->GetCapsuleComponent();
	const float Radius = Capsule->GetScaledCapsuleRadius();
	const float Half = Capsule->GetScaledCapsuleHalfHeight();
	const FVector Origin = Knight->GetActorLocation();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(WallRunProbe), false, Knight);

	// Either side of the way it's moving, and the two forward diagonals (for jumping in at an angle).
	const float SideReach = Radius + WallReach;
	const float DiagonalReach = Radius + WallReach * 1.4f;
	const FVector Directions[] = { Right, -Right, (Right + Fwd).GetSafeNormal(), (Fwd - Right).GetSafeNormal() };
	const float Reaches[] = { SideReach, SideReach, DiagonalReach, DiagonalReach };

	bool bFound = false;
	float BestDistance = TNumericLimits<float>::Max();
	for (int32 Index = 0; Index < 4; ++Index)
	{
		FHitResult Hit;
		if (!GetWorld()->LineTraceSingleByChannel(Hit, Origin, Origin + Directions[Index] * Reaches[Index], ECC_Visibility, Params)
			|| !IsRunnableHit(Hit))
		{
			continue;
		}
		const FVector Normal = FVector(Hit.ImpactNormal.X, Hit.ImpactNormal.Y, 0.f).GetSafeNormal();
		// The wall must reach down past the waist too (not a lip or a rail at chest height).
		FHitResult Low;
		const FVector LowStart = Origin - FVector(0.f, 0.f, Half * 0.5f);
		if (!GetWorld()->LineTraceSingleByChannel(Low, LowStart, LowStart - Normal * (Reaches[Index] + 40.f), ECC_Visibility, Params)
			|| !IsRunnableHit(Low))
		{
			continue;
		}
		// The wall just left is spent until the ground (or another wall) resets it.
		if (LastWallActor.IsValid() && Hit.GetActor() == LastWallActor.Get() && FVector::DotProduct(Normal, LastWallNormal) > 0.8)
		{
			continue;
		}
		// Enough speed along the wall: a head-on jump at it doesn't stick.
		const FVector Along = Flat - Normal * FVector::DotProduct(Flat, Normal);
		if (Along.Size() < MinEntrySpeed)
		{
			continue;
		}
		if (Hit.Distance < BestDistance)
		{
			BestDistance = Hit.Distance;
			OutHit = Hit;
			OutAlong = Along;
			bFound = true;
		}
	}
	return bFound;
}

// ---------------------------------------------------------------------------------------------
// Running
// ---------------------------------------------------------------------------------------------

void USoulsWallRunComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!Knight || !Knight->GetCharacterMovement())
	{
		return;
	}
	UCharacterMovementComponent* Movement = Knight->GetCharacterMovement();
	if (bWallRunning)
	{
		TickWallRun(DeltaTime);
	}
	else if (Movement->IsMovingOnGround() || Knight->IsSwimming())
	{
		// Landing resets: every wall is fresh again.
		LastWallActor = nullptr;
		LastWallNormal = FVector::ZeroVector;
		WallRunsThisAir = 0;
	}
	else if (CanStart())
	{
		FHitResult Hit;
		FVector Along;
		if (FindWall(Movement->Velocity, Hit, Along))
		{
			StartWallRun(Hit, Along);
		}
	}
	ApplyLean(DeltaTime, bWallRunning);
}

void USoulsWallRunComponent::StartWallRun(const FHitResult& Hit, const FVector& Along)
{
	UCharacterMovementComponent* Movement = Knight->GetCharacterMovement();
	// A glide that meets a wall becomes a wall run.
	if (Knight->bGliding)
	{
		Knight->EndGlide();
	}
	if (!bLeanApplied)
	{
		BaseMeshLocation = Knight->GetMesh()->GetRelativeLocation();
		BaseMeshRotation = Knight->GetMesh()->GetRelativeRotation().Quaternion();
	}

	WallNormal = FVector(Hit.ImpactNormal.X, Hit.ImpactNormal.Y, 0.f).GetSafeNormal();
	RunDirection = Along.GetSafeNormal();
	const float AlongSpeed = static_cast<float>(Along.Size());
	RunSpeed = FMath::Clamp(AlongSpeed * 1.05f, MinEntrySpeed, MaxRunSpeed);
	// Faster in, longer on the wall: sprinting gets the full time.
	const float SpeedAlpha = FMath::Clamp((RunSpeed - MinEntrySpeed) / FMath::Max(WallRunSprintSpeed - MinEntrySpeed, 1.f), 0.f, 1.f);
	RunDuration = FMath::Lerp(MinDuration, MaxDuration, SpeedAlpha);
	const FVector RunRight = FVector::CrossProduct(FVector::UpVector, RunDirection);
	WallSide = FVector::DotProduct(-WallNormal, RunRight) >= 0.0 ? 1.f : -1.f;

	SavedGravityScale = Movement->GravityScale;
	bSavedOrientToMovement = Movement->bOrientRotationToMovement;
	Movement->GravityScale = 0.f;   // gravity is applied here, reduced
	Movement->bOrientRotationToMovement = false;
	const float EntryRise = FMath::Clamp(static_cast<float>(Movement->Velocity.Z), -150.f, MaxEntryRise);
	Movement->Velocity = RunDirection * RunSpeed - WallNormal * WallRunInPush + FVector(0.f, 0.f, EntryRise);
	Knight->SetActorRotation(FRotator(0.f, RunDirection.Rotation().Yaw, 0.f));

	bWallRunning = true;
	RunElapsed = 0.f;
	StickIdle = 0.f;
	StepDistance = 0.f;
	WallActor = Hit.GetActor();
	LastWallActor = Hit.GetActor();
	LastWallNormal = WallNormal;
	LeanAwayFrom = WallNormal;
	++WallRunsThisAir;
	PlayRunLoop();
	PlayStep();
	UE_LOG(LogUnrealSoulslike, Log, TEXT("WallRun: on %s (%s side) at %.0f cm/s for %.2f s, run %d this jump"),
		Hit.GetActor() ? *Hit.GetActor()->GetName() : TEXT("?"), WallSide > 0.f ? TEXT("right") : TEXT("left"), RunSpeed, RunDuration, WallRunsThisAir);
}

void USoulsWallRunComponent::TickWallRun(float DeltaTime)
{
	UCharacterMovementComponent* Movement = Knight->GetCharacterMovement();
	if (Knight->IsDead() || Knight->IsSwimming() || Knight->IsBusy() || Knight->GetGrappleMode() != EGrappleMode::None
		|| (Knight->ShadowMelt && Knight->ShadowMelt->IsMerged()) || Knight->GetAttachParentActor())
	{
		StopWallRun(false);
		return;
	}
	if (Movement->MovementMode != MOVE_Falling)
	{
		// Landed (or anything else took the movement over).
		StopWallRun(false);
		if (Movement->IsMovingOnGround())
		{
			LastWallActor = nullptr;
			WallRunsThisAir = 0;
		}
		return;
	}
	RunElapsed += DeltaTime;
	if (RunElapsed >= RunDuration)
	{
		UE_LOG(LogUnrealSoulslike, Log, TEXT("WallRun: out of time after %.2f s"), RunElapsed);
		StopWallRun(true);
		return;
	}
	// Let go of the stick, or push it away from the wall: drop off.
	const FVector Stick = Movement->GetCurrentAcceleration().GetSafeNormal2D();
	if (Stick.IsNearlyZero())
	{
		StickIdle += DeltaTime;
		if (StickIdle > WallRunStickGrace)
		{
			UE_LOG(LogUnrealSoulslike, Log, TEXT("WallRun: stick released, dropping off after %.2f s"), RunElapsed);
			StopWallRun(true);
			return;
		}
	}
	else
	{
		StickIdle = 0.f;
		if (FVector::DotProduct(Stick, WallNormal) > 0.7)
		{
			UE_LOG(LogUnrealSoulslike, Log, TEXT("WallRun: steered off the wall after %.2f s"), RunElapsed);
			StopWallRun(true);
			return;
		}
	}
	// Still a wall beside us? Follow its curve; past its end, carry on through the air.
	const UCapsuleComponent* Capsule = Knight->GetCapsuleComponent();
	const FVector Origin = Knight->GetActorLocation();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(WallRunFollow), false, Knight);
	FHitResult Hit;
	if (!GetWorld()->LineTraceSingleByChannel(Hit, Origin, Origin - WallNormal * (Capsule->GetScaledCapsuleRadius() + WallReach + 20.f),
			ECC_Visibility, Params) || !IsRunnableHit(Hit))
	{
		UE_LOG(LogUnrealSoulslike, Log, TEXT("WallRun: ran off the wall after %.2f s (hit %d, normal z %.2f, distance %.0f, start-penetrating %d, at %s, wall normal %s, actor %s)"),
			RunElapsed, Hit.bBlockingHit, Hit.ImpactNormal.Z, Hit.Distance, Hit.bStartPenetrating, *Origin.ToCompactString(),
			*WallNormal.ToCompactString(), *GetNameSafe(Hit.GetActor()));
		StopWallRun(false);
		return;
	}
	WallNormal = FVector(Hit.ImpactNormal.X, Hit.ImpactNormal.Y, 0.f).GetSafeNormal();
	const FVector Followed = (RunDirection - WallNormal * FVector::DotProduct(RunDirection, WallNormal)).GetSafeNormal();
	if (Followed.IsNearlyZero())
	{
		StopWallRun(true);
		return;
	}
	RunDirection = Followed;
	LeanAwayFrom = WallNormal;
	if (Hit.GetActor() != WallActor.Get())
	{
		WallActor = Hit.GetActor();
		LastWallActor = Hit.GetActor();
	}
	LastWallNormal = WallNormal;

	// Along the wall at the run speed, gently pressed into it, under a fraction of gravity.
	const float Rise = FMath::Max(static_cast<float>(Movement->Velocity.Z) - WallGravity * DeltaTime, -MaxSlideSpeed);
	Movement->Velocity = RunDirection * RunSpeed - WallNormal * WallRunInPush + FVector(0.f, 0.f, Rise);
	Knight->SetActorRotation(FMath::RInterpTo(Knight->GetActorRotation(), FRotator(0.f, RunDirection.Rotation().Yaw, 0.f), DeltaTime, 12.f));

	StepDistance += RunSpeed * DeltaTime;
	if (StepDistance > WallRunStepLength)
	{
		StepDistance = 0.f;
		PlayStep();
	}
}

bool USoulsWallRunComponent::TryWallJump()
{
	if (!bWallRunning || !Knight)
	{
		return false;
	}
	const FVector OffWall = WallNormal;
	const FVector Onward = RunDirection;
	const float Carry = RunSpeed * JumpCarry;
	StopWallRun(false);
	// Out from the wall at an angle, on along the run, and up: across to a facing wall for a chain.
	const FVector Launch = OffWall * JumpOffSpeed + Onward * Carry + FVector(0.f, 0.f, JumpUpSpeed);
	Knight->LaunchCharacter(Launch, true, true);
	Knight->SetActorRotation(FRotator(0.f, Launch.Rotation().Yaw, 0.f));
	Knight->PlayRandomSound(Knight->FootstepSounds, 0.5f);
	Knight->PlayRandomSound(Knight->SwingSounds, 0.35f);
	UE_LOG(LogUnrealSoulslike, Log, TEXT("WallRun: wall jump %s"), *Launch.ToCompactString());
	return true;
}

void USoulsWallRunComponent::StopWallRun(bool bPushOff)
{
	if (!bWallRunning)
	{
		return;
	}
	bWallRunning = false;
	LastEndTime = GetWorld()->GetTimeSeconds();
	if (Knight)
	{
		UCharacterMovementComponent* Movement = Knight->GetCharacterMovement();
		Movement->GravityScale = SavedGravityScale;
		Movement->bOrientRotationToMovement = bSavedOrientToMovement;
		if (bPushOff && Movement->IsFalling())
		{
			Movement->Velocity += WallNormal * WallRunDropPush;
		}
	}
	StopRunLoop();
	WallActor = nullptr;
}

// ---------------------------------------------------------------------------------------------
// Look and sound
// ---------------------------------------------------------------------------------------------

void USoulsWallRunComponent::ApplyLean(float DeltaTime, bool bOnWall)
{
	USkeletalMeshComponent* Body = Knight->GetMesh();
	UCameraComponent* Camera = Knight->GetFollowCamera();
	if (!Body)
	{
		return;
	}
	Lean = FMath::FInterpTo(Lean, bOnWall ? BodyTilt : 0.f, DeltaTime, bOnWall ? 10.f : 7.f);
	// Head leans away from the wall, so the view rolls away from it too (wall on the right: roll left).
	Roll = FMath::FInterpTo(Roll, bOnWall ? -CameraRoll * WallSide : 0.f, DeltaTime, 6.f);
	if (!bOnWall && Lean < 0.05f && FMath::Abs(Roll) < 0.05f)
	{
		Lean = 0.f;
		Roll = 0.f;
		if (bLeanApplied)
		{
			Body->SetRelativeLocationAndRotation(BaseMeshLocation, BaseMeshRotation);
			if (Camera)
			{
				Camera->SetRelativeRotation(FRotator::ZeroRotator);
			}
			bLeanApplied = false;
		}
		return;
	}
	// Tip the body's up toward the wall's outward normal, about the capsule's centre: the feet swing
	// in onto the wall and the head out from it.
	const float LeanRadians = FMath::DegreesToRadians(Lean);
	const FVector TippedUp = (FVector::UpVector * FMath::Cos(LeanRadians) + LeanAwayFrom * FMath::Sin(LeanRadians)).GetSafeNormal();
	const FQuat WorldTilt = FQuat::FindBetweenNormals(FVector::UpVector, TippedUp);
	const FQuat ActorRotation = Knight->GetActorQuat();
	const FQuat LocalTilt = ActorRotation.Inverse() * WorldTilt * ActorRotation;
	Body->SetRelativeLocationAndRotation(LocalTilt.RotateVector(BaseMeshLocation), LocalTilt * BaseMeshRotation);
	if (Camera)
	{
		Camera->SetRelativeRotation(FRotator(0.f, 0.f, Roll));
	}
	bLeanApplied = true;
}

UAnimSequenceBase* USoulsWallRunComponent::FindRunLoop() const
{
	if (RunLoop)
	{
		return RunLoop;
	}
	if (bRunLoopSearched)
	{
		return CachedRunLoop;
	}
	USoulsWallRunComponent* Self = const_cast<USoulsWallRunComponent*>(this);
	Self->bRunLoopSearched = true;
	const USkeletalMesh* BodyMesh = Knight->GetMesh() ? Knight->GetMesh()->GetSkeletalMeshAsset() : nullptr;
	const USkeleton* BodySkeleton = BodyMesh ? BodyMesh->GetSkeleton() : nullptr;
	for (const TCHAR* Path : WallRunLoopPaths)
	{
		UAnimSequenceBase* Clip = LoadObject<UAnimSequenceBase>(nullptr, Path, {}, LOAD_NoWarn | LOAD_Quiet);
		if (Clip && Clip->GetSkeleton() == BodySkeleton)
		{
			Self->CachedRunLoop = Clip;
			UE_LOG(LogUnrealSoulslike, Log, TEXT("WallRun: running loop %s"), *Clip->GetName());
			return Clip;
		}
	}
	UE_LOG(LogUnrealSoulslike, Warning, TEXT("WallRun: no jog clip matches the body's skeleton %s; wall runs keep the air pose"),
		BodySkeleton ? *BodySkeleton->GetName() : TEXT("none"));
	return nullptr;
}

void USoulsWallRunComponent::PlayRunLoop()
{
	UAnimInstance* Anim = Knight->GetMesh() ? Knight->GetMesh()->GetAnimInstance() : nullptr;
	UAnimSequenceBase* Clip = FindRunLoop();
	if (!Anim || !Clip)
	{
		return;
	}
	const float Rate = FMath::Clamp(RunSpeed / 560.f, 1.f, 1.7f);
	RunMontage = Anim->PlaySlotAnimationAsDynamicMontage(Clip, FName(TEXT("DefaultSlot")), 0.12f, 0.2f, Rate, 1000);
}

void USoulsWallRunComponent::StopRunLoop()
{
	UAnimInstance* Anim = Knight && Knight->GetMesh() ? Knight->GetMesh()->GetAnimInstance() : nullptr;
	if (Anim && RunMontage && Anim->Montage_IsActive(RunMontage))
	{
		Anim->Montage_Stop(0.2f, RunMontage);
	}
	RunMontage = nullptr;
}

void USoulsWallRunComponent::PlayStep()
{
	Knight->PlayRandomSound(Knight->FootstepSounds, 0.32f);
}

// ---------------------------------------------------------------------------------------------
// -WallRunDemo
// ---------------------------------------------------------------------------------------------

void USoulsWallRunComponent::RunWallRunDemo()
{
	GEngine->Exec(GetWorld(), TEXT("r.MotionBlurQuality 0"));
	struct FWallRunProbe
	{
		int32 Phase = 0;
		FVector Start = FVector::ZeroVector;
		FVector Fwd = FVector::ForwardVector;
		float CorridorAt = 0.f;
		float PhaseTime = 0.f;
		FVector StuckAt = FVector::ZeroVector;
		FVector FirstNormal = FVector::ZeroVector;
		bool bStuck = false;
		bool bTravelled = false;
		bool bChained = false;
		bool bDropped = false;
		bool bLanded = false;
		bool bFinished = false;
		bool bRunShot = false;
	};
	const TSharedRef<FWallRunProbe> Probe = MakeShared<FWallRunProbe>();
	const TWeakObjectPtr<USoulsWallRunComponent> WeakThis(this);
	auto Log = [](const FString& Text) { UE_LOG(LogUnrealSoulslike, Log, TEXT("WallRun check: %s"), *Text); };
	auto Tap = [](ASoulsKnight* K, const FKey& Key, float Hold)
	{
		K->InputTestKey(Key, true);
		FTimerHandle Handle;
		const TWeakObjectPtr<ASoulsKnight> WeakK(K);
		K->GetWorldTimerManager().SetTimer(Handle, [WeakK, Key]() { if (WeakK.IsValid()) { WeakK->InputTestKey(Key, false); } }, Hold, false);
	};
	const FKey JumpKey = EKeys::Gamepad_FaceButton_Bottom;

	// Stand at the course's start marker, lined up a short way off the left wall of the first pair.
	FTimerHandle Setup;
	GetWorld()->GetTimerManager().SetTimer(Setup, [WeakThis, Probe, Log]()
	{
		USoulsWallRunComponent* Self = WeakThis.Get();
		ASoulsKnight* K = Self ? Self->Knight.Get() : nullptr;
		if (!K)
		{
			return;
		}
		ATargetPoint* Marker = nullptr;
		for (TActorIterator<ATargetPoint> It(K->GetWorld()); It; ++It)
		{
			if (It->ActorHasTag(FName(TEXT("WallRunStart"))))
			{
				Marker = *It;
				break;
			}
		}
		if (!Marker)
		{
			Log(TEXT("no WallRunStart marker (rebuild the island) FAIL"));
			Probe->bFinished = true;
			return;
		}
		K->GetCameraBoom()->bEnableCameraLag = false;
		K->GetCameraBoom()->bEnableCameraRotationLag = false;
		const FVector Fwd = Marker->GetActorForwardVector().GetSafeNormal2D();
		const FVector Left = -FVector::CrossProduct(FVector::UpVector, Fwd);
		const FVector From = Marker->GetActorLocation() + FVector(0.f, 0.f, 100.f);
		FCollisionQueryParams Params(SCENE_QUERY_STAT(WallRunDemo), false, K);
		// Walk the course's axis forward until a wall shows up on the left: where the corridor starts.
		float LeftGap = -1.f;
		for (float Ahead = 0.f; Ahead < 3000.f; Ahead += 50.f)
		{
			FHitResult Side;
			const FVector At = From + Fwd * Ahead;
			if (K->GetWorld()->LineTraceSingleByChannel(Side, At, At + Left * 600.f, ECC_Visibility, Params) && Self->IsRunnableHit(Side))
			{
				Probe->CorridorAt = Ahead;
				LeftGap = Side.Distance;
				break;
			}
		}
		if (LeftGap < 0.f)
		{
			Log(TEXT("no wall left of the course axis FAIL"));
			Probe->bFinished = true;
			return;
		}
		const float Radius = K->GetCapsuleComponent()->GetScaledCapsuleRadius();
		FVector Spot = Marker->GetActorLocation() + Left * FMath::Max(LeftGap - (Radius + 45.f), 0.f);
		FHitResult Ground;
		if (K->GetWorld()->LineTraceSingleByChannel(Ground, Spot + FVector(0.f, 0.f, 3000.f), Spot - FVector(0.f, 0.f, 6000.f), ECC_Visibility, Params))
		{
			Spot = Ground.ImpactPoint + FVector(0.f, 0.f, K->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 5.f);
		}
		K->SetActorLocation(Spot, false, nullptr, ETeleportType::TeleportPhysics);
		K->SetActorRotation(FRotator(0.f, Fwd.Rotation().Yaw, 0.f));
		if (K->Controller)
		{
			K->Controller->SetControlRotation(FRotator(-12.f, Fwd.Rotation().Yaw, 0.f));
		}
		Probe->Start = Spot;
		Probe->Fwd = Fwd;
		Probe->Phase = 1;
		Log(FString::Printf(TEXT("at the start, corridor %.0f cm ahead, left wall %.0f cm off the axis, knight %.0f cm from it"),
			Probe->CorridorAt, LeftGap, Radius + 45.f));
		K->Shot(TEXT("WallRun_0_start"));
	}, 3.f, false);

	// Hold the stick forward and tap Shift (L3) to sprint.
	FTimerHandle Go;
	GetWorld()->GetTimerManager().SetTimer(Go, [WeakThis, Probe, Tap]()
	{
		ASoulsKnight* K = WeakThis.IsValid() ? WeakThis->Knight.Get() : nullptr;
		if (K && Probe->Phase == 1)
		{
			K->InputTestKey(EKeys::W, true);
			Tap(K, EKeys::LeftShift, 0.08f);
		}
	}, 3.5f, false);

	FTimerHandle Watch;
	GetWorld()->GetTimerManager().SetTimer(Watch, [WeakThis, Probe, Log, Tap, JumpKey]()
	{
		USoulsWallRunComponent* Self = WeakThis.Get();
		ASoulsKnight* K = Self ? Self->Knight.Get() : nullptr;
		if (!K || Probe->bFinished)
		{
			return;
		}
		const float Now = K->GetWorld()->GetTimeSeconds();
		const FVector Loc = K->GetActorLocation();
		const UCharacterMovementComponent* Movement = K->GetCharacterMovement();
		auto Next = [&Probe, Now](int32 NewPhase) { Probe->Phase = NewPhase; Probe->PhaseTime = Now; };
		switch (Probe->Phase)
		{
		case 1: // sprinting up to the corridor: jump a little way into it, beside the left wall
		{
			const float Ahead = static_cast<float>(FVector::DotProduct(Loc - Probe->Start, Probe->Fwd));
			if (Ahead >= Probe->CorridorAt + 250.f)
			{
				Log(FString::Printf(TEXT("A at %.0f cm/s, %.0f cm into the corridor"), Movement->Velocity.Size2D(), Ahead - Probe->CorridorAt));
				Tap(K, JumpKey, 0.1f);
				K->Shot(TEXT("WallRun_1_jump"));
				Next(2);
			}
			break;
		}
		case 2: // it should catch the wall
			if (Self->IsWallRunning())
			{
				Probe->bStuck = true;
				Probe->StuckAt = Loc;
				Probe->FirstNormal = Self->GetWallNormal();
				Log(FString::Printf(TEXT("sticks to the wall PASS (%s side, %.0f cm/s, %.2f s after A)"),
					Self->GetWallSide() < 0.f ? TEXT("left") : TEXT("right"), Movement->Velocity.Size2D(), Now - Probe->PhaseTime));
				Next(3);
			}
			else if (Now - Probe->PhaseTime > 0.9f)
			{
				Log(FString::Printf(TEXT("sticks to the wall FAIL (falling=%d speed=%.0f)"), Movement->IsFalling(), Movement->Velocity.Size2D()));
				Next(6);
			}
			break;
		case 3: // run along it, then A across to the facing wall
			if (Now - Probe->PhaseTime > 0.3f && !Probe->bRunShot)
			{
				Probe->bRunShot = true;
				K->Shot(TEXT("WallRun_2_running"));
			}
			if (!Self->IsWallRunning())
			{
				Log(FString::Printf(TEXT("travels along the wall FAIL (dropped after %.2f s, %.0f cm)"), Now - Probe->PhaseTime,
					FVector::Dist2D(Loc, Probe->StuckAt)));
				Next(6);
			}
			else if (Now - Probe->PhaseTime >= 0.75f)
			{
				const float Travelled = static_cast<float>(FVector::Dist2D(Loc, Probe->StuckAt));
				const float Sag = static_cast<float>(Loc.Z - Probe->StuckAt.Z);
				Probe->bTravelled = Travelled > 400.f && Sag > -120.f;
				Log(FString::Printf(TEXT("travels along the wall %s (%.0f cm in %.2f s, height change %.0f cm, lean %.0f deg)"),
					Probe->bTravelled ? TEXT("PASS") : TEXT("FAIL"), Travelled, Now - Probe->PhaseTime, Sag, Self->Lean));
				Tap(K, JumpKey, 0.1f);
				Next(4);
			}
			break;
		case 4: // the wall jump: onto the opposite wall
			if (Self->IsWallRunning() && FVector::DotProduct(Self->GetWallNormal(), Probe->FirstNormal) < -0.7)
			{
				Probe->bChained = true;
				Log(FString::Printf(TEXT("wall jump to the facing wall PASS (%.2f s across, %.0f cm up)"), Now - Probe->PhaseTime,
					Loc.Z - Probe->StuckAt.Z));
				K->Shot(TEXT("WallRun_3_second_wall"));
				Next(5);
			}
			else if (Now - Probe->PhaseTime > 1.4f)
			{
				Log(FString::Printf(TEXT("wall jump to the facing wall FAIL (running=%d falling=%d)"), Self->IsWallRunning(), Movement->IsFalling()));
				Next(6);
			}
			break;
		case 5: // run on it a moment, then let go of the stick: drop off
			if (Now - Probe->PhaseTime > 0.5f && Now - Probe->PhaseTime < 0.53f)
			{
				K->InputTestKey(EKeys::W, false);
			}
			if (Now - Probe->PhaseTime > 0.5f && !Self->IsWallRunning())
			{
				Probe->bDropped = Now - Probe->PhaseTime < 1.1f;
				Log(FString::Printf(TEXT("letting go of the stick drops off %s (%.2f s after letting go)"),
					Probe->bDropped ? TEXT("PASS") : TEXT("FAIL"), Now - Probe->PhaseTime - 0.5f));
				Next(6);
			}
			break;
		case 6: // down onto the ground
			K->InputTestKey(EKeys::W, false);
			if (Movement->IsMovingOnGround() && Now - Probe->PhaseTime > 0.6f)
			{
				Probe->bLanded = true;
				const bool bPass = Probe->bStuck && Probe->bTravelled && Probe->bChained && Probe->bDropped && Self->Lean < 1.f;
				Log(FString::Printf(TEXT("landed PASS; done: stuck=%d travelled=%d chained=%d dropped=%d landed=%d lean=%.1f %s"),
					Probe->bStuck, Probe->bTravelled, Probe->bChained, Probe->bDropped, Probe->bLanded, Self->Lean, bPass ? TEXT("PASS") : TEXT("FAIL")));
				K->Shot(TEXT("WallRun_4_landed"));
				Probe->bFinished = true;
			}
			else if (Now - Probe->PhaseTime > 6.f)
			{
				Log(FString::Printf(TEXT("landed FAIL (falling=%d swimming=%d); done: stuck=%d travelled=%d chained=%d dropped=%d FAIL"),
					Movement->IsFalling(), K->IsSwimming(), Probe->bStuck, Probe->bTravelled, Probe->bChained, Probe->bDropped));
				Probe->bFinished = true;
			}
			break;
		default:
			break;
		}
	}, 0.02f, true, 3.6f);

	// A run that never got going still reports.
	FTimerHandle Timeout;
	GetWorld()->GetTimerManager().SetTimer(Timeout, [WeakThis, Probe, Log]()
	{
		if (!Probe->bFinished)
		{
			Probe->bFinished = true;
			if (ASoulsKnight* K = WeakThis.IsValid() ? WeakThis->Knight.Get() : nullptr)
			{
				K->InputTestKey(EKeys::W, false);
			}
			Log(FString::Printf(TEXT("timed out in phase %d; done: stuck=%d travelled=%d chained=%d dropped=%d landed=%d FAIL"),
				Probe->Phase, Probe->bStuck, Probe->bTravelled, Probe->bChained, Probe->bDropped, Probe->bLanded));
		}
	}, 20.f, false);
}
