// UNR-T40 Torches: carried by some enemies, burning in wall brackets, wielded, swung, dropped and thrown.
#include "SoulsTorch.h"
#include "SoulsEnemy.h"
#include "SoulsGameMode.h"
#include "SoulsKnight.h"
#include "SoulsSparks.h"
#include "UnrealSoulslike.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "CollisionQueryParams.h"
#include "CombatDamageable.h"
#include "Components/PointLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DamageEvents.h"
#include "Engine/Engine.h"
#include "Engine/OverlapResult.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/TargetPoint.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PhysicsVolume.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/WorldSettings.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "TimerManager.h"
#include "UnrealClient.h"
#include "UObject/ConstructorHelpers.h"

// ---------------------------------------------------------------------------------------------
// UNR-T41 HOOK (field fire). SoulsFire.h belongs to the field-fire ticket; until it exists the torch
// only lights knights. Once it lands this picks it up by itself (touch this file so it recompiles).
// ---------------------------------------------------------------------------------------------
#if __has_include("SoulsFire.h")
#include "SoulsFire.h"
#define SOULS_TORCH_HAS_FIELD_FIRE 1
#else
#define SOULS_TORCH_HAS_FIELD_FIRE 0
#endif

namespace
{
	const FName TorchHandR(TEXT("hand_r"));
	const FName TorchHandL(TEXT("hand_l"));
	const FName TorchMiddleL(TEXT("middle_01_l"));
	const FName TorchIndexL(TEXT("index_01_l"));
	const FName TorchPinkyL(TEXT("pinky_01_l"));
	const FName TorchBurnBone(TEXT("spine_03"));
	const FName TorchWallTag(TEXT("Torch"));

	constexpr float TorchLength = 72.f;           // cm, butt to the top of the head
	constexpr float TorchGripFromButt = 20.f;      // where the fist closes on the shaft
	constexpr float TorchLightIntensity = 6000.f;
	constexpr float TorchLightRadius = 1100.f;
	constexpr float TorchBurnSeconds = 240.f;      // off the wall and out of an enemy's hand
	constexpr float TorchSwingReach = 175.f;
	constexpr float TorchSwingDamageScale = 0.6f;  // of the wielder's LightDamage
	constexpr float TorchThrowDamage = 18.f;
	constexpr float TorchThrowSpeed = 1500.f;      // horizontal cm/s
	constexpr float TorchThrowRange = 1300.f;      // straight ahead when nothing is locked
	constexpr float TorchGravity = 980.f;
	constexpr float TorchFlightRadius = 9.f;
	constexpr float TorchSplashRadius = 150.f;
	constexpr float TorchBurnPerSecond = 3.5f;
	constexpr float TorchBurnLight = 4200.f;
	constexpr float TorchBearerPercent = 45.f;     // share of ordinary island knights carrying one
	const FLinearColor TorchFireColor(1.f, 0.52f, 0.2f);
	constexpr float TorchBurnFxScale = 1.7f;       // a burning knight's fire: the ground fire, spread wider

	// UNR-T51 fire, built by tools/build_torch_fire.py from the CC0 Kenney particle sprites.
	const TCHAR* const TorchFlameFxPath = TEXT("/Game/Souls/FX/Fire/NS_TorchFlame.NS_TorchFlame");
	const TCHAR* const TorchGroundFlameFxPath = TEXT("/Game/Souls/FX/Fire/NS_TorchFlameGround.NS_TorchFlameGround");
	const TCHAR* const TorchEmberFxPath = TEXT("/Game/Souls/FX/Fire/NS_TorchEmbers.NS_TorchEmbers");
	const TCHAR* const TorchSmokeFxPath = TEXT("/Game/Souls/FX/Fire/NS_TorchSmoke.NS_TorchSmoke");

	// Starts (from fresh) or lets die out (live particles finish) one of the torch's fire effects.
	void TorchSetFx(UNiagaraComponent* Fx, bool bOn)
	{
		if (!Fx || !Fx->GetAsset())
		{
			return;
		}
		if (bOn)
		{
			Fx->Activate(true);
		}
		else
		{
			Fx->Deactivate();
		}
	}

	// 0.7..1.2: a fire's uneven breathing.
	float TorchFlickerScale(float Phase)
	{
		return 0.92f + 0.12f * FMath::PerlinNoise1D(Phase * 5.3f) + 0.07f * FMath::Sin(Phase * 17.f) + 0.05f * FMath::Sin(Phase * 31.7f + 1.3f);
	}

	bool TorchInWater(const UWorld* World, const FVector& Point)
	{
		if (!World)
		{
			return false;
		}
		for (auto It = World->GetNonDefaultPhysicsVolumeIterator(); It; ++It)
		{
			const APhysicsVolume* Volume = It->Get();
			if (Volume && Volume->bWaterVolume && Volume->EncompassesPoint(Point))
			{
				return true;
			}
		}
		return false;
	}

	// Ground under Point (or Point itself when nothing is there).
	FVector TorchGroundBelow(const UWorld* World, const FVector& Point, const AActor* Ignore)
	{
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(TorchGround), false, Ignore);
		if (World && World->LineTraceSingleByChannel(Hit, Point + FVector(0.f, 0.f, 50.f), Point - FVector(0.f, 0.f, 400.f), ECC_Visibility, Params))
		{
			return Hit.ImpactPoint;
		}
		return Point;
	}

	// UNR-T41 HOOK: set the field alight where a torch lands or is swung.
	void TorchIgniteWorld(UObject* WorldContext, const FVector& Location, float Radius)
	{
#if SOULS_TORCH_HAS_FIELD_FIRE
		FSoulsFire::IgniteAt(WorldContext, Location, Radius);
#else
		(void)WorldContext;
		(void)Location;
		(void)Radius;
#endif
	}

	// Burning takes health without a flinch through ASoulsKnight::TakeBurnDamage (a one-line hook in
	// SoulsKnight.h). Without that hook the burn is dealt as one ordinary hit when it starts.
	template <typename TKnight>
	constexpr bool TorchHasBurnHook()
	{
		return requires(TKnight* Knight, AActor* Source) { Knight->TakeBurnDamage(1.f, Source); };
	}

	template <typename TKnight>
	void TorchBurnHealth(TKnight* Victim, float Amount, AActor* Source)
	{
		if constexpr (TorchHasBurnHook<TKnight>())
		{
			Victim->TakeBurnDamage(Amount, Source);
		}
		else
		{
			FDamageEvent Event;
			Victim->TakeDamage(Amount, Event, nullptr, Source);
		}
	}
}

// ---------------------------------------------------------------------------------------------
// Flames
// ---------------------------------------------------------------------------------------------

void FSoulsFlames::Build(AActor* Owner, USceneComponent* Parent, FName Socket, float Size)
{
	const ASoulsTorch* Defaults = GetDefault<ASoulsTorch>();
	if (!Owner || !Parent || !Defaults->SphereMesh)
	{
		return;
	}
	Anchor = NewObject<USceneComponent>(Owner);
	Anchor->SetupAttachment(Parent, Socket);
	Anchor->SetUsingAbsoluteRotation(true);
	Anchor->RegisterComponent();
	Anchor->SetWorldRotation(FRotator::ZeroRotator);

	struct FPart { FVector Offset; FVector Scale; bool bCore; };
	// A hot core and three tongues of different heights (unit sphere is 100 cm across).
	const FPart Layout[] = {
		{ FVector(0.f, 0.f, 3.f), FVector(0.05f, 0.05f, 0.09f), true },
		{ FVector(0.f, 0.f, 9.f), FVector(0.10f, 0.10f, 0.22f), false },
		{ FVector(2.5f, -2.f, 12.f), FVector(0.07f, 0.07f, 0.17f), false },
		{ FVector(-2.5f, 2.f, 11.f), FVector(0.065f, 0.065f, 0.15f), false },
	};
	Phase = FMath::FRand() * 10.f;
	for (const FPart& Part : Layout)
	{
		UStaticMeshComponent* Blob = NewObject<UStaticMeshComponent>(Owner);
		Blob->SetStaticMesh(Defaults->SphereMesh);
		if (UMaterialInterface* Material = Part.bCore ? Defaults->FlameCoreMaterial.Get() : Defaults->FlameMaterial.Get())
		{
			Blob->SetMaterial(0, Material);
		}
		Blob->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Blob->SetGenerateOverlapEvents(false);
		Blob->SetCastShadow(false);
		Blob->SetupAttachment(Anchor);
		Blob->SetRelativeLocation(Part.Offset * Size);
		Blob->SetRelativeScale3D(Part.Scale * Size);
		Blob->RegisterComponent();
		Parts.Add(Blob);
		BaseScale.Add(Part.Scale * Size);
		BaseOffset.Add(Part.Offset * Size);
	}
}

void FSoulsFlames::Flicker(float DeltaSeconds, const FVector& Lean)
{
	Phase += DeltaSeconds;
	for (int32 Index = 0; Index < Parts.Num(); ++Index)
	{
		UStaticMeshComponent* Blob = Parts[Index];
		if (!Blob || !BaseScale.IsValidIndex(Index))
		{
			continue;
		}
		const float Seed = 1.7f * Index;
		const float N1 = FMath::PerlinNoise1D(Phase * 7.f + Seed * 13.f);
		const float N2 = FMath::PerlinNoise1D(Phase * 11.f + Seed * 5.f);
		const float Up = 1.f + 0.4f * N1 + 0.12f * FMath::Sin(Phase * 23.f + Seed);
		const float Wide = 1.f + 0.15f * N2;
		const FVector& Base = BaseScale[Index];
		Blob->SetRelativeScale3D(FVector(Base.X * Wide, Base.Y * Wide, Base.Z * FMath::Max(Up, 0.3f)));
		// The taller tongues trail further.
		const FVector& Offset = BaseOffset[Index];
		const float Trail = FMath::Clamp(Offset.Z / 12.f, 0.2f, 1.2f);
		Blob->SetRelativeLocation(Offset + FVector(N2, N1, 0.f) * 1.2f + Lean * Trail);
	}
}

void FSoulsFlames::SetVisible(bool bVisible)
{
	for (UStaticMeshComponent* Blob : Parts)
	{
		if (Blob)
		{
			Blob->SetVisibility(bVisible);
		}
	}
}

void FSoulsFlames::DestroyParts()
{
	for (UStaticMeshComponent* Blob : Parts)
	{
		if (Blob)
		{
			Blob->DestroyComponent();
		}
	}
	Parts.Reset();
	BaseScale.Reset();
	BaseOffset.Reset();
	if (Anchor)
	{
		Anchor->DestroyComponent();
		Anchor = nullptr;
	}
}

// ---------------------------------------------------------------------------------------------
// The torch
// ---------------------------------------------------------------------------------------------

ASoulsTorch::ASoulsTorch()
{
	PrimaryActorTick.bCanEverTick = true;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderFinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereFinder(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> CoreFinder(TEXT("/Game/Souls/FX/M_GraceGlow.M_GraceGlow"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> FlameFinder(TEXT("/Game/Souls/FX/M_GraceBeam.M_GraceBeam"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> IronFinder(TEXT("/Game/Souls/FX/M_GrappleLine.M_GrappleLine"));
	CylinderMesh = CylinderFinder.Object;
	SphereMesh = SphereFinder.Object;
	CubeMesh = CubeFinder.Object;
	FlameCoreMaterial = CoreFinder.Object;
	FlameMaterial = FlameFinder.Object ? FlameFinder.Object : CoreFinder.Object;
	IronMaterial = IronFinder.Object;
	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> FlameFxFinder(TorchFlameFxPath);
	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> GroundFlameFxFinder(TorchGroundFlameFxPath);
	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> EmberFxFinder(TorchEmberFxPath);
	static ConstructorHelpers::FObjectFinder<UNiagaraSystem> SmokeFxFinder(TorchSmokeFxPath);
	FlameSystem = FlameFxFinder.Object;
	GroundFlameSystem = GroundFlameFxFinder.Object;
	EmberSystem = EmberFxFinder.Object;
	SmokeSystem = SmokeFxFinder.Object;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
	Root->SetMobility(EComponentMobility::Movable);

	// Local frame: origin at the butt, +Z up the shaft to the burning head.
	Shaft = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Shaft"));
	Shaft->SetupAttachment(Root);
	Shaft->SetStaticMesh(CylinderMesh);
	Shaft->SetRelativeLocation(FVector(0.f, 0.f, TorchLength * 0.45f));
	Shaft->SetRelativeScale3D(FVector(0.034f, 0.034f, TorchLength * 0.9f / 100.f));
	Shaft->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Shaft->SetGenerateOverlapEvents(false);

	// Pitch-soaked rag wound round the top: no shadow, or it would black out the ground under the flame.
	Head = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Head"));
	Head->SetupAttachment(Root);
	Head->SetStaticMesh(CylinderMesh);
	Head->SetRelativeLocation(FVector(0.f, 0.f, TorchLength - 7.f));
	Head->SetRelativeScale3D(FVector(0.062f, 0.062f, 0.14f));
	Head->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Head->SetGenerateOverlapEvents(false);
	Head->SetCastShadow(false);

	FlameRoot = CreateDefaultSubobject<USceneComponent>(TEXT("FlameRoot"));
	FlameRoot->SetupAttachment(Root);
	FlameRoot->SetRelativeLocation(FVector(0.f, 0.f, TorchLength - 1.f));
	FlameRoot->SetUsingAbsoluteRotation(true);

	Light = CreateDefaultSubobject<UPointLightComponent>(TEXT("Light"));
	Light->SetupAttachment(FlameRoot);
	Light->SetRelativeLocation(FVector(0.f, 0.f, 12.f));
	Light->SetLightColor(TorchFireColor);
	Light->SetIntensity(TorchLightIntensity);
	Light->SetAttenuationRadius(TorchLightRadius);
	Light->SetSourceRadius(6.f);
	Light->SetCastShadows(false);

	// The fire (UNR-T51). Its assets are set in BeginPlay when they exist and started by RefreshFire.
	auto MakeFx = [this](const TCHAR* Name)
	{
		UNiagaraComponent* Fx = CreateDefaultSubobject<UNiagaraComponent>(Name);
		Fx->SetupAttachment(FlameRoot);
		Fx->bAutoActivate = false;
		Fx->SetCastShadow(false);
		Fx->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Fx->SetGenerateOverlapEvents(false);
		return Fx;
	};
	FlameFx = MakeFx(TEXT("FlameFx"));
	GroundFlameFx = MakeFx(TEXT("GroundFlameFx"));
	// Lying down, the rag sits a hand above the ground: the fire hugs it a little lower.
	GroundFlameFx->SetRelativeLocation(FVector(0.f, 0.f, -4.f));
	EmberFx = MakeFx(TEXT("EmberFx"));
	SmokeFx = MakeFx(TEXT("SmokeFx"));
}

bool ASoulsTorch::HasFireFx()
{
	const ASoulsTorch* Defaults = GetDefault<ASoulsTorch>();
	return Defaults->FlameSystem && Defaults->GroundFlameSystem;
}

void ASoulsTorch::BeginPlay()
{
	Super::BeginPlay();
	if (IronMaterial)
	{
		Shaft->SetMaterial(0, IronMaterial);
		Head->SetMaterial(0, IronMaterial);
	}
	FlameRoot->SetWorldRotation(FRotator::ZeroRotator);
	if (HasFireFx())
	{
		FlameFx->SetAsset(FlameSystem);
		GroundFlameFx->SetAsset(GroundFlameSystem);
		EmberFx->SetAsset(EmberSystem);
		SmokeFx->SetAsset(SmokeSystem);
	}
	else
	{
		Flames.Build(this, FlameRoot, NAME_None, 1.f);
	}
	LightPhase = FMath::FRand() * 20.f;
	BurnLeft = TorchBurnSeconds;
	LastHeadLocation = GetHeadLocation();
	// The fire starts on the first Tick, once SpawnTorch's caller has mounted, laid or handed it out.
}

void ASoulsTorch::RefreshFire()
{
	if (!HasFireFx())
	{
		return;
	}
	const uint8 Flame = !bLit ? 0 : (State == ESoulsTorchState::Ground ? 2 : 1);
	// The fire simulates in the torch's own space, so a long-lived smoke plume would be dragged along
	// rigidly by a running bearer: only a torch at rest (bracket or ground) smokes.
	const bool bSmoke = bLit && (State == ESoulsTorchState::Ground || State == ESoulsTorchState::Mounted);
	const uint8 Want = Flame | (bSmoke ? 4 : 0);
	if (Want == FireLook)
	{
		return;
	}
	const uint8 Was = FireLook == 255 ? 0 : FireLook;
	FireLook = Want;
	// Only what changed is switched; the tall torch flame and the low ground fire cross-fade (the old
	// one's live particles burn out).
	if ((Was & 3) != Flame)
	{
		TorchSetFx(FlameFx, Flame == 1);
		TorchSetFx(GroundFlameFx, Flame == 2);
		TorchSetFx(EmberFx, Flame != 0);
	}
	if (((Was & 4) != 0) != bSmoke)
	{
		TorchSetFx(SmokeFx, bSmoke);
	}
}

void ASoulsTorch::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (State == ESoulsTorchState::Held)
	{
		Release();
	}
	Super::EndPlay(EndPlayReason);
}

ASoulsTorch* ASoulsTorch::SpawnTorch(UWorld* World, const FVector& Location)
{
	if (!World)
	{
		return nullptr;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	return World->SpawnActor<ASoulsTorch>(ASoulsTorch::StaticClass(), Location, FRotator::ZeroRotator, Params);
}

FVector ASoulsTorch::GetHeadLocation() const
{
	return Root->GetComponentTransform().TransformPosition(FVector(0.f, 0.f, TorchLength));
}

FVector ASoulsTorch::GetCenter() const
{
	return Root->GetComponentTransform().TransformPosition(FVector(0.f, 0.f, TorchLength * 0.5f));
}

FVector ASoulsTorch::GetAxis() const
{
	return Root->GetUpVector();
}

void ASoulsTorch::SetCenterAndAxis(const FVector& Center, const FVector& Axis)
{
	const FVector Up = Axis.GetSafeNormal().IsNearlyZero() ? FVector::UpVector : Axis.GetSafeNormal();
	SetActorLocationAndRotation(Center - Up * (TorchLength * 0.5f * GetActorScale3D().Z), FRotationMatrix::MakeFromZ(Up).Rotator());
}

// ---- Finding torches ----

ASoulsTorch* ASoulsTorch::GetHeld(const ASoulsKnight* Knight)
{
	if (!Knight || !Knight->GetWorld())
	{
		return nullptr;
	}
	for (TActorIterator<ASoulsTorch> It(Knight->GetWorld()); It; ++It)
	{
		if (It->State == ESoulsTorchState::Held && It->Holder.Get() == Knight)
		{
			return *It;
		}
	}
	return nullptr;
}

ASoulsTorch* ASoulsTorch::GetWielded(const ASoulsKnight* Knight)
{
	if (!Knight || !Knight->GetWorld())
	{
		return nullptr;
	}
	for (TActorIterator<ASoulsTorch> It(Knight->GetWorld()); It; ++It)
	{
		if (It->State == ESoulsTorchState::Held && It->bMainHand && It->Holder.Get() == Knight)
		{
			return *It;
		}
	}
	return nullptr;
}

void ASoulsTorch::DropWielded(ASoulsKnight* Knight)
{
	if (ASoulsTorch* Torch = GetWielded(Knight))
	{
		Torch->Drop();
	}
}

ASoulsTorch* ASoulsTorch::FindPickupFor(const ASoulsKnight* Knight)
{
	if (!Knight || Knight->IsDead() || !Knight->GetWorld())
	{
		return nullptr;
	}
	const FVector Me = Knight->GetActorLocation();
	ASoulsTorch* Best = nullptr;
	float BestDistance = TNumericLimits<float>::Max();
	for (TActorIterator<ASoulsTorch> It(Knight->GetWorld()); It; ++It)
	{
		ASoulsTorch* Torch = *It;
		if (!Torch->bLit || (Torch->State != ESoulsTorchState::Mounted && Torch->State != ESoulsTorchState::Ground))
		{
			continue;
		}
		const FVector Center = Torch->GetCenter();
		const float Flat = FVector::Dist2D(Center, Me);
		const float Rise = Center.Z - Me.Z;
		// A bracket is reached up to; a torch on the ground is stooped for.
		const bool bReach = Torch->State == ESoulsTorchState::Mounted
			? Flat < 210.f && Rise > -60.f && Rise < 220.f
			: Flat < 170.f && FMath::Abs(Rise) < 160.f;
		if (bReach && Flat < BestDistance)
		{
			Best = Torch;
			BestDistance = Flat;
		}
	}
	return Best;
}

bool ASoulsTorch::TryPickUp(ASoulsKnight* Knight)
{
	ASoulsTorch* Torch = FindPickupFor(Knight);
	if (!Torch)
	{
		return false;
	}
	DropWielded(Knight);
	Torch->GiveTo(Knight, true);
	if (Knight->IsPlayerControlled())
	{
		if (ASoulsGameMode* GameMode = Knight->GetWorld()->GetAuthGameMode<ASoulsGameMode>())
		{
			GameMode->ShowToast(NSLOCTEXT("Souls", "TorchTaken", "Torch taken  (X / LMB to swing)"));
			if (GameMode->PickupSound)
			{
				UGameplayStatics::PlaySound2D(Knight, GameMode->PickupSound, 0.6f);
			}
		}
	}
	UE_LOG(LogUnrealSoulslike, Log, TEXT("Torch: %s took %s"), *Knight->GetName(), *Torch->GetName());
	return true;
}

// ---- Placing ----

void ASoulsTorch::Mount(const FVector& WallPoint, const FVector& Out)
{
	Release();
	State = ESoulsTorchState::Mounted;
	bThrown = false;
	Velocity = FVector::ZeroVector;
	FVector OutN = Out.GetSafeNormal2D();
	if (OutN.IsNearlyZero())
	{
		OutN = FVector::ForwardVector;
	}
	// Leaning out from the wall, held a quarter of the way up by an iron arm.
	const FVector Axis = (FVector::UpVector * FMath::Cos(FMath::DegreesToRadians(22.f)) + OutN * FMath::Sin(FMath::DegreesToRadians(22.f))).GetSafeNormal();
	const FVector Grip = WallPoint + OutN * 14.f;
	SetActorLocationAndRotation(Grip - Axis * TorchGripFromButt, FRotationMatrix::MakeFromZX(Axis, OutN).Rotator());

	if (!Bracket && CubeMesh && GetWorld())
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		if (AStaticMeshActor* Arm = GetWorld()->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), WallPoint + OutN * 7.f, OutN.Rotation(), Params))
		{
			Arm->SetMobility(EComponentMobility::Movable);
			UStaticMeshComponent* ArmMesh = Arm->GetStaticMeshComponent();
			ArmMesh->SetStaticMesh(CubeMesh);
			if (IronMaterial)
			{
				ArmMesh->SetMaterial(0, IronMaterial);
			}
			ArmMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Arm->SetActorScale3D(FVector(0.15f, 0.035f, 0.05f));
			Bracket = Arm;
		}
	}
}

// Lies along the surface (butt down, head propped up a little so it keeps burning).
static void TorchLieOn(ASoulsTorch* Torch, const FVector& Point, const FVector& Normal, const FVector& Direction)
{
	const FVector N = Normal.GetSafeNormal().IsNearlyZero() ? FVector::UpVector : Normal.GetSafeNormal();
	FVector Along = FVector::VectorPlaneProject(Direction, N).GetSafeNormal();
	if (Along.IsNearlyZero())
	{
		Along = FVector::VectorPlaneProject(FVector::ForwardVector, N).GetSafeNormal();
	}
	const float Prop = FMath::DegreesToRadians(12.f);
	const FVector Axis = (Along * FMath::Cos(Prop) + N * FMath::Sin(Prop)).GetSafeNormal();
	Torch->SetActorLocationAndRotation(Point + N * 3.f, FRotationMatrix::MakeFromZX(Axis, N).Rotator());
}

void ASoulsTorch::LayOnGround(const FVector& Location, const FVector& Direction)
{
	Release();
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(TorchLay), false, this);
	FVector Point = Location;
	FVector Normal = FVector::UpVector;
	if (GetWorld() && GetWorld()->LineTraceSingleByChannel(Hit, Location + FVector(0.f, 0.f, 100.f), Location - FVector(0.f, 0.f, 500.f), ECC_Visibility, Params))
	{
		Point = Hit.ImpactPoint;
		Normal = Hit.ImpactNormal;
	}
	TorchLieOn(this, Point, Normal, Direction);
	State = bLit ? ESoulsTorchState::Ground : ESoulsTorchState::Out;
	Velocity = FVector::ZeroVector;
	bThrown = false;
}

void ASoulsTorch::GiveTo(ASoulsKnight* Knight, bool bInMainHand)
{
	if (!Knight || !Knight->GetMesh())
	{
		return;
	}
	// One torch per hand.
	for (TActorIterator<ASoulsTorch> It(Knight->GetWorld()); It; ++It)
	{
		if (*It != this && It->State == ESoulsTorchState::Held && It->Holder.Get() == Knight && It->bMainHand == bInMainHand)
		{
			It->Drop();
		}
	}
	Release();

	USkeletalMeshComponent* Mesh = Knight->GetMesh();
	State = ESoulsTorchState::Held;
	Holder = Knight;
	bMainHand = bInMainHand;
	bHolderWasAI = !Knight->IsPlayerControlled();
	bThrown = false;
	Velocity = FVector::ZeroVector;
	StruckThisFlight.Reset();

	// The sword hand: grip as the sword's (tools/build_souls_character.py SWORD_TRANSFORM), shaft along
	// the hand's +Z like the blade. The off hand: the fist's centre from the finger bones, shaft
	// across the knuckles (whichever way the mirrored bone's Z points toward the index finger).
	FVector GripLocal(-8.f, 3.f, 0.f);
	FVector AxisLocal(0.f, 0.f, 1.f);
	const FName Hand = bInMainHand ? TorchHandR : TorchHandL;
	if (!bInMainHand)
	{
		GripLocal = FVector(8.f, -3.f, 0.f);
		const FTransform HandTM = Mesh->GetSocketTransform(Hand);
		if (Mesh->GetBoneIndex(TorchMiddleL) != INDEX_NONE)
		{
			GripLocal = HandTM.InverseTransformPosition(FMath::Lerp(HandTM.GetLocation(), Mesh->GetSocketLocation(TorchMiddleL), 0.55f));
		}
		if (Mesh->GetBoneIndex(TorchIndexL) != INDEX_NONE && Mesh->GetBoneIndex(TorchPinkyL) != INDEX_NONE)
		{
			const FVector Across = HandTM.InverseTransformVectorNoScale(Mesh->GetSocketLocation(TorchIndexL) - Mesh->GetSocketLocation(TorchPinkyL));
			AxisLocal = FVector(0.f, 0.f, Across.Z >= 0.f ? 1.f : -1.f);
		}
		if (Knight->Shield)
		{
			Knight->Shield->SetVisibility(false, true);
		}
	}
	else if (Knight->Sword)
	{
		Knight->Sword->SetVisibility(false, true);
	}

	AttachToComponent(Mesh, FAttachmentTransformRules::KeepRelativeTransform, Hand);
	Root->SetRelativeLocationAndRotation(GripLocal - AxisLocal * TorchGripFromButt, FRotationMatrix::MakeFromZ(AxisLocal).Rotator());
	// The player's torch throws real shadows off the knight and the ruins; everyone else's is cheap.
	Light->SetCastShadows(!bHolderWasAI);
	LastHeadLocation = GetHeadLocation();
}

void ASoulsTorch::Release()
{
	if (State == ESoulsTorchState::Held)
	{
		if (ASoulsKnight* Knight = Holder.Get())
		{
			UStaticMeshComponent* Stowed = bMainHand ? Knight->Sword.Get() : Knight->Shield.Get();
			if (Stowed)
			{
				Stowed->SetVisibility(true, true);
			}
		}
	}
	Holder = nullptr;
	if (GetAttachParentActor() || Root->GetAttachParent())
	{
		DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	}
	SetActorScale3D(FVector::OneVector);
	Light->SetCastShadows(false);
}

void ASoulsTorch::Drop()
{
	if (State != ESoulsTorchState::Held)
	{
		return;
	}
	ASoulsKnight* Knight = Holder.Get();
	const FVector Center = GetCenter();
	const FVector Axis = GetAxis();
	Release();
	SetCenterAndAxis(Center, Axis);
	State = ESoulsTorchState::Flying;
	bThrown = false;
	Thrower = Knight;
	ThrowerTeam = Knight ? Knight->Team : 0;
	StruckThisFlight.Reset();
	FlightTime = 0.f;
	Velocity = FVector(0.f, 0.f, 120.f);
	if (Knight)
	{
		Velocity += Knight->GetVelocity() * 0.4f + Knight->GetActorForwardVector() * 70.f;
		UE_LOG(LogUnrealSoulslike, Log, TEXT("Torch: %s dropped %s"), *Knight->GetName(), *GetName());
	}
	const FVector Side = FMath::VRand().GetSafeNormal2D();
	SpinAxis = Side.IsNearlyZero() ? FVector::RightVector : Side;
	SpinSpeed = 220.f;
}

void ASoulsTorch::Throw(ASoulsKnight* Target)
{
	ASoulsKnight* Knight = Holder.Get();
	if (State != ESoulsTorchState::Held || !bMainHand || !Knight || Knight->IsDead() || !GetWorld())
	{
		return;
	}
	const FVector Start = GetCenter();
	FVector Aim;
	if (Target && !Target->IsDead())
	{
		Aim = Target->GetActorLocation() + FVector(0.f, 0.f, 20.f);
	}
	else
	{
		Target = nullptr;
		const FVector Ahead = Knight->GetActorForwardVector().GetSafeNormal2D();
		Aim = TorchGroundBelow(GetWorld(), Knight->GetActorLocation() + Ahead * TorchThrowRange + FVector(0.f, 0.f, 300.f), Knight);
	}
	float Time = FMath::Clamp(FVector::Dist2D(Aim, Start) / TorchThrowSpeed, 0.3f, 1.2f);
	if (Target)
	{
		// Lead a moving target a little.
		Aim += Target->GetVelocity() * FVector(1.f, 1.f, 0.f) * Time * 0.7f;
		Time = FMath::Clamp(FVector::Dist2D(Aim, Start) / TorchThrowSpeed, 0.3f, 1.2f);
	}
	const FVector Gravity(0.f, 0.f, -TorchGravity);
	Velocity = (Aim - Start - 0.5f * Gravity * Time * Time) / Time;

	Release();
	SetCenterAndAxis(Start, Velocity);
	State = ESoulsTorchState::Flying;
	bThrown = true;
	Thrower = Knight;
	ThrowerTeam = Knight->Team;
	StruckThisFlight.Reset();
	FlightTime = 0.f;
	const FVector Side = FVector::CrossProduct(Velocity.GetSafeNormal(), FVector::UpVector).GetSafeNormal();
	SpinAxis = Side.IsNearlyZero() ? FVector::RightVector : Side;
	SpinSpeed = 760.f;
	UE_LOG(LogUnrealSoulslike, Log, TEXT("Torch: %s threw %s at %s"), *Knight->GetName(), *GetName(), Target ? *Target->GetName() : TEXT("the ground ahead"));
}

void ASoulsTorch::Extinguish()
{
	if (!bLit)
	{
		return;
	}
	bLit = false;
	Flames.SetVisible(false);
	Light->SetVisibility(false);
	if (State == ESoulsTorchState::Held)
	{
		Drop();
	}
	else if (State != ESoulsTorchState::Flying)
	{
		State = ESoulsTorchState::Out;
	}
	RefreshFire(); // every effect stops spawning; what's alive burns out
	SetLifeSpan(30.f);
}

// ---- Fighting with it ----

bool ASoulsTorch::HandleSwingHit(ASoulsKnight* Knight, bool bHeavy)
{
	ASoulsTorch* Torch = GetWielded(Knight);
	if (!Torch)
	{
		return false;
	}
	Torch->SwingHit(Knight);
	return true;
}

void ASoulsTorch::SwingHit(ASoulsKnight* Knight)
{
	UWorld* World = GetWorld();
	if (!World || !Knight)
	{
		return;
	}
	const float Scale = Knight->GetActorScale3D().X;
	const float Reach = TorchSwingReach * Scale;
	const FVector Origin = Knight->GetActorLocation();
	const FVector Forward = Knight->GetActorForwardVector().GetSafeNormal2D();

	TArray<FOverlapResult> Overlaps;
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_Pawn);
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(TorchSwing), false, Knight);
	Params.AddIgnoredActor(this);
	World->OverlapMultiByObjectType(Overlaps, Origin, FQuat::Identity, Objects, FCollisionShape::MakeSphere(Reach + 60.f), Params);

	TSet<AActor*> Struck;
	bool bLanded = false;
	for (const FOverlapResult& Overlap : Overlaps)
	{
		AActor* Other = Overlap.GetActor();
		if (!Other || Other == Knight || Struck.Contains(Other))
		{
			continue;
		}
		FVector ToOther = Other->GetActorLocation() - Origin;
		ToOther.Z = 0.f;
		float Radius = 0.f, HalfHeight = 0.f;
		Other->GetSimpleCollisionCylinder(Radius, HalfHeight);
		const float Distance = ToOther.Size();
		if (Distance > Reach + Radius || FMath::Abs(Other->GetActorLocation().Z - Origin.Z) > 160.f * Scale + HalfHeight)
		{
			continue;
		}
		if (Distance > Radius + 40.f && FVector::DotProduct(Forward, ToOther / FMath::Max(Distance, 1.f)) < 0.25f)
		{
			continue;
		}
		const FVector Impact = Other->GetActorLocation() - ToOther.GetSafeNormal() * Radius + FVector(0.f, 0.f, 40.f);
		if (ASoulsKnight* Victim = Cast<ASoulsKnight>(Other))
		{
			if (Victim->Team == Knight->Team || Victim->IsDead())
			{
				continue;
			}
			Struck.Add(Other);
			// A torch is a club, not a blade: lighter blows, but they burn. A shield stops the fire.
			const ASoulsKnight::EMeleeResult Result = Victim->ReceiveMelee(Knight, Knight->LightDamage * TorchSwingDamageScale, 10.f, 8.f, false, Impact);
			if (Result == ASoulsKnight::EMeleeResult::Hit)
			{
				bLanded = true;
				USoulsBurnComponent::Ignite(Victim, Knight);
				ASoulsSparks::Burst(World, Impact, -ToOther.GetSafeNormal() + FVector(0.f, 0.f, 0.5f), 10, 0.7f, 0.9f, TorchFireColor);
			}
		}
		else if (ICombatDamageable* Damageable = Cast<ICombatDamageable>(Other))
		{
			Struck.Add(Other);
			Damageable->ApplyDamage(1.f, Knight, Impact, ToOther.GetSafeNormal() * 300.f + FVector(0.f, 0.f, 200.f));
			bLanded = true;
		}
	}
	if (bLanded)
	{
		Knight->Hitstop(0.05f);
	}
	// Whatever the swing passes over catches (UNR-T41's grass and heather).
	TorchIgniteWorld(this, TorchGroundBelow(World, Origin + Forward * Reach * 0.7f, Knight), 120.f);
}

void ASoulsTorch::StrikeKnight(ASoulsKnight* Victim, const FVector& Impact)
{
	ASoulsKnight* By = Thrower.Get();
	AActor* Causer = By ? static_cast<AActor*>(By) : static_cast<AActor*>(this);
	const FVector From = (GetCenter() - Victim->GetActorLocation()).GetSafeNormal2D();
	const bool bGuarded = Victim->IsBlocking() && FVector::DotProduct(Victim->GetActorForwardVector(), From) > 0.2f;
	FDamageEvent Event;
	// TakeDamage honours roll i-frames and a raised shield.
	const float Dealt = Victim->TakeDamage(TorchThrowDamage, Event, By ? By->GetController() : nullptr, Causer);
	if (Dealt > 0.f && !bGuarded)
	{
		USoulsBurnComponent::Ignite(Victim, Causer);
	}
	ASoulsSparks::Burst(GetWorld(), Impact, From + FVector(0.f, 0.f, 0.4f), 14, 0.8f, 0.9f, TorchFireColor);
	if (Dealt > 0.f)
	{
		Victim->Hitstop(0.06f);
	}
	UE_LOG(LogUnrealSoulslike, Log, TEXT("Torch: %s struck %s for %.0f%s"), *GetName(), *Victim->GetName(), Dealt, bGuarded ? TEXT(" (guarded)") : TEXT(""));
}

void ASoulsTorch::TickFlight(float DeltaSeconds)
{
	UWorld* World = GetWorld();
	FlightTime += DeltaSeconds;
	const FVector Center = GetCenter();
	Velocity.Z -= TorchGravity * DeltaSeconds;
	FVector Next = Center + Velocity * DeltaSeconds;

	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
	Objects.AddObjectTypesToQuery(ECC_Pawn);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(TorchFlight), false, this);
	if (Thrower.IsValid())
	{
		Params.AddIgnoredActor(Thrower.Get());
	}
	if (Bracket)
	{
		Params.AddIgnoredActor(Bracket.Get());
	}
	for (const TWeakObjectPtr<AActor>& Already : StruckThisFlight)
	{
		if (Already.IsValid())
		{
			Params.AddIgnoredActor(Already.Get());
		}
	}

	FHitResult Hit;
	if (World->SweepSingleByObjectType(Hit, Center, Next, FQuat::Identity, Objects, FCollisionShape::MakeSphere(TorchFlightRadius), Params))
	{
		AActor* Other = Hit.GetActor();
		if (APawn* Pawn = Cast<APawn>(Other))
		{
			// Bodies are struck once and the torch tumbles on past them.
			StruckThisFlight.Add(TWeakObjectPtr<AActor>(Pawn));
			ASoulsKnight* Victim = Cast<ASoulsKnight>(Pawn);
			if (bThrown && Victim && Victim->Team != ThrowerTeam && !Victim->IsDead())
			{
				StrikeKnight(Victim, Hit.ImpactPoint);
				Velocity = FVector(-Velocity.X * 0.15f, -Velocity.Y * 0.15f, FMath::Min(Velocity.Z, 0.f) * 0.2f);
			}
			else if (bThrown && !Victim)
			{
				if (ICombatDamageable* Damageable = Cast<ICombatDamageable>(Pawn))
				{
					Damageable->ApplyDamage(1.f, Thrower.Get(), Hit.ImpactPoint, Velocity.GetSafeNormal() * 300.f);
				}
			}
			SetCenterAndAxis(Hit.bStartPenetrating ? Center : FVector(Hit.Location), GetAxis());
			return;
		}
		if (Hit.ImpactNormal.Z < 0.5f && !Hit.bStartPenetrating && FlightTime < 6.f)
		{
			// A wall: glance off it and fall.
			Velocity = FVector::VectorPlaneProject(Velocity, Hit.ImpactNormal) * 0.3f + Hit.ImpactNormal * 60.f;
			SetCenterAndAxis(Hit.Location + Hit.ImpactNormal * 2.f, GetAxis());
			return;
		}
		Land(Hit);
		return;
	}

	const FQuat Spin(SpinAxis, FMath::DegreesToRadians(SpinSpeed) * DeltaSeconds);
	SetCenterAndAxis(Next, Spin.RotateVector(GetAxis()));
	if (bLit && TorchInWater(World, Next))
	{
		UE_LOG(LogUnrealSoulslike, Log, TEXT("Torch: %s doused in water"), *GetName());
		Extinguish();
		Velocity *= 0.2f;
	}
	const AWorldSettings* Settings = World->GetWorldSettings();
	if (FlightTime > 8.f || (Settings && Next.Z < Settings->KillZ))
	{
		Destroy();
	}
}

void ASoulsTorch::Land(const FHitResult& Hit)
{
	UWorld* World = GetWorld();
	const FVector Normal = Hit.bStartPenetrating ? FVector::UpVector : FVector(Hit.ImpactNormal);
	const FVector Point = Hit.bStartPenetrating ? GetCenter() : FVector(Hit.ImpactPoint);
	TorchLieOn(this, Point, Normal, Velocity.IsNearlyZero() ? GetAxis() : Velocity);
	const bool bWasThrown = bThrown;
	ASoulsKnight* By = Thrower.Get();
	Velocity = FVector::ZeroVector;
	bThrown = false;
	Thrower = nullptr;
	if (bLit && TorchInWater(World, Point + Normal * 10.f))
	{
		Extinguish();
	}
	State = bLit ? ESoulsTorchState::Ground : ESoulsTorchState::Out;
	if (bLit)
	{
		// UNR-T41 HOOK: the field catches where the torch comes down.
		TorchIgniteWorld(this, Point, bWasThrown ? 260.f : 150.f);
		ASoulsSparks::Burst(World, Point + Normal * 10.f, Normal, bWasThrown ? 14 : 6, 0.7f, 0.9f, TorchFireColor);
		if (bWasThrown)
		{
			// Burning pitch splashes onto anyone standing where it lands.
			for (TActorIterator<ASoulsKnight> It(World); It; ++It)
			{
				if (It->Team != ThrowerTeam && !It->IsDead() && !StruckThisFlight.Contains(TWeakObjectPtr<AActor>(*It))
					&& FVector::Dist2D(It->GetActorLocation(), Point) < TorchSplashRadius && FMath::Abs(It->GetActorLocation().Z - Point.Z) < 200.f)
				{
					USoulsBurnComponent::Ignite(*It, By ? static_cast<AActor*>(By) : static_cast<AActor*>(this), 3.f);
				}
			}
		}
	}
	StruckThisFlight.Reset();
	UE_LOG(LogUnrealSoulslike, Log, TEXT("Torch: %s landed at %s (%s)"), *GetName(), *Point.ToString(), bLit ? TEXT("burning") : TEXT("out"));
}

void ASoulsTorch::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	LightPhase += DeltaSeconds;

	// Flames trail behind a swung or thrown torch.
	const FVector HeadNow = GetHeadLocation();
	FVector HeadVelocity = DeltaSeconds > 0.f ? (HeadNow - LastHeadLocation) / DeltaSeconds : FVector::ZeroVector;
	LastHeadLocation = HeadNow;
	if (HeadVelocity.SizeSquared() > FMath::Square(3000.f))
	{
		HeadVelocity = FVector::ZeroVector; // a teleport, not a swing
	}
	FlameLean = FMath::VInterpTo(FlameLean, (-HeadVelocity * 0.012f).GetClampedToMaxSize(14.f), DeltaSeconds, 12.f);
	if (bLit)
	{
		Flames.Flicker(DeltaSeconds, FlameLean);
		Light->SetIntensity(TorchLightIntensity * TorchFlickerScale(LightPhase));
		// The Niagara fire simulates in FlameRoot's space: tip that space away from the motion so the
		// flames trail a swing or a throw, and stand straight up (world-up) when still.
		FlameRoot->SetWorldRotation(FRotationMatrix::MakeFromZ(FVector(0.f, 0.f, 22.f) + FlameLean).Rotator());
	}

	switch (State)
	{
	case ESoulsTorchState::Held:
	{
		ASoulsKnight* Knight = Holder.Get();
		if (!Knight)
		{
			// The bearer is gone (enemies are cleared away when the island resets).
			if (bHolderWasAI)
			{
				Destroy();
				return;
			}
			Release();
			State = ESoulsTorchState::Flying;
			Velocity = FVector::ZeroVector;
		}
		else if (Knight->IsDead())
		{
			Drop();
		}
		else if (Knight->IsSwimming())
		{
			Drop();
			Extinguish();
		}
		break;
	}
	case ESoulsTorchState::Flying:
		TickFlight(DeltaSeconds);
		break;
	default:
		break;
	}

	// Torches burn down once they're off the wall and out of an enemy's hand.
	if (bLit && State != ESoulsTorchState::Mounted && !(State == ESoulsTorchState::Held && bHolderWasAI))
	{
		BurnLeft -= DeltaSeconds;
		if (BurnLeft <= 0.f)
		{
			Extinguish();
		}
	}
	if (!IsActorBeingDestroyed())
	{
		RefreshFire();
	}
}

// ---------------------------------------------------------------------------------------------
// Burning knights
// ---------------------------------------------------------------------------------------------

USoulsBurnComponent::USoulsBurnComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	bAutoActivate = true;
}

bool USoulsBurnComponent::IsBurning(const ASoulsKnight* Knight)
{
	return Knight && Knight->FindComponentByClass<USoulsBurnComponent>() != nullptr;
}

void USoulsBurnComponent::Ignite(ASoulsKnight* Victim, AActor* InSource, float Seconds)
{
	if (!Victim || Victim->IsDead() || Victim->IsSwimming())
	{
		return;
	}
	USoulsBurnComponent* Burn = Victim->FindComponentByClass<USoulsBurnComponent>();
	if (!Burn)
	{
		Burn = NewObject<USoulsBurnComponent>(Victim);
		Burn->RegisterComponent();
		USkeletalMeshComponent* Mesh = Victim->GetMesh();
		const FName Bone = Mesh && Mesh->GetBoneIndex(TorchBurnBone) != INDEX_NONE ? TorchBurnBone : NAME_None;
		USceneComponent* Parent = Mesh ? static_cast<USceneComponent*>(Mesh) : Victim->GetRootComponent();
		const ASoulsTorch* Defaults = GetDefault<ASoulsTorch>();
		if (ASoulsTorch::HasFireFx())
		{
			// The torch's ground fire and embers on the chest, upright whichever way the body turns,
			// spread wider over the torso.
			for (UNiagaraSystem* System : { Defaults->GroundFlameSystem.Get(), Defaults->EmberSystem.Get() })
			{
				if (!System)
				{
					continue;
				}
				UNiagaraComponent* Fx = NewObject<UNiagaraComponent>(Victim);
				Fx->bAutoActivate = false;
				Fx->SetAsset(System);
				Fx->SetupAttachment(Parent, Bone);
				Fx->SetUsingAbsoluteRotation(true);
				Fx->SetUsingAbsoluteScale(true);
				Fx->SetCastShadow(false);
				Fx->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				Fx->RegisterComponent();
				Fx->SetWorldRotation(FRotator::ZeroRotator);
				Fx->SetWorldScale3D(FVector(TorchBurnFxScale));
				Fx->Activate(true);
				Burn->Fx.Add(Fx);
			}
		}
		else
		{
			Burn->Flames.Build(Victim, Parent, Bone, 2.2f);
		}
		Burn->Glow = NewObject<UPointLightComponent>(Victim);
		Burn->Glow->SetupAttachment(Parent, Bone);
		Burn->Glow->SetLightColor(TorchFireColor);
		Burn->Glow->SetIntensity(TorchBurnLight);
		Burn->Glow->SetAttenuationRadius(700.f);
		Burn->Glow->SetCastShadows(false);
		Burn->Glow->RegisterComponent();
		Burn->LastLocation = Victim->GetActorLocation();
		if (const ASoulsEnemy* Enemy = Cast<ASoulsEnemy>(Victim); Enemy && !Enemy->bFearless)
		{
			Victim->AddFear(8.f);
		}
		if constexpr (!TorchHasBurnHook<ASoulsKnight>())
		{
			// No flinch-free health hook on the knight yet: deal the whole burn as one blow now.
			TorchBurnHealth(Victim, TorchBurnPerSecond * Seconds, InSource);
		}
		UE_LOG(LogUnrealSoulslike, Log, TEXT("Torch: %s set alight"), *Victim->GetName());
	}
	Burn->Source = InSource;
	Burn->Remaining = FMath::Max(Burn->Remaining, Seconds);
}

void USoulsBurnComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	ASoulsKnight* Victim = Cast<ASoulsKnight>(GetOwner());
	if (!Victim)
	{
		DestroyComponent();
		return;
	}
	Remaining -= DeltaTime;
	Phase += DeltaTime;
	if (Remaining <= 0.f || Victim->IsSwimming())
	{
		DestroyComponent();
		return;
	}
	if constexpr (TorchHasBurnHook<ASoulsKnight>())
	{
		TickAccumulator += DeltaTime;
		while (TickAccumulator >= 0.5f && !Victim->IsDead())
		{
			TickAccumulator -= 0.5f;
			TorchBurnHealth(Victim, TorchBurnPerSecond * 0.5f, Source.Get());
		}
	}
	const FVector Location = Victim->GetActorLocation();
	const FVector Moving = DeltaTime > 0.f ? (Location - LastLocation) / DeltaTime : FVector::ZeroVector;
	LastLocation = Location;
	Flames.Flicker(DeltaTime, (-Moving * 0.02f).GetClampedToMaxSize(25.f));
	if (Glow)
	{
		Glow->SetIntensity(TorchBurnLight * TorchFlickerScale(Phase) * FMath::Clamp(Remaining / 0.8f, 0.f, 1.f));
	}
}

void USoulsBurnComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	Stop();
	Super::OnComponentDestroyed(bDestroyingHierarchy);
}

void USoulsBurnComponent::Stop()
{
	Flames.DestroyParts();
	// Let the body's fire die down (live particles finish), then the components go.
	for (UNiagaraComponent* Each : Fx)
	{
		if (Each)
		{
			Each->SetAutoDestroy(true);
			Each->Deactivate();
		}
	}
	Fx.Reset();
	if (Glow)
	{
		Glow->DestroyComponent();
		Glow = nullptr;
	}
}

// ---------------------------------------------------------------------------------------------
// Subsystem: wall torches, torch-bearers, -TorchDemo
// ---------------------------------------------------------------------------------------------

bool USoulsTorchSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void USoulsTorchSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	const TCHAR* Command = FCommandLine::Get();
	// Keep the existing verification shots as they were (a torch-bearer has no shield).
	bArmEnemies = !FParse::Param(Command, TEXT("KnightDemo")) && !FParse::Param(Command, TEXT("HandShots"))
		&& !FParse::Param(Command, TEXT("ClashDemo")) && !FParse::Param(Command, TEXT("NoTorchBearers"));

	int32 Mounted = 0;
	for (TActorIterator<ATargetPoint> It(&InWorld); It; ++It)
	{
		if (It->ActorHasTag(TorchWallTag))
		{
			if (ASoulsTorch* Torch = ASoulsTorch::SpawnTorch(&InWorld, It->GetActorLocation()))
			{
				Torch->Mount(It->GetActorLocation(), It->GetActorForwardVector());
				++Mounted;
			}
		}
	}
	SpawnHandle = InWorld.AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateUObject(this, &USoulsTorchSubsystem::OnActorSpawned));
	UE_LOG(LogUnrealSoulslike, Log, TEXT("Torches: %d mounted on walls; torch-bearers %s"), Mounted, bArmEnemies ? TEXT("on") : TEXT("off"));

	if (FParse::Param(Command, TEXT("TorchDemo")))
	{
		RunDemo();
	}
}

void USoulsTorchSubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld(); World && SpawnHandle.IsValid())
	{
		World->RemoveOnActorSpawnedHandler(SpawnHandle);
	}
	SpawnHandle.Reset();
	Super::Deinitialize();
}

void USoulsTorchSubsystem::OnActorSpawned(AActor* Actor)
{
	ASoulsEnemy* Enemy = Cast<ASoulsEnemy>(Actor);
	if (!Enemy || !bArmEnemies || !GetWorld())
	{
		return;
	}
	// Next tick: the game mode turns some into leapers right after spawning them.
	GetWorld()->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateUObject(this, &USoulsTorchSubsystem::ConsiderTorchBearer, TWeakObjectPtr<ASoulsEnemy>(Enemy)));
}

void USoulsTorchSubsystem::ConsiderTorchBearer(TWeakObjectPtr<ASoulsEnemy> WeakEnemy)
{
	ASoulsEnemy* Enemy = WeakEnemy.Get();
	if (!Enemy || Enemy->IsDead() || Enemy->bIsBoss || Enemy->bCanPounce || ASoulsTorch::GetHeld(Enemy))
	{
		return;
	}
	// The same posts carry torches every time the island resets.
	const FVector Post = Enemy->HomeLocation;
	const uint32 Hash = HashCombine(GetTypeHash(FMath::RoundToInt(Post.X / 100.f)), GetTypeHash(FMath::RoundToInt(Post.Y / 100.f)));
	if (static_cast<float>(Hash % 100u) < TorchBearerPercent)
	{
		ArmEnemy(Enemy);
	}
}

ASoulsTorch* USoulsTorchSubsystem::ArmEnemy(ASoulsEnemy* Enemy)
{
	if (!Enemy || Enemy->IsDead())
	{
		return nullptr;
	}
	ASoulsTorch* Torch = ASoulsTorch::SpawnTorch(Enemy->GetWorld(), Enemy->GetActorLocation());
	if (!Torch)
	{
		return nullptr;
	}
	Torch->GiveTo(Enemy, false);
	// No shield to raise.
	Enemy->BlockChance = 0.f;
	UE_LOG(LogUnrealSoulslike, Log, TEXT("Torch: %s carries %s"), *Enemy->GetName(), *Torch->GetName());
	return Torch;
}

namespace
{
	struct FTorchDemo
	{
		TWeakObjectPtr<UWorld> World;
		TWeakObjectPtr<ASoulsKnight> Foe;
		TWeakObjectPtr<ASoulsKnight> Bearer;
		TWeakObjectPtr<ASoulsTorch> Torch;
		TWeakObjectPtr<ASoulsTorch> BearerTorch;
		TWeakObjectPtr<ASoulsTorch> WallTorch;
		TWeakObjectPtr<ACameraActor> CloseCamera;
		FVector Origin = FVector::ZeroVector;
		FVector Forward = FVector::ForwardVector;
		int32 Passed = 0;
		int32 Failed = 0;
	};

	ASoulsKnight* TorchDemoPlayer(const TSharedRef<FTorchDemo>& Demo)
	{
		return Demo->World.IsValid() ? Cast<ASoulsKnight>(UGameplayStatics::GetPlayerPawn(Demo->World.Get(), 0)) : nullptr;
	}

	void TorchDemoCheck(const TSharedRef<FTorchDemo>& Demo, const TCHAR* What, bool bOk)
	{
		(bOk ? Demo->Passed : Demo->Failed)++;
		UE_LOG(LogUnrealSoulslike, Log, TEXT("Torch check: %s %s"), What, bOk ? TEXT("PASS") : TEXT("FAIL"));
	}

	void TorchDemoShot(const TCHAR* Name)
	{
		FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("TorchDemo_%s"), Name), true, false);
	}

	void TorchDemoView(ASoulsKnight* Knight, float YawOffset, float Distance, float Pitch)
	{
		if (!Knight)
		{
			return;
		}
		Knight->GetCameraBoom()->TargetArmLength = Distance;
		if (AController* Controller = Knight->GetController())
		{
			Controller->SetControlRotation(FRotator(Pitch, Knight->GetActorRotation().Yaw + YawOffset, 0.f));
		}
	}

	// UNR-T51: a close look at a torch's fire from a camera of its own (the knight's boom always centres
	// the knight, which hides or crops a fire lying at its feet). From the knight's side of the torch,
	// swung 40 degrees round, 1.4 m off and a little above, so the flames fill the middle of the frame.
	void TorchDemoCloseUp(const TSharedRef<FTorchDemo>& Demo, ASoulsKnight* Knight, const ASoulsTorch* Torch)
	{
		UWorld* World = Demo->World.Get();
		APlayerController* Player = Knight ? Cast<APlayerController>(Knight->GetController()) : nullptr;
		if (!World || !Player || !Torch)
		{
			return;
		}
		const FVector Fire = Torch->GetHeadLocation() + FVector(0.f, 0.f, 12.f);
		FVector Away = (Knight->GetActorLocation() - Fire).GetSafeNormal2D();
		if (Away.IsNearlyZero())
		{
			Away = -Demo->Forward;
		}
		Away = Away.RotateAngleAxis(40.f, FVector::UpVector);
		FVector Eye = Fire + Away * 140.f + FVector(0.f, 0.f, 45.f);
		// Keep the lens out of a rise in the ground.
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(TorchDemoEye), false, Knight);
		if (World->LineTraceSingleByChannel(Hit, Eye + FVector(0.f, 0.f, 300.f), Eye - FVector(0.f, 0.f, 300.f), ECC_Visibility, Params))
		{
			Eye.Z = FMath::Max(Eye.Z, Hit.ImpactPoint.Z + 35.f);
		}
		ACameraActor* Camera = Demo->CloseCamera.Get();
		if (!Camera)
		{
			FActorSpawnParameters Spawn;
			Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Camera = World->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), Eye, FRotator::ZeroRotator, Spawn);
			Demo->CloseCamera = Camera;
		}
		if (!Camera)
		{
			return;
		}
		Camera->SetActorLocationAndRotation(Eye, (Fire - Eye).Rotation());
		if (UCameraComponent* Lens = Camera->GetCameraComponent())
		{
			Lens->SetFieldOfView(55.f);
			Lens->SetConstraintAspectRatio(false);
		}
		Player->SetViewTarget(Camera);
	}

	void TorchDemoCloseUpEnd(ASoulsKnight* Knight)
	{
		if (APlayerController* Player = Knight ? Cast<APlayerController>(Knight->GetController()) : nullptr)
		{
			Player->SetViewTarget(Knight);
		}
	}

	void TorchDemoStage(const TSharedRef<FTorchDemo>& Demo, ASoulsKnight* Knight)
	{
		Knight->SetActorLocation(Demo->Origin, false, nullptr, ETeleportType::TeleportPhysics);
		Knight->SetActorRotation(Demo->Forward.Rotation());
	}

	// The nearest ordinary foe, brain switched off so it stands still for the shots, set Distance ahead.
	ASoulsKnight* TorchDemoFoe(const TSharedRef<FTorchDemo>& Demo, ASoulsKnight* Knight, float Distance, const ASoulsKnight* Except = nullptr)
	{
		ASoulsEnemy* Best = nullptr;
		for (TActorIterator<ASoulsEnemy> It(Knight->GetWorld()); It; ++It)
		{
			if (*It != Except && !It->IsDead() && !It->bIsBoss && It->Team != Knight->Team
				&& (!Best || Knight->GetDistanceTo(*It) < Knight->GetDistanceTo(Best)))
			{
				Best = *It;
			}
		}
		if (!Best)
		{
			return nullptr;
		}
		if (AController* Brain = Best->GetController())
		{
			Brain->UnPossess();
		}
		const FVector Spot = Demo->Origin + Demo->Forward * Distance + FVector(0.f, 0.f, 5.f);
		Best->SetActorLocation(Spot, false, nullptr, ETeleportType::TeleportPhysics);
		Best->SetActorRotation((-Demo->Forward).Rotation());
		return Best;
	}
}

void USoulsTorchSubsystem::RunDemo()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	GEngine->Exec(World, TEXT("r.MotionBlurQuality 0"));
	const TSharedRef<FTorchDemo> Demo = MakeShared<FTorchDemo>();
	Demo->World = World;

	struct FStep { float Time; TFunction<void(ASoulsKnight*)> Do; };
	TArray<FStep> Steps;
	Steps.Add({ 0.f, [Demo](ASoulsKnight* K)
	{
		Demo->Origin = K->GetActorLocation();
		Demo->Forward = K->GetActorForwardVector().GetSafeNormal2D();
		TorchDemoStage(Demo, K);
		if (ASoulsTorch* Torch = ASoulsTorch::SpawnTorch(K->GetWorld(), Demo->Origin + Demo->Forward * 110.f))
		{
			Torch->LayOnGround(Demo->Origin + Demo->Forward * 110.f, K->GetActorRightVector());
			Demo->Torch = Torch;
		}
		TorchDemoView(K, 150.f, 280.f, -22.f);
	} });
	// UNR-T51: the fire up close (its flames have had half a second to build).
	Steps.Add({ 0.5f, [Demo](ASoulsKnight* K)
	{
		TorchDemoCheck(Demo, TEXT("torches burn with the Niagara fire (else run tools/build_torch_fire.py)"), ASoulsTorch::HasFireFx());
		TorchDemoCloseUp(Demo, K, Demo->Torch.Get());
	} });
	Steps.Add({ 0.65f, [](ASoulsKnight*) { TorchDemoShot(TEXT("01b_ground_fire")); } });
	Steps.Add({ 0.8f, [](ASoulsKnight* K) { TorchDemoCloseUpEnd(K); } });
	Steps.Add({ 0.9f, [](ASoulsKnight*) { TorchDemoShot(TEXT("01_ground_torch")); } });
	Steps.Add({ 1.2f, [](ASoulsKnight* K) { K->InteractPressed(); } });
	Steps.Add({ 1.4f, [Demo](ASoulsKnight* K)
	{
		const bool bHooked = ASoulsTorch::GetWielded(K) != nullptr;
		TorchDemoCheck(Demo, TEXT("Interact picks up a torch on the ground (InteractPressed hook)"), bHooked);
		if (!bHooked)
		{
			ASoulsTorch::TryPickUp(K);
		}
		TorchDemoCheck(Demo, TEXT("the sword is stowed while the torch is wielded"), ASoulsTorch::GetWielded(K) && K->Sword && !K->Sword->IsVisible());
		TorchDemoView(K, 160.f, 230.f, -8.f);
	} });
	Steps.Add({ 2.2f, [](ASoulsKnight*) { TorchDemoShot(TEXT("02_wielded")); } });
	Steps.Add({ 2.4f, [Demo](ASoulsKnight* K)
	{
		TorchDemoStage(Demo, K);
		Demo->Foe = TorchDemoFoe(Demo, K, 190.f);
		TorchDemoCheck(Demo, TEXT("a foe to swing at"), Demo->Foe.IsValid());
		TorchDemoView(K, 40.f, 430.f, -12.f);
	} });
	Steps.Add({ 2.8f, [](ASoulsKnight* K) { K->DoComboAttackStart(); } });
	Steps.Add({ 3.15f, [](ASoulsKnight*) { TorchDemoShot(TEXT("03_swing")); } });
	Steps.Add({ 4.2f, [Demo](ASoulsKnight*)
	{
		TorchDemoCheck(Demo, TEXT("a torch swing sets the foe alight (SwingHit hook)"), USoulsBurnComponent::IsBurning(Demo->Foe.Get()));
		TorchDemoShot(TEXT("04_foe_burning"));
	} });
	Steps.Add({ 6.0f, [](ASoulsKnight* K)
	{
		FDamageEvent Event;
		K->TakeDamage(30.f, Event, nullptr, nullptr);
	} });
	Steps.Add({ 6.8f, [](ASoulsKnight* K) { K->HealPressed(); } });
	Steps.Add({ 7.0f, [Demo](ASoulsKnight* K)
	{
		TorchDemoCheck(Demo, TEXT("drinking a flask drops the torch (HealPressed hook)"), ASoulsTorch::GetWielded(K) == nullptr);
		TorchDemoCheck(Demo, TEXT("the sword is back in hand"), K->Sword && K->Sword->IsVisible());
		TorchDemoView(K, 150.f, 320.f, -25.f);
	} });
	Steps.Add({ 8.2f, [Demo](ASoulsKnight*)
	{
		const ASoulsTorch* Torch = Demo->Torch.Get();
		TorchDemoCheck(Demo, TEXT("the dropped torch lies burning on the ground"), Torch && Torch->GetState() == ESoulsTorchState::Ground && Torch->IsLit());
		TorchDemoShot(TEXT("05_dropped_burning"));
	} });
	Steps.Add({ 8.35f, [Demo](ASoulsKnight* K) { TorchDemoCloseUp(Demo, K, Demo->Torch.Get()); } });
	Steps.Add({ 8.55f, [](ASoulsKnight*) { TorchDemoShot(TEXT("05b_dropped_fire")); } });
	Steps.Add({ 8.75f, [](ASoulsKnight* K) { TorchDemoCloseUpEnd(K); } });
	// The flask's animation must finish before Interact is taken again.
	Steps.Add({ 9.4f, [](ASoulsKnight* K) { K->InteractPressed(); } });
	Steps.Add({ 9.6f, [Demo](ASoulsKnight* K)
	{
		const bool bHooked = ASoulsTorch::GetWielded(K) != nullptr;
		if (!bHooked)
		{
			ASoulsTorch::TryPickUp(K);
		}
		TorchDemoCheck(Demo, TEXT("the dropped torch can be picked up again"), bHooked);
		TorchDemoStage(Demo, K);
		Demo->Foe = TorchDemoFoe(Demo, K, 900.f);
		TorchDemoView(K, 25.f, 430.f, -10.f);
	} });
	Steps.Add({ 10.0f, [](ASoulsKnight* K)
	{
		if (!K->GetLockTarget())
		{
			K->ToggleLockOn();
		}
	} });
	Steps.Add({ 10.5f, [](ASoulsKnight* K) { K->DoChargedAttackStart(); } });
	Steps.Add({ 10.6f, [Demo](ASoulsKnight* K)
	{
		TorchDemoCheck(Demo, TEXT("heavy attack winds up a throw (DoChargedAttackStart hook)"), K->GetAction() == EKnightAction::HeavyAttack);
	} });
	Steps.Add({ 10.95f, [](ASoulsKnight*) { TorchDemoShot(TEXT("06_throw")); } });
	Steps.Add({ 11.35f, [](ASoulsKnight*) { TorchDemoShot(TEXT("07_in_flight")); } });
	Steps.Add({ 13.0f, [Demo](ASoulsKnight* K)
	{
		const ASoulsTorch* Torch = Demo->Torch.Get();
		const ASoulsKnight* Foe = Demo->Foe.Get();
		TorchDemoCheck(Demo, TEXT("the thrown torch landed burning near its target"), Torch && Foe && Torch->GetState() == ESoulsTorchState::Ground
			&& Torch->IsLit() && FVector::Dist2D(Torch->GetActorLocation(), Foe->GetActorLocation()) < 450.f);
		TorchDemoCheck(Demo, TEXT("the thrown torch set its target alight"), USoulsBurnComponent::IsBurning(Foe));
		TorchDemoShot(TEXT("08_landed"));
		if (K->GetLockTarget())
		{
			K->ToggleLockOn();
		}
	} });
	Steps.Add({ 14.5f, [Demo](ASoulsKnight* K)
	{
		TorchDemoStage(Demo, K);
		ASoulsEnemy* Bearer = nullptr;
		int32 Bearers = 0;
		for (TActorIterator<ASoulsEnemy> It(K->GetWorld()); It; ++It)
		{
			const ASoulsTorch* Held = ASoulsTorch::GetHeld(*It);
			if (!It->IsDead() && Held && !Held->IsInMainHand())
			{
				++Bearers;
				Bearer = Bearer ? Bearer : *It;
			}
		}
		// Which posts carry torches is a fixed pick, so a small map can have none: arm one then.
		UE_LOG(LogUnrealSoulslike, Log, TEXT("Torch demo: %d island knights spawned carrying a torch"), Bearers);
		if (!Bearer)
		{
			Bearer = Cast<ASoulsEnemy>(TorchDemoFoe(Demo, K, 380.f, Demo->Foe.Get()));
			USoulsTorchSubsystem::ArmEnemy(Bearer);
		}
		if (Bearer)
		{
			if (AController* Brain = Bearer->GetController())
			{
				Brain->UnPossess();
			}
			Bearer->SetActorLocation(Demo->Origin + Demo->Forward * 380.f + FVector(0.f, 0.f, 5.f), false, nullptr, ETeleportType::TeleportPhysics);
			Bearer->SetActorRotation((-Demo->Forward).Rotation());
		}
		Demo->Bearer = Bearer;
		Demo->BearerTorch = ASoulsTorch::GetHeld(Bearer);
		TorchDemoCheck(Demo, TEXT("the torch-bearer holds a lit torch in its off hand"), Demo->BearerTorch.IsValid() && Demo->BearerTorch->IsLit()
			&& Bearer && Bearer->Shield && !Bearer->Shield->IsVisible());
		TorchDemoView(K, 20.f, 520.f, -8.f);
	} });
	Steps.Add({ 15.5f, [](ASoulsKnight*) { TorchDemoShot(TEXT("09_torch_bearer")); } });
	Steps.Add({ 16.0f, [Demo](ASoulsKnight*)
	{
		if (ASoulsKnight* Bearer = Demo->Bearer.Get())
		{
			FDamageEvent Event;
			Bearer->TakeDamage(5000.f, Event, nullptr, nullptr);
		}
	} });
	Steps.Add({ 17.8f, [Demo](ASoulsKnight*)
	{
		const ASoulsTorch* Torch = Demo->BearerTorch.Get();
		TorchDemoCheck(Demo, TEXT("a slain torch-bearer drops its torch, still burning"), Torch && Torch->GetState() == ESoulsTorchState::Ground && Torch->IsLit());
		TorchDemoShot(TEXT("10_bearer_dropped"));
	} });
	Steps.Add({ 18.0f, [Demo](ASoulsKnight* K) { TorchDemoCloseUp(Demo, K, Demo->BearerTorch.Get()); } });
	Steps.Add({ 18.2f, [](ASoulsKnight*) { TorchDemoShot(TEXT("10b_bearer_fire")); } });
	Steps.Add({ 18.35f, [](ASoulsKnight* K) { TorchDemoCloseUpEnd(K); } });
	Steps.Add({ 18.5f, [Demo](ASoulsKnight* K)
	{
		ASoulsTorch* Wall = nullptr;
		for (TActorIterator<ASoulsTorch> It(K->GetWorld()); It; ++It)
		{
			if (It->IsMounted() && (!Wall || FVector::Dist(It->GetActorLocation(), Demo->Origin) < FVector::Dist(Wall->GetActorLocation(), Demo->Origin)))
			{
				Wall = *It;
			}
		}
		TorchDemoCheck(Demo, TEXT("the map has torches on its walls (rebuild Lvl_Isle if not)"), Wall != nullptr);
		if (!Wall)
		{
			// A stand-in sconce in mid-air ahead, so the rest of the check still runs.
			Wall = ASoulsTorch::SpawnTorch(K->GetWorld(), Demo->Origin);
			if (Wall)
			{
				Wall->Mount(Demo->Origin + Demo->Forward * 260.f + FVector(0.f, 0.f, 100.f), -Demo->Forward);
			}
		}
		Demo->WallTorch = Wall;
		if (Wall)
		{
			const FVector Out = Wall->GetActorUpVector().GetSafeNormal2D().IsNearlyZero() ? -Demo->Forward : Wall->GetActorUpVector().GetSafeNormal2D();
			const FVector Center = Wall->GetActorLocation();
			K->SetActorLocation(FVector(Center.X, Center.Y, Center.Z - 80.f) + Out * 120.f, false, nullptr, ETeleportType::TeleportPhysics);
			K->SetActorRotation((-Out).Rotation());
			TorchDemoView(K, 30.f, 340.f, -5.f);
		}
	} });
	Steps.Add({ 19.6f, [](ASoulsKnight*) { TorchDemoShot(TEXT("11_wall_torch")); } });
	Steps.Add({ 20.0f, [](ASoulsKnight* K) { K->InteractPressed(); } });
	Steps.Add({ 20.3f, [Demo](ASoulsKnight* K)
	{
		const bool bTook = Demo->WallTorch.IsValid() && ASoulsTorch::GetWielded(K) == Demo->WallTorch.Get();
		TorchDemoCheck(Demo, TEXT("Interact takes a torch off the wall"), bTook);
		if (!bTook && Demo->WallTorch.IsValid())
		{
			Demo->WallTorch->GiveTo(K, true);
		}
		TorchDemoView(K, 160.f, 240.f, -10.f);
	} });
	Steps.Add({ 21.2f, [](ASoulsKnight*) { TorchDemoShot(TEXT("12_wall_torch_taken")); } });
	Steps.Add({ 22.0f, [Demo](ASoulsKnight*)
	{
		if (ACameraActor* Camera = Demo->CloseCamera.Get())
		{
			Camera->Destroy();
		}
		UE_LOG(LogUnrealSoulslike, Log, TEXT("Torch demo: %d passed, %d failed"), Demo->Passed, Demo->Failed);
	} });

	// Wait for the player's knight (and a few seconds for the world to settle), then play the steps.
	const TSharedRef<FTimerHandle> Waiter = MakeShared<FTimerHandle>();
	TSharedRef<TArray<FStep>> Script = MakeShared<TArray<FStep>>(MoveTemp(Steps));
	const TWeakObjectPtr<UWorld> WeakWorld(World);
	World->GetTimerManager().SetTimer(*Waiter, [Demo, Waiter, Script, WeakWorld]()
	{
		UWorld* W = WeakWorld.Get();
		ASoulsKnight* Player = TorchDemoPlayer(Demo);
		if (!W || !Player || W->GetTimeSeconds() < 4.f)
		{
			return;
		}
		W->GetTimerManager().ClearTimer(*Waiter);
		UE_LOG(LogUnrealSoulslike, Log, TEXT("Torch demo: starting"));
		const TWeakObjectPtr<ASoulsKnight> WeakPlayer(Player);
		for (const FStep& Step : *Script)
		{
			FTimerHandle Handle;
			const TFunction<void(ASoulsKnight*)> Do = Step.Do;
			W->GetTimerManager().SetTimer(Handle, [WeakPlayer, Do]()
			{
				if (ASoulsKnight* K = WeakPlayer.Get())
				{
					Do(K);
				}
			}, FMath::Max(Step.Time, 0.01f), false);
		}
	}, 0.5f, true);
}
