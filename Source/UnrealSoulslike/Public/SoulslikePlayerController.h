#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "SoulslikePlayerController.generated.h"

class USoulsCharacterRosterComponent;

// Game-only input mode, plus unattended-run switches for verifying and comparing builds:
//   -AutoShot=<seconds>   take a screenshot every N seconds (Saved/Screenshots)
//   -QuitAfter=<seconds>  exit the game after N seconds
//   -Benchmark=<seconds>  after a 5 s warm-up, record frame/game-thread times and memory for N
//                         seconds, write Saved/Benchmark/benchmark-cpp.json, then exit
//                         (pair with -DemoBot so the run plays itself)
UCLASS()
class UNREALSOULSLIKE_API ASoulslikePlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ASoulslikePlayerController();

	// The playable-character roster (UNR-T85): , and . cycle through it.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Soulslike")
	TObjectPtr<USoulsCharacterRosterComponent> Roster;

	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;

private:
	void TakeAutoShot();
	void QuitGame();
	void WriteBenchmark();

	FTimerHandle AutoShotHandle;
	FTimerHandle QuitHandle;
	int32 ShotIndex = 0;

	float BenchmarkSeconds = 0.f;
	float BenchmarkElapsed = 0.f;
	TArray<float> FrameMs;
	TArray<float> GameThreadMs;
};
