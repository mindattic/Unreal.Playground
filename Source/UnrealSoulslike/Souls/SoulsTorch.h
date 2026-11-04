#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"
#include "Subsystems/WorldSubsystem.h"
#include "SoulsTorch.generated.h"

class ASoulsKnight;
class ASoulsEnemy;
class UMaterialInterface;
class UNiagaraComponent;
class UNiagaraSystem;
class UPointLightComponent;
class UStaticMesh;
class UStaticMeshComponent;

// A cheap fire: a few glowing, flickering blobs that always rise (world-up), whichever way their
// parent is turned. The fallback for a torch's head and a knight set alight when the Niagara fire
// (UNR-T51, tools/build_torch_fire.py) hasn't been built into Content/ yet.
USTRUCT()
struct FSoulsFlames
{
	GENERATED_BODY()

	// Turned to the world (absolute rotation), so the parts' offsets are world-aligned.
	UPROPERTY(Transient)
	TObjectPtr<USceneComponent> Anchor;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> Parts;

	TArray<FVector> BaseScale;
	TArray<FVector> BaseOffset;
	float Phase = 0.f;

	// Size 1 = a torch head; a burning knight uses about 2.5.
	void Build(AActor* Owner, USceneComponent* Parent, FName Socket, float Size);
	// Lean = where the flames trail (world space, cm), e.g. against the way the fire is moving.
	void Flicker(float DeltaSeconds, const FVector& Lean);
	void SetVisible(bool bVisible);
	void DestroyParts();
};

UENUM()
enum class ESoulsTorchState : uint8
{
	Mounted,   // in an iron bracket on a wall: take it with Interact
	Held,      // in a knight's hand
	Flying,    // thrown or falling
	Ground,    // lying where it landed, still burning: pick it up with Interact
	Out,       // burnt out or drowned: gone soon
};

// UNR-T40. A burning torch: a point light that flickers over Niagara flames, embers and smoke (UNR-T51:
// a tall tongue in a hand or bracket, a wider little fire when it lies on the ground; always rising
// world-up, leaning away from the way the head moves). Falls back to flame blobs without the FX assets.
// - Some enemies carry one in the off hand instead of a shield (USoulsTorchSubsystem arms them).
// - Torches burn in brackets on the ruined keep's and the broch's walls (TargetPoints tagged "Torch",
//   placed by tools/build_island.py). Interact takes one: it becomes the main-hand weapon, the sword is
//   stowed (hidden), light attacks swing the torch and set foes alight, heavy attack throws it.
// - Anything that needs the sword or another item (a riposte/backstab, a flask, the grapple, a brace)
//   drops it; it falls and keeps burning on the ground, where it can be picked up again.
// - A slain torch-bearer drops its torch the same way.
// Where a dropped or thrown torch lands it calls FSoulsFire::IgniteAt (UNR-T41) when that exists.
UCLASS()
class UNREALSOULSLIKE_API ASoulsTorch : public AActor
{
	GENERATED_BODY()

public:
	ASoulsTorch();

	// ---- Knight hooks (called from SoulsKnight.cpp / SoulsKnightAbilities.cpp) ----

	// The torch this knight holds in either hand, or null.
	static ASoulsTorch* GetHeld(const ASoulsKnight* Knight);
	// The torch this knight wields in place of its sword, or null (enemies carry theirs in the off hand).
	static ASoulsTorch* GetWielded(const ASoulsKnight* Knight);
	// The knight reaches for the sword or another item: a wielded torch falls (burning) to the ground.
	static void DropWielded(ASoulsKnight* Knight);
	// A light swing's hit moment. True if the knight swings a torch (the sword sweep must be skipped).
	static bool HandleSwingHit(ASoulsKnight* Knight, bool bHeavy);
	// The torch Interact would take (a lit torch in a bracket or on the ground within reach), or null.
	static ASoulsTorch* FindPickupFor(const ASoulsKnight* Knight);
	// Interact: take the nearest torch in reach into the main hand. False if there is none.
	static bool TryPickUp(ASoulsKnight* Knight);

	// Spawns a lit torch; Mount/LayOnGround/GiveTo place it.
	static ASoulsTorch* SpawnTorch(UWorld* World, const FVector& Location);

	// Fix this torch in a wall bracket. WallPoint is on the wall's face, Out points away from the wall.
	void Mount(const FVector& WallPoint, const FVector& Out);
	// Lay it on the ground at Location (traced down to the surface), pointing along Direction.
	void LayOnGround(const FVector& Location, const FVector& Direction);
	// Put it in Knight's hand: the main (sword) hand hides the sword, the off hand hides the shield.
	void GiveTo(ASoulsKnight* Knight, bool bInMainHand);
	// Let go: it falls from the hand and lands burning.
	void Drop();
	// Hurl it in an arc at Target (or straight ahead when null): damages and ignites what it strikes,
	// then lands burning. Does nothing unless a knight is holding it.
	void Throw(ASoulsKnight* Target);
	// Burnt out or drowned.
	void Extinguish();

	ESoulsTorchState GetState() const { return State; }
	bool IsLit() const { return bLit; }
	bool IsMounted() const { return State == ESoulsTorchState::Mounted; }
	bool IsInMainHand() const { return State == ESoulsTorchState::Held && bMainHand; }
	ASoulsKnight* GetHolder() const { return Holder.Get(); }
	// The burning end, in world space.
	FVector GetHeadLocation() const;

	virtual void Tick(float DeltaSeconds) override;

	// Hard references so the packaged game cooks them.
	UPROPERTY()
	TObjectPtr<UStaticMesh> CylinderMesh;

	UPROPERTY()
	TObjectPtr<UStaticMesh> SphereMesh;

	UPROPERTY()
	TObjectPtr<UStaticMesh> CubeMesh;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> FlameCoreMaterial;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> FlameMaterial;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> IronMaterial;

	// UNR-T51 fire (tools/build_torch_fire.py); null until that has run, then the blobs are used.
	UPROPERTY()
	TObjectPtr<UNiagaraSystem> FlameSystem;

	UPROPERTY()
	TObjectPtr<UNiagaraSystem> GroundFlameSystem;

	UPROPERTY()
	TObjectPtr<UNiagaraSystem> EmberSystem;

	UPROPERTY()
	TObjectPtr<UNiagaraSystem> SmokeSystem;

	// True when the Niagara fire assets were found (the class default's view).
	static bool HasFireFx();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	// Lets go of the holder (sword or shield back on show) without deciding where the torch goes.
	void Release();
	void SwingHit(ASoulsKnight* Knight);
	void TickFlight(float DeltaSeconds);
	void Land(const FHitResult& Hit);
	void StrikeKnight(ASoulsKnight* Victim, const FVector& Impact);
	// Places the actor so the shaft's middle is at Center with the given shaft axis.
	void SetCenterAndAxis(const FVector& Center, const FVector& Axis);
	FVector GetCenter() const;
	FVector GetAxis() const;
	// Switches the Niagara fire between held/wall flames, a ground fire and out, as the state demands.
	void RefreshFire();

	UPROPERTY(VisibleAnywhere, Category = "Torch")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, Category = "Torch")
	TObjectPtr<UStaticMeshComponent> Shaft;

	UPROPERTY(VisibleAnywhere, Category = "Torch")
	TObjectPtr<UStaticMeshComponent> Head;

	// The flames hang off this; it never turns, so they always rise.
	UPROPERTY(VisibleAnywhere, Category = "Torch")
	TObjectPtr<USceneComponent> FlameRoot;

	UPROPERTY(VisibleAnywhere, Category = "Torch")
	TObjectPtr<UPointLightComponent> Light;

	// Niagara fire on FlameRoot (UNR-T51): the flames in a hand / bracket / flight, the little fire on
	// the ground, and the embers and smoke both share.
	UPROPERTY(VisibleAnywhere, Category = "Torch")
	TObjectPtr<UNiagaraComponent> FlameFx;

	UPROPERTY(VisibleAnywhere, Category = "Torch")
	TObjectPtr<UNiagaraComponent> GroundFlameFx;

	UPROPERTY(VisibleAnywhere, Category = "Torch")
	TObjectPtr<UNiagaraComponent> EmberFx;

	UPROPERTY(VisibleAnywhere, Category = "Torch")
	TObjectPtr<UNiagaraComponent> SmokeFx;

	UPROPERTY(Transient)
	FSoulsFlames Flames;

	// The wall bracket left behind when a mounted torch is taken.
	UPROPERTY(Transient)
	TObjectPtr<AActor> Bracket;

	TWeakObjectPtr<ASoulsKnight> Holder;
	TWeakObjectPtr<ASoulsKnight> Thrower;
	TSet<TWeakObjectPtr<AActor>> StruckThisFlight;
	ESoulsTorchState State = ESoulsTorchState::Ground;
	bool bMainHand = false;
	bool bLit = true;
	bool bThrown = false;
	bool bHolderWasAI = false;
	uint8 ThrowerTeam = 0;
	FVector Velocity = FVector::ZeroVector;
	FVector SpinAxis = FVector::RightVector;
	float SpinSpeed = 0.f;
	float FlightTime = 0.f;
	float BurnLeft = 0.f;
	float LightPhase = 0.f;
	FVector LastHeadLocation = FVector::ZeroVector;
	FVector FlameLean = FVector::ZeroVector;
	// 0 = out, 1 = flames (hand, bracket, flight), 2 = ground fire; 255 = not set up yet.
	uint8 FireLook = 255;
};

// A knight set alight: small flames on the body and a little health lost every half second, until it
// burns out or the knight reaches water. Igniting again restarts the clock.
UCLASS()
class UNREALSOULSLIKE_API USoulsBurnComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USoulsBurnComponent();

	// Set Victim burning for Seconds (Source is credited with the damage).
	static void Ignite(ASoulsKnight* Victim, AActor* InSource, float Seconds = 5.f);
	static bool IsBurning(const ASoulsKnight* Knight);

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;

private:
	void Stop();

	UPROPERTY(Transient)
	FSoulsFlames Flames;

	UPROPERTY(Transient)
	TObjectPtr<UPointLightComponent> Glow;

	// The Niagara fire on the body (UNR-T51), when the assets exist (otherwise Flames).
	UPROPERTY(Transient)
	TArray<TObjectPtr<UNiagaraComponent>> Fx;

	TWeakObjectPtr<AActor> Source;
	float Remaining = 0.f;
	float TickAccumulator = 0.f;
	float Phase = 0.f;
	FVector LastLocation = FVector::ZeroVector;
};

// Spawns the wall torches (TargetPoints tagged "Torch"), hands some island knights a torch as they
// spawn, and runs the -TorchDemo verification.
UCLASS()
class UNREALSOULSLIKE_API USoulsTorchSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	// Give this enemy a torch in its off hand (in place of its shield).
	static ASoulsTorch* ArmEnemy(ASoulsEnemy* Enemy);

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	void OnActorSpawned(AActor* Actor);
	void ConsiderTorchBearer(TWeakObjectPtr<ASoulsEnemy> WeakEnemy);
	// -TorchDemo: picks up, swings, drops, throws, sees a torch-bearer fall and takes a wall torch,
	// screenshotting each beat (Saved/Screenshots/TorchDemo_*) and logging "Torch check:" PASS/FAIL.
	void RunDemo();

	FDelegateHandle SpawnHandle;
	bool bArmEnemies = true;
};
