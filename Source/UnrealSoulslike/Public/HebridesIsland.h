#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HebridesIsland.generated.h"

class UProceduralMeshComponent;
class UStaticMeshComponent;
class UInstancedStaticMeshComponent;
class UDirectionalLightComponent;
class USkyAtmosphereComponent;
class USkyLightComponent;
class UExponentialHeightFogComponent;
class UMaterialInterface;

// The whole world in one actor: terrain built from the heightmap, the surrounding sea, sky,
// sun and Hebridean mist, the ruins (crofts, broch, standing stones, keep) and, on the server,
// the skeletons and their boss. Replicated so co-op clients get it; every machine builds the
// same geometry from the same Content/Terrain files.
UCLASS()
class UNREALSOULSLIKE_API AHebridesIsland : public AActor
{
	GENERATED_BODY()

public:
	AHebridesIsland();

	UPROPERTY(VisibleAnywhere, Category = "Island")
	TObjectPtr<UProceduralMeshComponent> Terrain;

	UPROPERTY(VisibleAnywhere, Category = "Island")
	TObjectPtr<UStaticMeshComponent> Sea;

	UPROPERTY(VisibleAnywhere, Category = "Island|Ruins")
	TObjectPtr<UInstancedStaticMeshComponent> StoneBlocks;

	UPROPERTY(VisibleAnywhere, Category = "Island|Ruins")
	TObjectPtr<UInstancedStaticMeshComponent> Megaliths;

	UPROPERTY(VisibleAnywhere, Category = "Island|Ruins")
	TObjectPtr<UInstancedStaticMeshComponent> Thatch;

	UPROPERTY(VisibleAnywhere, Category = "Island|Ruins")
	TObjectPtr<UInstancedStaticMeshComponent> Towers;

	UPROPERTY(VisibleAnywhere, Category = "Island|Ruins")
	TObjectPtr<UInstancedStaticMeshComponent> Boulders;

	UPROPERTY(VisibleAnywhere, Category = "Island|Sky")
	TObjectPtr<UDirectionalLightComponent> Sun;

	UPROPERTY(VisibleAnywhere, Category = "Island|Sky")
	TObjectPtr<USkyAtmosphereComponent> SkyAtmosphere;

	UPROPERTY(VisibleAnywhere, Category = "Island|Sky")
	TObjectPtr<USkyLightComponent> SkyLight;

	UPROPERTY(VisibleAnywhere, Category = "Island|Sky")
	TObjectPtr<UExponentialHeightFogComponent> Mist;

	UPROPERTY(EditAnywhere, Category = "Island")
	int32 Seed = 1745;

	UPROPERTY(EditAnywhere, Category = "Island")
	bool bSpawnEnemies = true;

	virtual void OnConstruction(const FTransform& Transform) override;

protected:
	virtual void BeginPlay() override;

private:
	void BuildIsland();
	void BuildTerrain();
	void BuildSea();
	void BuildRuins();
	void BuildCroft(const FVector2D& UV, FRandomStream& Random);
	void BuildBroch(const FVector2D& UV, FRandomStream& Random);
	void BuildStandingStones(const FVector2D& UV, FRandomStream& Random);
	void BuildKeep(const FVector2D& UV, FRandomStream& Random);
	void ScatterBoulders(FRandomStream& Random);
	void BuildBuriedRuins(FRandomStream& Random);
	void BuildFieldWalls(FRandomStream& Random);
	void BuildBridge();
	void SpawnEnemies();

	// Adds a box of Size (cm) centred at Centre to Instances (the cube mesh is 100 cm).
	void AddBox(UInstancedStaticMeshComponent* Instances, const FVector& Centre, const FVector& Size, const FRotator& Rotation);
	UMaterialInterface* MakeTinted(const TCHAR* MaterialPath, const FLinearColor& Color);

	// The instanced component for a kit piece (created on first use, tagged Mergeable), or
	// nullptr when the piece wasn't imported.
	UInstancedStaticMeshComponent* Kit(FName Piece);
	bool HasKit(FName Piece) { return Kit(Piece) != nullptr; }

	// Local bounds of a kit piece (unscaled); the kit's origins vary, so placement uses these.
	FBox KitBounds(FName Piece);

	// Places a kit piece so the bottom-centre of its bounds lands on BottomCentre.
	bool PlaceKit(FName Piece, const FVector& BottomCentre, const FRotator& Rotation, const FVector& Scale);

	// A cottage: kit house scaled to Height (uniform), sunk by SinkFraction of its height.
	bool PlaceHouse(FName Piece, const FVector2D& UV, float Height, float SinkFraction, float MaxTilt, FRandomStream& Random);

	// A run of wall pieces from A to B (UV), each scaled to Height/Thickness and tiled to fit,
	// sunk by SinkFraction and tilted up to MaxTilt degrees; boxes when the kit is missing.
	void BuildWallRun(const FVector2D& A, const FVector2D& B, float Height, float Thickness, float SinkFraction, float MaxTilt, float BrokenChance, FRandomStream& Random);

	float GroundZ(const FVector& World) const;

	UPROPERTY(Transient)
	TMap<FName, TObjectPtr<UInstancedStaticMeshComponent>> KitInstances;

	bool bBuilt = false;
};
