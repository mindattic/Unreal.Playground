#include "SkeletonEnemy.h"
#include "SoulslikeData.h"
#include "Components/CombatComponent.h"
#include "Components/VitalityComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "AIController.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"

ASkeletonEnemy::ASkeletonEnemy()
{
	AIControllerClass = AAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;

	DisplayName = NSLOCTEXT("Soulslike", "Skeleton", "Skeleton");
	TargetMeshHeight = 180.f;
	RunSpeed = 360.f;
	BlockingSpeed = 150.f;

	Combat->Team = SoulslikeTeam::Skeletons;
	Combat->LightAttackWindup = 0.55f;
	Combat->LightAttackDuration = 1.0f;
	Combat->HeavyAttackWindup = 0.85f;
	Combat->HeavyAttackDuration = 1.4f;
	Combat->LightAttackDamage = 14.f;
	Combat->HeavyAttackDamage = 26.f;
	Combat->LightAttackStaminaCost = 10.f;
	Combat->HeavyAttackStaminaCost = 18.f;

	Vitality->MaxHealth = 60.f;
	Vitality->MaxStamina = 80.f;
	Vitality->MaxHealCharges = 0;
}

void ASkeletonEnemy::ConfigureAsBoss()
{
	bIsBoss = true;
	DisplayName = NSLOCTEXT("Soulslike", "Boss", "The Hollow Laird");
	SetActorScale3D(FVector(1.5f));
	RunSpeed = 400.f;
	SightRange = 3000.f;
	LeashRadius = 2600.f;
	HeavyAttackChance = 0.4f;
	BlockChance = 0.25f;
	Vitality->MaxHealth = 450.f;
	Vitality->MaxStamina = 200.f;
	Combat->DamageMultiplier = 1.6f;
	Combat->bCanBeHitStunned = false;
}

void ASkeletonEnemy::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ASkeletonEnemy, VisualIndex);
	DOREPLIFETIME(ASkeletonEnemy, bIsBoss);
	DOREPLIFETIME(ASkeletonEnemy, Awareness);
	DOREPLIFETIME(ASkeletonEnemy, Suspicion);
}

void ASkeletonEnemy::BeginPlay()
{
	Super::BeginPlay();
	HomeLocation = GetActorLocation();
	WanderDestination = HomeLocation;
	WanderTimer = FMath::FRandRange(1.f, 5.f);
	if (bIsBoss)
	{
		DisplayName = NSLOCTEXT("Soulslike", "Boss", "The Hollow Laird");
	}
	OnRep_Appearance();
}

void ASkeletonEnemy::OnRep_Appearance()
{
	const FSoulslikeAssets& Assets = FSoulslikeAssets::Get();
	if (bIsBoss && Assets.Boss.IsSet())
	{
		ApplyVisual(Assets.Boss);
	}
	else if (Assets.Skeletons.Num() > 0)
	{
		ApplyVisual(Assets.Skeletons[FMath::Abs(VisualIndex) % Assets.Skeletons.Num()]);
	}
	else
	{
		ApplyVisual(FSoulslikeVisual());
	}
}

void ASkeletonEnemy::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!HasAuthority() || !IsAlive())
	{
		return;
	}
	UpdatePerception(DeltaSeconds);
	UpdateBehaviour(DeltaSeconds);
}

bool ASkeletonEnemy::HasLineOfSight(const AActor* Other) const
{
	const FVector Eye = GetActorLocation() + FVector(0.f, 0.f, 60.f * GetActorScale3D().Z);
	const FVector Point = Other->GetActorLocation() + FVector(0.f, 0.f, 40.f);
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(SkeletonSight), false, this);
	if (!GetWorld()->LineTraceSingleByChannel(Hit, Eye, Point, ECC_Visibility, Params))
	{
		return true;
	}
	return Hit.GetActor() == Other;
}

ASoulslikeCombatant* ASkeletonEnemy::SenseBestTarget(float& OutGain) const
{
	OutGain = 0.f;
	ASoulslikeCombatant* Best = nullptr;
	const FVector Forward = GetActorForwardVector().GetSafeNormal2D();
	const float CosHalfAngle = FMath::Cos(FMath::DegreesToRadians(SightHalfAngle));

	for (TActorIterator<ASoulslikeCombatant> It(GetWorld()); It; ++It)
	{
		ASoulslikeCombatant* Other = *It;
		if (Other == this || Other->GetTeam() == GetTeam() || !Other->IsAlive() || Other->IsConcealed())
		{
			continue;
		}

		const FVector ToOther = Other->GetActorLocation() - GetActorLocation();
		const float Distance = ToOther.Size();
		float Gain = 0.f;

		const bool bInCone = FVector::DotProduct(Forward, ToOther.GetSafeNormal2D()) >= CosHalfAngle;
		if (Distance < SightRange && bInCone && HasLineOfSight(Other))
		{
			Gain = 0.6f + 2.4f * (1.f - Distance / SightRange);
		}

		const bool bNoisy = Other->GetVelocity().Size2D() > 380.f || Other->Combat->State == ECombatState::Attacking;
		if (Distance < HearingRange && bNoisy)
		{
			Gain = FMath::Max(Gain, 1.5f);
		}
		if (Distance < 120.f)
		{
			Gain = FMath::Max(Gain, 4.f);
		}

		if (Gain > OutGain)
		{
			OutGain = Gain;
			Best = Other;
		}
	}
	return Best;
}

void ASkeletonEnemy::UpdatePerception(float DeltaSeconds)
{
	PerceptionTimer -= DeltaSeconds;
	if (PerceptionTimer > 0.f)
	{
		return;
	}
	const float Step = 0.2f;
	PerceptionTimer = Step;

	float Gain = 0.f;
	ASoulslikeCombatant* Sensed = SenseBestTarget(Gain);
	if (Sensed)
	{
		Suspicion = FMath::Min(1.f, Suspicion + Gain * Step);
		LastKnownTargetLocation = Sensed->GetActorLocation();
	}
	else if (Awareness != EAwareness::Alerted)
	{
		Suspicion = FMath::Max(0.f, Suspicion - 0.25f * Step);
	}

	if (Awareness == EAwareness::Alerted)
	{
		const ASoulslikeCombatant* Current = Target.Get();
		const bool bTargetGone = !Current || !Current->IsAlive();
		AlertedLostSightTime = (Sensed && Sensed == Current) ? 0.f : AlertedLostSightTime + Step;
		if (Sensed && bTargetGone)
		{
			Target = Sensed;
			AlertedLostSightTime = 0.f;
		}
		else if (bTargetGone || AlertedLostSightTime > 8.f)
		{
			Target = nullptr;
			Suspicion = 0.6f;
			SetAwareness(EAwareness::Suspicious);
		}
		return;
	}

	if (Suspicion >= 1.f && Sensed)
	{
		Alert(Sensed);
	}
	else
	{
		SetAwareness(Suspicion >= SuspicionThreshold ? EAwareness::Suspicious : EAwareness::Unaware);
	}
}

void ASkeletonEnemy::Alert(AActor* NewTarget)
{
	ASoulslikeCombatant* Combatant = Cast<ASoulslikeCombatant>(NewTarget);
	if (!IsAlive() || !Combatant || Combatant->GetTeam() == GetTeam())
	{
		return;
	}

	const bool bWasAlerted = Awareness == EAwareness::Alerted;
	Target = Combatant;
	Suspicion = 1.f;
	LastKnownTargetLocation = Combatant->GetActorLocation();
	AlertedLostSightTime = 0.f;
	SetAwareness(EAwareness::Alerted);

	if (bWasAlerted)
	{
		return;
	}
	for (TActorIterator<ASkeletonEnemy> It(GetWorld()); It; ++It)
	{
		ASkeletonEnemy* Ally = *It;
		if (Ally != this && Ally->Awareness != EAwareness::Alerted && FVector::Dist(Ally->GetActorLocation(), GetActorLocation()) < AllyAlertRadius)
		{
			Ally->Alert(Combatant);
		}
	}
}

void ASkeletonEnemy::SetAwareness(EAwareness NewAwareness)
{
	if (Awareness == NewAwareness)
	{
		return;
	}
	if (NewAwareness == EAwareness::Alerted)
	{
		AttackCooldown = FMath::Max(AttackCooldown, 0.6f);
	}
	Awareness = NewAwareness;
}

void ASkeletonEnemy::MoveTowards(const FVector& Destination, float Scale)
{
	const FVector Direction = (Destination - GetActorLocation()).GetSafeNormal2D();
	if (!Direction.IsNearlyZero())
	{
		AddMovementInput(Direction, Scale);
	}
}

void ASkeletonEnemy::UpdateBehaviour(float DeltaSeconds)
{
	AttackCooldown -= DeltaSeconds;
	if (BlockTimer > 0.f)
	{
		BlockTimer -= DeltaSeconds;
		if (BlockTimer <= 0.f)
		{
			Combat->StopBlocking();
		}
	}

	const FVector Location = GetActorLocation();

	if (Awareness == EAwareness::Unaware)
	{
		WanderTimer -= DeltaSeconds;
		if (WanderTimer <= 0.f)
		{
			const FVector2D Offset = FMath::RandPointInCircle(WanderRadius);
			WanderDestination = HomeLocation + FVector(Offset.X, Offset.Y, 0.f);
			WanderTimer = FMath::FRandRange(4.f, 9.f);
		}
		if (FVector::Dist2D(Location, WanderDestination) > 80.f && Combat->CanAct())
		{
			MoveTowards(WanderDestination, 0.35f);
		}
		return;
	}

	if (Awareness == EAwareness::Suspicious)
	{
		if (Combat->CanAct())
		{
			FaceTowards(LastKnownTargetLocation, DeltaSeconds, 120.f);
			if (FVector::Dist2D(Location, LastKnownTargetLocation) > 150.f)
			{
				MoveTowards(LastKnownTargetLocation, 0.45f);
			}
		}
		return;
	}

	ASoulslikeCombatant* Current = Target.Get();
	if (!Current || !Current->IsAlive())
	{
		return;
	}

	if (FVector::Dist2D(HomeLocation, Current->GetActorLocation()) > LeashRadius)
	{
		Target = nullptr;
		Suspicion = 0.f;
		SetAwareness(EAwareness::Unaware);
		WanderDestination = HomeLocation;
		return;
	}

	const float Distance = FVector::Dist2D(Location, Current->GetActorLocation());
	if (!Combat->CanAct())
	{
		return;
	}

	FaceTowards(Current->GetActorLocation(), DeltaSeconds, 360.f);

	if (BlockChance > 0.f && Combat->State == ECombatState::Idle && BlockTimer <= 0.f && Distance < 320.f
		&& Current->Combat->State == ECombatState::Attacking && FMath::FRand() < BlockChance * DeltaSeconds * 6.f)
	{
		Combat->StartBlocking();
		BlockTimer = 0.9f;
		return;
	}

	if (Distance > AttackRange * GetActorScale3D().X)
	{
		MoveTowards(Current->GetActorLocation(), 1.f);
		return;
	}

	if (AttackCooldown <= 0.f)
	{
		if (Combat->State == ECombatState::Blocking)
		{
			BlockTimer = 0.f;
			Combat->StopBlocking();
		}
		if (FMath::FRand() < HeavyAttackChance)
		{
			Combat->TryHeavyAttack();
		}
		else
		{
			Combat->TryLightAttack();
		}
		AttackCooldown = bIsBoss ? FMath::FRandRange(0.9f, 1.8f) : FMath::FRandRange(1.2f, 2.4f);
	}
}

float ASkeletonEnemy::TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	const float Applied = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
	if (HasAuthority() && IsAlive())
	{
		Alert(DamageCauser);
	}
	return Applied;
}

void ASkeletonEnemy::OnDied()
{
	Target = nullptr;
	SetLifeSpan(10.f);
}
