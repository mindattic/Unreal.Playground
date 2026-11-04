#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "IslandBuilderLibrary.generated.h"

class ALandscape;
class UMaterialInterface;
class UTexture2D;
class UStaticMesh;

// Shape of the Hebrides island the build step (tools/build_island.py) generates. All distances in
// centimetres, world XY, sea level at Z = 0. The editor Python fills this in and hands it to
// UIslandBuilderLibrary, so the layout lives in one place (the build script).
USTRUCT(BlueprintType)
struct FIslandLayout
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Island")
	int32 Seed = 7;

	// Half-extents of the (noise-warped) coastline ellipse.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Island")
	float RadiusX = 20000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Island")
	float RadiusY = 16500.f;

	// Coast facing this way becomes sheer cliffs.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Island")
	FVector2D CliffDirection = FVector2D(0.6, 0.8);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Island")
	float CliffHeight = 1800.f;

	// Coast facing this way gets the wide start beach.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Island")
	FVector2D BeachDirection = FVector2D(-0.85, -0.5);

	// Hill crowned by the flat stone-circle arena.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Island")
	FVector2D HillCenter = FVector2D(7000.0, 6000.0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Island")
	float HillRadius = 8000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Island")
	float ArenaRadius = 2700.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Island")
	float ArenaHeight = 3000.f;

	// The walking route from the start beach to the arena; the ground along it is smoothed.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Island")
	TArray<FVector2D> Path;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Island")
	float PathWidth = 450.f;

	// Extra flat pads (X, Y, radius) for ruins and fights.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Island")
	TArray<FVector> Pads;

	// Peat marsh: a flat basin at MarshLevel with pools carved to below PoolLevel (the water surface).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Island|Biomes")
	FVector2D MarshCenter = FVector2D(3500.0, -7500.0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Island|Biomes")
	float MarshRadius = 4500.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Island|Biomes")
	float MarshLevel = 380.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Island|Biomes")
	float PoolLevel = 330.f;

	// Dry clearing inside the marsh kept free of pools (the shadow boss fight).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Island|Biomes")
	FVector2D ClearingCenter = FVector2D(4500.0, -8500.0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Island|Biomes")
	float ClearingRadius = 1200.f;

	// Woodland / green grassland weight (trees, ferns, flowers).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Island|Biomes")
	FVector2D WoodCenter = FVector2D(-8000.0, 2000.0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Island|Biomes")
	float WoodRadius = 5500.f;

	// Small offshore islets (X, Y, radius) rising to IsletHeight.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Island|Biomes")
	TArray<FVector> Islets;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Island|Biomes")
	float IsletHeight = 650.f;

	// Landscape: ComponentsPerSide x ComponentsPerSide components of 2x2 63-quad sections.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Island")
	int32 ComponentsPerSide = 8;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Island")
	float QuadSize = 60.f;
};

// Editor-side helpers the island build script calls: the procedural height field, the Landscape
// built from it, and a mask texture (path / dirt / water depth) the materials sample.
UCLASS()
class UNREALSOULSLIKE_API UIslandBuilderLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Island")
	static void SetIslandLayout(const FIslandLayout& Layout);

	// Terrain height (cm) at a world XY point.
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Island")
	static float GetIslandHeight(float X, float Y);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Island")
	static FVector GetIslandNormal(float X, float Y);

	// Distance (cm) from the route polyline.
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Island")
	static float GetPathDistance(float X, float Y);

	// Half the landscape's width (cm); the landscape spans [-Extent, +Extent] in X and Y.
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Island")
	static float GetLandscapeExtent();

	// Spawns the island ALandscape in the editor world (editor builds only; returns null otherwise).
	UFUNCTION(BlueprintCallable, Category = "Island", meta = (WorldContext = "WorldContextObject"))
	static ALandscape* CreateIslandLandscape(UObject* WorldContextObject, UMaterialInterface* Material);

	// Creates (or overwrites) an uncompressed BGRA texture asset over the landscape extent:
	// R = route, G = trampled dirt (pads, arena), B = water depth (0..10 m), A = cliff mask.
	UFUNCTION(BlueprintCallable, Category = "Island")
	static UTexture2D* CreateIslandMaskTexture(const FString& PackagePath, int32 Resolution = 1024);

	// Biome weights over the landscape extent: R = marsh, G = woodland, B = heather highland, A = pool depth (0..1.5 m).
	UFUNCTION(BlueprintCallable, Category = "Island")
	static UTexture2D* CreateIslandBiomeTexture(const FString& PackagePath, int32 Resolution = 1024);

	// 0..1 weights at a point (same values the biome texture stores).
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Island")
	static FVector4 GetIslandBiome(float X, float Y);

	// Editor: one actor holding a hierarchical instanced static mesh component (vegetation, scattered props).
	UFUNCTION(BlueprintCallable, Category = "Island", meta = (WorldContext = "WorldContextObject"))
	static AActor* CreateInstancedMeshes(UObject* WorldContextObject, UStaticMesh* Mesh, const TArray<FTransform>& Instances,
		const FString& Label, const FString& Folder, bool bCollision, float CullDistance = 0.f);

	// Editor: spawns a PhysicsVolume with bWaterVolume (characters swim in it) spanning Center +- Extent.
	UFUNCTION(BlueprintCallable, Category = "Island", meta = (WorldContext = "WorldContextObject"))
	static AActor* CreateWaterVolume(UObject* WorldContextObject, FVector Center, FVector Extent, const FString& Label);
};
