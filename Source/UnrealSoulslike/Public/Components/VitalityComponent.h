#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "VitalityComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnHealthChanged, float, NewHealth, float, MaxHealth);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnDeath);

// Health, stamina and a limited count of heal charges (estus-style), replicated so every
// co-op player and spectator sees accurate vitals for every other character.
UCLASS(ClassGroup = (Soulslike), meta = (BlueprintSpawnableComponent))
class UNREALSOULSLIKE_API UVitalityComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVitalityComponent();

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Vitality")
	float MaxHealth = 100.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Vitality")
	float MaxStamina = 100.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Vitality")
	float StaminaRegenPerSecond = 18.f;

	// Seconds of no stamina spend before regen resumes, so a player can't chain-dodge forever.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Vitality")
	float StaminaRegenDelay = 0.75f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Vitality")
	int32 MaxHealCharges = 3;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Vitality")
	float HealAmount = 45.f;

	UPROPERTY(ReplicatedUsing = OnRep_Health, BlueprintReadOnly, Category = "Vitality")
	float Health = 100.f;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Vitality")
	float Stamina = 100.f;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Vitality")
	int32 HealCharges = 3;

	UPROPERTY(BlueprintAssignable)
	FOnHealthChanged OnHealthChanged;

	UPROPERTY(BlueprintAssignable)
	FOnDeath OnDeath;

	bool IsAlive() const { return Health > 0.f; }
	bool HasStamina(float Amount) const { return Stamina >= Amount; }
	bool HasHealCharge() const { return HealCharges > 0; }

	// Authority-only mutators; the owning character's combat component calls these
	// from server-validated RPCs, never from client-predicted input directly.
	void ApplyDamage(float Amount, AActor* Instigator);
	void SpendStamina(float Amount);
	bool ConsumeHealCharge();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION()
	void OnRep_Health(float OldHealth);

private:
	float TimeSinceLastStaminaSpend = 0.f;
};
