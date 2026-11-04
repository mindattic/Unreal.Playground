#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Subsystems/WorldSubsystem.h"
#include "SoulsBlood.generated.h"

class ACameraActor;
class UDecalComponent;
class UInstancedStaticMeshComponent;
class UMaterialInterface;
class UStaticMesh;

// Ludicrous gore (UNR-T56): blood that looks like blood. One call, at the point where the blade meets
// the body, sprays an arterial spurt (a flipbook), a cloud of red mist and a fan of wet droplets that
// fall under gravity, then splashes decals on the ground under the wound, on any wall in the spray's
// path and wherever the fat drops land. Spawns the same frame; everything is pooled and capped.
// Intensity: ~1 a light hit, ~2 a heavy one, ~3 a critical or a kill (more is fine, it's clamped).
// Cosmetic and local: call it on every machine that shows the hit (it does nothing on a dedicated server).
struct FSoulsBlood
{
	static void Splash(UObject* WorldContext, const FVector& Location, const FVector& Direction, float Intensity);
};

// Pool internals (not reflected).
enum class ESoulsBloodKind : uint8 { Spurt, Mist, Drop };

struct FSoulsBloodBit
{
	FVector Pos = FVector::ZeroVector;
	FVector Vel = FVector::ZeroVector;
	FVector Axis = FVector::UpVector;  // spurts: the jet's direction
	FVector Origin = FVector::ZeroVector; // drag-free drops fly an exact arc from here...
	FVector Launch = FVector::ZeroVector; // ...at this velocity
	FVector Impact = FVector::ZeroVector;
	FVector ImpactNormal = FVector::UpVector;
	float Age = 0.f;
	float Life = 1.f;
	float Size = 1.f;        // cm: drop diameter, mist/spurt sprite size
	float Grow = 1.f;        // mist/spurt: size multiplier reached at the end of life
	float Drag = 0.f;
	float Gravity = 0.f;
	float GroundZ = -UE_BIG_NUMBER;
	float ImpactTime = -1.f; // drops that splat a decal where they hit (traced at spawn)
	float Roll = 0.f;
	float Seed = 0.f;
	bool bAlive = false;
};

struct FSoulsBloodPendingDecal
{
	FVector Point = FVector::ZeroVector;
	FVector Normal = FVector::UpVector;
	FVector Along = FVector::ForwardVector;
	float Radius = 20.f;
	float Stretch = 1.f;
	float Delay = 0.f;
};

// The world's one blood pool: three instanced quad layers (spurts, mist, drops) whose instances are
// recycled, plus the decals. Spawned by USoulsBloodSubsystem when play begins; use FSoulsBlood::Splash.
// Art and materials: /Game/Souls/FX/Blood, built by tools/build_blood_fx.py (hard references below, so
// a packaged build cooks them; /Game/Souls is also always cooked).
UCLASS(NotPlaceable, Transient)
class UNREALSOULSLIKE_API ASoulsBloodPool : public AActor
{
	GENERATED_BODY()

public:
	ASoulsBloodPool();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	void Splash(const FVector& Location, const FVector& Direction, float Intensity);

	int32 NumLiveDecals() const;
	int32 NumLiveParticles() const;
	bool HasArt() const { return bReady; }

private:
	void BuildQuad();
	void Allocate(UInstancedStaticMeshComponent* Layer, TArray<FSoulsBloodBit>& Bits, int32 Count);
	FSoulsBloodBit* Claim(TArray<FSoulsBloodBit>& Bits, int32& Cursor);
	void Simulate(TArray<FSoulsBloodBit>& Bits, ESoulsBloodKind Kind, float DeltaSeconds);
	void Upload(UInstancedStaticMeshComponent* Layer, TArray<FSoulsBloodBit>& Bits, ESoulsBloodKind Kind, bool& bWasLive);
	void SpawnDecal(const FSoulsBloodPendingDecal& Decal);
	bool TraceSurface(const FVector& From, const FVector& To, FHitResult& Hit) const;
	void TraceDropPath(FSoulsBloodBit& Drop) const;
	void ViewBasis();

	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> SpurtLayer;

	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> MistLayer;

	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> DropLayer;

	UPROPERTY()
	TObjectPtr<UStaticMesh> QuadMesh;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> SpurtMaterial;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> MistMaterial;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> DropMaterial;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> DecalMaterial;

	TArray<FSoulsBloodBit> Spurts;
	TArray<FSoulsBloodBit> Mists;
	TArray<FSoulsBloodBit> Drops;
	int32 SpurtCursor = 0;
	int32 MistCursor = 0;
	int32 DropCursor = 0;
	bool bSpurtsLive = false;
	bool bMistsLive = false;
	bool bDropsLive = false;

	TArray<FSoulsBloodPendingDecal> PendingDecals;
	TArray<TWeakObjectPtr<UDecalComponent>> Decals;
	int32 DecalSort = 0;

	// Where idle instances are parked (a speck at the last splash, so the layers' bounds stay local).
	FVector Park = FVector::ZeroVector;
	FVector CamLocation = FVector::ZeroVector;
	FVector CamUp = FVector::UpVector;
	bool bReady = false;
};

// Owns the world's ASoulsBloodPool (spawned as play begins, so the first hit doesn't pay for it), and
// runs the -BloodDemo check: on Lvl_Isle it splashes light, heavy and critical hits in front of the
// player under a fixed camera, screenshots each mid-spurt and the decals after into
// Saved/Screenshots/BloodDemo_*.png, logs "BloodDemo:" lines ending in BLOOD_DEMO_OK and quits.
UCLASS()
class UNREALSOULSLIKE_API USoulsBloodSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	static USoulsBloodSubsystem* Get(const UObject* WorldContext);

	ASoulsBloodPool* GetPool();

	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override { return bDemo; }

private:
	void DemoShot(const TCHAR* Name);
	void DemoFrame(const FVector& From, const FVector& At, float Fov);

	UPROPERTY()
	TObjectPtr<ASoulsBloodPool> Pool;

	UPROPERTY()
	TObjectPtr<ACameraActor> DemoCamera;

	bool bDemo = false;
	int32 DemoStep = 0;
	double DemoStart = 0.0;
	double StepStart = 0.0;
	float DemoWarmup = 15.f;
	FVector DemoPoint = FVector::ZeroVector;
	FVector DemoForward = FVector::ForwardVector;
	FVector DemoRight = FVector::RightVector;
	FVector DemoGround = FVector::ZeroVector;
};
