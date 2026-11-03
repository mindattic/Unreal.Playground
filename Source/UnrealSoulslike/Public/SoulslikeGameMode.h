#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "SoulslikeGameMode.generated.h"

// Co-op demo game mode: every connecting player gets their own SoulslikeCharacter at one of
// the level's PlayerStarts; there is no PvP and no single "host advantage" in the ruleset,
// so the only thing this overrides from the engine default is which pawn/controller classes
// to spawn.
UCLASS()
class UNREALSOULSLIKE_API ASoulslikeGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ASoulslikeGameMode();
};
