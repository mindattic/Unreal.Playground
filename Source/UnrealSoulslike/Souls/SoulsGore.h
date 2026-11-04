#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SoulsGore.generated.h"

class UStaticMesh;
class UStaticMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;

// Ludicrous gore (UNR-T50): every hit flings a spray of blood that arcs, lands and lies on the ground
// as splats for a while. Blood is lit spheres in the engine's basic shape material, tinted per drop.
UCLASS()
class UNREALSOULSLIKE_API ASoulsGore : public AActor
{
	GENERATED_BODY()

public:
	ASoulsGore();

	// Count drops sprayed from Location, mostly along Direction (spread 0..1); Scale sizes drops and speed.
	static ASoulsGore* Spray(UWorld* World, const FVector& Location, const FVector& Direction, int32 Count, float Scale, float Spread = 0.7f);

	virtual void Tick(float DeltaSeconds) override;

private:
	struct FDrop
	{
		TObjectPtr<UStaticMeshComponent> Mesh;
		FVector Velocity = FVector::ZeroVector;
		float Size = 1.f;
		float GroundZ = 0.f;
		bool bLanded = false;
		float SplatGrow = 0.f;
	};

	UPROPERTY()
	TObjectPtr<UStaticMesh> DropMesh;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> BaseMaterial;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> Meshes;

	UPROPERTY()
	TArray<TObjectPtr<UMaterialInstanceDynamic>> Tints;

	TArray<FDrop> Drops;
	float Age = 0.f;
	float Life = 24.f;
};
