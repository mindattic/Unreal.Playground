#include "SoulsMech.h"
#include "SoulsKnight.h"
#include "SoulsEnemy.h"
#include "SoulsSparks.h"
#include "SoulsBlood.h"
#include "SoulsFire.h"
#include "CombatDamageable.h"
#include "Camera/CameraComponent.h"
#include "CanvasItem.h"
#include "Components/CapsuleComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Canvas.h"
#include "Engine/DamageEvents.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/LocalPlayer.h"
#include "Engine/OverlapResult.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputKeyEventArgs.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundWaveProcedural.h"
#include "TimerManager.h"
#include "UnrealClient.h"
#include "UnrealSoulslike.h"

// ---------------------------------------------------------------------------------------------
// The rig: joint positions baked by tools/convert_mech.py (SourceArt/Environment/Mech/mech_rig.json), in
// Unreal space, cm, with the mech's origin on the ground under the middle of its hips; +X forward, +Y
// right. Every part mesh has its own pivot at its joint. Legs: 0 = left (-Y), 1 = right (+Y).
// ---------------------------------------------------------------------------------------------
namespace MechRig
{
	const FVector Pelvis(-17.5f, 0.f, 300.9f);
	const FVector Torso(-17.5f, 0.f, 363.1f);
	const FVector Canopy(0.3f, 0.f, 547.0f);
	const FVector Arm[2] = { FVector(-13.8f, -77.7f, 534.7f), FVector(-13.8f, 77.7f, 534.7f) };
	const FVector Hip[2] = { FVector(-17.5f, -105.6f, 300.9f), FVector(-17.5f, 105.6f, 300.9f) };
	const FVector Knee[2] = { FVector(109.4f, -105.6f, 205.0f), FVector(109.4f, 105.6f, 205.0f) };
	const FVector Ankle[2] = { FVector(-62.2f, -105.6f, 135.4f), FVector(-62.2f, 105.6f, 135.4f) };
	const FVector Foot[2] = { FVector(94.4f, -105.6f, 10.5f), FVector(94.4f, 105.6f, 10.5f) };
	// The foot's sole is this far below its pivot.
	constexpr float FootFloor = -12.6f;
	// In the arm's own frame (pivot at the shoulder).
	const FVector Muzzle[2] = { FVector(265.3f, -36.9f, -146.2f), FVector(265.3f, 36.9f, -146.2f) };
	// In the torso's frame (pivot at the waist): four tubes per shoulder pod.
	const FVector Launcher[8] = {
		FVector(36.2f, -78.1f, 193.5f), FVector(36.2f, 78.1f, 193.5f), FVector(36.2f, -106.7f, 215.5f), FVector(36.2f, 106.7f, 215.5f),
		FVector(36.2f, -78.1f, 215.5f), FVector(36.2f, 78.1f, 215.5f), FVector(36.2f, -106.7f, 193.5f), FVector(36.2f, 106.7f, 193.5f) };
	const FVector Seat(12.3f, 0.f, 167.3f);

	const TCHAR* const PartNames[13] = { TEXT("Pelvis"), TEXT("Torso"), TEXT("Canopy"), TEXT("ArmL"), TEXT("ArmR"),
		TEXT("ThighL"), TEXT("ShinL"), TEXT("MetaL"), TEXT("FootL"), TEXT("ThighR"), TEXT("ShinR"), TEXT("MetaR"), TEXT("FootR") };

	constexpr float CapsuleRadius = 190.f;
	constexpr float CapsuleHalfHeight = 370.f;
	constexpr float BoardReach = 600.f;       // cm from the mech's centre the knight can board from
	constexpr float StandRaise = 45.f;        // hips this much above the bake's crouch when standing
	constexpr float KneelDrop = 85.f;         // and this much below it when parked
	constexpr int32 RoundPoolSize = 64;
	constexpr int32 PuffPoolSize = 96;
	constexpr int32 LightPoolSize = 10;
	const TCHAR* const MeshFolder = TEXT("/Game/Island/Models/Mech");
	const TCHAR* const TracerPath = TEXT("/Game/Island/Materials/Mech/M_MechTracer.M_MechTracer");
	const TCHAR* const SmokePath = TEXT("/Game/Island/Materials/Mech/M_MechSmoke.M_MechSmoke");
	const TCHAR* const GlowPath = TEXT("/Game/Souls/FX/M_GraceGlow.M_GraceGlow");

	// The bird leg as a pantograph: the metatarsal stays (nearly) parallel to the thigh, so thigh + metatarsal
	// act as one long segment of length C at a fixed angle PhiC off the thigh, and the leg solves as two bones.
	struct FLegConstants
	{
		float L1 = 0.f, L2 = 0.f, L3 = 0.f;
		float ThighRest = 0.f, ShinRest = 0.f, MetaRest = 0.f; // planar angles (atan2(z, x)), radians
		float Delta = 0.f;  // metatarsal angle off the thigh
		float C = 0.f, PhiC = 0.f;
		float Bend = 1.f;   // which way the knee folds
	};

	inline float PlanarAngle(const FVector& V) { return FMath::Atan2(V.Z, V.X); }

	const FLegConstants& Leg()
	{
		static FLegConstants K = []()
		{
			FLegConstants R;
			const FVector U = Knee[0] - Hip[0], V = Ankle[0] - Knee[0], W = Foot[0] - Ankle[0];
			R.L1 = U.Size(); R.L2 = V.Size(); R.L3 = W.Size();
			R.ThighRest = PlanarAngle(U); R.ShinRest = PlanarAngle(V); R.MetaRest = PlanarAngle(W);
			R.Delta = R.MetaRest - R.ThighRest;
			const float Cx = R.L1 + R.L3 * FMath::Cos(R.Delta), Cz = R.L3 * FMath::Sin(R.Delta);
			R.C = FMath::Sqrt(Cx * Cx + Cz * Cz);
			R.PhiC = FMath::Atan2(Cz, Cx);
			const FVector T = Foot[0] - Hip[0];
			const float Gamma = R.ThighRest + R.PhiC;
			R.Bend = FMath::FindDeltaAngleRadians(PlanarAngle(T), Gamma) >= 0.f ? 1.f : -1.f;
			return R;
		}();
		return K;
	}
}

namespace MechAudio
{
	constexpr int32 Rate = 22050;

	// One-pole low pass.
	struct FLowPass { float Y = 0.f; float Step(float X, float A) { Y += A * (X - Y); return Y; } };

	TArray<int16> Synth(int32 Kind)
	{
		float Seconds = 0.6f;
		switch (Kind)
		{
		case 0: Seconds = 0.8f; break;  // footstep
		case 1: Seconds = 0.55f; break; // cannon
		case 2: Seconds = 1.0f; break;  // missile launch
		case 3: Seconds = 2.0f; break;  // explosion
		case 4: Seconds = 0.7f; break;  // clang
		case 5: Seconds = 0.8f; break;  // servo
		default: Seconds = 0.55f; break; // alarm
		}
		const int32 Count = static_cast<int32>(Seconds * Rate);
		TArray<float> Buffer;
		Buffer.SetNumZeroed(Count);
		FRandomStream Rng(1234 + Kind);
		FLowPass Lp, Lp2, Hp;
		double Phase = 0.0, Phase2 = 0.0;
		for (int32 i = 0; i < Count; ++i)
		{
			const float T = static_cast<float>(i) / Rate;
			const float Noise = Rng.FRandRange(-1.f, 1.f);
			float S = 0.f;
			switch (Kind)
			{
			case 0: // a heavy foot: a falling sub-boom, a thump, a clank of plates, a hiss of hydraulics
			{
				const float F = FMath::Lerp(64.f, 34.f, FMath::Min(T / 0.35f, 1.f));
				Phase += 2.0 * PI * F / Rate;
				S += FMath::Sin(Phase) * FMath::Exp(-T * 6.5f) * 0.95f;
				S += Lp.Step(Noise, 0.06f) * FMath::Exp(-T * 22.f) * 2.2f;
				S += (FMath::Sin(2.f * PI * 310.f * T) * 0.22f + FMath::Sin(2.f * PI * 847.f * T) * 0.1f) * FMath::Exp(-T * 16.f) * (T > 0.012f ? 1.f : 0.f);
				const float Hiss = Noise - Lp2.Step(Noise, 0.25f);
				S += Hiss * 0.07f * FMath::Clamp((T - 0.05f) * 12.f, 0.f, 1.f) * FMath::Exp(-FMath::Max(T - 0.1f, 0.f) * 7.f);
				break;
			}
			case 1: // autocannon: a crack and a punch
			{
				S += Noise * FMath::Exp(-T * 45.f) * 0.9f;
				const float F = FMath::Lerp(110.f, 55.f, FMath::Min(T / 0.2f, 1.f));
				Phase += 2.0 * PI * F / Rate;
				S += FMath::Sin(Phase) * FMath::Exp(-T * 10.f) * 0.85f;
				S += Lp.Step(Noise, 0.08f) * FMath::Exp(-T * 6.f) * 0.9f;
				break;
			}
			case 2: // missile: a pop, then a roaring whoosh that fades as it leaves
			{
				S += Noise * FMath::Exp(-T * 60.f) * 0.6f;
				const float Cut = FMath::Lerp(0.35f, 0.08f, FMath::Min(T, 1.f));
				const float Env = FMath::Min(T * 25.f, 1.f) * FMath::Exp(-T * 2.6f);
				S += Lp.Step(Noise, Cut) * Env * 1.6f;
				break;
			}
			case 3: // explosion: a deep boom, a rumbling roar and crackle
			{
				Phase += 2.0 * PI * FMath::Lerp(48.f, 28.f, FMath::Min(T, 1.f)) / Rate;
				S += FMath::Sin(Phase) * FMath::Exp(-T * 2.8f) * 0.9f;
				S += Lp.Step(Noise, 0.05f) * FMath::Exp(-T * 1.9f) * 3.2f;
				S += Noise * FMath::Exp(-T * 30.f) * 0.7f;
				if (Rng.FRand() < 0.002f * FMath::Exp(-T * 2.f))
				{
					S += Rng.FRandRange(-0.8f, 0.8f);
				}
				break;
			}
			case 4: // armour struck: a bell of inharmonic partials
			{
				S += Noise * FMath::Exp(-T * 90.f) * 0.5f;
				S += FMath::Sin(2.f * PI * 523.f * T) * FMath::Exp(-T * 7.f) * 0.32f;
				S += FMath::Sin(2.f * PI * 1307.f * T) * FMath::Exp(-T * 11.f) * 0.22f;
				S += FMath::Sin(2.f * PI * 2091.f * T) * FMath::Exp(-T * 16.f) * 0.16f;
				S += FMath::Sin(2.f * PI * 3011.f * T) * FMath::Exp(-T * 24.f) * 0.1f;
				break;
			}
			case 5: // servo: a rising motor whine, then a latch
			{
				const float F = FMath::Lerp(85.f, 190.f, FMath::Min(T / 0.6f, 1.f));
				Phase += F / Rate;
				Phase2 += 2.0 * PI * F * 2.01f / Rate;
				const float Saw = static_cast<float>(2.0 * (Phase - FMath::FloorToDouble(Phase)) - 1.0);
				const float Env = FMath::Min(T * 10.f, 1.f) * FMath::Clamp((0.62f - T) * 10.f, 0.f, 1.f);
				S += Lp.Step(Saw, 0.18f) * Env * 0.55f + FMath::Sin(Phase2) * Env * 0.12f;
				if (T > 0.62f)
				{
					S += Noise * FMath::Exp(-(T - 0.62f) * 70.f) * 0.7f;
				}
				break;
			}
			default: // alarm: two-tone
			{
				const float F = FMath::Fmod(T, 0.25f) < 0.125f ? 880.f : 660.f;
				Phase += F / Rate;
				S += (FMath::Fmod(static_cast<float>(Phase), 1.f) < 0.5f ? 0.35f : -0.35f) * (T < 0.5f ? 1.f : 0.f);
				break;
			}
			}
			Buffer[i] = S;
		}
		// Normalise and fade the tail to silence.
		float Peak = 0.01f;
		for (const float S : Buffer)
		{
			Peak = FMath::Max(Peak, FMath::Abs(S));
		}
		TArray<int16> Out;
		Out.SetNumUninitialized(Count);
		for (int32 i = 0; i < Count; ++i)
		{
			const float Tail = FMath::Clamp((Count - i) / (0.03f * Rate), 0.f, 1.f);
			Out[i] = static_cast<int16>(FMath::Clamp(Buffer[i] / Peak * 0.85f * Tail, -1.f, 1.f) * 30000.f);
		}
		return Out;
	}

	const TArray<int16>& Get(int32 Kind)
	{
		static TArray<int16> Cache[8];
		if (Cache[Kind].IsEmpty())
		{
			Cache[Kind] = Synth(Kind);
		}
		return Cache[Kind];
	}
}

FDelegateHandle ASoulsMech::HUDHandle;
int32 ASoulsMech::HUDUsers = 0;

// ---------------------------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------------------------

ASoulsMech::ASoulsMech()
{
	PrimaryActorTick.bCanEverTick = true;
	AutoPossessAI = EAutoPossessAI::Disabled;
	AIControllerClass = nullptr;
	bUseControllerRotationYaw = false;
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;

	GetCapsuleComponent()->InitCapsuleSize(MechRig::CapsuleRadius, MechRig::CapsuleHalfHeight);
	GetCapsuleComponent()->SetCanEverAffectNavigation(false);
	GetMesh()->SetHiddenInGame(true);
	GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	UCharacterMovementComponent* Move = GetCharacterMovement();
	Move->bOrientRotationToMovement = false;
	Move->MaxWalkSpeed = MaxForwardSpeed;
	Move->MaxAcceleration = 6000.f;
	Move->BrakingDecelerationWalking = 6000.f;
	Move->GroundFriction = 8.f;
	Move->MaxStepHeight = 95.f;
	Move->SetWalkableFloorAngle(42.f);
	Move->Mass = 40000.f;
	Move->GravityScale = 1.6f;
	Move->JumpZVelocity = 0.f;
	Move->Buoyancy = 0.f;
	Move->MaxSwimSpeed = 160.f;
	Move->bRunPhysicsWithNoController = true;
	Move->bCanWalkOffLedges = true;

	Chassis = CreateDefaultSubobject<USceneComponent>(TEXT("Chassis"));
	Chassis->SetupAttachment(GetCapsuleComponent());
	Chassis->SetRelativeLocation(FVector(0.f, 0.f, -MechRig::CapsuleHalfHeight));

	auto MakePart = [this](const TCHAR* Name, USceneComponent* Parent)
	{
		UStaticMeshComponent* Part = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Part->SetupAttachment(Parent);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->SetCanEverAffectNavigation(false);
		Part->SetGenerateOverlapEvents(false);
		return Part;
	};
	PelvisMesh = MakePart(TEXT("Pelvis"), Chassis);
	TorsoMesh = MakePart(TEXT("Torso"), PelvisMesh);
	CanopyMesh = MakePart(TEXT("Canopy"), TorsoMesh);
	ArmLMesh = MakePart(TEXT("ArmL"), TorsoMesh);
	ArmRMesh = MakePart(TEXT("ArmR"), TorsoMesh);
	PelvisMesh->SetRelativeLocation(MechRig::Pelvis);
	TorsoMesh->SetRelativeLocation(MechRig::Torso - MechRig::Pelvis);
	CanopyMesh->SetRelativeLocation(MechRig::Canopy - MechRig::Torso);
	ArmLMesh->SetRelativeLocation(MechRig::Arm[0] - MechRig::Torso);
	ArmRMesh->SetRelativeLocation(MechRig::Arm[1] - MechRig::Torso);
	for (const TCHAR* Name : { TEXT("ThighL"), TEXT("ShinL"), TEXT("MetaL"), TEXT("FootL"), TEXT("ThighR"), TEXT("ShinR"), TEXT("MetaR"), TEXT("FootR") })
	{
		LegParts.Add(MakePart(Name, Chassis));
	}

	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(Chassis);
	CameraBoom->SetRelativeLocation(FVector(0.f, 0.f, 560.f));
	CameraBoom->SetUsingAbsoluteRotation(true);
	CameraBoom->TargetArmLength = 1250.f;
	CameraBoom->SocketOffset = FVector(0.f, 0.f, 160.f);
	CameraBoom->bUsePawnControlRotation = false;
	CameraBoom->bEnableCameraLag = true;
	CameraBoom->CameraLagSpeed = 7.f;
	CameraBoom->bEnableCameraRotationLag = true;
	CameraBoom->CameraRotationLagSpeed = 12.f;
	CameraBoom->ProbeSize = 24.f;

	ChaseCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("ChaseCamera"));
	ChaseCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	ChaseCamera->SetFieldOfView(80.f);

	CockpitCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("CockpitCamera"));
	CockpitCamera->SetupAttachment(TorsoMesh);
	CockpitCamera->SetRelativeLocation(MechRig::Canopy - MechRig::Torso + FVector(62.f, 0.f, 32.f));
	CockpitCamera->SetFieldOfView(92.f);
	CockpitCamera->SetAutoActivate(false);

	for (int32 Arm = 0; Arm < 2; ++Arm)
	{
		UStaticMeshComponent* ArmPart = Arm == 0 ? ArmLMesh : ArmRMesh;
		UPointLightComponent* Light = CreateDefaultSubobject<UPointLightComponent>(Arm == 0 ? TEXT("MuzzleLightL") : TEXT("MuzzleLightR"));
		Light->SetupAttachment(ArmPart);
		Light->SetRelativeLocation(MechRig::Muzzle[Arm] + FVector(40.f, 0.f, 0.f));
		Light->SetCastShadows(false);
		Light->SetAttenuationRadius(1100.f);
		Light->SetLightColor(FLinearColor(1.f, 0.62f, 0.25f));
		Light->SetIntensity(0.f);
		MuzzleLights.Add(Light);
		UStaticMeshComponent* Flash = MakePart(Arm == 0 ? TEXT("MuzzleFlashL") : TEXT("MuzzleFlashR"), ArmPart);
		Flash->SetRelativeLocation(MechRig::Muzzle[Arm] + FVector(45.f, 0.f, 0.f));
		Flash->SetCastShadow(false);
		Flash->SetVisibility(false);
		MuzzleFlashes.Add(Flash);
	}
}

void ASoulsMech::LoadParts()
{
	if (bPartsLoaded)
	{
		return;
	}
	bPartsLoaded = true;
	UStaticMeshComponent* Parts[13] = { PelvisMesh, TorsoMesh, CanopyMesh, ArmLMesh, ArmRMesh,
		LegParts[0], LegParts[1], LegParts[2], LegParts[3], LegParts[4], LegParts[5], LegParts[6], LegParts[7] };
	int32 Found = 0;
	for (int32 Index = 0; Index < 13; ++Index)
	{
		const FString Path = FString::Printf(TEXT("%s/SM_Mech_%s.SM_Mech_%s"), MechRig::MeshFolder, MechRig::PartNames[Index], MechRig::PartNames[Index]);
		if (UStaticMesh* PartMesh = LoadObject<UStaticMesh>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet))
		{
			Parts[Index]->SetStaticMesh(PartMesh);
			++Found;
		}
	}
	bRealArt = Found == 13;
	if (!bRealArt)
	{
		UE_LOG(LogUnrealSoulslike, Warning, TEXT("Mech %s: %d of 13 part meshes under %s (run tools/convert_mech.py, then rebuild the island)"),
			*GetName(), Found, MechRig::MeshFolder);
	}
	// Keep what the map saved if a lookup fails (a cooked game only has what something referenced).
	if (UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"), nullptr, LOAD_NoWarn | LOAD_Quiet))
	{
		SphereMesh = Sphere;
	}
	if (UMaterialInterface* Tracer = LoadObject<UMaterialInterface>(nullptr, MechRig::TracerPath, nullptr, LOAD_NoWarn | LOAD_Quiet))
	{
		TracerMaterial = Tracer;
	}
	else if (!TracerMaterial)
	{
		TracerMaterial = LoadObject<UMaterialInterface>(nullptr, MechRig::GlowPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
	}
	if (UMaterialInterface* Smoke = LoadObject<UMaterialInterface>(nullptr, MechRig::SmokePath, nullptr, LOAD_NoWarn | LOAD_Quiet))
	{
		SmokeMaterial = Smoke;
	}
	for (UStaticMeshComponent* Flash : MuzzleFlashes)
	{
		Flash->SetStaticMesh(SphereMesh);
		Flash->SetMaterial(0, TracerMaterial);
	}
}

void ASoulsMech::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	bPartsLoaded = false;
	LoadParts();
	// Show it parked in the editor: crouched, feet at home.
	KneelAlpha = 1.f;
	CanopyAlpha = 0.f;
	TickPose(0.f);
}

void ASoulsMech::BeginPlay()
{
	Super::BeginPlay();
	LoadParts();
	GetCharacterMovement()->MaxWalkSpeed = MaxForwardSpeed;
	Armor = MaxArmor;
	CannonAmmo = MaxCannonAmmo;
	Missiles = MaxMissiles;
	KneelAlpha = 1.f;
	AimPitch = AimPitchTarget = -12.f;
	ArmPitch[0] = ArmPitch[1] = -18.f;

	SoundFalloff = NewObject<USoundAttenuation>(this);
	SoundFalloff->Attenuation.bAttenuate = true;
	SoundFalloff->Attenuation.bSpatialize = true;
	SoundFalloff->Attenuation.AttenuationShapeExtents = FVector(600.f, 0.f, 0.f);
	SoundFalloff->Attenuation.FalloffDistance = 9000.f;

	UStaticMeshComponent* Parts[] = { PelvisMesh, TorsoMesh, CanopyMesh, ArmLMesh, ArmRMesh };
	for (UStaticMeshComponent* Part : Parts)
	{
		if (Part->GetStaticMesh())
		{
			HullMaterials.Add(Part->CreateAndSetMaterialInstanceDynamic(0));
		}
	}
	for (UStaticMeshComponent* Part : LegParts)
	{
		if (Part->GetStaticMesh())
		{
			HullMaterials.Add(Part->CreateAndSetMaterialInstanceDynamic(0));
		}
	}
	BuildPools();

	if (HUDUsers++ == 0)
	{
		HUDHandle = AHUD::OnHUDPostRender.AddStatic(&ASoulsMech::DrawMechHUD);
	}
	UE_LOG(LogUnrealSoulslike, Log, TEXT("Mech %s parked at %s (art %s)"), *GetName(), *GetActorLocation().ToCompactString(), bRealArt ? TEXT("ok") : TEXT("MISSING"));
}

void ASoulsMech::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (--HUDUsers <= 0)
	{
		HUDUsers = 0;
		AHUD::OnHUDPostRender.Remove(HUDHandle);
		HUDHandle.Reset();
	}
	Super::EndPlay(EndPlayReason);
}

void ASoulsMech::BuildPools()
{
	auto MakeLoose = [this](UMaterialInterface* Material)
	{
		UStaticMeshComponent* Comp = NewObject<UStaticMeshComponent>(this);
		Comp->SetStaticMesh(SphereMesh);
		Comp->SetMaterial(0, Material);
		Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Comp->SetCastShadow(false);
		Comp->SetCanEverAffectNavigation(false);
		Comp->SetUsingAbsoluteLocation(true);
		Comp->SetUsingAbsoluteRotation(true);
		Comp->SetUsingAbsoluteScale(true);
		Comp->SetupAttachment(GetRootComponent());
		Comp->SetVisibility(false);
		Comp->RegisterComponent();
		return Comp;
	};
	for (int32 Index = 0; Index < MechRig::RoundPoolSize; ++Index)
	{
		RoundPool.Add(MakeLoose(TracerMaterial));
	}
	RoundPoolUsed.Init(false, MechRig::RoundPoolSize);
	for (int32 Index = 0; Index < MechRig::PuffPoolSize; ++Index)
	{
		UMaterialInstanceDynamic* Mid = SmokeMaterial ? UMaterialInstanceDynamic::Create(SmokeMaterial, this) : nullptr;
		PuffMaterials.Add(Mid);
		PuffPool.Add(MakeLoose(Mid ? static_cast<UMaterialInterface*>(Mid) : TracerMaterial.Get()));
	}
	PuffPoolUsed.Init(false, MechRig::PuffPoolSize);
	for (int32 Index = 0; Index < MechRig::LightPoolSize; ++Index)
	{
		UPointLightComponent* Light = NewObject<UPointLightComponent>(this);
		Light->SetUsingAbsoluteLocation(true);
		Light->SetCastShadows(false);
		Light->SetIntensity(0.f);
		Light->SetupAttachment(GetRootComponent());
		Light->RegisterComponent();
		LightPool.Add(Light);
		LightFade.Add(0.f);
		LightFadeTotal.Add(1.f);
		LightBase.Add(0.f);
	}
}

// ---------------------------------------------------------------------------------------------
// Input: one set of actions and one context shared by every mech (rooted, like the knight's), added to the
// player while they drive and removed when they climb out, so the knight's own controls come straight back.
// ---------------------------------------------------------------------------------------------

void ASoulsMech::EnsureMechInput()
{
	if (MechContext)
	{
		return;
	}
	static TMap<FName, UInputAction*> MechSharedActions;
	static UInputMappingContext* MechSharedContext = nullptr;
	auto MakeAction = [](const TCHAR* Name, EInputActionValueType Type)
	{
		if (UInputAction** Found = MechSharedActions.Find(FName(Name)))
		{
			return *Found;
		}
		UInputAction* NewAction = NewObject<UInputAction>(GetTransientPackage(), Name);
		NewAction->ValueType = Type;
		NewAction->AddToRoot();
		MechSharedActions.Add(FName(Name), NewAction);
		return NewAction;
	};
	MoveAction = MakeAction(TEXT("IA_MechMove"), EInputActionValueType::Axis2D);
	LookMouseAction = MakeAction(TEXT("IA_MechLookMouse"), EInputActionValueType::Axis2D);
	LookStickAction = MakeAction(TEXT("IA_MechLookStick"), EInputActionValueType::Axis2D);
	FireAction = MakeAction(TEXT("IA_MechFire"), EInputActionValueType::Boolean);
	MissileAction = MakeAction(TEXT("IA_MechMissiles"), EInputActionValueType::Boolean);
	ExitAction = MakeAction(TEXT("IA_MechExit"), EInputActionValueType::Boolean);
	ViewAction = MakeAction(TEXT("IA_MechView"), EInputActionValueType::Boolean);
	AlignAction = MakeAction(TEXT("IA_MechAlign"), EInputActionValueType::Boolean);
	StopAction = MakeAction(TEXT("IA_MechStop"), EInputActionValueType::Boolean);

	if (MechSharedContext)
	{
		MechContext = MechSharedContext;
		return;
	}
	UInputMappingContext* Context = NewObject<UInputMappingContext>(GetTransientPackage(), TEXT("IMC_Mech"));
	Context->AddToRoot();
	MechSharedContext = Context;
	MechContext = Context;

	auto Swizzle = [Context]() { return NewObject<UInputModifierSwizzleAxis>(Context); };
	auto Negate = [Context]() { return NewObject<UInputModifierNegate>(Context); };
	auto DeadZone = [Context]()
	{
		UInputModifierDeadZone* Zone = NewObject<UInputModifierDeadZone>(Context);
		Zone->LowerThreshold = 0.18f;
		return Zone;
	};
	// Legs: W/S throttle (Y), A/D turn (X); the left stick both.
	Context->MapKey(MoveAction, EKeys::W).Modifiers.Add(Swizzle());
	{
		FEnhancedActionKeyMapping& Back = Context->MapKey(MoveAction, EKeys::S);
		Back.Modifiers.Add(Swizzle());
		Back.Modifiers.Add(Negate());
	}
	Context->MapKey(MoveAction, EKeys::A).Modifiers.Add(Negate());
	Context->MapKey(MoveAction, EKeys::D);
	Context->MapKey(MoveAction, EKeys::Gamepad_Left2D).Modifiers.Add(DeadZone());
	// Torso: the mouse (deltas) and the right stick (rates).
	Context->MapKey(LookMouseAction, EKeys::Mouse2D);
	Context->MapKey(LookStickAction, EKeys::Gamepad_Right2D).Modifiers.Add(DeadZone());
	// Weapons on the triggers (UNR-T73): RT / LMB the autocannons (hold), LT / RMB a missile salvo.
	Context->MapKey(FireAction, EKeys::Gamepad_RightTrigger);
	Context->MapKey(FireAction, EKeys::LeftMouseButton);
	Context->MapKey(MissileAction, EKeys::Gamepad_LeftTrigger);
	Context->MapKey(MissileAction, EKeys::RightMouseButton);
	// X / F: bring the legs round to the torso. A / X key: full stop. D-pad up / E: climb out.
	Context->MapKey(AlignAction, EKeys::Gamepad_FaceButton_Left);
	Context->MapKey(AlignAction, EKeys::F);
	Context->MapKey(StopAction, EKeys::Gamepad_FaceButton_Bottom);
	Context->MapKey(StopAction, EKeys::X);
	Context->MapKey(ExitAction, EKeys::Gamepad_DPad_Up);
	Context->MapKey(ExitAction, EKeys::E);
	Context->MapKey(ViewAction, EKeys::Gamepad_RightThumbstick);
	Context->MapKey(ViewAction, EKeys::V);
}

void ASoulsMech::NotifyControllerChanged()
{
	Super::NotifyControllerChanged();
	EnsureMechInput();
	if (const APlayerController* Old = Cast<APlayerController>(PreviousController))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(Old->GetLocalPlayer()))
		{
			Subsystem->RemoveMappingContext(MechContext);
		}
	}
	if (const APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		// Above the knight's and the template's contexts, so its keys win while driving.
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
		{
			Subsystem->AddMappingContext(MechContext, 100);
		}
	}
	ThrottleInput = TurnInput = 0.f;
	StickLook = FVector2D::ZeroVector;
	bCannonHeld = false;
	bAligning = false;
	bStopLatched = false;
}

void ASoulsMech::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	EnsureMechInput();
	if (UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		Input->BindAction(MoveAction, ETriggerEvent::Triggered, this, &ASoulsMech::OnMove);
		Input->BindAction(MoveAction, ETriggerEvent::Completed, this, &ASoulsMech::OnMoveStop);
		Input->BindAction(LookMouseAction, ETriggerEvent::Triggered, this, &ASoulsMech::OnLookMouse);
		Input->BindAction(LookStickAction, ETriggerEvent::Triggered, this, &ASoulsMech::OnLookStick);
		Input->BindAction(LookStickAction, ETriggerEvent::Completed, this, &ASoulsMech::OnLookStickStop);
		Input->BindAction(FireAction, ETriggerEvent::Started, this, &ASoulsMech::OnFireStart);
		Input->BindAction(FireAction, ETriggerEvent::Completed, this, &ASoulsMech::OnFireStop);
		Input->BindAction(MissileAction, ETriggerEvent::Started, this, &ASoulsMech::OnMissiles);
		Input->BindAction(ExitAction, ETriggerEvent::Started, this, &ASoulsMech::OnExit);
		Input->BindAction(ViewAction, ETriggerEvent::Started, this, &ASoulsMech::OnView);
		Input->BindAction(AlignAction, ETriggerEvent::Started, this, &ASoulsMech::OnAlign);
		Input->BindAction(StopAction, ETriggerEvent::Started, this, &ASoulsMech::OnStop);
	}
}

void ASoulsMech::OnMove(const FInputActionValue& Value)
{
	const FVector2D Axis = Value.Get<FVector2D>();
	SetDriveInput(FMath::Clamp(Axis.Y, -1.f, 1.f), FMath::Clamp(Axis.X, -1.f, 1.f));
}

void ASoulsMech::OnMoveStop(const FInputActionValue& Value)
{
	SetDriveInput(0.f, 0.f);
}

void ASoulsMech::OnLookMouse(const FInputActionValue& Value)
{
	const FVector2D Axis = Value.Get<FVector2D>();
	AddAimInput(Axis.X * 0.3f, Axis.Y * 0.22f * (bInvertMousePitch ? -1.f : 1.f));
}

void ASoulsMech::OnLookStick(const FInputActionValue& Value)
{
	StickLook = Value.Get<FVector2D>();
}

void ASoulsMech::OnLookStickStop(const FInputActionValue& Value)
{
	StickLook = FVector2D::ZeroVector;
}

void ASoulsMech::OnFireStart(const FInputActionValue& Value) { SetFiring(true); }
void ASoulsMech::OnFireStop(const FInputActionValue& Value) { SetFiring(false); }
void ASoulsMech::OnMissiles(const FInputActionValue& Value) { FireMissiles(); }
void ASoulsMech::OnExit(const FInputActionValue& Value) { Disembark(); }
void ASoulsMech::OnView(const FInputActionValue& Value) { ToggleView(); }
void ASoulsMech::OnAlign(const FInputActionValue& Value) { AlignLegsToTorso(); }
void ASoulsMech::OnStop(const FInputActionValue& Value) { FullStop(); }

void ASoulsMech::AlignLegsToTorso()
{
	if (State == EMechState::Piloted && (FMath::Abs(TorsoYawTarget) > 1.5f || FMath::Abs(TorsoYaw) > 1.5f))
	{
		bAligning = true;
		PlayMechSound(EMechSound::Servo, GetActorLocation(), 0.5f, 0.8f);
	}
}

void ASoulsMech::FullStop()
{
	if (State != EMechState::Piloted)
	{
		return;
	}
	bStopLatched = true;
	bAligning = false;
	PlayMechSound(EMechSound::Servo, GetActorLocation(), 0.5f, 0.6f);
}

void ASoulsMech::AddAimInput(float YawDegrees, float PitchDegrees)
{
	if (State != EMechState::Piloted)
	{
		return;
	}
	TorsoYawTarget = FMath::Clamp(TorsoYawTarget + YawDegrees, -TorsoTwistLimit, TorsoTwistLimit);
	AimPitchTarget = FMath::Clamp(AimPitchTarget + PitchDegrees, AimPitchLimits.X, AimPitchLimits.Y);
}

void ASoulsMech::SetTorsoTarget(float Yaw, float Pitch)
{
	TorsoYawTarget = FMath::Clamp(Yaw, -TorsoTwistLimit, TorsoTwistLimit);
	AimPitchTarget = FMath::Clamp(Pitch, AimPitchLimits.X, AimPitchLimits.Y);
}

void ASoulsMech::ToggleView()
{
	if (State != EMechState::Piloted)
	{
		return;
	}
	bCockpitView = !bCockpitView;
	ChaseCamera->SetActive(!bCockpitView);
	CockpitCamera->SetActive(bCockpitView);
}

// ---------------------------------------------------------------------------------------------
// Boarding and leaving
// ---------------------------------------------------------------------------------------------

ASoulsMech* ASoulsMech::FindBoardable(const ASoulsKnight* Knight)
{
	if (!Knight || !Knight->GetWorld())
	{
		return nullptr;
	}
	ASoulsMech* Best = nullptr;
	float BestDistance = TNumericLimits<float>::Max();
	for (TActorIterator<ASoulsMech> It(Knight->GetWorld()); It; ++It)
	{
		const float Distance = FVector::Dist2D(It->GetActorLocation(), Knight->GetActorLocation());
		if (It->CanBoard(Knight) && Distance < BestDistance)
		{
			Best = *It;
			BestDistance = Distance;
		}
	}
	return Best;
}

bool ASoulsMech::TryBoard(ASoulsKnight* Knight)
{
	ASoulsMech* Mech = FindBoardable(Knight);
	return Mech && Mech->Board(Knight);
}

bool ASoulsMech::CanBoard(const ASoulsKnight* Knight) const
{
	if (!Knight || State != EMechState::Parked || Knight->IsDead() || Knight->IsBusy() || !Cast<APlayerController>(Knight->GetController()))
	{
		return false;
	}
	const FVector To = Knight->GetActorLocation() - GetActorLocation();
	return To.Size2D() < MechRig::BoardReach && FMath::Abs(To.Z) < MechRig::CapsuleHalfHeight + 150.f;
}

bool ASoulsMech::Board(ASoulsKnight* Knight)
{
	if (!CanBoard(Knight))
	{
		return false;
	}
	APlayerController* PC = Cast<APlayerController>(Knight->GetController());
	Pilot = Knight;
	State = EMechState::Boarding;
	SequenceT = 0.f;
	bExitQueued = false;
	ClimbFrom = Knight->GetActorLocation();
	ClimbTo = SeatLocation();

	Knight->GetCharacterMovement()->StopMovementImmediately();
	Knight->GetCharacterMovement()->DisableMovement();
	Knight->SetActorEnableCollision(false);
	Knight->SetActorRotation(FRotator(0.f, (GetActorLocation() - Knight->GetActorLocation()).Rotation().Yaw, 0.f));

	// The player drives the mech from here on: the climb is watched from its camera. The template controller
	// rebinds "respawn on pawn destroyed" to whatever it possesses; that must stay with the knight alone.
	Knight->OnDestroyed.RemoveAll(PC);
	PC->Possess(this);
	OnDestroyed.RemoveAll(PC);
	PC->SetControlRotation(GetActorRotation());
	bCockpitView = false;
	ChaseCamera->SetActive(true);
	CockpitCamera->SetActive(false);
	TorsoYaw = TorsoYawTarget = 0.f;
	PlayMechSound(EMechSound::Servo, SeatLocation(), 0.9f);
	UE_LOG(LogUnrealSoulslike, Log, TEXT("Mech: %s climbs into %s"), *Knight->GetName(), *GetName());
	return true;
}

void ASoulsMech::Disembark()
{
	if (State != EMechState::Piloted)
	{
		return;
	}
	if (FMath::Abs(Speed) > 90.f)
	{
		bExitQueued = true; // brake first, then climb out
		return;
	}
	bExitQueued = false;
	bCannonHeld = false;
	State = EMechState::Exiting;
	SequenceT = 0.f;
	PlayMechSound(EMechSound::Servo, SeatLocation(), 0.9f, 0.85f);
}

FVector ASoulsMech::SeatLocation() const
{
	return TorsoMesh->GetComponentTransform().TransformPosition(MechRig::Seat);
}

void ASoulsMech::SetCanopyOpen(float Alpha)
{
	CanopyAlpha = FMath::Clamp(Alpha, 0.f, 1.f);
}

void ASoulsMech::TickSequence(float DeltaSeconds)
{
	ASoulsKnight* Knight = Pilot.Get();
	auto Smooth = [](float A, float B, float T) { return FMath::SmoothStep(A, B, T); };
	auto Arc = [](const FVector& A, const FVector& Mid, const FVector& B, float T)
	{
		const float U = 1.f - T;
		return A * (U * U) + Mid * (2.f * U * T) + B * (T * T);
	};
	const FVector Right = GetActorRightVector();

	if (State == EMechState::Boarding)
	{
		SequenceT += DeltaSeconds;
		const float T = SequenceT;
		KneelAlpha = 1.f - Smooth(1.9f, 2.5f, T);
		SetCanopyOpen(Smooth(0.1f, 0.6f, T) * (1.f - Smooth(1.75f, 2.15f, T)));
		if (Knight)
		{
			ClimbTo = SeatLocation() + FVector(0.f, 0.f, 70.f);
			if (T < 1.75f)
			{
				const float U = FMath::InterpEaseInOut(0.f, 1.f, FMath::Clamp((T - 0.25f) / 1.45f, 0.f, 1.f), 2.f);
				const FVector Side = GetActorLocation() + Right * -260.f + FVector(0.f, 0.f, 260.f);
				Knight->SetActorLocation(Arc(ClimbFrom, Side, ClimbTo, U), false, nullptr, ETeleportType::TeleportPhysics);
				Knight->SetActorRotation(FRotator(0.f, GetActorRotation().Yaw, 0.f));
			}
			else if (!Knight->IsHidden())
			{
				// In: the hatch closes over the knight, who rides in the seat until climbing out.
				Knight->SetActorHiddenInGame(true);
				Knight->AttachToComponent(TorsoMesh, FAttachmentTransformRules::KeepWorldTransform);
				Knight->SetActorRelativeLocation(MechRig::Seat);
				Knight->SetActorRelativeRotation(FRotator::ZeroRotator);
				PlayMechSound(EMechSound::Clang, SeatLocation(), 0.5f, 0.7f);
			}
		}
		if (T >= 2.6f)
		{
			State = EMechState::Piloted;
			CannonAmmo = MaxCannonAmmo;
			Missiles = MaxMissiles;
			Heat = 0.f;
			bOverheated = false;
			AimPitchTarget = 0.f;
			PlayMechSound(EMechSound::Servo, GetActorLocation(), 0.7f, 1.3f);
			UE_LOG(LogUnrealSoulslike, Log, TEXT("Mech: systems online"));
		}
		return;
	}

	if (State == EMechState::Exiting)
	{
		SequenceT += DeltaSeconds;
		const float T = SequenceT;
		KneelAlpha = Smooth(0.f, 0.6f, T);
		SetCanopyOpen(Smooth(0.35f, 0.75f, T));
		TorsoYawTarget = 0.f;
		if (Knight && T >= 0.75f)
		{
			if (Knight->IsHidden())
			{
				Knight->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
				Knight->SetActorHiddenInGame(false);
				ClimbFrom = SeatLocation() + FVector(0.f, 0.f, 60.f);
				// Down on the left, clear of the foot.
				const FVector Spot = GetActorLocation() - Right * 420.f + GetActorForwardVector() * 60.f;
				bool bHit = false;
				const FVector Ground = GroundAt(Spot, &bHit);
				ClimbTo = (bHit ? Ground : Spot - FVector(0.f, 0.f, MechRig::CapsuleHalfHeight)) + FVector(0.f, 0.f, Knight->GetSimpleCollisionHalfHeight() + 8.f);
			}
			const float U = FMath::InterpEaseInOut(0.f, 1.f, FMath::Clamp((T - 0.75f) / 1.1f, 0.f, 1.f), 2.f);
			const FVector Side = GetActorLocation() - Right * 300.f + FVector(0.f, 0.f, 220.f);
			Knight->SetActorLocation(Arc(ClimbFrom, Side, ClimbTo, U), false, nullptr, ETeleportType::TeleportPhysics);
			Knight->SetActorRotation(FRotator(0.f, GetActorRotation().Yaw + TorsoYaw, 0.f));
		}
		if (T >= 1.9f)
		{
			APlayerController* PC = Cast<APlayerController>(GetController());
			const float Yaw = GetActorRotation().Yaw + TorsoYaw;
			State = EMechState::Parked;
			bCockpitView = false;
			ChaseCamera->SetActive(true);
			CockpitCamera->SetActive(false);
			if (Knight)
			{
				Knight->SetActorEnableCollision(true);
				Knight->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
				if (PC)
				{
					OnDestroyed.RemoveAll(PC);
					Knight->OnDestroyed.RemoveAll(PC);
					// Its keys (A among them) must stop answering before the knight takes over (UNR-T73).
					if (UEnhancedInputLocalPlayerSubsystem* KnightInput = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
					{
						KnightInput->RemoveMappingContext(MechContext);
					}
					PC->Possess(Knight);
					PC->SetControlRotation(FRotator(-10.f, Yaw, 0.f));
				}
				UE_LOG(LogUnrealSoulslike, Log, TEXT("Mech: %s climbs out of %s"), *Knight->GetName(), *GetName());
			}
			else if (PC)
			{
				PC->UnPossess();
			}
			Pilot = nullptr;
			ThrottleInput = TurnInput = 0.f;
		}
	}
}

// ---------------------------------------------------------------------------------------------
// Tick
// ---------------------------------------------------------------------------------------------

void ASoulsMech::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const float Dt = FMath::Min(DeltaSeconds, 0.05f);
	if (State == EMechState::Boarding || State == EMechState::Exiting)
	{
		TickSequence(Dt);
	}
	else if (State == EMechState::Parked)
	{
		KneelAlpha = FMath::FInterpTo(KneelAlpha, 1.f, Dt, 2.f);
		CanopyAlpha = FMath::FInterpTo(CanopyAlpha, 0.f, Dt, 3.f);
	}
	else if (State == EMechState::Wrecked)
	{
		KneelAlpha = FMath::FInterpTo(KneelAlpha, 1.f, Dt, 3.f);
		const float Now = GetWorld()->GetTimeSeconds();
		if (FMath::FRand() < Dt * 8.f)
		{
			SpawnPuff(TorsoMesh->GetComponentLocation() + FVector(FMath::FRandRange(-60.f, 60.f), FMath::FRandRange(-60.f, 60.f), 150.f),
				FVector(FMath::FRandRange(-30.f, 30.f), FMath::FRandRange(-30.f, 30.f), 220.f), 0.9f, 2.6f, 3.f, 0.7f, false);
		}
		if (FMath::FRand() < Dt * 1.5f)
		{
			ASoulsSparks::Burst(GetWorld(), TorsoMesh->GetComponentLocation() + FMath::VRand() * 80.f + FVector(0.f, 0.f, 120.f), FMath::VRand(), 10, 1.2f);
		}
		if (Now - WreckedAt > RebootSeconds)
		{
			Reboot();
		}
	}
	if (bExitQueued && FMath::Abs(Speed) < 60.f)
	{
		Disembark();
	}

	TickDrive(Dt);
	TickTorso(Dt);
	TickGait(Dt);
	TickPose(Dt);
	TickWeapons(Dt);
	TickRounds(Dt);
	TickPuffs(Dt);
	TickThreats(Dt);
	TickCamera(Dt);
}

void ASoulsMech::TickDrive(float DeltaSeconds)
{
	const bool bDriving = State == EMechState::Piloted && !bExitQueued;
	// Full stop holds until the stick (or WASD) is let go, then driving resumes as normal.
	if (bStopLatched && FMath::Abs(ThrottleInput) < 0.1f && FMath::Abs(TurnInput) < 0.1f && FMath::Abs(Speed) < 5.f)
	{
		bStopLatched = false;
	}
	const bool bHalted = bStopLatched || !bDriving;
	const float Throttle = bHalted ? 0.f : ThrottleInput;
	float Turn = bHalted ? 0.f : TurnInput;
	if (bAligning)
	{
		if (!bDriving || FMath::Abs(TurnInput) > 0.35f || (FMath::Abs(TorsoYawTarget) < 1.5f && FMath::Abs(TorsoYaw) < 3.f))
		{
			bAligning = false; // done, or the pilot took the legs back
		}
		else
		{
			Turn = FMath::Clamp(TorsoYawTarget / 20.f, -1.f, 1.f); // bring the legs round under the torso
		}
	}
	const float Target = Throttle >= 0.f ? Throttle * MaxForwardSpeed : Throttle * MaxReverseSpeed;
	const bool bSpeedingUp = FMath::Abs(Target) > FMath::Abs(Speed) && Target * Speed >= 0.f;
	LastSpeed = Speed;
	Speed = FMath::FInterpConstantTo(Speed, Target, DeltaSeconds, bSpeedingUp ? Acceleration : (bStopLatched ? Deceleration * 2.2f : Deceleration));

	// Walking into something: the legs can't outrun the body.
	const float Actual = FVector::DotProduct(GetVelocity(), GetActorForwardVector());
	if (FMath::Abs(Speed) > 50.f && FMath::Abs(Actual) + 120.f < FMath::Abs(Speed) && GetCharacterMovement()->IsMovingOnGround())
	{
		Speed = FMath::Sign(Speed) * (FMath::Abs(Actual) + 120.f);
	}

	const float SpeedFrac = FMath::Clamp(FMath::Abs(Speed) / MaxForwardSpeed, 0.f, 1.f);
	const float MaxTurn = FMath::Lerp(TurnRate.X, TurnRate.Y, SpeedFrac);
	YawRate = FMath::FInterpTo(YawRate, Turn * MaxTurn, DeltaSeconds, bStopLatched ? 10.f : (FMath::IsNearlyZero(Turn) ? 6.f : 3.5f));
	const float YawStep = YawRate * DeltaSeconds;
	if (FMath::Abs(YawStep) > KINDA_SMALL_NUMBER)
	{
		SetActorRotation(FRotator(0.f, GetActorRotation().Yaw + YawStep, 0.f));
		if (bAligning)
		{
			// Aligning keeps the torso on its world heading while the legs swing round beneath it.
			TorsoYaw -= YawStep;
			TorsoYawTarget = FMath::Clamp(TorsoYawTarget - YawStep, -TorsoTwistLimit, TorsoTwistLimit);
		}
	}
	if (FMath::Abs(Speed) > 1.f)
	{
		AddMovementInput(GetActorForwardVector(), Speed / FMath::Max(GetCharacterMovement()->MaxWalkSpeed, 1.f));
	}
}

void ASoulsMech::TickTorso(float DeltaSeconds)
{
	if (State == EMechState::Piloted)
	{
		TorsoYawTarget = FMath::Clamp(TorsoYawTarget + StickLook.X * 95.f * DeltaSeconds, -TorsoTwistLimit, TorsoTwistLimit);
		AimPitchTarget = FMath::Clamp(AimPitchTarget + StickLook.Y * (bInvertStickPitch ? -55.f : 55.f) * DeltaSeconds, AimPitchLimits.X, AimPitchLimits.Y);
	}
	else if (State != EMechState::Boarding)
	{
		TorsoYawTarget = FMath::FInterpTo(TorsoYawTarget, 0.f, DeltaSeconds, 1.5f);
		AimPitchTarget = -12.f;
	}
	// The torso slews at its motor's rate, easing into the last few degrees.
	const float Gap = TorsoYawTarget - TorsoYaw;
	const float Rate = TorsoTwistRate * FMath::Clamp(FMath::Abs(Gap) / 12.f, 0.25f, 1.f);
	TorsoYaw = FMath::FInterpConstantTo(TorsoYaw, TorsoYawTarget, DeltaSeconds, Rate);
	AimPitch = FMath::FInterpTo(AimPitch, AimPitchTarget, DeltaSeconds, 7.f);

	const bool bAiming = State == EMechState::Piloted;
	const FVector Aim = bAiming ? ComputeAimPoint() : FVector::ZeroVector;
	for (int32 Arm = 0; Arm < 2; ++Arm)
	{
		float Desired = -18.f;
		if (bAiming)
		{
			const FVector Local = TorsoMesh->GetComponentTransform().InverseTransformPosition(Aim) - (MechRig::Arm[Arm] - MechRig::Torso);
			Desired = FMath::RadiansToDegrees(FMath::Atan2(Local.Z - MechRig::Muzzle[Arm].Z, FMath::Max(Local.X, 100.f)));
		}
		ArmPitch[Arm] = FMath::FInterpTo(ArmPitch[Arm], FMath::Clamp(Desired, AimPitchLimits.X - 6.f, AimPitchLimits.Y + 6.f), DeltaSeconds, 6.f);
		ArmRecoil[Arm] = FMath::FInterpTo(ArmRecoil[Arm], 0.f, DeltaSeconds, 14.f);
	}
}

FVector ASoulsMech::GroundAt(const FVector& Point, bool* bOutHit) const
{
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MechGround), false, this);
	if (const ASoulsKnight* Knight = Pilot.Get())
	{
		Params.AddIgnoredActor(Knight);
	}
	const bool bHit = GetWorld()->LineTraceSingleByChannel(Hit, Point + FVector(0.f, 0.f, 450.f), Point - FVector(0.f, 0.f, 900.f), ECC_Visibility, Params);
	if (bOutHit)
	{
		*bOutHit = bHit;
	}
	return bHit ? Hit.ImpactPoint : Point;
}

FVector ASoulsMech::FootHome(int32 Leg, const FVector& AtLocation, float AtYaw) const
{
	const FVector Local(MechRig::Foot[Leg].X, MechRig::Foot[Leg].Y, 0.f);
	const FVector Flat = AtLocation + FRotator(0.f, AtYaw, 0.f).RotateVector(Local);
	bool bHit = false;
	const FVector Ground = GroundAt(Flat, &bHit);
	return FVector(Flat.X, Flat.Y, (bHit ? Ground.Z : AtLocation.Z) - MechRig::FootFloor);
}

void ASoulsMech::TickGait(float DeltaSeconds)
{
	const FVector Base = Chassis->GetComponentLocation();
	const float Yaw = GetActorRotation().Yaw;
	const float Now = GetWorld()->GetTimeSeconds();
	if (!bFeetPlaced || FVector::Dist2D(Feet[0].Planted, Base) > 1500.f)
	{
		for (int32 Leg = 0; Leg < 2; ++Leg)
		{
			Feet[Leg].Planted = Feet[Leg].Current = FootHome(Leg, Base, Yaw);
			Feet[Leg].PlantedYaw = Feet[Leg].CurrentYaw = Yaw;
			Feet[Leg].SwingT = -1.f;
		}
		bFeetPlaced = true;
		return;
	}
	const float SpeedFrac = FMath::Clamp(FMath::Abs(Speed) / MaxForwardSpeed, 0.f, 1.f);
	const float StepTime = FMath::Lerp(0.62f, 0.46f, SpeedFrac);
	const FVector Velocity = GetVelocity() * FVector(1.f, 1.f, 0.f);
	auto Predict = [&](int32 Leg, float Ahead, float Lead)
	{
		return FootHome(Leg, Base + Velocity * (Ahead + Lead), Yaw + YawRate * (Ahead + Lead));
	};

	// Start a step: one foot in the air at a time, and the feet take turns. The foot that has stood longest
	// goes next; the one that just landed may only step again (to settle when stopping) once it has stood a
	// while. (UNR-T73: picking simply the foot furthest off re-picked the one that had just landed - at speed
	// it is always behind again at once - so one leg did all the walking while the other never moved.)
	const bool bAnySwinging = Feet[0].IsSwinging() || Feet[1].IsSwinging();
	if (!bAnySwinging)
	{
		const bool bIdle = FMath::Abs(Speed) < 15.f && FMath::Abs(YawRate) < 3.f;
		const float Threshold = bIdle ? 22.f : 34.f;
		auto Error = [&](int32 Leg)
		{
			const FVector Want = Predict(Leg, StepTime, StepTime * 0.85f);
			return FVector::Dist2D(Feet[Leg].Planted, Want)
				+ 4.f * FMath::Abs(FMath::FindDeltaAngleDegrees(Feet[Leg].PlantedYaw, Yaw + YawRate * StepTime * 2.f));
		};
		const int32 Older = Feet[0].LandedAt <= Feet[1].LandedAt ? 0 : 1;
		const int32 Newer = 1 - Older;
		int32 Pick = INDEX_NONE;
		if (Error(Older) > Threshold && Now - Feet[Newer].LandedAt > (bIdle ? 0.2f : 0.03f))
		{
			Pick = Older;
		}
		else if (Error(Newer) > Threshold && Now - Feet[Newer].LandedAt > FMath::Max(0.35f, StepTime * 1.2f))
		{
			Pick = Newer;
		}
		if (Pick != INDEX_NONE)
		{
			FMechFoot& Foot = Feet[Pick];
			Foot.From = Foot.Planted;
			Foot.FromYaw = Foot.PlantedYaw;
			Foot.SwingT = 0.f;
			Foot.SwingTime = StepTime;
		}
	}

	for (int32 Leg = 0; Leg < 2; ++Leg)
	{
		FMechFoot& Foot = Feet[Leg];
		if (!Foot.IsSwinging())
		{
			Foot.Current = Foot.Planted;
			Foot.CurrentYaw = Foot.PlantedYaw;
			Foot.CurrentPitch = FMath::FInterpTo(Foot.CurrentPitch, 0.f, DeltaSeconds, 10.f);
			continue;
		}
		Foot.SwingT = FMath::Min(Foot.SwingT + DeltaSeconds / Foot.SwingTime, 1.f);
		const float Remaining = (1.f - Foot.SwingT) * Foot.SwingTime;
		const FVector Target = Predict(Leg, Remaining, Foot.SwingTime * 0.85f);
		const float TargetYaw = Yaw + YawRate * (Remaining + Foot.SwingTime);
		const float Ease = FMath::InterpEaseInOut(0.f, 1.f, Foot.SwingT, 2.2f);
		const float Lift = FMath::Lerp(38.f, 72.f, SpeedFrac) + (FMath::Abs(YawRate) > 10.f ? 18.f : 0.f);
		Foot.Current = FMath::Lerp(Foot.From, Target, Ease) + FVector(0.f, 0.f, Lift * FMath::Sin(PI * Foot.SwingT));
		Foot.CurrentYaw = Foot.FromYaw + FMath::FindDeltaAngleDegrees(Foot.FromYaw, TargetYaw) * Ease;
		// Toe up as it lifts, flat again to land.
		Foot.CurrentPitch = 14.f * FMath::Sin(PI * FMath::Min(Foot.SwingT * 1.2f, 1.f));
		if (Foot.SwingT >= 1.f)
		{
			Foot.Planted = Target;
			Foot.PlantedYaw = TargetYaw;
			Foot.Current = Target;
			Foot.SwingT = -1.f;
			Foot.LandedAt = Now;
			++FootSteps[Leg];
			Footfall(Leg);
		}
	}
}

void ASoulsMech::Footfall(int32 Leg)
{
	++Footfalls;
	const float SpeedFrac = FMath::Clamp(FMath::Abs(Speed) / MaxForwardSpeed, 0.f, 1.f);
	const FVector Where = Feet[Leg].Planted + FVector(0.f, 0.f, MechRig::FootFloor);
	PlayMechSound(EMechSound::Footstep, Where, FMath::Lerp(0.75f, 1.f, SpeedFrac), FMath::FRandRange(0.92f, 1.06f));
	PelvisDropVelocity -= 70.f + 110.f * SpeedFrac;
	if (IsPlayerControlled())
	{
		Shake(0.22f + 0.3f * SpeedFrac);
	}
	// Dust thrown up round the foot.
	for (int32 Index = 0; Index < 5; ++Index)
	{
		const FVector Out = FRotator(0.f, Index * 72.f + FMath::FRandRange(-20.f, 20.f), 0.f).Vector();
		SpawnPuff(Where + Out * 60.f + FVector(0.f, 0.f, 15.f), Out * FMath::FRandRange(90.f, 180.f) + FVector(0.f, 0.f, 40.f),
			0.45f, 2.4f, FMath::FRandRange(1.2f, 1.8f), 0.32f, false);
	}
	// Anything under the foot is stamped on.
	if (State == EMechState::Piloted)
	{
		for (TActorIterator<ASoulsKnight> It(GetWorld()); It; ++It)
		{
			ASoulsKnight* Other = *It;
			const FVector Off = Other->GetActorLocation() - Where;
			if (Other->Team != 0 && !Other->IsDead() && Off.Size2D() < 180.f && FMath::Abs(Off.Z) < 220.f)
			{
				DamageActor(Other, 34.f, Other->GetActorLocation(), Off.GetSafeNormal2D(), 650.f);
				Other->AddFear(18.f);
			}
		}
	}
}

void ASoulsMech::Shake(float Strength)
{
	ShakeAmount = FMath::Clamp(FMath::Max(ShakeAmount, Strength), 0.f, 1.5f);
}

// ---------------------------------------------------------------------------------------------
// Pose: hips, torso, arms, canopy, and the legs solved to the feet.
// ---------------------------------------------------------------------------------------------

void ASoulsMech::TickPose(float DeltaSeconds)
{
	const float SpeedFrac = FMath::Clamp(FMath::Abs(Speed) / MaxForwardSpeed, 0.f, 1.f);
	float Bob = 0.f, Roll = 0.f, Sway = 0.f;
	for (int32 Leg = 0; Leg < 2; ++Leg)
	{
		if (Feet[Leg].IsSwinging())
		{
			const float S = FMath::Sin(PI * Feet[Leg].SwingT);
			Bob += S * FMath::Lerp(4.f, 13.f, SpeedFrac);
			// Weight goes over the planted foot.
			Roll += (Leg == 0 ? 1.f : -1.f) * S * FMath::Lerp(1.5f, 3.f, SpeedFrac);
			Sway += (Leg == 0 ? -1.f : 1.f) * S * 2.5f * SpeedFrac;
		}
	}
	if (DeltaSeconds > 0.f)
	{
		// The footfall spring: hips dip under each step and settle.
		PelvisDropVelocity += (-PelvisDrop * 140.f - PelvisDropVelocity * 15.f) * DeltaSeconds;
		PelvisDrop = FMath::Clamp(PelvisDrop + PelvisDropVelocity * DeltaSeconds, -35.f, 15.f);
		const float Accel = (Speed - LastSpeed) / DeltaSeconds;
		const float LeanTarget = -(SpeedFrac * 3.f + FMath::Clamp(Accel * 0.01f, -4.f, 4.f)) * FMath::Sign(Speed == 0.f ? 1.f : Speed)
			- 7.f * KneelAlpha - (State == EMechState::Wrecked ? 8.f : 0.f);
		Lean = FMath::FInterpTo(Lean, LeanTarget, DeltaSeconds, 3.f);
	}
	else
	{
		Lean = -7.f * KneelAlpha;
	}

	// Feet in the chassis's frame (the editor's parked pose uses their homes).
	FVector FootC[2];
	float FootYawRel[2], FootPitch[2];
	const FTransform& ChassisXf = Chassis->GetComponentTransform();
	for (int32 Leg = 0; Leg < 2; ++Leg)
	{
		if (bFeetPlaced)
		{
			FootC[Leg] = ChassisXf.InverseTransformPosition(Feet[Leg].Current);
			FootYawRel[Leg] = FMath::FindDeltaAngleDegrees(GetActorRotation().Yaw, Feet[Leg].CurrentYaw);
			FootPitch[Leg] = Feet[Leg].CurrentPitch;
		}
		else
		{
			FootC[Leg] = MechRig::Foot[Leg];
			FootYawRel[Leg] = 0.f;
			FootPitch[Leg] = 0.f;
		}
	}
	// Downhill: the hips sink so the lower leg can still reach.
	if (DeltaSeconds > 0.f)
	{
		const float Low = FMath::Min(FootC[0].Z, FootC[1].Z) - MechRig::Foot[0].Z;
		GroundOffset = FMath::FInterpTo(GroundOffset, FMath::Clamp(Low, -90.f, 30.f), DeltaSeconds, 5.f);
	}
	const float HipZ = FMath::Lerp(MechRig::StandRaise - 22.f * SpeedFrac, -MechRig::KneelDrop, KneelAlpha);
	const FVector PelvisLoc = MechRig::Pelvis + FVector(0.f, 0.f, HipZ + Bob + PelvisDrop + GroundOffset);
	const FRotator PelvisRot(Lean, Sway, Roll);
	PelvisMesh->SetRelativeLocationAndRotation(PelvisLoc, PelvisRot);
	TorsoMesh->SetRelativeLocationAndRotation(MechRig::Torso - MechRig::Pelvis, FRotator(-Lean * 0.6f, TorsoYaw, -Roll * 0.7f));
	CanopyMesh->SetRelativeRotation(FRotator(CanopyAlpha * 42.f, 0.f, 0.f));
	ArmLMesh->SetRelativeLocationAndRotation(MechRig::Arm[0] - MechRig::Torso - FVector(ArmRecoil[0] * 22.f, 0.f, 0.f), FRotator(ArmPitch[0] + ArmRecoil[0] * 3.f, 0.f, 0.f));
	ArmRMesh->SetRelativeLocationAndRotation(MechRig::Arm[1] - MechRig::Torso - FVector(ArmRecoil[1] * 22.f, 0.f, 0.f), FRotator(ArmPitch[1] + ArmRecoil[1] * 3.f, 0.f, 0.f));

	// Legs.
	const MechRig::FLegConstants& K = MechRig::Leg();
	const FTransform PelvisXf(PelvisRot, PelvisLoc);
	for (int32 Leg = 0; Leg < 2; ++Leg)
	{
		const FVector HipC = PelvisXf.TransformPosition(MechRig::Hip[Leg] - MechRig::Pelvis);
		const FVector D = FootC[Leg] - HipC;
		// Swing the leg's plane out about the forward axis so it holds the foot (hip abduction).
		const float Down = FMath::Max(-D.Z, 1.f);
		const float Phi = FMath::Atan2(D.Y, Down);
		const FQuat RollQ = FQuat::FindBetweenNormals(FVector(0.f, 0.f, -1.f), FVector(0.f, FMath::Sin(Phi), -FMath::Cos(Phi)));
		const float Tx = D.X, Tz = -FMath::Sqrt(D.Y * D.Y + D.Z * D.Z) * (D.Z <= 0.f ? 1.f : -1.f);
		// Two-bone solve: the combined thigh+metatarsal (C) and the shin (L2).
		const float Dist = FMath::Clamp(FMath::Sqrt(Tx * Tx + Tz * Tz), FMath::Abs(K.C - K.L2) + 1.f, K.C + K.L2 - 1.f);
		const float Beta = FMath::Atan2(Tz, Tx);
		const float CosA = FMath::Clamp((K.C * K.C + Dist * Dist - K.L2 * K.L2) / (2.f * K.C * Dist), -1.f, 1.f);
		const float Gamma = Beta + K.Bend * FMath::Acos(CosA);
		const float UAng = Gamma - K.PhiC;
		const float WAng = UAng + K.Delta;
		const FVector2D Cvec(K.C * FMath::Cos(Gamma), K.C * FMath::Sin(Gamma));
		const FVector2D Tn(Dist * FMath::Cos(Beta), Dist * FMath::Sin(Beta));
		const FVector2D V = (Tn - Cvec) / K.L2;
		const float VAng = FMath::Atan2(V.Y, V.X);
		const FVector Knee(K.L1 * FMath::Cos(UAng), 0.f, K.L1 * FMath::Sin(UAng));
		const FVector Ankle = Knee + FVector(K.L2 * V.X, 0.f, K.L2 * V.Y);
		const FVector FootP = Ankle + FVector(K.L3 * FMath::Cos(WAng), 0.f, K.L3 * FMath::Sin(WAng));
		auto Segment = [&RollQ](float NewAngle, float RestAngle)
		{
			return (RollQ * FQuat(FRotator(FMath::RadiansToDegrees(NewAngle - RestAngle), 0.f, 0.f))).Rotator();
		};
		const TObjectPtr<UStaticMeshComponent>* Parts = &LegParts[Leg * 4];
		Parts[0]->SetRelativeLocationAndRotation(HipC, Segment(UAng, K.ThighRest));
		Parts[1]->SetRelativeLocationAndRotation(HipC + RollQ.RotateVector(Knee), Segment(VAng, K.ShinRest));
		Parts[2]->SetRelativeLocationAndRotation(HipC + RollQ.RotateVector(Ankle), Segment(WAng, K.MetaRest));
		Parts[3]->SetRelativeLocationAndRotation(HipC + RollQ.RotateVector(FootP), FRotator(FootPitch[Leg], FootYawRel[Leg], 0.f));
	}
}

void ASoulsMech::TickCamera(float DeltaSeconds)
{
	const float Yaw = GetActorRotation().Yaw + TorsoYaw;
	float Pitch = AimPitch * 0.75f - 7.f;
	float Length = 1250.f;
	float ViewYaw = Yaw;
	if (State == EMechState::Boarding || State == EMechState::Exiting)
	{
		ViewYaw = GetActorRotation().Yaw + 125.f; // camera out at the front left, where the knight climbs
		Pitch = -14.f;
		Length = 1500.f;
	}
	CameraBoom->TargetArmLength = FMath::FInterpTo(CameraBoom->TargetArmLength, Length, DeltaSeconds, 3.f);
	CameraBoom->SetWorldRotation(FRotator(Pitch, ViewYaw, 0.f));

	ShakeTime += DeltaSeconds;
	ShakeAmount = FMath::FInterpTo(ShakeAmount, 0.f, DeltaSeconds, 5.5f);
	const float S = ShakeAmount;
	const FVector Offset(0.f, FMath::PerlinNoise1D(ShakeTime * 23.f) * S * 20.f, FMath::PerlinNoise1D(ShakeTime * 29.f + 7.3f) * S * 28.f);
	const FRotator Jolt(FMath::PerlinNoise1D(ShakeTime * 19.f + 3.1f) * S * 1.6f, FMath::PerlinNoise1D(ShakeTime * 17.f + 11.f) * S * 0.9f,
		FMath::PerlinNoise1D(ShakeTime * 13.f + 5.f) * S * 1.2f);
	ChaseCamera->SetRelativeLocationAndRotation(Offset, Jolt);
	// The cockpit sits in the torso: it already bobs with the walk; it adds the aim pitch and the shake.
	CockpitCamera->SetRelativeLocationAndRotation(MechRig::Canopy - MechRig::Torso + FVector(62.f, 0.f, 32.f) + Offset * 0.4f,
		FRotator(AimPitch, 0.f, 0.f) + Jolt * 1.4f);
}

// ---------------------------------------------------------------------------------------------
// Weapons
// ---------------------------------------------------------------------------------------------

FVector ASoulsMech::MuzzleLocation(int32 Arm) const
{
	const UStaticMeshComponent* ArmPart = Arm == 0 ? ArmLMesh : ArmRMesh;
	return ArmPart->GetComponentTransform().TransformPosition(MechRig::Muzzle[Arm]);
}

FVector ASoulsMech::LauncherLocation(int32 Tube) const
{
	return TorsoMesh->GetComponentTransform().TransformPosition(MechRig::Launcher[Tube % 8]);
}

FVector ASoulsMech::ComputeAimPoint(AActor** OutTarget) const
{
	FVector From;
	FRotator Rot;
	const APlayerController* PC = Cast<APlayerController>(GetController());
	if (PC && PC->PlayerCameraManager)
	{
		From = PC->PlayerCameraManager->GetCameraLocation();
		Rot = PC->PlayerCameraManager->GetCameraRotation();
	}
	else
	{
		From = CanopyMesh->GetComponentLocation();
		Rot = FRotator(AimPitch, GetActorRotation().Yaw + TorsoYaw, 0.f);
	}
	const FVector Dir = Rot.Vector();
	// Start the ray past the mech itself (the chase camera is behind it).
	const float Skip = FMath::Max(FVector::DotProduct(GetActorLocation() - From, Dir) + 250.f, 0.f);
	const FVector Start = From + Dir * Skip;
	const FVector End = From + Dir * 30000.f;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MechAim), false, this);
	if (const ASoulsKnight* Knight = Pilot.Get())
	{
		Params.AddIgnoredActor(Knight);
	}
	FHitResult WorldHit, PawnHit;
	const bool bWorld = GetWorld()->LineTraceSingleByChannel(WorldHit, Start, End, ECC_Visibility, Params);
	const bool bPawn = GetWorld()->SweepSingleByObjectType(PawnHit, Start, End, FQuat::Identity, FCollisionObjectQueryParams(ECC_Pawn),
		FCollisionShape::MakeSphere(25.f), Params);
	FVector Point = End;
	AActor* Target = nullptr;
	if (bPawn && (!bWorld || PawnHit.Time <= WorldHit.Time))
	{
		Point = PawnHit.ImpactPoint;
		Target = PawnHit.GetActor();
	}
	else if (bWorld)
	{
		Point = WorldHit.ImpactPoint;
	}
	if (OutTarget)
	{
		// Missile lock: a living foe nearest the reticle, inside a few degrees of it.
		if (!Cast<ASoulsKnight>(Target) || Cast<ASoulsKnight>(Target)->Team == 0)
		{
			Target = nullptr;
			float Best = FMath::Cos(FMath::DegreesToRadians(6.f));
			for (TActorIterator<ASoulsKnight> It(GetWorld()); It; ++It)
			{
				if (It->Team == 0 || It->IsDead() || *It == Pilot.Get())
				{
					continue;
				}
				const FVector To = It->GetActorLocation() - From;
				const float Distance = To.Size();
				const float Dot = FVector::DotProduct(To / FMath::Max(Distance, 1.f), Dir);
				if (Distance < 22000.f && Dot > Best)
				{
					Best = Dot;
					Target = *It;
				}
			}
		}
		*OutTarget = Target;
	}
	return Point;
}

int32 ASoulsMech::TakeRoundVisual(bool bMissile)
{
	for (int32 Index = 0; Index < RoundPool.Num(); ++Index)
	{
		if (!RoundPoolUsed[Index])
		{
			RoundPoolUsed[Index] = true;
			RoundPool[Index]->SetVisibility(true);
			return Index;
		}
	}
	return INDEX_NONE;
}

int32 ASoulsMech::TakeLight(const FVector& Where, const FLinearColor& Color, float Intensity, float Radius, float Fade)
{
	int32 Pick = 0;
	for (int32 Index = 0; Index < LightPool.Num(); ++Index)
	{
		if (LightFade[Index] <= 0.f)
		{
			Pick = Index;
			break;
		}
		if (LightFade[Index] < LightFade[Pick])
		{
			Pick = Index;
		}
	}
	if (!LightPool.IsValidIndex(Pick))
	{
		return INDEX_NONE;
	}
	UPointLightComponent* Light = LightPool[Pick];
	Light->SetWorldLocation(Where);
	Light->SetLightColor(Color);
	Light->SetAttenuationRadius(Radius);
	Light->SetIntensity(Intensity);
	LightFade[Pick] = Fade; // seconds left (negative stays lit until released)
	LightFadeTotal[Pick] = FMath::Max(Fade, KINDA_SMALL_NUMBER);
	LightBase[Pick] = Intensity;
	return Pick;
}

void ASoulsMech::SpawnPuff(const FVector& Where, const FVector& Velocity, float Size, float Grow, float Life, float Opacity, bool bFire)
{
	if (!bFire && !SmokeMaterial)
	{
		return;
	}
	for (int32 Index = 0; Index < PuffPool.Num(); ++Index)
	{
		if (PuffPoolUsed[Index])
		{
			continue;
		}
		PuffPoolUsed[Index] = true;
		UStaticMeshComponent* Comp = PuffPool[Index];
		Comp->SetMaterial(0, bFire ? TracerMaterial.Get() : static_cast<UMaterialInterface*>(PuffMaterials[Index].Get()));
		Comp->SetWorldLocationAndRotation(Where, FRotator(FMath::FRandRange(0.f, 360.f), FMath::FRandRange(0.f, 360.f), 0.f));
		Comp->SetWorldScale3D(FVector(Size * 0.2f));
		Comp->SetVisibility(true);
		FMechPuff Puff;
		Puff.Visual = Index;
		Puff.Location = Where;
		Puff.Velocity = Velocity;
		Puff.Life = Life;
		Puff.Size = Size;
		Puff.Grow = Grow;
		Puff.Opacity = Opacity;
		Puff.bFire = bFire;
		Puffs.Add(Puff);
		return;
	}
}

void ASoulsMech::TickPuffs(float DeltaSeconds)
{
	for (int32 Index = Puffs.Num() - 1; Index >= 0; --Index)
	{
		FMechPuff& Puff = Puffs[Index];
		Puff.Age += DeltaSeconds;
		const float T = Puff.Age / Puff.Life;
		UStaticMeshComponent* Comp = PuffPool[Puff.Visual];
		if (T >= 1.f)
		{
			Comp->SetVisibility(false);
			PuffPoolUsed[Puff.Visual] = false;
			Puffs.RemoveAtSwap(Index);
			continue;
		}
		Puff.Velocity *= FMath::Max(1.f - 1.4f * DeltaSeconds, 0.f);
		Puff.Velocity.Z += (Puff.bFire ? 60.f : 35.f) * DeltaSeconds;
		Puff.Location += Puff.Velocity * DeltaSeconds;
		float Scale;
		if (Puff.bFire)
		{
			Scale = Puff.Size * (0.35f + 1.65f * FMath::Sqrt(T)) * FMath::Pow(1.f - T, 0.7f);
		}
		else
		{
			Scale = Puff.Size * (1.f + Puff.Grow * FMath::Sqrt(T));
			if (UMaterialInstanceDynamic* Mid = PuffMaterials[Puff.Visual])
			{
				Mid->SetScalarParameterValue(TEXT("Opacity"), Puff.Opacity * FMath::Min(T * 8.f, 1.f) * FMath::Pow(1.f - T, 1.5f));
			}
		}
		Comp->SetWorldLocation(Puff.Location);
		Comp->SetWorldScale3D(FVector(Scale));
	}
	for (int32 Index = 0; Index < LightPool.Num(); ++Index)
	{
		if (LightFade[Index] > 0.f)
		{
			LightFade[Index] = FMath::Max(LightFade[Index] - DeltaSeconds, 0.f);
			LightPool[Index]->SetIntensity(LightBase[Index] * FMath::Square(LightFade[Index] / LightFadeTotal[Index]));
		}
	}
	for (int32 Arm = 0; Arm < 2; ++Arm)
	{
		MuzzleFlashT[Arm] = FMath::Max(MuzzleFlashT[Arm] - DeltaSeconds, 0.f);
		const float F = MuzzleFlashT[Arm] / 0.06f;
		MuzzleLights[Arm]->SetIntensity(45000.f * F);
		MuzzleFlashes[Arm]->SetVisibility(F > 0.f);
		if (F > 0.f)
		{
			MuzzleFlashes[Arm]->SetRelativeScale3D(FVector(1.3f, 0.55f, 0.55f) * (0.5f + 0.5f * F) * FMath::FRandRange(0.8f, 1.2f));
		}
	}
}

void ASoulsMech::TickWeapons(float DeltaSeconds)
{
	const float Now = GetWorld()->GetTimeSeconds();
	Heat = FMath::Max(Heat - HeatDissipation * DeltaSeconds, 0.f);
	if (!bOverheated && Heat >= 100.f)
	{
		bOverheated = true;
		Heat = 100.f;
		PlayMechSound(EMechSound::Alarm, GetActorLocation(), 0.6f);
		LastAlarm = Now;
		UE_LOG(LogUnrealSoulslike, Log, TEXT("Mech: overheated, weapons offline"));
	}
	else if (bOverheated && Heat < 40.f)
	{
		bOverheated = false;
	}
	if (bOverheated)
	{
		if (Now - LastAlarm > 1.2f)
		{
			PlayMechSound(EMechSound::Alarm, GetActorLocation(), 0.45f);
			LastAlarm = Now;
		}
		if (FMath::FRand() < DeltaSeconds * 10.f)
		{
			// Venting steam off the back of the torso.
			const FVector Back = TorsoMesh->GetComponentTransform().TransformPosition(FVector(-90.f, FMath::FRandRange(-80.f, 80.f), 120.f));
			SpawnPuff(Back, TorsoMesh->GetForwardVector() * -160.f + FVector(0.f, 0.f, 180.f), 0.4f, 2.f, 1.2f, 0.4f, false);
		}
	}

	CannonCooldown = FMath::Max(CannonCooldown - DeltaSeconds, 0.f);
	if (State == EMechState::Piloted && bCannonHeld && CannonCooldown <= 0.f)
	{
		FireCannon();
		CannonCooldown += 1.f / FMath::Max(CannonRate, 0.5f);
	}
	if (SalvoLeft > 0)
	{
		SalvoTimer -= DeltaSeconds;
		if (SalvoTimer <= 0.f)
		{
			LaunchMissile(NextTube);
			NextTube = (NextTube + 1) % 8;
			--SalvoLeft;
			SalvoTimer = 0.09f;
		}
	}
}

void ASoulsMech::FireCannon()
{
	if (CannonAmmo <= 0 || bOverheated)
	{
		return;
	}
	const int32 Arm = NextArm;
	NextArm = 1 - NextArm;
	const FVector Muzzle = MuzzleLocation(Arm);
	FVector Dir = (ComputeAimPoint() - Muzzle).GetSafeNormal();
	if (Dir.IsNearlyZero())
	{
		Dir = TorsoMesh->GetForwardVector();
	}
	Dir = FMath::VRandCone(Dir, FMath::DegreesToRadians(0.5f));
	FMechRound Round;
	Round.Location = Muzzle;
	Round.Velocity = Dir * 21000.f + GetVelocity();
	Round.Life = 1.6f;
	Round.Visual = TakeRoundVisual(false);
	Rounds.Add(Round);

	MuzzleFlashT[Arm] = 0.06f;
	MuzzleFlashes[Arm]->SetWorldRotation(Dir.Rotation());
	ArmRecoil[Arm] = 1.f;
	ASoulsSparks::Burst(GetWorld(), Muzzle + Dir * 40.f, Dir, 4, 0.7f, 0.35f);
	SpawnPuff(Muzzle + Dir * 50.f, Dir * 300.f + GetVelocity(), 0.35f, 2.6f, 0.7f, 0.35f, false);
	PlayMechSound(EMechSound::Cannon, Muzzle, 0.85f, FMath::FRandRange(0.94f, 1.06f));
	Heat += CannonHeat;
	--CannonAmmo;
	++ShellsFired;
	Shake(0.07f);
}

void ASoulsMech::FireMissiles()
{
	if (State != EMechState::Piloted || Missiles <= 0 || bOverheated || SalvoLeft > 0 || MissileCooldown > GetWorld()->GetTimeSeconds())
	{
		return;
	}
	SalvoLeft = FMath::Min(4, Missiles);
	SalvoTimer = 0.f;
	Heat += MissileSalvoHeat;
	MissileCooldown = GetWorld()->GetTimeSeconds() + 1.2f;
}

void ASoulsMech::LaunchMissile(int32 Tube)
{
	if (Missiles <= 0)
	{
		SalvoLeft = 0;
		return;
	}
	AActor* Lock = nullptr;
	const FVector Aim = ComputeAimPoint(&Lock);
	const FVector Start = LauncherLocation(Tube);
	const FVector Forward = TorsoMesh->GetForwardVector();
	FMechRound Round;
	Round.bMissile = true;
	Round.Location = Start;
	Round.Velocity = (Forward + FVector(0.f, 0.f, 0.32f) + FMath::VRand() * 0.12f).GetSafeNormal() * 2600.f + GetVelocity();
	Round.Life = 5.f;
	Round.Aim = Aim + FMath::VRand() * 110.f;
	Round.Homing = Lock;
	Round.Visual = TakeRoundVisual(true);
	Round.Light = TakeLight(Start, FLinearColor(1.f, 0.55f, 0.2f), 9000.f, 700.f, -1.f);
	Rounds.Add(Round);
	--Missiles;
	++MissilesFired;
	SpawnPuff(Start, Forward * -200.f, 0.6f, 2.5f, 1.4f, 0.5f, false);
	ASoulsSparks::Burst(GetWorld(), Start, -Forward, 5, 0.8f, 0.5f);
	PlayMechSound(EMechSound::MissileLaunch, Start, 0.8f, FMath::FRandRange(0.92f, 1.08f));
	Shake(0.1f);
}

void ASoulsMech::TickRounds(float DeltaSeconds)
{
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MechRound), false, this);
	if (const ASoulsKnight* Knight = Pilot.Get())
	{
		Params.AddIgnoredActor(Knight);
	}
	for (int32 Index = Rounds.Num() - 1; Index >= 0; --Index)
	{
		FMechRound& Round = Rounds[Index];
		Round.Age += DeltaSeconds;
		if (Round.bMissile)
		{
			// Boost, then steer onto the lock (or the aim point) at a limited turn rate.
			const float Speed2 = FMath::Min(Round.Velocity.Size() + 9000.f * DeltaSeconds, 7800.f);
			FVector Dir = Round.Velocity.GetSafeNormal();
			if (Round.Age > 0.18f)
			{
				const FVector Goal = Round.Homing.IsValid() ? Round.Homing->GetActorLocation() + FVector(0.f, 0.f, 40.f) : Round.Aim;
				const FVector Want = (Goal - Round.Location).GetSafeNormal();
				const float MaxTurn = FMath::DegreesToRadians(180.f) * DeltaSeconds;
				const float Angle = FMath::Acos(FMath::Clamp(FVector::DotProduct(Dir, Want), -1.f, 1.f));
				if (Angle > KINDA_SMALL_NUMBER)
				{
					Dir = FMath::Lerp(Dir, Want, FMath::Min(MaxTurn / Angle, 1.f)).GetSafeNormal();
				}
			}
			Round.Velocity = Dir * Speed2;
			Round.NextPuff -= DeltaSeconds;
			if (Round.NextPuff <= 0.f)
			{
				Round.NextPuff = 0.028f;
				SpawnPuff(Round.Location - Dir * 70.f, -Dir * 120.f + FMath::VRand() * 40.f, 0.32f, 2.2f, 1.6f, 0.55f, false);
			}
		}
		else
		{
			Round.Velocity.Z -= 350.f * DeltaSeconds;
		}
		const FVector From = Round.Location;
		const FVector To = From + Round.Velocity * DeltaSeconds;
		FHitResult WorldHit, PawnHit;
		const bool bWorld = GetWorld()->LineTraceSingleByChannel(WorldHit, From, To, ECC_Visibility, Params);
		const bool bPawn = GetWorld()->SweepSingleByObjectType(PawnHit, From, To, FQuat::Identity, FCollisionObjectQueryParams(ECC_Pawn),
			FCollisionShape::MakeSphere(Round.bMissile ? 35.f : 16.f), Params);
		const bool bHit = bWorld || bPawn;
		Round.Location = To;
		if (Round.Visual != INDEX_NONE)
		{
			UStaticMeshComponent* Comp = RoundPool[Round.Visual];
			const FVector Dir = Round.Velocity.GetSafeNormal();
			Comp->SetWorldLocationAndRotation(To - Dir * (Round.bMissile ? 0.f : 60.f), Dir.Rotation());
			Comp->SetWorldScale3D(Round.bMissile ? FVector(0.55f, 0.16f, 0.16f) : FVector(1.6f, 0.07f, 0.07f));
		}
		if (Round.Light != INDEX_NONE && LightPool.IsValidIndex(Round.Light))
		{
			LightPool[Round.Light]->SetWorldLocation(To);
		}
		if (bHit || Round.Age >= Round.Life)
		{
			if (bHit)
			{
				const FHitResult& Hit = (bPawn && (!bWorld || PawnHit.Time <= WorldHit.Time)) ? PawnHit : WorldHit;
				RoundImpact(Round, Hit);
			}
			else if (Round.bMissile)
			{
				Explode(To, FVector::UpVector); // self-destruct at the end of its fuel
			}
			if (Round.Visual != INDEX_NONE)
			{
				RoundPool[Round.Visual]->SetVisibility(false);
				RoundPoolUsed[Round.Visual] = false;
			}
			if (Round.Light != INDEX_NONE && LightPool.IsValidIndex(Round.Light))
			{
				LightPool[Round.Light]->SetIntensity(0.f);
				LightFade[Round.Light] = 0.f;
			}
			Rounds.RemoveAtSwap(Index);
		}
	}
}

void ASoulsMech::RoundImpact(FMechRound& Round, const FHitResult& Hit)
{
	const FVector Dir = Round.Velocity.GetSafeNormal();
	const FVector Point = Hit.ImpactPoint;
	if (Round.bMissile)
	{
		Explode(Point, Hit.ImpactNormal);
		return;
	}
	ASoulsSparks::Burst(GetWorld(), Point, Hit.ImpactNormal, 7, 1.1f, 0.7f);
	SpawnPuff(Point + Hit.ImpactNormal * 25.f, Hit.ImpactNormal * 140.f, 0.35f, 2.4f, 1.f, 0.45f, false);
	DamageActor(Hit.GetActor(), CannonDamage, Point, Dir, 140.f);
	if (UPrimitiveComponent* Comp = Hit.GetComponent())
	{
		if (Comp->IsSimulatingPhysics())
		{
			Comp->AddImpulseAtLocation(Dir * 60000.f, Point);
		}
	}
}

void ASoulsMech::Explode(const FVector& Where, const FVector& Normal)
{
	ASoulsSparks::Burst(GetWorld(), Where, Normal, 26, 2.6f, 1.f, FLinearColor(1.f, 0.55f, 0.18f));
	TakeLight(Where + Normal * 80.f, FLinearColor(1.f, 0.5f, 0.15f), 90000.f, 2200.f, 0.45f);
	for (int32 Index = 0; Index < 5; ++Index)
	{
		SpawnPuff(Where + Normal * 60.f + FMath::VRand() * 70.f, FMath::VRand() * 260.f + Normal * 200.f, FMath::FRandRange(1.4f, 2.2f), 1.f, FMath::FRandRange(0.35f, 0.55f), 1.f, true);
	}
	for (int32 Index = 0; Index < 7; ++Index)
	{
		SpawnPuff(Where + Normal * 80.f + FMath::VRand() * 90.f, FMath::VRand() * 160.f + FVector(0.f, 0.f, 200.f), FMath::FRandRange(1.2f, 1.9f), 1.8f, FMath::FRandRange(2.4f, 3.6f), 0.65f, false);
	}
	PlayMechSound(EMechSound::Explosion, Where, 1.f, FMath::FRandRange(0.9f, 1.1f));
	if (IsPlayerControlled())
	{
		Shake(FMath::Clamp(1.f - FVector::Dist(Where, GetActorLocation()) / 3500.f, 0.f, 1.f) * 0.9f);
	}
	// Blast: falls off with distance; the terrified flee.
	for (TActorIterator<ASoulsKnight> It(GetWorld()); It; ++It)
	{
		ASoulsKnight* Other = *It;
		if (Other == Pilot.Get() || Other->IsDead())
		{
			continue;
		}
		const FVector Off = Other->GetActorLocation() - Where;
		const float Distance = Off.Size();
		if (Distance < MissileRadius)
		{
			const float Falloff = 1.f - 0.65f * Distance / MissileRadius;
			DamageActor(Other, MissileDamage * Falloff, Other->GetActorLocation(), Off.GetSafeNormal(), 900.f * Falloff);
		}
		if (Other->Team != 0 && !Other->IsDead() && Distance < MissileRadius * 2.5f)
		{
			Other->AddFear(22.f);
		}
	}
	TArray<FOverlapResult> Overlaps;
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_PhysicsBody);
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
	if (GetWorld()->OverlapMultiByObjectType(Overlaps, Where, FQuat::Identity, Objects, FCollisionShape::MakeSphere(MissileRadius)))
	{
		for (const FOverlapResult& Overlap : Overlaps)
		{
			if (UPrimitiveComponent* Comp = Overlap.GetComponent())
			{
				if (Comp->IsSimulatingPhysics())
				{
					Comp->AddRadialImpulse(Where, MissileRadius, 150000.f, ERadialImpulseFalloff::RIF_Linear);
				}
			}
		}
	}
	if (FSoulsFire::CanBurnAt(this, Where) && FMath::FRand() < 0.5f)
	{
		FSoulsFire::IgniteAt(this, Where, 120.f);
	}
}

bool ASoulsMech::DamageActor(AActor* Victim, float Damage, const FVector& Where, const FVector& Direction, float Push)
{
	if (!Victim || Victim == this || Damage <= 0.f)
	{
		return false;
	}
	AActor* Causer = Pilot.IsValid() ? static_cast<AActor*>(Pilot.Get()) : this;
	const float Now = GetWorld()->GetTimeSeconds();
	if (ASoulsKnight* Knight = Cast<ASoulsKnight>(Victim))
	{
		// Co-op: no friendly fire on the players' side.
		if (Knight == Pilot.Get() || Knight->Team == 0 || Knight->IsDead())
		{
			return false;
		}
		FDamageEvent Event;
		Knight->TakeDamage(Damage, Event, GetController(), Causer);
		FSoulsBlood::Splash(this, Where, -Direction, Damage > 40.f ? 2.5f : 1.3f);
		if (Push > 0.f && !Knight->IsDead())
		{
			Knight->LaunchCharacter(Direction.GetSafeNormal2D() * Push + FVector(0.f, 0.f, Push * 0.35f), true, false);
		}
		++HitsLanded;
		LastHitMarker = Now;
		return true;
	}
	if (ASoulsMech* OtherMech = Cast<ASoulsMech>(Victim))
	{
		OtherMech->ApplyMechDamage(Damage, Causer, Where);
		++HitsLanded;
		LastHitMarker = Now;
		return true;
	}
	if (ICombatDamageable* Damageable = Cast<ICombatDamageable>(Victim))
	{
		// The template's damage scale is ~12x smaller than ours.
		Damageable->ApplyDamage(Damage / 12.f, Causer, Where, Direction * Push);
		++HitsLanded;
		LastHitMarker = Now;
		return true;
	}
	if (Victim->ActorHasTag(FName(TEXT("MechTarget"))))
	{
		++HitsLanded;
		LastHitMarker = Now;
		return true;
	}
	return false;
}

// ---------------------------------------------------------------------------------------------
// Taking hits, enemies, wreck and reboot
// ---------------------------------------------------------------------------------------------

float ASoulsMech::TakeDamage(float Damage, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	if (DamageCauser == this || (DamageCauser && DamageCauser == Pilot.Get()))
	{
		return 0.f;
	}
	ApplyMechDamage(Damage, DamageCauser, DamageCauser ? DamageCauser->GetActorLocation() : GetActorLocation());
	return Damage;
}

void ASoulsMech::ApplyMechDamage(float Amount, AActor* Causer, const FVector& Where)
{
	if (State == EMechState::Wrecked || Amount <= 0.f)
	{
		return;
	}
	Armor -= Amount;
	LastHitTime = GetWorld()->GetTimeSeconds();
	LastHitFrom = Causer ? Causer->GetActorLocation() : Where;
	const float Scorch = 0.85f * (1.f - GetArmorFraction());
	for (UMaterialInstanceDynamic* Mid : HullMaterials)
	{
		if (Mid)
		{
			Mid->SetScalarParameterValue(TEXT("Scorch"), Scorch);
		}
	}
	PlayMechSound(EMechSound::Clang, Where, FMath::Clamp(0.5f + Amount / 40.f, 0.5f, 1.f), FMath::FRandRange(0.9f, 1.1f));
	if (IsPlayerControlled())
	{
		Shake(0.12f + FMath::Min(Amount * 0.006f, 0.4f));
	}
	if (Armor <= 0.f)
	{
		Armor = 0.f;
		Wreck();
	}
}

void ASoulsMech::TickThreats(float DeltaSeconds)
{
	if (State != EMechState::Piloted && State != EMechState::Boarding && State != EMechState::Exiting)
	{
		SwingSeen.Reset();
		return;
	}
	ASoulsKnight* Knight = Pilot.Get();
	const FVector Center = GetActorLocation();
	ThreatTimer -= DeltaSeconds;
	const bool bScan = ThreatTimer <= 0.f;
	if (bScan)
	{
		ThreatTimer = 0.3f;
	}
	for (TActorIterator<ASoulsEnemy> It(GetWorld()); It; ++It)
	{
		ASoulsEnemy* Foe = *It;
		if (Foe->IsDead())
		{
			continue;
		}
		const FVector To = Center - Foe->GetActorLocation();
		const float Distance = To.Size2D();

		// A war machine stomping about is hard to miss: foes in sight (and further than they'd spot a knight) come for it.
		if (bScan && Knight && State == EMechState::Piloted && Distance < Foe->SightRange * 1.8f
			&& FVector::Dist2D(Center, Foe->HomeLocation) < Foe->LeashRange)
		{
			ASoulsEnemyController* Brain = Cast<ASoulsEnemyController>(Foe->GetController());
			if (Brain && Brain->GetMind() != EEnemyMind::Engage)
			{
				FHitResult Block;
				FCollisionQueryParams Params(SCENE_QUERY_STAT(MechSeen), false, this);
				Params.AddIgnoredActor(Foe);
				Params.AddIgnoredActor(Knight);
				if (!GetWorld()->LineTraceSingleByChannel(Block, Foe->GetActorLocation() + FVector(0.f, 0.f, 70.f), TorsoMesh->GetComponentLocation(), ECC_Visibility, Params))
				{
					Brain->ForceEngage(Knight);
				}
			}
		}

		// Blades on the plating: a swing that reaches its strike while the foe is at the mech's legs lands on the armour.
		FMechSwing& Swing = SwingSeen.FindOrAdd(TWeakObjectPtr<AActor>(Foe));
		const bool bAttacking = Foe->IsAttacking();
		const float Progress = bAttacking ? Foe->GetActionProgress() : 0.f;
		if (!bAttacking || Progress < Swing.LastProgress - 0.05f || Foe->GetAction() != Swing.LastAction)
		{
			Swing.bLanded = false;
		}
		Swing.LastProgress = Progress;
		Swing.LastAction = Foe->GetAction();
		if (bAttacking && !Swing.bLanded && Progress >= 0.45f && Distance < MechRig::CapsuleRadius + Foe->SwordReach + 90.f
			&& FVector::DotProduct(Foe->GetActorForwardVector(), To.GetSafeNormal2D()) > 0.25f)
		{
			Swing.bLanded = true;
			const bool bHeavy = Foe->GetAction() == EKnightAction::HeavyAttack;
			const float Damage = (bHeavy ? Foe->HeavyDamage : Foe->LightDamage) * MeleeArmorFactor * (Foe->bIsBoss ? 1.6f : 1.f);
			const FVector Where = Center - To.GetSafeNormal2D() * MechRig::CapsuleRadius * 0.8f
				+ FVector(0.f, 0.f, FMath::FRandRange(-MechRig::CapsuleHalfHeight * 0.7f, -MechRig::CapsuleHalfHeight * 0.3f));
			ASoulsSparks::Burst(GetWorld(), Where, -To.GetSafeNormal2D(), 12, 1.3f, 0.8f);
			ApplyMechDamage(Damage, Foe, Where);
		}
	}
}

void ASoulsMech::Wreck()
{
	State = EMechState::Wrecked;
	WreckedAt = GetWorld()->GetTimeSeconds();
	bCannonHeld = false;
	SalvoLeft = 0;
	Speed = 0.f;
	Explode(TorsoMesh->GetComponentLocation() + FVector(0.f, 0.f, 80.f), FVector::UpVector);
	UE_LOG(LogUnrealSoulslike, Log, TEXT("Mech: %s wrecked"), *GetName());
	// The pilot is thrown clear.
	if (ASoulsKnight* Knight = Pilot.Get())
	{
		APlayerController* PC = Cast<APlayerController>(GetController());
		Knight->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
		Knight->SetActorHiddenInGame(false);
		const FVector Spot = GetActorLocation() - GetActorRightVector() * 520.f;
		bool bHit = false;
		const FVector Ground = GroundAt(Spot, &bHit);
		Knight->SetActorLocation((bHit ? Ground : Spot) + FVector(0.f, 0.f, Knight->GetSimpleCollisionHalfHeight() + 30.f), false, nullptr, ETeleportType::TeleportPhysics);
		Knight->SetActorEnableCollision(true);
		Knight->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
		if (PC)
		{
			OnDestroyed.RemoveAll(PC);
			Knight->OnDestroyed.RemoveAll(PC);
			// Its keys (A among them) must stop answering before the knight takes over (UNR-T73).
			if (UEnhancedInputLocalPlayerSubsystem* KnightInput = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
			{
				KnightInput->RemoveMappingContext(MechContext);
			}
			PC->Possess(Knight);
			PC->SetControlRotation(FRotator(-15.f, (GetActorLocation() - Knight->GetActorLocation()).Rotation().Yaw, 0.f));
		}
		Knight->LaunchCharacter(-GetActorRightVector() * 500.f + FVector(0.f, 0.f, 400.f), true, true);
		FDamageEvent Event;
		Knight->TakeDamage(25.f, Event, nullptr, this);
	}
	Pilot = nullptr;
	bCockpitView = false;
	ChaseCamera->SetActive(true);
	CockpitCamera->SetActive(false);
}

void ASoulsMech::Reboot()
{
	State = EMechState::Parked;
	Armor = MaxArmor;
	Heat = 0.f;
	bOverheated = false;
	for (UMaterialInstanceDynamic* Mid : HullMaterials)
	{
		if (Mid)
		{
			Mid->SetScalarParameterValue(TEXT("Scorch"), 0.f);
		}
	}
	PlayMechSound(EMechSound::Servo, GetActorLocation(), 0.8f, 1.2f);
	UE_LOG(LogUnrealSoulslike, Log, TEXT("Mech: %s rebooted"), *GetName());
}

// ---------------------------------------------------------------------------------------------
// Sound
// ---------------------------------------------------------------------------------------------

void ASoulsMech::PlayMechSound(EMechSound Sound, const FVector& Where, float Volume, float Pitch) const
{
	const int32 Kind = static_cast<int32>(Sound);
	const TArray<int16>& Samples = MechAudio::Get(Kind);
	if (Samples.IsEmpty() || !GetWorld() || GetWorld()->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	USoundWaveProcedural* Wave = NewObject<USoundWaveProcedural>(GetTransientPackage());
	Wave->SetSampleRate(MechAudio::Rate);
	Wave->NumChannels = 1;
	Wave->Duration = static_cast<float>(Samples.Num()) / MechAudio::Rate;
	Wave->SoundGroup = SOUNDGROUP_Default;
	Wave->bLooping = false;
	Wave->QueueAudio(reinterpret_cast<const uint8*>(Samples.GetData()), Samples.Num() * sizeof(int16));
	UGameplayStatics::PlaySoundAtLocation(this, Wave, Where, Volume, Pitch, 0.f, SoundFalloff.Get());
}

// ---------------------------------------------------------------------------------------------
// HUD: drawn over the game's own HUD through AHUD::OnHUDPostRender (no changes to ASoulsHUD).
// ---------------------------------------------------------------------------------------------

void ASoulsMech::DrawMechHUD(AHUD* HUD, UCanvas* DebugCanvas)
{
	UCanvas* Canvas = DebugCanvas;
	if (!HUD || !Canvas || !HUD->GetWorld() || !HUD->GetWorld()->IsGameWorld())
	{
		return;
	}
	APawn* Pawn = HUD->GetOwningPawn();
	if (const ASoulsMech* Mech = Cast<ASoulsMech>(Pawn))
	{
		Mech->DrawCockpitHUD(Canvas);
		return;
	}
	if (const ASoulsKnight* Knight = Cast<ASoulsKnight>(Pawn))
	{
		if (FindBoardable(Knight))
		{
			const float S = Canvas->ClipY / 1080.f;
			UFont* Font = GEngine->GetLargeFont();
			FCanvasTileItem Back(FVector2D(Canvas->ClipX * 0.5f - 300.f * S, Canvas->ClipY * 0.66f - 10.f * S), FVector2D(600.f * S, 50.f * S), FLinearColor(0.f, 0.f, 0.f, 0.55f));
			Back.BlendMode = SE_BLEND_Translucent;
			Canvas->DrawItem(Back);
			Canvas->K2_DrawText(Font, TEXT("Climb into the mech      [ F ]  /  D-pad Up"), FVector2D(Canvas->ClipX * 0.5f, Canvas->ClipY * 0.66f),
				FVector2D(1.05f * S, 1.05f * S), FLinearColor(0.93f, 0.86f, 0.7f), 0.f, FLinearColor::Black, FVector2D(1.f, 1.f), true, false, true, FLinearColor::Black);
		}
	}
}

void ASoulsMech::DrawCockpitHUD(UCanvas* Canvas) const
{
	const float S = Canvas->ClipY / 1080.f;
	const float Cx = Canvas->ClipX * 0.5f, Cy = Canvas->ClipY * 0.5f;
	const float Now = GetWorld()->GetTimeSeconds();
	UFont* Font = GEngine->GetMediumFont();
	UFont* Big = GEngine->GetLargeFont();
	const FLinearColor Green(0.45f, 1.f, 0.55f, 0.9f);
	const FLinearColor Dim(0.45f, 1.f, 0.55f, 0.35f);
	const FLinearColor Amber(1.f, 0.72f, 0.2f, 0.95f);
	const FLinearColor Red(1.f, 0.18f, 0.12f, 0.95f);
	auto Rect = [Canvas](float X, float Y, float W, float H, const FLinearColor& Color)
	{
		FCanvasTileItem Tile(FVector2D(X, Y), FVector2D(W, H), Color);
		Tile.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(Tile);
	};
	auto Line = [Canvas, S](float X1, float Y1, float X2, float Y2, const FLinearColor& Color, float Width = 2.f)
	{
		Canvas->K2_DrawLine(FVector2D(X1, Y1), FVector2D(X2, Y2), Width * S, Color);
	};
	auto Text = [Canvas, S](UFont* F, const FString& Str, float X, float Y, const FLinearColor& Color, float Scale, bool bCentre)
	{
		Canvas->K2_DrawText(F, Str, FVector2D(X, Y), FVector2D(Scale * S, Scale * S), Color, 0.f, FLinearColor::Black, FVector2D(1.f, 1.f), bCentre, false, true, FLinearColor(0.f, 0.f, 0.f, 0.7f));
	};

	if (State == EMechState::Boarding)
	{
		Text(Big, TEXT("BOOTING ..."), Cx, Cy + 200.f * S, Green, 1.2f, true);
		return;
	}
	if (State != EMechState::Piloted)
	{
		return;
	}
	const FLinearColor Reticle = bOverheated ? Red : Green;
	const APlayerController* PC = Cast<APlayerController>(GetController());

	// Centre reticle.
	const float Gap = 12.f * S, Len = 20.f * S;
	Line(Cx - Gap - Len, Cy, Cx - Gap, Cy, Reticle);
	Line(Cx + Gap, Cy, Cx + Gap + Len, Cy, Reticle);
	Line(Cx, Cy - Gap - Len, Cx, Cy - Gap, Reticle);
	Line(Cx, Cy + Gap, Cx, Cy + Gap + Len, Reticle);
	Rect(Cx - 2.f * S, Cy - 2.f * S, 4.f * S, 4.f * S, Reticle);
	if (Now - LastHitMarker < 0.18f)
	{
		const float A = 18.f * S, B = 30.f * S;
		for (const FVector2D Sign : { FVector2D(1.f, 1.f), FVector2D(-1.f, 1.f), FVector2D(1.f, -1.f), FVector2D(-1.f, -1.f) })
		{
			Line(Cx + Sign.X * A, Cy + Sign.Y * A, Cx + Sign.X * B, Cy + Sign.Y * B, FLinearColor(1.f, 1.f, 1.f, 0.95f), 2.5f);
		}
	}
	// Where the arm guns actually point (they lag the reticle as the arms slew).
	for (int32 Arm = 0; Arm < 2; ++Arm)
	{
		const UStaticMeshComponent* ArmPart = Arm == 0 ? ArmLMesh : ArmRMesh;
		const FVector Muzzle = MuzzleLocation(Arm);
		const FVector Far = Muzzle + ArmPart->GetForwardVector() * 6000.f;
		FVector2D P;
		if (PC && PC->ProjectWorldLocationToScreen(Far, P, true))
		{
			const float R = 9.f * S;
			Line(P.X - R, P.Y - R, P.X + R, P.Y - R, Dim, 1.5f);
			Line(P.X - R, P.Y + R, P.X + R, P.Y + R, Dim, 1.5f);
			Line(P.X - R, P.Y - R, P.X - R, P.Y + R, Dim, 1.5f);
			Line(P.X + R, P.Y - R, P.X + R, P.Y + R, Dim, 1.5f);
		}
	}
	// Missile lock brackets.
	AActor* Lock = nullptr;
	ComputeAimPoint(&Lock);
	if (Lock)
	{
		FVector2D P;
		if (PC && PC->ProjectWorldLocationToScreen(Lock->GetActorLocation(), P, true))
		{
			const float R = 34.f * S, C = 12.f * S;
			for (const FVector2D Sign : { FVector2D(1.f, 1.f), FVector2D(-1.f, 1.f), FVector2D(1.f, -1.f), FVector2D(-1.f, -1.f) })
			{
				Line(P.X + Sign.X * R, P.Y + Sign.Y * R, P.X + Sign.X * (R - C), P.Y + Sign.Y * R, Amber, 2.f);
				Line(P.X + Sign.X * R, P.Y + Sign.Y * R, P.X + Sign.X * R, P.Y + Sign.Y * (R - C), Amber, 2.f);
			}
			Text(Font, TEXT("LOCK"), P.X, P.Y + R + 4.f * S, Amber, 1.f, true);
		}
	}

	// Torso twist: the legs' heading under the torso, MechWarrior style.
	{
		const float W = 460.f * S, Y = Canvas->ClipY - 120.f * S;
		Line(Cx - W * 0.5f, Y, Cx + W * 0.5f, Y, Dim, 2.f);
		for (int32 Tick = -4; Tick <= 4; ++Tick)
		{
			const float X = Cx + Tick * W / 8.f;
			Line(X, Y - (Tick == 0 ? 12.f : 6.f) * S, X, Y, Dim, 1.5f);
		}
		const float Legs = Cx - FMath::Clamp(TorsoYaw / TorsoTwistLimit, -1.f, 1.f) * W * 0.5f;
		Rect(Legs - 7.f * S, Y + 4.f * S, 14.f * S, 14.f * S, FMath::Abs(TorsoYaw) > TorsoTwistLimit - 3.f ? Amber : Green);
		Text(Font, TEXT("LEGS"), Legs, Y + 20.f * S, Dim, 0.9f, true);
	}
	// Throttle / speed, left.
	{
		const float X = 70.f * S, Top = Cy - 160.f * S, H = 320.f * S, Mid = Top + H * (MaxForwardSpeed / (MaxForwardSpeed + MaxReverseSpeed));
		Rect(X, Top, 22.f * S, H, FLinearColor(0.f, 0.f, 0.f, 0.35f));
		const float Fill = Speed >= 0.f ? (Mid - Top) * Speed / MaxForwardSpeed : (Top + H - Mid) * -Speed / MaxReverseSpeed;
		if (Speed >= 0.f)
		{
			Rect(X, Mid - Fill, 22.f * S, Fill, Green);
		}
		else
		{
			Rect(X, Mid, 22.f * S, Fill, Amber);
		}
		Line(X - 6.f * S, Mid, X + 28.f * S, Mid, Green, 2.f);
		Text(Font, FString::Printf(TEXT("%.0f KPH"), Speed * 0.036f), X + 11.f * S, Top + H + 8.f * S, Green, 1.f, true);
	}
	// Heat, right.
	{
		const float X = Canvas->ClipX - 92.f * S, Top = Cy - 160.f * S, H = 320.f * S;
		const float F = FMath::Clamp(Heat / 100.f, 0.f, 1.f);
		const FLinearColor Col = F < 0.6f ? Green : (F < 0.85f ? Amber : Red);
		Rect(X, Top, 22.f * S, H, FLinearColor(0.f, 0.f, 0.f, 0.35f));
		Rect(X, Top + H * (1.f - F), 22.f * S, H * F, Col);
		Text(Font, TEXT("HEAT"), X + 11.f * S, Top + H + 8.f * S, Col, 1.f, true);
		if (bOverheated && FMath::Fmod(Now, 0.5f) < 0.3f)
		{
			Text(Big, TEXT("OVERHEAT - WEAPONS OFFLINE"), Cx, Cy + 90.f * S, Red, 1.2f, true);
		}
	}
	// Armour and ammunition, bottom corners.
	{
		const float X = 60.f * S, Y = Canvas->ClipY - 90.f * S, W = 300.f * S;
		const float F = GetArmorFraction();
		Text(Font, FString::Printf(TEXT("ARMOR  %d%%"), FMath::RoundToInt(F * 100.f)), X, Y - 24.f * S, F > 0.3f ? Green : Red, 1.f, false);
		Rect(X, Y, W, 12.f * S, FLinearColor(0.f, 0.f, 0.f, 0.4f));
		Rect(X, Y, W * F, 12.f * S, F > 0.3f ? Green : Red);
		const float RX = Canvas->ClipX - 320.f * S;
		Text(Font, FString::Printf(TEXT("AUTOCANNON  %d"), CannonAmmo), RX, Y - 24.f * S, CannonAmmo > 0 ? Green : Red, 1.f, false);
		Text(Font, FString::Printf(TEXT("MISSILES    %d"), Missiles), RX, Y, Missiles > 0 ? Green : Red, 1.f, false);
	}
	// Hit warning: a red bar on the side the blow came from.
	if (Now - LastHitTime < 0.6f)
	{
		const float A = 1.f - (Now - LastHitTime) / 0.6f;
		const float ViewYaw = GetActorRotation().Yaw + TorsoYaw;
		const float Rel = FMath::FindDeltaAngleDegrees(ViewYaw, (LastHitFrom - GetActorLocation()).Rotation().Yaw);
		const FLinearColor Col(1.f, 0.1f, 0.05f, 0.55f * A);
		if (FMath::Abs(Rel) > 135.f)
		{
			Rect(0.f, Canvas->ClipY - 16.f * S, Canvas->ClipX, 16.f * S, Col);
		}
		else if (Rel < -45.f)
		{
			Rect(0.f, 0.f, 16.f * S, Canvas->ClipY, Col);
		}
		else if (Rel > 45.f)
		{
			Rect(Canvas->ClipX - 16.f * S, 0.f, 16.f * S, Canvas->ClipY, Col);
		}
		else
		{
			Rect(0.f, 0.f, Canvas->ClipX, 16.f * S, Col);
		}
	}
	Text(Font, TEXT("RT / LMB  cannons    LT / RMB  missiles    X / F  legs to torso    A / X  full stop    R3 / V  view    D-pad Up / E  climb out"),
		Cx, Canvas->ClipY - 46.f * S, Dim, 0.85f, true);
	if (bAligning)
	{
		Text(Font, TEXT("ALIGNING LEGS"), Cx, Canvas->ClipY - 150.f * S, Amber, 0.9f, true);
	}
	else if (bStopLatched)
	{
		Text(Font, TEXT("FULL STOP"), Cx, Canvas->ClipY - 150.f * S, Amber, 0.9f, true);
	}
}

// ---------------------------------------------------------------------------------------------
// -MechDemo
// ---------------------------------------------------------------------------------------------

bool UMechDemoSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	return Super::ShouldCreateSubsystem(Outer) && FParse::Param(FCommandLine::Get(), TEXT("MechDemo"));
}

void UMechDemoSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (!InWorld.IsGameWorld())
	{
		return;
	}
	TWeakObjectPtr<UMechDemoSubsystem> WeakThis(this);
	FTimerHandle Start;
	InWorld.GetTimerManager().SetTimer(Start, [WeakThis]() { if (WeakThis.IsValid()) { WeakThis->Run(); } }, 4.f, false);
}

void UMechDemoSubsystem::Check(const TCHAR* What, bool bPass, const FString& Detail)
{
	(bPass ? Passes : Fails) += 1;
	UE_LOG(LogUnrealSoulslike, Display, TEXT("MechDemo: %s %s (%s)"), What, bPass ? TEXT("PASS") : TEXT("FAIL"), *Detail);
}

void UMechDemoSubsystem::Key(const FKey& InKey, bool bDown)
{
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		PC->InputKey(FInputKeyEventArgs::CreateSimulated(InKey, bDown ? IE_Pressed : IE_Released, bDown ? 1.f : 0.f));
	}
}

void UMechDemoSubsystem::HoldAxis(const FKey& InKey, float Value, float Seconds)
{
	AxisKey = InKey;
	AxisValue = Value;
	AxisUntil = GetWorld()->GetTimeSeconds() + Seconds;
	const TWeakObjectPtr<UMechDemoSubsystem> WeakThis(this);
	GetWorld()->GetTimerManager().SetTimer(AxisTimer, [WeakThis]()
	{
		UMechDemoSubsystem* D = WeakThis.Get();
		if (!D)
		{
			return;
		}
		const bool bDone = D->GetWorld()->GetTimeSeconds() >= D->AxisUntil;
		if (APlayerController* PC = D->GetWorld()->GetFirstPlayerController())
		{
			PC->InputKey(FInputKeyEventArgs::CreateSimulated(D->AxisKey, IE_Axis, bDone ? 0.f : D->AxisValue, 1));
		}
		if (bDone)
		{
			D->GetWorld()->GetTimerManager().ClearTimer(D->AxisTimer);
		}
	}, 0.016f, true);
}

void UMechDemoSubsystem::Run()
{
	UWorld* World = GetWorld();
	GEngine->Exec(World, TEXT("r.MotionBlurQuality 0"));
	ASoulsKnight* Player = Cast<ASoulsKnight>(UGameplayStatics::GetPlayerPawn(World, 0));
	if (!Player)
	{
		Check(TEXT("a knight to drive with"), false, TEXT("the player pawn is not an ASoulsKnight"));
		FPlatformMisc::RequestExit(false, TEXT("MechDemo"));
		return;
	}
	DemoKnight = Player;
	ASoulsMech* Found = nullptr;
	for (TActorIterator<ASoulsMech> It(World); It; ++It)
	{
		if (!Found || It->GetDistanceTo(Player) < Found->GetDistanceTo(Player))
		{
			Found = *It;
		}
	}
	if (!Found)
	{
		// No mech on this map: drop one on the ground ahead of the knight.
		FVector At = Player->GetActorLocation() + Player->GetActorForwardVector().GetSafeNormal2D() * 1400.f;
		FHitResult Hit;
		if (World->LineTraceSingleByChannel(Hit, At + FVector(0.f, 0.f, 2000.f), At - FVector(0.f, 0.f, 4000.f), ECC_Visibility))
		{
			At = Hit.ImpactPoint;
		}
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
		Found = World->SpawnActor<ASoulsMech>(ASoulsMech::StaticClass(), At + FVector(0.f, 0.f, MechRig::CapsuleHalfHeight + 20.f),
			FRotator(0.f, Player->GetActorRotation().Yaw, 0.f), Params);
		UE_LOG(LogUnrealSoulslike, Display, TEXT("MechDemo: no mech on the map, spawned one at %s"), Found ? *Found->GetActorLocation().ToCompactString() : TEXT("(failed)"));
	}
	Check(TEXT("a mech stands on the map"), Found != nullptr, Found ? Found->GetName() : FString(TEXT("none")));
	if (!Found)
	{
		FPlatformMisc::RequestExit(false, TEXT("MechDemo"));
		return;
	}
	DemoMech = Found;

	struct FStep { float Time; TFunction<void(UMechDemoSubsystem*)> Do; };
	auto Shot = [](const TCHAR* Name) { FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("Mech_%s"), Name), true, false); };
	auto AimAt = [](ASoulsMech* M, const FVector& Where)
	{
		// Twist so the chase camera's ray (the reticle) runs through Where.
		const float Yaw = FMath::FindDeltaAngleDegrees(M->GetActorRotation().Yaw, (Where - M->GetActorLocation()).Rotation().Yaw);
		const FVector Cam = M->ChaseCamera->GetComponentLocation();
		const float CamPitch = (Where - Cam).Rotation().Pitch;
		M->SetTorsoTarget(Yaw, (CamPitch + 7.f) / 0.75f);
	};
	auto NearestFoe = [](UWorld* W, const FVector& From, AActor* Not) -> ASoulsEnemy*
	{
		ASoulsEnemy* Best = nullptr;
		for (TActorIterator<ASoulsEnemy> It(W); It; ++It)
		{
			if (!It->IsDead() && !It->bIsBoss && *It != Not && (!Best || FVector::Dist(It->GetActorLocation(), From) < FVector::Dist(Best->GetActorLocation(), From)))
			{
				Best = *It;
			}
		}
		return Best;
	};
	auto PlaceOnGround = [](AActor* Actor, const FVector& At, float Lift)
	{
		FHitResult Hit;
		FVector P = At;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(MechDemoPlace), false, Actor);
		if (Actor->GetWorld()->LineTraceSingleByChannel(Hit, At + FVector(0.f, 0.f, 1500.f), At - FVector(0.f, 0.f, 3000.f), ECC_Visibility, Params))
		{
			P = Hit.ImpactPoint;
		}
		Actor->SetActorLocation(P + FVector(0.f, 0.f, Lift), false, nullptr, ETeleportType::TeleportPhysics);
	};

	const TArray<FStep> Steps = {
		// 1. Park the knight beside the mech and look at it.
		{ 0.f, [PlaceOnGround](UMechDemoSubsystem* D)
			{
				ASoulsMech* M = D->Mech();
				ASoulsKnight* K = D->Knight();
				PlaceOnGround(K, M->GetActorLocation() - M->GetActorRightVector() * 430.f + M->GetActorForwardVector() * 150.f, K->GetSimpleCollisionHalfHeight() + 5.f);
				K->SetActorRotation(FRotator(0.f, (M->GetActorLocation() - K->GetActorLocation()).Rotation().Yaw, 0.f));
				if (AController* C = K->GetController())
				{
					C->SetControlRotation(FRotator(-8.f, (M->GetActorLocation() - K->GetActorLocation()).Rotation().Yaw + 25.f, 0.f));
				}
			} },
		{ 2.5f, [Shot](UMechDemoSubsystem* D)
			{
				D->Check(TEXT("the knight can board from beside it"), ASoulsMech::FindBoardable(D->Knight()) == D->Mech(),
					FString::Printf(TEXT("distance %.0f cm"), FVector::Dist2D(D->Knight()->GetActorLocation(), D->Mech()->GetActorLocation())));
				Shot(TEXT("1_parked"));
			} },
		// 2. Interact: climb in (the hook ASoulsKnight::InteractPressed calls).
		{ 3.f, [](UMechDemoSubsystem* D) { D->Check(TEXT("interact starts the climb"), ASoulsMech::TryBoard(D->Knight()), TEXT("TryBoard")); } },
		{ 4.1f, [Shot](UMechDemoSubsystem* D) { Shot(TEXT("2_climbing")); } },
		{ 6.4f, [Shot](UMechDemoSubsystem* D)
			{
				ASoulsMech* M = D->Mech();
				APlayerController* PC = D->GetWorld()->GetFirstPlayerController();
				D->Check(TEXT("boarded: the player drives the mech, the knight is aboard and hidden"),
					M->IsPiloted() && PC && PC->GetPawn() == M && D->Knight()->IsHidden() && M->GetPilot() == D->Knight(),
					FString::Printf(TEXT("state=%d pawn=%s hidden=%d"), (int32)M->GetState(), PC && PC->GetPawn() ? *PC->GetPawn()->GetName() : TEXT("none"), D->Knight()->IsHidden()));
				D->StartLocation = M->GetActorLocation();
				D->StartYaw = M->GetActorRotation().Yaw;
				Shot(TEXT("3_aboard"));
			} },
		// 3. Walk: hold W (through the real input mapping) for four seconds; both legs must take steps.
		{ 6.6f, [](UMechDemoSubsystem* D) { D->Key(EKeys::W, true); } },
		{ 8.9f, [Shot](UMechDemoSubsystem* D) { Shot(TEXT("4_walking")); } },
		{ 9.25f, [Shot](UMechDemoSubsystem* D) { Shot(TEXT("4b_stride")); } },
		{ 10.6f, [](UMechDemoSubsystem* D)
			{
				D->Key(EKeys::W, false);
				ASoulsMech* M = D->Mech();
				const float Moved = FVector::Dist2D(M->GetActorLocation(), D->StartLocation);
				const int32 L = M->GetFootSteps(0), R = M->GetFootSteps(1);
				D->Check(TEXT("W walks the mech forward"), Moved > 500.f, FString::Printf(TEXT("moved %.0f cm, speed %.0f"), Moved, M->GetForwardSpeed()));
				D->Check(TEXT("both legs step, taking turns"), L >= 2 && R >= 2 && FMath::Abs(L - R) <= 2,
					FString::Printf(TEXT("left foot %d steps, right foot %d"), L, R));
			} },
		// 4. Turn the legs: hold D.
		{ 11.6f, [](UMechDemoSubsystem* D) { D->StartYaw = D->Mech()->GetActorRotation().Yaw; D->Key(EKeys::D, true); } },
		{ 12.6f, [Shot](UMechDemoSubsystem* D) { Shot(TEXT("5_turning")); } },
		{ 13.4f, [](UMechDemoSubsystem* D)
			{
				D->Key(EKeys::D, false);
				const float Turned = FMath::FindDeltaAngleDegrees(D->StartYaw, D->Mech()->GetActorRotation().Yaw);
				D->Check(TEXT("D turns the legs"), Turned > 25.f, FString::Printf(TEXT("turned %.0f deg"), Turned));
			} },
		// 5. Twist the torso over the standing legs, past its stop.
		{ 14.2f, [](UMechDemoSubsystem* D) { D->StartYaw = D->Mech()->GetActorRotation().Yaw; D->Mech()->SetTorsoTarget(150.f, 0.f); } },
		{ 15.6f, [Shot](UMechDemoSubsystem* D)
			{
				ASoulsMech* M = D->Mech();
				const float LegsTurned = FMath::Abs(FMath::FindDeltaAngleDegrees(D->StartYaw, M->GetActorRotation().Yaw));
				D->Check(TEXT("the torso twists on its own and stops at its limit"),
					M->GetTorsoYaw() > 85.f && M->GetTorsoYaw() <= M->TorsoTwistLimit + 0.1f && LegsTurned < 5.f,
					FString::Printf(TEXT("torso %.0f deg, limit %.0f, legs moved %.1f deg"), M->GetTorsoYaw(), M->TorsoTwistLimit, LegsTurned));
				Shot(TEXT("6_torso_twist"));
			} },
		// 6. X (gamepad): the legs come round under the torso, which keeps pointing the same way.
		{ 15.8f, [](UMechDemoSubsystem* D)
			{
				ASoulsMech* M = D->Mech();
				D->StartYaw = M->GetActorRotation().Yaw;
				D->ProbeYaw = M->GetActorRotation().Yaw + M->GetTorsoYaw();
				D->Key(EKeys::Gamepad_FaceButton_Left, true);
			} },
		{ 15.9f, [](UMechDemoSubsystem* D) { D->Key(EKeys::Gamepad_FaceButton_Left, false); } },
		{ 16.7f, [Shot](UMechDemoSubsystem* D) { Shot(TEXT("7_aligning")); } },
		{ 19.0f, [Shot](UMechDemoSubsystem* D)
			{
				ASoulsMech* M = D->Mech();
				const float LegsTurned = FMath::Abs(FMath::FindDeltaAngleDegrees(D->StartYaw, M->GetActorRotation().Yaw));
				const float TorsoDrift = FMath::Abs(FMath::FindDeltaAngleDegrees(D->ProbeYaw, M->GetActorRotation().Yaw + M->GetTorsoYaw()));
				D->Check(TEXT("X turns the legs to face the torso (torso holds its heading)"),
					!M->IsAligning() && FMath::Abs(M->GetTorsoYaw()) < 4.f && LegsTurned > 70.f && TorsoDrift < 8.f,
					FString::Printf(TEXT("legs turned %.0f deg, torso now %.1f deg off the legs, torso heading drifted %.1f deg"), LegsTurned, M->GetTorsoYaw(), TorsoDrift));
				Shot(TEXT("7b_aligned"));
				M->SetTorsoTarget(-45.f, 0.f);
			} },
		// ...and F on the keyboard does the same.
		{ 20.2f, [](UMechDemoSubsystem* D) { D->StartYaw = D->Mech()->GetActorRotation().Yaw; D->Key(EKeys::F, true); } },
		{ 20.3f, [](UMechDemoSubsystem* D) { D->Key(EKeys::F, false); } },
		{ 22.2f, [](UMechDemoSubsystem* D)
			{
				ASoulsMech* M = D->Mech();
				const float LegsTurned = FMath::FindDeltaAngleDegrees(D->StartYaw, M->GetActorRotation().Yaw);
				D->Check(TEXT("F turns the legs to face the torso"), FMath::Abs(M->GetTorsoYaw()) < 4.f && LegsTurned < -30.f,
					FString::Printf(TEXT("legs turned %.0f deg, torso %.1f deg off the legs"), LegsTurned, M->GetTorsoYaw()));
				M->SetTorsoTarget(0.f, 0.f);
			} },
		// 7. Pitch: the right stick and the mouse, through the real mappings (UNR-T73: up aims up).
		{ 23.0f, [](UMechDemoSubsystem* D) { D->Probe = D->Mech()->GetAimPitch(); D->HoldAxis(EKeys::Gamepad_RightY, 1.f, 0.6f); } },
		{ 23.8f, [](UMechDemoSubsystem* D)
			{
				ASoulsMech* M = D->Mech();
				const float Delta = M->GetAimPitch() - D->Probe;
				const float Want = M->bInvertStickPitch ? -1.f : 1.f;
				D->Check(TEXT("right stick Y pitches the aim the playtested way"), Delta * Want > 3.f,
					FString::Printf(TEXT("RightY +1 moved the aim %+.1f deg (invert %d)"), Delta, M->bInvertStickPitch));
				M->SetTorsoTarget(0.f, 0.f);
			} },
		{ 24.6f, [](UMechDemoSubsystem* D) { D->Probe = D->Mech()->GetAimPitch(); D->HoldAxis(EKeys::MouseY, 3.f, 0.4f); } },
		{ 25.3f, [](UMechDemoSubsystem* D)
			{
				ASoulsMech* M = D->Mech();
				const float Delta = M->GetAimPitch() - D->Probe;
				const float Want = M->bInvertMousePitch ? -1.f : 1.f;
				D->Check(TEXT("mouse Y pitches the aim the same way as the stick"), Delta * Want > 1.f,
					FString::Printf(TEXT("MouseY +3/frame moved the aim %+.1f deg (invert %d)"), Delta, M->bInvertMousePitch));
				M->SetTorsoTarget(0.f, 0.f);
			} },
		// 8. A foe ahead; RT (through the mapping) fires the autocannons, and only them.
		{ 25.8f, [NearestFoe, PlaceOnGround, AimAt](UMechDemoSubsystem* D)
			{
				ASoulsMech* M = D->Mech();
				const FVector Ahead = M->GetActorLocation() + M->GetActorForwardVector() * 2600.f;
				AActor* Target = NearestFoe(D->GetWorld(), Ahead, nullptr);
				if (!Target)
				{
					// No enemies on this map: a block to shoot at.
					FActorSpawnParameters Params;
					Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
					AStaticMeshActor* Block = D->GetWorld()->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), Ahead, FRotator::ZeroRotator, Params);
					Block->SetMobility(EComponentMobility::Movable);
					Block->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
					Block->SetActorScale3D(FVector(1.5f, 1.5f, 2.5f));
					Block->Tags.Add(FName(TEXT("MechTarget")));
					Target = Block;
					PlaceOnGround(Block, Ahead, 125.f);
				}
				else
				{
					PlaceOnGround(Target, Ahead, 100.f);
					Target->SetActorRotation(FRotator(0.f, (M->GetActorLocation() - Ahead).Rotation().Yaw, 0.f));
					Cast<ASoulsEnemy>(Target)->HomeLocation = Target->GetActorLocation();
					D->TargetHealth = Cast<ASoulsKnight>(Target)->GetHealth();
				}
				D->DemoTarget = Target;
				AimAt(M, Target->GetActorLocation());
			} },
		{ 26.6f, [AimAt](UMechDemoSubsystem* D) { if (D->DemoTarget.IsValid()) { AimAt(D->Mech(), D->DemoTarget->GetActorLocation()); } } },
		{ 27.1f, [](UMechDemoSubsystem* D) { D->ProbeCount = D->Mech()->GetMissilesFired(); D->Key(EKeys::Gamepad_RightTrigger, true); } },
		{ 27.35f, [Shot](UMechDemoSubsystem* D) { Shot(TEXT("8_autocannon")); } },
		{ 27.9f, [Shot, AimAt](UMechDemoSubsystem* D) { Shot(TEXT("9_impacts")); if (D->DemoTarget.IsValid()) { AimAt(D->Mech(), D->DemoTarget->GetActorLocation()); } } },
		{ 28.9f, [](UMechDemoSubsystem* D)
			{
				D->Key(EKeys::Gamepad_RightTrigger, false);
				ASoulsMech* M = D->Mech();
				const ASoulsKnight* Foe = Cast<ASoulsKnight>(D->DemoTarget.Get());
				D->Check(TEXT("RT fires the autocannons (and not the missiles)"), M->GetShellsFired() >= 8 && M->GetMissilesFired() == D->ProbeCount,
					FString::Printf(TEXT("%d shells, %d missiles, heat %.0f%%"), M->GetShellsFired(), M->GetMissilesFired() - D->ProbeCount, M->GetHeatFraction() * 100.f));
				D->Check(TEXT("shells hit the target"), M->GetHitsLanded() > 0 && (!Foe || Foe->GetHealth() < D->TargetHealth),
					FString::Printf(TEXT("%d hits, target health %.0f -> %.0f"), M->GetHitsLanded(), D->TargetHealth, Foe ? Foe->GetHealth() : 0.f));
			} },
		// 9. LT fires a missile salvo (and not the cannons) at the next foe, or the same spot.
		{ 29.4f, [NearestFoe, PlaceOnGround, AimAt](UMechDemoSubsystem* D)
			{
				ASoulsMech* M = D->Mech();
				const FVector Ahead = M->GetActorLocation() + FRotator(0.f, M->GetActorRotation().Yaw + 25.f, 0.f).Vector() * 3200.f;
				if (ASoulsEnemy* Foe = NearestFoe(D->GetWorld(), Ahead, nullptr))
				{
					PlaceOnGround(Foe, Ahead, 100.f);
					Foe->HomeLocation = Foe->GetActorLocation();
					D->DemoTarget = Foe;
					D->TargetHealth = Foe->GetHealth();
				}
				AimAt(M, D->DemoTarget.IsValid() ? D->DemoTarget->GetActorLocation() : Ahead);
			} },
		{ 30.2f, [AimAt](UMechDemoSubsystem* D)
			{
				if (D->DemoTarget.IsValid()) { AimAt(D->Mech(), D->DemoTarget->GetActorLocation()); }
				D->ArmorBefore = static_cast<float>(D->Mech()->GetHitsLanded());
				D->ProbeCount = D->Mech()->GetShellsFired();
				D->Key(EKeys::Gamepad_LeftTrigger, true);
			} },
		{ 30.35f, [](UMechDemoSubsystem* D) { D->Key(EKeys::Gamepad_LeftTrigger, false); } },
		{ 30.7f, [Shot](UMechDemoSubsystem* D) { Shot(TEXT("10_missiles")); } },
		{ 31.3f, [Shot](UMechDemoSubsystem* D) { Shot(TEXT("11_explosions")); } },
		{ 33.8f, [](UMechDemoSubsystem* D)
			{
				ASoulsMech* M = D->Mech();
				D->Check(TEXT("LT fires a missile salvo (and not the cannons)"), M->GetMissilesFired() >= 4 && M->GetShellsFired() == D->ProbeCount,
					FString::Printf(TEXT("%d missiles, %d shells"), M->GetMissilesFired(), M->GetShellsFired() - D->ProbeCount));
				D->Check(TEXT("missiles strike"), M->GetHitsLanded() > static_cast<int32>(D->ArmorBefore) || !Cast<ASoulsKnight>(D->DemoTarget.Get()),
					FString::Printf(TEXT("hits %d -> %d"), static_cast<int32>(D->ArmorBefore), M->GetHitsLanded()));
				M->SetTorsoTarget(0.f, 0.f);
			} },
		// 10. A (gamepad) is a full stop, even with the throttle still held; then the X key does the same.
		{ 34.3f, [](UMechDemoSubsystem* D) { D->Key(EKeys::W, true); } },
		{ 35.7f, [](UMechDemoSubsystem* D)
			{
				D->Probe = D->Mech()->GetForwardSpeed();
				D->Key(EKeys::Gamepad_FaceButton_Bottom, true);
			} },
		{ 35.8f, [](UMechDemoSubsystem* D) { D->Key(EKeys::Gamepad_FaceButton_Bottom, false); } },
		{ 38.6f, [Shot](UMechDemoSubsystem* D)
			{
				ASoulsMech* M = D->Mech();
				const float Moving = M->GetVelocity().Size2D();
				D->Check(TEXT("A brings the mech to a full stop with W still held"), D->Probe > 250.f && FMath::Abs(M->GetForwardSpeed()) < 15.f && Moving < 40.f && M->IsFullStopped(),
					FString::Printf(TEXT("speed %.0f -> %.0f (moving %.0f cm/s), stopped=%d"), D->Probe, M->GetForwardSpeed(), Moving, M->IsFullStopped()));
				Shot(TEXT("12_full_stop"));
				D->Key(EKeys::W, false);
			} },
		{ 39.2f, [](UMechDemoSubsystem* D) { D->Key(EKeys::W, true); } },
		{ 40.6f, [](UMechDemoSubsystem* D)
			{
				D->Probe = D->Mech()->GetForwardSpeed();
				D->Key(EKeys::X, true);
			} },
		{ 40.7f, [](UMechDemoSubsystem* D) { D->Key(EKeys::X, false); } },
		{ 42.4f, [](UMechDemoSubsystem* D)
			{
				ASoulsMech* M = D->Mech();
				D->Check(TEXT("the X key is a full stop too (and W drives again after it)"), D->Probe > 250.f && FMath::Abs(M->GetForwardSpeed()) < 15.f,
					FString::Printf(TEXT("speed %.0f -> %.0f"), D->Probe, M->GetForwardSpeed()));
				D->Key(EKeys::W, false);
			} },
		// 11. An enemy at its feet attacks it (its post moved here, so its leash doesn't drag it home).
		{ 44.0f, [NearestFoe, PlaceOnGround](UMechDemoSubsystem* D)
			{
				ASoulsMech* M = D->Mech();
				D->ArmorBefore = M->GetArmor();
				const FVector At = M->GetActorLocation() + M->GetActorForwardVector() * 330.f;
				if (ASoulsEnemy* Foe = NearestFoe(D->GetWorld(), At, nullptr))
				{
					PlaceOnGround(Foe, At, 100.f);
					Foe->HomeLocation = Foe->GetActorLocation();
					Foe->SetActorRotation(FRotator(0.f, (M->GetActorLocation() - At).Rotation().Yaw, 0.f));
					if (ASoulsEnemyController* Brain = Cast<ASoulsEnemyController>(Foe->GetController()))
					{
						Brain->ForceEngage(D->Knight());
					}
					D->DemoTarget = Foe;
				}
				else
				{
					D->DemoTarget = nullptr;
				}
				M->SetTorsoTarget(-60.f, -10.f);
			} },
		{ 47.5f, [Shot](UMechDemoSubsystem* D) { Shot(TEXT("13_under_attack")); } },
		{ 52.0f, [](UMechDemoSubsystem* D)
			{
				ASoulsMech* M = D->Mech();
				if (D->DemoTarget.IsValid())
				{
					D->Check(TEXT("an enemy attacks the mech and the armour takes it"), M->GetArmor() < D->ArmorBefore,
						FString::Printf(TEXT("armour %.0f -> %.0f"), D->ArmorBefore, M->GetArmor()));
				}
				else
				{
					UE_LOG(LogUnrealSoulslike, Display, TEXT("MechDemo: an enemy attacks the mech SKIP (no enemies left alive)"));
				}
				M->SetTorsoTarget(0.f, 0.f);
			} },
		// 12. Climb out (E through the mapping) and get the knight's own controls back.
		{ 52.5f, [](UMechDemoSubsystem* D) { D->Key(EKeys::E, true); } },
		{ 52.6f, [](UMechDemoSubsystem* D) { D->Key(EKeys::E, false); } },
		{ 53.6f, [Shot](UMechDemoSubsystem* D) { Shot(TEXT("14_climbing_out")); } },
		{ 55.5f, [Shot](UMechDemoSubsystem* D)
			{
				ASoulsMech* M = D->Mech();
				ASoulsKnight* K = D->Knight();
				APlayerController* PC = D->GetWorld()->GetFirstPlayerController();
				const float Away = FVector::Dist2D(K->GetActorLocation(), M->GetActorLocation());
				D->Check(TEXT("E climbs out: the knight is back, visible, beside the mech"),
					PC && PC->GetPawn() == K && !K->IsHidden() && M->GetState() == EMechState::Parked && Away > 250.f && Away < 900.f,
					FString::Printf(TEXT("pawn=%s hidden=%d state=%d %.0f cm away"), PC && PC->GetPawn() ? *PC->GetPawn()->GetName() : TEXT("none"), K->IsHidden(), (int32)M->GetState(), Away));
				Shot(TEXT("15_disembarked"));
			} },
		// A moment for the knight's controls to come back, then A must jump.
		{ 56.25f, [](UMechDemoSubsystem* D) { D->Key(EKeys::Gamepad_FaceButton_Bottom, true); } },
		{ 57.00f, [](UMechDemoSubsystem* D)
			{
				ASoulsKnight* K = D->Knight();
				if (APlayerController* PC = D->GetWorld()->GetFirstPlayerController())
				{
					if (UEnhancedInputLocalPlayerSubsystem* Sub = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
					{
						UE_LOG(LogUnrealSoulslike, Display, TEXT("MechDemo: after climbing out: subsystem=%d, pawn=%s, movement mode=%d, input component=%d"),
							Sub ? 1 : 0, PC->GetPawn() ? *PC->GetPawn()->GetName() : TEXT("none"),
							static_cast<int32>(K->GetCharacterMovement()->MovementMode.GetValue()), K->InputComponent ? 1 : 0);
					}
				}
				D->Check(TEXT("the knight's controls are back (A jumps)"), K->GetCharacterMovement()->IsFalling(), FString::Printf(TEXT("vz=%.0f"), K->GetVelocity().Z));
				D->Key(EKeys::Gamepad_FaceButton_Bottom, false);
			} },
		{ 57.5f, [](UMechDemoSubsystem* D)
			{
				ASoulsMech* M = D->Mech();
				UE_LOG(LogUnrealSoulslike, Display, TEXT("MechDemo: steps taken: left foot %d, right foot %d"), M->GetFootSteps(0), M->GetFootSteps(1));
				UE_LOG(LogUnrealSoulslike, Display, TEXT("MechDemo: %d passed, %d failed"), D->Passes, D->Fails);
				UE_LOG(LogUnrealSoulslike, Display, TEXT("MECH_DEMO_DONE"));
				FPlatformMisc::RequestExit(false, TEXT("MechDemo"));
			} },
	};
	const TWeakObjectPtr<UMechDemoSubsystem> WeakThis(this);
	for (const FStep& Step : Steps)
	{
		const TFunction<void(UMechDemoSubsystem*)> Do = Step.Do;
		if (Step.Time <= 0.f)
		{
			Do(this);
			continue;
		}
		FTimerHandle Handle;
		World->GetTimerManager().SetTimer(Handle, [WeakThis, Do]()
		{
			if (WeakThis.IsValid() && WeakThis->Mech() && WeakThis->Knight())
			{
				Do(WeakThis.Get());
			}
		}, Step.Time, false);
	}
}
