#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SoulslikeTypes.h"
#include "CombatComponent.generated.h"

class UVitalityComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCombatStateChanged, ECombatState, NewState);

// Server-authoritative combat state machine shared by players and skeletons: dodge (i-frames),
// block (stamina-gated damage reduction, guard break), parry (short window that staggers the
// attacker), light/heavy attacks (wind-up, then one hit sweep), heal, hit-stun and death.
// State, the current attack and an action serial replicate so every co-op client animates it.
UCLASS(ClassGroup = (Soulslike), meta = (BlueprintSpawnableComponent))
class UNREALSOULSLIKE_API UCombatComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCombatComponent();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	uint8 Team = SoulslikeTeam::Players;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|Cost")
	float LightAttackStaminaCost = 12.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|Cost")
	float HeavyAttackStaminaCost = 24.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|Cost")
	float DodgeStaminaCost = 20.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|Cost")
	float ParryStaminaCost = 6.f;

	// Stamina drained per point of damage absorbed by a block; running out breaks the guard.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|Cost")
	float BlockStaminaPerDamage = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|Timing")
	float LightAttackWindup = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|Timing")
	float LightAttackDuration = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|Timing")
	float HeavyAttackWindup = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|Timing")
	float HeavyAttackDuration = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|Timing")
	float DodgeDuration = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|Timing")
	float DodgeInvulnerabilityDuration = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|Timing")
	float ParryWindowDuration = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|Timing")
	float HealCastDuration = 1.6f;

	// How long a successful parry leaves the attacker open.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|Timing")
	float StaggerDuration = 1.2f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|Timing")
	float HitStunDuration = 0.4f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|Timing")
	float GuardBreakDuration = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|Timing")
	float ExecutionDuration = 1.1f;

	// False for the boss: ordinary hits don't interrupt it, only parries and guard breaks do.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	bool bCanBeHitStunned = true;

	// Fraction of damage that still gets through a held block.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|Damage")
	float BlockDamageMultiplier = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|Damage")
	float LightAttackDamage = 18.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|Damage")
	float HeavyAttackDamage = 36.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|Damage")
	float DamageMultiplier = 1.f;

	// The hit sweep is a sphere this far in front of the owner.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|Damage")
	float AttackReach = 150.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|Damage")
	float AttackRadius = 110.f;

	UPROPERTY(ReplicatedUsing = OnRep_State, BlueprintReadOnly, Category = "Combat")
	ECombatState State = ECombatState::Idle;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Combat")
	EAttackType CurrentAttack = EAttackType::Light;

	// Bumped every time an action starts, so clients restart an animation even when the
	// state value itself didn't change between two replication updates (attack -> attack).
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Combat")
	uint8 ActionSerial = 0;

	UPROPERTY(BlueprintAssignable)
	FOnCombatStateChanged OnCombatStateChanged;

	UFUNCTION(BlueprintCallable, Category = "Combat")
	void TryLightAttack();

	UFUNCTION(BlueprintCallable, Category = "Combat")
	void TryHeavyAttack();

	UFUNCTION(BlueprintCallable, Category = "Combat")
	void TryDodge();

	UFUNCTION(BlueprintCallable, Category = "Combat")
	void StartBlocking();

	UFUNCTION(BlueprintCallable, Category = "Combat")
	void StopBlocking();

	UFUNCTION(BlueprintCallable, Category = "Combat")
	void TryParry();

	UFUNCTION(BlueprintCallable, Category = "Combat")
	void TryHeal();

	UFUNCTION(BlueprintPure, Category = "Combat")
	bool IsDead() const { return State == ECombatState::Dead; }

	UFUNCTION(BlueprintPure, Category = "Combat")
	bool IsInvulnerable() const { return bIsInvulnerable; }

	UFUNCTION(BlueprintPure, Category = "Combat")
	bool CanAct() const { return State == ECombatState::Idle || State == ECombatState::Blocking; }

	float AttackDuration(EAttackType Attack) const { return Attack == EAttackType::Heavy ? HeavyAttackDuration : LightAttackDuration; }

	// Server: the defender's TakeDamage asks how much of a hit lands. Handles i-frames, parry
	// (zero damage, attacker staggered), block (reduced, costs stamina, may guard-break) and
	// hit-stun on a clean hit.
	float ResolveIncomingDamage(float RawDamage, AActor* Attacker);

	// Server: puts this combatant into Staggered for Duration, cancelling whatever it was doing.
	void ApplyStagger(float Duration);

	// Server: play the stealth-execution finisher (heavy swing, no hit sweep: the kill is applied
	// by the executor directly).
	void StartExecution();

	// Server: final state; clears every pending action.
	void SetDead();

	// Server: back to a fresh Idle (respawn / reuse).
	void ResetCombat();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION(Server, Reliable)
	void ServerAttack(EAttackType Attack);

	UFUNCTION(Server, Reliable)
	void ServerDodge();

	UFUNCTION(Server, Reliable)
	void ServerSetBlocking(bool bBlock);

	UFUNCTION(Server, Reliable)
	void ServerParry();

	UFUNCTION(Server, Reliable)
	void ServerHeal();

	UFUNCTION()
	void OnRep_State(ECombatState OldState);

	void BeginAction(ECombatState NewState, float Duration);
	void SetState(ECombatState NewState);
	void ReturnToIdleOrBlocking();
	void PerformAttackSweep();
	void FinishHeal();
	void ClearInvulnerability();
	void ClearTimers();

private:
	UPROPERTY()
	TObjectPtr<UVitalityComponent> Vitality;

	bool bIsInvulnerable = false;
	bool bWantsToBlock = false;

	FTimerHandle StateTimerHandle;
	FTimerHandle AttackTimerHandle;
	FTimerHandle InvulnerabilityTimerHandle;
	FTimerHandle HealTimerHandle;
};
