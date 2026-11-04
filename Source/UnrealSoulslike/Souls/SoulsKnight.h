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
class AGrapplePoint;
class ASoulsPickup;

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
	Grapple,       // flying along the grapple line
	Brace,         // weapon braced against a pouncing foe
	Pounce,        // leaping onto a foe
	Shocked,       // ambushed: frozen in surprise, open to a critical
	Impaled,       // caught on a braced blade
	Submerged,     // melted into shadow (ASoulsShadowBoss): untouchable until it rises
};

UENUM(BlueprintType)
enum class EGrappleMode : uint8
{
	None,
	ToPoint,      // swing up to a grapple point's ledge
	ToFoe,        // reel yourself in to a large foe, into an aerial strike
	ReelFoe,      // drag a smaller foe to your blade
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

	// The grapple's line, stretched from the off hand to the hook while it's out.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Knight")
	TObjectPtr<UStaticMeshComponent> GrappleLine;

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

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knight|Animation")
	TObjectPtr<UAnimMontage> ThrowMontage;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knight|Animation")
	TObjectPtr<UAnimMontage> SwimMontage;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knight|Animation")
	TObjectPtr<UAnimMontage> SwimIdleMontage;

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

	// ---- Grapple ----
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knight|Grapple")
	float GrappleRange = 3400.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knight|Grapple")
	float GrappleFoeRange = 1800.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knight|Grapple")
	float GrappleCost = 12.f;

	// ---- Pounce (enemies that leap) / brace ----
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knight|Pounce")
	float PounceDamage = 38.f;

	// ---- Swimming ----
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knight|Swim")
	float SwimStaminaPerSecond = 7.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knight|Swim")
	float DrownDamagePerSecond = 14.f;

	// ---- Fear (enemies) ----
	// 0..100. Shaken from 30 (slow, faltering swings), terrified from 60 (cowers or flees).
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Knight|Fear")
	float Fear = 0.f;

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

	// False while this knight can't be targeted (dead, or a boss melted into shadow).
	virtual bool CanBeLockedOn() const { return !IsDead(); }

	bool IsBusy() const { return Action != EKnightAction::None; }
	bool IsAttacking() const { return Action == EKnightAction::LightAttack || Action == EKnightAction::HeavyAttack; }
	bool IsOpenToCritical() const { return Action == EKnightAction::Parried || Action == EKnightAction::Shocked; }
	bool IsShocked() const { return Action == EKnightAction::Shocked; }
	bool IsBracing() const { return Action == EKnightAction::Brace; }
	bool IsPouncing() const { return Action == EKnightAction::Pounce; }
	bool IsSwimming() const;
	bool IsShaken() const { return Fear >= 30.f; }
	bool IsTerrified() const { return Fear >= 60.f; }
	EGrappleMode GetGrappleMode() const { return GrappleMode; }
	AActor* GetGrappleCandidate() const { return GrappleCandidate.Get(); }
	float GetLastAmbushTime() const { return LastAmbushTime; }
	float GetActionProgress() const { return ActionDuration > 0.f ? ActionElapsed / ActionDuration : 1.f; }
	float GetLastDamagedTime() const { return LastDamagedTime; }
	ASoulsKnight* GetLockTarget() const { return LockTarget.Get(); }
	ASiteOfGrace* GetNearbyGrace() const { return NearbyGrace.Get(); }
	void SetNearbyGrace(ASiteOfGrace* Grace);
	ASoulsPickup* GetNearbyPickup() const { return NearbyPickup.Get(); }
	void SetNearbyPickup(ASoulsPickup* Pickup);

	// Commands (player input and the enemy AI both drive the knight through these).
	virtual void DoMove(float Right, float Forward) override;
	virtual void DoLook(float Yaw, float Pitch) override;
	virtual void DoComboAttackStart() override;
	virtual void DoComboAttackEnd() override {}
	virtual void DoChargedAttackStart() override;
	virtual void DoChargedAttackEnd() override {}
	void DodgePressed();
	void DodgeReleased();
	void SprintPressed();
	void JumpPressed();
	void JumpReleased();
	void DodgeToward(const FVector& Direction);
	void BlockPressed();
	void BlockReleased();
	void ParryPressed();
	void HealPressed();
	void InteractPressed();
	void ToggleLockOn();
	void GrapplePressed();

	// Fear and surprise (enemies).
	void AddFear(float Amount);
	void Shock(float Duration);
	bool WasKilledByAmbush() const { return bKilledByAmbush; }

	// Leaping attack (enemies): crouch, then launch onto Target.
	void BeginPounce(ASoulsKnight* Target);
	// Pounced on without a braced blade: knocked flat.
	void GetPinned(ASoulsKnight* Pouncer);
	// Pounced on with a braced blade: the pouncer is run through and rolls off dead.
	void ImpalePouncer(ASoulsKnight* Pouncer);

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

	// The Shade drives the shared kit (actions, sweeps, lock-on, stamina) from SoulsShadowBoss.cpp.
	friend class ASoulsShadowBoss;

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

	// Grapple / brace / pounce / swim (SoulsKnightAbilities.cpp)
	AActor* FindGrappleTarget() const;
	void StartGrapple(AActor* Target);
	void LaunchGrapple();
	void TickGrapple(float DeltaSeconds);
	void EndGrapple(bool bArrived);
	void UpdateGrappleLine();
	ASoulsKnight* FindIncomingPouncer() const;
	void StartBrace();
	void LaunchPounce();
	void TickPounce(float DeltaSeconds);
	void TickSwimming(float DeltaSeconds);
	void TickFear(float DeltaSeconds);

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

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> GrappleAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> SprintAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> KnightJumpAction;

	TWeakObjectPtr<ASoulsKnight> LockTarget;
	TWeakObjectPtr<ASoulsKnight> CriticalVictim;
	TWeakObjectPtr<AActor> TrackTarget;
	TWeakObjectPtr<ASiteOfGrace> NearbyGrace;
	TWeakObjectPtr<ASoulsPickup> NearbyPickup;
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
	bool bSprintToggled = false;
	float DodgePressTime = 0.f;
	float SprintPressTime = 0.f;
	float BlockPressTime = -100.f;
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
	FTimerHandle GrappleTimer;
	FTimerHandle PounceTimer;

	EGrappleMode GrappleMode = EGrappleMode::None;
	TWeakObjectPtr<AActor> GrappleTarget;
	TWeakObjectPtr<AActor> GrappleCandidate;
	FVector GrappleStart = FVector::ZeroVector;
	FVector GrappleEnd = FVector::ZeroVector;
	FVector GrappleHook = FVector::ZeroVector;
	float GrappleT = 0.f;
	float GrappleDuration = 0.f;
	bool bGrappleFlying = false;
	float GrappleCandidateCheck = 0.f;

	TWeakObjectPtr<ASoulsKnight> PounceTarget;
	TWeakObjectPtr<ASoulsKnight> Impaler;
	bool bPounceAirborne = false;
	FVector PounceVelocity = FVector::ZeroVector;
	bool bWasSwimming = false;
	bool bKilledByAmbush = false;
	float LastFearTime = -100.f;
	float LastAmbushTime = -100.f;
};
