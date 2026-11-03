#include "Components/CombatComponent.h"
#include "Components/VitalityComponent.h"
#include "Net/UnrealNetwork.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Character.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/DamageType.h"
#include "TimerManager.h"

UCombatComponent::UCombatComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UCombatComponent::BeginPlay()
{
	Super::BeginPlay();
	Vitality = GetOwner() ? GetOwner()->FindComponentByClass<UVitalityComponent>() : nullptr;
}

void UCombatComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearTimers();
	Super::EndPlay(EndPlayReason);
}

void UCombatComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UCombatComponent, State);
	DOREPLIFETIME(UCombatComponent, CurrentAttack);
	DOREPLIFETIME(UCombatComponent, ActionSerial);
}

void UCombatComponent::TryLightAttack() { ServerAttack(EAttackType::Light); }
void UCombatComponent::TryHeavyAttack() { ServerAttack(EAttackType::Heavy); }
void UCombatComponent::TryDodge(const FVector& Direction) { ServerDodge(Direction); }
void UCombatComponent::StartBlocking() { bWantsToBlock = true; ServerSetBlocking(true); }
void UCombatComponent::StopBlocking() { bWantsToBlock = false; ServerSetBlocking(false); }
void UCombatComponent::TryParry() { ServerParry(); }
void UCombatComponent::TryHeal() { ServerHeal(); }

void UCombatComponent::ServerAttack_Implementation(EAttackType Attack)
{
	const float Cost = Attack == EAttackType::Heavy ? HeavyAttackStaminaCost : LightAttackStaminaCost;
	if (!CanAct() || !Vitality || !Vitality->HasStamina(Cost))
	{
		return;
	}

	Vitality->SpendStamina(Cost);
	CurrentAttack = Attack;
	BeginAction(ECombatState::Attacking, AttackDuration(Attack));
	const float Windup = Attack == EAttackType::Heavy ? HeavyAttackWindup : LightAttackWindup;
	GetWorld()->GetTimerManager().SetTimer(AttackTimerHandle, this, &UCombatComponent::PerformAttackSweep, Windup, false);
}

void UCombatComponent::ServerDodge_Implementation(FVector_NetQuantizeNormal Direction)
{
	if (!CanAct() || !Vitality || !Vitality->HasStamina(DodgeStaminaCost))
	{
		return;
	}

	Vitality->SpendStamina(DodgeStaminaCost);
	BeginAction(ECombatState::Dodging, DodgeDuration);
	bIsInvulnerable = true;
	GetWorld()->GetTimerManager().SetTimer(InvulnerabilityTimerHandle, this, &UCombatComponent::ClearInvulnerability, DodgeInvulnerabilityDuration, false);

	// The owning client launched itself already; a remote client's launch must also happen here
	// or the server corrects the roll away.
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (Character && !Character->IsLocallyControlled())
	{
		Character->SetActorRotation(FRotator(0.f, FVector(Direction).Rotation().Yaw, 0.f));
		Character->LaunchCharacter(FVector(Direction) * DodgeLaunchSpeed, true, false);
	}
}

void UCombatComponent::ServerSetBlocking_Implementation(bool bBlock)
{
	bWantsToBlock = bBlock;
	if (bBlock && State == ECombatState::Idle)
	{
		SetState(ECombatState::Blocking);
	}
	else if (!bBlock && State == ECombatState::Blocking)
	{
		SetState(ECombatState::Idle);
	}
}

void UCombatComponent::ServerParry_Implementation()
{
	if (!CanAct() || !Vitality || !Vitality->HasStamina(ParryStaminaCost))
	{
		return;
	}

	Vitality->SpendStamina(ParryStaminaCost);
	BeginAction(ECombatState::Parrying, ParryWindowDuration);
}

void UCombatComponent::ServerHeal_Implementation()
{
	if (!CanAct() || !Vitality || !Vitality->HasHealCharge())
	{
		return;
	}

	BeginAction(ECombatState::Healing, HealCastDuration + 0.1f);
	GetWorld()->GetTimerManager().SetTimer(HealTimerHandle, this, &UCombatComponent::FinishHeal, HealCastDuration, false);
}

void UCombatComponent::FinishHeal()
{
	if (State == ECombatState::Healing && Vitality)
	{
		Vitality->ConsumeHealCharge();
	}
}

void UCombatComponent::PerformAttackSweep()
{
	AActor* Owner = GetOwner();
	if (!Owner || State != ECombatState::Attacking)
	{
		return;
	}

	const FVector Centre = Owner->GetActorLocation() + Owner->GetActorForwardVector() * AttackReach * Owner->GetActorScale3D().X;
	const float Radius = AttackRadius * Owner->GetActorScale3D().X;

	TArray<FOverlapResult> Overlaps;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(SoulslikeAttack), false, Owner);
	GetWorld()->OverlapMultiByObjectType(Overlaps, Centre, FQuat::Identity, FCollisionObjectQueryParams(ECC_Pawn), FCollisionShape::MakeSphere(Radius), Params);

	const float Damage = (CurrentAttack == EAttackType::Heavy ? HeavyAttackDamage : LightAttackDamage) * DamageMultiplier;
	APawn* OwnerPawn = Cast<APawn>(Owner);
	AController* Instigator = OwnerPawn ? OwnerPawn->GetController() : nullptr;

	TSet<AActor*> AlreadyHit;
	for (const FOverlapResult& Overlap : Overlaps)
	{
		AActor* Target = Overlap.GetActor();
		if (!Target || AlreadyHit.Contains(Target))
		{
			continue;
		}
		AlreadyHit.Add(Target);

		const UCombatComponent* TargetCombat = Target->FindComponentByClass<UCombatComponent>();
		if (!TargetCombat || TargetCombat->Team == Team || TargetCombat->IsDead())
		{
			continue;
		}
		UGameplayStatics::ApplyDamage(Target, Damage, Instigator, Owner, UDamageType::StaticClass());
	}
}

float UCombatComponent::ResolveIncomingDamage(float RawDamage, AActor* Attacker)
{
	if (IsDead() || bIsInvulnerable || RawDamage <= 0.f)
	{
		return 0.f;
	}

	if (State == ECombatState::Parrying)
	{
		if (UCombatComponent* AttackerCombat = Attacker ? Attacker->FindComponentByClass<UCombatComponent>() : nullptr)
		{
			AttackerCombat->ApplyStagger(AttackerCombat->StaggerDuration);
		}
		return 0.f;
	}

	if (State == ECombatState::Blocking && Vitality)
	{
		const float Cost = RawDamage * BlockStaminaPerDamage;
		if (Vitality->HasStamina(Cost))
		{
			Vitality->SpendStamina(Cost);
			return RawDamage * BlockDamageMultiplier;
		}
		Vitality->SpendStamina(Vitality->Stamina);
		ApplyStagger(GuardBreakDuration);
		return RawDamage * 0.5f;
	}

	if (bCanBeHitStunned)
	{
		ApplyStagger(HitStunDuration);
	}
	return RawDamage;
}

void UCombatComponent::ApplyStagger(float Duration)
{
	if (IsDead())
	{
		return;
	}
	ClearTimers();
	bIsInvulnerable = false;
	BeginAction(ECombatState::Staggered, Duration);
}

void UCombatComponent::StartExecution()
{
	ClearTimers();
	bIsInvulnerable = true;
	CurrentAttack = EAttackType::Heavy;
	BeginAction(ECombatState::Attacking, ExecutionDuration);
	GetWorld()->GetTimerManager().SetTimer(InvulnerabilityTimerHandle, this, &UCombatComponent::ClearInvulnerability, ExecutionDuration, false);
}

void UCombatComponent::SetDead()
{
	ClearTimers();
	bIsInvulnerable = false;
	++ActionSerial;
	SetState(ECombatState::Dead);
}

void UCombatComponent::ResetCombat()
{
	ClearTimers();
	bIsInvulnerable = false;
	bWantsToBlock = false;
	++ActionSerial;
	SetState(ECombatState::Idle);
}

void UCombatComponent::BeginAction(ECombatState NewState, float Duration)
{
	++ActionSerial;
	SetState(NewState);
	GetWorld()->GetTimerManager().SetTimer(StateTimerHandle, this, &UCombatComponent::ReturnToIdleOrBlocking, Duration, false);
}

void UCombatComponent::SetState(ECombatState NewState)
{
	if (State == NewState)
	{
		return;
	}
	State = NewState;
	OnCombatStateChanged.Broadcast(State);
}

void UCombatComponent::ReturnToIdleOrBlocking()
{
	if (IsDead())
	{
		return;
	}
	SetState(bWantsToBlock ? ECombatState::Blocking : ECombatState::Idle);
}

void UCombatComponent::ClearInvulnerability()
{
	bIsInvulnerable = false;
}

void UCombatComponent::ClearTimers()
{
	if (UWorld* World = GetWorld())
	{
		FTimerManager& Timers = World->GetTimerManager();
		Timers.ClearTimer(StateTimerHandle);
		Timers.ClearTimer(AttackTimerHandle);
		Timers.ClearTimer(InvulnerabilityTimerHandle);
		Timers.ClearTimer(HealTimerHandle);
	}
}

void UCombatComponent::OnRep_State(ECombatState OldState)
{
	OnCombatStateChanged.Broadcast(State);
}
