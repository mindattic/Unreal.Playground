#pragma once

#include "CoreMinimal.h"
#include "CombatCharacter.h"
#include "SoulsKnight.generated.h"

class UAnimMontage;
class UStaticMesh;
class UStaticMeshComponent;
class UInputAction;
class UInputMappingContext;

UENUM(BlueprintType)
enum class EKnightAction : uint8
{
	None,
	LightAttack,
	HeavyAttack,
	Roll,
	Heal,
	Staggered,
};

// The player's sword knight: Epic's combat character (locomotion, jump, hit reactions, death and
// respawn, life bar) plus a three-swing sword combo, a heavy swing, a dodge roll with i-frames,
// a raised-shield block, flask heals and a stamina pool that all of them draw on.
// BP_SoulsKnight (built by tools/build_souls_character.py) supplies the montages and meshes.
UCLASS()
class UNREALSOULSLIKE_API ASoulsKnight : public ACombatCharacter
{
	GENERATED_BODY()

public:
	ASoulsKnight();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Knight")
	TObjectPtr<UStaticMeshComponent> Sword;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Knight")
	TObjectPtr<UStaticMeshComponent> Shield;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knight|Animation")
	TArray<TObjectPtr<UAnimMontage>> SwordComboMontages;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knight|Animation")
	TObjectPtr<UAnimMontage> HeavyAttackMontage;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knight|Animation")
	TObjectPtr<UAnimMontage> RollMontage;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knight|Animation")
	TObjectPtr<UAnimMontage> BlockMontage;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knight|Animation")
	TObjectPtr<UAnimMontage> HealMontage;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knight|Animation")
	TObjectPtr<UAnimMontage> HitMontage;

	// Fraction of each swing's length at which its blade sweep lands.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knight|Combat")
	float SwingHitFraction = 0.42f;

	// Past this fraction of a swing a queued light attack chains into the next swing.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knight|Combat")
	float ComboChainFraction = 0.62f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knight|Combat")
	float LightDamage = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knight|Combat")
	float HeavyDamage = 2.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knight|Combat")
	float BlockDamageFraction = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knight|Roll")
	float RollSpeed = 900.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knight|Roll")
	float RollDuration = 0.7f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knight|Roll")
	float RollInvulnerableStart = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knight|Roll")
	float RollInvulnerableEnd = 0.45f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knight|Stamina")
	float MaxStamina = 100.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Knight|Stamina")
	float Stamina = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knight|Stamina")
	float StaminaRegenPerSecond = 30.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knight|Stamina")
	float StaminaRegenDelay = 0.8f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knight|Stamina")
	float LightAttackCost = 15.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knight|Stamina")
	float HeavyAttackCost = 30.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knight|Stamina")
	float RollCost = 25.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knight|Heal")
	int32 MaxFlasks = 3;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Knight|Heal")
	int32 Flasks = 3;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knight|Heal")
	float FlaskHealing = 2.f;

	UFUNCTION(BlueprintPure, Category = "Knight")
	EKnightAction GetAction() const { return Action; }

	UFUNCTION(BlueprintPure, Category = "Knight")
	bool IsBlocking() const { return bBlocking; }

	UFUNCTION(BlueprintPure, Category = "Knight")
	float GetHealthFraction() const { return MaxHP > 0.f ? CurrentHP / MaxHP : 0.f; }

	UFUNCTION(BlueprintPure, Category = "Knight")
	float GetStaminaFraction() const { return MaxStamina > 0.f ? Stamina / MaxStamina : 0.f; }

	virtual void Tick(float DeltaSeconds) override;
	virtual void DoMove(float Right, float Forward) override;
	virtual void DoComboAttackStart() override;
	virtual void DoComboAttackEnd() override {}
	virtual void DoChargedAttackStart() override;
	virtual void DoChargedAttackEnd() override {}
	virtual float TakeDamage(float Damage, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;
	virtual void HandleDeath() override;

protected:
	virtual void BeginPlay() override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void NotifyControllerChanged() override;
	virtual bool CanJumpInternal_Implementation() const override;

	void DodgePressed();
	void BlockPressed();
	void BlockReleased();
	void HealPressed();

private:
	void RunVerificationDemo();
	void Shot(const TCHAR* Name);
	void SpawnSparringEnemy();
	void FaceSparringEnemy();
	void EnsureKnightInput();
	bool SpendStamina(float Cost);
	bool IsBusy() const { return Action != EKnightAction::None; }
	void StartSwing(int32 Index);
	void StartHeavy();
	void PlayAction(EKnightAction NewAction, UAnimMontage* Montage, float Duration);
	void EndAction();
	void SwingHit();
	void FinishHeal();
	void SetBlocking(bool bNewBlocking);
	FVector DesiredDirection() const;

	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> KnightMappingContext;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> DodgeAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> BlockAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> HealAction;

	EKnightAction Action = EKnightAction::None;
	int32 SwingIndex = 0;
	bool bComboQueued = false;
	bool bBlocking = false;
	bool bBlockHeld = false;
	bool bVerificationDemo = false;
	bool bDemoCameraSet = false;

	UPROPERTY(Transient)
	TObjectPtr<APawn> SparringEnemy;
	float ActionElapsed = 0.f;
	float ActionDuration = 0.f;
	float StaminaIdleTime = 0.f;
	FVector RollDirection = FVector::ForwardVector;
	FTimerHandle ActionTimer;
	FTimerHandle BlockHoldTimer;
	FTimerHandle HitTimer;
	FTimerHandle HealTimer;
};
