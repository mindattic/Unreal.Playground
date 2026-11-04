#pragma once

#include "CoreMinimal.h"
#include "CollisionQueryParams.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Subsystems/WorldSubsystem.h"
#include "SoulsFire.generated.h"

class AActor;
class ACameraActor;
class APhysicsVolume;
class UInstancedStaticMeshComponent;
class UMaterialInterface;
class UPointLightComponent;
class UStaticMesh;
class UStaticMeshComponent;
class UTexture2D;

// Field fire (UNR-T41). A fire started in grass, heather, ferns, shrubs or trees grows and spreads
// through the vegetation on a 1.28 m grid, then burns out; it never crosses water, sand or bare rock.
// Burning plants char black, then shrink and dissolve away. The burn state lives in one small texture
// (/Game/Souls/FX/T_FireField, built by tools/build_island.py) that the island's grass, foliage,
// prop and terrain materials sample: R = char, G = heat (ember glow), B = consumed (grass shrinks).
struct UNREALSOULSLIKE_API FSoulsFire
{
	// Sets fire to every burnable cell within Radius (cm) of Location (at least the cell under it).
	static void IgniteAt(UObject* WorldContext, const FVector& Location, float Radius);

	// True when Location (a character's feet or centre) stands in live flames.
	static bool IsBurningAt(UObject* WorldContext, const FVector& Location);

	// 0..1 flame strength at Location (0 = no fire), for scaling burn damage.
	static float FlameIntensityAt(UObject* WorldContext, const FVector& Location);

	// True when the ground at Location could catch (vegetated land, not water, sand or rock).
	static bool CanBurnAt(UObject* WorldContext, const FVector& Location);
};

enum class ESoulsFireItemKind : uint8 { Flower, Fern, Shrub, Tree, Log };

// Runs the fire simulation and its cheap visuals (additive flame cones, smoke puffs, a few pooled
// flickering point lights) for one game world. Idle and free until something ignites.
// -FireDemo: ignites a grassy spot near the player start and screenshots the spread, char and dissolve.
UCLASS()
class UNREALSOULSLIKE_API USoulsFireSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	static USoulsFireSubsystem* Get(const UObject* WorldContext);

	void Ignite(const FVector& Location, float Radius);
	float IntensityAt(const FVector& Location) const;
	bool CanBurn(const FVector& Location);

	int32 GetBurningCellCount() const { return BurningCells; }

	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override { return bActive || bDemo; }

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	struct FItem
	{
		TWeakObjectPtr<UStaticMeshComponent> Comp;
		int32 Instance = INDEX_NONE;   // INDEX_NONE: a whole static mesh component
		FTransform Original;           // instance: component-local; component: world
		FVector Base = FVector::ZeroVector; // world, bottom-centre
		float Height = 100.f;
		ESoulsFireItemKind Kind = ESoulsFireItemKind::Fern;
		float IgniteTime = -1.f;
		float Scale = 1.f;
		bool bDone = false;
	};

	struct FPuff
	{
		FVector Location = FVector::ZeroVector;
		float Age = 0.f;
		float Life = 0.f;
		float Size = 1.f;
	};

	void EnsureStarted();
	void IndexVegetation();
	void LoadAssets();
	void SpawnVisuals();
	void ClearField();

	int32 CellOf(const FVector& Location) const;
	FVector CellCenter(int32 Cell) const;
	uint8 EvaluateCell(int32 Cell);
	bool IgniteCell(int32 Cell, float InFireStart);

	void Step(float Dt);
	void StepItems();
	void UpdateVisuals(float Dt);
	void UploadField();

	float CellIntensity(int32 Cell, float T) const;
	float CellDuration(int32 Cell) const;
	float ItemIntensity(const FItem& Item, float T) const;
	static float ItemBurnTime(ESoulsFireItemKind Kind);

	void TickDemo(float DeltaTime);
	bool PickDemoSpot(FVector& OutSpot);

	// Grid state (dense, N x N over the field square).
	TArray<uint8> Fuel;
	TArray<float> Load;        // fuel load: 1 = grass, more with shrubs/trees in the cell
	TArray<float> GroundZ;
	TArray<float> IgniteTime;  // < 0: never lit
	TArray<float> FireStart;   // when the fire this cell belongs to began (bounds a fire's life)
	TArray<uint8> InActive;
	TArray<FColor> Pixels;     // CPU copy of the field texture (BGRA)
	TArray<int32> Active;      // cells burning, smouldering or still changing
	TMap<int32, TArray<int32>> CellItems; // cell -> plants standing in it
	TArray<FItem> Items;
	TArray<int32> ActiveItems;
	TArray<FPuff> Puffs;
	TArray<TWeakObjectPtr<APhysicsVolume>> WaterVolumes;
	TArray<float> LightTarget;
	FCollisionQueryParams TraceParams;

	int32 DirtyMinX = MAX_int32, DirtyMinY = MAX_int32, DirtyMaxX = -1, DirtyMaxY = -1;
	int32 BurningCells = 0;
	int32 BurntCells = 0;
	int32 ConsumedItems = 0;
	int32 FlamesShown = 0;
	int32 LightsLit = 0;
	float Clock = 0.f;
	float StepAccum = 0.f;
	float PuffAccum = 0.f;
	FRandomStream Rng;
	bool bStarted = false;
	bool bActive = false;
	bool bFieldTouched = false;

	// -FireDemo
	bool bDemo = false;
	bool bDemoIgnited = false;
	float DemoClock = 0.f;
	float DemoIgniteAt = 0.f;
	int32 DemoShot = 0;
	float DemoNextLog = 0.f;
	FVector DemoSpot = FVector::ZeroVector;
	FVector DemoOrigin = FVector::ZeroVector;

	UPROPERTY()
	TObjectPtr<UTexture2D> FieldTexture;

	UPROPERTY()
	TObjectPtr<UStaticMesh> FlameMesh;

	UPROPERTY()
	TObjectPtr<UStaticMesh> PuffMesh;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> FlameMaterial;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> SmokeMaterial;

	UPROPERTY()
	TObjectPtr<AActor> Visuals;

	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> Flames;

	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> Smoke;

	UPROPERTY()
	TArray<TObjectPtr<UPointLightComponent>> Lights;

	UPROPERTY()
	TObjectPtr<ACameraActor> DemoCamera;
};

// Editor helper for tools/build_island.py: the blank burn-field texture the vegetation materials sample.
UCLASS()
class UNREALSOULSLIKE_API USoulsFireLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// Creates (or resets) an uncompressed, unstreamed, mip-less BGRA texture of zeros at PackagePath
	// (editor builds only). The runtime fire writes it through UpdateTextureRegions.
	UFUNCTION(BlueprintCallable, Category = "Fire")
	static UTexture2D* CreateFireFieldTexture(const FString& PackagePath, int32 Resolution = 512);

	// Half the side of the square the field texture covers (cm, centred on the world origin).
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Fire")
	static float GetFireFieldHalfExtent();
};
