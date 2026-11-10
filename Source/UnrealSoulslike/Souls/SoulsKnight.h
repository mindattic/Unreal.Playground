#pragma once

#include "CoreMinimal.h"
#include "CombatCharacter.h"
#include "ISoulsPlayableCharacter.h"
#include "SoulsKnight.generated.h"

class UAnimMontage;
class UMaterialInterface;
class USoundBase;
class UStaticMeshComponent;
class UInputAction;
class UShadowMergeComponent;
class USoulsWallRunComponent;
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
	StanceShift,   // diving to the ground or getting back up
};

// How low the knight is carrying itself: lower is slower but much harder to see or hear.
UENUM(BlueprintType)
enum class EKnightStance : uint8
{
	Standing,
	Crouched,
	Prone,
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
class UNREALSOULSLIKE_API ASoulsKnight : public ACombatCharacter, public ISoulsPlayableCharacter
{
	GENERATED_BODY()

public:
	ASoulsKnight();

	// ISoulsPlayableCharacter (UNR-T85): the character roster's hide-and-park swap, same calls
	// ASoulsMech::Board()/Disembark() already use on this knight while it rides along piloting a
	// vehicle.
	virtual void OnBecomeActive() override;
	virtual void OnBecomeInactive() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Knight")
	TObjectPtr<UStaticMeshComponent> Sword;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Knight")
	TObjectPtr<UStaticMeshComponent> Shield;

	// The grapple's line, stretched from the off hand to the hook while it's out.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Knight")
	TObjectPtr<UStaticMeshComponent> GrappleLine;

	// Melt into a shadow that crawls over the ground and up walls (UNR-T71): LT / Q.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Knight")
	TObjectPtr<UShadowMergeComponent> ShadowMelt;

	// Wall running (UNR-T77): A alongside a wall at speed; A again kicks off it. SoulsWallRun.*.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Knight")
	TObjectPtr<USoulsWallRunComponent> WallRun;

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

	// Stealth stances: crouch loops, the army crawl, and lying down / getting up (played forwards
	// from its first, lying frame).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knight|Animation")
	TObjectPtr<UAnimMontage> CrouchIdleMontage;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knight|Animation")
	TObjectPtr<UAnimMontage> CrouchMoveMontage;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knight|Animation")
	TObjectPtr<UAnimMontage> CrawlMontage;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knight|Animation")
	TObjectPtr<UAnimMontage> GetUpMontage;

	// Idle talk between patrollers: a talking loop and the odd nod or shake of the head.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knight|Animation")
	TObjectPtr<UAnimMontage> TalkMontage;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knight|Animation")
	TArray<TObjectPtr<UAnimMontage>> GestureMontages;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knight|Stealth")
	float CrouchSpeed = 220.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knight|Stealth")
	float CrawlSpeed = 110.f;

	// How far the crawl clip's body sits above the ground; the mesh is lowered by this while crawling.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Knight|Stealth")
	float CrawlMeshDrop = 70.f;

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
	float SwordReach = 235.f;

	// Burn damage per second at full flame (field fire, UNR-T41).
	UPROPERTY(EditAnywhere, Category = "Souls|Hazards")
	float BurnDamagePerSecond = 18.f;

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

	// Stealth: how much of the enemies' sight range this knight shows up at, and how far off it
	// can be heard (enemies notice you inside this radius whichever way they face).
	EKnightStance GetStance() const { return Stance; }
	float GetVisibilityScale() const;
	float GetNoiseRadius() const;
	void SetStance(EKnightStance NewStance);
	void DiveProne();
	void GetUpFromProne();

	// Idle talk (patrolling enemies): loop the talking animation, or stop it.
	void SetTalking(bool bTalking);
	void PlayGesture();

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
	// D-pad up away from anything to use: whistle, and unaware foes in earshot come over to look.
	void Whistle();
	void ToggleLockOn();
	void GrapplePressed();
	void ShadowPressed();
	// X: the one normal attack (the full overhead swing), or a torch swing with a torch in hand.
	void AttackPressed();
	// R3: crouch, or stand back up (from prone too).
	void CrouchToggle();

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

	// Burning (SoulsTorch.cpp): health only, no flinch, sound or hitstop.
	void TakeBurnDamage(float Amount, AActor* Source) { LoseHealth(Amount, Source); }

protected:
	virtual void BeginPlay() override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void NotifyControllerChanged() override;
	virtual bool CanJumpInternal_Implementation() const override;

	// Drives the action/busy state machine and plays its montage (or none); a roster sibling like
	// ASoulsRanger uses this to register its own non-sword "attacks" the same way.
	void PlayAction(EKnightAction NewAction, UAnimMontage* Montage, float Duration, float StartFraction = 0.f);

	// The Shade drives the shared kit (actions, sweeps, lock-on, stamina) from SoulsShadowBoss.cpp.
	friend class ASoulsShadowBoss;
	friend class USoulsWallRunComponent;

private:
	// Verification run (-KnightDemo): performs every move and screenshots it.
	void RunVerificationDemo();
	// -StealthDemo: dive prone, crawl, get up, then crouch-walk up behind a sentry and backstab it.
	void RunStealthDemo();
	void TickStealthDemo();
	void Shot(const TCHAR* Name);
	// -InputTest: presses real controller buttons through the player controller and checks each result.
	void RunInputTest();
	// -HandShots: close views of the sword hand and the shield arm, after textures stream in.
	void RunHandShots();
	// -ChainShots: X, X: the slash and the back-slash, frame by frame from the side.
	void RunChainShots();
	// -GrappleDemo: RT at a grapple point, the zip, the launch, A at the apex to glide, the landing.
	void RunGrappleDemo();
	// -ShadowDemo: melt (LT), crawl to a ruin wall and up it, rise out (LT).
	void RunShadowDemo();
	// -ClashDemo: a foe attacks into the raised guard (sparks, jolt, bounce), then a timed guard parries.
	// -WhistleDemo: whistle from out of sight; the foe wanders over to look.
	void RunClashDemo();
	void RunWhistleDemo();
	void TickClashDemo();
	bool bClashParry = false;
	int32 ClashBlocks = 0;
	int32 ClashParries = 0;
	void InputTestKey(const FKey& Key, bool bDown);
	TWeakObjectPtr<ASoulsKnight> DemoVictim;
	FVector DemoMove = FVector::ZeroVector;
	bool bDemoStalk = false;
	float DemoStalkStart = 0.f;

	void EnsureKnightInput();
	// Light/heavy swings step in toward the foe in front, so the blade reaches it: after Delay, over Window.
	void StartLunge(float MaxDistance, float Delay, float Window);
	// AI swings telegraph: a slow raise, then a fast strike. Retimes the playing montage and the
	// action; returns the seconds until the blade lands.
	float ApplyWindup(UAnimMontage* Montage, float BaseRate, float HitFraction, bool bHeavy);
	// A blow caught on this knight's raised shield: the guard jolts, the feet slide, sparks fly.
	void GuardImpact(const FVector& ToAttacker, bool bHeavy);
	// This knight's blade glanced off a shield: it bounces back a beat and the knight is shoved off.
	void RecoilFromGuard(bool bHeavy);
	void TickStance(float DeltaSeconds);
	// Prone and asked to do something: get up instead (true = the request was used up).
	bool GetUpFirst();
	bool SpendStamina(float Cost);
	void StartSwing(int32 Index);
	void StartHeavy();
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
	// The blade (UNR-T57): swept frame to frame against bodies while the strike is live.
	bool GetBladeSegment(FVector& OutBase, FVector& OutTip) const;
	void SweepBlade();
	// Opens the blade for the middle of a swing; the near-miss pass runs as it closes.
	void OpenBlade(float From, float Until);
	void CloseBlade();
	void ResolveStrike(ASoulsKnight* Victim, const FVector& Impact, float Damage, float PoiseDamage, float StanceHit, bool bHeavy);
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
	// The grappling gun (UNR-T70): any ledge in view; zip, launch over the top, then glide on A.
	bool FindLedge(FVector& OutHook, FVector& OutGround) const;
	void StartGlide();
	void EndGlide();
	void TickGlide(float DeltaSeconds);
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

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> AttackAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> ShadowAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> CrouchAction;

	FTimerHandle WindupTimer;
	FTimerHandle StrikeTimer;
	FTimerHandle RecoilTimer;
	float LungeDelay = 0.f;
	bool bGliding = false;
	// Airborne off a grapple launch: the only time A spreads into a glide (UNR-T76).
	bool bGrappleLaunched = false;
	float GrappleLaunchTime = -100.f;
	bool bBladeTracked = false;
	FVector BladePrevBase = FVector::ZeroVector;
	FVector BladePrevTip = FVector::ZeroVector;
	float BladeLiveFrom = -1.f;
	float BladeLiveUntil = -1.f;
	FTimerHandle BladeTimer;
	// Closest the blade came to each foe this swing (gap to its body, and the blade point).
	TMap<TWeakObjectPtr<AActor>, TPair<float, FVector>> BladeNear;
	float LastWhistleTime = -10.f;
	FVector LungeDirection = FVector::ZeroVector;
	float LungeRemaining = 0.f;
	float LungeSpeed = 0.f;

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
	bool bSprinting = false;
	bool bSprintToggled = false;
	bool bTalking = false;
	EKnightStance Stance = EKnightStance::Standing;
	float BaseMeshZ = 0.f;
	float MeshDrop = 0.f;
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
