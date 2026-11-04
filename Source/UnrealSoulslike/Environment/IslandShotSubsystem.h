#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "IslandShotSubsystem.generated.h"

class ACameraActor;

// Dev-only screenshot director for verifying the island look without anyone at the keyboard.
// Inert unless the game is started with -IslandShots=<file>. Each line of the file is
//   <name> player                         (the normal third-person view)
//   <name> X Y Z Pitch Yaw [FOV]          (a fixed camera, cm / degrees)
// After the world has warmed up (shaders compiled, streaming settled) it frames each shot, writes
// <ShotDir>/<name>.png (-IslandShotDir=<dir>, default Saved/IslandShots) and then quits.
UCLASS()
class UNREALSOULSLIKE_API UIslandShotSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override { return bActive; }

private:
	struct FShot
	{
		FString Name;
		bool bPlayerView = true;
		FVector Location = FVector::ZeroVector;
		FRotator Rotation = FRotator::ZeroRotator;
		float Fov = 70.f;
	};

	void Frame(const FShot& Shot);

	TArray<FShot> Shots;
	FString ShotDir;
	int32 Index = 0;
	float Clock = 0.f;
	float PhaseStart = 0.f;
	bool bActive = false;
	bool bWarm = false;
	bool bFramed = false;
	bool bRequested = false;
	float Warmup = 25.f;
	float Settle = 6.f;

	UPROPERTY()
	TObjectPtr<ACameraActor> Camera;
};
