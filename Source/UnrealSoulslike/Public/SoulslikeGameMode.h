#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "SoulslikeGameMode.generated.h"

// Co-op rules: every player lands on the beach (one of the island's player starts), the
// island is spawned if the map doesn't already hold one, and a dead player respawns on the
// beach after a few seconds with full vitals and flasks. No PvP.
UCLASS()
class UNREALSOULSLIKE_API ASoulslikeGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ASoulslikeGameMode();

	UPROPERTY(EditAnywhere, Category = "Soulslike")
	float RespawnDelay = 6.f;

	virtual void StartPlay() override;
	virtual APawn* SpawnDefaultPawnFor_Implementation(AController* NewPlayer, AActor* StartSpot) override;

	void NotifyPlayerDied(AController* Player);

private:
	void Respawn(TWeakObjectPtr<AController> Player);

	int32 NextStartIndex = 0;
};
