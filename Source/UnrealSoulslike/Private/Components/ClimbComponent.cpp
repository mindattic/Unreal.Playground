#include "Components/ClimbComponent.h"
#include "Components/CombatComponent.h"
#include "Components/VitalityComponent.h"
#include "Components/ShadowMergeComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"

namespace
{
	const FName AnimClimb(TEXT("climb"));
	const FName AnimClimbIdle(TEXT("climb_idle"));
	const FName AnimClimbJump(TEXT("climb_jump"));
	const FName AnimMantle(TEXT("mantle"));
}

UClimbComponent::UClimbComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	SetIsReplicatedByDefault(true);
}

void UClimbComponent::BeginPlay()
{
	Super::BeginPlay();
	if (UCharacterMovementComponent* Movement = GetMovement())
	{
		DefaultMaxFlySpeed = Movement->MaxFlySpeed;
		DefaultBrakingFlying = Movement->BrakingDecelerationFlying;
		bDefaultOrientToMovement = Movement->bOrientRotationToMovement;
		// Our input must be in before the movement component consumes it this frame.
		Movement->PrimaryComponentTick.AddPrerequisite(this, PrimaryComponentTick);
	}
}

void UClimbComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	// The owner predicts its own climbing; only everyone else needs the server's copy.
	DOREPLIFETIME_CONDITION(UClimbComponent, bClimbing, COND_SkipOwner);
}

ACharacter* UClimbComponent::GetCharacter() const
{
	return Cast<ACharacter>(GetOwner());
}

UCharacterMovementComponent* UClimbComponent::GetMovement() const
{
	const ACharacter* Character = GetCharacter();
	return Character ? Character->GetCharacterMovement() : nullptr;
}

bool UClimbComponent::IsClimbableNormal(const FVector& Normal) const
{
	return Normal.Z < MaxClimbableNormalZ && Normal.Z > -0.3f;
}

bool UClimbComponent::TraceWall(const FVector& Start, const FVector& Direction, float Distance, FHitResult& OutHit) const
{
	FCollisionObjectQueryParams Objects(ECC_WorldStatic);
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ClimbTrace), true, GetOwner());
	return GetWorld()->LineTraceSingleByObjectType(OutHit, Start, Start + Direction * Distance, Objects, Params);
}

void UClimbComponent::AddClimbInput(const FVector2D& Input)
{
	PendingInput = Input;
}

void UClimbComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	ACharacter* Character = GetCharacter();
	if (!Character)
	{
		return;
	}

	ReleaseCooldown = FMath::Max(0.f, ReleaseCooldown - DeltaTime);

	if (Character->IsLocallyControlled())
	{
		if (bClimbing)
		{
			bMantling ? TickMantle(DeltaTime) : TickClimbing(DeltaTime);
		}
		else
		{
			TryEnter(DeltaTime);
		}
	}

	if (Character->HasAuthority() && bClimbing)
	{
		DrainStamina(DeltaTime);
	}
	PendingInput = FVector2D::ZeroVector;
}

void UClimbComponent::TryEnter(float DeltaTime)
{
	ACharacter* Character = GetCharacter();
	UCharacterMovementComponent* Movement = GetMovement();
	const UCombatComponent* Combat = Character->FindComponentByClass<UCombatComponent>();
	const UVitalityComponent* Vitality = Character->FindComponentByClass<UVitalityComponent>();
	const UShadowMergeComponent* Merge = Character->FindComponentByClass<UShadowMergeComponent>();
	if (!Movement || ReleaseCooldown > 0.f || !Combat || !Combat->CanAct() || !Vitality || !Vitality->IsAlive()
		|| Vitality->Stamina < 5.f || (Merge && Merge->IsMerged()))
	{
		EnterTimer = 0.f;
		return;
	}

	const bool bFalling = Movement->IsFalling();
	FVector Direction = Character->GetLastMovementInputVector().GetSafeNormal2D();
	if (Direction.IsNearlyZero())
	{
		if (!bFalling)
		{
			EnterTimer = 0.f;
			return;
		}
		Direction = Movement->Velocity.GetSafeNormal2D();
		if (Direction.IsNearlyZero())
		{
			Direction = Character->GetActorForwardVector();
		}
	}

	const float Radius = Character->GetCapsuleComponent()->GetScaledCapsuleRadius();
	const FVector Chest = Character->GetActorLocation() + FVector(0.f, 0.f, 30.f);
	FHitResult Hit;
	if (!TraceWall(Chest, Direction, Radius + 40.f, Hit) || !IsClimbableNormal(Hit.ImpactNormal)
		|| FVector::DotProduct(Direction, -Hit.ImpactNormal.GetSafeNormal2D()) < 0.5f)
	{
		EnterTimer = 0.f;
		return;
	}

	EnterTimer += DeltaTime;
	if (bFalling || EnterTimer >= EnterDelay)
	{
		StartClimbing(Hit);
	}
}

void UClimbComponent::StartClimbing(const FHitResult& Hit)
{
	UCharacterMovementComponent* Movement = GetMovement();
	EnterTimer = 0.f;
	bClimbing = true;
	bMantling = false;
	ClimbJumpTimer = 0.f;
	WallNormal = Hit.ImpactNormal;

	Movement->StopMovementImmediately();
	Movement->bOrientRotationToMovement = false;
	Movement->BrakingDecelerationFlying = 4000.f;
	Movement->SetMovementMode(MOVE_Flying);
	SetFlySpeed(ClimbSpeed);
	FaceWall();

	ServerSetClimbing(true, WallNormal);
}

void UClimbComponent::ExitClimbing(bool bToWalking)
{
	UCharacterMovementComponent* Movement = GetMovement();
	bClimbing = false;
	bMantling = false;
	ClimbJumpTimer = 0.f;
	if (Movement)
	{
		Movement->bOrientRotationToMovement = bDefaultOrientToMovement;
		Movement->MaxFlySpeed = DefaultMaxFlySpeed;
		Movement->BrakingDecelerationFlying = DefaultBrakingFlying;
		Movement->SetMovementMode(bToWalking ? MOVE_Walking : MOVE_Falling);
	}
	ServerSetClimbing(false, WallNormal);
}

void UClimbComponent::FaceWall()
{
	ACharacter* Character = GetCharacter();
	const float Yaw = (-WallNormal).GetSafeNormal2D().Rotation().Yaw;
	Character->SetActorRotation(FRotator(0.f, Yaw, 0.f));
	if (!Character->HasAuthority() && FMath::Abs(FMath::FindDeltaAngleDegrees(Yaw, LastSentYaw)) > 3.f)
	{
		LastSentYaw = Yaw;
		ServerSetFacing(Yaw);
	}
}

void UClimbComponent::SetFlySpeed(float Speed)
{
	UCharacterMovementComponent* Movement = GetMovement();
	if (!Movement || FMath::IsNearlyEqual(Movement->MaxFlySpeed, Speed))
	{
		return;
	}
	Movement->MaxFlySpeed = Speed;
	if (!GetOwner()->HasAuthority())
	{
		ServerSetFlySpeed(Speed);
	}
}

void UClimbComponent::ServerSetFlySpeed_Implementation(float Speed)
{
	UCharacterMovementComponent* Movement = GetMovement();
	if (Movement && bClimbing)
	{
		Movement->MaxFlySpeed = FMath::Clamp(Speed, 0.f, 1200.f);
	}
}

void UClimbComponent::TickClimbing(float DeltaTime)
{
	ACharacter* Character = GetCharacter();
	UCharacterMovementComponent* Movement = GetMovement();
	const UVitalityComponent* Vitality = Character->FindComponentByClass<UVitalityComponent>();
	if (!Vitality || !Vitality->IsAlive())
	{
		ExitClimbing(false);
		return;
	}

	const float Radius = Character->GetCapsuleComponent()->GetScaledCapsuleRadius();
	const float HalfHeight = Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const FVector Centre = Character->GetActorLocation();
	const FVector Chest = Centre + FVector(0.f, 0.f, 30.f);
	const FVector Head = Centre + FVector(0.f, 0.f, HalfHeight - 10.f);
	const float Reach = Radius + 60.f;

	FHitResult ChestHit;
	if (!TraceWall(Chest, -WallNormal, Reach, ChestHit) && !TraceWall(Chest, Character->GetActorForwardVector(), Reach, ChestHit))
	{
		ExitClimbing(false);
		return;
	}
	if (ChestHit.ImpactNormal.Z >= MaxClimbableNormalZ)
	{
		StartMantle(ChestHit.ImpactPoint + FVector(0.f, 0.f, HalfHeight + 5.f));
		return;
	}
	if (!IsClimbableNormal(ChestHit.ImpactNormal))
	{
		ExitClimbing(false);
		return;
	}

	WallNormal = FMath::VInterpNormalRotationTo(WallNormal, ChestHit.ImpactNormal, DeltaTime, 360.f).GetSafeNormal();
	FaceWall();

	const FVector Up = (FVector::UpVector - WallNormal * FVector::DotProduct(FVector::UpVector, WallNormal)).GetSafeNormal();
	const FVector Right = FVector::CrossProduct(Up, -WallNormal).GetSafeNormal();

	FHitResult HeadHit;
	if (PendingInput.Y > 0.1f && !TraceWall(Head, -WallNormal, Reach, HeadHit))
	{
		const FVector LedgeProbe = Head - WallNormal * (Radius + 50.f) + FVector(0.f, 0.f, 60.f);
		FHitResult Floor;
		if (TraceWall(LedgeProbe, -FVector::UpVector, 200.f, Floor) && Floor.ImpactNormal.Z >= MaxClimbableNormalZ)
		{
			StartMantle(Floor.ImpactPoint + FVector(0.f, 0.f, HalfHeight + 5.f));
			return;
		}
	}

	if (PendingInput.Y < -0.1f)
	{
		FHitResult Floor;
		if (TraceWall(Centre, -FVector::UpVector, HalfHeight + 15.f, Floor) && Floor.ImpactNormal.Z >= MaxClimbableNormalZ)
		{
			ExitClimbing(true);
			return;
		}
	}

	// Hold the capsule just off the surface so it follows curved cliffs and terrain.
	const float Gap = ChestHit.Distance - (Radius + 5.f);
	const FVector Stick = -WallNormal * FMath::Clamp(Gap / 30.f, -1.f, 1.f) * 0.5f;

	if (ClimbJumpTimer > 0.f)
	{
		ClimbJumpTimer -= DeltaTime;
		SetFlySpeed(ClimbJumpDistance / FMath::Max(ClimbJumpDuration, 0.05f));
		Character->AddMovementInput(ClimbJumpDirection + Stick, 1.f);
		return;
	}

	SetFlySpeed(ClimbSpeed);
	const FVector Move = Right * PendingInput.X + Up * PendingInput.Y;
	const FVector Desired = Move + Stick;
	if (!Desired.IsNearlyZero())
	{
		Character->AddMovementInput(Desired.GetSafeNormal(), FMath::Min(Desired.Size(), 1.f));
	}
}

void UClimbComponent::StartMantle(const FVector& Target)
{
	bMantling = true;
	bMantleRising = true;
	MantleTimer = 0.f;
	MantleTarget = Target;
	SetFlySpeed(400.f);
}

void UClimbComponent::TickMantle(float DeltaTime)
{
	ACharacter* Character = GetCharacter();
	MantleTimer += DeltaTime;
	if (MantleTimer > MantleDuration * 3.f)
	{
		ExitClimbing(false);
		return;
	}

	const FVector Location = Character->GetActorLocation();
	const FVector Waypoint = bMantleRising ? FVector(Location.X, Location.Y, MantleTarget.Z) : MantleTarget;
	const FVector ToWaypoint = Waypoint - Location;
	if (ToWaypoint.Size() < 20.f)
	{
		if (bMantleRising)
		{
			bMantleRising = false;
			return;
		}
		ExitClimbing(true);
		return;
	}
	Character->AddMovementInput(ToWaypoint.GetSafeNormal(), 1.f);
}

void UClimbComponent::HandleJumpPressed()
{
	ACharacter* Character = GetCharacter();
	if (!Character)
	{
		return;
	}
	if (!bClimbing)
	{
		Character->Jump();
		return;
	}
	if (bMantling || ClimbJumpTimer > 0.f)
	{
		return;
	}
	const UVitalityComponent* Vitality = Character->FindComponentByClass<UVitalityComponent>();
	if (!Vitality || Vitality->Stamina < ClimbJumpStaminaCost)
	{
		return;
	}

	const FVector Up = (FVector::UpVector - WallNormal * FVector::DotProduct(FVector::UpVector, WallNormal)).GetSafeNormal();
	const FVector Right = FVector::CrossProduct(Up, -WallNormal).GetSafeNormal();
	ClimbJumpDirection = FMath::Abs(PendingInput.X) > FMath::Abs(PendingInput.Y) && FMath::Abs(PendingInput.X) > 0.3f
		? Right * FMath::Sign(PendingInput.X)
		: Up;
	ClimbJumpTimer = ClimbJumpDuration;
	ServerClimbJump();
}

void UClimbComponent::Release()
{
	ACharacter* Character = GetCharacter();
	if (!bClimbing || !Character)
	{
		return;
	}
	const FVector PushOff = WallNormal * 300.f;
	ExitClimbing(false);
	ReleaseCooldown = 0.4f;
	Character->LaunchCharacter(PushOff, true, false);
}

void UClimbComponent::DrainStamina(float DeltaTime)
{
	ACharacter* Character = GetCharacter();
	UVitalityComponent* Vitality = Character->FindComponentByClass<UVitalityComponent>();
	if (!Vitality)
	{
		return;
	}
	const bool bMoving = Character->GetVelocity().SizeSquared() > FMath::Square(20.f);
	Vitality->SpendStamina((bMoving ? StaminaDrainMoving : StaminaDrainStill) * DeltaTime);
	if (Vitality->Stamina <= 0.f)
	{
		bClimbing = false;
		if (UCharacterMovementComponent* Movement = GetMovement())
		{
			Movement->SetMovementMode(MOVE_Falling);
		}
		if (Character->IsLocallyControlled())
		{
			Release();
		}
		else
		{
			ClientForceRelease();
		}
	}
}

void UClimbComponent::ServerSetClimbing_Implementation(bool bNewClimbing, FVector_NetQuantizeNormal Normal)
{
	ACharacter* Character = GetCharacter();
	UCharacterMovementComponent* Movement = GetMovement();
	if (!Character || !Movement || Character->IsLocallyControlled())
	{
		return;
	}
	bClimbing = bNewClimbing;
	WallNormal = Normal;
	if (bNewClimbing)
	{
		Movement->bOrientRotationToMovement = false;
		Movement->MaxFlySpeed = ClimbSpeed;
		Movement->BrakingDecelerationFlying = 4000.f;
		Movement->SetMovementMode(MOVE_Flying);
		Character->SetActorRotation(FRotator(0.f, (-WallNormal).GetSafeNormal2D().Rotation().Yaw, 0.f));
	}
	else
	{
		Movement->bOrientRotationToMovement = bDefaultOrientToMovement;
		Movement->MaxFlySpeed = DefaultMaxFlySpeed;
		Movement->BrakingDecelerationFlying = DefaultBrakingFlying;
		if (Movement->MovementMode == MOVE_Flying)
		{
			Movement->SetMovementMode(MOVE_Falling);
		}
	}
}

void UClimbComponent::ServerSetFacing_Implementation(float Yaw)
{
	if (bClimbing && GetCharacter())
	{
		GetCharacter()->SetActorRotation(FRotator(0.f, Yaw, 0.f));
	}
}

void UClimbComponent::ServerClimbJump_Implementation()
{
	ACharacter* Character = GetCharacter();
	UVitalityComponent* Vitality = Character ? Character->FindComponentByClass<UVitalityComponent>() : nullptr;
	if (bClimbing && Vitality)
	{
		Vitality->SpendStamina(ClimbJumpStaminaCost);
	}
}

void UClimbComponent::ClientForceRelease_Implementation()
{
	Release();
}

FName UClimbComponent::GetAnimOverride(bool& bOutLoop, float& OutDuration) const
{
	if (!bClimbing)
	{
		return NAME_None;
	}
	if (bMantling)
	{
		bOutLoop = false;
		OutDuration = MantleDuration;
		return AnimMantle;
	}
	if (ClimbJumpTimer > 0.f)
	{
		bOutLoop = false;
		OutDuration = ClimbJumpDuration;
		return AnimClimbJump;
	}
	bOutLoop = true;
	OutDuration = 0.f;
	const ACharacter* Character = GetCharacter();
	return Character && Character->GetVelocity().SizeSquared() > FMath::Square(20.f) ? AnimClimb : AnimClimbIdle;
}
