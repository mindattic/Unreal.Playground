// ASoulsKnight's traversal and set-piece abilities: the grapple (to ledges, reeling foes in, reeling
// yourself to big ones), bracing against pouncing foes and impaling them, the enemies' pounce,
// swimming, and the fear/shock that ambushes inflict.
#include "SoulsKnight.h"
#include "SoulsEnemy.h"
#include "GrapplePoint.h"
#include "SoulsTorch.h"
#include "UnrealSoulslike.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PhysicsVolume.h"
#include "TimerManager.h"

namespace
{
	const FName AbilityOffHand(TEXT("hand_l"));
	constexpr float AbilityHookThrowTime = 0.22f;
	constexpr float AbilityGrappleSpeed = 3600.f;   // the zip
	constexpr float AbilityLaunchUp = 1250.f;        // flung up and over the ledge on arrival...
	constexpr float AbilityLaunchForward = 700.f;    // ...and on past it
	constexpr float AbilityGlideSpeed = 1150.f;      // gliding: forward speed
	constexpr float AbilityGlideSink = 230.f;        // gliding: the most it sinks per second
	constexpr float AbilityGlideTurn = 120.f;        // gliding: degrees a second it banks round
	constexpr float AbilityPounceWindup = 0.6f;
	constexpr float AbilityPounceFlight = 0.75f;
}

bool ASoulsKnight::IsSwimming() const
{
	return GetCharacterMovement() && GetCharacterMovement()->IsSwimming();
}

// ---------------------------------------------------------------------------------------------
// Grapple
// ---------------------------------------------------------------------------------------------

AActor* ASoulsKnight::FindGrappleTarget() const
{
	if (ASoulsKnight* Locked = LockTarget.Get())
	{
		return FVector::Dist(Locked->GetActorLocation(), GetActorLocation()) <= GrappleFoeRange ? Locked : nullptr;
	}
	const AController* C = GetController();
	if (!C)
	{
		return nullptr;
	}
	FVector ViewLoc;
	FRotator ViewRot;
	C->GetPlayerViewPoint(ViewLoc, ViewRot);
	const FVector ViewDir = ViewRot.Vector();
	const FVector Eye = GetActorLocation() + FVector(0.f, 0.f, 60.f);

	AActor* Best = nullptr;
	float BestScore = TNumericLimits<float>::Max();
	auto Consider = [&](AActor* Candidate, const FVector& Point, float Range)
	{
		const float FromMe = FVector::Dist(Point, GetActorLocation());
		if (FromMe > Range || FromMe < 250.f)
		{
			return;
		}
		const FVector To = Point - ViewLoc;
		const float Dot = FVector::DotProduct(ViewDir, To.GetSafeNormal());
		if (Dot < 0.9f)
		{
			return;
		}
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(GrappleSight), false, this);
		Params.AddIgnoredActor(Candidate);
		const FVector End = Point - (Point - Eye).GetSafeNormal() * 40.f;
		if (GetWorld()->LineTraceSingleByChannel(Hit, Eye, End, ECC_Visibility, Params))
		{
			return;
		}
		const float Score = (1.f - Dot) * 9000.f + FromMe * 0.25f;
		if (Score < BestScore)
		{
			BestScore = Score;
			Best = Candidate;
		}
	};
	for (TActorIterator<AGrapplePoint> It(GetWorld()); It; ++It)
	{
		Consider(*It, It->GetActorLocation(), GrappleRange);
	}
	for (TActorIterator<ASoulsKnight> It(GetWorld()); It; ++It)
	{
		if (*It != this && It->Team != Team && It->CanBeLockedOn())
		{
			Consider(*It, It->GetActorLocation() + FVector(0.f, 0.f, 40.f), GrappleFoeRange);
		}
	}
	return Best;
}

void ASoulsKnight::GrapplePressed()
{
	if (IsDead() || IsBusy() || IsSwimming() || GrappleMode != EGrappleMode::None || GetUpFirst())
	{
		return;
	}
	AActor* Target = FindGrappleTarget();
	if (!Target && IsPlayerControlled())
	{
		// No grapple point or foe in view: hook whatever ledge the camera is on.
		FVector Hook, Ground;
		if (FindLedge(Hook, Ground))
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			const FVector Out = (GetActorLocation() - Hook).GetSafeNormal2D();
			if (AGrapplePoint* Anchor = GetWorld()->SpawnActor<AGrapplePoint>(AGrapplePoint::StaticClass(), Hook, (-Out).Rotation(), Params))
			{
				Anchor->MakeLedgeAnchor(Ground);
				Target = Anchor;
			}
		}
	}
	if (!Target || !SpendStamina(GrappleCost))
	{
		return;
	}
	if (bGliding)
	{
		EndGlide();
	}
	ASoulsTorch::DropWielded(this);
	StartGrapple(Target);
}

void ASoulsKnight::StartGrapple(AActor* Target)
{
	SetBlocking(false);
	GrappleTarget = Target;
	if (Cast<AGrapplePoint>(Target))
	{
		GrappleMode = EGrappleMode::ToPoint;
		GrappleHook = Target->GetActorLocation();
	}
	else
	{
		const ASoulsKnight* Foe = Cast<ASoulsKnight>(Target);
		const ASoulsEnemy* Enemy = Cast<ASoulsEnemy>(Target);
		const bool bLarge = (Foe && Foe->GetActorScale3D().X > 1.2f) || (Enemy && Enemy->bIsBoss);
		GrappleMode = bLarge ? EGrappleMode::ToFoe : EGrappleMode::ReelFoe;
		GrappleHook = Target->GetActorLocation() + FVector(0.f, 0.f, 30.f);
	}
	FaceTowards(GrappleHook);
	PlayAction(EKnightAction::Grapple, ThrowMontage, 0.55f);
	GetWorldTimerManager().ClearTimer(ActionTimer); // the grapple ends itself
	bGrappleFlying = false;
	GrappleT = 0.f;
	GrappleLine->SetVisibility(true);
	PlaySound(RollSound, 0.4f);
	GetWorldTimerManager().SetTimer(GrappleTimer, this, &ASoulsKnight::LaunchGrapple, AbilityHookThrowTime, false);
}

void ASoulsKnight::LaunchGrapple()
{
	AActor* Target = GrappleTarget.Get();
	ASoulsKnight* Foe = Cast<ASoulsKnight>(Target);
	if (!Target || (Foe && !Foe->CanBeLockedOn()))
	{
		EndGrapple(false);
		return;
	}
	ACharacter* Mover = GrappleMode == EGrappleMode::ReelFoe ? static_cast<ACharacter*>(Foe) : static_cast<ACharacter*>(this);
	GrappleStart = Mover->GetActorLocation();
	float Speed = AbilityGrappleSpeed;
	if (GrappleMode == EGrappleMode::ToPoint)
	{
		GrappleEnd = Cast<AGrapplePoint>(Target)->GetLandingLocation(GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	}
	else if (GrappleMode == EGrappleMode::ToFoe)
	{
		GrappleEnd = Foe->GetActorLocation();
	}
	else
	{
		GrappleEnd = GetActorLocation() + GetActorForwardVector() * 150.f;
		// The foe is yanked off its feet and can't act until it lands at your blade.
		Foe->Interrupt();
		Foe->PlayAction(EKnightAction::Staggered, Foe->HitMontage, 2.f);
		Speed *= 1.15f;
	}
	GrappleDuration = FMath::Clamp(FVector::Dist(GrappleStart, GrappleEnd) / Speed, 0.3f, 1.25f);
	UCharacterMovementComponent* Movement = Mover->GetCharacterMovement();
	Movement->SetMovementMode(MOVE_Falling);
	Movement->GravityScale = 0.f;
	bGrappleFlying = true;
	PlayRandomSound(SwingSounds, 0.5f);
}

void ASoulsKnight::TickGrapple(float DeltaSeconds)
{
	if (GrappleMode == EGrappleMode::None || !bGrappleFlying)
	{
		return;
	}
	AActor* Target = GrappleTarget.Get();
	ASoulsKnight* Foe = Cast<ASoulsKnight>(Target);
	if (!Target || (Foe && !Foe->CanBeLockedOn()))   // dead, or melted into shadow
	{
		EndGrapple(false);
		return;
	}
	ACharacter* Mover = GrappleMode == EGrappleMode::ReelFoe ? static_cast<ACharacter*>(Foe) : static_cast<ACharacter*>(this);

	// The far end can move: the foe you're flying at, or you, the one reeling.
	if (GrappleMode == EGrappleMode::ToFoe)
	{
		const FVector Back = (GetActorLocation() - Foe->GetActorLocation()).GetSafeNormal2D();
		GrappleEnd = Foe->GetActorLocation() + Back * (95.f * Foe->GetActorScale3D().X + 45.f) + FVector(0.f, 0.f, 40.f);
		GrappleHook = Foe->GetActorLocation() + FVector(0.f, 0.f, 30.f);
	}
	else if (GrappleMode == EGrappleMode::ReelFoe)
	{
		GrappleEnd = GetActorLocation() + GetActorForwardVector() * 150.f;
		GrappleHook = Foe->GetActorLocation() + FVector(0.f, 0.f, 30.f);
		FaceTowards(Foe->GetActorLocation());
	}

	GrappleT = FMath::Min(GrappleT + DeltaSeconds / FMath::Max(GrappleDuration, 0.05f), 1.f);
	const float T = 1.f - FMath::Square(1.f - GrappleT); // ease out
	const float Lift = GrappleMode == EGrappleMode::ToPoint ? FMath::Max(160.f, FVector::Dist(GrappleStart, GrappleEnd) * 0.22f) : 70.f;
	const FVector Control = (GrappleStart + GrappleEnd) * 0.5f + FVector(0.f, 0.f, Lift);
	const FVector Point = FMath::Lerp(FMath::Lerp(GrappleStart, Control, T), FMath::Lerp(Control, GrappleEnd, T), T);
	FVector Velocity = (Point - Mover->GetActorLocation()) / FMath::Max(DeltaSeconds, 0.001f);
	Velocity = Velocity.GetClampedToMaxSize(5000.f);
	Mover->GetCharacterMovement()->Velocity = Velocity;
	if (Mover == this && !Velocity.IsNearlyZero())
	{
		SetActorRotation(FRotator(0.f, (GrappleMode == EGrappleMode::ToPoint ? Velocity : GrappleHook - GetActorLocation()).Rotation().Yaw, 0.f));
	}

	const float Remaining = FVector::Dist(Mover->GetActorLocation(), GrappleEnd);
	if (GrappleT >= 1.f || (GrappleMode != EGrappleMode::ToPoint && Remaining < 70.f))
	{
		EndGrapple(true);
	}
}

void ASoulsKnight::EndGrapple(bool bArrived)
{
	const EGrappleMode Mode = GrappleMode;
	ASoulsKnight* Foe = Cast<ASoulsKnight>(GrappleTarget.Get());
	GrappleMode = EGrappleMode::None;
	bGrappleFlying = false;
	GrappleTarget = nullptr;
	GrappleLine->SetVisibility(false);
	GetWorldTimerManager().ClearTimer(GrappleTimer);

	ACharacter* Mover = (Mode == EGrappleMode::ReelFoe && Foe) ? static_cast<ACharacter*>(Foe) : static_cast<ACharacter*>(this);
	UCharacterMovementComponent* Movement = Mover->GetCharacterMovement();
	Movement->GravityScale = 1.f;
	if (Movement->MovementMode == MOVE_Falling)
	{
		Movement->Velocity = Movement->Velocity.GetClampedToMaxSize(250.f);
	}
	GetCharacterMovement()->GravityScale = 1.f;

	if (Mode == EGrappleMode::ToFoe && bArrived && Foe && !Foe->IsDead())
	{
		// Reeled in on the big one: carry the momentum into a falling cut.
		FaceTowards(Foe->GetActorLocation());
		UAnimMontage* Leap = SwordComboMontages.Num() > 2 ? SwordComboMontages[2].Get() : HeavyAttackMontage.Get();
		PlayAction(EKnightAction::HeavyAttack, Leap, 0.8f, 0.35f);
		HitThisSwing.Reset();
		const TWeakObjectPtr<ASoulsKnight> WeakThis(this);
		const TWeakObjectPtr<ASoulsKnight> WeakFoe(Foe);
		GetWorldTimerManager().SetTimer(HitTimer, [WeakThis, WeakFoe]()
		{
			if (WeakThis.IsValid() && WeakFoe.IsValid() && !WeakFoe->IsDead())
			{
				const FVector Impact = WeakFoe->GetActorLocation() + FVector(0.f, 0.f, 60.f);
				WeakFoe->ReceiveMelee(WeakThis.Get(), WeakThis->HeavyDamage * 1.7f, 60.f, 80.f, true, Impact);
			}
		}, 0.22f, false);
		return;
	}
	if (Mode == EGrappleMode::ReelFoe && Foe && !Foe->IsDead())
	{
		// Dragged to your feet, dazed: a free opening, and a critical if it never saw you coming.
		Foe->Shock(bArrived ? 1.4f : 0.8f);
	}
	EndAction();
	if (Mode == EGrappleMode::ToPoint && bArrived && IsPlayerControlled())
	{
		// Reeled up to the ledge: flung up and over it, tumbling - and free to glide (A) from the top.
		const FVector Forward = GetActorForwardVector().GetSafeNormal2D();
		LaunchCharacter(Forward * AbilityLaunchForward + FVector(0.f, 0.f, AbilityLaunchUp), true, true);
		GrappleLaunchTime = GetWorld()->GetTimeSeconds();
		if (UAnimInstance* Anim = GetMesh()->GetAnimInstance(); Anim && RollMontage)
		{
			Anim->Montage_Play(RollMontage, 1.25f);
		}
		PlayRandomSound(SwingSounds, 0.6f);
		UE_LOG(LogUnrealSoulslike, Log, TEXT("Grapple: launched off the ledge at %s"), *GetActorLocation().ToCompactString());
	}
}

bool ASoulsKnight::FindLedge(FVector& OutHook, FVector& OutGround) const
{
	const AController* C = GetController();
	if (!C)
	{
		return false;
	}
	FVector ViewLoc;
	FRotator ViewRot;
	C->GetPlayerViewPoint(ViewLoc, ViewRot);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(GrappleLedge), true, this);
	FHitResult Hit;
	if (!GetWorld()->LineTraceSingleByChannel(Hit, ViewLoc, ViewLoc + ViewRot.Vector() * GrappleRange, ECC_Visibility, Params)
		|| Cast<APawn>(Hit.GetActor()))
	{
		return false;
	}
	const FVector Point = Hit.ImpactPoint;
	if (FVector::Dist(Point, GetActorLocation()) < 300.f)
	{
		return false;
	}
	if (Hit.ImpactNormal.Z > 0.7f)
	{
		// Looking at a top: land on it.
		OutGround = Point;
		OutHook = Point + FVector(0.f, 0.f, 20.f);
	}
	else
	{
		// Looking at a wall: find its top edge above the hit, and the walkable ground just behind it.
		const FVector Into = Point - FVector(Hit.ImpactNormal.X, Hit.ImpactNormal.Y, 0.f).GetSafeNormal() * 60.f;
		FHitResult Top;
		if (!GetWorld()->LineTraceSingleByChannel(Top, Into + FVector(0.f, 0.f, 1600.f), Into - FVector(0.f, 0.f, 60.f), ECC_Visibility, Params)
			|| Top.ImpactNormal.Z < 0.7f || Top.bStartPenetrating)
		{
			return false;
		}
		OutGround = Top.ImpactPoint;
		OutHook = FVector(Point.X, Point.Y, Top.ImpactPoint.Z);
	}
	// Somewhere worth going: not below the feet, and room to stand.
	if (OutGround.Z < GetActorLocation().Z - GetCapsuleComponent()->GetScaledCapsuleHalfHeight() - 50.f)
	{
		return false;
	}
	const float Half = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	return !GetWorld()->OverlapBlockingTestByChannel(OutGround + FVector(0.f, 0.f, Half + 10.f), FQuat::Identity, ECC_Pawn,
		FCollisionShape::MakeCapsule(GetCapsuleComponent()->GetScaledCapsuleRadius() * 0.8f, Half), Params);
}

void ASoulsKnight::StartGlide()
{
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	if (bGliding || IsDead() || IsSwimming() || !Movement->IsFalling() || GrappleMode != EGrappleMode::None)
	{
		return;
	}
	bGliding = true;
	Movement->GravityScale = 0.15f;
	Movement->bOrientRotationToMovement = false;
	// Spread out flat, face down, arms wide.
	if (UAnimInstance* Anim = GetMesh()->GetAnimInstance(); Anim && SwimMontage)
	{
		Anim->Montage_Play(SwimMontage, 1.f);
		Anim->Montage_Pause(SwimMontage);
	}
	UE_LOG(LogUnrealSoulslike, Log, TEXT("Grapple: gliding from %.0f cm up"), GetActorLocation().Z);
}

void ASoulsKnight::EndGlide()
{
	if (!bGliding)
	{
		return;
	}
	bGliding = false;
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	Movement->GravityScale = 1.f;
	Movement->bOrientRotationToMovement = true;
	if (UAnimInstance* Anim = GetMesh()->GetAnimInstance(); Anim && SwimMontage && Anim->Montage_IsActive(SwimMontage))
	{
		Anim->Montage_Stop(0.25f, SwimMontage);
	}
}

void ASoulsKnight::TickGlide(float DeltaSeconds)
{
	if (!bGliding)
	{
		return;
	}
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	if (IsDead() || IsSwimming() || !Movement->IsFalling())
	{
		EndGlide();
		return;
	}
	// Bank round toward the stick.
	const FVector Stick = Movement->GetCurrentAcceleration().GetSafeNormal2D();
	FRotator Facing(0.f, GetActorRotation().Yaw, 0.f);
	if (!Stick.IsNearlyZero())
	{
		Facing = FMath::RInterpConstantTo(Facing, FRotator(0.f, Stick.Rotation().Yaw, 0.f), DeltaSeconds, AbilityGlideTurn);
		SetActorRotation(Facing);
	}
	// Carried forward on the air, sinking slowly.
	FVector Velocity = Movement->Velocity;
	const FVector Want = Facing.Vector() * AbilityGlideSpeed;
	const FVector Flat = FMath::VInterpTo(FVector(Velocity.X, Velocity.Y, 0.f), Want, DeltaSeconds, 1.8f);
	Velocity.X = Flat.X;
	Velocity.Y = Flat.Y;
	Velocity.Z = FMath::Max(Velocity.Z, -AbilityGlideSink);
	Movement->Velocity = Velocity;
}

void ASoulsKnight::UpdateGrappleLine()
{
	if (GrappleMode == EGrappleMode::None)
	{
		return;
	}
	const FVector Hand = GetMesh()->GetSocketLocation(AbilityOffHand);
	FVector Hook = GrappleHook;
	if (!bGrappleFlying)
	{
		const float Alpha = FMath::Clamp(GetWorldTimerManager().GetTimerElapsed(GrappleTimer) / AbilityHookThrowTime, 0.f, 1.f);
		Hook = FMath::Lerp(Hand, GrappleHook, Alpha);
	}
	const FVector Span = Hook - Hand;
	const float Length = Span.Size();
	if (Length < 1.f)
	{
		return;
	}
	GrappleLine->SetWorldLocationAndRotation((Hand + Hook) * 0.5f, FRotationMatrix::MakeFromZ(Span / Length).Rotator());
	GrappleLine->SetWorldScale3D(FVector(0.025f, 0.025f, Length / 100.f));
}

// ---------------------------------------------------------------------------------------------
// Pounce, brace, impale
// ---------------------------------------------------------------------------------------------

ASoulsKnight* ASoulsKnight::FindIncomingPouncer() const
{
	for (TActorIterator<ASoulsKnight> It(GetWorld()); It; ++It)
	{
		if (It->IsPouncing() && It->PounceTarget.Get() == this && FVector::Dist(It->GetActorLocation(), GetActorLocation()) < 1400.f)
		{
			return *It;
		}
	}
	return nullptr;
}

void ASoulsKnight::StartBrace()
{
	ASoulsTorch::DropWielded(this);
	SetBlocking(false);
	if (ASoulsKnight* Pouncer = FindIncomingPouncer())
	{
		FaceTowards(Pouncer->GetActorLocation());
	}
	// Set the blade like a pike: the heavy thrust, held at full extension.
	PlayAction(EKnightAction::Brace, HeavyAttackMontage, 1.1f, 0.45f);
	const TWeakObjectPtr<ASoulsKnight> WeakThis(this);
	GetWorldTimerManager().SetTimer(BlockHoldTimer, [WeakThis]()
	{
		if (WeakThis.IsValid() && WeakThis->IsBracing())
		{
			if (UAnimInstance* Anim = WeakThis->GetMesh()->GetAnimInstance())
			{
				Anim->Montage_Pause(WeakThis->HeavyAttackMontage);
			}
		}
	}, 0.12f, false);
	PlayRandomSound(SwingSounds, 0.5f);
}

void ASoulsKnight::BeginPounce(ASoulsKnight* Target)
{
	if (IsDead() || IsBusy() || !Target || GetCharacterMovement()->IsFalling())
	{
		return;
	}
	PounceTarget = Target;
	bPounceAirborne = false;
	FaceTowards(Target->GetActorLocation());
	// Crouch behind the shield, then spring.
	PlayAction(EKnightAction::Pounce, BlockMontage, AbilityPounceWindup + AbilityPounceFlight + 1.2f);
	GetWorldTimerManager().SetTimer(PounceTimer, this, &ASoulsKnight::LaunchPounce, AbilityPounceWindup, false);
}

void ASoulsKnight::LaunchPounce()
{
	ASoulsKnight* Target = PounceTarget.Get();
	if (Action != EKnightAction::Pounce || !Target || Target->IsDead())
	{
		EndAction();
		return;
	}
	// A ballistic arc that comes down on where the target is heading.
	const FVector Aim = Target->GetActorLocation() + Target->GetVelocity() * 0.25f + FVector(0.f, 0.f, 40.f);
	const FVector Delta = Aim - GetActorLocation();
	const float Gravity = FMath::Abs(GetWorld()->GetGravityZ()) * GetCharacterMovement()->GravityScale;
	const float Time = AbilityPounceFlight;
	FVector Launch = Delta / Time;
	Launch.Z = (Delta.Z + 0.5f * Gravity * Time * Time) / Time;
	FaceTowards(Aim);
	LaunchCharacter(Launch, true, true);
	PounceVelocity = Launch;
	bPounceAirborne = true;
	if (SwordComboMontages.Num() > 2 && SwordComboMontages[2])
	{
		if (UAnimInstance* Anim = GetMesh()->GetAnimInstance())
		{
			Anim->Montage_Play(SwordComboMontages[2], 1.1f, EMontagePlayReturnType::MontageLength, 0.2f);
		}
	}
	PlayRandomSound(SwingSounds, 1.f);
	ActionElapsed = 0.f;
}

void ASoulsKnight::TickPounce(float DeltaSeconds)
{
	if (!bPounceAirborne)
	{
		return;
	}
	if (Action != EKnightAction::Pounce)
	{
		bPounceAirborne = false;
		return;
	}
	// Hold the leap's carry: the movement component's air braking would otherwise drop it short.
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	if (Movement->IsFalling())
	{
		Movement->Velocity.X = PounceVelocity.X;
		Movement->Velocity.Y = PounceVelocity.Y;
	}
	ASoulsKnight* Target = PounceTarget.Get();
	if (Target && !Target->IsDead())
	{
		// Contact anywhere from beside to right on top of the target (pawns slide off each other).
		const float Flat = FVector::Dist2D(GetActorLocation(), Target->GetActorLocation());
		const float Rise = GetActorLocation().Z - Target->GetActorLocation().Z;
		if (Flat < 175.f && Rise > -40.f && Rise < 260.f)
		{
			bPounceAirborne = false;
			const bool bFacing = FVector::DotProduct(Target->GetActorForwardVector(), (GetActorLocation() - Target->GetActorLocation()).GetSafeNormal2D()) > 0.2f;
			if (Target->IsBracing() && bFacing)
			{
				Target->ImpalePouncer(this);
			}
			else
			{
				Target->GetPinned(this);
			}
			return;
		}
	}
	if (ActionElapsed > 0.2f && GetCharacterMovement()->IsMovingOnGround())
	{
		// Missed: a clumsy landing, wide open.
		bPounceAirborne = false;
		PlayAction(EKnightAction::Staggered, HitMontage, 0.9f);
	}
}

void ASoulsKnight::GetPinned(ASoulsKnight* Pouncer)
{
	if (IsDead())
	{
		return;
	}
	if (Action == EKnightAction::Roll && ActionElapsed >= RollInvulnerableStart && ActionElapsed <= RollInvulnerableEnd)
	{
		return;
	}
	const FVector Away = (GetActorLocation() - Pouncer->GetActorLocation()).GetSafeNormal2D();
	PlayRandomSound(HitSounds, 1.f);
	PlayRandomSound(HurtSounds, 0.9f);
	ReceivedDamage(Pouncer->PounceDamage, GetActorLocation() + FVector(0.f, 0.f, 60.f), Away);
	if (bBlocking && FVector::DotProduct(GetActorForwardVector(), -Away) > 0.2f)
	{
		// A raised shield takes the brunt, but the weight still knocks you back.
		Stamina -= 45.f;
		LoseHealth(Pouncer->PounceDamage * 0.35f, Pouncer);
	}
	else
	{
		LoseHealth(Pouncer->PounceDamage, Pouncer);
	}
	if (!IsDead())
	{
		Interrupt();
		PlayAction(EKnightAction::Staggered, StaggerMontage ? StaggerMontage.Get() : HitMontage.Get(), 1.2f);
		GetCharacterMovement()->AddImpulse(Away * 500.f, true);
	}
	Hitstop(0.12f);
	KickCamera(1.f);
	Pouncer->GetCharacterMovement()->Velocity = -Away * 300.f + FVector(0.f, 0.f, 150.f);
}

void ASoulsKnight::ImpalePouncer(ASoulsKnight* Pouncer)
{
	if (!Pouncer || Pouncer->IsDead())
	{
		return;
	}
	PlaySound(CriticalSound, 1.f);
	PlayRandomSound(HitSounds, 1.f);
	Hitstop(0.24f);
	KickCamera(1.f);
	PlayAction(EKnightAction::Critical, nullptr, 1.3f);

	// Run through on the braced blade, held aloft for a beat.
	Pouncer->Impaler = this;
	Pouncer->Interrupt();
	Pouncer->GetCharacterMovement()->StopMovementImmediately();
	Pouncer->GetCharacterMovement()->DisableMovement();
	Pouncer->GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	const FVector Forward = GetActorForwardVector().GetSafeNormal2D();
	Pouncer->SetActorLocationAndRotation(GetActorLocation() + Forward * 115.f + FVector(0.f, 0.f, 115.f),
		FRotator(-62.f, (-Forward).Rotation().Yaw, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
	Pouncer->PlayAction(EKnightAction::Impaled, Pouncer->StaggerMontage, 1.f);
	Pouncer->ReceivedDamage(Pouncer->CurrentHP, Pouncer->GetActorLocation(), -Forward);
	Pouncer->Hitstop(0.24f);
	Pouncer->bKilledByAmbush = true;

	const TWeakObjectPtr<ASoulsKnight> WeakThis(this);
	const TWeakObjectPtr<ASoulsKnight> WeakPouncer(Pouncer);
	FTimerHandle RollOff;
	GetWorldTimerManager().SetTimer(RollOff, [WeakThis, WeakPouncer]()
	{
		if (!WeakThis.IsValid() || !WeakPouncer.IsValid() || WeakPouncer->IsDead())
		{
			return;
		}
		ASoulsKnight* Victim = WeakPouncer.Get();
		Victim->SetActorRotation(FRotator(0.f, Victim->GetActorRotation().Yaw, 0.f));
		Victim->LoseHealth(Victim->CurrentHP + 1.f, WeakThis.Get());
		// ...and it slides off the blade to one side.
		const FVector Side = WeakThis->GetActorRightVector() * (FMath::RandBool() ? 1.f : -1.f);
		Victim->GetMesh()->AddImpulse((Side * 420.f + WeakThis->GetActorForwardVector() * 120.f + FVector(0.f, 0.f, 80.f)), NAME_None, true);
		WeakThis->PlayRandomSound(WeakThis->HitSounds, 0.6f);
	}, 0.8f, false);
}

// ---------------------------------------------------------------------------------------------
// Fear and shock
// ---------------------------------------------------------------------------------------------

void ASoulsKnight::AddFear(float Amount)
{
	Fear = FMath::Clamp(Fear + Amount, 0.f, 100.f);
	LastFearTime = GetWorld()->GetTimeSeconds();
}

void ASoulsKnight::Shock(float Duration)
{
	if (IsDead() || Action == EKnightAction::Crippled || Action == EKnightAction::Impaled)
	{
		return;
	}
	Interrupt();
	GetWorldTimerManager().ClearTimer(ActionTimer);
	PlayAction(EKnightAction::Shocked, GuardBreakMontage ? GuardBreakMontage.Get() : HitMontage.Get(), Duration);
	LastAmbushTime = GetWorld()->GetTimeSeconds();
	AddFear(20.f);
}

void ASoulsKnight::TickFear(float DeltaSeconds)
{
	if (Fear > 0.f && GetWorld()->GetTimeSeconds() - LastFearTime > 4.f)
	{
		Fear = FMath::Max(0.f, Fear - 7.f * DeltaSeconds);
	}
}

// ---------------------------------------------------------------------------------------------
// Swimming
// ---------------------------------------------------------------------------------------------

void ASoulsKnight::TickSwimming(float DeltaSeconds)
{
	const bool bSwim = IsSwimming();
	UAnimInstance* Anim = GetMesh()->GetAnimInstance();
	if (bSwim)
	{
		if (!bWasSwimming)
		{
			Interrupt();
			GetWorldTimerManager().ClearTimer(ActionTimer);
			Action = EKnightAction::None;
			LockTarget = nullptr;
			PlayRandomSound(FootstepSounds, 0.6f);
		}
		// Float at the surface, head and shoulders out (no diving).
		UCharacterMovementComponent* Movement = GetCharacterMovement();
		if (const APhysicsVolume* Water = Movement->GetPhysicsVolume())
		{
			const float Surface = Water->GetComponentsBoundingBox().Max.Z;
			const float TargetZ = Surface - 45.f;
			const float Lift = FMath::Clamp((TargetZ - GetActorLocation().Z) * 4.f, -150.f, 300.f);
			Movement->Velocity.Z = FMath::FInterpTo(Movement->Velocity.Z, Lift, DeltaSeconds, 6.f);
		}
		const bool bMoving = GetVelocity().Size2D() > 60.f;
		UAnimMontage* Want = bMoving ? SwimMontage.Get() : SwimIdleMontage.Get();
		UAnimMontage* Other = bMoving ? SwimIdleMontage.Get() : SwimMontage.Get();
		if (Anim && Want && !Anim->Montage_IsPlaying(Want))
		{
			if (Other)
			{
				Anim->Montage_Stop(0.3f, Other);
			}
			Anim->Montage_Play(Want, 1.f);
			const FName Section = Want->GetSectionName(0);
			Anim->Montage_SetNextSection(Section, Section, Want);
		}
	}
	else if (bWasSwimming && Anim)
	{
		if (SwimMontage)
		{
			Anim->Montage_Stop(0.25f, SwimMontage);
		}
		if (SwimIdleMontage)
		{
			Anim->Montage_Stop(0.25f, SwimIdleMontage);
		}
	}
	bWasSwimming = bSwim;
}

// ---------------------------------------------------------------------------------------------
// Stealth stances: crouch, dive prone, army crawl
// ---------------------------------------------------------------------------------------------

namespace
{
	// Keep a looping stance montage going (and the one it replaces stopped).
	void AbilityLoopMontage(UAnimInstance* Anim, UAnimMontage* Want, UAnimMontage* Other, float Rate)
	{
		if (!Anim || !Want || Anim->Montage_IsPlaying(Want))
		{
			return;
		}
		if (Other && Anim->Montage_IsActive(Other))
		{
			Anim->Montage_Stop(0.25f, Other);
		}
		Anim->Montage_Play(Want, Rate);
		const FName Section = Want->GetSectionName(0);
		Anim->Montage_SetNextSection(Section, Section, Want);
	}

	// Lie still, face down: the crawl held mid-stroke.
	void AbilityHoldLying(UAnimInstance* Anim, UAnimMontage* Crawl)
	{
		if (!Anim || !Crawl)
		{
			return;
		}
		if (!Anim->Montage_IsActive(Crawl))
		{
			Anim->Montage_Play(Crawl, 1.f);
		}
		Anim->Montage_Pause(Crawl);
	}
}

float ASoulsKnight::GetVisibilityScale() const
{
	switch (Stance)
	{
	case EKnightStance::Prone: return 0.3f;
	case EKnightStance::Crouched: return 0.55f;
	default: return bSprinting ? 1.15f : 1.f;
	}
}

float ASoulsKnight::GetNoiseRadius() const
{
	// Inside this radius an enemy notices you whichever way it faces. Walking (not jogging) keeps it
	// inside backstab reach; crouching or crawling all but silences you.
	const float Speed = GetVelocity().Size2D();
	switch (Stance)
	{
	case EKnightStance::Prone: return 50.f;
	case EKnightStance::Crouched: return 110.f;
	default:
		if (bSprinting)
		{
			return 950.f;
		}
		return Speed > 350.f ? 420.f : (Speed > 60.f ? 190.f : 140.f);
	}
}

void ASoulsKnight::SetStance(EKnightStance NewStance)
{
	if (NewStance == Stance)
	{
		return;
	}
	Stance = NewStance;
	if (NewStance != EKnightStance::Standing)
	{
		bSprintToggled = false;
		SetBlocking(false);
	}
	if (UAnimInstance* Anim = GetMesh()->GetAnimInstance())
	{
		for (UAnimMontage* Montage : { CrouchIdleMontage.Get(), CrouchMoveMontage.Get(), CrawlMontage.Get(), GetUpMontage.Get() })
		{
			if (Montage && Anim->Montage_IsActive(Montage))
			{
				Anim->Montage_Stop(0.25f, Montage);
			}
		}
	}
}

bool ASoulsKnight::GetUpFirst()
{
	if (Stance != EKnightStance::Prone || IsDead())
	{
		return false;
	}
	if (!IsBusy())
	{
		GetUpFromProne();
	}
	return true;
}

void ASoulsKnight::DiveProne()
{
	if (Stance == EKnightStance::Prone || IsDead() || IsBusy() || IsSwimming())
	{
		return;
	}
	const FVector Stick = GetCharacterMovement()->GetCurrentAcceleration().GetSafeNormal2D();
	const bool bMoving = !Stick.IsNearlyZero() || GetVelocity().Size2D() > 200.f;
	const FVector Forward = Stick.IsNearlyZero() ? GetActorForwardVector().GetSafeNormal2D() : Stick;
	PlayAction(EKnightAction::StanceShift, nullptr, bMoving ? 0.6f : 0.45f);
	SetStance(EKnightStance::Prone);
	SetActorRotation(FRotator(0.f, Forward.Rotation().Yaw, 0.f));
	// A running leap flat onto the belly; from a standstill just drop.
	LaunchCharacter(Forward * (bMoving ? 560.f : 120.f) + FVector(0.f, 0.f, bMoving ? 190.f : 0.f), true, true);
	AbilityHoldLying(GetMesh()->GetAnimInstance(), CrawlMontage);
	PlayRandomSound(FootstepSounds, 0.5f);
	UE_LOG(LogTemp, Log, TEXT("Stealth: %s dives prone"), *GetName());
}

void ASoulsKnight::GetUpFromProne()
{
	if (Stance != EKnightStance::Prone)
	{
		return;
	}
	Stance = EKnightStance::Standing;
	if (UAnimInstance* Anim = GetMesh()->GetAnimInstance())
	{
		if (CrawlMontage && Anim->Montage_IsActive(CrawlMontage))
		{
			Anim->Montage_Stop(0.2f, CrawlMontage);
		}
	}
	// Push up off the belly through a crouch (the get-up clip starts on its back, so it would flip).
	PlayAction(EKnightAction::StanceShift, CrouchIdleMontage, 0.5f);
	UE_LOG(LogTemp, Log, TEXT("Stealth: %s gets up"), *GetName());
}

void ASoulsKnight::TickStance(float DeltaSeconds)
{
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	if (Stance != EKnightStance::Standing
		&& (IsDead() || IsSwimming() || (Movement->IsFalling() && Action != EKnightAction::StanceShift)))
	{
		SetStance(EKnightStance::Standing);
	}
	float WantDrop = 0.f;
	UAnimInstance* Anim = GetMesh()->GetAnimInstance();
	if (Anim && !IsBusy() && Stance != EKnightStance::Standing)
	{
		const bool bMoving = GetVelocity().Size2D() > 30.f;
		if (Stance == EKnightStance::Crouched)
		{
			AbilityLoopMontage(Anim, bMoving ? CrouchMoveMontage.Get() : CrouchIdleMontage.Get(),
				bMoving ? CrouchIdleMontage.Get() : CrouchMoveMontage.Get(), 1.f);
		}
		else if (bMoving)
		{
			if (CrawlMontage && Anim->Montage_IsActive(CrawlMontage) && !Anim->Montage_IsPlaying(CrawlMontage))
			{
				Anim->Montage_Resume(CrawlMontage);
				Anim->Montage_SetPlayRate(CrawlMontage, 0.75f);
				const FName Section = CrawlMontage->GetSectionName(0);
				Anim->Montage_SetNextSection(Section, Section, CrawlMontage);
			}
			AbilityLoopMontage(Anim, CrawlMontage, nullptr, 0.75f);
		}
		else
		{
			AbilityHoldLying(Anim, CrawlMontage);
		}
	}
	if (Stance == EKnightStance::Prone)
	{
		WantDrop = CrawlMeshDrop;
	}
	if (WantDrop > 0.f || MeshDrop > 0.01f)
	{
		MeshDrop = FMath::FInterpTo(MeshDrop, WantDrop, DeltaSeconds, 8.f);
		FVector Relative = GetMesh()->GetRelativeLocation();
		Relative.Z = BaseMeshZ - MeshDrop;
		GetMesh()->SetRelativeLocation(Relative);
	}
}

// ---------------------------------------------------------------------------------------------
// Idle talk
// ---------------------------------------------------------------------------------------------

void ASoulsKnight::SetTalking(bool bNewTalking)
{
	UAnimInstance* Anim = GetMesh()->GetAnimInstance();
	if (bNewTalking)
	{
		bTalking = true;
		if (Anim && TalkMontage && !Anim->IsAnyMontagePlaying())
		{
			Anim->Montage_Play(TalkMontage, FMath::FRandRange(0.9f, 1.1f));
			const FName Section = TalkMontage->GetSectionName(0);
			Anim->Montage_SetNextSection(Section, Section, TalkMontage);
		}
	}
	else if (bTalking)
	{
		bTalking = false;
		if (Anim && TalkMontage)
		{
			Anim->Montage_Stop(0.35f, TalkMontage);
		}
	}
}

void ASoulsKnight::PlayGesture()
{
	UAnimInstance* Anim = GetMesh()->GetAnimInstance();
	if (Anim && !GestureMontages.IsEmpty() && !IsBusy())
	{
		if (UAnimMontage* Gesture = GestureMontages[FMath::RandRange(0, GestureMontages.Num() - 1)])
		{
			Anim->Montage_Play(Gesture, 1.f);
		}
	}
}
