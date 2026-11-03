#pragma once

#include "CoreMinimal.h"
#include "SoulslikeCombatant.h"
#include "SkeletonEnemy.generated.h"

UENUM(BlueprintType)
enum class EAwareness : uint8
{
	Unaware,
	Suspicious,
	Alerted,
};

// A skeleton of the island. Server-side AI: perceives players by sight (cone + line of sight)
// and hearing (running nearby), builds suspicion, alerts its neighbours, then chases and
// fights with telegraphed attacks and the occasional shield block; leashes back home.
UCLASS()
class UNREALSOULSLIKE_API ASkeletonEnemy : public ASoulslikeCombatant
{
	GENERATED_BODY()

public:
	ASkeletonEnemy();

	// Which entry of CharacterAssets.json "skeletons" to wear; replicated so clients match.
	UPROPERTY(ReplicatedUsing = OnRep_Appearance, BlueprintReadOnly, Category = "Skeleton")
	int32 VisualIndex = 0;

	UPROPERTY(ReplicatedUsing = OnRep_Appearance, BlueprintReadOnly, Category = "Skeleton")
	bool bIsBoss = false;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Skeleton")
	EAwareness Awareness = EAwareness::Unaware;

	// 0..1 detection meter; Suspicious past SuspicionThreshold, Alerted at 1.
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Skeleton")
	float Suspicion = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skeleton|Perception")
	float SightRange = 2200.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skeleton|Perception")
	float SightHalfAngle = 60.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skeleton|Perception")
	float HearingRange = 450.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skeleton|Perception")
	float SuspicionThreshold = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skeleton|Perception")
	float AllyAlertRadius = 1200.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skeleton|AI")
	float AttackRange = 190.f;

	// How far an unaware skeleton wanders from home (bridge guards keep it small).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skeleton|AI")
	float WanderRadius = 600.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skeleton|AI")
	float LeashRadius = 3500.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skeleton|AI")
	float HeavyAttackChance = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Skeleton|AI")
	float BlockChance = 0.0f;

	// Server, before FinishSpawning: turn this skeleton into the keep's boss.
	void ConfigureAsBoss();

	// Server: become Alerted towards Target (hit, ally shout).
	void Alert(AActor* Target);

	bool IsUnaware() const { return Awareness == EAwareness::Unaware; }

	virtual void Tick(float DeltaSeconds) override;
	virtual float TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;
	virtual void OnDied() override;

	UFUNCTION()
	void OnRep_Appearance();

private:
	void UpdatePerception(float DeltaSeconds);
	void UpdateBehaviour(float DeltaSeconds);
	void SetAwareness(EAwareness NewAwareness);
	ASoulslikeCombatant* SenseBestTarget(float& OutSightGain) const;
	bool HasLineOfSight(const AActor* Other) const;
	void MoveTowards(const FVector& Destination, float Scale);

	UPROPERTY(Transient)
	TWeakObjectPtr<ASoulslikeCombatant> Target;

	FVector HomeLocation = FVector::ZeroVector;
	FVector LastKnownTargetLocation = FVector::ZeroVector;
	FVector WanderDestination = FVector::ZeroVector;
	float PerceptionTimer = 0.f;
	float AttackCooldown = 1.f;
	float BlockTimer = 0.f;
	float WanderTimer = 0.f;
	float AlertedLostSightTime = 0.f;
	bool bAppearanceApplied = false;
};
