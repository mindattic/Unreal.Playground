#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SoulslikeTypes.h"
#include "CombatComponent.generated.h"

class UVitalityComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCombatStateChanged, ECombatState, NewState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnAttackPerformed, EAttackType, Attack);

// Drives the co-op combat loop: dodge roll (with an i-frame window), block (damage reduction
// while held), parry (a short all-or-nothing window), light/heavy attacks, and estus-style
// heal. State lives authoritatively on the server and replicates out for every co-op client's
// animation/VFX hooks (bound in Blueprint via OnCombatStateChanged/OnAttackPerformed).
UCLASS(ClassGroup = (Soulslike), meta = (BlueprintSpawnableComponent))
class UNREALSOULSLIKE_API UCombatComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCombatComponent();

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Cost")
	float LightAttackStaminaCost = 12.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Cost")
	float HeavyAttackStaminaCost = 24.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Cost")
	float DodgeStaminaCost = 20.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Cost")
	float ParryStaminaCost = 6.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Timing")
	float LightAttackDuration = 0.55f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Timing")
	float HeavyAttackDuration = 0.95f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Timing")
	float DodgeDuration = 0.6f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Timing")
	float DodgeInvulnerabilityDuration = 0.35f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Timing")
	float ParryWindowDuration = 0.3f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Timing")
	float HealCastDuration = 1.6f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Timing")
	float StaggerDuration = 1.2f;

	// Fraction of damage that still gets through while blocking (0.15 = 85% reduction).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Damage")
	float BlockDamageMultiplier = 0.15f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Damage")
	float LightAttackDamage = 18.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Damage")
	float HeavyAttackDamage = 36.f;

	UPROPERTY(ReplicatedUsing = OnRep_State, BlueprintReadOnly, Category = "Combat")
	ECombatState State = ECombatState::Idle;

	UPROPERTY(BlueprintAssignable)
	FOnCombatStateChanged OnCombatStateChanged;

	UPROPERTY(BlueprintAssignable)
	FOnAttackPerformed OnAttackPerformed;

	// Bound to input (X). Owning client call; validated and executed on the server.
	UFUNCTION(BlueprintCallable, Category = "Combat")
	void TryLightAttack();

	// Bound to input (Y).
	UFUNCTION(BlueprintCallable, Category = "Combat")
	void TryHeavyAttack();

	// Bound to the dodge input; MoveDirection is the character's current input-space move
	// vector (zero = backstep in place).
	UFUNCTION(BlueprintCallable, Category = "Combat")
	void TryDodge(FVector2D MoveDirection);

	UFUNCTION(BlueprintCallable, Category = "Combat")
	void StartBlocking();

	UFUNCTION(BlueprintCallable, Category = "Combat")
	void StopBlocking();

	// A tap of the parry input while holding block; opens a short all-or-nothing window.
	UFUNCTION(BlueprintCallable, Category = "Combat")
	void TryParry();

	// Bound to input (RB).
	UFUNCTION(BlueprintCallable, Category = "Combat")
	void TryHeal();

	UFUNCTION(BlueprintPure, Category = "Combat")
	bool IsBlocking() const { return State == ECombatState::Blocking; }

	UFUNCTION(BlueprintPure, Category = "Combat")
	bool IsParrying() const { return State == ECombatState::Parrying; }

	UFUNCTION(BlueprintPure, Category = "Combat")
	bool IsInvulnerable() const { return bIsInvulnerable; }

	UFUNCTION(BlueprintPure, Category = "Combat")
	bool CanAct() const { return State == ECombatState::Idle || State == ECombatState::Blocking; }

	// Called by the defender's TakeDamage path before applying damage; reduces it for a
	// block, zeroes it and staggers the attacker for a parry, leaves it untouched otherwise.
	float ResolveIncomingDamage(float RawDamage, AActor* Attacker);

	// Cancels healing/attacking when the owner takes a hit, so getting hit mid-heal or
	// mid-swing has a real cost instead of being free-cast.
	void InterruptOnHit();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION(Server, Reliable)
	void ServerLightAttack();

	UFUNCTION(Server, Reliable)
	void ServerHeavyAttack();

	UFUNCTION(Server, Reliable)
	void ServerDodge(FVector2D MoveDirection);

	UFUNCTION(Server, Reliable)
	void ServerStartBlocking();

	UFUNCTION(Server, Reliable)
	void ServerStopBlocking();

	UFUNCTION(Server, Reliable)
	void ServerParry();

	UFUNCTION(Server, Reliable)
	void ServerHeal();

	UFUNCTION(NetMulticast, Reliable)
	void MulticastAttackPerformed(EAttackType Attack);

	UFUNCTION()
	void OnRep_State(ECombatState OldState);

	void SetState(ECombatState NewState);
	void ReturnToIdleOrBlocking();

private:
	UPROPERTY()
	TObjectPtr<UVitalityComponent> Vitality;

	bool bIsInvulnerable = false;
	bool bWantsToBlock = false;

	FTimerHandle StateTimerHandle;
	FTimerHandle InvulnerabilityTimerHandle;
	FTimerHandle HealTimerHandle;

	UFUNCTION()
	void ClearInvulnerability();
};
