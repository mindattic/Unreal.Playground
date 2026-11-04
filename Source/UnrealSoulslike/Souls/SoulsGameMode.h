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
	bool bLeaper = false;

	// Bosses only: walking within ArenaRadius of ArenaCenter starts the fight; felled bosses stay felled.
	FVector ArenaCenter = FVector::ZeroVector;
	float ArenaRadius = 0.f;
	float MusicPitch = 1.f;
	bool bDefeated = false;

	UPROPERTY()
	TObjectPtr<USoundBase> Music;
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

	// The Shade of the Fen, spawned at a TargetPoint tagged ShadowBoss (or by -ShadowBossTest).
	UPROPERTY(EditDefaultsOnly, Category = "Souls")
	TSubclassOf<ASoulsEnemy> ShadowBossClass;

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

	// The Shade's fight music; empty plays BossMusic pitched down into something darker.
	UPROPERTY(EditDefaultsOnly, Category = "Souls|Sound")
	TObjectPtr<USoundBase> ShadowBossMusic;

	UPROPERTY(EditDefaultsOnly, Category = "Souls|Sound")
	TObjectPtr<USoundBase> AmbientLoop;

	UPROPERTY(EditDefaultsOnly, Category = "Souls|Sound")
	TObjectPtr<USoundBase> BossRoar;

	// Radius around the Boss point that starts the fight.
	UPROPERTY(EditDefaultsOnly, Category = "Souls")
	float BossArenaRadius = 2600.f;

	// Radius around the ShadowBoss point that starts the Shade's fight.
	UPROPERTY(EditDefaultsOnly, Category = "Souls")
	float ShadowBossArenaRadius = 2200.f;

	int32 Runes = 0;
	int32 GoldenSeeds = 0;
	int32 CrimsonTears = 0;

	UPROPERTY(EditDefaultsOnly, Category = "Souls|Sound")
	TObjectPtr<USoundBase> PickupSound;

	FText ToastText;
	float ToastTime = -100.f;
	void ShowToast(const FText& Text);
	float LastAmbushKillTime = -100.f;
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
	// Whichever boss the player is fighting right now (the HUD's boss bar), or null.
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
	FSoulsSpawn& AddBossSpawn(TSubclassOf<ASoulsEnemy> Class, const FTransform& Point, float ArenaRadius, USoundBase* Music, float MusicPitch);
	void StartBossFight(int32 SpawnIndex);
	void EndBossFight(bool bWon);
	void RunVerificationTour();

	UPROPERTY()
	TArray<FSoulsSpawn> Spawns;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> MusicComponent;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> AmbientComponent;

	bool bBossFightActive = false;
	int32 ActiveBossSpawn = INDEX_NONE;
	FTimerHandle ResetTimer;
	TWeakObjectPtr<ASoulsEnemy> TokenHolder;
	float TokenTime = -100.f;
};
