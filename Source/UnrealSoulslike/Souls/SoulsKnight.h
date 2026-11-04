#pragma once

#include "CoreMinimal.h"
#include "CombatCharacter.h"
#include "SoulsKnight.generated.h"

class UAnimMontage;
class UMaterialInterface;
class USoundBase;
class UStaticMeshComponent;
class UInputAction;
class UInputMappingContext;
class ASiteOfGrace;

UENUM(BlueprintType)
enum class EKnightAction : uint8
{
	None,
	LightAttack,
	HeavyAttack,
	Roll,
	Heal,
	Parry,
	Critical,      // performing a riposte or backstab
	Staggered,     // flinch, guard break, poise break
	Parried,       // parried or stance broken: open to a riposte
	Crippled,      // on the receiving end of a critical
};

// A sword-and-shield knight in the Elden Ring mould, used for the player and (through ASoulsEnemy)
// for every enemy and the boss, so both sides fight by the same rules:
// - light combo (3 swings) / heavy, both on a shared stamina bar
// - dodge roll with i-frames, raised-shield block, timed shield parry
// - poise (enough hits interrupt an attack) and stance (enough damage breaks it: open to a riposte)
// - ripostes on parried/stance-broken foes, backstabs from behind
// - flasks, hitstop and camera kick on impact
// The player additionally gets lock-on, Sites of Grace and the HUD.
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

	// 0 = players, 1 = the island's enemies. Knights never hurt their own team.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knight")
	uint8 Team = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knight")
	FText DisplayName;

	// Per-slot overrides for the body mesh (faction armour); empty entries keep the mesh's own.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knight")
	TArray<TObjectPtr<UMaterialInterface>> BodyMaterials;

	void ApplyBodyMaterials(const TArray<TObjectPtr<UMaterialInterface>>& Materials);

	// ---- Animation ----
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

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knight|Animation")
	TObjectPtr<UAnimMontage> ParryMontage;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knight|Animation")
	TObjectPtr<UAnimMontage> StaggerMontage;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knight|Animation")
	TObjectPtr<UAnimMontage> GuardBreakMontage;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knight|Animation")
	TObjectPtr<UAnimMontage> CriticalMontage;

	// Play-rate of every attack animation (enemies swing slower so their attacks read).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knight|Animation")
	float AttackSpeed = 0.85f;

	// Enemies only: show the floating health bar over the head once hurt (the boss uses the HUD).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knight")
	bool bFloatingLifeBar = false;

	// Can be backstabbed (bosses can't).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knight")
	bool bCanBeBackstabbed = true;

	// ---- Combat numbers ----
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knight|Combat")
	float MaxHealth = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knight|Combat")
	float LightDamage = 14.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knight|Combat")
	float HeavyDamage = 26.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knight|Combat")
	float CriticalDamage = 60.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knight|Combat")
	float SwordReach = 190.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knight|Combat")
	float SwingHitFraction = 0.42f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knight|Combat")
	float ComboChainFraction = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knight|Combat")
	float BlockDamageFraction = 0.15f;

	// Hits that land within this many seconds of pressing parry are deflected.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knight|Combat")
	float ParryWindowStart = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knight|Combat")
	float ParryWindowEnd = 0.32f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knight|Combat")
	float MaxPoise = 30.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knight|Combat")
	float MaxStance = 90.f;

	// ---- Movement / roll ----
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knight|Roll")
	float RunSpeed = 520.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knight|Roll")
	float RollSpeed = 900.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knight|Roll")
	float RollDuration = 0.7f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knight|Roll")
	float RollInvulnerableStart = 0.04f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knight|Roll")
	float RollInvulnerableEnd = 0.42f;

	// ---- Stamina ----
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knight|Stamina")
	float MaxStamina = 100.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Knight|Stamina")
	float Stamina = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knight|Stamina")
	float StaminaRegenPerSecond = 45.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knight|Stamina")
	float StaminaRegenDelay = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knight|Stamina")
	float LightAttackCost = 14.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knight|Stamina")
	float HeavyAttackCost = 28.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knight|Stamina")
	float RollCost = 18.f;

	// ---- Flasks ----
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knight|Heal")
	int32 MaxFlasks = 4;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Knight|Heal")
	int32 Flasks = 4;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knight|Heal")
	float FlaskHealing = 55.f;

	// Runes this foe is worth when felled.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knight|Rewards")
	int32 RuneReward = 0;

	// ---- Sound (filled by tools/build_souls_character.py) ----
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knight|Sound")
	TArray<TObjectPtr<USoundBase>> SwingSounds;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knight|Sound")
	TArray<TObjectPtr<USoundBase>> HitSounds;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knight|Sound")
	TArray<TObjectPtr<USoundBase>> BlockSounds;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knight|Sound")
	TArray<TObjectPtr<USoundBase>> HurtSounds;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knight|Sound")
	TArray<TObjectPtr<USoundBase>> FootstepSounds;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knight|Sound")
	TObjectPtr<USoundBase> ParrySound;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knight|Sound")
	TObjectPtr<USoundBase> CriticalSound;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knight|Sound")
	TObjectPtr<USoundBase> RollSound;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knight|Sound")
	TObjectPtr<USoundBase> DrinkSound;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knight|Sound")
	TObjectPtr<USoundBase> DeathSound;

	// ---- Queries ----
	UFUNCTION(BlueprintPure, Category = "Knight")
	EKnightAction GetAction() const { return Action; }

	UFUNCTION(BlueprintPure, Category = "Knight")
	bool IsBlocking() const { return bBlocking; }

	UFUNCTION(BlueprintPure, Category = "Knight")
	bool IsDead() const { return CurrentHP <= 0.f; }

	UFUNCTION(BlueprintPure, Category = "Knight")
	float GetHealth() const { return CurrentHP; }

	UFUNCTION(BlueprintPure, Category = "Knight")
	float GetHealthFraction() const { return MaxHP > 0.f ? FMath::Max(CurrentHP, 0.f) / MaxHP : 0.f; }

	UFUNCTION(BlueprintPure, Category = "Knight")
	float GetStaminaFraction() const { return MaxStamina > 0.f ? FMath::Max(Stamina, 0.f) / MaxStamina : 0.f; }

	UFUNCTION(BlueprintPure, Category = "Knight")
	float GetStanceFraction() const { return MaxStance > 0.f ? StanceDamage / MaxStance : 0.f; }

	bool IsBusy() const { return Action != EKnightAction::None; }
	bool IsAttacking() const { return Action == EKnightAction::LightAttack || Action == EKnightAction::HeavyAttack; }
	bool IsOpenToCritical() const { return Action == EKnightAction::Parried; }
	float GetActionProgress() const { return ActionDuration > 0.f ? ActionElapsed / ActionDuration : 1.f; }
	float GetLastDamagedTime() const { return LastDamagedTime; }
	ASoulsKnight* GetLockTarget() const { return LockTarget.Get(); }
	ASiteOfGrace* GetNearbyGrace() const { return NearbyGrace.Get(); }
	void SetNearbyGrace(ASiteOfGrace* Grace);

	// Commands (player input and the enemy AI both drive the knight through these).
	virtual void DoMove(float Right, float Forward) override;
	virtual void DoLook(float Yaw, float Pitch) override;
	virtual void DoComboAttackStart() override;
	virtual void DoComboAttackEnd() override {}
	virtual void DoChargedAttackStart() override;
	virtual void DoChargedAttackEnd() override {}
	void DodgePressed();
	void DodgeReleased();
	void DodgeToward(const FVector& Direction);
	void BlockPressed();
	void BlockReleased();
	void ParryPressed();
	void HealPressed();
	void InteractPressed();
	void ToggleLockOn();

	// Rested at a Site of Grace: full health, stamina and flasks.
	void RestoreAll();

	// AI: the enemy this knight turns to face while winding up.
	void SetTrackTarget(AActor* Target) { TrackTarget = Target; }

	virtual void Tick(float DeltaSeconds) override;
	virtual float TakeDamage(float Damage, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;
	virtual void ApplyDamage(float Damage, AActor* DamageCauser, const FVector& DamageLocation, const FVector& DamageImpulse) override;
	virtual void HandleDeath() override;
	virtual void FellOutOfWorld(const UDamageType& DmgType) override;
	virtual void NotifyDanger(const FVector& DangerLocation, AActor* DangerSource) override;

	enum class EMeleeResult : uint8 { Ignored, Dodged, Hit, Blocked, Parried };

	// A sword (or critical) strike from another knight lands on this one.
	EMeleeResult ReceiveMelee(ASoulsKnight* Attacker, float Damage, float PoiseDamage, float StanceHit, bool bHeavy, const FVector& ImpactPoint);

	// Hitstop: freeze this knight's animation and movement for a beat.
	void Hitstop(float Seconds);

protected:
	virtual void BeginPlay() override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void NotifyControllerChanged() override;
	virtual bool CanJumpInternal_Implementation() const override;

private:
	// Verification run (-KnightDemo): performs every move and screenshots it.
	void RunVerificationDemo();
	void Shot(const TCHAR* Name);

	void EnsureKnightInput();
	bool SpendStamina(float Cost);
	void StartSwing(int32 Index);
	void StartHeavy();
	void PlayAction(EKnightAction NewAction, UAnimMontage* Montage, float Duration, float StartFraction = 0.f);
	void EndAction();
	void SwingHit();
	void FinishHeal();
	void SetBlocking(bool bNewBlocking);
	void FaceTowards(const FVector& Location);
	FVector DesiredDirection() const;
	FVector AttackDirection() const;
	void PlaySound(USoundBase* Sound, float Volume = 1.f) const;
	void PlayRandomSound(const TArray<TObjectPtr<USoundBase>>& Sounds, float Volume = 1.f) const;
	void KickCamera(float Strength);

	// Melee
	void MeleeSweep(float Damage, float PoiseDamage, float StanceHit, bool bHeavy);
	bool TryCritical();
	void LandCritical();
	void ReceiveCritical(ASoulsKnight* Attacker, float Duration);
	void GetParried(ASoulsKnight* Parrier);
	void TakePoiseAndStance(float PoiseDamage, float StanceHit);
	bool IsInParryWindow() const;
	void LoseHealth(float Amount, AActor* Source);
	void Interrupt();

	// Lock-on
	ASoulsKnight* FindLockTarget() const;
	void UpdateLockOn(float DeltaSeconds);

	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> KnightMappingContext;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> DodgeAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> BlockAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> HealAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> ParryAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> LockOnAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> InteractAction;

	TWeakObjectPtr<ASoulsKnight> LockTarget;
	TWeakObjectPtr<ASoulsKnight> CriticalVictim;
	TWeakObjectPtr<AActor> TrackTarget;
	TWeakObjectPtr<ASiteOfGrace> NearbyGrace;
	TSet<TWeakObjectPtr<AActor>> HitThisSwing;

	EKnightAction Action = EKnightAction::None;
	int32 SwingIndex = 0;
	bool bComboQueued = false;
	bool bBlocking = false;
	bool bBlockHeld = false;
	bool bVerificationDemo = false;
	bool bDemoCameraSet = false;
	bool bHeavySwing = false;
	bool bDodgeHeld = false;
	bool bSprinting = false;
	float DodgePressTime = 0.f;
	float ActionElapsed = 0.f;
	float ActionDuration = 0.f;
	float StaminaIdleTime = 0.f;
	float Poise = 30.f;
	float StanceDamage = 0.f;
	float LastDamagedTime = -100.f;
	float StepDistance = 0.f;
	float CameraKick = 0.f;
	FVector RollDirection = FVector::ForwardVector;
	FTimerHandle ActionTimer;
	FTimerHandle BlockHoldTimer;
	FTimerHandle HitTimer;
	FTimerHandle HealTimer;
	FTimerHandle HitstopTimer;
	FTimerHandle WhooshTimer;
};
