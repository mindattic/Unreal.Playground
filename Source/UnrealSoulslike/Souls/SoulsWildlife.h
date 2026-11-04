#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SoulsWildlife.generated.h"

class UStaticMesh;
class UStaticMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;

UENUM(BlueprintType)
enum class ESoulsWildlifeKind : uint8
{
	Crab,  // scuttles sideways about the beach; bolts away from anyone who comes close
	Frog,  // sits at a pool's edge; leaps into the water (WaterTarget) when someone comes near
	Fish,  // swims about a pool (within RoamRadius of where it was placed, between FloorZ and WaterLevel)
};

// Ambient island wildlife (UNR-T36/T37). Placed in Lvl_Isle by tools/build_island.py. Deliberately cheap:
// no controller, no navmesh, no collision; a static mesh moved by hand each tick, with ground traces only
// for crabs while they move, and a slow tick when no player is anywhere near.
UCLASS()
class UNREALSOULSLIKE_API ASoulsWildlife : public AActor
{
	GENERATED_BODY()

public:
	ASoulsWildlife();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wildlife")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wildlife")
	TObjectPtr<UStaticMeshComponent> Body;

	// The frog's splash: a ring on the water and a few flung droplets.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wildlife")
	TObjectPtr<UStaticMeshComponent> Ripple;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wildlife")
	TArray<TObjectPtr<UStaticMeshComponent>> Droplets;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wildlife")
	ESoulsWildlifeKind Kind = ESoulsWildlifeKind::Crab;

	// Mesh authored facing +X with Z up (SourceArt/Environment/Critters, tools/convert_critters.py).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wildlife")
	TObjectPtr<UStaticMesh> Mesh;

	// Length along the mesh's X (cm) to scale it to; 0 keeps the mesh's own size.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wildlife")
	float BodyLength = 0.f;

	// Crab: wander radius about home. Fish: radius of the open water it swims in.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wildlife")
	float RoamRadius = 400.f;

	// A player this close (cm, horizontal) startles it.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wildlife")
	float ScareRadius = 500.f;

	// Top speed (cm/s): crab flight, frog unused, fish cruise.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wildlife")
	float Speed = 260.f;

	// Frog / fish: the water surface height.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wildlife")
	float WaterLevel = 0.f;

	// Fish: lowest height it may swim at (the pool bottom plus a margin).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wildlife")
	float FloorZ = 0.f;

	// Frog: where it lands in the water (Z is ignored; it lands at WaterLevel).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wildlife")
	FVector WaterTarget = FVector::ZeroVector;

	// Translucent material with an "Opacity" scalar for the splash (defaults to /Game/Island/Materials/M_CritterSplash).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wildlife")
	TObjectPtr<UMaterialInterface> SplashMaterial;

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;

private:
	enum class EState : uint8 { Idle, Walk, Flee, Leap, Under, Surface };

	void FitBody();
	void SetBodyPose(const FRotator& Rotation, const FVector& Offset, const FVector& Stretch = FVector::OneVector);
	bool NearestPlayer(FVector& OutLocation, float& OutDistance2D) const;
	bool GroundAt(const FVector& At, FVector& OutPoint, FVector& OutNormal) const;

	void TickCrab(float Dt, bool bHasPlayer, const FVector& PlayerLoc, float PlayerDist);
	void TickFrog(float Dt, bool bHasPlayer, const FVector& PlayerLoc, float PlayerDist);
	void TickFish(float Dt, bool bHasPlayer, const FVector& PlayerLoc, float PlayerDist);
	void StartLeap(const FVector& From, const FVector& To, float Duration, float Height);
	void StartSplash(const FVector& At);
	void TickSplash(float Dt);
	void PickFishTarget(const FVector* AwayFrom);

	FVector Home = FVector::ZeroVector;
	FVector BodyBase = FVector::ZeroVector;
	float BodyScale = 1.f;
	EState State = EState::Idle;
	float StateTime = 0.f;
	float StateDuration = 0.f;
	float Phase = 0.f;
	float Yaw = 0.f;
	FVector Target = FVector::ZeroVector;
	float CurrentSpeed = 0.f;

	// Frog leap.
	FVector LeapFrom = FVector::ZeroVector;
	FVector LeapTo = FVector::ZeroVector;
	float LeapHeight = 0.f;
	bool bLeapIntoWater = true;

	// Splash.
	float SplashAge = -1.f;
	FVector SplashAt = FVector::ZeroVector;
	TArray<FVector> DropletVelocity;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> SplashMID;
};
