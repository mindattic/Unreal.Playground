#pragma once

#include "CoreMinimal.h"
#include "CombatGameMode.h"
#include "SoulsGameMode.generated.h"

class ASoulsKnight;
class ASoulsEnemy;
class ASiteOfGrace;
class USoundBase;
class UAudioComponent;

USTRUCT()
struct FSoulsSpawn
{
	GENERATED_BODY()

	UPROPERTY()
	TSubclassOf<ASoulsEnemy> Class;

	UPROPERTY()
	FTransform Transform;

	UPROPERTY()
	TWeakObjectPtr<ASoulsEnemy> Actor;

	bool bBoss = false;
};

// Runs the island: spawns enemies, the boss and Sites of Grace at the level's tagged TargetPoints
// (Enemy / Boss / Grace), resets the world when the player rests or dies, keeps the rune count and
// raises the big Elden-Ring-style banners (YOU DIED, ENEMY FELLED, LOST GRACE DISCOVERED).
UCLASS()
class UNREALSOULSLIKE_API ASoulsGameMode : public ACombatGameMode
{
	GENERATED_BODY()

public:
	ASoulsGameMode();

	UPROPERTY(EditDefaultsOnly, Category = "Souls")
	TSubclassOf<ASoulsEnemy> EnemyClass;

	UPROPERTY(EditDefaultsOnly, Category = "Souls")
	TSubclassOf<ASoulsEnemy> BossClass;

	UPROPERTY(EditDefaultsOnly, Category = "Souls")
	TSubclassOf<ASiteOfGrace> GraceClass;

	UPROPERTY(EditDefaultsOnly, Category = "Souls|Sound")
	TObjectPtr<USoundBase> GraceSound;

	UPROPERTY(EditDefaultsOnly, Category = "Souls|Sound")
	TObjectPtr<USoundBase> FelledSound;

	UPROPERTY(EditDefaultsOnly, Category = "Souls|Sound")
	TObjectPtr<USoundBase> DiedSound;

	UPROPERTY(EditDefaultsOnly, Category = "Souls|Sound")
	TObjectPtr<USoundBase> BossMusic;

	UPROPERTY(EditDefaultsOnly, Category = "Souls|Sound")
	TObjectPtr<USoundBase> AmbientLoop;

	UPROPERTY(EditDefaultsOnly, Category = "Souls|Sound")
	TObjectPtr<USoundBase> BossRoar;

	// Radius around the Boss point that starts the fight.
	UPROPERTY(EditDefaultsOnly, Category = "Souls")
	float BossArenaRadius = 2600.f;

	int32 Runes = 0;
	int32 LastRunesGained = 0;
	float LastRunesTime = -100.f;

	FText BannerText;
	FLinearColor BannerColor = FLinearColor::White;
	float BannerTime = -100.f;
	float BannerDuration = 0.f;

	void ShowBanner(const FText& Text, const FLinearColor& Color, float Duration = 4.f);
	void KnightDied(ASoulsKnight* Knight);
	void RestAtGrace(ASiteOfGrace* Grace, ASoulsKnight* Knight);
	void GraceDiscovered(ASiteOfGrace* Grace);
	ASoulsEnemy* GetActiveBoss() const;

	// Mobs take turns: one grunt commits to an attack at a time (the boss ignores this).
	bool RequestAttackToken(ASoulsEnemy* Enemy);
	ASoulsKnight* GetPlayerKnight() const;

	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;

private:
	void GatherSpawns();
	void RespawnEnemies(bool bIncludeBoss);
	ASoulsEnemy* SpawnFrom(FSoulsSpawn& Spawn);
	void StartBossFight();
	void EndBossFight(bool bWon);
	void RunVerificationTour();

	UPROPERTY()
	TArray<FSoulsSpawn> Spawns;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> MusicComponent;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> AmbientComponent;

	FVector BossPoint = FVector::ZeroVector;
	bool bHasBossPoint = false;
	bool bBossFightActive = false;
	bool bBossDefeated = false;
	FTimerHandle ResetTimer;
	TWeakObjectPtr<ASoulsEnemy> TokenHolder;
	float TokenTime = -100.f;
};
