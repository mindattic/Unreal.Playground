#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "Subsystems/WorldSubsystem.h"
#include "SoulsFighter.generated.h"

class ASoulsKnight;
class AHUD;
class UAudioComponent;
class UCanvas;
class UCameraComponent;
class UInputAction;
class UInputMappingContext;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UPointLightComponent;
class USoundAttenuation;
class USoundWaveProcedural;
class USphereComponent;
class UStaticMesh;
class UStaticMeshComponent;
struct FInputActionValue;

// The fighter's synthesised one-shot sounds (SoulsFighter.cpp, FighterAudio).
enum class EFighterSound : uint8 { Laser, DroneLaser, Missile, Explosion, Scrape, Canopy, LockTone, Boost, Zap, Count };

UENUM(BlueprintType)
enum class EFighterState : uint8
{
	Parked,     // empty, sitting on its landing legs
	Boarding,   // canopy up, the knight is climbing in
	Piloted,    // the player flies it (see EFighterFlight)
	Exiting,    // the pilot is climbing down
	Wrecked,    // hull gone: a smoking wreck until it is repaired back on its pad
	Drone,      // an AI drone fighter (never boardable)
};

UENUM(BlueprintType)
enum class EFighterFlight : uint8
{
	Grounded,   // on its legs (parked, or piloted before take-off / after landing)
	Lifting,    // take-off: vertical lift on the belly thrusters, legs folding up
	Flying,     // free flight
	Landing,    // auto-land: level out, bleed speed, sink straight down onto the legs
};

// UNR-T69. A star fighter parked on the island (Quaternius's CC0 "Striker" from the Ultimate Spaceships Pack, split into
// hull, canopy and landing legs by tools/convert_fighter.py) that the knight can climb into and fly. Its own flight model
// (no CharacterMovement): throttle, boost and brake; pitch and yaw on the stick (or the mouse as a self-centring virtual
// stick); full free roll (barrel rolls, inverted flight) on the left stick X / A-D; banking into turns; and when the guns
// are not firing the ship smoothly rolls itself back to level with the horizon. Vertical take-off and auto-landing on the
// legs; terrain and sea contact scrapes, bounces and damages the hull. Weapons: alternating twin lasers (bolts with muzzle
// flashes, impacts and light) and lock-on homing missiles. Chase camera with lag and a FOV kick on boost (R3 / V for the
// cockpit view), and a HUD with reticle, lead indicators on targets, speed, altitude, throttle, boost and hull.
// A few AI drone fighters (Quaternius's "Bob") circle the island and come for you as soon as you are airborne.
// Interact (D-pad up / F) next to it to climb in (ASoulsFighter::TryBoard, called from the knight); in the air, low and
// slow, the same button lands it; landed, it climbs out beside it.
// -FighterDemo (with -RenderOffscreen) boards, takes off, flies, rolls inverted, checks the auto-level (and that firing holds
// the roll), shoots a drone, lands and climbs out, screenshotting each beat (Saved/Screenshots/Fighter_*) and logging
// PASS/FAIL.
UCLASS()
class UNREALSOULSLIKE_API ASoulsFighter : public APawn
{
	GENERATED_BODY()

public:
	ASoulsFighter();

	// ---- Boarding: the knight's interact calls this; true = it is climbing into a fighter. ----
	static bool TryBoard(ASoulsKnight* Knight);
	static ASoulsFighter* FindBoardable(const ASoulsKnight* Knight);
	bool CanBoard(const ASoulsKnight* Knight) const;
	bool Board(ASoulsKnight* Knight);
	// D-pad up / F while piloting: land when low and slow, climb out when landed.
	void InteractInFlight();
	void Disembark();
	void BeginLanding();
	void BeginTakeOff();

	EFighterState GetState() const { return State; }
	EFighterFlight GetFlight() const { return Flight; }
	ASoulsKnight* GetPilot() const { return Pilot.Get(); }
	bool IsPiloted() const { return State == EFighterState::Piloted; }
	bool IsDrone() const { return bDrone; }
	bool IsAirborne() const { return (State == EFighterState::Piloted || State == EFighterState::Drone) && (Flight == EFighterFlight::Flying || Flight == EFighterFlight::Lifting || Flight == EFighterFlight::Landing); }
	bool IsAlive() const { return State != EFighterState::Wrecked && !bDroneDown; }
	float GetHullFraction() const { return MaxHull > 0.f ? FMath::Max(Hull, 0.f) / MaxHull : 0.f; }
	float GetHull() const { return Hull; }
	float GetForwardSpeed() const { return FlightSpeed; }
	float GetThrottle() const { return ThrottleSetting; }
	FVector GetFlightVelocity() const { return FlightVelocity; }
	// Roll off the horizon in degrees: 0 level, +-180 inverted (positive = right wing up).
	float GetRollAngle() const;
	float GetAltitudeAboveGround() const { return GroundClearance; }
	int32 GetBoltsFired() const { return BoltsFired; }
	int32 GetMissilesFired() const { return MissilesFired; }
	int32 GetHitsLanded() const { return HitsLanded; }
	int32 GetKills() const { return Kills; }
	int32 GetScrapes() const { return Scrapes; }
	FTransform GetHomeTransform() const { return HomeTransform; }
	float GetGearAlpha() const { return GearAlpha; }
	float GetCanopyAlpha() const { return CanopyAlpha; }
	// The target the missiles are locked onto (none until the lock tone goes solid).
	AActor* GetLockedTarget() const { return LockProgress >= 1.f ? LockedTarget.Get() : nullptr; }

	// Flight inputs (the player's actions, or -FighterDemo / the drone brain directly), all -1..1.
	void SetStickInput(float Pitch, float Yaw) { StickPitch = Pitch; StickYaw = Yaw; }
	void SetRollInput(float Roll) { RollInput = Roll; }
	void SetRudderInput(float Rudder) { RudderInput = Rudder; }
	void SetThrottleInput(float Delta) { ThrottleInput = Delta; }
	void SetThrottleSetting(float Value) { ThrottleSetting = FMath::Clamp(Value, 0.f, 1.f); }
	void SetBoost(bool bOn) { bBoostHeld = bOn; }
	void SetBrake(bool bOn) { bBrakeHeld = bOn; }
	void SetFiring(bool bOn) { bFireHeld = bOn; }
	void FireMissile();
	void ToggleView();
	// Teleport into free flight (demo staging): level, heading Yaw, at Speed.
	void PlaceInFlight(const FVector& Where, float Yaw, float Speed);
	// Point the nose straight at Where (demo staging).
	void FaceToward(const FVector& Where);
	// Drones: fly straight on and hold fire for Seconds (demo staging).
	void HoldCourse(float Seconds);

	// Hull damage from any source (lasers, missiles, terrain, TakeDamage, a demo).
	void ApplyHullDamage(float Amount, AActor* Causer, const FVector& Where);

	// ---- Tuning ----
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fighter|Flight")
	float CruiseSpeed = 4200.f;    // cm/s at full throttle

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fighter|Flight")
	float BoostSpeed = 9000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fighter|Flight")
	float BrakeSpeed = 700.f;      // the brake pulls the speed down to this (it hovers below it)

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fighter|Flight")
	float Acceleration = 2200.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fighter|Flight")
	float Deceleration = 3000.f;

	// Degrees/s at full stick: pitch, yaw, roll.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fighter|Flight")
	FVector TurnRates = FVector(95.f, 55.f, 240.f);

	// How fast the angular rates follow the stick (1/s).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fighter|Flight")
	float Agility = 7.f;

	// How hard the velocity follows the nose (1/s; lower = more drift).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fighter|Flight")
	float Grip = 4.f;

	// Bank into a full-stick turn (degrees) and the auto-level's roll rate cap (degrees/s).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fighter|Flight")
	float BankAngle = 40.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fighter|Flight")
	float AutoLevelRate = 140.f;

	// Seconds after the last shot before the auto-level takes the roll back.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fighter|Flight")
	float AutoLevelDelay = 0.45f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fighter|Flight")
	bool bInvertPitch = false;

	// Land only below this speed (cm/s) and this height over the ground (cm).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fighter|Flight")
	FVector2D LandingWindow = FVector2D(1800.f, 3500.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fighter|Hull")
	float MaxHull = 1000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fighter|Hull")
	float RepairSeconds = 45.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fighter|Weapons")
	float LaserDamage = 14.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fighter|Weapons")
	float LaserRate = 10.f;         // bolts per second (both guns together)

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fighter|Weapons")
	float LaserSpeed = 32000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fighter|Weapons")
	float MissileDamage = 120.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fighter|Weapons")
	int32 MaxMissiles = 8;

	// Missile lock: cone half-angle (degrees), range (cm), seconds to lock.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fighter|Weapons")
	FVector LockParams = FVector(14.f, 30000.f, 0.9f);

	// ---- Drones (spawned by the player's fighter at BeginPlay; they circle CentreOfIsland) ----
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fighter|Drones")
	int32 DroneCount = 3;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fighter|Drones")
	FVector DroneOrbitCentre = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fighter|Drones")
	float DroneOrbitRadius = 15000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fighter|Drones")
	float DroneRespawnSeconds = 25.f;

	// ---- Parts (meshes from /Game/Island/Models/Fighter, built by tools/build_island.py) ----
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Fighter")
	TObjectPtr<USphereComponent> Body;

	// Carries the meshes: the visual bank into turns rides on it.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Fighter")
	TObjectPtr<USceneComponent> Frame;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Fighter")
	TObjectPtr<UStaticMeshComponent> HullMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Fighter")
	TObjectPtr<UStaticMeshComponent> CanopyMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Fighter")
	TArray<TObjectPtr<UStaticMeshComponent>> GearMeshes;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Fighter")
	TArray<TObjectPtr<UStaticMeshComponent>> EngineGlows;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Fighter")
	TObjectPtr<UPointLightComponent> EngineLight;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Fighter")
	TArray<TObjectPtr<UStaticMeshComponent>> MuzzleFlashes;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Fighter")
	TArray<TObjectPtr<UPointLightComponent>> MuzzleLights;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Fighter")
	TObjectPtr<UCameraComponent> ChaseCamera;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Fighter")
	TObjectPtr<UCameraComponent> CockpitCamera;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Fighter")
	TObjectPtr<UAudioComponent> EngineAudio;

	virtual void Tick(float DeltaSeconds) override;
	virtual float TakeDamage(float Damage, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

	// Spawn an AI drone fighter (not placed in the editor: the player's fighter brings its own).
	static ASoulsFighter* SpawnDrone(UWorld* World, const FVector& Where, float Yaw, ASoulsFighter* InLeader, int32 Index);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void NotifyControllerChanged() override;

private:
	// ---- Input ----
	void EnsureFighterInput();
	void OnThrottle(const FInputActionValue& Value);
	void OnThrottleStop(const FInputActionValue& Value);
	void OnRoll(const FInputActionValue& Value);
	void OnRollStop(const FInputActionValue& Value);
	void OnRudder(const FInputActionValue& Value);
	void OnRudderStop(const FInputActionValue& Value);
	void OnLookMouse(const FInputActionValue& Value);
	void OnLookStick(const FInputActionValue& Value);
	void OnLookStickStop(const FInputActionValue& Value);
	void OnFireStart(const FInputActionValue& Value);
	void OnFireStop(const FInputActionValue& Value);
	void OnMissile(const FInputActionValue& Value);
	void OnBoostStart(const FInputActionValue& Value);
	void OnBoostStop(const FInputActionValue& Value);
	void OnBrakeStart(const FInputActionValue& Value);
	void OnBrakeStop(const FInputActionValue& Value);
	void OnInteract(const FInputActionValue& Value);
	void OnView(const FInputActionValue& Value);

	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> FighterContext;
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> ThrottleAction;
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> RollAction;
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> RudderAction;
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> LookMouseAction;
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> LookStickAction;
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> FireAction;
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> MissileAction;
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> BoostAction;
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> BrakeAction;
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> InteractAction;
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> ViewAction;

	// ---- Assembly ----
	void LoadParts();
	void ApplyDroneLook();
	void BuildPools();

	// ---- Sequences and flight ----
	void TickSequence(float DeltaSeconds);
	void TickGrounded(float DeltaSeconds);
	void TickLifting(float DeltaSeconds);
	void TickFlying(float DeltaSeconds);
	void TickLanding(float DeltaSeconds);
	void TickParts(float DeltaSeconds);
	void TickCamera(float DeltaSeconds);
	void TickEngineSound(float DeltaSeconds);
	void Integrate(float DeltaSeconds, float PitchCmd, float YawCmd, float RollCmd, bool bAllowLevel);
	void MoveWithCollision(float DeltaSeconds);
	void Contact(const FVector& Point, const FVector& Normal, AActor* Other, bool bWater);
	void SettleOnGround(float DeltaSeconds, bool bSnap);
	bool GroundBelow(const FVector& From, FVector& OutPoint, FVector& OutNormal, float Depth = 60000.f) const;
	void UpdateClearance();
	void Shake(float Strength);
	FVector SeatLocation() const;
	float FloorOffset() const;   // the legs' floor (or the drone's belly) below the origin, cm (negative)

	// ---- Drone brain ----
	void TickDroneBrain(float DeltaSeconds);
	ASoulsFighter* FindPlayerTarget() const;
	void DroneDown();
	void DroneRespawn();
	FVector LeadPoint(const AActor* Target, const FVector& From, float ProjectileSpeed) const;
	TWeakObjectPtr<ASoulsFighter> Leader;
	float DroneAngle = 0.f;      // its slot on the orbit
	float DroneAltitude = 9000.f;
	float DroneBreakUntil = -100.f;
	float DroneHoldUntil = -100.f;
	float DroneDownAt = -100.f;
	FVector DroneBreakDir = FVector::UpVector;
	bool bDroneDown = false;

	// ---- Weapons ----
	struct FFighterBolt
	{
		bool bMissile = false;
		bool bEnemy = false;
		FVector Location = FVector::ZeroVector;
		FVector Velocity = FVector::ZeroVector;
		float Age = 0.f;
		float Life = 1.2f;
		float NextPuff = 0.f;
		TWeakObjectPtr<AActor> Homing;
		int32 Visual = INDEX_NONE;
		int32 Light = INDEX_NONE;
	};
	struct FFighterPuff
	{
		int32 Visual = INDEX_NONE;
		FVector Location = FVector::ZeroVector;
		FVector Velocity = FVector::ZeroVector;
		float Age = 0.f;
		float Life = 1.f;
		float Size = 1.f;
		float Grow = 1.f;
		float Opacity = 0.6f;
		bool bFire = false;
	};
	TArray<FFighterBolt> Bolts;
	TArray<FFighterPuff> Puffs;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> BoltPool;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> PuffPool;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> PuffMaterials;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UPointLightComponent>> LightPool;
	TArray<float> LightFade;
	TArray<float> LightFadeTotal;
	TArray<float> LightBase;
	TBitArray<> BoltPoolUsed;
	TBitArray<> PuffPoolUsed;

	int32 TakeBoltVisual(bool bMissile);
	int32 TakeLight(const FVector& Where, const FLinearColor& Color, float Intensity, float Radius, float Fade);
	void SpawnPuff(const FVector& Where, const FVector& Velocity, float Size, float Grow, float Life, float Opacity, bool bFire);
	void TickWeapons(float DeltaSeconds);
	void TickBolts(float DeltaSeconds);
	void TickPuffs(float DeltaSeconds);
	void TickLock(float DeltaSeconds);
	void FireLaser();
	void BoltImpact(const FFighterBolt& Bolt, const FHitResult& Hit);
	void Explode(const FVector& Where, const FVector& Normal, float Scale, bool bDamage);
	bool DamageActor(AActor* Victim, float Damage, const FVector& Where, const FVector& Direction, float Push);
	FVector MuzzleLocation(int32 Gun) const;
	FVector AimPoint(AActor** OutAssist = nullptr) const;
	bool IsHostileTo(const AActor* Other) const;

	// ---- Wreck ----
	void Wreck();
	void Repair();
	void EjectPilot(bool bHurt);

	// ---- HUD ----
	static void DrawFighterHUD(AHUD* HUD, UCanvas* Canvas);
	void DrawCockpitHUD(UCanvas* Canvas) const;
	static FDelegateHandle HUDHandle;
	static int32 HUDUsers;

	// ---- Sound ----
	void PlayFighterSound(EFighterSound Sound, const FVector& Where, float Volume = 1.f, float Pitch = 1.f) const;
	UPROPERTY(Transient)
	TObjectPtr<USoundAttenuation> SoundFalloff;
	UPROPERTY(Transient)
	TObjectPtr<USoundWaveProcedural> EngineWave;

	// ---- State ----
	UPROPERTY(EditAnywhere, Category = "Fighter")
	bool bDrone = false;
	EFighterState State = EFighterState::Parked;
	EFighterFlight Flight = EFighterFlight::Grounded;
	TWeakObjectPtr<ASoulsKnight> Pilot;
	float SequenceT = 0.f;
	float PhaseT = 0.f;
	FVector ClimbFrom = FVector::ZeroVector;
	FVector ClimbTo = FVector::ZeroVector;
	FVector LiftFrom = FVector::ZeroVector;
	FTransform HomeTransform;
	bool bHomeSet = false;

	// Inputs
	float StickPitch = 0.f;
	float StickYaw = 0.f;
	FVector2D MouseStick = FVector2D::ZeroVector;
	FVector2D PadStick = FVector2D::ZeroVector;
	float RollInput = 0.f;
	float RudderInput = 0.f;
	float ThrottleInput = 0.f;
	bool bBoostHeld = false;
	bool bBrakeHeld = false;
	bool bFireHeld = false;
	bool bCockpitView = false;

	// Flight
	FQuat FlightRotation = FQuat::Identity;
	FVector FlightVelocity = FVector::ZeroVector;
	FVector AngularRate = FVector::ZeroVector;   // pitch, yaw, roll rates in deg/s (local)
	float FlightSpeed = 0.f;
	float ThrottleSetting = 0.f;
	float BoostEnergy = 1.f;
	bool bBoosting = false;
	float VisualBank = 0.f;
	float GroundClearance = 0.f;  // cm from the legs' floor to the ground below
	float GearAlpha = 1.f;        // 1 = legs down
	float GearTarget = 1.f;
	float CanopyAlpha = 0.f;
	float LastShotTime = -100.f;
	float LastContactTime = -100.f;
	float LastBoostSound = -100.f;
	float PullUpWarning = 0.f;
	FString FlashMessage;
	float FlashMessageUntil = -100.f;

	// Camera
	FQuat CameraRotation = FQuat::Identity;
	FVector CameraLocation = FVector::ZeroVector;
	bool bCameraPlaced = false;
	float CameraFov = 80.f;
	float ShakeAmount = 0.f;
	float ShakeTime = 0.f;

	// Weapons
	int32 NextGun = 0;
	float LaserCooldown = 0.f;
	int32 Missiles = 8;
	float MissileCooldown = 0.f;
	float MuzzleFlashT[2] = { 0.f, 0.f };
	TWeakObjectPtr<AActor> LockCandidate;
	TWeakObjectPtr<AActor> LockedTarget;
	float LockProgress = 0.f;
	float LastLockTone = -100.f;

	// Hull
	float Hull = 1000.f;
	float WreckedAt = -100.f;
	float LastHitTime = -100.f;
	float LastHitMarker = -100.f;
	FVector LastHitFrom = FVector::ZeroVector;

	// Counters (demo checks)
	int32 BoltsFired = 0;
	int32 MissilesFired = 0;
	int32 HitsLanded = 0;
	int32 Kills = 0;
	int32 Scrapes = 0;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> HullMaterials;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> BoltMaterial;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> EnemyBoltMaterial;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> EngineMaterial;
	// Not transient: set in the editor (OnConstruction), saved with the map so a packaged game cooks them.
	UPROPERTY()
	TObjectPtr<UMaterialInterface> GlowMaterial;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> SmokeMaterial;
	UPROPERTY()
	TObjectPtr<UStaticMesh> SphereMesh;
	UPROPERTY()
	TObjectPtr<UStaticMesh> DroneMesh;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> DroneMaterial;
	bool bPartsLoaded = false;
	bool bRealArt = false;
};

// -FighterDemo: stages and checks the whole fighter loop on whatever map is loaded (spawns a fighter beside the player if
// the map has none), screenshotting each beat and logging "FighterDemo: ... PASS/FAIL" lines, then FIGHTER_DEMO_DONE and
// quits.
UCLASS()
class UNREALSOULSLIKE_API UFighterDemoSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

private:
	void Run();
	void Check(const TCHAR* What, bool bPass, const FString& Detail);
	void Key(const FKey& InKey, bool bDown);
	ASoulsFighter* Fighter() const { return DemoFighter.Get(); }
	ASoulsKnight* Knight() const { return DemoKnight.Get(); }

	TWeakObjectPtr<ASoulsFighter> DemoFighter;
	TWeakObjectPtr<ASoulsKnight> DemoKnight;
	TWeakObjectPtr<ASoulsFighter> DemoDrone;
	FVector StartLocation = FVector::ZeroVector;
	float StartValue = 0.f;
	float MinUpZ = 1.f;
	float MaxRollWhileFiring = 0.f;
	int32 HitsBefore = 0;
	int32 Passes = 0;
	int32 Fails = 0;
	FTimerHandle WatchHandle;
};
