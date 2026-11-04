#pragma once

#include "CoreMinimal.h"
#include "Engine/TimerHandle.h"
#include "GameFramework/Actor.h"
#include "Subsystems/WorldSubsystem.h"
#include "TallGrassPatch.generated.h"

class UHierarchicalInstancedStaticMeshComponent;
class UStaticMesh;

// A round patch of chest-high grass to hide in (UNR-T43). Its own instanced grass (filled by
// tools/build_island.py through SetGrass) is purely visual and has no collision; anyone whose feet are within
// Radius of the centre counts as inside, and USoulsConcealment scales enemy sight of them by Concealment.
UCLASS()
class UNREALSOULSLIKE_API ATallGrassPatch : public AActor
{
	GENERATED_BODY()

public:
	ATallGrassPatch();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// Editor (build_island.py): replace the patch's grass with Mesh at these world transforms.
	UFUNCTION(BlueprintCallable, Category = "TallGrass")
	void SetGrass(UStaticMesh* Mesh, const TArray<FTransform>& WorldTransforms);

	// True when Location (an actor's location, e.g. a capsule centre) is inside the patch.
	bool Contains(const FVector& Location) const;

	// Horizontal radius of the hiding area, cm, about the actor's location (which sits on the ground).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TallGrass", meta = (ClampMin = "50"))
	float Radius = 450.f;

	// Height of the grass tops above the actor's location, cm.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TallGrass", meta = (ClampMin = "10"))
	float GrassHeight = 140.f;

	// Multiplier on enemy sight range for someone inside: 0.25 means a 16 m sighting needs 4 m. It stacks with
	// the knight's own stance scale (crouched ~2.2 m, prone ~1.2 m).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TallGrass", meta = (ClampMin = "0.01", ClampMax = "1"))
	float Concealment = 0.25f;

protected:
	UPROPERTY(VisibleAnywhere, Category = "TallGrass")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> Grass;
};

// Keeps the live tall-grass patches of a world for fast concealment queries. With -TallGrassShot it also walks
// the player into the patch nearest the start, logs the concealment there and screenshots it
// (Saved/TallGrassShots/TallGrass_Inside.png), then quits.
UCLASS()
class UNREALSOULSLIKE_API UTallGrassSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	void Register(ATallGrassPatch* Patch);
	void Unregister(ATallGrassPatch* Patch);

	const TArray<TWeakObjectPtr<ATallGrassPatch>>& GetPatches() const { return Patches; }

private:
	void ShotEnter();
	void ShotTake();
	void ShotQuit();

	TArray<TWeakObjectPtr<ATallGrassPatch>> Patches;
	FTimerHandle ShotTimer;
};
