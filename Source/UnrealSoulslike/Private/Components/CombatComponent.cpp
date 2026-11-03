#include "Components/CombatComponent.h"
#include "Components/VitalityComponent.h"
#include "Net/UnrealNetwork.h"
#include "GameFramework/Actor.h"
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
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(StateTimerHandle);
		World->GetTimerManager().ClearTimer(InvulnerabilityTimerHandle);
		World->GetTimerManager().ClearTimer(HealTimerHandle);
	}
	Super::EndPlay(EndPlayReason);
}

void UCombatComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UCombatComponent, State);
}

void UCombatComponent::TryLightAttack() { ServerLightAttack(); }
void UCombatComponent::TryHeavyAttack() { ServerHeavyAttack(); }
void UCombatComponent::TryDodge(FVector2D MoveDirection) { ServerDodge(MoveDirection); }
void UCombatComponent::StartBlocking() { bWantsToBlock = true; ServerStartBlocking(); }
void UCombatComponent::StopBlocking() { bWantsToBlock = false; ServerStopBlocking(); }
void UCombatComponent::TryParry() { ServerParry(); }
void UCombatComponent::TryHeal() { ServerHeal(); }

void UCombatComponent::ServerLightAttack_Implementation()
{
	if (!CanAct() || !Vitality || !Vitality->HasStamina(LightAttackStaminaCost))
	{
		return;
	}

	Vitality->SpendStamina(LightAttackStaminaCost);
	SetState(ECombatState::Attacking);
	MulticastAttackPerformed(EAttackType::Light);
	GetWorld()->GetTimerManager().SetTimer(StateTimerHandle, this, &UCombatComponent::ReturnToIdleOrBlocking, LightAttackDuration, false);
}

void UCombatComponent::ServerHeavyAttack_Implementation()
{
	if (!CanAct() || !Vitality || !Vitality->HasStamina(HeavyAttackStaminaCost))
	{
		return;
	}

	Vitality->SpendStamina(HeavyAttackStaminaCost);
	SetState(ECombatState::Attacking);
	MulticastAttackPerformed(EAttackType::Heavy);
	GetWorld()->GetTimerManager().SetTimer(StateTimerHandle, this, &UCombatComponent::ReturnToIdleOrBlocking, HeavyAttackDuration, false);
}

void UCombatComponent::ServerDodge_Implementation(FVector2D MoveDirection)
{
	if (!CanAct() || !Vitality || !Vitality->HasStamina(DodgeStaminaCost))
	{
		return;
	}

	Vitality->SpendStamina(DodgeStaminaCost);
	SetState(ECombatState::Dodging);
	bIsInvulnerable = true;
	GetWorld()->GetTimerManager().SetTimer(InvulnerabilityTimerHandle, this, &UCombatComponent::ClearInvulnerability, DodgeInvulnerabilityDuration, false);
	GetWorld()->GetTimerManager().SetTimer(StateTimerHandle, this, &UCombatComponent::ReturnToIdleOrBlocking, DodgeDuration, false);
}

void UCombatComponent::ServerStartBlocking_Implementation()
{
	if (!CanAct())
	{
		return;
	}

	SetState(ECombatState::Blocking);
}

void UCombatComponent::ServerStopBlocking_Implementation()
{
	if (State == ECombatState::Blocking)
	{
		SetState(ECombatState::Idle);
	}
}

void UCombatComponent::ServerParry_Implementation()
{
	if (State != ECombatState::Blocking || !Vitality || !Vitality->HasStamina(ParryStaminaCost))
	{
		return;
	}

	Vitality->SpendStamina(ParryStaminaCost);
	SetState(ECombatState::Parrying);
	GetWorld()->GetTimerManager().SetTimer(StateTimerHandle, this, &UCombatComponent::ReturnToIdleOrBlocking, ParryWindowDuration, false);
}

void UCombatComponent::ServerHeal_Implementation()
{
	if (!CanAct() || !Vitality || !Vitality->HasHealCharge())
	{
		return;
	}

	SetState(ECombatState::Healing);
	GetWorld()->GetTimerManager().SetTimer(HealTimerHandle, [this]()
	{
		if (Vitality)
		{
			Vitality->ConsumeHealCharge();
		}
		ReturnToIdleOrBlocking();
	}, HealCastDuration, false);
}

void UCombatComponent::MulticastAttackPerformed_Implementation(EAttackType Attack)
{
	OnAttackPerformed.Broadcast(Attack);
}

float UCombatComponent::ResolveIncomingDamage(float RawDamage, AActor* Attacker)
{
	if (bIsInvulnerable)
	{
		return 0.f;
	}

	if (State == ECombatState::Parrying)
	{
		if (UCombatComponent* AttackerCombat = Attacker ? Attacker->FindComponentByClass<UCombatComponent>() : nullptr)
		{
			AttackerCombat->SetState(ECombatState::Staggered);
			AttackerCombat->GetWorld()->GetTimerManager().SetTimer(AttackerCombat->StateTimerHandle, AttackerCombat, &UCombatComponent::ReturnToIdleOrBlocking, AttackerCombat->StaggerDuration, false);
		}
		return 0.f;
	}

	if (State == ECombatState::Blocking)
	{
		return RawDamage * BlockDamageMultiplier;
	}

	return RawDamage;
}

void UCombatComponent::InterruptOnHit()
{
	if (State == ECombatState::Attacking || State == ECombatState::Healing)
	{
		GetWorld()->GetTimerManager().ClearTimer(StateTimerHandle);
		GetWorld()->GetTimerManager().ClearTimer(HealTimerHandle);
		SetState(bWantsToBlock ? ECombatState::Blocking : ECombatState::Idle);
	}
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
	SetState(bWantsToBlock ? ECombatState::Blocking : ECombatState::Idle);
}

void UCombatComponent::ClearInvulnerability()
{
	bIsInvulnerable = false;
}

void UCombatComponent::OnRep_State(ECombatState OldState)
{
	OnCombatStateChanged.Broadcast(State);
}
