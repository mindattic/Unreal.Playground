#include "SoulsFighter.h"
#include "SoulsKnight.h"
#include "SoulsEnemy.h"
#include "SoulsSparks.h"
#include "SoulsBlood.h"
#include "CombatDamageable.h"
#include "Camera/CameraComponent.h"
#include "CanvasItem.h"
#include "Components/AudioComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Canvas.h"
#include "Engine/CollisionProfile.h"
#include "Engine/DamageEvents.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/LocalPlayer.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
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
// The rig: hardpoints baked by tools/convert_fighter.py (SourceArt/Environment/Fighter/fighter_rig.json), in Unreal
// space, cm, about the ship's origin; +X forward, +Y right. Guns and legs: 0 = left (-Y), 1 = right (+Y).
// ---------------------------------------------------------------------------------------------
namespace FighterRig
{
	const FVector CanopyHinge(-83.f, 0.f, 94.3f);
	// Nose, left and right legs (pivot where each meets the belly); their pads touch down at GearFloor.
	const FVector Gear[3] = { FVector(308.f, 0.f, -87.6f), FVector(-126.f, -133.f, -33.5f), FVector(-126.f, 133.f, -33.5f) };
	constexpr float GearFloor = -245.4f;
	const FVector Muzzle[2] = { FVector(702.4f, -205.1f, -72.1f), FVector(702.4f, 205.1f, -72.1f) };
	const FVector Engine[4] = { FVector(-358.4f, -47.6f, 16.8f), FVector(-358.4f, 47.6f, 16.8f), FVector(-238.f, -177.8f, -16.8f), FVector(-238.f, 177.8f, -16.8f) };
	const FVector Seat(32.1f, 0.f, -41.8f);
	const FVector Eye(52.f, 0.f, 78.f);
	// Points that scrape the terrain: gun tips, wing tips, tail, belly, spine.
	const FVector Probes[7] = { FVector(700.f, -205.f, -72.f), FVector(700.f, 205.f, -72.f), FVector(-260.f, -345.f, -195.f),
		FVector(-260.f, 345.f, -195.f), FVector(-490.f, 0.f, 40.f), FVector(60.f, 0.f, -200.f), FVector(-200.f, 0.f, 170.f) };

	const FVector DroneMuzzle[2] = { FVector(254.9f, -26.3f, -8.8f), FVector(254.9f, 26.3f, -8.8f) };
	const FVector DroneEngine(-216.9f, 0.f, 12.2f);
	constexpr float DroneFloor = -74.3f;
	const FVector DroneProbes[3] = { FVector(255.f, 0.f, 0.f), FVector(-40.f, -440.f, 0.f), FVector(-40.f, 440.f, 0.f) };

	constexpr float BodyRadius = 230.f;
	constexpr float DroneRadius = 210.f;
	constexpr float BoardReach = 650.f;
	constexpr float SeaZ = 0.f;
	constexpr float LiftHeight = 1000.f;       // the vertical take-off rises this far before flying off
	constexpr float LiftSeconds = 2.3f;
	const TCHAR* const MeshFolder = TEXT("/Game/Island/Models/Fighter");
	const TCHAR* const GlowPath = TEXT("/Game/Island/Materials/Fighter/M_FighterGlow.M_FighterGlow");
	const TCHAR* const SmokePath = TEXT("/Game/Island/Materials/Fighter/M_FighterSmoke.M_FighterSmoke");
	const TCHAR* const MechGlowPath = TEXT("/Game/Island/Materials/Mech/M_MechTracer.M_MechTracer");
	const TCHAR* const MechSmokePath = TEXT("/Game/Island/Materials/Mech/M_MechSmoke.M_MechSmoke");
	const TCHAR* const GraceGlowPath = TEXT("/Game/Souls/FX/M_GraceGlow.M_GraceGlow");
	const TCHAR* const PartNames[5] = { TEXT("Hull"), TEXT("Canopy"), TEXT("GearN"), TEXT("GearL"), TEXT("GearR") };

	FCollisionObjectQueryParams Terrain()
	{
		FCollisionObjectQueryParams Objects;
		Objects.AddObjectTypesToQuery(ECC_WorldStatic);
		Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
		return Objects;
	}
}

namespace FighterAudio
{
	constexpr int32 Rate = 22050;

	struct FFighterLowPass { float Y = 0.f; float Step(float X, float A) { Y += A * (X - Y); return Y; } };

	TArray<int16> Normalise(const TArray<float>& Buffer, bool bFadeTail)
	{
		float Peak = 0.01f;
		for (const float Sample : Buffer)
		{
			Peak = FMath::Max(Peak, FMath::Abs(Sample));
		}
		TArray<int16> Out;
		Out.SetNumUninitialized(Buffer.Num());
		for (int32 i = 0; i < Buffer.Num(); ++i)
		{
			const float Tail = bFadeTail ? FMath::Clamp((Buffer.Num() - i) / (0.02f * Rate), 0.f, 1.f) : 1.f;
			Out[i] = static_cast<int16>(FMath::Clamp(Buffer[i] / Peak * 0.85f * Tail, -1.f, 1.f) * 30000.f);
		}
		return Out;
	}

	TArray<int16> Synth(int32 Kind)
	{
		float Seconds = 0.4f;
		switch (Kind)
		{
		case 0: Seconds = 0.32f; break; // laser
		case 1: Seconds = 0.36f; break; // drone laser
		case 2: Seconds = 1.1f; break;  // missile
		case 3: Seconds = 2.2f; break;  // explosion
		case 4: Seconds = 0.7f; break;  // scrape
		case 5: Seconds = 0.9f; break;  // canopy
		case 6: Seconds = 0.09f; break; // lock tone
		case 7: Seconds = 1.2f; break;  // boost
		default: Seconds = 0.25f; break; // zap
		}
		const int32 Count = static_cast<int32>(Seconds * Rate);
		TArray<float> Buffer;
		Buffer.SetNumZeroed(Count);
		FRandomStream Rng(4321 + Kind);
		FFighterLowPass Lp, Lp2;
		double Phase = 0.0, Phase2 = 0.0;
		for (int32 i = 0; i < Count; ++i)
		{
			const float T = static_cast<float>(i) / Rate;
			const float Noise = Rng.FRandRange(-1.f, 1.f);
			float S = 0.f;
			switch (Kind)
			{
			case 0: // a bright falling zip with a ringing overtone
			case 1: // the drones' is lower and buzzier
			{
				const bool bDroneShot = Kind == 1;
				const float F = bDroneShot ? FMath::Lerp(1300.f, 180.f, FMath::Min(T / 0.22f, 1.f)) : FMath::Lerp(2600.f, 340.f, FMath::Min(T / 0.2f, 1.f));
				Phase += 2.0 * PI * F / Rate;
				Phase2 += 2.0 * PI * F * 1.503 / Rate;
				const float Env = FMath::Min(T * 400.f, 1.f) * FMath::Exp(-T * (bDroneShot ? 11.f : 14.f));
				const float Square = FMath::Sin(Phase) > 0.0 ? 1.f : -1.f;
				S += (FMath::Sin(Phase) * 0.6f + Square * (bDroneShot ? 0.35f : 0.15f) + FMath::Sin(Phase2) * 0.25f) * Env;
				S += Noise * FMath::Exp(-T * 90.f) * 0.3f;
				break;
			}
			case 2: // missile: a pop and a roaring whoosh pulling away
			{
				S += Noise * FMath::Exp(-T * 60.f) * 0.6f;
				const float Env = FMath::Min(T * 25.f, 1.f) * FMath::Exp(-T * 2.4f);
				S += Lp.Step(Noise, FMath::Lerp(0.4f, 0.07f, FMath::Min(T, 1.f))) * Env * 1.8f;
				break;
			}
			case 3: // explosion: deep boom, roar and crackle
			{
				Phase += 2.0 * PI * FMath::Lerp(52.f, 26.f, FMath::Min(T, 1.f)) / Rate;
				S += FMath::Sin(Phase) * FMath::Exp(-T * 2.6f) * 0.9f;
				S += Lp.Step(Noise, 0.05f) * FMath::Exp(-T * 1.8f) * 3.2f;
				S += Noise * FMath::Exp(-T * 28.f) * 0.7f;
				if (Rng.FRand() < 0.002f * FMath::Exp(-T * 2.f))
				{
					S += Rng.FRandRange(-0.8f, 0.8f);
				}
				break;
			}
			case 4: // scrape: grinding metal and a clang
			{
				const float Grind = Noise - Lp.Step(Noise, 0.3f);
				S += Grind * (0.5f + 0.5f * FMath::Sin(2.f * PI * 37.f * T)) * FMath::Exp(-T * 3.5f);
				S += FMath::Sin(2.f * PI * 412.f * T) * FMath::Exp(-T * 9.f) * 0.4f;
				S += FMath::Sin(2.f * PI * 1093.f * T) * FMath::Exp(-T * 14.f) * 0.25f;
				S += Lp2.Step(Noise, 0.05f) * FMath::Exp(-T * 10.f) * 2.f;
				break;
			}
			case 5: // canopy: a pneumatic hiss and a motor
			{
				const float Hiss = Noise - Lp.Step(Noise, 0.2f);
				const float Env = FMath::Min(T * 12.f, 1.f) * FMath::Clamp((0.8f - T) * 6.f, 0.f, 1.f);
				Phase += FMath::Lerp(120.f, 210.f, FMath::Min(T / 0.8f, 1.f)) / Rate;
				const float Saw = static_cast<float>(2.0 * (Phase - FMath::FloorToDouble(Phase)) - 1.0);
				S += Hiss * Env * 0.35f + Lp2.Step(Saw, 0.15f) * Env * 0.5f;
				break;
			}
			case 6: // lock tone: a clean beep
			{
				S += FMath::Sin(2.f * PI * 1320.f * T) * FMath::Min(T * 300.f, 1.f) * FMath::Clamp((Seconds - T) * 300.f, 0.f, 1.f);
				break;
			}
			case 7: // boost: a rising roar
			{
				const float Env = FMath::Min(T * 6.f, 1.f) * FMath::Exp(-FMath::Max(T - 0.3f, 0.f) * 2.5f);
				S += Lp.Step(Noise, FMath::Lerp(0.05f, 0.3f, FMath::Min(T / 0.4f, 1.f))) * Env * 2.f;
				Phase += 2.0 * PI * FMath::Lerp(70.f, 140.f, FMath::Min(T / 0.5f, 1.f)) / Rate;
				S += FMath::Sin(Phase) * Env * 0.4f;
				break;
			}
			default: // zap: a bolt striking
			{
				S += Noise * FMath::Exp(-T * 30.f) * 0.8f;
				S += FMath::Sin(2.f * PI * 860.f * T + Noise * 0.4f) * FMath::Exp(-T * 24.f) * 0.5f;
				break;
			}
			}
			Buffer[i] = S;
		}
		return Normalise(Buffer, true);
	}

	const TArray<int16>& Get(int32 Kind)
	{
		static TArray<int16> Cache[static_cast<int32>(EFighterSound::Count)];
		if (Cache[Kind].IsEmpty())
		{
			Cache[Kind] = Synth(Kind);
		}
		return Cache[Kind];
	}

	// One second that loops seamlessly: every partial is a whole number of cycles per second.
	const TArray<int16>& EngineLoop()
	{
		static TArray<int16> Loop = []()
		{
			const int32 Count = Rate;
			TArray<float> Buffer;
			Buffer.SetNumZeroed(Count);
			FRandomStream Rng(77);
			struct FPartial { float Hz; float Amp; float Phase; };
			TArray<FPartial> Partials = { { 62.f, 0.55f, 0.f }, { 124.f, 0.35f, 1.1f }, { 186.f, 0.18f, 2.3f }, { 310.f, 0.08f, 0.4f },
				{ 931.f, 0.05f, 0.9f }, { 1862.f, 0.025f, 2.f } };
			for (int32 Index = 0; Index < 70; ++Index)
			{
				// A band of rushing air: random partials, also whole cycles per second.
				Partials.Add(FPartial{ static_cast<float>(Rng.RandRange(240, 1500)), static_cast<float>(0.03f * Rng.FRandRange(0.4f, 1.f)), static_cast<float>(Rng.FRandRange(0.f, 2.f * PI)) });
			}
			for (int32 i = 0; i < Count; ++i)
			{
				const float T = static_cast<float>(i) / Rate;
				float S = 0.f;
				for (const FPartial& P : Partials)
				{
					S += FMath::Sin(2.f * PI * P.Hz * T + P.Phase) * P.Amp;
				}
				// A slow throb, also periodic in the second.
				S *= 0.85f + 0.15f * FMath::Sin(2.f * PI * 6.f * T);
				Buffer[i] = S;
			}
			return Normalise(Buffer, false);
		}();
		return Loop;
	}
}

FDelegateHandle ASoulsFighter::HUDHandle;
int32 ASoulsFighter::HUDUsers = 0;

// ---------------------------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------------------------

ASoulsFighter::ASoulsFighter()
{
	PrimaryActorTick.bCanEverTick = true;
	AutoPossessAI = EAutoPossessAI::Disabled;
	AIControllerClass = nullptr;
	bUseControllerRotationYaw = false;
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;

	Body = CreateDefaultSubobject<USphereComponent>(TEXT("Body"));
	Body->InitSphereRadius(FighterRig::BodyRadius);
	Body->SetCollisionProfileName(UCollisionProfile::Pawn_ProfileName);
	Body->SetCanEverAffectNavigation(false);
	Body->SetSimulatePhysics(false);
	SetRootComponent(Body);

	Frame = CreateDefaultSubobject<USceneComponent>(TEXT("Frame"));
	Frame->SetupAttachment(Body);

	auto MakePart = [this](const TCHAR* Name, USceneComponent* Parent)
	{
		UStaticMeshComponent* Part = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Part->SetupAttachment(Parent);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->SetCanEverAffectNavigation(false);
		Part->SetGenerateOverlapEvents(false);
		return Part;
	};
	HullMesh = MakePart(TEXT("Hull"), Frame);
	CanopyMesh = MakePart(TEXT("Canopy"), Frame);
	CanopyMesh->SetRelativeLocation(FighterRig::CanopyHinge);
	for (int32 Leg = 0; Leg < 3; ++Leg)
	{
		UStaticMeshComponent* Part = MakePart(*FString::Printf(TEXT("Gear%d"), Leg), Frame);
		Part->SetRelativeLocation(FighterRig::Gear[Leg]);
		GearMeshes.Add(Part);
	}
	for (int32 Index = 0; Index < 4; ++Index)
	{
		UStaticMeshComponent* Glow = MakePart(*FString::Printf(TEXT("EngineGlow%d"), Index), Frame);
		Glow->SetRelativeLocation(FighterRig::Engine[Index]);
		Glow->SetCastShadow(false);
		Glow->SetVisibility(false);
		EngineGlows.Add(Glow);
	}
	EngineLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("EngineLight"));
	EngineLight->SetupAttachment(Frame);
	EngineLight->SetRelativeLocation(FVector(-480.f, 0.f, 0.f));
	EngineLight->SetCastShadows(false);
	EngineLight->SetAttenuationRadius(1400.f);
	EngineLight->SetLightColor(FLinearColor(0.45f, 0.65f, 1.f));
	EngineLight->SetIntensity(0.f);
	for (int32 Gun = 0; Gun < 2; ++Gun)
	{
		UPointLightComponent* Light = CreateDefaultSubobject<UPointLightComponent>(Gun == 0 ? TEXT("MuzzleLightL") : TEXT("MuzzleLightR"));
		Light->SetupAttachment(Frame);
		Light->SetRelativeLocation(FighterRig::Muzzle[Gun] + FVector(40.f, 0.f, 0.f));
		Light->SetCastShadows(false);
		Light->SetAttenuationRadius(900.f);
		Light->SetLightColor(FLinearColor(0.4f, 1.f, 0.55f));
		Light->SetIntensity(0.f);
		MuzzleLights.Add(Light);
		UStaticMeshComponent* Flash = MakePart(Gun == 0 ? TEXT("MuzzleFlashL") : TEXT("MuzzleFlashR"), Frame);
		Flash->SetRelativeLocation(FighterRig::Muzzle[Gun] + FVector(40.f, 0.f, 0.f));
		Flash->SetCastShadow(false);
		Flash->SetVisibility(false);
		MuzzleFlashes.Add(Flash);
	}

	ChaseCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("ChaseCamera"));
	ChaseCamera->SetupAttachment(Body);
	ChaseCamera->SetUsingAbsoluteLocation(true);
	ChaseCamera->SetUsingAbsoluteRotation(true);
	ChaseCamera->SetFieldOfView(80.f);

	CockpitCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("CockpitCamera"));
	CockpitCamera->SetupAttachment(Frame);
	CockpitCamera->SetRelativeLocation(FighterRig::Eye);
	CockpitCamera->SetFieldOfView(95.f);
	CockpitCamera->SetAutoActivate(false);

	EngineAudio = CreateDefaultSubobject<UAudioComponent>(TEXT("EngineAudio"));
	EngineAudio->SetupAttachment(Frame);
	EngineAudio->SetRelativeLocation(FVector(-350.f, 0.f, 0.f));
	EngineAudio->SetAutoActivate(false);
	EngineAudio->bAllowSpatialization = true;
}

void ASoulsFighter::LoadParts()
{
	if (bPartsLoaded)
	{
		return;
	}
	bPartsLoaded = true;
	int32 Found = 0;
	if (bDrone)
	{
		const FString Path = FString::Printf(TEXT("%s/SM_Fighter_Drone.SM_Fighter_Drone"), FighterRig::MeshFolder);
		if (UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet))
		{
			DroneMesh = Mesh;
		}
		if (DroneMesh)
		{
			HullMesh->SetStaticMesh(DroneMesh);
			Found = 5;
		}
	}
	else
	{
		UStaticMeshComponent* Parts[5] = { HullMesh, CanopyMesh, GearMeshes[0], GearMeshes[1], GearMeshes[2] };
		for (int32 Index = 0; Index < 5; ++Index)
		{
			const FString Path = FString::Printf(TEXT("%s/SM_Fighter_%s.SM_Fighter_%s"), FighterRig::MeshFolder, FighterRig::PartNames[Index], FighterRig::PartNames[Index]);
			if (UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet))
			{
				Parts[Index]->SetStaticMesh(Mesh);
				++Found;
			}
		}
		// The drones' mesh too, so a packaged game cooks it with the map (drones are spawned, not placed).
		const FString DronePath = FString::Printf(TEXT("%s/SM_Fighter_Drone.SM_Fighter_Drone"), FighterRig::MeshFolder);
		if (UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *DronePath, nullptr, LOAD_NoWarn | LOAD_Quiet))
		{
			DroneMesh = Mesh;
		}
	}
	bRealArt = Found == 5;
	if (!bRealArt)
	{
		UE_LOG(LogUnrealSoulslike, Warning, TEXT("Fighter %s: %d of 5 part meshes under %s (run tools/convert_fighter.py, then rebuild the island)"),
			*GetName(), Found, FighterRig::MeshFolder);
	}
	if (UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"), nullptr, LOAD_NoWarn | LOAD_Quiet))
	{
		SphereMesh = Sphere;
	}
	for (const TCHAR* Path : { FighterRig::GlowPath, FighterRig::MechGlowPath, FighterRig::GraceGlowPath })
	{
		if (UMaterialInterface* Glow = LoadObject<UMaterialInterface>(nullptr, Path, nullptr, LOAD_NoWarn | LOAD_Quiet))
		{
			GlowMaterial = Glow;
			break;
		}
	}
	for (const TCHAR* Path : { FighterRig::SmokePath, FighterRig::MechSmokePath })
	{
		if (UMaterialInterface* Smoke = LoadObject<UMaterialInterface>(nullptr, Path, nullptr, LOAD_NoWarn | LOAD_Quiet))
		{
			SmokeMaterial = Smoke;
			break;
		}
	}
	for (UStaticMeshComponent* Flash : MuzzleFlashes)
	{
		Flash->SetStaticMesh(SphereMesh);
		Flash->SetMaterial(0, GlowMaterial);
	}
	for (UStaticMeshComponent* Glow : EngineGlows)
	{
		Glow->SetStaticMesh(SphereMesh);
		Glow->SetMaterial(0, GlowMaterial);
	}
	if (bDrone)
	{
		ApplyDroneLook();
	}
}

void ASoulsFighter::ApplyDroneLook()
{
	Body->SetSphereRadius(FighterRig::DroneRadius);
	CanopyMesh->SetVisibility(false);
	for (UStaticMeshComponent* Leg : GearMeshes)
	{
		Leg->SetVisibility(false);
	}
	for (int32 Index = 0; Index < EngineGlows.Num(); ++Index)
	{
		EngineGlows[Index]->SetRelativeLocation(FighterRig::DroneEngine);
		if (Index > 0)
		{
			EngineGlows[Index]->SetStaticMesh(nullptr);
		}
	}
	EngineLight->SetRelativeLocation(FighterRig::DroneEngine - FVector(80.f, 0.f, 0.f));
	EngineLight->SetLightColor(FLinearColor(1.f, 0.35f, 0.15f));
	for (int32 Gun = 0; Gun < 2; ++Gun)
	{
		MuzzleLights[Gun]->SetRelativeLocation(FighterRig::DroneMuzzle[Gun] + FVector(40.f, 0.f, 0.f));
		MuzzleLights[Gun]->SetLightColor(FLinearColor(1.f, 0.25f, 0.15f));
		MuzzleFlashes[Gun]->SetRelativeLocation(FighterRig::DroneMuzzle[Gun] + FVector(40.f, 0.f, 0.f));
	}
}

void ASoulsFighter::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	bPartsLoaded = false;
	LoadParts();
	GearAlpha = bDrone ? 0.f : 1.f;
	CanopyAlpha = 0.f;
	TickParts(0.f);
}

void ASoulsFighter::BeginPlay()
{
	Super::BeginPlay();
	LoadParts();
	Hull = MaxHull;
	Missiles = MaxMissiles;
	FlightRotation = GetActorQuat();

	SoundFalloff = NewObject<USoundAttenuation>(this);
	SoundFalloff->Attenuation.bAttenuate = true;
	SoundFalloff->Attenuation.bSpatialize = true;
	SoundFalloff->Attenuation.AttenuationShapeExtents = FVector(900.f, 0.f, 0.f);
	SoundFalloff->Attenuation.FalloffDistance = bDrone ? 14000.f : 9000.f;

	if (HullMesh->GetStaticMesh())
	{
		HullMaterials.Add(HullMesh->CreateAndSetMaterialInstanceDynamic(0));
	}
	if (CanopyMesh->GetStaticMesh())
	{
		HullMaterials.Add(CanopyMesh->CreateAndSetMaterialInstanceDynamic(0));
	}
	if (GlowMaterial)
	{
		BoltMaterial = UMaterialInstanceDynamic::Create(GlowMaterial, this);
		BoltMaterial->SetVectorParameterValue(TEXT("Color"), bDrone ? FLinearColor(18.f, 2.2f, 1.2f) : FLinearColor(3.f, 18.f, 6.f));
		EnemyBoltMaterial = BoltMaterial;
		EngineMaterial = UMaterialInstanceDynamic::Create(GlowMaterial, this);
		EngineMaterial->SetVectorParameterValue(TEXT("Color"), bDrone ? FLinearColor(12.f, 3.5f, 1.2f) : FLinearColor(3.f, 6.f, 16.f));
		for (UStaticMeshComponent* Glow : EngineGlows)
		{
			Glow->SetMaterial(0, EngineMaterial);
		}
		for (UStaticMeshComponent* Flash : MuzzleFlashes)
		{
			Flash->SetMaterial(0, BoltMaterial);
		}
	}
	BuildPools();

	// The engine: a seamless procedural loop, pitched and swelled with the throttle.
	FighterAudio::EngineLoop();
	EngineWave = NewObject<USoundWaveProcedural>(this);
	EngineWave->SetSampleRate(FighterAudio::Rate);
	EngineWave->NumChannels = 1;
	EngineWave->Duration = INDEFINITELY_LOOPING_DURATION;
	EngineWave->SoundGroup = SOUNDGROUP_Default;
	EngineWave->bLooping = false;
	EngineWave->OnSoundWaveProceduralUnderflow.BindLambda([](USoundWaveProcedural* Wave, int32 SamplesRequired)
	{
		const TArray<int16>& Loop = FighterAudio::EngineLoop();
		Wave->QueueAudio(reinterpret_cast<const uint8*>(Loop.GetData()), Loop.Num() * sizeof(int16));
	});
	{
		const TArray<int16>& Loop = FighterAudio::EngineLoop();
		EngineWave->QueueAudio(reinterpret_cast<const uint8*>(Loop.GetData()), Loop.Num() * sizeof(int16));
	}
	EngineAudio->SetSound(EngineWave);
	EngineAudio->AttenuationSettings = SoundFalloff;

	if (bDrone)
	{
		State = EFighterState::Drone;
		Flight = EFighterFlight::Flying;
		GearAlpha = GearTarget = 0.f;
		ThrottleSetting = 0.7f;
		FlightSpeed = CruiseSpeed * 0.6f;
		FlightVelocity = FlightRotation.GetForwardVector() * FlightSpeed;
	}
	else
	{
		State = EFighterState::Parked;
		Flight = EFighterFlight::Grounded;
		GearAlpha = GearTarget = 1.f;
		SettleOnGround(0.f, true);
		HomeTransform = GetActorTransform();
		bHomeSet = true;
		if (HUDUsers++ == 0)
		{
			HUDHandle = AHUD::OnHUDPostRender.AddStatic(&ASoulsFighter::DrawFighterHUD);
		}
		// Bring the drones: a few circling the island, unless some fighter already has.
		bool bAny = false;
		for (TActorIterator<ASoulsFighter> It(GetWorld()); It; ++It)
		{
			bAny |= It->IsDrone();
		}
		if (!bAny && DroneCount > 0 && GetWorld()->IsGameWorld())
		{
			float Highest = 0.f;
			for (int32 Index = 0; Index < 16; ++Index)
			{
				const float A = 2.f * PI * Index / 16.f;
				FVector P, N;
				if (GroundBelow(DroneOrbitCentre + FVector(FMath::Cos(A), FMath::Sin(A), 0.f) * DroneOrbitRadius + FVector(0.f, 0.f, 60000.f), P, N, 90000.f))
				{
					Highest = FMath::Max(Highest, P.Z);
				}
			}
			FVector P, N;
			if (GroundBelow(DroneOrbitCentre + FVector(0.f, 0.f, 60000.f), P, N, 90000.f))
			{
				Highest = FMath::Max(Highest, P.Z);
			}
			const float Altitude = FMath::Max(Highest + 4500.f, 7000.f);
			for (int32 Index = 0; Index < DroneCount; ++Index)
			{
				const float A = 2.f * PI * Index / DroneCount;
				const FVector Where = DroneOrbitCentre + FVector(FMath::Cos(A), FMath::Sin(A), 0.f) * DroneOrbitRadius + FVector(0.f, 0.f, Altitude + Index * 600.f);
				if (ASoulsFighter* Drone = SpawnDrone(GetWorld(), Where, FMath::RadiansToDegrees(A) + 90.f, this, Index))
				{
					Drone->DroneAltitude = Altitude + Index * 600.f;
				}
			}
			UE_LOG(LogUnrealSoulslike, Log, TEXT("Fighter: %d drones circling at %.0f cm"), DroneCount, Altitude);
		}
	}
	UE_LOG(LogUnrealSoulslike, Log, TEXT("Fighter %s %s at %s (art %s)"), *GetName(), bDrone ? TEXT("drone up") : TEXT("parked"),
		*GetActorLocation().ToCompactString(), bRealArt ? TEXT("ok") : TEXT("MISSING"));
}

ASoulsFighter* ASoulsFighter::SpawnDrone(UWorld* World, const FVector& Where, float Yaw, ASoulsFighter* InLeader, int32 Index)
{
	if (!World)
	{
		return nullptr;
	}
	const FTransform Xf(FRotator(0.f, Yaw, 0.f), Where);
	ASoulsFighter* Drone = World->SpawnActorDeferred<ASoulsFighter>(ASoulsFighter::StaticClass(), Xf, nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
	if (!Drone)
	{
		return nullptr;
	}
	Drone->bDrone = true;
	Drone->Leader = InLeader;
	Drone->DroneAngle = Index;
	Drone->DroneCount = 0;
	Drone->MaxHull = 70.f;
	Drone->CruiseSpeed = 5600.f;
	Drone->BoostSpeed = 5600.f;
	Drone->TurnRates = FVector(75.f, 62.f, 200.f);
	Drone->Agility = 4.f;
	Drone->LaserDamage = 6.f;
	Drone->LaserRate = 3.5f;
	Drone->LaserSpeed = 22000.f;
	Drone->BankAngle = 50.f;
	if (InLeader)
	{
		Drone->DroneOrbitCentre = InLeader->DroneOrbitCentre;
		Drone->DroneOrbitRadius = InLeader->DroneOrbitRadius;
		Drone->DroneRespawnSeconds = InLeader->DroneRespawnSeconds;
	}
	Drone->FinishSpawning(Xf);
	return Drone;
}

void ASoulsFighter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (EngineWave)
	{
		EngineWave->OnSoundWaveProceduralUnderflow.Unbind();
	}
	if (EngineAudio)
	{
		EngineAudio->Stop();
	}
	if (!bDrone && --HUDUsers <= 0)
	{
		HUDUsers = 0;
		AHUD::OnHUDPostRender.Remove(HUDHandle);
		HUDHandle.Reset();
	}
	Super::EndPlay(EndPlayReason);
}

void ASoulsFighter::BuildPools()
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
	const int32 BoltCount = bDrone ? 16 : 64;
	const int32 PuffCount = bDrone ? 28 : 110;
	const int32 LightCount = bDrone ? 3 : 8;
	for (int32 Index = 0; Index < BoltCount; ++Index)
	{
		BoltPool.Add(MakeLoose(BoltMaterial ? static_cast<UMaterialInterface*>(BoltMaterial.Get()) : GlowMaterial.Get()));
	}
	BoltPoolUsed.Init(false, BoltCount);
	for (int32 Index = 0; Index < PuffCount; ++Index)
	{
		UMaterialInstanceDynamic* Mid = SmokeMaterial ? UMaterialInstanceDynamic::Create(SmokeMaterial, this) : nullptr;
		PuffMaterials.Add(Mid);
		PuffPool.Add(MakeLoose(Mid ? static_cast<UMaterialInterface*>(Mid) : GlowMaterial.Get()));
	}
	PuffPoolUsed.Init(false, PuffCount);
	for (int32 Index = 0; Index < LightCount; ++Index)
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
// Input: one set of actions and one context shared by every fighter (rooted), added to the player while they fly and
// removed when they climb out.
// ---------------------------------------------------------------------------------------------

void ASoulsFighter::EnsureFighterInput()
{
	if (FighterContext)
	{
		return;
	}
	static TMap<FName, UInputAction*> FighterSharedActions;
	static UInputMappingContext* FighterSharedContext = nullptr;
	auto MakeAction = [](const TCHAR* Name, EInputActionValueType Type)
	{
		if (UInputAction** Existing = FighterSharedActions.Find(FName(Name)))
		{
			return *Existing;
		}
		UInputAction* NewAction = NewObject<UInputAction>(GetTransientPackage(), Name);
		NewAction->ValueType = Type;
		NewAction->AddToRoot();
		FighterSharedActions.Add(FName(Name), NewAction);
		return NewAction;
	};
	ThrottleAction = MakeAction(TEXT("IA_FighterThrottle"), EInputActionValueType::Axis1D);
	RollAction = MakeAction(TEXT("IA_FighterRoll"), EInputActionValueType::Axis1D);
	RudderAction = MakeAction(TEXT("IA_FighterRudder"), EInputActionValueType::Axis1D);
	LookMouseAction = MakeAction(TEXT("IA_FighterLookMouse"), EInputActionValueType::Axis2D);
	LookStickAction = MakeAction(TEXT("IA_FighterLookStick"), EInputActionValueType::Axis2D);
	FireAction = MakeAction(TEXT("IA_FighterFire"), EInputActionValueType::Boolean);
	MissileAction = MakeAction(TEXT("IA_FighterMissile"), EInputActionValueType::Boolean);
	BoostAction = MakeAction(TEXT("IA_FighterBoost"), EInputActionValueType::Boolean);
	BrakeAction = MakeAction(TEXT("IA_FighterBrake"), EInputActionValueType::Boolean);
	InteractAction = MakeAction(TEXT("IA_FighterInteract"), EInputActionValueType::Boolean);
	ViewAction = MakeAction(TEXT("IA_FighterView"), EInputActionValueType::Boolean);

	if (FighterSharedContext)
	{
		FighterContext = FighterSharedContext;
		return;
	}
	UInputMappingContext* Context = NewObject<UInputMappingContext>(GetTransientPackage(), TEXT("IMC_Fighter"));
	Context->AddToRoot();
	FighterSharedContext = Context;
	FighterContext = Context;

	auto Negate = [Context]() { return NewObject<UInputModifierNegate>(Context); };
	auto DeadZone = [Context]()
	{
		UInputModifierDeadZone* Zone = NewObject<UInputModifierDeadZone>(Context);
		Zone->LowerThreshold = 0.18f;
		return Zone;
	};
	// Throttle: W / S, the left stick up / down.
	Context->MapKey(ThrottleAction, EKeys::W);
	Context->MapKey(ThrottleAction, EKeys::S).Modifiers.Add(Negate());
	Context->MapKey(ThrottleAction, EKeys::Gamepad_LeftY).Modifiers.Add(DeadZone());
	// Roll: A / D, the left stick across.
	Context->MapKey(RollAction, EKeys::D);
	Context->MapKey(RollAction, EKeys::A).Modifiers.Add(Negate());
	Context->MapKey(RollAction, EKeys::Gamepad_LeftX).Modifiers.Add(DeadZone());
	// Rudder (fine yaw): Q / E, LB / RB.
	Context->MapKey(RudderAction, EKeys::E);
	Context->MapKey(RudderAction, EKeys::Q).Modifiers.Add(Negate());
	Context->MapKey(RudderAction, EKeys::Gamepad_RightShoulder);
	Context->MapKey(RudderAction, EKeys::Gamepad_LeftShoulder).Modifiers.Add(Negate());
	// Pitch and yaw: the mouse (a self-centring virtual stick) and the right stick.
	Context->MapKey(LookMouseAction, EKeys::Mouse2D);
	Context->MapKey(LookStickAction, EKeys::Gamepad_Right2D).Modifiers.Add(DeadZone());
	Context->MapKey(FireAction, EKeys::LeftMouseButton);
	Context->MapKey(FireAction, EKeys::Gamepad_RightTrigger);
	Context->MapKey(FireAction, EKeys::Gamepad_FaceButton_Left);
	Context->MapKey(MissileAction, EKeys::RightMouseButton);
	Context->MapKey(MissileAction, EKeys::Gamepad_FaceButton_Top);
	Context->MapKey(BoostAction, EKeys::LeftShift);
	Context->MapKey(BoostAction, EKeys::SpaceBar);
	Context->MapKey(BoostAction, EKeys::Gamepad_FaceButton_Bottom);
	Context->MapKey(BrakeAction, EKeys::LeftControl);
	Context->MapKey(BrakeAction, EKeys::Gamepad_LeftTrigger);
	Context->MapKey(BrakeAction, EKeys::Gamepad_FaceButton_Right);
	Context->MapKey(InteractAction, EKeys::F);
	Context->MapKey(InteractAction, EKeys::Gamepad_DPad_Up);
	Context->MapKey(ViewAction, EKeys::V);
	Context->MapKey(ViewAction, EKeys::Gamepad_RightThumbstick);
}

void ASoulsFighter::NotifyControllerChanged()
{
	Super::NotifyControllerChanged();
	EnsureFighterInput();
	if (const APlayerController* Old = Cast<APlayerController>(PreviousController))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(Old->GetLocalPlayer()))
		{
			Subsystem->RemoveMappingContext(FighterContext);
		}
	}
	if (const APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
		{
			Subsystem->AddMappingContext(FighterContext, 100);
		}
	}
	ThrottleInput = RollInput = RudderInput = 0.f;
	StickPitch = StickYaw = 0.f;
	MouseStick = PadStick = FVector2D::ZeroVector;
	bFireHeld = bBoostHeld = bBrakeHeld = false;
}

void ASoulsFighter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	EnsureFighterInput();
	if (UEnhancedInputComponent* Bindings = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		Bindings->BindAction(ThrottleAction, ETriggerEvent::Triggered, this, &ASoulsFighter::OnThrottle);
		Bindings->BindAction(ThrottleAction, ETriggerEvent::Completed, this, &ASoulsFighter::OnThrottleStop);
		Bindings->BindAction(RollAction, ETriggerEvent::Triggered, this, &ASoulsFighter::OnRoll);
		Bindings->BindAction(RollAction, ETriggerEvent::Completed, this, &ASoulsFighter::OnRollStop);
		Bindings->BindAction(RudderAction, ETriggerEvent::Triggered, this, &ASoulsFighter::OnRudder);
		Bindings->BindAction(RudderAction, ETriggerEvent::Completed, this, &ASoulsFighter::OnRudderStop);
		Bindings->BindAction(LookMouseAction, ETriggerEvent::Triggered, this, &ASoulsFighter::OnLookMouse);
		Bindings->BindAction(LookStickAction, ETriggerEvent::Triggered, this, &ASoulsFighter::OnLookStick);
		Bindings->BindAction(LookStickAction, ETriggerEvent::Completed, this, &ASoulsFighter::OnLookStickStop);
		Bindings->BindAction(FireAction, ETriggerEvent::Started, this, &ASoulsFighter::OnFireStart);
		Bindings->BindAction(FireAction, ETriggerEvent::Completed, this, &ASoulsFighter::OnFireStop);
		Bindings->BindAction(MissileAction, ETriggerEvent::Started, this, &ASoulsFighter::OnMissile);
		Bindings->BindAction(BoostAction, ETriggerEvent::Started, this, &ASoulsFighter::OnBoostStart);
		Bindings->BindAction(BoostAction, ETriggerEvent::Completed, this, &ASoulsFighter::OnBoostStop);
		Bindings->BindAction(BrakeAction, ETriggerEvent::Started, this, &ASoulsFighter::OnBrakeStart);
		Bindings->BindAction(BrakeAction, ETriggerEvent::Completed, this, &ASoulsFighter::OnBrakeStop);
		Bindings->BindAction(InteractAction, ETriggerEvent::Started, this, &ASoulsFighter::OnInteract);
		Bindings->BindAction(ViewAction, ETriggerEvent::Started, this, &ASoulsFighter::OnView);
	}
}

void ASoulsFighter::OnThrottle(const FInputActionValue& Value) { ThrottleInput = FMath::Clamp(Value.Get<float>(), -1.f, 1.f); }
void ASoulsFighter::OnThrottleStop(const FInputActionValue& Value) { ThrottleInput = 0.f; }
void ASoulsFighter::OnRoll(const FInputActionValue& Value) { RollInput = FMath::Clamp(Value.Get<float>(), -1.f, 1.f); }
void ASoulsFighter::OnRollStop(const FInputActionValue& Value) { RollInput = 0.f; }
void ASoulsFighter::OnRudder(const FInputActionValue& Value) { RudderInput = FMath::Clamp(Value.Get<float>(), -1.f, 1.f); }
void ASoulsFighter::OnRudderStop(const FInputActionValue& Value) { RudderInput = 0.f; }

void ASoulsFighter::OnLookMouse(const FInputActionValue& Value)
{
	// The mouse pushes a virtual stick that drifts back to centre on its own.
	const FVector2D Axis = Value.Get<FVector2D>();
	MouseStick += FVector2D(Axis.X, Axis.Y) * 0.014f;
	if (MouseStick.SizeSquared() > 1.f)
	{
		MouseStick.Normalize();
	}
}

void ASoulsFighter::OnLookStick(const FInputActionValue& Value) { PadStick = Value.Get<FVector2D>(); }
void ASoulsFighter::OnLookStickStop(const FInputActionValue& Value) { PadStick = FVector2D::ZeroVector; }
void ASoulsFighter::OnFireStart(const FInputActionValue& Value) { SetFiring(true); }
void ASoulsFighter::OnFireStop(const FInputActionValue& Value) { SetFiring(false); }
void ASoulsFighter::OnMissile(const FInputActionValue& Value) { FireMissile(); }
void ASoulsFighter::OnBoostStart(const FInputActionValue& Value) { SetBoost(true); }
void ASoulsFighter::OnBoostStop(const FInputActionValue& Value) { SetBoost(false); }
void ASoulsFighter::OnBrakeStart(const FInputActionValue& Value) { SetBrake(true); }
void ASoulsFighter::OnBrakeStop(const FInputActionValue& Value) { SetBrake(false); }
void ASoulsFighter::OnInteract(const FInputActionValue& Value) { InteractInFlight(); }
void ASoulsFighter::OnView(const FInputActionValue& Value) { ToggleView(); }

void ASoulsFighter::ToggleView()
{
	if (State != EFighterState::Piloted)
	{
		return;
	}
	bCockpitView = !bCockpitView;
	ChaseCamera->SetActive(!bCockpitView);
	CockpitCamera->SetActive(bCockpitView);
}

void ASoulsFighter::HoldCourse(float Seconds)
{
	DroneHoldUntil = GetWorld() ? GetWorld()->GetTimeSeconds() + Seconds : 0.f;
}

// ---------------------------------------------------------------------------------------------
// Boarding, landing, take-off, leaving
// ---------------------------------------------------------------------------------------------

ASoulsFighter* ASoulsFighter::FindBoardable(const ASoulsKnight* Knight)
{
	if (!Knight || !Knight->GetWorld())
	{
		return nullptr;
	}
	ASoulsFighter* Best = nullptr;
	float BestDistance = TNumericLimits<float>::Max();
	for (TActorIterator<ASoulsFighter> It(Knight->GetWorld()); It; ++It)
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

bool ASoulsFighter::TryBoard(ASoulsKnight* Knight)
{
	ASoulsFighter* Ship = FindBoardable(Knight);
	return Ship && Ship->Board(Knight);
}

bool ASoulsFighter::CanBoard(const ASoulsKnight* Knight) const
{
	if (bDrone || !Knight || State != EFighterState::Parked || Knight->IsDead() || Knight->IsBusy() || !Cast<APlayerController>(Knight->GetController()))
	{
		return false;
	}
	const FVector To = Knight->GetActorLocation() - GetActorLocation();
	return To.Size2D() < FighterRig::BoardReach && FMath::Abs(To.Z) < 650.f;
}

bool ASoulsFighter::Board(ASoulsKnight* Knight)
{
	if (!CanBoard(Knight))
	{
		return false;
	}
	APlayerController* PC = Cast<APlayerController>(Knight->GetController());
	Pilot = Knight;
	State = EFighterState::Boarding;
	Flight = EFighterFlight::Grounded;
	SequenceT = 0.f;
	ClimbFrom = Knight->GetActorLocation();
	ClimbTo = SeatLocation();

	Knight->GetCharacterMovement()->StopMovementImmediately();
	Knight->GetCharacterMovement()->DisableMovement();
	Knight->SetActorEnableCollision(false);
	Knight->SetActorRotation(FRotator(0.f, (GetActorLocation() - Knight->GetActorLocation()).Rotation().Yaw, 0.f));

	// The template controller rebinds "respawn on pawn destroyed" to whatever it possesses; that stays with the knight.
	Knight->OnDestroyed.RemoveAll(PC);
	PC->Possess(this);
	OnDestroyed.RemoveAll(PC);
	PC->SetControlRotation(GetActorRotation());
	bCockpitView = false;
	ChaseCamera->SetActive(true);
	CockpitCamera->SetActive(false);
	bCameraPlaced = false;
	PlayFighterSound(EFighterSound::Canopy, SeatLocation(), 0.8f);
	UE_LOG(LogUnrealSoulslike, Log, TEXT("Fighter: %s climbs into %s"), *Knight->GetName(), *GetName());
	return true;
}

void ASoulsFighter::InteractInFlight()
{
	if (State != EFighterState::Piloted)
	{
		return;
	}
	if (Flight == EFighterFlight::Grounded)
	{
		Disembark();
	}
	else if (Flight == EFighterFlight::Flying)
	{
		BeginLanding();
	}
}

void ASoulsFighter::BeginLanding()
{
	const float Now = GetWorld()->GetTimeSeconds();
	if (State != EFighterState::Piloted || Flight != EFighterFlight::Flying)
	{
		return;
	}
	if (FlightSpeed > LandingWindow.X)
	{
		FlashMessage = TEXT("TOO FAST TO LAND - BRAKE");
		FlashMessageUntil = Now + 2.f;
		return;
	}
	if (GroundClearance > LandingWindow.Y)
	{
		FlashMessage = TEXT("TOO HIGH TO LAND - DESCEND");
		FlashMessageUntil = Now + 2.f;
		return;
	}
	FVector P, N;
	if (!GroundBelow(GetActorLocation(), P, N) || P.Z < FighterRig::SeaZ + 20.f || N.Z < 0.86f)
	{
		FlashMessage = P.Z < FighterRig::SeaZ + 20.f ? TEXT("NO LANDING ON WATER") : TEXT("GROUND TOO STEEP TO LAND");
		FlashMessageUntil = Now + 2.f;
		return;
	}
	Flight = EFighterFlight::Landing;
	PhaseT = 0.f;
	ThrottleSetting = 0.f;
	GearTarget = 1.f;
	bFireHeld = false;
	PlayFighterSound(EFighterSound::Canopy, GetActorLocation(), 0.5f, 0.7f);
	UE_LOG(LogUnrealSoulslike, Log, TEXT("Fighter: landing from %.0f cm at %.0f cm/s"), GroundClearance, FlightSpeed);
}

void ASoulsFighter::BeginTakeOff()
{
	if (State != EFighterState::Piloted || Flight != EFighterFlight::Grounded)
	{
		return;
	}
	Flight = EFighterFlight::Lifting;
	PhaseT = 0.f;
	LiftFrom = GetActorLocation();
	FlightRotation = FRotator(0.f, GetActorRotation().Yaw, 0.f).Quaternion();
	ThrottleSetting = FMath::Max(ThrottleSetting, 0.35f);
	FlightSpeed = 0.f;
	FlightVelocity = FVector::ZeroVector;
	AngularRate = FVector::ZeroVector;
	PlayFighterSound(EFighterSound::Boost, GetActorLocation(), 0.8f, 0.8f);
	UE_LOG(LogUnrealSoulslike, Log, TEXT("Fighter: lift-off"));
}

void ASoulsFighter::Disembark()
{
	if (State != EFighterState::Piloted || Flight != EFighterFlight::Grounded)
	{
		return;
	}
	bFireHeld = false;
	State = EFighterState::Exiting;
	SequenceT = 0.f;
	PlayFighterSound(EFighterSound::Canopy, SeatLocation(), 0.8f, 0.95f);
}

FVector ASoulsFighter::SeatLocation() const
{
	return Frame->GetComponentTransform().TransformPosition(FighterRig::Seat);
}

float ASoulsFighter::FloorOffset() const
{
	return bDrone ? FighterRig::DroneFloor : FighterRig::GearFloor;
}

void ASoulsFighter::TickSequence(float DeltaSeconds)
{
	ASoulsKnight* Knight = Pilot.Get();
	auto Arc = [](const FVector& A, const FVector& Mid, const FVector& B, float T)
	{
		const float U = 1.f - T;
		return A * (U * U) + Mid * (2.f * U * T) + B * (T * T);
	};
	const FVector Right = GetActorRightVector();
	SequenceT += DeltaSeconds;
	const float T = SequenceT;

	if (State == EFighterState::Boarding)
	{
		CanopyAlpha = FMath::SmoothStep(0.f, 0.6f, T) * (1.f - FMath::SmoothStep(1.75f, 2.25f, T));
		if (Knight)
		{
			ClimbTo = SeatLocation() + FVector(0.f, 0.f, 60.f);
			if (T < 1.55f)
			{
				const float U = FMath::InterpEaseInOut(0.f, 1.f, FMath::Clamp((T - 0.3f) / 1.2f, 0.f, 1.f), 2.f);
				const FVector Side = GetActorLocation() - Right * 320.f + FVector(0.f, 0.f, 330.f);
				Knight->SetActorLocation(Arc(ClimbFrom, Side, ClimbTo, U), false, nullptr, ETeleportType::TeleportPhysics);
				Knight->SetActorRotation(FRotator(0.f, GetActorRotation().Yaw, 0.f));
			}
			else if (!Knight->IsHidden())
			{
				Knight->SetActorHiddenInGame(true);
				Knight->AttachToComponent(Frame, FAttachmentTransformRules::KeepWorldTransform);
				Knight->SetActorRelativeLocation(FighterRig::Seat + FVector(0.f, 0.f, 60.f));
				Knight->SetActorRelativeRotation(FRotator::ZeroRotator);
			}
		}
		if (T >= 2.4f)
		{
			State = EFighterState::Piloted;
			Flight = EFighterFlight::Grounded;
			Missiles = MaxMissiles;
			BoostEnergy = 1.f;
			ThrottleSetting = 0.f;
			CanopyAlpha = 0.f;
			PlayFighterSound(EFighterSound::Boost, GetActorLocation(), 0.5f, 0.6f);
			UE_LOG(LogUnrealSoulslike, Log, TEXT("Fighter: engines lit"));
		}
		return;
	}

	if (State == EFighterState::Exiting)
	{
		CanopyAlpha = FMath::SmoothStep(0.f, 0.5f, T);
		if (Knight && T >= 0.5f)
		{
			if (Knight->IsHidden())
			{
				Knight->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
				Knight->SetActorHiddenInGame(false);
				ClimbFrom = SeatLocation() + FVector(0.f, 0.f, 70.f);
				// Down on the left, clear of the wing.
				const FVector Spot = GetActorLocation() - Right * 520.f + GetActorForwardVector() * 120.f;
				FVector P, N;
				const bool bGround = GroundBelow(Spot + FVector(0.f, 0.f, 600.f), P, N, 4000.f);
				ClimbTo = (bGround ? P : Spot + FVector(0.f, 0.f, FighterRig::GearFloor)) + FVector(0.f, 0.f, Knight->GetSimpleCollisionHalfHeight() + 8.f);
			}
			const float U = FMath::InterpEaseInOut(0.f, 1.f, FMath::Clamp((T - 0.5f) / 1.1f, 0.f, 1.f), 2.f);
			const FVector Side = GetActorLocation() - Right * 380.f + FVector(0.f, 0.f, 280.f);
			Knight->SetActorLocation(Arc(ClimbFrom, Side, ClimbTo, U), false, nullptr, ETeleportType::TeleportPhysics);
			Knight->SetActorRotation(FRotator(0.f, GetActorRotation().Yaw, 0.f));
		}
		if (T >= 1.8f)
		{
			APlayerController* PC = Cast<APlayerController>(GetController());
			const float Yaw = GetActorRotation().Yaw;
			State = EFighterState::Parked;
			Flight = EFighterFlight::Grounded;
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
						KnightInput->RemoveMappingContext(FighterContext);
					}
					PC->Possess(Knight);
					PC->SetControlRotation(FRotator(-10.f, Yaw + 60.f, 0.f));
				}
				UE_LOG(LogUnrealSoulslike, Log, TEXT("Fighter: %s climbs out of %s"), *Knight->GetName(), *GetName());
			}
			else if (PC)
			{
				PC->UnPossess();
			}
			Pilot = nullptr;
			ThrottleInput = RollInput = RudderInput = 0.f;
			StickPitch = StickYaw = 0.f;
			bBoostHeld = bBrakeHeld = bFireHeld = false;
		}
	}
}

// ---------------------------------------------------------------------------------------------
// Tick
// ---------------------------------------------------------------------------------------------

void ASoulsFighter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const float Dt = FMath::Min(DeltaSeconds, 0.05f);
	const float Now = GetWorld()->GetTimeSeconds();
	MouseStick *= FMath::Exp(-1.6f * Dt);

	switch (State)
	{
	case EFighterState::Boarding:
	case EFighterState::Exiting:
		TickSequence(Dt);
		break;
	case EFighterState::Parked:
		CanopyAlpha = FMath::FInterpTo(CanopyAlpha, 0.f, Dt, 3.f);
		GearTarget = 1.f;
		ThrottleSetting = 0.f;
		break;
	case EFighterState::Wrecked:
		if (FMath::FRand() < Dt * 7.f)
		{
			SpawnPuff(GetActorLocation() + FMath::VRand() * 120.f + FVector(0.f, 0.f, 80.f), FVector(FMath::FRandRange(-30.f, 30.f), FMath::FRandRange(-30.f, 30.f), 240.f),
				1.f, 2.6f, 3.2f, 0.7f, false);
		}
		if (FMath::FRand() < Dt * 1.2f)
		{
			ASoulsSparks::Burst(GetWorld(), GetActorLocation() + FMath::VRand() * 150.f, FMath::VRand(), 8, 1.1f);
		}
		if (Now - WreckedAt > RepairSeconds)
		{
			Repair();
		}
		break;
	case EFighterState::Drone:
		if (bDroneDown)
		{
			if (Now - DroneDownAt > DroneRespawnSeconds)
			{
				DroneRespawn();
			}
		}
		else
		{
			TickDroneBrain(Dt);
		}
		break;
	default:
		break;
	}

	if (State == EFighterState::Piloted)
	{
		switch (Flight)
		{
		case EFighterFlight::Grounded: TickGrounded(Dt); break;
		case EFighterFlight::Lifting: TickLifting(Dt); break;
		case EFighterFlight::Flying: TickFlying(Dt); break;
		case EFighterFlight::Landing: TickLanding(Dt); break;
		}
		TickLock(Dt);
	}
	if (State == EFighterState::Piloted || (State == EFighterState::Drone && !bDroneDown))
	{
		UpdateClearance();
	}
	Body->ComponentVelocity = (State == EFighterState::Piloted || State == EFighterState::Drone) && !bDroneDown ? FlightVelocity : FVector::ZeroVector;

	TickWeapons(Dt);
	TickBolts(Dt);
	TickPuffs(Dt);
	TickParts(Dt);
	TickEngineSound(Dt);
	if (IsPlayerControlled())
	{
		TickCamera(Dt);
	}
}

void ASoulsFighter::TickGrounded(float DeltaSeconds)
{
	GearTarget = 1.f;
	FlightSpeed = 0.f;
	FlightVelocity = FVector::ZeroVector;
	// Pivot on the pads with the stick, settle onto the ground, and lift off on throttle or boost.
	const float Pivot = FMath::Clamp(StickYaw + PadStick.X + MouseStick.X + RudderInput, -1.f, 1.f);
	if (FMath::Abs(Pivot) > 0.05f)
	{
		SetActorRotation(FRotator(GetActorRotation().Pitch, GetActorRotation().Yaw + Pivot * 35.f * DeltaSeconds, GetActorRotation().Roll));
	}
	SettleOnGround(DeltaSeconds, false);
	if (ThrottleInput > 0.3f || bBoostHeld)
	{
		BeginTakeOff();
	}
}

void ASoulsFighter::TickLifting(float DeltaSeconds)
{
	PhaseT += DeltaSeconds;
	const float U = FMath::SmoothStep(0.f, FighterRig::LiftSeconds, PhaseT);
	const float Pivot = FMath::Clamp(StickYaw + PadStick.X + MouseStick.X + RudderInput, -1.f, 1.f);
	FlightRotation = (FlightRotation * FQuat(FVector::ZAxisVector, FMath::DegreesToRadians(Pivot * 45.f * DeltaSeconds))).GetNormalized();
	// Level out from the ground's tilt while rising.
	const FQuat Level = FRotator(0.f, FlightRotation.Rotator().Yaw, 0.f).Quaternion();
	FlightRotation = FQuat::Slerp(FlightRotation, Level, 1.f - FMath::Exp(-3.f * DeltaSeconds));
	FVector Target = LiftFrom + FVector(0.f, 0.f, FighterRig::LiftHeight * U);
	FHitResult Hit;
	SetActorLocationAndRotation(Target, FlightRotation, true, &Hit, ETeleportType::None);
	FlightVelocity = FVector(0.f, 0.f, FighterRig::LiftHeight / FighterRig::LiftSeconds);
	if (PhaseT > 1.1f)
	{
		GearTarget = 0.f;
	}
	if (PhaseT < 1.6f && FMath::FRand() < DeltaSeconds * 26.f)
	{
		// Thrust wash kicking up dust.
		FVector P, N;
		if (GroundBelow(GetActorLocation(), P, N, 3000.f))
		{
			const FVector Out = FVector(FMath::FRandRange(-1.f, 1.f), FMath::FRandRange(-1.f, 1.f), 0.f).GetSafeNormal();
			SpawnPuff(P + Out * FMath::FRandRange(100.f, 400.f) + FVector(0.f, 0.f, 40.f), Out * FMath::FRandRange(500.f, 900.f) + FVector(0.f, 0.f, 60.f),
				0.9f, 2.2f, 1.8f, 0.4f, false);
		}
	}
	if (PhaseT >= FighterRig::LiftSeconds)
	{
		Flight = EFighterFlight::Flying;
		FlightSpeed = 500.f;
		FlightVelocity = FlightRotation.GetForwardVector() * FlightSpeed;
		UE_LOG(LogUnrealSoulslike, Log, TEXT("Fighter: airborne, throttle %.0f%%"), ThrottleSetting * 100.f);
	}
}

void ASoulsFighter::TickFlying(float DeltaSeconds)
{
	const float Now = GetWorld()->GetTimeSeconds();
	// Throttle, boost and brake.
	ThrottleSetting = FMath::Clamp(ThrottleSetting + ThrottleInput * 0.75f * DeltaSeconds, 0.f, 1.f);
	const bool bWasBoosting = bBoosting;
	bBoosting = bBoostHeld && BoostEnergy > (bWasBoosting ? 0.f : 0.12f);
	if (bBoosting)
	{
		BoostEnergy = FMath::Max(BoostEnergy - 0.32f * DeltaSeconds, 0.f);
		if (!bWasBoosting && Now - LastBoostSound > 0.6f)
		{
			PlayFighterSound(EFighterSound::Boost, GetActorLocation(), 0.9f);
			LastBoostSound = Now;
		}
	}
	else
	{
		BoostEnergy = FMath::Min(BoostEnergy + 0.16f * DeltaSeconds, 1.f);
	}
	float TargetSpeed = ThrottleSetting * CruiseSpeed;
	float Rate = Acceleration;
	if (bBoosting)
	{
		TargetSpeed = BoostSpeed;
		Rate = Acceleration * 2.6f;
	}
	else if (bBrakeHeld)
	{
		TargetSpeed = FMath::Min(TargetSpeed, BrakeSpeed);
		Rate = Deceleration * 1.6f;
	}
	else if (TargetSpeed < FlightSpeed)
	{
		Rate = Deceleration;
	}
	FlightSpeed = FMath::FInterpConstantTo(FlightSpeed, TargetSpeed, DeltaSeconds, Rate);

	// Stick: the pad, the mouse's virtual stick and any scripted input, summed.
	float PitchCmd = FMath::Clamp(PadStick.Y + MouseStick.Y + StickPitch, -1.f, 1.f) * (bInvertPitch ? -1.f : 1.f);
	const float YawCmd = FMath::Clamp(PadStick.X + MouseStick.X + StickYaw + RudderInput * 0.8f, -1.f, 1.f);
	const bool bLevel = !bFireHeld && Now - LastShotTime > AutoLevelDelay;
	if (PullUpWarning > 0.5f && GroundClearance < 600.f)
	{
		PitchCmd = FMath::Max(PitchCmd, 0.35f); // a nudge of terrain avoidance right at the deck
	}
	Integrate(DeltaSeconds, PitchCmd, YawCmd, RollInput, bLevel);
	GearTarget = FlightSpeed < LandingWindow.X && GroundClearance < LandingWindow.Y * 0.6f ? 1.f : 0.f;
	MoveWithCollision(DeltaSeconds);

	// Ground proximity: where will we be in two and a half seconds?
	FHitResult Ahead;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(FighterAhead), false, this);
	const FVector From = GetActorLocation();
	const bool bAhead = GetWorld()->LineTraceSingleByObjectType(Ahead, From, From + FlightVelocity * 2.5f + FVector(0.f, 0.f, FighterRig::GearFloor),
		FighterRig::Terrain(), Params);
	PullUpWarning = FMath::FInterpTo(PullUpWarning, bAhead && FlightSpeed > 1500.f ? 1.f : 0.f, DeltaSeconds, 8.f);
}

void ASoulsFighter::TickLanding(float DeltaSeconds)
{
	PhaseT += DeltaSeconds;
	GearTarget = 1.f;
	FlightSpeed = FMath::FInterpTo(FlightSpeed, 0.f, DeltaSeconds, 1.4f);
	// Level, keep the heading (the stick still turns it).
	const float Pivot = FMath::Clamp(StickYaw + PadStick.X + MouseStick.X + RudderInput, -1.f, 1.f);
	FRotator Heading = FlightRotation.Rotator();
	const FQuat Level = FRotator(0.f, Heading.Yaw + Pivot * 40.f * DeltaSeconds, 0.f).Quaternion();
	FlightRotation = FQuat::Slerp(FlightRotation, Level, 1.f - FMath::Exp(-3.f * DeltaSeconds));
	AngularRate = FVector::ZeroVector;

	FVector P, N;
	const bool bGround = GroundBelow(GetActorLocation(), P, N);
	const float GroundZ = bGround ? FMath::Max(P.Z, FighterRig::SeaZ) : FighterRig::SeaZ;
	const float Clearance = GetActorLocation().Z + FighterRig::GearFloor - GroundZ;
	const float SinkRate = FMath::Clamp(Clearance * 0.9f, 140.f, 900.f);
	FVector Horizontal = FlightVelocity;
	Horizontal.Z = 0.f;
	Horizontal = FMath::VInterpTo(Horizontal, FlightRotation.GetForwardVector().GetSafeNormal2D() * FlightSpeed, DeltaSeconds, 2.f);
	FlightVelocity = Horizontal + FVector(0.f, 0.f, -SinkRate);
	FHitResult Hit;
	SetActorLocationAndRotation(GetActorLocation() + FlightVelocity * DeltaSeconds, FlightRotation, true, &Hit, ETeleportType::None);
	if (Hit.bStartPenetrating)
	{
		AddActorWorldOffset(Hit.Normal * (Hit.PenetrationDepth + 5.f), false, nullptr, ETeleportType::TeleportPhysics);
	}
	if (PhaseT > 0.4f && FMath::FRand() < DeltaSeconds * 20.f && Clearance < 1500.f && bGround)
	{
		const FVector Out = FVector(FMath::FRandRange(-1.f, 1.f), FMath::FRandRange(-1.f, 1.f), 0.f).GetSafeNormal();
		SpawnPuff(P + Out * FMath::FRandRange(100.f, 400.f) + FVector(0.f, 0.f, 40.f), Out * FMath::FRandRange(400.f, 800.f), 0.9f, 2.2f, 1.6f, 0.35f, false);
	}
	// Abort: throttle up or boost takes it back into the air.
	if (ThrottleInput > 0.5f || bBoostHeld)
	{
		Flight = EFighterFlight::Flying;
		ThrottleSetting = 0.4f;
		return;
	}
	if ((Clearance < 12.f || Hit.bBlockingHit) && Horizontal.Size() < 250.f && PhaseT > 0.5f)
	{
		Flight = EFighterFlight::Grounded;
		FlightSpeed = 0.f;
		FlightVelocity = FVector::ZeroVector;
		GearAlpha = 1.f;
		SettleOnGround(0.f, true);
		Missiles = MaxMissiles;
		BoostEnergy = 1.f;
		PlayFighterSound(EFighterSound::Scrape, GetActorLocation() + FVector(0.f, 0.f, FighterRig::GearFloor), 0.35f, 0.6f);
		Shake(0.25f);
		UE_LOG(LogUnrealSoulslike, Log, TEXT("Fighter: touchdown at %s"), *GetActorLocation().ToCompactString());
	}
}

float ASoulsFighter::GetRollAngle() const
{
	const FQuat Q = GetActorQuat();
	const FVector Fwd = Q.GetForwardVector();
	const FVector Right = Q.GetRightVector();
	FVector LevelRight = FVector::CrossProduct(FVector::UpVector, Fwd);
	if (LevelRight.SizeSquared() < 1e-4f)
	{
		return 0.f; // straight up or down: no horizon to roll against
	}
	LevelRight.Normalize();
	return FMath::RadiansToDegrees(FMath::Atan2(FVector::DotProduct(Fwd, FVector::CrossProduct(LevelRight, Right)), FVector::DotProduct(LevelRight, Right)));
}

void ASoulsFighter::Integrate(float DeltaSeconds, float PitchCmd, float YawCmd, float RollCmd, bool bAllowLevel)
{
	// Control authority falls off when nearly stopped (it still turns, just lazily) and a touch at full boost.
	const float SpeedFrac = FMath::Clamp(FlightSpeed / FMath::Max(CruiseSpeed, 1.f), 0.f, 1.4f);
	const float Authority = FMath::Lerp(0.55f, 1.f, FMath::Min(SpeedFrac * 2.f, 1.f)) * (bBoosting ? 0.85f : 1.f);
	FVector Target(PitchCmd * TurnRates.X * Authority, YawCmd * TurnRates.Y * Authority, RollCmd * TurnRates.Z);
	AngularRate.X = FMath::FInterpTo(AngularRate.X, Target.X, DeltaSeconds, Agility);
	AngularRate.Y = FMath::FInterpTo(AngularRate.Y, Target.Y, DeltaSeconds, Agility);

	// The roll: the stick when it's pushed; otherwise (and not firing) back to level with the horizon, banked into the turn.
	const FVector Fwd = FlightRotation.GetForwardVector();
	if (FMath::Abs(RollCmd) > 0.1f || !bAllowLevel)
	{
		AngularRate.Z = FMath::FInterpTo(AngularRate.Z, Target.Z, DeltaSeconds, Agility * 1.4f);
	}
	else
	{
		const float Horizon = FMath::Clamp((0.97f - FMath::Abs(Fwd.Z)) / 0.25f, 0.f, 1.f);
		const float Current = GetRollAngle();
		const float Desired = -YawCmd * BankAngle;
		const float Error = FMath::FindDeltaAngleDegrees(Current, Desired);
		const float LevelRate = FMath::Clamp(-Error * 3.2f, -AutoLevelRate, AutoLevelRate) * Horizon;
		AngularRate.Z = FMath::FInterpTo(AngularRate.Z, LevelRate, DeltaSeconds, 5.f);
	}

	// Pitch up = about -Y, yaw right = about +Z, roll right = about -X, all in the ship's own frame.
	const FQuat Delta = FQuat(FVector::YAxisVector, FMath::DegreesToRadians(-AngularRate.X * DeltaSeconds))
		* FQuat(FVector::ZAxisVector, FMath::DegreesToRadians(AngularRate.Y * DeltaSeconds))
		* FQuat(FVector::XAxisVector, FMath::DegreesToRadians(-AngularRate.Z * DeltaSeconds));
	FlightRotation = (FlightRotation * Delta).GetNormalized();

	// The velocity follows the nose with a little slip (more on boost), so turns carve and drift.
	const FVector Want = FlightRotation.GetForwardVector() * FlightSpeed;
	FlightVelocity = FMath::VInterpTo(FlightVelocity, Want, DeltaSeconds, Grip * (bBoosting ? 0.6f : 1.f));
	// A hint of the wings' lift: bleeding off speed in a hard pull.
	FlightSpeed = FMath::Max(FlightSpeed - FMath::Abs(AngularRate.X) * 0.9f * DeltaSeconds * SpeedFrac, 0.f);

	VisualBank = FMath::FInterpTo(VisualBank, -AngularRate.Y / FMath::Max(TurnRates.Y, 1.f) * 14.f, DeltaSeconds, 4.f);
}

void ASoulsFighter::MoveWithCollision(float DeltaSeconds)
{
	const FTransform OldXf = GetActorTransform();
	const FVector NewLoc = OldXf.GetLocation() + FlightVelocity * DeltaSeconds;
	const FTransform NewXf(FlightRotation, NewLoc);

	// The extremities first: gun tips, wing tips, tail, belly.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(FighterProbe), false, this);
	if (const ASoulsKnight* Knight = Pilot.Get())
	{
		Params.AddIgnoredActor(Knight);
	}
	const FVector* Probes = bDrone ? FighterRig::DroneProbes : FighterRig::Probes;
	const int32 ProbeCount = bDrone ? 3 : 7;
	bool bTouched = false;
	for (int32 Index = 0; Index < ProbeCount && !bTouched; ++Index)
	{
		const FVector A = OldXf.TransformPosition(Probes[Index]);
		const FVector B = NewXf.TransformPosition(Probes[Index]);
		FHitResult Hit;
		if (GetWorld()->LineTraceSingleByObjectType(Hit, A, B, FighterRig::Terrain(), Params))
		{
			SetActorRotation(FlightRotation);
			Contact(Hit.ImpactPoint, Hit.ImpactNormal, Hit.GetActor(), false);
			bTouched = true;
		}
		else if (B.Z < FighterRig::SeaZ)
		{
			SetActorRotation(FlightRotation);
			Contact(B, FVector::UpVector, nullptr, true);
			bTouched = true;
		}
	}
	if (bTouched)
	{
		return;
	}
	FHitResult Hit;
	SetActorLocationAndRotation(NewLoc, FlightRotation, true, &Hit, ETeleportType::None);
	if (Hit.bStartPenetrating)
	{
		AddActorWorldOffset(Hit.Normal * (Hit.PenetrationDepth + 5.f), false, nullptr, ETeleportType::TeleportPhysics);
	}
	else if (Hit.bBlockingHit)
	{
		Contact(Hit.ImpactPoint, Hit.ImpactNormal, Hit.GetActor(), false);
	}
}

void ASoulsFighter::Contact(const FVector& Point, const FVector& Normal, AActor* Other, bool bWater)
{
	const float Now = GetWorld()->GetTimeSeconds();
	const FVector N = Normal.GetSafeNormal();
	// Out of the surface first, whatever happens.
	AddActorWorldOffset(N * 35.f, false, nullptr, ETeleportType::TeleportPhysics);
	const float Into = -FVector::DotProduct(FlightVelocity, N);
	if (Into <= 0.f)
	{
		return;
	}
	// Bounce: lose most of the speed into the surface, scrub some along it, turn the nose away.
	FlightVelocity += N * Into * 1.35f;
	FlightVelocity *= 0.85f;
	FlightSpeed = FMath::Min(FlightSpeed, FlightVelocity.Size());
	const FVector Fwd = FlightRotation.GetForwardVector();
	if (FVector::DotProduct(Fwd, N) < 0.25f)
	{
		const FVector Away = (Fwd - N * FVector::DotProduct(Fwd, N) * 1.5f + N * 0.25f).GetSafeNormal();
		const FQuat Turn = FQuat::FindBetweenNormals(Fwd, Away);
		FlightRotation = (FQuat::Slerp(FQuat::Identity, Turn, 0.65f) * FlightRotation).GetNormalized();
		SetActorRotation(FlightRotation);
	}
	AngularRate *= 0.3f;
	if (Now - LastContactTime < 0.12f)
	{
		return; // the same scrape
	}
	LastContactTime = Now;
	++Scrapes;
	const float Damage = FMath::Max(Into - 250.f, 0.f) * (bWater ? 0.035f : 0.055f);
	if (bWater)
	{
		for (int32 Index = 0; Index < 6; ++Index)
		{
			SpawnPuff(Point + FMath::VRand() * 120.f, FVector(FMath::FRandRange(-300.f, 300.f), FMath::FRandRange(-300.f, 300.f), FMath::FRandRange(300.f, 700.f)),
				1.f, 1.6f, 1.4f, 0.35f, false);
		}
	}
	else
	{
		ASoulsSparks::Burst(GetWorld(), Point, N, 18, 1.6f, 0.9f);
		SpawnPuff(Point + N * 40.f, N * 200.f, 0.8f, 2.f, 1.4f, 0.45f, false);
	}
	PlayFighterSound(EFighterSound::Scrape, Point, FMath::Clamp(0.4f + Into / 3000.f, 0.4f, 1.f), FMath::FRandRange(0.9f, 1.1f));
	if (IsPlayerControlled())
	{
		Shake(0.3f + FMath::Min(Into / 4000.f, 0.7f));
	}
	if (ASoulsFighter* OtherShip = Cast<ASoulsFighter>(Other))
	{
		OtherShip->ApplyHullDamage(Damage * 0.8f + 10.f, this, Point);
	}
	else if (ASoulsKnight* Struck = Cast<ASoulsKnight>(Other))
	{
		DamageActor(Struck, FMath::Min(Into * 0.03f, 200.f), Point, -N, 700.f);
	}
	UE_LOG(LogUnrealSoulslike, Verbose, TEXT("Fighter %s: contact %.0f cm/s into %s"), *GetName(), Into, Other ? *Other->GetName() : TEXT("the sea"));
	ApplyHullDamage(Damage, Other, Point);
}

bool ASoulsFighter::GroundBelow(const FVector& From, FVector& OutPoint, FVector& OutNormal, float Depth) const
{
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(FighterGround), false, this);
	if (const ASoulsKnight* Knight = Pilot.Get())
	{
		Params.AddIgnoredActor(Knight);
	}
	if (GetWorld()->LineTraceSingleByObjectType(Hit, From, From - FVector(0.f, 0.f, Depth), FighterRig::Terrain(), Params))
	{
		OutPoint = Hit.ImpactPoint;
		OutNormal = Hit.ImpactNormal;
		return true;
	}
	OutPoint = FVector(From.X, From.Y, FighterRig::SeaZ - 5000.f);
	OutNormal = FVector::UpVector;
	return false;
}

void ASoulsFighter::UpdateClearance()
{
	FVector P, N;
	const bool bGround = GroundBelow(GetActorLocation(), P, N);
	const float GroundZ = bGround ? FMath::Max(P.Z, FighterRig::SeaZ) : FighterRig::SeaZ;
	GroundClearance = GetActorLocation().Z + FloorOffset() - GroundZ;
}

void ASoulsFighter::SettleOnGround(float DeltaSeconds, bool bSnap)
{
	const FRotator Current = GetActorRotation();
	const FQuat YawQ = FRotator(0.f, Current.Yaw, 0.f).Quaternion();
	const FVector Loc = GetActorLocation();
	FVector Pads[3];
	for (int32 Leg = 0; Leg < 3; ++Leg)
	{
		const FVector Foot = Loc + YawQ.RotateVector(FVector(FighterRig::Gear[Leg].X, FighterRig::Gear[Leg].Y, 0.f));
		FVector N;
		if (!GroundBelow(Foot + FVector(0.f, 0.f, 900.f), Pads[Leg], N, 9000.f))
		{
			return;
		}
	}
	FVector Normal = FVector::CrossProduct(Pads[1] - Pads[0], Pads[2] - Pads[0]).GetSafeNormal();
	if (Normal.Z < 0.f)
	{
		Normal = -Normal;
	}
	if (Normal.Z < 0.6f)
	{
		Normal = FVector::UpVector;
	}
	FVector Fwd = YawQ.GetForwardVector();
	Fwd = (Fwd - Normal * FVector::DotProduct(Fwd, Normal)).GetSafeNormal();
	const FQuat Want = FRotationMatrix::MakeFromXZ(Fwd, Normal).ToQuat();
	const FVector Centre = (Pads[0] + Pads[1] + Pads[2]) / 3.f;
	const FVector LocalCentre((FighterRig::Gear[0].X + FighterRig::Gear[1].X + FighterRig::Gear[2].X) / 3.f, 0.f, FighterRig::GearFloor);
	const FVector WantLoc = Centre - Want.RotateVector(LocalCentre);
	FQuat NewQ = Want;
	FVector NewLoc = WantLoc;
	if (!bSnap)
	{
		NewQ = FQuat::Slerp(GetActorQuat(), Want, 1.f - FMath::Exp(-6.f * DeltaSeconds));
		NewLoc = FMath::VInterpTo(Loc, WantLoc, DeltaSeconds, 6.f);
	}
	SetActorLocationAndRotation(NewLoc, NewQ, false, nullptr, ETeleportType::TeleportPhysics);
	FlightRotation = NewQ;
}

void ASoulsFighter::PlaceInFlight(const FVector& Where, float Yaw, float Speed)
{
	if (State != EFighterState::Piloted && State != EFighterState::Drone)
	{
		return;
	}
	Flight = EFighterFlight::Flying;
	GearAlpha = GearTarget = bDrone ? 0.f : (Speed < LandingWindow.X ? 1.f : 0.f);
	FlightRotation = FRotator(0.f, Yaw, 0.f).Quaternion();
	FlightSpeed = Speed;
	FlightVelocity = FlightRotation.GetForwardVector() * Speed;
	ThrottleSetting = FMath::Clamp(Speed / FMath::Max(CruiseSpeed, 1.f), 0.f, 1.f);
	AngularRate = FVector::ZeroVector;
	SetActorLocationAndRotation(Where, FlightRotation, false, nullptr, ETeleportType::TeleportPhysics);
	bCameraPlaced = false;
}

void ASoulsFighter::FaceToward(const FVector& Where)
{
	const FVector Dir = (Where - GetActorLocation()).GetSafeNormal();
	if (Dir.IsNearlyZero())
	{
		return;
	}
	FlightRotation = FRotationMatrix::MakeFromX(Dir).ToQuat();
	FlightVelocity = Dir * FlightSpeed;
	AngularRate = FVector::ZeroVector;
	SetActorRotation(FlightRotation);
}

void ASoulsFighter::Shake(float Strength)
{
	ShakeAmount = FMath::Min(FMath::Max(ShakeAmount, Strength), 1.2f);
}

// ---------------------------------------------------------------------------------------------
// Parts, camera, engine sound
// ---------------------------------------------------------------------------------------------

void ASoulsFighter::TickParts(float DeltaSeconds)
{
	if (DeltaSeconds > 0.f)
	{
		GearAlpha = FMath::FInterpConstantTo(GearAlpha, GearTarget, DeltaSeconds, 1.25f);
	}
	Frame->SetRelativeRotation(FQuat(FVector::XAxisVector, FMath::DegreesToRadians(VisualBank)));
	if (!bDrone)
	{
		CanopyMesh->SetRelativeRotation(FRotator(CanopyAlpha * 50.f, 0.f, 0.f));
		for (UStaticMeshComponent* Leg : GearMeshes)
		{
			const float Fold = FMath::SmoothStep(0.f, 1.f, GearAlpha);
			Leg->SetRelativeScale3D(FVector(1.f, 1.f, FMath::Max(Fold, 0.02f)));
			Leg->SetVisibility(GearAlpha > 0.03f);
		}
	}
	const bool bLive = (State == EFighterState::Piloted || State == EFighterState::Drone) && !bDroneDown;
	const float Idle = bLive ? (Flight == EFighterFlight::Grounded ? 0.25f : 0.45f) : 0.f;
	const float Thrust = bLive ? FMath::Clamp(Idle + ThrottleSetting * 0.8f + (bBoosting ? 1.4f : 0.f) + (Flight == EFighterFlight::Lifting ? 0.6f : 0.f), 0.f, 2.6f) : 0.f;
	const int32 Count = bDrone ? 1 : EngineGlows.Num();
	for (int32 Index = 0; Index < EngineGlows.Num(); ++Index)
	{
		UStaticMeshComponent* Glow = EngineGlows[Index];
		const bool bOn = Index < Count && Thrust > 0.01f;
		Glow->SetVisibility(bOn);
		if (bOn)
		{
			const float Size = Index < 2 || bDrone ? 0.42f : 0.3f;
			const float Flicker = DeltaSeconds > 0.f ? FMath::FRandRange(0.9f, 1.1f) : 1.f;
			Glow->SetRelativeScale3D(FVector(Size * (0.6f + Thrust * 1.5f) * Flicker, Size, Size));
			Glow->SetRelativeLocation((bDrone ? FighterRig::DroneEngine : FighterRig::Engine[Index]) - FVector(Size * (0.6f + Thrust * 1.5f) * 40.f, 0.f, 0.f));
		}
	}
	EngineLight->SetIntensity(Thrust * 9000.f);
}

void ASoulsFighter::TickCamera(float DeltaSeconds)
{
	const FVector Loc = GetActorLocation();
	FQuat WantRot = FlightRotation;
	float Distance = 1650.f;
	float Height = 420.f;
	float RotLag = 5.f;
	if (State == EFighterState::Boarding || State == EFighterState::Exiting || State == EFighterState::Parked || Flight == EFighterFlight::Grounded)
	{
		// Out at the front left, where the knight climbs, looking down a little.
		const bool bSequence = State == EFighterState::Boarding || State == EFighterState::Exiting;
		WantRot = FRotator(-14.f, GetActorRotation().Yaw + (bSequence ? 145.f : 0.f), 0.f).Quaternion();
		Distance = bSequence ? 2000.f : 1750.f;
		Height = 300.f;
		RotLag = 3.f;
	}
	else
	{
		const float CruiseFrac = FMath::Clamp(FlightSpeed / FMath::Max(CruiseSpeed, 1.f), 0.f, 1.f);
		Distance = 1650.f + CruiseFrac * 250.f + (bBoosting ? 450.f : 0.f);
	}
	if (!bCameraPlaced)
	{
		CameraRotation = WantRot;
		bCameraPlaced = true;
	}
	CameraRotation = FQuat::Slerp(CameraRotation, WantRot, 1.f - FMath::Exp(-RotLag * DeltaSeconds)).GetNormalized();
	FVector Want = Loc + CameraRotation.RotateVector(FVector(-Distance, 0.f, Height));
	const FVector LookAt = Loc + CameraRotation.RotateVector(FVector(1400.f, 0.f, 120.f));

	// Keep the camera out of the ground and the sea.
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(FighterCamera), false, this);
	if (const ASoulsKnight* Knight = Pilot.Get())
	{
		Params.AddIgnoredActor(Knight);
	}
	if (GetWorld()->SweepSingleByObjectType(Hit, Loc + FVector(0.f, 0.f, 150.f), Want, FQuat::Identity, FighterRig::Terrain(), FCollisionShape::MakeSphere(30.f), Params))
	{
		Want = Hit.Location;
	}
	Want.Z = FMath::Max(Want.Z, FighterRig::SeaZ + 60.f);
	CameraLocation = Want;

	ShakeTime += DeltaSeconds;
	ShakeAmount = FMath::FInterpTo(ShakeAmount, 0.f, DeltaSeconds, 5.f);
	const float S = ShakeAmount + (bBoosting ? 0.05f : 0.f);
	const FVector Jitter(0.f, FMath::PerlinNoise1D(ShakeTime * 23.f) * S * 22.f, FMath::PerlinNoise1D(ShakeTime * 29.f + 7.3f) * S * 26.f);
	const FRotator Jolt(FMath::PerlinNoise1D(ShakeTime * 19.f + 3.1f) * S * 1.6f, FMath::PerlinNoise1D(ShakeTime * 17.f + 11.f) * S * 1.f,
		FMath::PerlinNoise1D(ShakeTime * 13.f + 5.f) * S * 1.4f);
	const FVector LookDir = (LookAt - CameraLocation).GetSafeNormal();
	const FQuat View = FRotationMatrix::MakeFromXZ(LookDir, CameraRotation.GetUpVector()).ToQuat();
	ChaseCamera->SetWorldLocationAndRotation(CameraLocation + View.RotateVector(Jitter), View * Jolt.Quaternion());

	const float SpeedFrac = FMath::Clamp(FlightSpeed / FMath::Max(CruiseSpeed, 1.f), 0.f, 1.f);
	const float WantFov = 78.f + (Flight == EFighterFlight::Flying ? SpeedFrac * 6.f : 0.f) + (bBoosting ? 16.f : 0.f);
	CameraFov = FMath::FInterpTo(CameraFov, WantFov, DeltaSeconds, bBoosting ? 5.f : 2.5f);
	ChaseCamera->SetFieldOfView(CameraFov);
	CockpitCamera->SetFieldOfView(CameraFov + 14.f);
	CockpitCamera->SetRelativeLocationAndRotation(FighterRig::Eye + Jitter * 0.3f, Jolt * 1.2f);
}

void ASoulsFighter::TickEngineSound(float DeltaSeconds)
{
	const bool bLive = (State == EFighterState::Piloted || State == EFighterState::Drone) && !bDroneDown;
	if (!bLive)
	{
		if (EngineAudio->IsPlaying())
		{
			EngineAudio->FadeOut(0.8f, 0.f);
		}
		return;
	}
	if (!EngineAudio->IsPlaying())
	{
		EngineAudio->FadeIn(1.f, 1.f);
	}
	const float SpeedFrac = FMath::Clamp(FlightSpeed / FMath::Max(CruiseSpeed, 1.f), 0.f, 1.6f);
	const float Lift = Flight == EFighterFlight::Lifting || Flight == EFighterFlight::Landing ? 0.25f : 0.f;
	EngineAudio->SetPitchMultiplier(FMath::Clamp(0.6f + SpeedFrac * 0.5f + Lift + (bBoosting ? 0.3f : 0.f), 0.4f, 2.f));
	EngineAudio->SetVolumeMultiplier(FMath::Clamp((bDrone ? 0.55f : 0.35f) + ThrottleSetting * 0.35f + (bBoosting ? 0.3f : 0.f) + Lift, 0.1f, 1.2f));
}

// ---------------------------------------------------------------------------------------------
// Drone brain: circle the island; when a piloted fighter is airborne, chase it, lead it and shoot; break off before a
// collision; never fly into the ground.
// ---------------------------------------------------------------------------------------------

ASoulsFighter* ASoulsFighter::FindPlayerTarget() const
{
	ASoulsFighter* Best = nullptr;
	float BestDistance = 50000.f;
	for (TActorIterator<ASoulsFighter> It(GetWorld()); It; ++It)
	{
		if (It->IsDrone() || !It->IsPiloted() || !It->IsAirborne())
		{
			continue;
		}
		const float Distance = FVector::Dist(It->GetActorLocation(), GetActorLocation());
		if (Distance < BestDistance)
		{
			Best = *It;
			BestDistance = Distance;
		}
	}
	return Best;
}

FVector ASoulsFighter::LeadPoint(const AActor* Target, const FVector& From, float ProjectileSpeed) const
{
	if (!Target)
	{
		return From;
	}
	const FVector TargetLoc = Target->GetActorLocation();
	// Bolts carry the shooter's velocity: lead on the relative velocity.
	const FVector Relative = Target->GetVelocity() - FlightVelocity;
	FVector Point = TargetLoc;
	for (int32 Pass = 0; Pass < 3; ++Pass)
	{
		const float Time = FVector::Dist(From, Point) / FMath::Max(ProjectileSpeed, 1.f);
		Point = TargetLoc + Relative * Time;
	}
	return Point;
}

void ASoulsFighter::TickDroneBrain(float DeltaSeconds)
{
	const float Now = GetWorld()->GetTimeSeconds();
	const FVector Pos = GetActorLocation();
	const FVector Fwd = FlightRotation.GetForwardVector();
	FVector Desired = Fwd;
	float WantSpeed = 3600.f;
	bool bWantFire = false;
	ASoulsFighter* Prey = FindPlayerTarget();
	if (Now < DroneHoldUntil)
	{
		WantSpeed = FlightSpeed;
	}
	else if (Prey && Now < DroneBreakUntil)
	{
		Desired = DroneBreakDir;
		WantSpeed = CruiseSpeed;
	}
	else if (Prey)
	{
		const FVector Lead = LeadPoint(Prey, Pos, LaserSpeed);
		const float Distance = FVector::Dist(Prey->GetActorLocation(), Pos);
		Desired = (Lead - Pos).GetSafeNormal();
		WantSpeed = FMath::Clamp(Prey->GetForwardSpeed() + (Distance > 6000.f ? 1600.f : 200.f), 2600.f, CruiseSpeed);
		if (Distance < 1700.f)
		{
			// Too close: break off to one side and climb, then come round again.
			const float Side = FMath::FRand() < 0.5f ? -1.f : 1.f;
			DroneBreakDir = (Fwd + FlightRotation.GetRightVector() * Side * 1.2f + FVector(0.f, 0.f, 0.5f)).GetSafeNormal();
			DroneBreakUntil = Now + FMath::FRandRange(1.4f, 2.2f);
		}
		if (FVector::DotProduct(Fwd, Desired) > FMath::Cos(FMath::DegreesToRadians(7.f)) && Distance < 11000.f)
		{
			FHitResult Block;
			FCollisionQueryParams Params(SCENE_QUERY_STAT(DroneSight), false, this);
			Params.AddIgnoredActor(Prey);
			bWantFire = !GetWorld()->LineTraceSingleByObjectType(Block, Pos, Prey->GetActorLocation(), FighterRig::Terrain(), Params);
		}
	}
	else
	{
		// Patrol: round the island anticlockwise at its altitude.
		const FVector Off = Pos - DroneOrbitCentre;
		const float Angle = FMath::Atan2(Off.Y, Off.X) + 0.45f;
		const FVector Point = DroneOrbitCentre + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * DroneOrbitRadius;
		Desired = (FVector(Point.X, Point.Y, DroneAltitude) - Pos).GetSafeNormal();
		WantSpeed = 3400.f;
	}
	// Terrain and sea avoidance.
	FHitResult Ahead;
	FCollisionQueryParams AheadParams(SCENE_QUERY_STAT(DroneAhead), false, this);
	if (GetWorld()->LineTraceSingleByObjectType(Ahead, Pos, Pos + FlightVelocity * 2.2f - FVector(0.f, 0.f, 700.f), FighterRig::Terrain(), AheadParams)
		|| GroundClearance < 2200.f)
	{
		Desired = (Desired + FVector(0.f, 0.f, 2.5f)).GetSafeNormal();
	}
	if (Desired.Z < 0.f && GroundClearance < 3500.f)
	{
		Desired.Z *= 0.3f;
		Desired.Normalize();
	}

	const FVector Local = FlightRotation.UnrotateVector(Desired);
	float YawErr = FMath::RadiansToDegrees(FMath::Atan2(Local.Y, Local.X));
	const float PitchErr = FMath::RadiansToDegrees(FMath::Atan2(Local.Z, FMath::Max(Local.Size2D(), 0.01f)));
	if (Local.X < 0.f && FMath::Abs(YawErr) > 150.f)
	{
		YawErr = 150.f;
	}
	const float YawCmd = FMath::Clamp(YawErr / 25.f, -1.f, 1.f);
	const float PitchCmd = FMath::Clamp(PitchErr / 15.f, -1.f, 1.f);
	ThrottleSetting = FMath::Clamp(WantSpeed / FMath::Max(CruiseSpeed, 1.f), 0.2f, 1.f);
	FlightSpeed = FMath::FInterpConstantTo(FlightSpeed, ThrottleSetting * CruiseSpeed, DeltaSeconds, Acceleration);
	bFireHeld = bWantFire;
	Integrate(DeltaSeconds, PitchCmd, YawCmd, 0.f, true);
	MoveWithCollision(DeltaSeconds);
}

void ASoulsFighter::DroneDown()
{
	bDroneDown = true;
	DroneDownAt = GetWorld()->GetTimeSeconds();
	bFireHeld = false;
	Explode(GetActorLocation(), FVector::UpVector, 1.6f, false);
	for (int32 Index = 0; Index < 5; ++Index)
	{
		ASoulsSparks::Burst(GetWorld(), GetActorLocation() + FMath::VRand() * 120.f, FMath::VRand(), 10, 1.6f, 1.f, FLinearColor(1.f, 0.5f, 0.2f));
	}
	SetActorHiddenInGame(true);
	SetActorEnableCollision(false);
	UE_LOG(LogUnrealSoulslike, Log, TEXT("Fighter: drone %s shot down"), *GetName());
}

void ASoulsFighter::DroneRespawn()
{
	// Back in on the far side of the orbit from whoever is flying.
	FVector Away = FVector(1.f, 0.f, 0.f);
	if (const ASoulsFighter* Prey = FindPlayerTarget())
	{
		Away = (DroneOrbitCentre - Prey->GetActorLocation()).GetSafeNormal2D();
	}
	if (Away.IsNearlyZero())
	{
		Away = FVector(1.f, 0.f, 0.f);
	}
	const FVector Where = DroneOrbitCentre + Away * DroneOrbitRadius + FVector(0.f, 0.f, DroneAltitude);
	bDroneDown = false;
	Hull = MaxHull;
	for (UMaterialInstanceDynamic* Mid : HullMaterials)
	{
		if (Mid)
		{
			Mid->SetScalarParameterValue(TEXT("Scorch"), 0.f);
		}
	}
	SetActorHiddenInGame(false);
	SetActorEnableCollision(true);
	PlaceInFlight(Where, Away.Rotation().Yaw + 90.f, 3400.f);
	UE_LOG(LogUnrealSoulslike, Log, TEXT("Fighter: drone %s back on patrol"), *GetName());
}

// ---------------------------------------------------------------------------------------------
// Weapons
// ---------------------------------------------------------------------------------------------

FVector ASoulsFighter::MuzzleLocation(int32 Gun) const
{
	return Frame->GetComponentTransform().TransformPosition(bDrone ? FighterRig::DroneMuzzle[Gun] : FighterRig::Muzzle[Gun]);
}

bool ASoulsFighter::IsHostileTo(const AActor* Other) const
{
	if (!Other || Other == this)
	{
		return false;
	}
	if (const ASoulsFighter* Ship = Cast<ASoulsFighter>(Other))
	{
		if (!Ship->IsAlive() || Ship->IsDrone() == bDrone)
		{
			return false;
		}
		return bDrone ? Ship->IsPiloted() : true;
	}
	if (bDrone)
	{
		return false;
	}
	if (const ASoulsKnight* Knight = Cast<ASoulsKnight>(Other))
	{
		return Knight->Team != 0 && !Knight->IsDead() && Knight != Pilot.Get();
	}
	return false;
}

FVector ASoulsFighter::AimPoint(AActor** OutAssist) const
{
	const FVector From = GetActorLocation();
	const FVector Fwd = FlightRotation.GetForwardVector();
	// The guns converge where the nose is pointing (on whatever is there, else 300 m out).
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(FighterAim), false, this);
	float Converge = 30000.f;
	if (GetWorld()->LineTraceSingleByObjectType(Hit, From + Fwd * 700.f, From + Fwd * 30000.f, FighterRig::Terrain(), Params))
	{
		Converge = FMath::Max(Hit.Distance + 700.f, 1500.f);
	}
	FVector Point = From + Fwd * Converge;
	// A little aim assist: a hostile whose lead point sits within a few degrees of the nose draws the bolts onto it.
	float Best = FMath::DegreesToRadians(bDrone ? 4.f : 3.5f);
	AActor* Assist = nullptr;
	auto Consider = [&](AActor* Candidate)
	{
		if (!IsHostileTo(Candidate))
		{
			return;
		}
		const FVector Lead = LeadPoint(Candidate, From, LaserSpeed);
		const float Distance = FVector::Dist(Lead, From);
		if (Distance > 25000.f || Distance < 300.f)
		{
			return;
		}
		const float Angle = FMath::Acos(FMath::Clamp(FVector::DotProduct(Fwd, (Lead - From) / Distance), -1.f, 1.f));
		if (Angle < Best)
		{
			Best = Angle;
			Point = Lead;
			Assist = Candidate;
		}
	};
	for (TActorIterator<ASoulsFighter> It(GetWorld()); It; ++It)
	{
		Consider(*It);
	}
	if (!bDrone)
	{
		for (TActorIterator<ASoulsKnight> It(GetWorld()); It; ++It)
		{
			if (It->Team != 0 && FVector::DistSquared(It->GetActorLocation(), From) < FMath::Square(25000.f))
			{
				Consider(*It);
			}
		}
	}
	if (OutAssist)
	{
		*OutAssist = Assist;
	}
	return Point;
}

int32 ASoulsFighter::TakeBoltVisual(bool bMissile)
{
	for (int32 Index = 0; Index < BoltPool.Num(); ++Index)
	{
		if (!BoltPoolUsed[Index])
		{
			BoltPoolUsed[Index] = true;
			BoltPool[Index]->SetMaterial(0, bMissile ? GlowMaterial.Get() : (BoltMaterial ? static_cast<UMaterialInterface*>(BoltMaterial.Get()) : GlowMaterial.Get()));
			BoltPool[Index]->SetVisibility(true);
			return Index;
		}
	}
	return INDEX_NONE;
}

int32 ASoulsFighter::TakeLight(const FVector& Where, const FLinearColor& Color, float Intensity, float Radius, float Fade)
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
	LightFade[Pick] = Fade;
	LightFadeTotal[Pick] = FMath::Max(Fade, KINDA_SMALL_NUMBER);
	LightBase[Pick] = Intensity;
	return Pick;
}

void ASoulsFighter::SpawnPuff(const FVector& Where, const FVector& Velocity, float Size, float Grow, float Life, float Opacity, bool bFire)
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
		Comp->SetMaterial(0, bFire ? GlowMaterial.Get() : static_cast<UMaterialInterface*>(PuffMaterials[Index].Get()));
		Comp->SetWorldLocationAndRotation(Where, FRotator(FMath::FRandRange(0.f, 360.f), FMath::FRandRange(0.f, 360.f), 0.f));
		Comp->SetWorldScale3D(FVector(Size * 0.2f));
		Comp->SetVisibility(true);
		FFighterPuff Puff;
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

void ASoulsFighter::TickPuffs(float DeltaSeconds)
{
	for (int32 Index = Puffs.Num() - 1; Index >= 0; --Index)
	{
		FFighterPuff& Puff = Puffs[Index];
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
	for (int32 Gun = 0; Gun < 2; ++Gun)
	{
		MuzzleFlashT[Gun] = FMath::Max(MuzzleFlashT[Gun] - DeltaSeconds, 0.f);
		const float F = MuzzleFlashT[Gun] / 0.05f;
		MuzzleLights[Gun]->SetIntensity(30000.f * F);
		MuzzleFlashes[Gun]->SetVisibility(F > 0.f);
		if (F > 0.f)
		{
			MuzzleFlashes[Gun]->SetRelativeScale3D(FVector(1.4f, 0.6f, 0.6f) * (bDrone ? 0.7f : 1.f) * (0.5f + 0.5f * F) * FMath::FRandRange(0.8f, 1.2f));
		}
	}
	// Damaged: trailing smoke (and fire, near the end).
	if (IsAlive() && (State == EFighterState::Piloted || State == EFighterState::Drone) && GetHullFraction() < 0.5f
		&& FMath::FRand() < DeltaSeconds * (GetHullFraction() < 0.25f ? 18.f : 8.f))
	{
		const FVector Back = GetActorLocation() - FlightRotation.GetForwardVector() * 200.f + FMath::VRand() * 60.f;
		SpawnPuff(Back, -FlightVelocity * 0.05f + FVector(0.f, 0.f, 80.f), 0.6f, 2.4f, 1.6f, 0.55f, GetHullFraction() < 0.25f && FMath::FRand() < 0.3f);
	}
}

void ASoulsFighter::TickWeapons(float DeltaSeconds)
{
	LaserCooldown = FMath::Max(LaserCooldown - DeltaSeconds, 0.f);
	const bool bArmed = (State == EFighterState::Piloted && Flight == EFighterFlight::Flying) || (State == EFighterState::Drone && !bDroneDown);
	if (bArmed && bFireHeld && LaserCooldown <= 0.f)
	{
		FireLaser();
		LaserCooldown += 1.f / FMath::Max(LaserRate, 0.5f);
	}
	if (!bArmed && !bFireHeld)
	{
		LaserCooldown = 0.f;
	}
}

void ASoulsFighter::FireLaser()
{
	const float Now = GetWorld()->GetTimeSeconds();
	const int32 Gun = NextGun;
	NextGun = 1 - NextGun;
	const FVector Muzzle = MuzzleLocation(Gun);
	FVector Dir = (AimPoint() - Muzzle).GetSafeNormal();
	if (Dir.IsNearlyZero() || FVector::DotProduct(Dir, FlightRotation.GetForwardVector()) < 0.8f)
	{
		Dir = FlightRotation.GetForwardVector();
	}
	Dir = FMath::VRandCone(Dir, FMath::DegreesToRadians(bDrone ? 1.2f : 0.25f));
	FFighterBolt Bolt;
	Bolt.bEnemy = bDrone;
	Bolt.Location = Muzzle;
	Bolt.Velocity = Dir * LaserSpeed + FlightVelocity;
	Bolt.Life = 1.3f;
	Bolt.Visual = TakeBoltVisual(false);
	Bolts.Add(Bolt);

	MuzzleFlashT[Gun] = 0.05f;
	MuzzleFlashes[Gun]->SetWorldRotation(Dir.Rotation());
	PlayFighterSound(bDrone ? EFighterSound::DroneLaser : EFighterSound::Laser, Muzzle, bDrone ? 0.8f : 0.55f, FMath::FRandRange(0.95f, 1.05f));
	++BoltsFired;
	LastShotTime = Now;
	if (IsPlayerControlled())
	{
		Shake(0.04f);
	}
}

void ASoulsFighter::FireMissile()
{
	const float Now = GetWorld()->GetTimeSeconds();
	if (State != EFighterState::Piloted || Flight != EFighterFlight::Flying || Missiles <= 0 || Now < MissileCooldown)
	{
		return;
	}
	const FVector Fwd = FlightRotation.GetForwardVector();
	const float Side = (MissilesFired % 2 == 0) ? -1.f : 1.f;
	const FVector Start = GetActorTransform().TransformPosition(FVector(120.f, Side * 160.f, -170.f));
	FFighterBolt Rocket;
	Rocket.bMissile = true;
	Rocket.Location = Start;
	Rocket.Velocity = FlightVelocity + Fwd * 1500.f - FlightRotation.GetUpVector() * 350.f;
	Rocket.Life = 6.f;
	Rocket.Homing = GetLockedTarget();
	Rocket.Visual = TakeBoltVisual(true);
	Rocket.Light = TakeLight(Start, FLinearColor(1.f, 0.55f, 0.2f), 8000.f, 700.f, -1.f);
	Bolts.Add(Rocket);
	--Missiles;
	++MissilesFired;
	MissileCooldown = Now + 0.6f;
	SpawnPuff(Start, -Fwd * 300.f, 0.6f, 2.5f, 1.2f, 0.5f, false);
	PlayFighterSound(EFighterSound::Missile, Start, 0.9f, FMath::FRandRange(0.94f, 1.06f));
	Shake(0.12f);
	UE_LOG(LogUnrealSoulslike, Log, TEXT("Fighter: missile away (%s)"), Rocket.Homing.IsValid() ? *Rocket.Homing->GetName() : TEXT("no lock"));
}

void ASoulsFighter::TickLock(float DeltaSeconds)
{
	const float Now = GetWorld()->GetTimeSeconds();
	if (Flight != EFighterFlight::Flying)
	{
		LockCandidate = nullptr;
		LockProgress = 0.f;
		return;
	}
	const FVector From = GetActorLocation();
	const FVector Fwd = FlightRotation.GetForwardVector();
	float Best = FMath::Cos(FMath::DegreesToRadians(LockParams.X));
	AActor* Pick = nullptr;
	auto Consider = [&](AActor* Candidate)
	{
		if (!IsHostileTo(Candidate))
		{
			return;
		}
		const FVector To = Candidate->GetActorLocation() - From;
		const float Distance = To.Size();
		const float Dot = FVector::DotProduct(To / FMath::Max(Distance, 1.f), Fwd);
		if (Distance < LockParams.Y && Distance > 500.f && Dot > Best)
		{
			FHitResult Block;
			FCollisionQueryParams Params(SCENE_QUERY_STAT(FighterLock), false, this);
			Params.AddIgnoredActor(Candidate);
			if (!GetWorld()->LineTraceSingleByObjectType(Block, From, Candidate->GetActorLocation(), FighterRig::Terrain(), Params))
			{
				Best = Dot;
				Pick = Candidate;
			}
		}
	};
	for (TActorIterator<ASoulsFighter> It(GetWorld()); It; ++It)
	{
		Consider(*It);
	}
	for (TActorIterator<ASoulsEnemy> It(GetWorld()); It; ++It)
	{
		if (FVector::DistSquared(It->GetActorLocation(), From) < FMath::Square(LockParams.Y))
		{
			Consider(*It);
		}
	}
	if (!Pick)
	{
		LockCandidate = nullptr;
		LockedTarget = nullptr;
		LockProgress = 0.f;
		return;
	}
	if (LockCandidate.Get() != Pick)
	{
		LockCandidate = Pick;
		LockProgress = 0.f;
	}
	const bool bWasLocked = LockProgress >= 1.f;
	LockProgress = FMath::Min(LockProgress + DeltaSeconds / FMath::Max(LockParams.Z, 0.05f), 1.f);
	LockedTarget = Pick;
	if (IsPlayerControlled())
	{
		if (LockProgress >= 1.f && !bWasLocked)
		{
			PlayFighterSound(EFighterSound::LockTone, From, 0.45f, 1.35f);
			LastLockTone = Now;
		}
		else if (LockProgress < 1.f && Now - LastLockTone > 0.22f)
		{
			PlayFighterSound(EFighterSound::LockTone, From, 0.3f, 1.f);
			LastLockTone = Now;
		}
	}
}

void ASoulsFighter::TickBolts(float DeltaSeconds)
{
	FCollisionQueryParams Params(SCENE_QUERY_STAT(FighterBolt), false, this);
	if (const ASoulsKnight* Knight = Pilot.Get())
	{
		Params.AddIgnoredActor(Knight);
	}
	for (int32 Index = Bolts.Num() - 1; Index >= 0; --Index)
	{
		FFighterBolt& Bolt = Bolts[Index];
		Bolt.Age += DeltaSeconds;
		if (Bolt.bMissile)
		{
			const float NewSpeed = FMath::Min(Bolt.Velocity.Size() + 9000.f * DeltaSeconds, 15000.f);
			FVector Dir = Bolt.Velocity.GetSafeNormal();
			if (Bolt.Age > 0.25f && Bolt.Homing.IsValid())
			{
				const FVector Want = (LeadPoint(Bolt.Homing.Get(), Bolt.Location, NewSpeed) - Bolt.Location).GetSafeNormal();
				const float MaxTurn = FMath::DegreesToRadians(150.f) * DeltaSeconds;
				const float Angle = FMath::Acos(FMath::Clamp(FVector::DotProduct(Dir, Want), -1.f, 1.f));
				if (Angle > KINDA_SMALL_NUMBER)
				{
					Dir = FMath::Lerp(Dir, Want, FMath::Min(MaxTurn / Angle, 1.f)).GetSafeNormal();
				}
			}
			Bolt.Velocity = Dir * NewSpeed;
			Bolt.NextPuff -= DeltaSeconds;
			if (Bolt.NextPuff <= 0.f)
			{
				Bolt.NextPuff = 0.03f;
				SpawnPuff(Bolt.Location - Dir * 70.f, -Dir * 120.f + FMath::VRand() * 40.f, 0.3f, 2.2f, 1.5f, 0.5f, false);
			}
		}
		const FVector From = Bolt.Location;
		const FVector To = From + Bolt.Velocity * DeltaSeconds;
		FHitResult WorldHit, PawnHit;
		const bool bWorld = GetWorld()->LineTraceSingleByObjectType(WorldHit, From, To, FighterRig::Terrain(), Params);
		bool bPawn = GetWorld()->SweepSingleByObjectType(PawnHit, From, To, FQuat::Identity, FCollisionObjectQueryParams(ECC_Pawn),
			FCollisionShape::MakeSphere(Bolt.bMissile ? 90.f : (Bolt.bEnemy ? 35.f : 60.f)), Params);
		if (bPawn)
		{
			// Bolts fly through friends (drones don't shoot drones; no friendly fire on the players' side).
			const ASoulsFighter* Ship = Cast<ASoulsFighter>(PawnHit.GetActor());
			const ASoulsKnight* Person = Cast<ASoulsKnight>(PawnHit.GetActor());
			if ((Ship && !IsHostileTo(Ship)) || (Person && (Bolt.bEnemy || Person->Team == 0)))
			{
				bPawn = false;
			}
		}
		// Proximity fuse on a homing missile.
		bool bFuse = false;
		if (Bolt.bMissile && Bolt.Homing.IsValid() && FVector::Dist(To, Bolt.Homing->GetActorLocation()) < 420.f)
		{
			bFuse = true;
		}
		Bolt.Location = To;
		if (Bolt.Visual != INDEX_NONE)
		{
			UStaticMeshComponent* Comp = BoltPool[Bolt.Visual];
			const FVector Dir = Bolt.Velocity.GetSafeNormal();
			Comp->SetWorldLocationAndRotation(To - Dir * (Bolt.bMissile ? 0.f : 150.f), Dir.Rotation());
			Comp->SetWorldScale3D(Bolt.bMissile ? FVector(0.6f, 0.18f, 0.18f) : (Bolt.bEnemy ? FVector(2.4f, 0.11f, 0.11f) : FVector(3.2f, 0.12f, 0.12f)));
		}
		if (Bolt.Light != INDEX_NONE && LightPool.IsValidIndex(Bolt.Light))
		{
			LightPool[Bolt.Light]->SetWorldLocation(To);
		}
		const bool bHit = bWorld || bPawn;
		if (bHit || bFuse || Bolt.Age >= Bolt.Life)
		{
			if (bFuse)
			{
				Explode(To, -Bolt.Velocity.GetSafeNormal(), 1.f, true);
			}
			else if (bHit)
			{
				const FHitResult& Hit = (bPawn && (!bWorld || PawnHit.Time <= WorldHit.Time)) ? PawnHit : WorldHit;
				BoltImpact(Bolt, Hit);
			}
			else if (Bolt.bMissile)
			{
				Explode(To, FVector::UpVector, 0.8f, true);
			}
			if (Bolt.Visual != INDEX_NONE)
			{
				BoltPool[Bolt.Visual]->SetVisibility(false);
				BoltPoolUsed[Bolt.Visual] = false;
			}
			if (Bolt.Light != INDEX_NONE && LightPool.IsValidIndex(Bolt.Light))
			{
				LightPool[Bolt.Light]->SetIntensity(0.f);
				LightFade[Bolt.Light] = 0.f;
			}
			Bolts.RemoveAtSwap(Index);
		}
	}
}

void ASoulsFighter::BoltImpact(const FFighterBolt& Bolt, const FHitResult& Hit)
{
	const FVector Dir = Bolt.Velocity.GetSafeNormal();
	const FVector Point = Hit.ImpactPoint;
	if (Bolt.bMissile)
	{
		Explode(Point, Hit.ImpactNormal, 1.f, true);
		return;
	}
	const FLinearColor Flash = Bolt.bEnemy ? FLinearColor(1.f, 0.3f, 0.15f) : FLinearColor(0.4f, 1.f, 0.55f);
	ASoulsSparks::Burst(GetWorld(), Point, Hit.ImpactNormal, 8, 1.f, 0.7f, Flash);
	TakeLight(Point + Hit.ImpactNormal * 40.f, Flash, 12000.f, 600.f, 0.15f);
	SpawnPuff(Point + Hit.ImpactNormal * 25.f, Hit.ImpactNormal * 140.f, 0.3f, 2.2f, 0.8f, 0.4f, false);
	PlayFighterSound(EFighterSound::Zap, Point, 0.45f, FMath::FRandRange(0.9f, 1.15f));
	DamageActor(Hit.GetActor(), LaserDamage, Point, Dir, 120.f);
	if (UPrimitiveComponent* Comp = Hit.GetComponent())
	{
		if (Comp->IsSimulatingPhysics())
		{
			Comp->AddImpulseAtLocation(Dir * 30000.f, Point);
		}
	}
}

void ASoulsFighter::Explode(const FVector& Where, const FVector& Normal, float Scale, bool bDamage)
{
	ASoulsSparks::Burst(GetWorld(), Where, Normal, 24, 2.4f * Scale, 1.f, FLinearColor(1.f, 0.55f, 0.18f));
	TakeLight(Where + Normal * 80.f, FLinearColor(1.f, 0.5f, 0.15f), 90000.f * Scale, 2400.f * Scale, 0.5f);
	for (int32 Index = 0; Index < 5; ++Index)
	{
		SpawnPuff(Where + Normal * 60.f + FMath::VRand() * 80.f * Scale, FMath::VRand() * 280.f * Scale + Normal * 200.f,
			FMath::FRandRange(1.4f, 2.2f) * Scale, 1.f, FMath::FRandRange(0.35f, 0.55f), 1.f, true);
	}
	for (int32 Index = 0; Index < 6; ++Index)
	{
		SpawnPuff(Where + Normal * 80.f + FMath::VRand() * 100.f * Scale, FMath::VRand() * 160.f + FVector(0.f, 0.f, 200.f),
			FMath::FRandRange(1.2f, 1.9f) * Scale, 1.8f, FMath::FRandRange(2.4f, 3.6f), 0.65f, false);
	}
	PlayFighterSound(EFighterSound::Explosion, Where, 1.f, FMath::FRandRange(0.9f, 1.1f));
	if (IsPlayerControlled())
	{
		Shake(FMath::Clamp(1.f - FVector::Dist(Where, GetActorLocation()) / 4000.f, 0.f, 1.f) * 0.9f);
	}
	if (!bDamage)
	{
		return;
	}
	const float Radius = 650.f * Scale;
	for (TActorIterator<ASoulsFighter> It(GetWorld()); It; ++It)
	{
		const float Distance = FVector::Dist(It->GetActorLocation(), Where);
		if (Distance < Radius + 200.f && IsHostileTo(*It))
		{
			DamageActor(*It, MissileDamage * (1.f - 0.5f * Distance / (Radius + 200.f)), It->GetActorLocation(), (It->GetActorLocation() - Where).GetSafeNormal(), 0.f);
		}
	}
	for (TActorIterator<ASoulsKnight> It(GetWorld()); It; ++It)
	{
		const FVector Off = It->GetActorLocation() - Where;
		const float Distance = Off.Size();
		if (Distance < Radius && IsHostileTo(*It))
		{
			const float Falloff = 1.f - 0.65f * Distance / Radius;
			DamageActor(*It, MissileDamage * 0.6f * Falloff, It->GetActorLocation(), Off.GetSafeNormal(), 900.f * Falloff);
		}
		if (!bDrone && It->Team != 0 && !It->IsDead() && Distance < Radius * 2.5f)
		{
			It->AddFear(20.f);
		}
	}
}

bool ASoulsFighter::DamageActor(AActor* Victim, float Damage, const FVector& Where, const FVector& Direction, float Push)
{
	if (!Victim || Victim == this || Damage <= 0.f)
	{
		return false;
	}
	const float Now = GetWorld()->GetTimeSeconds();
	if (ASoulsFighter* Ship = Cast<ASoulsFighter>(Victim))
	{
		if (!IsHostileTo(Ship))
		{
			return false;
		}
		Ship->ApplyHullDamage(Damage, this, Where);
		++HitsLanded;
		LastHitMarker = Now;
		return true;
	}
	if (bDrone)
	{
		return false;
	}
	AActor* Causer = Pilot.IsValid() ? static_cast<AActor*>(Pilot.Get()) : this;
	if (ASoulsKnight* Knight = Cast<ASoulsKnight>(Victim))
	{
		if (!IsHostileTo(Knight))
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
	if (ICombatDamageable* Damageable = Cast<ICombatDamageable>(Victim))
	{
		// The template's damage scale is ~12x smaller than ours.
		Damageable->ApplyDamage(Damage / 12.f, Causer, Where, Direction * Push);
		++HitsLanded;
		LastHitMarker = Now;
		return true;
	}
	if (Victim->ActorHasTag(FName(TEXT("FighterTarget"))))
	{
		++HitsLanded;
		LastHitMarker = Now;
		return true;
	}
	return false;
}

// ---------------------------------------------------------------------------------------------
// Hull, wreck, repair
// ---------------------------------------------------------------------------------------------

float ASoulsFighter::TakeDamage(float Damage, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	if (DamageCauser == this || (DamageCauser && DamageCauser == Pilot.Get()))
	{
		return 0.f;
	}
	// Co-op: the players' own blades don't scratch it.
	if (const ASoulsKnight* Knight = Cast<ASoulsKnight>(DamageCauser))
	{
		if (Knight->Team == 0)
		{
			return 0.f;
		}
	}
	ApplyHullDamage(Damage, DamageCauser, DamageCauser ? DamageCauser->GetActorLocation() : GetActorLocation());
	return Damage;
}

void ASoulsFighter::ApplyHullDamage(float Amount, AActor* Causer, const FVector& Where)
{
	if (!IsAlive() || Amount <= 0.f || State == EFighterState::Boarding || State == EFighterState::Exiting)
	{
		return;
	}
	Hull -= Amount;
	LastHitTime = GetWorld()->GetTimeSeconds();
	LastHitFrom = Causer ? Causer->GetActorLocation() : Where;
	const float Scorch = 0.85f * (1.f - GetHullFraction());
	for (UMaterialInstanceDynamic* Mid : HullMaterials)
	{
		if (Mid)
		{
			Mid->SetScalarParameterValue(TEXT("Scorch"), Scorch);
		}
	}
	if (IsPlayerControlled())
	{
		Shake(0.1f + FMath::Min(Amount * 0.006f, 0.4f));
	}
	if (Hull <= 0.f)
	{
		Hull = 0.f;
		if (ASoulsFighter* Victor = Cast<ASoulsFighter>(Causer))
		{
			++Victor->Kills;
		}
		if (bDrone)
		{
			DroneDown();
		}
		else
		{
			Wreck();
		}
	}
}

void ASoulsFighter::EjectPilot(bool bHurt)
{
	ASoulsKnight* Knight = Pilot.Get();
	if (!Knight)
	{
		return;
	}
	APlayerController* PC = Cast<APlayerController>(GetController());
	Knight->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	Knight->SetActorHiddenInGame(false);
	const FVector Spot = GetActorLocation() - GetActorRightVector() * 600.f;
	FVector P, N;
	const bool bGround = GroundBelow(FVector(Spot.X, Spot.Y, GetActorLocation().Z + 500.f), P, N);
	const float GroundZ = bGround ? FMath::Max(P.Z, FighterRig::SeaZ) : FighterRig::SeaZ;
	Knight->SetActorLocation(FVector(Spot.X, Spot.Y, GroundZ + Knight->GetSimpleCollisionHalfHeight() + 40.f), false, nullptr, ETeleportType::TeleportPhysics);
	Knight->SetActorEnableCollision(true);
	Knight->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
	if (PC)
	{
		OnDestroyed.RemoveAll(PC);
		Knight->OnDestroyed.RemoveAll(PC);
		// Its keys (A among them) must stop answering before the knight takes over (UNR-T73).
		if (UEnhancedInputLocalPlayerSubsystem* KnightInput = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
		{
			KnightInput->RemoveMappingContext(FighterContext);
		}
		PC->Possess(Knight);
		PC->SetControlRotation(FRotator(-15.f, (GetActorLocation() - Knight->GetActorLocation()).Rotation().Yaw, 0.f));
	}
	Knight->LaunchCharacter(-GetActorRightVector() * 400.f + FVector(0.f, 0.f, 350.f), true, true);
	if (bHurt)
	{
		FDamageEvent Event;
		Knight->TakeDamage(25.f, Event, nullptr, this);
	}
	Pilot = nullptr;
	bCockpitView = false;
	ChaseCamera->SetActive(true);
	CockpitCamera->SetActive(false);
}

void ASoulsFighter::Wreck()
{
	State = EFighterState::Wrecked;
	Flight = EFighterFlight::Grounded;
	WreckedAt = GetWorld()->GetTimeSeconds();
	bFireHeld = bBoostHeld = bBrakeHeld = false;
	bBoosting = false;
	Explode(GetActorLocation(), FVector::UpVector, 2.f, false);
	UE_LOG(LogUnrealSoulslike, Log, TEXT("Fighter: %s destroyed"), *GetName());
	EjectPilot(true);
	// The wreck comes down where it fell, nose in; over the sea it sinks out of sight until it's repaired.
	FVector P, N;
	const bool bGround = GroundBelow(GetActorLocation() + FVector(0.f, 0.f, 200.f), P, N);
	FlightVelocity = FVector::ZeroVector;
	FlightSpeed = 0.f;
	GearAlpha = GearTarget = 0.f;
	if (bGround && P.Z > FighterRig::SeaZ)
	{
		const FRotator Tilt(-12.f, GetActorRotation().Yaw, FMath::FRandRange(-25.f, 25.f));
		SetActorLocationAndRotation(P + FVector(0.f, 0.f, 120.f), Tilt, false, nullptr, ETeleportType::TeleportPhysics);
		FlightRotation = GetActorQuat();
	}
	else
	{
		SetActorHiddenInGame(true);
	}
	SetActorEnableCollision(false);
}

void ASoulsFighter::Repair()
{
	State = EFighterState::Parked;
	Flight = EFighterFlight::Grounded;
	Hull = MaxHull;
	Missiles = MaxMissiles;
	BoostEnergy = 1.f;
	GearAlpha = GearTarget = 1.f;
	for (UMaterialInstanceDynamic* Mid : HullMaterials)
	{
		if (Mid)
		{
			Mid->SetScalarParameterValue(TEXT("Scorch"), 0.f);
		}
	}
	SetActorHiddenInGame(false);
	SetActorEnableCollision(true);
	if (bHomeSet)
	{
		SetActorTransform(HomeTransform, false, nullptr, ETeleportType::TeleportPhysics);
	}
	SettleOnGround(0.f, true);
	UE_LOG(LogUnrealSoulslike, Log, TEXT("Fighter: %s repaired on its pad"), *GetName());
}

// ---------------------------------------------------------------------------------------------
// Sound
// ---------------------------------------------------------------------------------------------

void ASoulsFighter::PlayFighterSound(EFighterSound Sound, const FVector& Where, float Volume, float Pitch) const
{
	const int32 Kind = static_cast<int32>(Sound);
	const TArray<int16>& Samples = FighterAudio::Get(Kind);
	if (Samples.IsEmpty() || !GetWorld() || GetWorld()->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	USoundWaveProcedural* Wave = NewObject<USoundWaveProcedural>(GetTransientPackage());
	Wave->SetSampleRate(FighterAudio::Rate);
	Wave->NumChannels = 1;
	Wave->Duration = static_cast<float>(Samples.Num()) / FighterAudio::Rate;
	Wave->SoundGroup = SOUNDGROUP_Default;
	Wave->bLooping = false;
	Wave->QueueAudio(reinterpret_cast<const uint8*>(Samples.GetData()), Samples.Num() * sizeof(int16));
	UGameplayStatics::PlaySoundAtLocation(this, Wave, Where, Volume, Pitch, 0.f, SoundFalloff.Get());
}

// ---------------------------------------------------------------------------------------------
// HUD: drawn over the game's own HUD through AHUD::OnHUDPostRender (no changes to ASoulsHUD).
// ---------------------------------------------------------------------------------------------

void ASoulsFighter::DrawFighterHUD(AHUD* HUD, UCanvas* DebugCanvas)
{
	UCanvas* Canvas = DebugCanvas;
	if (!HUD || !Canvas || !HUD->GetWorld() || !HUD->GetWorld()->IsGameWorld())
	{
		return;
	}
	APawn* Pawn = HUD->GetOwningPawn();
	if (const ASoulsFighter* Ship = Cast<ASoulsFighter>(Pawn))
	{
		Ship->DrawCockpitHUD(Canvas);
		return;
	}
	if (const ASoulsKnight* Knight = Cast<ASoulsKnight>(Pawn))
	{
		if (FindBoardable(Knight))
		{
			const float S = Canvas->ClipY / 1080.f;
			UFont* Font = GEngine->GetLargeFont();
			FCanvasTileItem Back(FVector2D(Canvas->ClipX * 0.5f - 320.f * S, Canvas->ClipY * 0.72f - 10.f * S), FVector2D(640.f * S, 50.f * S), FLinearColor(0.f, 0.f, 0.f, 0.55f));
			Back.BlendMode = SE_BLEND_Translucent;
			Canvas->DrawItem(Back);
			Canvas->K2_DrawText(Font, TEXT("Climb into the star fighter      [ F ]  /  D-pad Up"), FVector2D(Canvas->ClipX * 0.5f, Canvas->ClipY * 0.72f),
				FVector2D(1.05f * S, 1.05f * S), FLinearColor(0.75f, 0.9f, 1.f), 0.f, FLinearColor::Black, FVector2D(1.f, 1.f), true, false, true, FLinearColor::Black);
		}
	}
}

void ASoulsFighter::DrawCockpitHUD(UCanvas* Canvas) const
{
	const float S = Canvas->ClipY / 1080.f;
	const float Cx = Canvas->ClipX * 0.5f, Cy = Canvas->ClipY * 0.5f;
	const float Now = GetWorld()->GetTimeSeconds();
	UFont* Font = GEngine->GetMediumFont();
	UFont* Big = GEngine->GetLargeFont();
	const FLinearColor Cyan(0.45f, 0.95f, 1.f, 0.9f);
	const FLinearColor Dim(0.45f, 0.95f, 1.f, 0.35f);
	const FLinearColor Amber(1.f, 0.72f, 0.2f, 0.95f);
	const FLinearColor Red(1.f, 0.2f, 0.12f, 0.95f);
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
	auto Ring = [&Line](float X, float Y, float R, const FLinearColor& Color, int32 Sides, float Width)
	{
		for (int32 Index = 0; Index < Sides; ++Index)
		{
			const float A0 = 2.f * PI * Index / Sides, A1 = 2.f * PI * (Index + 1) / Sides;
			Line(X + FMath::Cos(A0) * R, Y + FMath::Sin(A0) * R, X + FMath::Cos(A1) * R, Y + FMath::Sin(A1) * R, Color, Width);
		}
	};

	if (State == EFighterState::Boarding)
	{
		Text(Big, TEXT("CANOPY ... ENGINES ..."), Cx, Cy + 220.f * S, Cyan, 1.2f, true);
		return;
	}
	if (State != EFighterState::Piloted)
	{
		return;
	}
	const APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC || !PC->PlayerCameraManager)
	{
		return;
	}
	const FVector Loc = GetActorLocation();
	const FVector Fwd = FlightRotation.GetForwardVector();
	const FVector CamLoc = PC->PlayerCameraManager->GetCameraLocation();
	const FQuat CamRot = PC->PlayerCameraManager->GetCameraRotation().Quaternion();

	// Horizon: a long dim line where the world's horizon is.
	{
		FVector Flat = Fwd;
		Flat.Z = 0.f;
		if (Flat.Normalize())
		{
			const FVector Side = FVector::CrossProduct(FVector::UpVector, Flat);
			FVector2D A, B;
			const FVector Far = FVector(CamLoc.X, CamLoc.Y, CamLoc.Z) + Flat * 60000.f;
			if (PC->ProjectWorldLocationToScreen(Far - Side * 9000.f, A, true) && PC->ProjectWorldLocationToScreen(Far + Side * 9000.f, B, true))
			{
				const FVector2D Mid = (A + B) * 0.5f;
				const FVector2D Half = (B - A).GetSafeNormal() * 420.f * S;
				Line(Mid.X - Half.X, Mid.Y - Half.Y, Mid.X - Half.X * 0.18f, Mid.Y - Half.Y * 0.18f, Dim, 1.5f);
				Line(Mid.X + Half.X * 0.18f, Mid.Y + Half.Y * 0.18f, Mid.X + Half.X, Mid.Y + Half.Y, Dim, 1.5f);
			}
		}
	}

	// Gun reticle: where the bolts converge.
	FVector2D Ret(Cx, Cy);
	const bool bAirborne = Flight == EFighterFlight::Flying;
	AActor* Assist = nullptr;
	const FVector Aim = AimPoint(&Assist);
	if (PC->ProjectWorldLocationToScreen(Loc + Fwd * FMath::Min(FVector::Dist(Aim, Loc), 12000.f), Ret, true))
	{
		const FLinearColor Col = Assist ? Amber : Cyan;
		const float Gap = 10.f * S, Len = 16.f * S;
		Line(Ret.X - Gap - Len, Ret.Y, Ret.X - Gap, Ret.Y, Col);
		Line(Ret.X + Gap, Ret.Y, Ret.X + Gap + Len, Ret.Y, Col);
		Line(Ret.X, Ret.Y + Gap, Ret.X, Ret.Y + Gap + Len, Col);
		Ring(Ret.X, Ret.Y, 26.f * S, FLinearColor(Col.R, Col.G, Col.B, 0.5f), 24, 1.5f);
		Rect(Ret.X - 2.f * S, Ret.Y - 2.f * S, 4.f * S, 4.f * S, Col);
		if (Now - LastHitMarker < 0.18f)
		{
			const float A = 16.f * S, B = 28.f * S;
			for (const FVector2D Sign : { FVector2D(1.f, 1.f), FVector2D(-1.f, 1.f), FVector2D(1.f, -1.f), FVector2D(-1.f, -1.f) })
			{
				Line(Ret.X + Sign.X * A, Ret.Y + Sign.Y * A, Ret.X + Sign.X * B, Ret.Y + Sign.Y * B, FLinearColor(1.f, 1.f, 1.f, 0.95f), 2.5f);
			}
		}
	}
	// Flight path marker: where the ship is actually going.
	if (bAirborne && FlightVelocity.Size() > 300.f)
	{
		FVector2D Fpm;
		if (PC->ProjectWorldLocationToScreen(Loc + FlightVelocity.GetSafeNormal() * 8000.f, Fpm, true))
		{
			Ring(Fpm.X, Fpm.Y, 7.f * S, Dim, 12, 1.5f);
			Line(Fpm.X - 18.f * S, Fpm.Y, Fpm.X - 7.f * S, Fpm.Y, Dim, 1.5f);
			Line(Fpm.X + 7.f * S, Fpm.Y, Fpm.X + 18.f * S, Fpm.Y, Dim, 1.5f);
			Line(Fpm.X, Fpm.Y - 7.f * S, Fpm.X, Fpm.Y - 14.f * S, Dim, 1.5f);
		}
	}

	// Targets: brackets, a lead pip on each, lock state; arrows at the edge for those off screen.
	int32 Hostiles = 0;
	const AActor* Locked = GetLockedTarget();
	for (TActorIterator<ASoulsFighter> It(GetWorld()); It; ++It)
	{
		const ASoulsFighter* Foe = *It;
		if (!IsHostileTo(Foe))
		{
			continue;
		}
		++Hostiles;
		const FVector FoeLoc = Foe->GetActorLocation();
		const float Distance = FVector::Dist(FoeLoc, Loc);
		FVector2D P;
		const bool bLockedFoe = Foe == Locked;
		const bool bLocking = !bLockedFoe && LockCandidate.Get() == Foe && LockProgress > 0.f;
		const FLinearColor Col = bLockedFoe ? Amber : Red;
		if (PC->ProjectWorldLocationToScreen(FoeLoc, P, true) && P.X > 0.f && P.Y > 0.f && P.X < Canvas->ClipX && P.Y < Canvas->ClipY)
		{
			const float R = FMath::Clamp(9000.f / FMath::Max(Distance, 1.f), 0.6f, 3.f) * 16.f * S;
			const float C = R * 0.45f;
			for (const FVector2D Sign : { FVector2D(1.f, 1.f), FVector2D(-1.f, 1.f), FVector2D(1.f, -1.f), FVector2D(-1.f, -1.f) })
			{
				Line(P.X + Sign.X * R, P.Y + Sign.Y * R, P.X + Sign.X * (R - C), P.Y + Sign.Y * R, Col, 2.f);
				Line(P.X + Sign.X * R, P.Y + Sign.Y * R, P.X + Sign.X * R, P.Y + Sign.Y * (R - C), Col, 2.f);
			}
			Text(Font, FString::Printf(TEXT("%.0f m"), Distance / 100.f), P.X, P.Y + R + 4.f * S, Col, 0.85f, true);
			if (bLockedFoe)
			{
				Text(Font, TEXT("LOCKED"), P.X, P.Y - R - 22.f * S, Amber, 0.9f, true);
			}
			else if (bLocking)
			{
				Ring(P.X, P.Y, R + (1.f - LockProgress) * 60.f * S, Amber, 20, 1.5f);
			}
			// The lead pip: aim the reticle here to hit it.
			FVector2D L;
			if (Distance < 25000.f && PC->ProjectWorldLocationToScreen(LeadPoint(Foe, Loc, LaserSpeed), L, true))
			{
				const float D = 8.f * S;
				Line(L.X, L.Y - D, L.X + D, L.Y, Amber, 2.f);
				Line(L.X + D, L.Y, L.X, L.Y + D, Amber, 2.f);
				Line(L.X, L.Y + D, L.X - D, L.Y, Amber, 2.f);
				Line(L.X - D, L.Y, L.X, L.Y - D, Amber, 2.f);
				Line(P.X, P.Y, L.X, L.Y, FLinearColor(1.f, 0.72f, 0.2f, 0.25f), 1.f);
			}
		}
		else
		{
			const FVector Rel = CamRot.UnrotateVector(FoeLoc - CamLoc);
			const float Angle = FMath::Atan2(-Rel.Z, Rel.Y);
			const float Rx = Canvas->ClipX * 0.42f, Ry = Canvas->ClipY * 0.4f;
			const FVector2D Edge(Cx + FMath::Cos(Angle) * Rx, Cy + FMath::Sin(Angle) * Ry);
			const FVector2D Out(FMath::Cos(Angle), FMath::Sin(Angle));
			const FVector2D Across(-Out.Y, Out.X);
			const float T = 14.f * S;
			const FVector2D Tip = Edge + Out * T, A = Edge - Out * T * 0.4f + Across * T * 0.7f, B = Edge - Out * T * 0.4f - Across * T * 0.7f;
			Line(Tip.X, Tip.Y, A.X, A.Y, Col, 2.f);
			Line(A.X, A.Y, B.X, B.Y, Col, 2.f);
			Line(B.X, B.Y, Tip.X, Tip.Y, Col, 2.f);
		}
	}

	// Left: speed, throttle and boost.
	{
		const float X = 80.f * S, Top = Cy - 170.f * S, H = 340.f * S;
		Rect(X, Top, 20.f * S, H, FLinearColor(0.f, 0.f, 0.f, 0.35f));
		Rect(X, Top + H * (1.f - ThrottleSetting), 20.f * S, H * ThrottleSetting, bBoosting ? Amber : Cyan);
		Rect(X + 26.f * S, Top, 8.f * S, H, FLinearColor(0.f, 0.f, 0.f, 0.35f));
		Rect(X + 26.f * S, Top + H * (1.f - BoostEnergy), 8.f * S, H * BoostEnergy, BoostEnergy > 0.15f ? Amber : Red);
		Text(Font, TEXT("THR"), X + 10.f * S, Top - 26.f * S, Dim, 0.9f, true);
		Text(Big, FString::Printf(TEXT("%.0f"), FlightVelocity.Size() * 0.036f), X + 140.f * S, Cy - 16.f * S, bBoosting ? Amber : Cyan, 1.1f, true);
		Text(Font, TEXT("KM/H"), X + 140.f * S, Cy + 18.f * S, Dim, 0.9f, true);
		if (bBoosting)
		{
			Text(Font, TEXT("BOOST"), X + 140.f * S, Cy + 42.f * S, Amber, 1.f, true);
		}
		else if (bBrakeHeld)
		{
			Text(Font, TEXT("BRAKE"), X + 140.f * S, Cy + 42.f * S, Cyan, 1.f, true);
		}
	}
	// Right: altitude (over the ground and the sea) and climb rate.
	{
		const float X = Canvas->ClipX - 200.f * S;
		Text(Big, FString::Printf(TEXT("%.0f"), FMath::Max(GroundClearance, 0.f) / 100.f), X, Cy - 16.f * S, GroundClearance < 1500.f && bAirborne ? Amber : Cyan, 1.1f, true);
		Text(Font, TEXT("M AGL"), X, Cy + 18.f * S, Dim, 0.9f, true);
		Text(Font, FString::Printf(TEXT("ALT %.0f m   VS %+.0f m/s"), Loc.Z / 100.f, FlightVelocity.Z / 100.f), X, Cy + 42.f * S, Dim, 0.9f, true);
	}
	// Top: heading and roll.
	{
		float Heading = FMath::Fmod(FlightRotation.Rotator().Yaw + 360.f, 360.f);
		Text(Font, FString::Printf(TEXT("HDG %03.0f     ROLL %+04.0f"), Heading, GetRollAngle()), Cx, 40.f * S, Dim, 1.f, true);
	}
	// Bottom: hull, missiles, targets.
	{
		const float X = 60.f * S, Y = Canvas->ClipY - 90.f * S, W = 300.f * S;
		const float F = GetHullFraction();
		Text(Font, FString::Printf(TEXT("HULL  %d%%"), FMath::RoundToInt(F * 100.f)), X, Y - 24.f * S, F > 0.3f ? Cyan : Red, 1.f, false);
		Rect(X, Y, W, 12.f * S, FLinearColor(0.f, 0.f, 0.f, 0.4f));
		Rect(X, Y, W * F, 12.f * S, F > 0.3f ? Cyan : Red);
		const float RX = Canvas->ClipX - 340.f * S;
		Text(Font, FString::Printf(TEXT("MISSILES  %d%s"), Missiles, Locked ? TEXT("   LOCK") : TEXT("")), RX, Y - 24.f * S, Missiles > 0 ? (Locked ? Amber : Cyan) : Red, 1.f, false);
		Text(Font, FString::Printf(TEXT("HOSTILES  %d     KILLS %d"), Hostiles, Kills), RX, Y, Cyan, 1.f, false);
	}
	// Warnings and prompts.
	if (PullUpWarning > 0.5f && bAirborne && FMath::Fmod(Now, 0.4f) < 0.25f)
	{
		Text(Big, TEXT("PULL UP"), Cx, Cy + 120.f * S, Red, 1.5f, true);
	}
	FString Prompt;
	if (Flight == EFighterFlight::Grounded)
	{
		Prompt = TEXT("W / L-stick up / Shift  TAKE OFF          [F] / D-pad Up  CLIMB OUT");
	}
	else if (Flight == EFighterFlight::Lifting)
	{
		Prompt = TEXT("LIFT-OFF");
	}
	else if (Flight == EFighterFlight::Landing)
	{
		Prompt = TEXT("LANDING ...   (W / boost to abort)");
	}
	else if (GroundClearance < LandingWindow.Y && FlightSpeed < LandingWindow.X)
	{
		Prompt = TEXT("[F] / D-pad Up  LAND");
	}
	if (!Prompt.IsEmpty())
	{
		Text(Big, Prompt, Cx, Canvas->ClipY * 0.72f, Cyan, 1.f, true);
	}
	if (Now < FlashMessageUntil)
	{
		Text(Big, FlashMessage, Cx, Canvas->ClipY * 0.72f + 40.f * S, Amber, 1.f, true);
	}
	// Hit warning: a red bar on the side the shot came from.
	if (Now - LastHitTime < 0.6f)
	{
		const float A = 1.f - (Now - LastHitTime) / 0.6f;
		const FVector Rel = CamRot.UnrotateVector(LastHitFrom - CamLoc);
		const FLinearColor Col(1.f, 0.1f, 0.05f, 0.55f * A);
		if (Rel.X < 0.f && FMath::Abs(Rel.Y) < -Rel.X)
		{
			Rect(0.f, Canvas->ClipY - 16.f * S, Canvas->ClipX, 16.f * S, Col);
		}
		else if (FMath::Abs(Rel.Y) > FMath::Abs(Rel.Z))
		{
			Rect(Rel.Y < 0.f ? 0.f : Canvas->ClipX - 16.f * S, 0.f, 16.f * S, Canvas->ClipY, Col);
		}
		else
		{
			Rect(0.f, Rel.Z > 0.f ? 0.f : Canvas->ClipY - 16.f * S, Canvas->ClipX, 16.f * S, Col);
		}
	}
	Text(Font, TEXT("Mouse / R-stick pitch+yaw   A D / L-stick roll   W S throttle   Shift/Space / A boost   Ctrl / LT brake   LMB / RT fire   RMB / Y missile   V / R3 view"),
		Cx, Canvas->ClipY - 40.f * S, Dim, 0.8f, true);
}

// ---------------------------------------------------------------------------------------------
// -FighterDemo
// ---------------------------------------------------------------------------------------------

bool UFighterDemoSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	return Super::ShouldCreateSubsystem(Outer) && FParse::Param(FCommandLine::Get(), TEXT("FighterDemo"));
}

void UFighterDemoSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (!InWorld.IsGameWorld())
	{
		return;
	}
	TWeakObjectPtr<UFighterDemoSubsystem> WeakThis(this);
	FTimerHandle Start;
	InWorld.GetTimerManager().SetTimer(Start, [WeakThis]() { if (WeakThis.IsValid()) { WeakThis->Run(); } }, 4.f, false);
}

void UFighterDemoSubsystem::Check(const TCHAR* What, bool bPass, const FString& Detail)
{
	(bPass ? Passes : Fails) += 1;
	UE_LOG(LogUnrealSoulslike, Display, TEXT("FighterDemo: %s %s (%s)"), What, bPass ? TEXT("PASS") : TEXT("FAIL"), *Detail);
}

void UFighterDemoSubsystem::Key(const FKey& InKey, bool bDown)
{
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		PC->InputKey(FInputKeyEventArgs::CreateSimulated(InKey, bDown ? IE_Pressed : IE_Released, bDown ? 1.f : 0.f));
	}
}

void UFighterDemoSubsystem::Run()
{
	UWorld* World = GetWorld();
	GEngine->Exec(World, TEXT("r.MotionBlurQuality 0"));
	ASoulsKnight* Player = Cast<ASoulsKnight>(UGameplayStatics::GetPlayerPawn(World, 0));
	if (!Player)
	{
		Check(TEXT("a knight to fly with"), false, TEXT("the player pawn is not an ASoulsKnight"));
		FPlatformMisc::RequestExit(false, TEXT("FighterDemo"));
		return;
	}
	DemoKnight = Player;
	ASoulsFighter* Found = nullptr;
	for (TActorIterator<ASoulsFighter> It(World); It; ++It)
	{
		if (!It->IsDrone() && (!Found || It->GetDistanceTo(Player) < Found->GetDistanceTo(Player)))
		{
			Found = *It;
		}
	}
	if (!Found)
	{
		// No fighter on this map: set one down on the ground ahead of the knight.
		FVector At = Player->GetActorLocation() + Player->GetActorForwardVector().GetSafeNormal2D() * 1600.f;
		FHitResult Hit;
		if (World->LineTraceSingleByChannel(Hit, At + FVector(0.f, 0.f, 3000.f), At - FVector(0.f, 0.f, 6000.f), ECC_Visibility))
		{
			At = Hit.ImpactPoint;
		}
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Found = World->SpawnActor<ASoulsFighter>(ASoulsFighter::StaticClass(), At + FVector(0.f, 0.f, 300.f), FRotator(0.f, Player->GetActorRotation().Yaw, 0.f), Params);
		UE_LOG(LogUnrealSoulslike, Display, TEXT("FighterDemo: no fighter on the map, spawned one at %s"), Found ? *Found->GetActorLocation().ToCompactString() : TEXT("(failed)"));
	}
	Check(TEXT("a star fighter is parked on the map"), Found != nullptr, Found ? FString::Printf(TEXT("%s at %s"), *Found->GetName(), *Found->GetActorLocation().ToCompactString()) : FString(TEXT("none")));
	if (!Found)
	{
		FPlatformMisc::RequestExit(false, TEXT("FighterDemo"));
		return;
	}
	DemoFighter = Found;

	struct FStep { float Time; TFunction<void(UFighterDemoSubsystem*)> Do; };
	auto Shot = [](const TCHAR* Name) { FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("Fighter_%s"), Name), true, false); };
	auto GroundZ = [](UWorld* W, const FVector& At) -> float
	{
		FHitResult Hit;
		FCollisionObjectQueryParams Objects;
		Objects.AddObjectTypesToQuery(ECC_WorldStatic);
		return W->LineTraceSingleByObjectType(Hit, FVector(At.X, At.Y, 60000.f), FVector(At.X, At.Y, -20000.f), Objects) ? FMath::Max(Hit.ImpactPoint.Z, 0.f) : 0.f;
	};
	auto NearestDrone = [](UWorld* W, const FVector& From) -> ASoulsFighter*
	{
		ASoulsFighter* Best = nullptr;
		for (TActorIterator<ASoulsFighter> It(W); It; ++It)
		{
			if (It->IsDrone() && It->IsAlive() && (!Best || FVector::Dist(It->GetActorLocation(), From) < FVector::Dist(Best->GetActorLocation(), From)))
			{
				Best = *It;
			}
		}
		return Best;
	};
	// Put a drone dead ahead of the fighter, flying the same way slower, and hold its course.
	auto StageDrone = [NearestDrone](UFighterDemoSubsystem* D, float Ahead) -> ASoulsFighter*
	{
		ASoulsFighter* F = D->Fighter();
		ASoulsFighter* Drone = NearestDrone(D->GetWorld(), F->GetActorLocation());
		if (Drone)
		{
			const FVector Fwd = F->GetActorForwardVector().GetSafeNormal2D();
			Drone->PlaceInFlight(F->GetActorLocation() + Fwd * Ahead + FVector(0.f, 0.f, 150.f), Fwd.Rotation().Yaw, 1200.f);
			Drone->HoldCourse(6.f);
			F->PlaceInFlight(F->GetActorLocation(), Fwd.Rotation().Yaw, 1500.f);
			F->FaceToward(Drone->GetActorLocation());
		}
		D->DemoDrone = Drone;
		return Drone;
	};

	const TArray<FStep> Steps = {
		// 1. The knight beside the parked fighter.
		{ 0.f, [](UFighterDemoSubsystem* D)
			{
				ASoulsFighter* F = D->Fighter();
				ASoulsKnight* K = D->Knight();
				const FVector Spot = F->GetActorLocation() - F->GetActorRightVector() * 470.f + F->GetActorForwardVector() * 100.f;
				FHitResult Hit;
				FVector P = Spot;
				FCollisionQueryParams Params(SCENE_QUERY_STAT(FighterDemoPlace), false, K);
				Params.AddIgnoredActor(F);
				if (D->GetWorld()->LineTraceSingleByChannel(Hit, Spot + FVector(0.f, 0.f, 1500.f), Spot - FVector(0.f, 0.f, 3000.f), ECC_Visibility, Params))
				{
					P = Hit.ImpactPoint;
				}
				K->SetActorLocation(P + FVector(0.f, 0.f, K->GetSimpleCollisionHalfHeight() + 5.f), false, nullptr, ETeleportType::TeleportPhysics);
				K->SetActorRotation(FRotator(0.f, (F->GetActorLocation() - K->GetActorLocation()).Rotation().Yaw, 0.f));
				if (AController* C = K->GetController())
				{
					C->SetControlRotation(FRotator(-10.f, (F->GetActorLocation() - K->GetActorLocation()).Rotation().Yaw + 30.f, 0.f));
				}
			} },
		{ 2.5f, [Shot](UFighterDemoSubsystem* D)
			{
				D->Check(TEXT("the knight can board from beside it"), ASoulsFighter::FindBoardable(D->Knight()) == D->Fighter(),
					FString::Printf(TEXT("distance %.0f cm"), FVector::Dist2D(D->Knight()->GetActorLocation(), D->Fighter()->GetActorLocation())));
				Shot(TEXT("01_parked"));
			} },
		// 2. Interact: climb in (the hook ASoulsKnight::InteractPressed calls).
		{ 3.f, [](UFighterDemoSubsystem* D) { D->Check(TEXT("interact starts boarding"), ASoulsFighter::TryBoard(D->Knight()), TEXT("TryBoard")); } },
		{ 3.9f, [Shot](UFighterDemoSubsystem* D)
			{
				D->Check(TEXT("the canopy opens"), D->Fighter()->GetCanopyAlpha() > 0.5f, FString::Printf(TEXT("canopy %.2f"), D->Fighter()->GetCanopyAlpha()));
				Shot(TEXT("02_boarding"));
			} },
		{ 6.f, [Shot](UFighterDemoSubsystem* D)
			{
				ASoulsFighter* F = D->Fighter();
				APlayerController* PC = D->GetWorld()->GetFirstPlayerController();
				D->Check(TEXT("boarded: the player flies the fighter, the knight is aboard and hidden"),
					F->IsPiloted() && PC && PC->GetPawn() == F && D->Knight()->IsHidden() && F->GetPilot() == D->Knight() && F->GetFlight() == EFighterFlight::Grounded,
					FString::Printf(TEXT("state=%d flight=%d pawn=%s hidden=%d"), (int32)F->GetState(), (int32)F->GetFlight(), PC && PC->GetPawn() ? *PC->GetPawn()->GetName() : TEXT("none"), D->Knight()->IsHidden()));
				D->StartLocation = F->GetActorLocation();
				Shot(TEXT("03_aboard"));
			} },
		// 3. Take off: W (through the real mapping) lifts it straight up, legs folding, then it flies off.
		{ 6.2f, [](UFighterDemoSubsystem* D) { D->Key(EKeys::W, true); } },
		{ 6.7f, [](UFighterDemoSubsystem* D)
			{
				D->Key(EKeys::W, false);
				D->Check(TEXT("W starts a vertical take-off"), D->Fighter()->GetFlight() == EFighterFlight::Lifting, FString::Printf(TEXT("flight=%d"), (int32)D->Fighter()->GetFlight()));
			} },
		{ 7.7f, [Shot](UFighterDemoSubsystem* D) { Shot(TEXT("04_liftoff")); } },
		{ 9.3f, [](UFighterDemoSubsystem* D)
			{
				ASoulsFighter* F = D->Fighter();
				const float Rise = F->GetActorLocation().Z - D->StartLocation.Z;
				D->Check(TEXT("lifted vertically, legs up, now flying"), F->GetFlight() == EFighterFlight::Flying && Rise > 700.f && F->GetGearAlpha() < 0.5f,
					FString::Printf(TEXT("flight=%d rose %.0f cm, gear %.2f"), (int32)F->GetFlight(), Rise, F->GetGearAlpha()));
				D->StartLocation = F->GetActorLocation();
				D->Key(EKeys::W, true); // throttle up
			} },
		{ 10.4f, [](UFighterDemoSubsystem* D) { D->Key(EKeys::W, false); D->Fighter()->SetStickInput(0.35f * (D->Fighter()->bInvertPitch ? -1.f : 1.f), 0.f); } }, // climb
		{ 12.4f, [Shot](UFighterDemoSubsystem* D)
			{
				ASoulsFighter* F = D->Fighter();
				F->SetStickInput(0.f, 0.f);
				const float Moved = FVector::Dist(F->GetActorLocation(), D->StartLocation);
				D->Check(TEXT("throttle flies it forward and climbs"), F->GetForwardSpeed() > 2000.f && Moved > 2500.f && F->GetActorLocation().Z > D->StartLocation.Z,
					FString::Printf(TEXT("speed %.0f cm/s, throttle %.0f%%, moved %.0f cm"), F->GetForwardSpeed(), F->GetThrottle() * 100.f, Moved));
				Shot(TEXT("05_flying"));
			} },
		// 4. Full roll: up to safe altitude, hold D through a barrel roll past inverted.
		{ 12.6f, [GroundZ](UFighterDemoSubsystem* D)
			{
				ASoulsFighter* F = D->Fighter();
				const FVector L = F->GetActorLocation();
				const float Z = FMath::Max(L.Z, GroundZ(D->GetWorld(), L) + 8000.f);
				F->PlaceInFlight(FVector(L.X, L.Y, Z), F->GetActorRotation().Yaw, 3000.f);
				D->MinUpZ = 1.f;
			} },
		{ 13.f, [](UFighterDemoSubsystem* D) { D->Key(EKeys::D, true); } },
		{ 13.75f, [Shot](UFighterDemoSubsystem* D) { Shot(TEXT("06_rolling")); } },
		{ 14.f, [](UFighterDemoSubsystem* D)
			{
				D->Key(EKeys::D, false);
				D->Check(TEXT("full free roll: flew inverted"), D->MinUpZ < -0.7f, FString::Printf(TEXT("lowest up.z %.2f, roll now %.0f deg"), D->MinUpZ, D->Fighter()->GetRollAngle()));
				D->StartValue = FMath::Abs(D->Fighter()->GetRollAngle());
			} },
		{ 14.15f, [Shot](UFighterDemoSubsystem* D) { Shot(TEXT("07_released")); } },
		{ 16.8f, [Shot](UFighterDemoSubsystem* D)
			{
				const float Roll = D->Fighter()->GetRollAngle();
				D->Check(TEXT("released and not firing: rolls back level with the horizon"), FMath::Abs(Roll) < 8.f,
					FString::Printf(TEXT("roll %.0f deg at release -> %.1f deg"), D->StartValue, Roll));
				Shot(TEXT("08_autolevel"));
			} },
		// 5. Roll to a bank, then fire: no auto-level while the guns are going; it comes back after.
		{ 17.f, [](UFighterDemoSubsystem* D) { D->Key(EKeys::D, true); } },
		{ 17.4f, [](UFighterDemoSubsystem* D) { D->Key(EKeys::D, false); D->Key(EKeys::LeftMouseButton, true); D->StartValue = static_cast<float>(D->Fighter()->GetBoltsFired()); } },
		{ 18.0f, [Shot](UFighterDemoSubsystem* D) { Shot(TEXT("09_firing_banked")); } },
		{ 18.7f, [](UFighterDemoSubsystem* D)
			{
				ASoulsFighter* F = D->Fighter();
				const float Roll = F->GetRollAngle();
				D->Check(TEXT("firing holds the bank (no auto-level while shooting)"), FMath::Abs(Roll) > 45.f, FString::Printf(TEXT("roll %.0f deg after 1.3 s of fire"), Roll));
				D->Check(TEXT("LMB fires alternating twin lasers"), F->GetBoltsFired() - static_cast<int32>(D->StartValue) >= 8,
					FString::Printf(TEXT("%d bolts"), F->GetBoltsFired() - static_cast<int32>(D->StartValue)));
				D->Key(EKeys::LeftMouseButton, false);
			} },
		{ 21.f, [](UFighterDemoSubsystem* D)
			{
				const float Roll = D->Fighter()->GetRollAngle();
				D->Check(TEXT("fire released: the auto-level comes back"), FMath::Abs(Roll) < 8.f, FString::Printf(TEXT("roll %.1f deg"), Roll));
			} },
		// 6. Dogfight: a drone dead ahead; RT (through the mapping) shoots it down.
		{ 21.2f, [StageDrone](UFighterDemoSubsystem* D)
			{
				ASoulsFighter* Drone = StageDrone(D, 5000.f);
				D->Check(TEXT("drone fighters are up"), Drone != nullptr, Drone ? Drone->GetName() : FString(TEXT("none")));
				D->HitsBefore = D->Fighter()->GetHitsLanded();
			} },
		{ 21.5f, [](UFighterDemoSubsystem* D) { D->Key(EKeys::Gamepad_RightTrigger, true); } },
		{ 21.9f, [Shot](UFighterDemoSubsystem* D) { Shot(TEXT("10_dogfight")); } },
		{ 22.6f, [Shot](UFighterDemoSubsystem* D) { Shot(TEXT("11_drone_hit")); } },
		{ 24.f, [](UFighterDemoSubsystem* D)
			{
				D->Key(EKeys::Gamepad_RightTrigger, false);
				ASoulsFighter* F = D->Fighter();
				const ASoulsFighter* Drone = D->DemoDrone.Get();
				D->Check(TEXT("lasers hit and shoot down the drone"), F->GetHitsLanded() > D->HitsBefore && Drone && (!Drone->IsAlive() || Drone->GetHullFraction() < 1.f),
					FString::Printf(TEXT("hits %d -> %d, drone hull %.0f%%, kills %d"), D->HitsBefore, F->GetHitsLanded(), Drone ? Drone->GetHullFraction() * 100.f : -1.f, F->GetKills()));
			} },
		// 7. A missile: lock on (hold the drone in the cone) and fire with Y.
		{ 24.3f, [StageDrone](UFighterDemoSubsystem* D) { StageDrone(D, 6500.f); } },
		{ 25.6f, [](UFighterDemoSubsystem* D)
			{
				ASoulsFighter* F = D->Fighter();
				D->Check(TEXT("missile lock acquired"), F->GetLockedTarget() != nullptr, F->GetLockedTarget() ? F->GetLockedTarget()->GetName() : FString(TEXT("none")));
				D->HitsBefore = F->GetHitsLanded();
				D->Key(EKeys::Gamepad_FaceButton_Top, true);
			} },
		{ 25.7f, [](UFighterDemoSubsystem* D) { D->Key(EKeys::Gamepad_FaceButton_Top, false); } },
		{ 26.1f, [Shot](UFighterDemoSubsystem* D) { Shot(TEXT("12_missile")); } },
		{ 27.6f, [](UFighterDemoSubsystem* D)
			{
				ASoulsFighter* F = D->Fighter();
				D->Check(TEXT("Y fires a homing missile that strikes"), F->GetMissilesFired() >= 1 && F->GetHitsLanded() > D->HitsBefore,
					FString::Printf(TEXT("%d missiles, hits %d -> %d"), F->GetMissilesFired(), D->HitsBefore, F->GetHitsLanded()));
			} },
		// 8. Terrain: dive into the ground at speed; it scrapes, bounces off and survives.
		{ 27.8f, [GroundZ](UFighterDemoSubsystem* D)
			{
				ASoulsFighter* F = D->Fighter();
				const FTransform Home = F->GetHomeTransform();
				const FVector At = Home.GetLocation() + Home.GetRotation().GetForwardVector() * -3000.f;
				F->PlaceInFlight(FVector(At.X, At.Y, GroundZ(D->GetWorld(), At) + 900.f), Home.GetRotation().Rotator().Yaw, 2600.f);
				F->SetStickInput(F->bInvertPitch ? 1.f : -1.f, 0.f); // dive
				D->HitsBefore = F->GetScrapes();
				D->StartValue = F->GetHull();
			} },
		{ 28.6f, [Shot](UFighterDemoSubsystem* D) { Shot(TEXT("13_scrape")); D->Fighter()->SetStickInput(0.f, 0.f); } },
		{ 29.4f, [](UFighterDemoSubsystem* D)
			{
				ASoulsFighter* F = D->Fighter();
				D->Check(TEXT("hitting the terrain scrapes, damages and bounces"), F->GetScrapes() > D->HitsBefore && F->GetHull() < D->StartValue && F->IsAlive()
					&& F->GetAltitudeAboveGround() > -50.f,
					FString::Printf(TEXT("scrapes %d -> %d, hull %.0f -> %.0f, clearance %.0f"), D->HitsBefore, F->GetScrapes(), D->StartValue, F->GetHull(), F->GetAltitudeAboveGround()));
			} },
		// 9. Land: low and slow over the pad, F (through the mapping) settles it on its legs.
		{ 29.6f, [](UFighterDemoSubsystem* D)
			{
				ASoulsFighter* F = D->Fighter();
				const FTransform Home = F->GetHomeTransform();
				F->PlaceInFlight(Home.GetLocation() + FVector(0.f, 0.f, 1300.f), Home.GetRotation().Rotator().Yaw, 500.f);
			} },
		{ 29.9f, [](UFighterDemoSubsystem* D) { D->Key(EKeys::F, true); } },
		{ 30.f, [](UFighterDemoSubsystem* D)
			{
				D->Key(EKeys::F, false);
				D->Check(TEXT("F low and slow starts the landing"), D->Fighter()->GetFlight() == EFighterFlight::Landing, FString::Printf(TEXT("flight=%d"), (int32)D->Fighter()->GetFlight()));
			} },
		{ 31.f, [Shot](UFighterDemoSubsystem* D) { Shot(TEXT("14_landing")); } },
		{ 36.f, [Shot](UFighterDemoSubsystem* D)
			{
				ASoulsFighter* F = D->Fighter();
				D->Check(TEXT("landed: settled on its legs"), F->GetFlight() == EFighterFlight::Grounded && F->GetGearAlpha() > 0.95f && FMath::Abs(F->GetAltitudeAboveGround()) < 60.f,
					FString::Printf(TEXT("flight=%d gear %.2f clearance %.0f cm"), (int32)F->GetFlight(), F->GetGearAlpha(), F->GetAltitudeAboveGround()));
				Shot(TEXT("15_landed"));
			} },
		// 10. Climb out (D-pad up through the mapping) and get the knight's own controls back.
		{ 36.4f, [](UFighterDemoSubsystem* D) { D->Key(EKeys::Gamepad_DPad_Up, true); } },
		{ 36.5f, [](UFighterDemoSubsystem* D) { D->Key(EKeys::Gamepad_DPad_Up, false); } },
		{ 37.3f, [Shot](UFighterDemoSubsystem* D) { Shot(TEXT("16_climbing_out")); } },
		{ 39.f, [Shot](UFighterDemoSubsystem* D)
			{
				ASoulsFighter* F = D->Fighter();
				ASoulsKnight* K = D->Knight();
				APlayerController* PC = D->GetWorld()->GetFirstPlayerController();
				const float Away = FVector::Dist2D(K->GetActorLocation(), F->GetActorLocation());
				D->Check(TEXT("climbed out: the knight is back, visible, beside the fighter"),
					PC && PC->GetPawn() == K && !K->IsHidden() && F->GetState() == EFighterState::Parked && Away > 250.f && Away < 1000.f,
					FString::Printf(TEXT("pawn=%s hidden=%d state=%d %.0f cm away"), PC && PC->GetPawn() ? *PC->GetPawn()->GetName() : TEXT("none"), K->IsHidden(), (int32)F->GetState(), Away));
				Shot(TEXT("17_disembarked"));
			} },
		// A moment for the knight's controls to come back, then A must jump.
		{ 39.75f, [](UFighterDemoSubsystem* D) { D->Key(EKeys::Gamepad_FaceButton_Bottom, true); } },
		{ 40.50f, [](UFighterDemoSubsystem* D)
			{
				ASoulsKnight* K = D->Knight();
				D->Check(TEXT("the knight's controls are back (A jumps)"), K->GetCharacterMovement()->IsFalling(), FString::Printf(TEXT("vz=%.0f"), K->GetVelocity().Z));
				D->Key(EKeys::Gamepad_FaceButton_Bottom, false);
			} },
		{ 41.f, [](UFighterDemoSubsystem* D)
			{
				UE_LOG(LogUnrealSoulslike, Display, TEXT("FighterDemo: %d passed, %d failed"), D->Passes, D->Fails);
				UE_LOG(LogUnrealSoulslike, Display, TEXT("FIGHTER_DEMO_DONE"));
				FPlatformMisc::RequestExit(false, TEXT("FighterDemo"));
			} },
	};
	const TWeakObjectPtr<UFighterDemoSubsystem> WeakThis(this);
	for (const FStep& Step : Steps)
	{
		const TFunction<void(UFighterDemoSubsystem*)> Do = Step.Do;
		if (Step.Time <= 0.f)
		{
			Do(this);
			continue;
		}
		FTimerHandle Handle;
		World->GetTimerManager().SetTimer(Handle, [WeakThis, Do]()
		{
			if (WeakThis.IsValid() && WeakThis->Fighter() && WeakThis->Knight())
			{
				Do(WeakThis.Get());
			}
		}, Step.Time, false);
	}
	// A watcher: the lowest the ship's up vector got (inverted flight), and keeping the guns on a staged drone.
	World->GetTimerManager().SetTimer(WatchHandle, [WeakThis]()
	{
		if (!WeakThis.IsValid() || !WeakThis->Fighter())
		{
			return;
		}
		UFighterDemoSubsystem* D = WeakThis.Get();
		ASoulsFighter* F = D->Fighter();
		D->MinUpZ = FMath::Min(D->MinUpZ, F->GetActorUpVector().Z);
		const ASoulsFighter* Drone = D->DemoDrone.Get();
		if (Drone && Drone->IsAlive() && F->GetFlight() == EFighterFlight::Flying && FVector::Dist(Drone->GetActorLocation(), F->GetActorLocation()) < 9000.f
			&& FVector::DotProduct(F->GetActorForwardVector(), (Drone->GetActorLocation() - F->GetActorLocation()).GetSafeNormal()) > 0.9f)
		{
			F->FaceToward(Drone->GetActorLocation());
		}
	}, 0.05f, true);
}
