#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Subsystems/WorldSubsystem.h"
#include "SoulsKnight.h"
#include "SoulsMech.generated.h"

class ASoulsKnight;
class AHUD;
class UCanvas;
class UCameraComponent;
class UInputAction;
class UInputMappingContext;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UPointLightComponent;
class USoundAttenuation;
class USpringArmComponent;
class UStaticMesh;
class UStaticMeshComponent;
struct FInputActionValue;

// The mech's synthesised sounds (SoulsMech.cpp, MechAudio).
enum class EMechSound : uint8 { Footstep, Cannon, MissileLaunch, Explosion, Clang, Servo, Alarm, Count };

UENUM(BlueprintType)
enum class EMechState : uint8
{
	Parked,     // empty, kneeling where it was left
	Boarding,   // the knight is climbing up into the cockpit
	Piloted,    // the player drives it
	Exiting,    // the pilot is climbing down
	Wrecked,    // armour gone: smoking, nobody aboard, rebooting
};

// UNR-T68. A two-legged walking war machine (Quaternius's CC0 "George" mech, rebuilt as rigid parts by
// tools/convert_mech.py) the knight can climb into and drive, in the manner of the classic stompy-robot
// simulators: the legs walk where the left stick / WASD says (throttle and turn, with weight and
// inertia), while the torso twists on its own (right stick / mouse, about +-100 degrees off the legs)
// and the gun arms pitch to the aim. Every step is procedural: each foot plants and lifts on its own,
// solved by a pantograph IK for the bird leg (thigh, shin, metatarsal, foot), the hips bob and roll with
// the gait and dip under each footfall, which shakes the camera, thuds and kicks up dust.
// Weapons: twin arm autocannons (X / LMB / RT, hold) and four-tube shoulder missile pods (Y / RMB / LT),
// on a shared heat scale (overheat locks them out) and an ammo count refilled whenever someone boards.
// Enemies notice it, come for it and hack at its legs; it takes the hits on its armour, is wrecked at
// zero (the pilot is thrown clear) and reboots itself after a while.
// Interact (D-pad up / F) next to it to climb in (ASoulsMech::TryBoard, called from the knight), and
// again inside to climb out beside it. R3 / V swaps between the chase camera and the cockpit view.
// -MechDemo (with -RenderOffscreen) boards, walks, turns, twists, fires both weapons at a target, lets an
// enemy attack and climbs out, screenshotting each beat (Saved/Screenshots/Mech_*) and logging PASS/FAIL.
UCLASS()
class UNREALSOULSLIKE_API ASoulsMech : public ACharacter
{
	GENERATED_BODY()

public:
	ASoulsMech();

	// ---- Boarding: the knight's interact calls this first; true = it is climbing into a mech. ----
	static bool TryBoard(ASoulsKnight* Knight);
	// The mech this knight could board from where it stands (free, not wrecked, in reach), if any.
	static ASoulsMech* FindBoardable(const ASoulsKnight* Knight);
	bool CanBoard(const ASoulsKnight* Knight) const;
	bool Board(ASoulsKnight* Knight);
	void Disembark();

	EMechState GetState() const { return State; }
	ASoulsKnight* GetPilot() const { return Pilot.Get(); }
	bool IsPiloted() const { return State == EMechState::Piloted; }
	float GetArmorFraction() const { return MaxArmor > 0.f ? FMath::Max(Armor, 0.f) / MaxArmor : 0.f; }
	float GetHeatFraction() const { return Heat / 100.f; }
	float GetTorsoYaw() const { return TorsoYaw; }
	float GetForwardSpeed() const { return Speed; }
	int32 GetShellsFired() const { return ShellsFired; }
	int32 GetMissilesFired() const { return MissilesFired; }
	int32 GetHitsLanded() const { return HitsLanded; }
	int32 GetFootfalls() const { return Footfalls; }
	float GetArmor() const { return Armor; }

	// Driving inputs (the player's actions, or -MechDemo directly). Throttle/Turn -1..1 (held);
	// look adds degrees to the torso's twist and the arms' aim.
	void SetDriveInput(float InThrottle, float InTurn) { ThrottleInput = InThrottle; TurnInput = InTurn; }
	void AddAimInput(float YawDegrees, float PitchDegrees);
	void SetTorsoTarget(float Yaw, float Pitch);
	void SetFiring(bool bCannons) { bCannonHeld = bCannons; }
	void FireMissiles();
	void ToggleView();

	// Armour damage from any source (enemy blades, TakeDamage, a demo).
	void ApplyMechDamage(float Amount, AActor* Causer, const FVector& Where);

	// ---- Tuning ----
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mech|Drive")
	float MaxForwardSpeed = 520.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mech|Drive")
	float MaxReverseSpeed = 260.f;

	// cm/s^2: how fast the legs build speed, and how fast they shed it.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mech|Drive")
	float Acceleration = 300.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mech|Drive")
	float Deceleration = 520.f;

	// Leg turn rate, degrees/s standing still and at full speed.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mech|Drive")
	FVector2D TurnRate = FVector2D(62.f, 34.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mech|Torso")
	float TorsoTwistLimit = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mech|Torso")
	float TorsoTwistRate = 130.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mech|Torso")
	FVector2D AimPitchLimits = FVector2D(-24.f, 22.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mech|Armour")
	float MaxArmor = 1400.f;

	// Enemy blades are scaled by this against the plating.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mech|Armour")
	float MeleeArmorFactor = 0.8f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mech|Armour")
	float RebootSeconds = 40.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mech|Weapons")
	float CannonDamage = 16.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mech|Weapons")
	float CannonRate = 7.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mech|Weapons")
	float CannonHeat = 3.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mech|Weapons")
	int32 MaxCannonAmmo = 600;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mech|Weapons")
	float MissileDamage = 70.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mech|Weapons")
	float MissileRadius = 420.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mech|Weapons")
	float MissileSalvoHeat = 24.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mech|Weapons")
	int32 MaxMissiles = 32;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mech|Weapons")
	float HeatDissipation = 16.f;

	// ---- Parts (meshes from /Game/Island/Models/Mech, built by tools/build_island.py) ----
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mech")
	TObjectPtr<USceneComponent> Chassis;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mech")
	TObjectPtr<UStaticMeshComponent> PelvisMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mech")
	TObjectPtr<UStaticMeshComponent> TorsoMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mech")
	TObjectPtr<UStaticMeshComponent> CanopyMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mech")
	TObjectPtr<UStaticMeshComponent> ArmLMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mech")
	TObjectPtr<UStaticMeshComponent> ArmRMesh;

	// Per leg (left then right): thigh, shin, metatarsal, foot.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mech")
	TArray<TObjectPtr<UStaticMeshComponent>> LegParts;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mech")
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mech")
	TObjectPtr<UCameraComponent> ChaseCamera;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mech")
	TObjectPtr<UCameraComponent> CockpitCamera;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mech")
	TArray<TObjectPtr<UPointLightComponent>> MuzzleLights;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mech")
	TArray<TObjectPtr<UStaticMeshComponent>> MuzzleFlashes;

	virtual void Tick(float DeltaSeconds) override;
	virtual float TakeDamage(float Damage, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void NotifyControllerChanged() override;

private:
	// ---- Input ----
	void EnsureMechInput();
	void OnMove(const FInputActionValue& Value);
	void OnMoveStop(const FInputActionValue& Value);
	void OnLookMouse(const FInputActionValue& Value);
	void OnLookStick(const FInputActionValue& Value);
	void OnLookStickStop(const FInputActionValue& Value);
	void OnFireStart(const FInputActionValue& Value);
	void OnFireStop(const FInputActionValue& Value);
	void OnMissiles(const FInputActionValue& Value);
	void OnExit(const FInputActionValue& Value);
	void OnView(const FInputActionValue& Value);
	void OnAlignStart(const FInputActionValue& Value);
	void OnAlignStop(const FInputActionValue& Value);

	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> MechContext;
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> MoveAction;
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> LookMouseAction;
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> LookStickAction;
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> FireAction;
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> MissileAction;
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> ExitAction;
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> ViewAction;
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> AlignAction;

	// ---- Assembly and animation ----
	void LoadParts();
	void TickDrive(float DeltaSeconds);
	void TickTorso(float DeltaSeconds);
	void TickGait(float DeltaSeconds);
	void TickPose(float DeltaSeconds);
	void TickCamera(float DeltaSeconds);
	void TickSequence(float DeltaSeconds);
	FVector FootHome(int32 Leg, const FVector& AtLocation, float AtYaw) const;
	FVector GroundAt(const FVector& Point, bool* bOutHit = nullptr) const;
	void Footfall(int32 Leg);
	void Shake(float Strength);
	void SetCanopyOpen(float Alpha);
	FVector SeatLocation() const;

	struct FMechFoot
	{
		FVector Planted = FVector::ZeroVector;  // world
		float PlantedYaw = 0.f;
		FVector From = FVector::ZeroVector;
		float FromYaw = 0.f;
		float SwingT = -1.f;                    // <0: planted
		float SwingTime = 0.5f;
		float LandedAt = -10.f;
		FVector Current = FVector::ZeroVector;  // world: planted, or in the air
		float CurrentYaw = 0.f;
		float CurrentPitch = 0.f;
		bool IsSwinging() const { return SwingT >= 0.f; }
	};
	FMechFoot Feet[2];
	bool bFeetPlaced = false;

	// ---- Weapons ----
	struct FMechRound
	{
		bool bMissile = false;
		FVector Location = FVector::ZeroVector;
		FVector Velocity = FVector::ZeroVector;
		float Age = 0.f;
		float Life = 2.f;
		float NextPuff = 0.f;
		FVector Aim = FVector::ZeroVector;
		TWeakObjectPtr<AActor> Homing;
		int32 Visual = INDEX_NONE;  // index into RoundPool
		int32 Light = INDEX_NONE;   // index into LightPool
	};
	struct FMechPuff
	{
		int32 Visual = INDEX_NONE;
		FVector Location = FVector::ZeroVector;
		FVector Velocity = FVector::ZeroVector;
		float Age = 0.f;
		float Life = 1.f;
		float Size = 1.f;
		float Grow = 1.f;
		float Opacity = 0.6f;
		bool bFire = false;   // a fireball: glows and shrinks instead of drifting smoke
	};
	TArray<FMechRound> Rounds;
	TArray<FMechPuff> Puffs;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> RoundPool;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> PuffPool;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> PuffMaterials;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UPointLightComponent>> LightPool;
	TArray<float> LightFade;
	TArray<float> LightFadeTotal;
	TArray<float> LightBase;
	TBitArray<> RoundPoolUsed;
	TBitArray<> PuffPoolUsed;

	void BuildPools();
	int32 TakeRoundVisual(bool bMissile);
	int32 TakeLight(const FVector& Where, const FLinearColor& Color, float Intensity, float Radius, float Fade);
	void SpawnPuff(const FVector& Where, const FVector& Velocity, float Size, float Grow, float Life, float Opacity, bool bFire);
	void TickWeapons(float DeltaSeconds);
	void TickRounds(float DeltaSeconds);
	void TickPuffs(float DeltaSeconds);
	void FireCannon();
	void LaunchMissile(int32 Tube);
	void RoundImpact(FMechRound& Round, const FHitResult& Hit);
	void Explode(const FVector& Where, const FVector& Normal);
	bool DamageActor(AActor* Victim, float Damage, const FVector& Where, const FVector& Direction, float Push);
	FVector ComputeAimPoint(AActor** OutTarget = nullptr) const;
	FVector MuzzleLocation(int32 Arm) const;
	FVector LauncherLocation(int32 Tube) const;

	// ---- Enemies ----
	void TickThreats(float DeltaSeconds);
	struct FMechSwing
	{
		float LastProgress = 0.f;
		EKnightAction LastAction = EKnightAction::None;
		bool bLanded = false;
	};
	TMap<TWeakObjectPtr<AActor>, FMechSwing> SwingSeen;   // each foe's swing in progress, and whether it already landed
	void Wreck();
	void Reboot();

	// ---- HUD ----
	static void DrawMechHUD(AHUD* HUD, UCanvas* Canvas);
	void DrawCockpitHUD(UCanvas* Canvas) const;
	static FDelegateHandle HUDHandle;
	static int32 HUDUsers;

	// ---- Sound (synthesised, like the knight's whistle) ----
	void PlayMechSound(EMechSound Sound, const FVector& Where, float Volume = 1.f, float Pitch = 1.f) const;
	UPROPERTY(Transient)
	TObjectPtr<USoundAttenuation> SoundFalloff;

	// ---- State ----
	EMechState State = EMechState::Parked;
	TWeakObjectPtr<ASoulsKnight> Pilot;
	float SequenceT = 0.f;
	FVector ClimbFrom = FVector::ZeroVector;
	FVector ClimbTo = FVector::ZeroVector;
	bool bExitQueued = false;

	float ThrottleInput = 0.f;
	float TurnInput = 0.f;
	FVector2D StickLook = FVector2D::ZeroVector;
	bool bCannonHeld = false;
	bool bAlignHeld = false;
	bool bCockpitView = false;

	float Speed = 0.f;          // cm/s along the legs' heading (negative = reverse)
	float YawRate = 0.f;        // deg/s the legs are turning
	float LastSpeed = 0.f;
	float TorsoYaw = 0.f;       // relative to the legs
	float TorsoYawTarget = 0.f;
	float AimPitch = 0.f;
	float AimPitchTarget = 0.f;
	float ArmPitch[2] = { 0.f, 0.f };
	float ArmRecoil[2] = { 0.f, 0.f };
	float MuzzleFlashT[2] = { 0.f, 0.f };

	float KneelAlpha = 1.f;     // 1 = crouched low (parked), 0 = standing tall
	float CanopyAlpha = 0.f;
	float PelvisDrop = 0.f;     // footfall compression spring
	float PelvisDropVelocity = 0.f;
	float GroundOffset = 0.f;
	float Lean = 0.f;
	float ShakeAmount = 0.f;
	float ShakeTime = 0.f;

	float Armor = 1400.f;
	float Heat = 0.f;
	bool bOverheated = false;
	int32 CannonAmmo = 600;
	int32 Missiles = 32;
	float CannonCooldown = 0.f;
	float MissileCooldown = 0.f;
	int32 NextArm = 0;
	int32 NextTube = 0;
	int32 SalvoLeft = 0;
	float SalvoTimer = 0.f;
	float WreckedAt = -100.f;
	float LastHitTime = -100.f;
	float LastHitMarker = -100.f;
	float LastAlarm = -100.f;
	float ThreatTimer = 0.f;
	FVector LastHitFrom = FVector::ZeroVector;

	int32 ShellsFired = 0;
	int32 MissilesFired = 0;
	int32 HitsLanded = 0;
	int32 Footfalls = 0;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> HullMaterials;
	// Not transient: set in the editor (OnConstruction), they're saved with the map so a packaged game cooks them.
	UPROPERTY()
	TObjectPtr<UMaterialInterface> TracerMaterial;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> SmokeMaterial;
	UPROPERTY()
	TObjectPtr<UStaticMesh> SphereMesh;
	bool bPartsLoaded = false;
	bool bRealArt = false;
};

// -MechDemo: stages and checks the whole mech loop on whatever map is loaded (spawns a mech beside the
// player if the map has none), screenshotting each beat and logging "MechDemo: ... PASS/FAIL" lines,
// then MECH_DEMO_DONE and quits.
UCLASS()
class UNREALSOULSLIKE_API UMechDemoSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

private:
	void Run();
	void Check(const TCHAR* What, bool bPass, const FString& Detail);
	void Key(const FKey& Key, bool bDown);
	ASoulsMech* Mech() const { return DemoMech.Get(); }
	ASoulsKnight* Knight() const { return DemoKnight.Get(); }

	TWeakObjectPtr<ASoulsMech> DemoMech;
	TWeakObjectPtr<ASoulsKnight> DemoKnight;
	TWeakObjectPtr<AActor> DemoTarget;
	FVector StartLocation = FVector::ZeroVector;
	float StartYaw = 0.f;
	float TargetHealth = 0.f;
	float ArmorBefore = 0.f;
	int32 Passes = 0;
	int32 Fails = 0;
};
