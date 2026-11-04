#include "SoulsBlood.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/DecalComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/HUD.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "MeshDescription.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "StaticMeshAttributes.h"
#include "UObject/ConstructorHelpers.h"
#include "UnrealClient.h"

#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogSoulsBlood, Log, All);

namespace SoulsBloodTuning
{
	// Pool sizes: when a layer is full the oldest bit is recycled, so a brawl never grows the cost.
	constexpr int32 MaxSpurts = 24;
	constexpr int32 MaxMists = 64;
	constexpr int32 MaxDrops = 448;
	constexpr int32 MaxDecals = 56;     // the oldest goes when a new one needs room
	constexpr float DecalHold = 26.f;   // s before a decal starts to soak away
	constexpr float DecalFade = 5.f;
	constexpr float Hidden = 0.001f;    // idle instances: a speck parked at the last splash
	constexpr int32 SpurtFrames = 8;    // T_BloodSpurtAtlas 4x2
	constexpr int32 MistFrames = 16;    // T_BloodMistAtlas 4x4
	constexpr float Gravity = 1080.f;
}

namespace
{
	FORCEINLINE float SoulsBloodSmooth(float Edge0, float Edge1, float X)
	{
		const float T = FMath::Clamp((X - Edge0) / (Edge1 - Edge0), 0.f, 1.f);
		return T * T * (3.f - 2.f * T);
	}

	// A quad (local X along the sprite's "up", Y across, +Z its face) turned to face the camera, with
	// its up as close to Axis as facing allows.
	FQuat SoulsBloodFacing(const FVector& Center, const FVector& Axis, const FVector& CamLocation, const FVector& CamUp, FVector& OutUp)
	{
		FVector ToCam = CamLocation - Center;
		if (!ToCam.Normalize())
		{
			ToCam = FVector::UpVector;
		}
		FVector Up = Axis - (Axis | ToCam) * ToCam;
		if (!Up.Normalize())
		{
			Up = CamUp - (CamUp | ToCam) * ToCam;
			if (!Up.Normalize())
			{
				Up = FVector::CrossProduct(ToCam, FVector::RightVector).GetSafeNormal();
			}
		}
		OutUp = Up;
		return FRotationMatrix::MakeFromXZ(Up, ToCam).ToQuat();
	}

	UInstancedStaticMeshComponent* SoulsBloodLayer(AActor* Owner, const TCHAR* Name)
	{
		UInstancedStaticMeshComponent* Layer = Owner->CreateDefaultSubobject<UInstancedStaticMeshComponent>(Name);
		Layer->SetupAttachment(Owner->GetRootComponent());
		Layer->SetMobility(EComponentMobility::Movable);
		Layer->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Layer->SetGenerateOverlapEvents(false);
		Layer->SetCanEverAffectNavigation(false);
		Layer->CastShadow = false;
		Layer->bReceivesDecals = false;
		Layer->bAffectDistanceFieldLighting = false;
		Layer->bAffectDynamicIndirectLighting = false;
		Layer->bVisibleInRayTracing = false;
		Layer->NumCustomDataFloats = 3; // flipbook frame, fade 0..1, random seed
		return Layer;
	}
}

// ---------------------------------------------------------------------------------------------------
// FSoulsBlood
// ---------------------------------------------------------------------------------------------------

void FSoulsBlood::Splash(UObject* WorldContext, const FVector& Location, const FVector& Direction, float Intensity)
{
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	if (!World || !World->IsGameWorld() || World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	if (USoulsBloodSubsystem* Blood = World->GetSubsystem<USoulsBloodSubsystem>())
	{
		if (ASoulsBloodPool* Pool = Blood->GetPool())
		{
			Pool->Splash(Location, Direction, Intensity);
		}
	}
}

// ---------------------------------------------------------------------------------------------------
// ASoulsBloodPool
// ---------------------------------------------------------------------------------------------------

ASoulsBloodPool::ASoulsBloodPool()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	// After the camera has moved this frame, so the sprites face where it is now.
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;
	SetCanBeDamaged(false);
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));

	// Hard references, so the packaged game cooks them (tools/build_blood_fx.py makes them).
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> SpurtFinder(TEXT("/Game/Souls/FX/Blood/M_BloodSpurt.M_BloodSpurt"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MistFinder(TEXT("/Game/Souls/FX/Blood/M_BloodMist.M_BloodMist"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> DropFinder(TEXT("/Game/Souls/FX/Blood/M_BloodDrop.M_BloodDrop"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> DecalFinder(TEXT("/Game/Souls/FX/Blood/M_BloodDecal.M_BloodDecal"));
	SpurtMaterial = SpurtFinder.Object;
	MistMaterial = MistFinder.Object;
	DropMaterial = DropFinder.Object;
	DecalMaterial = DecalFinder.Object;

	SpurtLayer = SoulsBloodLayer(this, TEXT("Spurts"));
	MistLayer = SoulsBloodLayer(this, TEXT("Mist"));
	DropLayer = SoulsBloodLayer(this, TEXT("Drops"));
	// The mist is translucent: draw it over the opaque spray.
	MistLayer->TranslucencySortPriority = 1;
}

void ASoulsBloodPool::BuildQuad()
{
	// A 1x1 cm quad in local XY facing +Z; the texture's top is at +X and its right at +Y.
	FMeshDescription Desc;
	FStaticMeshAttributes Attributes(Desc);
	Attributes.Register();

	const FPolygonGroupID Group = Desc.CreatePolygonGroup();
	Attributes.GetPolygonGroupMaterialSlotNames()[Group] = FName(TEXT("Blood"));

	TVertexAttributesRef<FVector3f> Positions = Attributes.GetVertexPositions();
	TVertexInstanceAttributesRef<FVector3f> Normals = Attributes.GetVertexInstanceNormals();
	TVertexInstanceAttributesRef<FVector3f> Tangents = Attributes.GetVertexInstanceTangents();
	TVertexInstanceAttributesRef<float> Signs = Attributes.GetVertexInstanceBinormalSigns();
	TVertexInstanceAttributesRef<FVector4f> Colors = Attributes.GetVertexInstanceColors();
	TVertexInstanceAttributesRef<FVector2f> UVs = Attributes.GetVertexInstanceUVs();

	const FVector2f Corners[4] = { FVector2f(-0.5f, -0.5f), FVector2f(0.5f, -0.5f), FVector2f(0.5f, 0.5f), FVector2f(-0.5f, 0.5f) };
	FVertexInstanceID Instances[4];
	for (int32 Index = 0; Index < 4; ++Index)
	{
		const FVertexID Vertex = Desc.CreateVertex();
		Positions[Vertex] = FVector3f(Corners[Index].X, Corners[Index].Y, 0.f);
		const FVertexInstanceID Instance = Desc.CreateVertexInstance(Vertex);
		Normals[Instance] = FVector3f(0.f, 0.f, 1.f);
		Tangents[Instance] = FVector3f(1.f, 0.f, 0.f);
		Signs[Instance] = 1.f;
		Colors[Instance] = FVector4f(1.f, 1.f, 1.f, 1.f);
		UVs.Set(Instance, 0, FVector2f(Corners[Index].Y + 0.5f, 0.5f - Corners[Index].X));
		Instances[Index] = Instance;
	}
	const FVertexInstanceID TriangleA[3] = { Instances[0], Instances[2], Instances[1] };
	const FVertexInstanceID TriangleB[3] = { Instances[0], Instances[3], Instances[2] };
	Desc.CreateTriangle(Group, TriangleA);
	Desc.CreateTriangle(Group, TriangleB);

	QuadMesh = NewObject<UStaticMesh>(this, TEXT("BloodQuad"), RF_Transient);
	QuadMesh->GetStaticMaterials().Add(FStaticMaterial(SpurtMaterial, TEXT("Blood")));
	UStaticMesh::FBuildMeshDescriptionsParams Params;
	Params.bFastBuild = true;              // the only build a packaged game can do
	Params.bCommitMeshDescription = false;
	Params.bMarkPackageDirty = false;
	Params.bBuildSimpleCollision = false;
	TArray<const FMeshDescription*> Descs;
	Descs.Add(&Desc);
	QuadMesh->BuildFromMeshDescriptions(Descs, Params);
}

void ASoulsBloodPool::Allocate(UInstancedStaticMeshComponent* Layer, TArray<FSoulsBloodBit>& Bits, int32 Count)
{
	Bits.SetNum(Count);
	TArray<FTransform> Transforms;
	Transforms.Init(FTransform(FQuat::Identity, Park, FVector(SoulsBloodTuning::Hidden)), Count);
	Layer->AddInstances(Transforms, false, false, false);
}

void ASoulsBloodPool::BeginPlay()
{
	Super::BeginPlay();
	SetActorLocation(FVector::ZeroVector); // instances are written in the layers' space = world space
	bReady = SpurtMaterial && MistMaterial && DropMaterial && DecalMaterial;
	if (!bReady)
	{
		UE_LOG(LogSoulsBlood, Warning, TEXT("Blood FX art missing under /Game/Souls/FX/Blood (run tools/build_blood_fx.py); no blood."));
		return;
	}
	BuildQuad();
	SpurtLayer->SetStaticMesh(QuadMesh);
	MistLayer->SetStaticMesh(QuadMesh);
	DropLayer->SetStaticMesh(QuadMesh);
	SpurtLayer->SetMaterial(0, SpurtMaterial);
	MistLayer->SetMaterial(0, MistMaterial);
	DropLayer->SetMaterial(0, DropMaterial);
	Allocate(SpurtLayer, Spurts, SoulsBloodTuning::MaxSpurts);
	Allocate(MistLayer, Mists, SoulsBloodTuning::MaxMists);
	Allocate(DropLayer, Drops, SoulsBloodTuning::MaxDrops);
}

FSoulsBloodBit* ASoulsBloodPool::Claim(TArray<FSoulsBloodBit>& Bits, int32& Cursor)
{
	const int32 Num = Bits.Num();
	if (Num == 0)
	{
		return nullptr;
	}
	for (int32 Step = 0; Step < Num; ++Step)
	{
		const int32 Index = (Cursor + Step) % Num;
		if (!Bits[Index].bAlive)
		{
			Cursor = (Index + 1) % Num;
			Bits[Index] = FSoulsBloodBit();
			return &Bits[Index];
		}
	}
	// Full: recycle the next in line (the ring runs oldest-first).
	FSoulsBloodBit* Bit = &Bits[Cursor];
	Cursor = (Cursor + 1) % Num;
	*Bit = FSoulsBloodBit();
	return Bit;
}

void ASoulsBloodPool::ViewBasis()
{
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		if (PC->PlayerCameraManager)
		{
			CamLocation = PC->PlayerCameraManager->GetCameraLocation();
			CamUp = PC->PlayerCameraManager->GetCameraRotation().Quaternion().GetUpVector();
		}
	}
}

bool ASoulsBloodPool::TraceSurface(const FVector& From, const FVector& To, FHitResult& Hit) const
{
	// Ground, walls and props only: never the bodies themselves (pawns), so the spray carries on past them.
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
	FCollisionQueryParams Query(SCENE_QUERY_STAT(SoulsBlood), false, this);
	return GetWorld()->LineTraceSingleByObjectType(Hit, From, To, Objects, Query);
}

void ASoulsBloodPool::TraceDropPath(FSoulsBloodBit& Drop) const
{
	// Follow the drop's (drag-free) arc for a second in a few straight steps; where it first meets
	// something is where it splats.
	constexpr int32 Steps = 5;
	constexpr float Span = 1.1f;
	const FVector Fall(0.f, 0.f, -Drop.Gravity);
	FVector Prev = Drop.Origin;
	float PrevTime = 0.f;
	for (int32 Step = 1; Step <= Steps; ++Step)
	{
		const float Time = Span * Step / Steps;
		const FVector Next = Drop.Origin + Drop.Launch * Time + 0.5f * Fall * Time * Time;
		FHitResult Hit;
		if (TraceSurface(Prev, Next, Hit))
		{
			Drop.ImpactTime = FMath::Lerp(PrevTime, Time, Hit.Time);
			Drop.Impact = Hit.ImpactPoint;
			Drop.ImpactNormal = Hit.ImpactNormal;
			return;
		}
		Prev = Next;
		PrevTime = Time;
	}
}

void ASoulsBloodPool::Splash(const FVector& Location, const FVector& Direction, float Intensity)
{
	if (!bReady)
	{
		return;
	}
	using namespace SoulsBloodTuning;
	const float I = FMath::Clamp(Intensity, 0.25f, 4.f);
	const float RootI = FMath::Sqrt(I);
	FVector Dir = Direction.GetSafeNormal();
	if (Dir.IsNearlyZero())
	{
		Dir = FVector::UpVector;
	}
	FVector Flat = FVector(Dir.X, Dir.Y, 0.f).GetSafeNormal();
	if (Flat.IsNearlyZero())
	{
		Flat = FMath::VRand().GetSafeNormal2D();
	}
	Park = Location;
	ViewBasis();

	// The ground under the wound: where the fine drops vanish, and a pool of blood a moment later.
	float GroundZ = Location.Z - 250.f;
	FHitResult Hit;
	if (TraceSurface(Location + FVector(0.f, 0.f, 20.f), Location - FVector(0.f, 0.f, 600.f), Hit))
	{
		GroundZ = Hit.ImpactPoint.Z;
	}
	const FVector PoolFrom = Location + Flat * (20.f + 15.f * I);
	if (TraceSurface(PoolFrom + FVector(0.f, 0.f, 40.f), PoolFrom - FVector(0.f, 0.f, 600.f), Hit))
	{
		FSoulsBloodPendingDecal Pool;
		Pool.Point = Hit.ImpactPoint;
		Pool.Normal = Hit.ImpactNormal;
		Pool.Along = Flat;
		Pool.Radius = (20.f + 15.f * I) * FMath::FRandRange(0.9f, 1.15f);
		Pool.Stretch = 1.f + 0.2f * I;
		Pool.Delay = 0.1f + 0.05f * I;
		PendingDecals.Add(Pool);
	}
	// Kills (and crits) paint the ground around too.
	for (int32 Extra = 0; I >= 2.5f && Extra < 2; ++Extra)
	{
		const FVector From = Location + Flat * FMath::FRandRange(50.f, 110.f) + FMath::VRand().GetSafeNormal2D() * 35.f;
		if (TraceSurface(From + FVector(0.f, 0.f, 40.f), From - FVector(0.f, 0.f, 600.f), Hit))
		{
			FSoulsBloodPendingDecal Splat;
			Splat.Point = Hit.ImpactPoint;
			Splat.Normal = Hit.ImpactNormal;
			Splat.Along = Flat;
			Splat.Radius = FMath::FRandRange(14.f, 26.f);
			Splat.Stretch = FMath::FRandRange(1.f, 1.8f);
			Splat.Delay = FMath::FRandRange(0.25f, 0.5f);
			PendingDecals.Add(Splat);
		}
	}
	// A wall (or tree, or rock) in the spray's path takes a streak, running the way the spray flew.
	if (TraceSurface(Location, Location + (Dir + FVector(0.f, 0.f, 0.1f)).GetSafeNormal() * (180.f + 90.f * I), Hit) && Hit.ImpactNormal.Z < 0.6f)
	{
		FSoulsBloodPendingDecal Streak;
		Streak.Point = Hit.ImpactPoint;
		Streak.Normal = Hit.ImpactNormal;
		Streak.Along = (Dir - FVector(0.f, 0.f, 0.4f)).GetSafeNormal();
		Streak.Radius = 24.f + 14.f * I;
		Streak.Stretch = 1.6f;
		Streak.Delay = Hit.Distance / 900.f;
		PendingDecals.Add(Streak);
	}

	// The arterial spurt: one jet for a light hit, a fan of them for a crit or a kill.
	const int32 NumSpurts = I < 1.5f ? 1 : (I < 2.5f ? 2 : 3);
	for (int32 Index = 0; Index < NumSpurts; ++Index)
	{
		if (FSoulsBloodBit* Spurt = Claim(Spurts, SpurtCursor))
		{
			Spurt->bAlive = true;
			Spurt->Pos = Location;
			Spurt->Axis = (Dir + FMath::VRand() * (0.12f + 0.25f * Index) + FVector(0.f, 0.f, 0.15f)).GetSafeNormal();
			Spurt->Vel = Spurt->Axis * 40.f;
			Spurt->Size = (36.f + 24.f * I) * FMath::FRandRange(0.85f, 1.15f) * (Index == 0 ? 1.f : 0.75f);
			Spurt->Grow = 1.f;
			Spurt->Life = (0.3f + 0.07f * I) * FMath::FRandRange(0.9f, 1.1f);
			Spurt->Seed = FMath::FRand();
		}
	}

	// Red mist hanging round the wound.
	const int32 NumMist = 2 + FMath::RoundToInt(2.f * I);
	for (int32 Index = 0; Index < NumMist; ++Index)
	{
		if (FSoulsBloodBit* Mist = Claim(Mists, MistCursor))
		{
			Mist->bAlive = true;
			Mist->Pos = Location + FMath::VRand() * 4.f;
			Mist->Vel = Dir * FMath::FRandRange(60.f, 180.f) * RootI + FMath::VRand() * 40.f;
			Mist->Size = FMath::FRandRange(22.f, 38.f) * RootI;
			Mist->Grow = 1.8f;
			Mist->Life = FMath::FRandRange(0.45f, 0.8f);
			Mist->Gravity = 120.f;
			Mist->Drag = 2.5f;
			Mist->Roll = FMath::FRandRange(0.f, 360.f);
			Mist->Seed = FMath::FRand();
		}
	}

	// Droplets: a fine spray and fat gouts that arc and fall. The fat ones are traced so each one
	// splats a decal where it lands, on the ground or anything else in its way.
	const int32 NumDrops = FMath::Min(FMath::RoundToInt(22.f * FMath::Pow(I, 1.35f)), 170);
	const int32 NumGouts = FMath::RoundToInt(2.f + 3.f * I);
	for (int32 Index = 0; Index < NumDrops; ++Index)
	{
		FSoulsBloodBit* Drop = Claim(Drops, DropCursor);
		if (!Drop)
		{
			break;
		}
		const bool bGout = Index < NumGouts;
		const float Roll = FMath::FRand();
		const float Scale = 0.85f + 0.15f * I;
		Drop->bAlive = true;
		Drop->Seed = FMath::FRand();
		Drop->Life = 2.5f;
		Drop->GroundZ = GroundZ;
		Drop->Pos = Location + FMath::VRand() * 3.f;
		float Spread;
		float Speed;
		if (bGout)
		{
			Drop->Size = FMath::FRandRange(2.6f, 4.8f) * Scale;
			Spread = 0.45f;
			Speed = FMath::FRandRange(250.f, 650.f) * RootI;
			Drop->Gravity = Gravity;
			Drop->Drag = 0.f; // drag-free, so the traced arc is the real one
		}
		else if (Roll < 0.3f)
		{
			Drop->Size = FMath::FRandRange(1.2f, 2.4f) * Scale;
			Spread = 0.55f;
			Speed = FMath::FRandRange(280.f, 800.f) * RootI;
			Drop->Gravity = Gravity;
			Drop->Drag = 0.6f;
		}
		else
		{
			Drop->Size = FMath::FRandRange(0.45f, 1.1f) * Scale;
			Spread = 0.7f;
			Speed = FMath::FRandRange(300.f, 950.f) * RootI;
			Drop->Gravity = 900.f;
			Drop->Drag = 1.6f;
		}
		const FVector Out = (Dir + FMath::VRand() * Spread + FVector(0.f, 0.f, FMath::FRandRange(0.05f, 0.6f))).GetSafeNormal();
		Drop->Vel = Out * Speed;
		Drop->Origin = Drop->Pos;
		Drop->Launch = Drop->Vel;
		if (bGout)
		{
			TraceDropPath(*Drop);
		}
	}

	SetActorTickEnabled(true);
	// Show it this very frame.
	Upload(SpurtLayer, Spurts, ESoulsBloodKind::Spurt, bSpurtsLive);
	Upload(MistLayer, Mists, ESoulsBloodKind::Mist, bMistsLive);
	Upload(DropLayer, Drops, ESoulsBloodKind::Drop, bDropsLive);
}

void ASoulsBloodPool::Simulate(TArray<FSoulsBloodBit>& Bits, ESoulsBloodKind Kind, float DeltaSeconds)
{
	for (FSoulsBloodBit& Bit : Bits)
	{
		if (!Bit.bAlive)
		{
			continue;
		}
		Bit.Age += DeltaSeconds;
		if (Bit.Age >= Bit.Life)
		{
			Bit.bAlive = false;
			continue;
		}
		if (Kind == ESoulsBloodKind::Drop && Bit.ImpactTime >= 0.f && Bit.Age >= Bit.ImpactTime)
		{
			// Splat where it hit, a little longer the faster it was going.
			const FVector Hit = Bit.Launch + FVector(0.f, 0.f, -Bit.Gravity) * Bit.ImpactTime;
			FSoulsBloodPendingDecal Splat;
			Splat.Point = Bit.Impact;
			Splat.Normal = Bit.ImpactNormal;
			Splat.Along = Hit.GetSafeNormal();
			Splat.Radius = Bit.Size * FMath::FRandRange(2.4f, 3.8f);
			Splat.Stretch = 1.f + FMath::Min(Hit.Size() / 900.f, 1.f) * 0.8f;
			Splat.Delay = 0.f;
			PendingDecals.Add(Splat);
			Bit.bAlive = false;
			continue;
		}
		if (Kind == ESoulsBloodKind::Drop && Bit.Drag <= 0.f)
		{
			const FVector Fall(0.f, 0.f, -Bit.Gravity);
			Bit.Pos = Bit.Origin + Bit.Launch * Bit.Age + 0.5f * Fall * Bit.Age * Bit.Age;
			Bit.Vel = Bit.Launch + Fall * Bit.Age;
		}
		else
		{
			Bit.Vel.Z -= Bit.Gravity * DeltaSeconds;
			Bit.Vel *= FMath::Max(0.f, 1.f - Bit.Drag * DeltaSeconds);
			Bit.Pos += Bit.Vel * DeltaSeconds;
		}
		if (Kind == ESoulsBloodKind::Drop && Bit.Pos.Z <= Bit.GroundZ)
		{
			Bit.bAlive = false;
		}
	}
}

void ASoulsBloodPool::Upload(UInstancedStaticMeshComponent* Layer, TArray<FSoulsBloodBit>& Bits, ESoulsBloodKind Kind, bool& bWasLive)
{
	using namespace SoulsBloodTuning;
	const int32 Num = Bits.Num();
	if (!Layer || Num == 0 || Layer->GetInstanceCount() != Num)
	{
		return;
	}
	bool bAny = false;
	for (const FSoulsBloodBit& Bit : Bits)
	{
		bAny |= Bit.bAlive;
	}
	if (!bAny && !bWasLive)
	{
		return; // already parked
	}
	bWasLive = bAny;

	TArray<FTransform> Transforms;
	Transforms.SetNumUninitialized(Num);
	TArray<float> Data;
	Data.SetNumZeroed(Num * 3);
	const FTransform Parked(FQuat::Identity, Park, FVector(Hidden));
	for (int32 Index = 0; Index < Num; ++Index)
	{
		const FSoulsBloodBit& Bit = Bits[Index];
		if (!Bit.bAlive)
		{
			Transforms[Index] = Parked;
			continue;
		}
		const float T = FMath::Clamp(Bit.Age / Bit.Life, 0.f, 1.f);
		FVector Up;
		float Frame = 0.f;
		float Fade = 1.f;
		switch (Kind)
		{
		case ESoulsBloodKind::Spurt:
		{
			// The jet shoots out to full length in the first third, then breaks up and thins.
			const float Reach = FMath::Lerp(0.45f, Bit.Grow, 1.f - FMath::Square(1.f - FMath::Min(T / 0.35f, 1.f)));
			const float Length = Bit.Size * Reach;
			const FQuat Facing = SoulsBloodFacing(Bit.Pos, Bit.Axis, CamLocation, CamUp, Up);
			// The sheet's base (bottom centre) sits on the wound.
			Transforms[Index] = FTransform(Facing, Bit.Pos + Up * Length * 0.5f, FVector(Length, Length, 1.f));
			Frame = FMath::Min(FMath::FloorToFloat(T * SpurtFrames), float(SpurtFrames - 1));
			Fade = 1.f - SoulsBloodSmooth(0.6f, 1.f, T);
			break;
		}
		case ESoulsBloodKind::Mist:
		{
			const float Size = Bit.Size * FMath::Lerp(0.6f, Bit.Grow, FMath::Sqrt(T));
			FVector ToCam = (CamLocation - Bit.Pos).GetSafeNormal();
			const FVector Axis = CamUp.RotateAngleAxis(Bit.Roll, ToCam.IsNearlyZero() ? FVector::UpVector : ToCam);
			const FQuat Facing = SoulsBloodFacing(Bit.Pos, Axis, CamLocation, CamUp, Up);
			Transforms[Index] = FTransform(Facing, Bit.Pos, FVector(Size, Size, 1.f));
			// Cell 0 of the sheet is empty: start on 1.
			Frame = FMath::Min(1.f + FMath::FloorToFloat(T * (MistFrames - 1)), float(MistFrames - 1));
			Fade = FMath::Pow(1.f - T, 1.5f);
			break;
		}
		case ESoulsBloodKind::Drop:
		default:
		{
			// Streaked along its flight.
			const float Speed = Bit.Vel.Size();
			const float Stretch = FMath::Clamp(1.f + Speed / 300.f, 1.f, 4.5f);
			const FVector Axis = Speed > 1.f ? Bit.Vel / Speed : CamUp;
			const FQuat Facing = SoulsBloodFacing(Bit.Pos, Axis, CamLocation, CamUp, Up);
			Transforms[Index] = FTransform(Facing, Bit.Pos, FVector(Bit.Size * Stretch, Bit.Size, 1.f));
			Fade = 1.f - SoulsBloodSmooth(0.85f, 1.f, T);
			break;
		}
		}
		Data[Index * 3 + 0] = Frame;
		Data[Index * 3 + 1] = Fade;
		Data[Index * 3 + 2] = Bit.Seed;
	}
	Layer->BatchUpdateInstancesTransforms(0, Transforms, false, false, true);
	Layer->SetCustomData(0, Num - 1, Data, false);
}

void ASoulsBloodPool::SpawnDecal(const FSoulsBloodPendingDecal& Pending)
{
	using namespace SoulsBloodTuning;
	if (!DecalMaterial)
	{
		return;
	}
	Decals.RemoveAll([](const TWeakObjectPtr<UDecalComponent>& Decal) { return !Decal.IsValid(); });
	while (Decals.Num() >= MaxDecals)
	{
		if (UDecalComponent* Oldest = Decals[0].Get())
		{
			Oldest->DestroyComponent();
		}
		Decals.RemoveAt(0);
	}

	// Projects along its X axis, into the surface; Z runs along the spray.
	const FVector Into = -Pending.Normal.GetSafeNormal();
	FVector Along = Pending.Along - (Pending.Along | Into) * Into;
	if (!Along.Normalize())
	{
		Along = FVector::CrossProduct(Into, FVector::RightVector);
		if (!Along.Normalize())
		{
			Along = FVector::CrossProduct(Into, FVector::ForwardVector).GetSafeNormal();
		}
	}
	if (Pending.Stretch < 1.2f)
	{
		Along = Along.RotateAngleAxis(FMath::FRandRange(0.f, 360.f), Into); // round splats: any way up
	}
	const FRotator Rotation = FRotationMatrix::MakeFromXZ(Into, Along).Rotator();

	UDecalComponent* Decal = NewObject<UDecalComponent>(this);
	Decal->SetUsingAbsoluteLocation(true);
	Decal->SetUsingAbsoluteRotation(true);
	Decal->SetUsingAbsoluteScale(true);
	Decal->SetDecalMaterial(DecalMaterial);
	Decal->DecalSize = FVector(FMath::Max(8.f, Pending.Radius * 0.5f), Pending.Radius, Pending.Radius * Pending.Stretch);
	// R: how dark (fresher blood on top is a touch brighter); A: which of the sheet's four splats.
	Decal->DecalColor = FLinearColor(FMath::FRandRange(0.75f, 1.1f), 0.f, 0.f, float(FMath::RandRange(0, 3)));
	DecalSort = (DecalSort + 1) % 10000;
	Decal->SortOrder = DecalSort;
	Decal->FadeScreenSize = 0.0005f;
	Decal->SetupAttachment(GetRootComponent());
	Decal->RegisterComponent();
	Decal->SetWorldLocationAndRotation(Pending.Point, Rotation);
	Decal->SetFadeIn(0.f, 0.08f);
	Decal->SetFadeOut(DecalHold * FMath::FRandRange(0.9f, 1.1f), DecalFade, false);
	Decals.Add(Decal);
}

void ASoulsBloodPool::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const float Dt = FMath::Min(DeltaSeconds, 0.05f);
	ViewBasis();
	Simulate(Spurts, ESoulsBloodKind::Spurt, Dt);
	Simulate(Mists, ESoulsBloodKind::Mist, Dt);
	Simulate(Drops, ESoulsBloodKind::Drop, Dt);
	for (int32 Index = PendingDecals.Num() - 1; Index >= 0; --Index)
	{
		PendingDecals[Index].Delay -= Dt;
		if (PendingDecals[Index].Delay <= 0.f)
		{
			const FSoulsBloodPendingDecal Ready = PendingDecals[Index];
			PendingDecals.RemoveAtSwap(Index);
			SpawnDecal(Ready);
		}
	}
	Upload(SpurtLayer, Spurts, ESoulsBloodKind::Spurt, bSpurtsLive);
	Upload(MistLayer, Mists, ESoulsBloodKind::Mist, bMistsLive);
	Upload(DropLayer, Drops, ESoulsBloodKind::Drop, bDropsLive);
	if (!bSpurtsLive && !bMistsLive && !bDropsLive && PendingDecals.Num() == 0)
	{
		SetActorTickEnabled(false); // idle until the next hit (decals fade on their own)
	}
}

int32 ASoulsBloodPool::NumLiveDecals() const
{
	int32 Count = 0;
	for (const TWeakObjectPtr<UDecalComponent>& Decal : Decals)
	{
		Count += Decal.IsValid() ? 1 : 0;
	}
	return Count;
}

int32 ASoulsBloodPool::NumLiveParticles() const
{
	int32 Count = 0;
	for (const TArray<FSoulsBloodBit>* Bits : { &Spurts, &Mists, &Drops })
	{
		for (const FSoulsBloodBit& Bit : *Bits)
		{
			Count += Bit.bAlive ? 1 : 0;
		}
	}
	return Count;
}

// ---------------------------------------------------------------------------------------------------
// USoulsBloodSubsystem (+ the -BloodDemo check)
// ---------------------------------------------------------------------------------------------------

USoulsBloodSubsystem* USoulsBloodSubsystem::Get(const UObject* WorldContext)
{
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	return World ? World->GetSubsystem<USoulsBloodSubsystem>() : nullptr;
}

bool USoulsBloodSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

ASoulsBloodPool* USoulsBloodSubsystem::GetPool()
{
	UWorld* World = GetWorld();
	if (IsValid(Pool) || !World || World->GetNetMode() == NM_DedicatedServer)
	{
		return IsValid(Pool) ? Pool.Get() : nullptr;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.ObjectFlags |= RF_Transient;
	Pool = World->SpawnActor<ASoulsBloodPool>(ASoulsBloodPool::StaticClass(), FTransform::Identity, Params);
	return Pool;
}

void USoulsBloodSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (InWorld.GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	GetPool(); // now, so the first hit doesn't pay for it
	bDemo = FParse::Param(FCommandLine::Get(), TEXT("BloodDemo"));
	if (bDemo)
	{
		FParse::Value(FCommandLine::Get(), TEXT("BloodDemoWarmup="), DemoWarmup);
		DemoStart = FPlatformTime::Seconds();
		StepStart = DemoStart;
		UE_LOG(LogSoulsBlood, Display, TEXT("BloodDemo: armed (warm-up %.0f s), art %s"), DemoWarmup,
			Pool && Pool->HasArt() ? TEXT("loaded") : TEXT("MISSING - run tools/build_blood_fx.py"));
	}
}

void USoulsBloodSubsystem::DemoFrame(const FVector& From, const FVector& At, float Fov)
{
	UWorld* World = GetWorld();
	APlayerController* PC = World->GetFirstPlayerController();
	if (!PC)
	{
		return;
	}
	if (!DemoCamera)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		DemoCamera = World->SpawnActor<ACameraActor>(From, (At - From).Rotation(), Params);
		DemoCamera->GetCameraComponent()->bConstrainAspectRatio = false;
	}
	DemoCamera->SetActorLocationAndRotation(From, (At - From).Rotation());
	DemoCamera->GetCameraComponent()->SetFieldOfView(Fov);
	PC->SetViewTarget(DemoCamera);
	if (PC->MyHUD)
	{
		PC->MyHUD->bShowHUD = false;
	}
}

void USoulsBloodSubsystem::DemoShot(const TCHAR* Name)
{
	const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Screenshots") / (FString(Name) + TEXT(".png")));
	FScreenshotRequest::RequestScreenshot(Path, false, false);
	UE_LOG(LogSoulsBlood, Display, TEXT("BloodDemo: shot %s -> %s (live bits %d, decals %d)"), Name, *Path,
		Pool ? Pool->NumLiveParticles() : 0, Pool ? Pool->NumLiveDecals() : 0);
}

void USoulsBloodSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	UWorld* World = GetWorld();
	if (!bDemo || !World)
	{
		return;
	}
	// Real time, not game time: the spurts are shot in slow motion.
	const double Now = FPlatformTime::Seconds();
	const double Since = Now - StepStart;
	auto Next = [this, Now]() { ++DemoStep; StepStart = Now; };

	switch (DemoStep)
	{
	case 0:
	{
		bool bCompiling = false;
#if WITH_EDITOR
		bCompiling = GShaderCompilingManager && GShaderCompilingManager->GetNumRemainingJobs() > 0;
#endif
		APlayerController* PC = World->GetFirstPlayerController();
		APawn* Pawn = PC ? PC->GetPawn() : nullptr;
		if (Now - DemoStart < DemoWarmup || bCompiling || !Pawn)
		{
			return;
		}
		DemoForward = Pawn->GetActorForwardVector().GetSafeNormal2D();
		if (DemoForward.IsNearlyZero())
		{
			DemoForward = FVector::ForwardVector;
		}
		DemoRight = FVector::CrossProduct(FVector::UpVector, DemoForward);
		DemoPoint = Pawn->GetActorLocation() + DemoForward * 260.f + FVector(0.f, 0.f, 20.f);
		DemoGround = DemoPoint - FVector(0.f, 0.f, 110.f);
		FHitResult Hit;
		FCollisionQueryParams Query(SCENE_QUERY_STAT(SoulsBloodDemo), false, Pawn);
		if (World->LineTraceSingleByChannel(Hit, DemoPoint + FVector(0.f, 0.f, 300.f), DemoPoint - FVector(0.f, 0.f, 800.f), ECC_Visibility, Query))
		{
			DemoGround = Hit.ImpactPoint;
			DemoPoint.Z = DemoGround.Z + 110.f; // chest height over the ground there
		}
		DemoFrame(DemoPoint + DemoRight * 280.f - DemoForward * 40.f + FVector(0.f, 0.f, 35.f), DemoPoint + DemoForward * 60.f, 55.f);
		UGameplayStatics::SetGlobalTimeDilation(World, 0.25f);
		UE_LOG(LogSoulsBlood, Display, TEXT("BloodDemo: staged at %s (ground %s), art %s"), *DemoPoint.ToCompactString(), *DemoGround.ToCompactString(),
			Pool && Pool->HasArt() ? TEXT("loaded") : TEXT("MISSING"));
		Next();
		break;
	}
	case 1:
		if (Since >= 1.0)
		{
			FSoulsBlood::Splash(World, DemoPoint, DemoForward + FVector(0.f, 0.f, 0.35f), 1.f);
			UE_LOG(LogSoulsBlood, Display, TEXT("BloodDemo: light splash (1)"));
			Next();
		}
		break;
	case 2:
		if (Since >= 0.35)
		{
			DemoShot(TEXT("BloodDemo_1_Light"));
			Next();
		}
		break;
	case 3:
		if (Since >= 2.0)
		{
			FSoulsBlood::Splash(World, DemoPoint, DemoForward - DemoRight * 0.25f + FVector(0.f, 0.f, 0.25f), 2.f);
			UE_LOG(LogSoulsBlood, Display, TEXT("BloodDemo: heavy splash (2)"));
			Next();
		}
		break;
	case 4:
		if (Since >= 0.35)
		{
			DemoShot(TEXT("BloodDemo_2_Heavy"));
			Next();
		}
		break;
	case 5:
		if (Since >= 2.0)
		{
			FSoulsBlood::Splash(World, DemoPoint, DemoForward + FVector(0.f, 0.f, 0.55f), 3.f);
			UE_LOG(LogSoulsBlood, Display, TEXT("BloodDemo: critical splash (3)"));
			Next();
		}
		break;
	case 6:
		if (Since >= 0.3)
		{
			DemoShot(TEXT("BloodDemo_3_Critical"));
			Next();
		}
		break;
	case 7:
		if (Since >= 0.9)
		{
			DemoShot(TEXT("BloodDemo_3_Critical_Drops"));
			Next();
		}
		break;
	case 8:
		if (Since >= 0.5)
		{
			UGameplayStatics::SetGlobalTimeDilation(World, 1.f);
			Next();
		}
		break;
	case 9:
		if (Since >= 3.0)
		{
			DemoShot(TEXT("BloodDemo_4_Decals_Side"));
			Next();
		}
		break;
	case 10:
		if (Since >= 0.5)
		{
			const FVector At = DemoGround + DemoForward * 100.f;
			DemoFrame(At + FVector(0.f, 0.f, 330.f) - DemoForward * 150.f + DemoRight * 60.f, At, 60.f);
			Next();
		}
		break;
	case 11:
		if (Since >= 2.0)
		{
			DemoShot(TEXT("BloodDemo_5_Decals_Top"));
			Next();
		}
		break;
	case 12:
		if (Since >= 1.5)
		{
			const int32 NumDecals = Pool ? Pool->NumLiveDecals() : 0;
			UE_LOG(LogSoulsBlood, Display, TEXT("BloodDemo: %s, %d decals on the ground"), NumDecals > 0 ? TEXT("PASS") : TEXT("FAIL (no decals)"), NumDecals);
			UE_LOG(LogSoulsBlood, Display, TEXT("BLOOD_DEMO_OK"));
			bDemo = false;
			FPlatformMisc::RequestExit(false, TEXT("BloodDemo"));
		}
		break;
	default:
		break;
	}
}

TStatId USoulsBloodSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USoulsBloodSubsystem, STATGROUP_Tickables);
}
