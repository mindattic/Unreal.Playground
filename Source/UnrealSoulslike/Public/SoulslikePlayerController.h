#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "SoulslikePlayerController.generated.h"

// Game-only input mode, plus unattended-run switches for verifying a build:
//   -AutoShot=<seconds>  take a screenshot every N seconds (Saved/Screenshots)
//   -QuitAfter=<seconds> exit the game after N seconds
UCLASS()
class UNREALSOULSLIKE_API ASoulslikePlayerController : public APlayerController
{
	GENERATED_BODY()

protected:
	virtual void BeginPlay() override;

private:
	void TakeAutoShot();
	void QuitGame();

	FTimerHandle AutoShotHandle;
	FTimerHandle QuitHandle;
	int32 ShotIndex = 0;
};
