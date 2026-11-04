#pragma once

#include "CoreMinimal.h"
#include "SoulsEnemy.h"
#include "Subsystems/WorldSubsystem.h"
#include "SoulsMonster.generated.h"

class UAnimMontage;

// The bestiary (UNR-T64/T67): island foes that aren't knights. Each type is its own rigged CC0 model
// (tools/convert_bestiary.py -> tools/build_bestiary.py -> BP_SoulsEnemy_<Type>), plays the knight's
// whole retargeted kit, and adds habits of its own through the data-driven knobs below. The shared
// brain (ASoulsEnemyController) asks ThinkMonster() first every combat tick; returning false hands the
// tick back to the knight's ordinary circling and attack strings.
UCLASS()
class UNREALSOULSLIKE_API ASoulsMonster : public ASoulsEnemy
{
	GENERATED_BODY()

public:
	ASoulsMonster();

	// The type's name as the build script and the island markers spell it ("Skeleton", "Brute"...).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Monster")
	FName Kind;

	// ---- Flight: cowards break off when hurt, catch their breath, then come back ----
	// Below this health fraction it turns and runs (0 = never).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Monster|Flee")
	float FleeBelowHealth = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Monster|Flee")
	FVector2D FleeDuration = FVector2D(2.5f, 4.f);

	// Seconds after a flight before it will run again.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Monster|Flee")
	float FleeCooldown = 7.f;

	// ---- Swarm: packs alert each other and swing without waiting their turn ----
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Monster|Swarm")
	bool bSwarm = false;

	// Same-kind foes this close join the fight when one of them engages.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Monster|Swarm")
	float SwarmCallRange = 2600.f;

	// ---- Grab: lurch in and seize the foe (knocked down, bitten) ----
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Monster|Grab")
	float GrabChance = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Monster|Grab")
	float GrabRange = 190.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Monster|Grab")
	float GrabDamage = 30.f;

	// Lunge wind-up before the hands close (s): roll or step back to slip it.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Monster|Grab")
	float GrabWindup = 0.55f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Monster|Grab")
	float GrabCooldown = 6.f;

	// ---- Gait ----
	// 0..1: how much its pace surges and stalls (zombies lurch, skeletons shamble).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Monster|Gait")
	float Lurch = 0.f;

	// Lurches per second.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Monster|Gait")
	float LurchRate = 0.8f;

	// ---- Evasion: chance to roll clear of an incoming swing instead of guarding ----
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Monster|Evade")
	float DodgeChance = 0.f;

	// ---- Animation extras (set by tools/build_bestiary.py) ----
	// Played and held when it dies on a mesh without a physics asset (no ragdoll).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Monster|Animation")
	TObjectPtr<UAnimMontage> DeathMontage;

	// The grab's lunge; falls back to the heavy attack.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Monster|Animation")
	TObjectPtr<UAnimMontage> GrabMontage;

	// Called by ASoulsEnemyController::ThinkCombat before its own logic; true = this tick is handled.
	bool ThinkMonster(ASoulsEnemyController* Brain, ASoulsKnight* Foe, float DeltaSeconds);

	// One line with every tuned number (logged by -BestiaryShots and on spawn at Verbose).
	FString DescribeStats() const;

	// "Bestiary_<Type>" on an Enemy TargetPoint -> /Game/Souls/BP_SoulsEnemy_<Type>, or null.
	static TSubclassOf<ASoulsEnemy> ClassForMarker(const AActor* Marker);
	static TSubclassOf<ASoulsEnemy> ClassForKind(FName InKind);

	virtual void Tick(float DeltaSeconds) override;
	virtual void HandleDeath() override;
	virtual void NotifyDanger(const FVector& DangerLocation, AActor* DangerSource) override;
	virtual void PossessedBy(AController* NewController) override;

protected:
	virtual void BeginPlay() override;

private:
	void CallThePack(ASoulsKnight* Foe);
	void StartGrab(ASoulsKnight* Foe);
	void FinishGrab();

	TWeakObjectPtr<ASoulsKnight> GrabVictim;
	FTimerHandle GrabTimer;
	FTimerHandle DeathHoldTimer;
	float GrabUntil = 0.f;
	float NextGrab = 0.f;
	float FleeUntil = 0.f;
	float NextFlee = 0.f;
	float NextSwarmSwing = 0.f;
	float LurchPhase = 0.f;
	float GaitBaseSpeed = 0.f;
	float GaitAppliedSpeed = -1.f;
	bool bPackCalled = false;
};

// Bones and a rusted cleaver from the barrow: never flinches, never fears, shambles; ruins and stones.
UCLASS()
class UNREALSOULSLIKE_API ASoulsSkeleton : public ASoulsMonster
{
	GENERATED_BODY()
public:
	ASoulsSkeleton();
};

// A hulking moor orc with a great club: slow, heavy, hard to stagger; long pauses, crushing blows.
UCLASS()
class UNREALSOULSLIKE_API ASoulsBrute : public ASoulsMonster
{
	GENERATED_BODY()
public:
	ASoulsBrute();
};

// A drowned thing from the peat: lurches, soaks blows, lunges to seize and bite; fearless.
UCLASS()
class UNREALSOULSLIKE_API ASoulsZombie : public ASoulsMonster
{
	GENERATED_BODY()
public:
	ASoulsZombie();
};

// Hollowed villagers of the woods: frail, quick, cowardly; they swarm, harass, and flee when hurt.
UCLASS()
class UNREALSOULSLIKE_API ASoulsHollow : public ASoulsMonster
{
	GENERATED_BODY()
public:
	ASoulsHollow();
};

// A hooded woodland huntress: quick strings, rolls away from your swings, keeps her distance.
UCLASS()
class UNREALSOULSLIKE_API ASoulsHuntress : public ASoulsMonster
{
	GENERATED_BODY()
public:
	ASoulsHuntress();
};

// A marsh imp with a cudgel: small, fast, springs from range (brace to impale it), skittish.
UCLASS()
class UNREALSOULSLIKE_API ASoulsImp : public ASoulsMonster
{
	GENERATED_BODY()
public:
	ASoulsImp();
};

// -BestiaryShots: lines every bestiary type up in front of a fixed camera, one at a time, and
// screenshots it idle and mid-attack (Bestiary_<Type>_1_Idle / _2_Windup / _3_Strike), logging each
// type's numbers, then quits ("BestiaryShots: ... BESTIARY_SHOTS_OK").
UCLASS()
class UNREALSOULSLIKE_API USoulsBestiaryShots : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

private:
	void Stage(int32 Index);
	void Finish();

	UPROPERTY(Transient)
	TObjectPtr<AActor> Camera;

	UPROPERTY(Transient)
	TObjectPtr<ASoulsEnemy> Current;

	FVector Spot = FVector::ZeroVector;
	FVector CameraSpot = FVector::ZeroVector;
	int32 Shown = 0;
	int32 Missing = 0;
};
