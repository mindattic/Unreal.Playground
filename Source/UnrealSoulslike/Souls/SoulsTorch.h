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

UENUM()
enum class ESoulsTorchState : uint8
{
	Mounted,   // in an iron bracket on a wall: take it with Interact
	Held,      // in a knight's hand
	Flying,    // thrown or falling
	Ground,    // lying where it landed, still burning: pick it up with Interact
	Out,       // burnt out or drowned: gone soon
};

// UNR-T40. A burning torch: a point light that flickers over Niagara fire, embers, smoke and heat haze
// (UNR-T51, UNR-T75: flipbooks rendered from a real fire simulation; a tall flame in a hand or bracket,
// a wider fire when it lies on the ground; always rising world-up, leaning away from the way the head
// moves). The torches at rest nearest the camera burn with a live Niagara Fluids 3D gas fire instead
// (souls.Fire.Fluid*). No mesh flames: without the FX assets a torch only glows.
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
	TObjectPtr<UStaticMesh> CubeMesh;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> IronMaterial;

	// UNR-T51/T75 fire: /Game/Souls/FX/Fire/NS_* (tools/build_torch_fire.py), loaded on first use in
	// play, never from this class's constructor. In a packaged (monolithic) build class defaults are
	// built before the Niagara module has started; loading a Niagara system then crashes in
	// UNiagaraStatelessEmitter::Serialize. They're cooked through DirectoriesToAlwaysCook (/Game/Souls).
	// True when they exist (else a torch only glows).
	static bool HasFireFx();
	// The loaded systems (null without them): the flame in a hand / bracket / flight, the ground fire,
	// the embers, the smoke, the heat haze and the Niagara Fluids gas fire (null without the plugin).
	static UNiagaraSystem* GetFlameSystem();
	static UNiagaraSystem* GetGroundFlameSystem();
	static UNiagaraSystem* GetEmberSystem();
	static UNiagaraSystem* GetSmokeSystem();
	static UNiagaraSystem* GetHazeSystem();
	static UNiagaraSystem* GetFluidFireSystem();

	// True while this torch burns with the live gas simulation (one of the few hero fires).
	bool IsFluidFire() const { return bFluidHero; }
	// Picks the hero fires: the lit torches at rest (bracket or ground) nearest the view, up to
	// souls.Fire.FluidCount within souls.Fire.FluidRange. Called from every torch's Tick, runs once a
	// frame at most and re-picks a few times a second.
	static void UpdateFluidHeroes(UWorld* World);

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
	// Switches the Niagara fire between held/wall flames, a ground fire, the hero gas fire and out, as the
	// state demands.
	void RefreshFire();
	// Sizes the gas fire's grid and points its source at the burning head, then starts it.
	void StartFluidFire();

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

	// Heat shimmer over a torch at rest (UNR-T75).
	UPROPERTY(VisibleAnywhere, Category = "Torch")
	TObjectPtr<UNiagaraComponent> HazeFx;

	// The hero fire: Niagara Fluids' 3D gas fire (UNR-T75), only while picked by UpdateFluidHeroes.
	UPROPERTY(VisibleAnywhere, Category = "Torch")
	TObjectPtr<UNiagaraComponent> FluidFx;

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
	bool bLoggedBadTransform = false;
	// Bits: 1 = flames (hand, bracket, flight), 2 = ground fire, 4 = smoke and haze, 8 = gas fire;
	// 255 = not set up yet.
	uint8 FireLook = 255;
	bool bFluidHero = false;
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
	TObjectPtr<UPointLightComponent> Glow;

	// The Niagara fire on the body (UNR-T51), when the assets exist.
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
	// The fire close-ups are shot twice, with the hero gas fire and with the flipbook sprites only
	// (..._fluid / ..._sprites), and a night close-up ends it (TorchDemo_13_night_*; -TimeOfDay=21 at
	// launch shoots the whole demo at night).
	void RunDemo();

	FDelegateHandle SpawnHandle;
	bool bArmEnemies = true;
};
