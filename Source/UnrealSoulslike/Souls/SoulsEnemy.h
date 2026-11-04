#pragma once

#include "CoreMinimal.h"
#include "SoulsKnight.h"
#include "AIController.h"
#include "SoulsEnemy.generated.h"

// An island knight: the player's own move set, slower and readable, driven by ASoulsEnemyController.
UCLASS()
class UNREALSOULSLIKE_API ASoulsEnemy : public ASoulsKnight
{
	GENERATED_BODY()

public:
	ASoulsEnemy();

	// How far it notices the player (cm), and how far from home it will chase.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy|AI")
	float SightRange = 1600.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy|AI")
	float LeashRange = 3500.f;

	// Preferred fighting distance and pause between attack strings (seconds, min..max).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy|AI")
	float AttackRange = 230.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy|AI")
	FVector2D AttackPause = FVector2D(1.2f, 2.6f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy|AI")
	float BlockChance = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy|AI")
	float HeavyChance = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy|AI")
	int32 MaxComboLength = 3;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy|AI")
	float WalkSpeed = 330.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy|AI")
	float ChaseSpeed = 480.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy")
	bool bIsBoss = false;

	// Boss: second phase below this health fraction (faster, more aggressive).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy|Boss")
	float PhaseTwoAt = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Enemy|Boss")
	TObjectPtr<UMaterialInterface> PhaseTwoMaterial;

	bool bPhaseTwo = false;
	FVector HomeLocation = FVector::ZeroVector;
	FRotator HomeRotation = FRotator::ZeroRotator;

	void EnterPhaseTwo();

protected:
	virtual void BeginPlay() override;
};

// The Warden of the stone circle: half again as tall, heavy poise, hard-hitting long strings and a
// faster second phase. Can be parried and stance-broken but not backstabbed.
UCLASS()
class UNREALSOULSLIKE_API ASoulsBoss : public ASoulsEnemy
{
	GENERATED_BODY()

public:
	ASoulsBoss();
};

UENUM()
enum class EEnemyMind : uint8
{
	Idle,
	Engage,
	Return,
};

// Elden-Ring-style melee brain, no navmesh needed: notices the player by sight, closes in, circles at
// sword range, then commits to telegraphed strings (1-3 light swings or a heavy), guards or rolls
// away from incoming attacks, gives up and walks home past its leash.
UCLASS()
class UNREALSOULSLIKE_API ASoulsEnemyController : public AAIController
{
	GENERATED_BODY()

public:
	ASoulsEnemyController();
	virtual void Tick(float DeltaSeconds) override;

	void OnDanger(AActor* Source);
	void OnDamaged(AActor* Source);
	EEnemyMind GetMind() const { return Mind; }

private:
	ASoulsEnemy* Knight() const;
	bool CanSee(const AActor* Target) const;
	void Engage(ASoulsKnight* Target);
	void ThinkCombat(ASoulsKnight* Target, float DeltaSeconds);
	void Steer(const FVector& Direction, float Scale);

	TWeakObjectPtr<ASoulsKnight> Target;
	EEnemyMind Mind = EEnemyMind::Idle;
	float NextDecision = 0.f;
	float StrafeSign = 1.f;
	float StrafeUntil = 0.f;
	float BlockUntil = 0.f;
	int32 SwingsLeft = 0;
	float LastSightCheck = 0.f;
};
