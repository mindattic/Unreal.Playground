#include "SoulsFire.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/HUD.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PhysicsVolume.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerStart.h"
#include "HAL/PlatformMisc.h"
#include "Kismet/GameplayStatics.h"
#include "LandscapeProxy.h"
#include "IslandDayNight.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "RenderingThread.h"
#include "UnrealClient.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/PackageName.h"
#include "ShaderCompiler.h"
#include "UObject/Package.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogSoulsFire, Log, All);

namespace SoulsFireTuning
{
	// The field: N x N cells over [-Half, Half]^2 (cm). Must match FIRE_FIELD_* in tools/build_island.py.
	constexpr int32 N = 512;
	constexpr float Half = 32768.f;
	constexpr float CellSize = 2.f * Half / N; // 128 cm

	constexpr uint8 FuelUnknown = 0;
	constexpr uint8 FuelNone = 1;
	constexpr uint8 FuelWater = 2;
	constexpr uint8 FuelBurnable = 3;

	constexpr float SimDt = 0.1f;
	// UNR-T75: a grass fire races. Ignition hazard per neighbour per second at full flame, times the wind
	// factor: about 2.6x straight downwind (the front runs ~1.5 m/s), 1x across it, 0.12x into the wind.
	constexpr float SpreadRate = 0.62f;
	constexpr float WindPush = 1.6f;
	constexpr float UpwindFloor = 0.12f;
	// Embers blown ahead: chance per second that a strongly burning cell lights one 2..4 cells downwind.
	constexpr float SpotRate = 0.05f;
	constexpr float FireLife = 60.f;      // a fire stops spreading this long after it started
	constexpr float SandLine = 260.f;     // ground below this (cm) is beach / sea: no fuel
	constexpr float MinGroundNormalZ = 0.78f; // steeper ground is bare rock
	constexpr int32 MaxBurningCells = 6000;
	// Visual budget (pooled Niagara components; the nearest / strongest win).
	constexpr int32 PatchCells = 2;        // a flame patch covers 2 x 2 cells (2.56 m)
	constexpr int32 MaxFlamePatches = 90;
	constexpr int32 MaxSmokeColumns = 10;
	constexpr int32 MaxEmberClouds = 10;
	constexpr int32 MaxHazeTiles = 6;
	constexpr int32 MaxLights = 8;
	constexpr float LightTile = 800.f;

	// Cell timeline (seconds after ignition): grass flares fast and burns out behind the front.
	constexpr float Ramp = 1.f;
	constexpr float Decay = 2.5f;
	constexpr float EmberTime = 15.f;

	// The prevailing wind (XY, unit); the live wind veers about it (USoulsFireSubsystem::WindNow).
	const FVector2D Wind(0.6f, 0.8f);

	const TCHAR* FieldPath = TEXT("/Game/Souls/FX/T_FireField.T_FireField");
	const TCHAR* FlameFxPath = TEXT("/Game/Souls/FX/Fire/NS_FieldFire.NS_FieldFire");
	const TCHAR* SmokeFxPath = TEXT("/Game/Souls/FX/Fire/NS_FieldSmoke.NS_FieldSmoke");
	const TCHAR* EmberFxPath = TEXT("/Game/Souls/FX/Fire/NS_FieldEmbers.NS_FieldEmbers");
	const TCHAR* HazeFxPath = TEXT("/Game/Souls/FX/Fire/NS_FireHaze.NS_FireHaze");

	float Smooth(float A, float B, float X)
	{
		const float T = FMath::Clamp((X - A) / FMath::Max(B - A, 1e-4f), 0.f, 1.f);
		return T * T * (3.f - 2.f * T);
	}

	uint32 Hash(uint32 X)
	{
		X ^= X >> 16; X *= 0x7feb352dU; X ^= X >> 15; X *= 0x846ca68bU; X ^= X >> 16;
		return X;
	}

	float Hash01(uint32 X) { return (Hash(X) & 0xFFFFFF) / float(0xFFFFFF); }

	uint8 To8(float V) { return (uint8)FMath::RoundToInt(FMath::Clamp(V, 0.f, 1.f) * 255.f); }

	bool Classify(const FString& MeshName, ESoulsFireItemKind& Out)
	{
		const FString N_ = MeshName.ToLower();
		if (N_.Contains(TEXT("dead_tree")) || N_.Contains(TEXT("trunk")) || N_.Contains(TEXT("driftwood")))
		{
			Out = ESoulsFireItemKind::Log;
		}
		else if (N_.Contains(TEXT("tree")))
		{
			Out = ESoulsFireItemKind::Tree;
		}
		else if (N_.Contains(TEXT("shrub")) || N_.Contains(TEXT("bush")))
		{
			Out = ESoulsFireItemKind::Shrub;
		}
		else if (N_.Contains(TEXT("fern")) || N_.Contains(TEXT("bracken")))
		{
			Out = ESoulsFireItemKind::Fern;
		}
		else if (N_.Contains(TEXT("dandelion")) || N_.Contains(TEXT("flower")))
		{
			Out = ESoulsFireItemKind::Flower;
		}
		else
		{
			return false;
		}
		return true;
	}

	float KindLoad(ESoulsFireItemKind K)
	{
		switch (K)
		{
		case ESoulsFireItemKind::Tree: return 1.2f;
		case ESoulsFireItemKind::Log: return 0.8f;
		case ESoulsFireItemKind::Shrub: return 0.6f;
		case ESoulsFireItemKind::Fern: return 0.25f;
		default: return 0.08f;
		}
	}

	float KindDelay(ESoulsFireItemKind K)
	{
		switch (K)
		{
		case ESoulsFireItemKind::Tree: return 3.f;
		case ESoulsFireItemKind::Log: return 2.5f;
		case ESoulsFireItemKind::Shrub: return 1.4f;
		case ESoulsFireItemKind::Fern: return 0.6f;
		default: return 0.3f;
		}
	}
}

// ------------------------------------------------------------------------------------------------
// FSoulsFire (static API)
// ------------------------------------------------------------------------------------------------

void FSoulsFire::IgniteAt(UObject* WorldContext, const FVector& Location, float Radius)
{
	if (USoulsFireSubsystem* Fire = USoulsFireSubsystem::Get(WorldContext))
	{
		Fire->Ignite(Location, Radius);
	}
}

bool FSoulsFire::IsBurningAt(UObject* WorldContext, const FVector& Location)
{
	return FlameIntensityAt(WorldContext, Location) > 0.2f;
}

float FSoulsFire::FlameIntensityAt(UObject* WorldContext, const FVector& Location)
{
	const USoulsFireSubsystem* Fire = USoulsFireSubsystem::Get(WorldContext);
	return Fire ? Fire->IntensityAt(Location) : 0.f;
}

bool FSoulsFire::CanBurnAt(UObject* WorldContext, const FVector& Location)
{
	USoulsFireSubsystem* Fire = USoulsFireSubsystem::Get(WorldContext);
	return Fire && Fire->CanBurn(Location);
}

// ------------------------------------------------------------------------------------------------
// Subsystem: setup
// ------------------------------------------------------------------------------------------------

USoulsFireSubsystem* USoulsFireSubsystem::Get(const UObject* WorldContext)
{
	UWorld* World = (WorldContext && GEngine) ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	return World ? World->GetSubsystem<USoulsFireSubsystem>() : nullptr;
}

bool USoulsFireSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void USoulsFireSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	bDemo = InWorld.IsGameWorld() && FParse::Param(FCommandLine::Get(), TEXT("FireDemo"));
	if (bDemo)
	{
		UE_LOG(LogSoulsFire, Display, TEXT("FireDemo: armed"));
	}
}

void USoulsFireSubsystem::Deinitialize()
{
	if (bFieldTouched && FieldTexture)
	{
		// The field texture is a shared asset: leave it blank for the editor and the next session.
		FMemory::Memzero(Pixels.GetData(), Pixels.Num() * sizeof(FColor));
		DirtyMinX = DirtyMinY = 0;
		DirtyMaxX = DirtyMaxY = SoulsFireTuning::N - 1;
		UploadField();
		FlushRenderingCommands();
	}
	Super::Deinitialize();
}

TStatId USoulsFireSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USoulsFireSubsystem, STATGROUP_Tickables);
}

void USoulsFireSubsystem::EnsureStarted()
{
	if (bStarted)
	{
		return;
	}
	bStarted = true;
	using namespace SoulsFireTuning;
	const int32 Count = N * N;
	Fuel.SetNumZeroed(Count);
	Load.Init(1.f, Count);
	GroundZ.SetNumZeroed(Count);
	IgniteTime.Init(-1.f, Count);
	FireStart.Init(0.f, Count);
	InActive.SetNumZeroed(Count);
	Pixels.SetNumZeroed(Count);
	Rng.Initialize(0x51F1E);

	LoadAssets();
	IndexVegetation();
	SpawnVisuals();
	ClearField();
}

void USoulsFireSubsystem::LoadAssets()
{
	using namespace SoulsFireTuning;
	FieldTexture = LoadObject<UTexture2D>(nullptr, FieldPath, {}, LOAD_NoWarn);
	if (FieldTexture)
	{
		if (FieldTexture->GetSizeX() != N || FieldTexture->GetSizeY() != N)
		{
			UE_LOG(LogSoulsFire, Warning, TEXT("T_FireField is %dx%d, expected %d: no char on the ground"), FieldTexture->GetSizeX(), FieldTexture->GetSizeY(), N);
			FieldTexture = nullptr;
		}
#if WITH_EDITOR
		else if (FieldTexture->IsStreamable())
		{
			FieldTexture->TemporarilyDisableStreaming();
		}
#endif
	}
	else
	{
		UE_LOG(LogSoulsFire, Warning, TEXT("T_FireField missing (rebuild the island with tools/build_island.py): flames only, no char"));
	}
	// Loaded here, on the first ignition in play (never from a constructor: see ASoulsTorch::HasFireFx).
	FlameSystem = LoadObject<UNiagaraSystem>(nullptr, FlameFxPath, {}, LOAD_NoWarn | LOAD_Quiet);
	SmokeSystem = LoadObject<UNiagaraSystem>(nullptr, SmokeFxPath, {}, LOAD_NoWarn | LOAD_Quiet);
	EmberSystem = LoadObject<UNiagaraSystem>(nullptr, EmberFxPath, {}, LOAD_NoWarn | LOAD_Quiet);
	HazeSystem = LoadObject<UNiagaraSystem>(nullptr, HazeFxPath, {}, LOAD_NoWarn | LOAD_Quiet);
	if (!FlameSystem)
	{
		UE_LOG(LogSoulsFire, Warning, TEXT("NS_FieldFire missing (run tools/build_torch_fire.py): the fire chars the ground but shows no flames"));
	}
}

void USoulsFireSubsystem::IndexVegetation()
{
	using namespace SoulsFireTuning;
	UWorld* World = GetWorld();
	TArray<AActor*> Ignore;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (APhysicsVolume* Volume = Cast<APhysicsVolume>(Actor))
		{
			if (Volume->bWaterVolume)
			{
				WaterVolumes.Add(Volume);
			}
			continue;
		}
		if (Actor->IsA<ALandscapeProxy>() || Actor->IsA<APawn>())
		{
			continue; // landscape grass is charred through the field texture, not per instance
		}
		bool bVegetation = false;
		TInlineComponentArray<UStaticMeshComponent*> Comps(Actor);
		for (UStaticMeshComponent* Comp : Comps)
		{
			UStaticMesh* Mesh = Comp ? Comp->GetStaticMesh() : nullptr;
			ESoulsFireItemKind Kind = ESoulsFireItemKind::Fern;
			if (!Mesh || !Classify(Mesh->GetName(), Kind))
			{
				continue;
			}
			bVegetation = true;
			const FBox Box = Mesh->GetBoundingBox();
			auto Add = [&](int32 Instance, const FTransform& Original, const FTransform& World_)
			{
				FItem Item;
				Item.Comp = Comp;
				Item.Instance = Instance;
				Item.Original = Original;
				Item.Kind = Kind;
				Item.Base = World_.TransformPosition(FVector(Box.GetCenter().X, Box.GetCenter().Y, Box.Min.Z));
				Item.Height = FMath::Max((float)(Box.GetSize().Z * FMath::Abs(World_.GetScale3D().Z)), 20.f);
				const int32 Cell = CellOf(Item.Base);
				if (Cell == INDEX_NONE)
				{
					return;
				}
				const int32 Index = Items.Add(Item);
				CellItems.FindOrAdd(Cell).Add(Index);
			};
			if (UInstancedStaticMeshComponent* Ism = Cast<UInstancedStaticMeshComponent>(Comp))
			{
				const FTransform CompXf = Ism->GetComponentTransform();
				for (int32 i = 0; i < Ism->GetInstanceCount(); ++i)
				{
					FTransform Local;
					if (Ism->GetInstanceTransform(i, Local, false))
					{
						Add(i, Local, Local * CompXf);
					}
				}
			}
			else
			{
				Add(INDEX_NONE, Comp->GetComponentTransform(), Comp->GetComponentTransform());
			}
		}
		if (bVegetation)
		{
			Ignore.Add(Actor);
		}
	}
	TraceParams = FCollisionQueryParams(SCENE_QUERY_STAT(SoulsFireGround), false);
	TraceParams.AddIgnoredActors(Ignore);
	UE_LOG(LogSoulsFire, Display, TEXT("Fire: indexed %d burnable plants in %d actors, %d water volumes, field texture %s"),
		Items.Num(), Ignore.Num(), WaterVolumes.Num(), FieldTexture ? TEXT("ok") : TEXT("missing"));
}

void USoulsFireSubsystem::SpawnVisuals()
{
	using namespace SoulsFireTuning;
	UWorld* World = GetWorld();
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.ObjectFlags |= RF_Transient;
	Visuals = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Params);
	if (!Visuals)
	{
		return;
	}
	USceneComponent* Root = NewObject<USceneComponent>(Visuals, TEXT("FireRoot"));
	Root->SetMobility(EComponentMobility::Movable);
	Visuals->SetRootComponent(Root);
	Root->RegisterComponent();

	for (int32 i = 0; i < MaxLights; ++i)
	{
		UPointLightComponent* Light = NewObject<UPointLightComponent>(Visuals);
		Light->SetMobility(EComponentMobility::Movable);
		Light->SetupAttachment(Root);
		Light->SetIntensityUnits(ELightUnits::Candelas);
		Light->SetIntensity(0.f);
		Light->SetLightColor(FLinearColor(1.f, 0.48f, 0.16f));
		Light->SetAttenuationRadius(1600.f);
		Light->SetSourceRadius(60.f);
		Light->SetCastShadows(false);
		Light->SetVisibility(false);
		Light->RegisterComponent();
		Lights.Add(Light);
	}
	LightTarget.Init(0.f, MaxLights);
}

void USoulsFireSubsystem::ClearField()
{
	if (!FieldTexture)
	{
		return;
	}
	FMemory::Memzero(Pixels.GetData(), Pixels.Num() * sizeof(FColor));
	DirtyMinX = DirtyMinY = 0;
	DirtyMaxX = DirtyMaxY = SoulsFireTuning::N - 1;
	bFieldTouched = true;
	UploadField();
}

// ------------------------------------------------------------------------------------------------
// Grid
// ------------------------------------------------------------------------------------------------

int32 USoulsFireSubsystem::CellOf(const FVector& Location) const
{
	using namespace SoulsFireTuning;
	const int32 X = (int32)FMath::FloorToInt((float)((Location.X + Half) / CellSize));
	const int32 Y = (int32)FMath::FloorToInt((float)((Location.Y + Half) / CellSize));
	return (X >= 0 && Y >= 0 && X < N && Y < N) ? Y * N + X : INDEX_NONE;
}

FVector USoulsFireSubsystem::CellCenter(int32 Cell) const
{
	using namespace SoulsFireTuning;
	const int32 X = Cell % N;
	const int32 Y = Cell / N;
	return FVector(-Half + (X + 0.5f) * CellSize, -Half + (Y + 0.5f) * CellSize, GroundZ.IsValidIndex(Cell) ? GroundZ[Cell] : 0.f);
}

uint8 USoulsFireSubsystem::EvaluateCell(int32 Cell)
{
	using namespace SoulsFireTuning;
	if (Fuel[Cell] != FuelUnknown)
	{
		return Fuel[Cell];
	}
	const FVector C = CellCenter(Cell);
	float ItemLoad = 0.f;
	if (const TArray<int32>* List = CellItems.Find(Cell))
	{
		for (const int32 Index : *List)
		{
			ItemLoad += KindLoad(Items[Index].Kind);
		}
	}
	uint8 Result = FuelNone;
	float Z = 0.f;
	FHitResult Hit;
	if (GetWorld()->LineTraceSingleByObjectType(Hit, FVector(C.X, C.Y, 40000.f), FVector(C.X, C.Y, -5000.f),
		FCollisionObjectQueryParams(ECC_WorldStatic), TraceParams))
	{
		Z = (float)Hit.ImpactPoint.Z;
		const bool bLand = Hit.GetActor() && Hit.GetActor()->IsA<ALandscapeProxy>();
		bool bWet = false;
		for (const TWeakObjectPtr<APhysicsVolume>& Water : WaterVolumes)
		{
			if (Water.IsValid() && Water->EncompassesPoint(FVector(C.X, C.Y, Z + 30.f)))
			{
				bWet = true;
				break;
			}
		}
		if (bWet)
		{
			Result = FuelWater;
		}
		else if (bLand && Z > SandLine && Hit.ImpactNormal.Z > MinGroundNormalZ)
		{
			Result = FuelBurnable; // turf, heather, marram, reeds: the landscape grass
		}
		else if (ItemLoad > 0.f && Z > SandLine * 0.5f)
		{
			Result = FuelBurnable; // a shrub or tree standing on rougher ground still burns
		}
	}
	GroundZ[Cell] = Z;
	Load[Cell] = 1.f + ItemLoad;
	Fuel[Cell] = Result;
	return Result;
}

bool USoulsFireSubsystem::IgniteCell(int32 Cell, float InFireStart)
{
	using namespace SoulsFireTuning;
	if (Cell == INDEX_NONE || IgniteTime[Cell] >= 0.f || EvaluateCell(Cell) != FuelBurnable)
	{
		return false;
	}
	IgniteTime[Cell] = Clock;
	FireStart[Cell] = InFireStart;
	if (!InActive[Cell])
	{
		InActive[Cell] = 1;
		Active.Add(Cell);
	}
	if (const TArray<int32>* List = CellItems.Find(Cell))
	{
		for (const int32 Index : *List)
		{
			FItem& Item = Items[Index];
			if (Item.IgniteTime < 0.f && !Item.bDone)
			{
				Item.IgniteTime = Clock + KindDelay(Item.Kind) * Rng.FRandRange(0.6f, 1.4f);
				ActiveItems.Add(Index);
			}
		}
	}
	bActive = true;
	return true;
}

float USoulsFireSubsystem::CellDuration(int32 Cell) const
{
	using namespace SoulsFireTuning;
	// Bare grass is a quick flash; heather, shrubs and trees keep a patch burning longer.
	const float Hold = 2.5f + 4.f * FMath::Min(Load[Cell] - 1.f, 2.f);
	return Ramp + Hold + Decay;
}

float USoulsFireSubsystem::CellIntensity(int32 Cell, float T) const
{
	using namespace SoulsFireTuning;
	if (T < 0.f)
	{
		return 0.f;
	}
	const float End = CellDuration(Cell);
	if (T < Ramp)
	{
		return T / Ramp;
	}
	if (T < End - Decay)
	{
		return 1.f;
	}
	return FMath::Max(0.f, (End - T) / Decay);
}

float USoulsFireSubsystem::ItemBurnTime(ESoulsFireItemKind Kind)
{
	switch (Kind)
	{
	case ESoulsFireItemKind::Tree: return 20.f;
	case ESoulsFireItemKind::Log: return 16.f;
	case ESoulsFireItemKind::Shrub: return 10.f;
	case ESoulsFireItemKind::Fern: return 6.f;
	default: return 4.f;
	}
}

float USoulsFireSubsystem::ItemIntensity(const FItem& Item, float T) const
{
	const float Burn = ItemBurnTime(Item.Kind);
	if (T <= 0.f || T >= Burn)
	{
		return 0.f;
	}
	return FMath::Min(T / 1.5f, 1.f) * FMath::Clamp((Burn - T) / (0.3f * Burn), 0.f, 1.f);
}

// ------------------------------------------------------------------------------------------------
// Queries
// ------------------------------------------------------------------------------------------------

void USoulsFireSubsystem::Ignite(const FVector& Location, float Radius)
{
	EnsureStarted();
	using namespace SoulsFireTuning;
	const int32 Center = CellOf(Location);
	if (Center == INDEX_NONE)
	{
		return;
	}
	const int32 Cx = Center % N;
	const int32 Cy = Center / N;
	const int32 R = FMath::Clamp(FMath::CeilToInt(Radius / CellSize), 0, 24);
	int32 Lit = 0;
	for (int32 Dy = -R; Dy <= R; ++Dy)
	{
		for (int32 Dx = -R; Dx <= R; ++Dx)
		{
			const int32 X = Cx + Dx;
			const int32 Y = Cy + Dy;
			if (X < 0 || Y < 0 || X >= N || Y >= N)
			{
				continue;
			}
			if ((Dx != 0 || Dy != 0) && FMath::Square(Dx * CellSize) + FMath::Square(Dy * CellSize) > FMath::Square(Radius))
			{
				continue;
			}
			Lit += IgniteCell(Y * N + X, Clock) ? 1 : 0;
		}
	}
	UE_LOG(LogSoulsFire, Log, TEXT("Fire: ignite at %s r=%.0f -> %d cells lit"), *Location.ToCompactString(), Radius, Lit);
}

float USoulsFireSubsystem::IntensityAt(const FVector& Location) const
{
	if (!bStarted)
	{
		return 0.f;
	}
	const int32 Cell = CellOf(Location);
	if (Cell == INDEX_NONE || IgniteTime[Cell] < 0.f)
	{
		return 0.f;
	}
	const float Z = (float)Location.Z - GroundZ[Cell];
	if (Z < -150.f || Z > 450.f)
	{
		return 0.f; // above the flames (a glide, a ledge) or under the ground
	}
	return CellIntensity(Cell, Clock - IgniteTime[Cell]);
}

bool USoulsFireSubsystem::CanBurn(const FVector& Location)
{
	EnsureStarted();
	const int32 Cell = CellOf(Location);
	return Cell != INDEX_NONE && EvaluateCell(Cell) == SoulsFireTuning::FuelBurnable;
}

// ------------------------------------------------------------------------------------------------
// Simulation
// ------------------------------------------------------------------------------------------------

void USoulsFireSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (bDemo)
	{
		TickDemo(DeltaTime);
	}
	if (!bActive)
	{
		return;
	}
	Clock += DeltaTime;
	// The wind veers slowly (tens of degrees over a minute) and gusts.
	{
		const float Base = (float)FMath::Atan2(SoulsFireTuning::Wind.Y, SoulsFireTuning::Wind.X);
		const float Veer = FMath::DegreesToRadians(35.f) * FMath::PerlinNoise1D(Clock * 0.035f + 3.7f);
		const float Gust = 1.f + 0.3f * FMath::PerlinNoise1D(Clock * 0.21f + 11.3f);
		WindNow = FVector2D(FMath::Cos(Base + Veer), FMath::Sin(Base + Veer)) * Gust;
	}
	StepAccum += FMath::Min(DeltaTime, 0.25f);
	int32 Steps = 0;
	while (StepAccum >= SoulsFireTuning::SimDt && Steps < 3)
	{
		StepAccum -= SoulsFireTuning::SimDt;
		Step(SoulsFireTuning::SimDt);
		++Steps;
	}
	// Flicker the pooled lights every frame with the fire's breathing (layered noise, never repeating).
	for (int32 i = 0; i < Lights.Num(); ++i)
	{
		if (Lights[i] && LightTarget[i] > 0.f)
		{
			const float F = 0.82f + 0.14f * FMath::PerlinNoise1D(Clock * 1.9f + i * 7.1f) + 0.1f * FMath::PerlinNoise1D(Clock * 7.3f + i * 3.3f)
				+ 0.05f * FMath::PerlinNoise1D(Clock * 17.f + i * 1.3f);
			Lights[i]->SetIntensity(LightTarget[i] * FMath::Max(F, 0.4f));
		}
	}
}

void USoulsFireSubsystem::Step(float Dt)
{
	using namespace SoulsFireTuning;
	int32 Burning = 0;
	const int32 NumActive = Active.Num();
	TArray<int32> Finished;
	for (int32 k = 0; k < NumActive; ++k)
	{
		const int32 Cell = Active[k];
		const float T = Clock - IgniteTime[Cell];
		const float End = CellDuration(Cell);
		const float I = CellIntensity(Cell, T);
		Burning += I > 0.f ? 1 : 0;

		// Field pixel: R char, G heat (glowing embers), B consumed (grass shrinks away).
		const float Char = Smooth(0.3f, 4.f, T);
		const float Gone = Smooth(3.f, End + 2.f, T);
		const float Embers = T > Ramp ? 0.55f * FMath::Exp(-FMath::Max(T - End, 0.f) / 5.f) : 0.f;
		const bool bDoneCell = T > End + EmberTime;
		const float Heat = bDoneCell ? 0.f : FMath::Max(I, Embers);
		const FColor Px(To8(Char), To8(Heat), To8(bDoneCell ? 1.f : Gone), 255);
		if (Px != Pixels[Cell])
		{
			Pixels[Cell] = Px;
			const int32 X = Cell % N;
			const int32 Y = Cell / N;
			DirtyMinX = FMath::Min(DirtyMinX, X);
			DirtyMinY = FMath::Min(DirtyMinY, Y);
			DirtyMaxX = FMath::Max(DirtyMaxX, X);
			DirtyMaxY = FMath::Max(DirtyMaxY, Y);
		}
		if (bDoneCell)
		{
			Finished.Add(Cell);
			continue;
		}

		// Spread to the eight neighbours, far faster downwind, slowing as the fire ages.
		if (I > 0.15f && BurningCells < MaxBurningCells)
		{
			const float Age = Clock - FireStart[Cell];
			const float AgeFactor = 1.f - Smooth(FireLife * 0.6f, FireLife, Age);
			if (AgeFactor <= 0.f)
			{
				continue;
			}
			const int32 Cx = Cell % N;
			const int32 Cy = Cell / N;
			for (int32 Dy = -1; Dy <= 1; ++Dy)
			{
				for (int32 Dx = -1; Dx <= 1; ++Dx)
				{
					const int32 X = Cx + Dx;
					const int32 Y = Cy + Dy;
					if ((Dx == 0 && Dy == 0) || X < 0 || Y < 0 || X >= N || Y >= N)
					{
						continue;
					}
					const int32 Next = Y * N + X;
					if (IgniteTime[Next] >= 0.f)
					{
						continue;
					}
					const FVector2D Dir = FVector2D(float(Dx), float(Dy)).GetSafeNormal();
					const float Diag = (Dx != 0 && Dy != 0) ? 0.7f : 1.f;
					const float WindF = FMath::Max(UpwindFloor, 1.f + WindPush * (float)FVector2D::DotProduct(Dir, WindNow));
					const float P = SpreadRate * I * AgeFactor * Diag * WindF * FMath::Min(0.7f + 0.3f * Load[Cell], 1.6f) * Dt;
					if (Rng.FRand() < P)
					{
						IgniteCell(Next, FireStart[Cell]);
					}
				}
			}
			// Embers blown ahead light spot fires a few cells downwind: the front jumps and runs.
			if (I > 0.6f && Rng.FRand() < SpotRate * AgeFactor * (float)WindNow.Size() * Dt)
			{
				const float Jump = Rng.FRandRange(2.f, 4.5f);
				const float Side = Rng.FRandRange(-1.2f, 1.2f);
				const FVector2D W = WindNow.GetSafeNormal();
				const int32 X = Cx + FMath::RoundToInt(W.X * Jump - W.Y * Side);
				const int32 Y = Cy + FMath::RoundToInt(W.Y * Jump + W.X * Side);
				if (X >= 0 && Y >= 0 && X < N && Y < N)
				{
					IgniteCell(Y * N + X, FireStart[Cell]);
				}
			}
		}
	}
	if (Finished.Num() > 0)
	{
		for (const int32 Cell : Finished)
		{
			InActive[Cell] = 0;
		}
		Active.RemoveAllSwap([this](int32 Cell) { return InActive[Cell] == 0; }, EAllowShrinking::No);
		BurntCells += Finished.Num();
	}
	BurningCells = Burning;

	StepItems();
	UpdateVisuals(Dt);
	UploadField();

	if (Active.Num() == 0 && ActiveItems.Num() == 0)
	{
		bActive = false;
		for (int32 i = 0; i < Lights.Num(); ++i)
		{
			LightTarget[i] = 0.f;
			if (Lights[i])
			{
				Lights[i]->SetVisibility(false);
			}
		}
		UE_LOG(LogSoulsFire, Display, TEXT("Fire: burnt out (%d cells burnt, %d plants consumed)"), BurntCells, ConsumedItems);
	}
}

void USoulsFireSubsystem::StepItems()
{
	for (int32 k = ActiveItems.Num() - 1; k >= 0; --k)
	{
		FItem& Item = Items[ActiveItems[k]];
		const float T = Clock - Item.IgniteTime;
		if (T < 0.f)
		{
			continue;
		}
		UStaticMeshComponent* Comp = Item.Comp.Get();
		if (!Comp)
		{
			ActiveItems.RemoveAtSwap(k, EAllowShrinking::No);
			continue;
		}
		const float Burn = ItemBurnTime(Item.Kind);
		const bool bDone = T > Burn + 3.f;
		// Charring comes from the field texture; the plant itself shrinks into the ground as it burns away.
		const float S = bDone ? 0.001f : FMath::Max(0.001f, 1.f - SoulsFireTuning::Smooth(0.35f * Burn, Burn + 3.f, T));
		if (!bDone && FMath::Abs(S - Item.Scale) < 0.015f)
		{
			continue;
		}
		Item.Scale = S;
		if (Comp->Mobility != EComponentMobility::Movable)
		{
			Comp->SetMobility(EComponentMobility::Movable);
		}
		FTransform Xf = Item.Original;
		Xf.SetScale3D(Item.Original.GetScale3D() * S);
		if (UInstancedStaticMeshComponent* Ism = Cast<UInstancedStaticMeshComponent>(Comp))
		{
			Xf.AddToTranslation(FVector(0.f, 0.f, -(1.f - S) * Item.Height * 0.06f));
			Ism->UpdateInstanceTransform(Item.Instance, Xf, false, false, true);
		}
		else
		{
			Comp->SetWorldScale3D(Xf.GetScale3D());
			if (bDone)
			{
				Comp->SetVisibility(false);
				Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			}
		}
		if (bDone)
		{
			Item.bDone = true;
			++ConsumedItems;
			ActiveItems.RemoveAtSwap(k, EAllowShrinking::No);
		}
	}
}

void USoulsFireSubsystem::UpdateVisuals(float Dt)
{
	using namespace SoulsFireTuning;
	if (!Visuals)
	{
		return;
	}
	const FVector View = ViewLocation();
	struct FTile
	{
		FVector Sum = FVector::ZeroVector;
		float Weight = 0.f;
	};
	TMap<int64, FTile> Tiles;
	auto TileKey = [](const FVector& P, float Size)
	{
		return (int64(FMath::FloorToInt32((float)(P.X / Size))) << 32) ^ int64(uint32(FMath::FloorToInt32((float)(P.Y / Size))));
	};
	auto AddLight = [&](const FVector& P, float W)
	{
		FTile& Tile = Tiles.FindOrAdd(TileKey(P, LightTile));
		Tile.Sum += P * W;
		Tile.Weight += W;
	};

	// Flames: one patch of flipbook fire per 2 x 2 burning cells, taller as the fire matures and where the
	// fuel is heavy; more on the plants burning in it.
	struct FPatch
	{
		float SumZ = 0.f;
		float MaxI = 0.f;
		float MaxLoad = 1.f;
		float Grow = 0.f;
		int32 Cells = 0;
		int32 X = 0;
		int32 Y = 0;
	};
	TMap<int64, FPatch> Patches;
	for (const int32 Cell : Active)
	{
		const float I = CellIntensity(Cell, Clock - IgniteTime[Cell]);
		if (I < 0.03f)
		{
			continue;
		}
		const int32 Px = (Cell % N) / PatchCells;
		const int32 Py = (Cell / N) / PatchCells;
		FPatch& Patch = Patches.FindOrAdd(int64(Py) * N + Px);
		Patch.X = Px;
		Patch.Y = Py;
		Patch.SumZ += GroundZ[Cell];
		Patch.MaxI = FMath::Max(Patch.MaxI, I);
		Patch.MaxLoad = FMath::Max(Patch.MaxLoad, Load[Cell]);
		Patch.Grow = FMath::Max(Patch.Grow, Smooth(0.f, 18.f, Clock - FireStart[Cell]));
		++Patch.Cells;
		AddLight(CellCenter(Cell), I);
	}
	TArray<FFxWant> FlameWants;
	FlameWants.Reserve(Patches.Num() + ActiveItems.Num());
	for (const TPair<int64, FPatch>& Pair : Patches)
	{
		const FPatch& Patch = Pair.Value;
		const float Span = PatchCells * CellSize;
		const FVector P(-Half + (Patch.X + 0.5f) * Span, -Half + (Patch.Y + 0.5f) * Span, Patch.SumZ / Patch.Cells - 6.f);
		const float Cover = FMath::Sqrt(float(Patch.Cells) / float(PatchCells * PatchCells));
		FFxWant& Want = FlameWants.AddDefaulted_GetRef();
		Want.Key = Pair.Key;
		Want.Location = P;
		Want.Scale = FMath::Clamp((0.45f + 0.55f * Patch.MaxI) * (0.6f + 0.4f * Patch.Grow) * (0.75f + 0.25f * Cover)
			* (0.9f + 0.3f * FMath::Min(Patch.MaxLoad - 1.f, 2.f)), 0.25f, 1.8f);
		// Near first; a strong patch counts as a little nearer than a dying one.
		Want.Priority = (float)FVector::Dist(View, P) * (1.25f - 0.25f * Patch.MaxI);
	}
	// Burning plants: the fire climbs them (flames up the trunk and through the crown of a tree).
	constexpr int64 ItemKeyBase = int64(1) << 40;
	for (const int32 Index : ActiveItems)
	{
		const FItem& Item = Items[Index];
		const float I = ItemIntensity(Item, Clock - Item.IgniteTime);
		if (I < 0.05f)
		{
			continue;
		}
		const float H = Item.Height * FMath::Max(Item.Scale, 0.3f);
		auto AddItemFlame = [&](int32 Part, const FVector& At, float Scale)
		{
			FFxWant& Want = FlameWants.AddDefaulted_GetRef();
			Want.Key = ItemKeyBase + int64(Index) * 4 + Part;
			Want.Location = At;
			Want.Scale = FMath::Clamp(Scale * I, 0.2f, 3.f);
			Want.Priority = (float)FVector::Dist(View, At) * (1.2f - 0.2f * I);
		};
		switch (Item.Kind)
		{
		case ESoulsFireItemKind::Tree:
			AddItemFlame(0, Item.Base + FVector(0.f, 0.f, H * 0.1f), H / 380.f);
			AddItemFlame(1, Item.Base + FVector(H * 0.06f, -H * 0.04f, H * 0.4f), H / 320.f);
			AddItemFlame(2, Item.Base + FVector(-H * 0.05f, H * 0.06f, H * 0.62f), H / 360.f);
			AddLight(Item.Base + FVector(0.f, 0.f, H * 0.4f), 4.f * I);
			break;
		case ESoulsFireItemKind::Log:
			AddItemFlame(0, Item.Base, 0.8f);
			AddLight(Item.Base, 1.5f * I);
			break;
		case ESoulsFireItemKind::Shrub:
			AddItemFlame(0, Item.Base, FMath::Max(H, 60.f) / 110.f);
			AddLight(Item.Base, 2.f * I);
			break;
		default:
			AddItemFlame(0, Item.Base, FMath::Max(H, 40.f) / 140.f);
			break;
		}
	}
	FlamesShown = FMath::Min(FlameWants.Num(), MaxFlamePatches);
	UpdatePool(FlamePool, FlameSystem, FlameWants, MaxFlamePatches, 1.8f);

	// Smoke columns and ember clouds over the busiest 8 m tiles (seen from afar: by weight, not distance);
	// heat haze over the burning tiles nearest the camera.
	TArray<FFxWant> SmokeWants;
	TArray<FFxWant> EmberWants;
	TArray<FFxWant> HazeWants;
	for (const TPair<int64, FTile>& Pair : Tiles)
	{
		const FTile& Tile = Pair.Value;
		if (Tile.Weight < 1.2f)
		{
			continue;
		}
		const FVector P = Tile.Sum / Tile.Weight;
		const float Distance = (float)FVector::Dist(View, P);
		FFxWant Want;
		Want.Key = Pair.Key;
		Want.Location = P;
		Want.Scale = FMath::Clamp(0.55f + 0.1f * FMath::Sqrt(Tile.Weight), 0.55f, 1.6f);
		Want.Priority = -Tile.Weight;
		SmokeWants.Add(Want);
		if (Distance < 9000.f)
		{
			Want.Scale = FMath::Clamp(0.6f + 0.08f * FMath::Sqrt(Tile.Weight), 0.6f, 1.4f);
			Want.Priority = Distance - 150.f * Tile.Weight;
			EmberWants.Add(Want);
		}
		if (Distance < 3500.f)
		{
			Want.Location = P + FVector(0.f, 0.f, 60.f);
			Want.Scale = 4.f;
			Want.Priority = Distance;
			HazeWants.Add(Want);
		}
	}
	SmokeShown = FMath::Min(SmokeWants.Num(), MaxSmokeColumns);
	UpdatePool(SmokePool, SmokeSystem, SmokeWants, MaxSmokeColumns, 9.5f);
	UpdatePool(EmberPool, EmberSystem, EmberWants, MaxEmberClouds, 4.5f);
	UpdatePool(HazePool, HazeSystem, HazeWants, MaxHazeTiles, 1.6f);

	// Lights: the brightest few patches of fire each get one pooled light.
	TArray<FTile> Sorted;
	Tiles.GenerateValueArray(Sorted);
	Sorted.Sort([](const FTile& A, const FTile& B) { return A.Weight > B.Weight; });
	LightsLit = 0;
	for (int32 i = 0; i < Lights.Num(); ++i)
	{
		UPointLightComponent* Light = Lights[i];
		if (!Light)
		{
			continue;
		}
		if (i < Sorted.Num() && Sorted[i].Weight > 0.3f)
		{
			Light->SetWorldLocation(Sorted[i].Sum / Sorted[i].Weight + FVector(0.f, 0.f, 140.f));
			LightTarget[i] = 45.f * FMath::Sqrt(FMath::Min(Sorted[i].Weight, 30.f));
			Light->SetVisibility(true);
			++LightsLit;
		}
		else if (LightTarget[i] > 0.f)
		{
			LightTarget[i] = 0.f;
			Light->SetIntensity(0.f);
			Light->SetVisibility(false);
		}
	}
}

void USoulsFireSubsystem::UpdatePool(TArray<FSoulsFireFxSlot>& Pool, UNiagaraSystem* System, TArray<FFxWant>& Wants, int32 Max, float Linger)
{
	if (!System || !Visuals)
	{
		return;
	}
	Wants.Sort([](const FFxWant& A, const FFxWant& B) { return A.Priority < B.Priority; });
	if (Wants.Num() > Max)
	{
		Wants.SetNum(Max);
	}
	TMap<int64, int32> WantIndex;
	WantIndex.Reserve(Wants.Num());
	for (int32 i = 0; i < Wants.Num(); ++i)
	{
		WantIndex.Add(Wants[i].Key, i);
	}
	TArray<bool> Served;
	Served.Init(false, Wants.Num());
	// Local +X downwind: the field systems push their smoke, embers and flames that way.
	const FRotator Facing(0.f, FMath::RadiansToDegrees(FMath::Atan2(WindNow.Y, WindNow.X)), 0.f);
	auto Place = [&Facing](UNiagaraComponent* Comp, const FFxWant& Want)
	{
		Comp->SetWorldLocationAndRotation(Want.Location, Facing);
		Comp->SetWorldScale3D(FVector(Want.Scale));
	};
	for (FSoulsFireFxSlot& Slot : Pool)
	{
		if (!Slot.bOn || !Slot.Comp)
		{
			continue;
		}
		if (const int32* Found = WantIndex.Find(Slot.Key))
		{
			Place(Slot.Comp, Wants[*Found]);
			Served[*Found] = true;
		}
		else
		{
			Slot.Comp->Deactivate();
			Slot.bOn = false;
			Slot.Key = -1;
			Slot.FreeAt = Clock + Linger;
		}
	}
	int32 Search = 0;
	for (int32 i = 0; i < Wants.Num(); ++i)
	{
		if (Served[i])
		{
			continue;
		}
		FSoulsFireFxSlot* Free = nullptr;
		for (; Search < Pool.Num(); ++Search)
		{
			FSoulsFireFxSlot& Slot = Pool[Search];
			if (Slot.Comp && !Slot.bOn && Clock >= Slot.FreeAt)
			{
				Free = &Slot;
				++Search;
				break;
			}
		}
		if (!Free)
		{
			// Room for the ones still burning out, then no more.
			if (Pool.Num() >= Max + Max / 2)
			{
				break;
			}
			UNiagaraComponent* Comp = NewObject<UNiagaraComponent>(Visuals);
			Comp->bAutoActivate = false;
			Comp->SetAsset(System);
			Comp->SetupAttachment(Visuals->GetRootComponent());
			Comp->SetCastShadow(false);
			Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Comp->RegisterComponent();
			FSoulsFireFxSlot& Added = Pool.AddDefaulted_GetRef();
			Added.Comp = Comp;
			Free = &Added;
			Search = Pool.Num();
		}
		Free->Key = Wants[i].Key;
		Free->bOn = true;
		Place(Free->Comp, Wants[i]);
		Free->Comp->Activate(true);
	}
}

FVector USoulsFireSubsystem::ViewLocation() const
{
	if (const UWorld* World = GetWorld())
	{
		if (const APlayerController* Player = World->GetFirstPlayerController(); Player && Player->PlayerCameraManager)
		{
			return Player->PlayerCameraManager->GetCameraLocation();
		}
	}
	return DemoSpot;
}

FVector USoulsFireSubsystem::BurningCentroid() const
{
	FVector Sum = FVector::ZeroVector;
	float Weight = 0.f;
	for (const int32 Cell : Active)
	{
		const float I = CellIntensity(Cell, Clock - IgniteTime[Cell]);
		if (I > 0.3f)
		{
			Sum += CellCenter(Cell) * I;
			Weight += I;
		}
	}
	return Weight > 0.f ? Sum / Weight : DemoSpot;
}

void USoulsFireSubsystem::UploadField()
{
	using namespace SoulsFireTuning;
	if (!FieldTexture || DirtyMaxX < DirtyMinX || DirtyMaxY < DirtyMinY)
	{
		DirtyMinX = DirtyMinY = MAX_int32;
		DirtyMaxX = DirtyMaxY = -1;
		return;
	}
	bFieldTouched = true;
	const int32 W = DirtyMaxX - DirtyMinX + 1;
	const int32 H = DirtyMaxY - DirtyMinY + 1;
	// The render thread reads the data later: hand it its own copy (freed by the cleanup callback).
	uint8* Copy = static_cast<uint8*>(FMemory::Malloc(SIZE_T(W) * H * sizeof(FColor)));
	for (int32 Row = 0; Row < H; ++Row)
	{
		FMemory::Memcpy(Copy + SIZE_T(Row) * W * sizeof(FColor), &Pixels[(DirtyMinY + Row) * N + DirtyMinX], W * sizeof(FColor));
	}
	FUpdateTextureRegion2D* Region = new FUpdateTextureRegion2D(DirtyMinX, DirtyMinY, 0, 0, W, H);
	FieldTexture->UpdateTextureRegions(0, 1, Region, W * sizeof(FColor), sizeof(FColor), Copy,
		[](uint8* Data, const FUpdateTextureRegion2D* Regions)
		{
			FMemory::Free(Data);
			delete Regions;
		});
	DirtyMinX = DirtyMinY = MAX_int32;
	DirtyMaxX = DirtyMaxY = -1;
}

// ------------------------------------------------------------------------------------------------
// -FireDemo
// ------------------------------------------------------------------------------------------------

bool USoulsFireSubsystem::PickDemoSpot(FVector& OutSpot)
{
	using namespace SoulsFireTuning;
	UWorld* World = GetWorld();
	FVector Origin = FVector::ZeroVector;
	if (APawn* Pawn = UGameplayStatics::GetPlayerPawn(World, 0))
	{
		Origin = Pawn->GetActorLocation();
	}
	else
	{
		for (TActorIterator<APlayerStart> It(World); It; ++It)
		{
			Origin = It->GetActorLocation();
			break;
		}
	}
	EnsureStarted();
	auto Patch = [&](int32 Cell, int32 R)
	{
		int32 Good = 0, Total = 0;
		const int32 Cx = Cell % N, Cy = Cell / N;
		for (int32 Dy = -R; Dy <= R; ++Dy)
		{
			for (int32 Dx = -R; Dx <= R; ++Dx)
			{
				const int32 X = Cx + Dx, Y = Cy + Dy;
				++Total;
				if (X >= 0 && Y >= 0 && X < N && Y < N && EvaluateCell(Y * N + X) == FuelBurnable)
				{
					++Good;
				}
			}
		}
		return float(Good) / Total;
	};
	auto PlantsNear = [&](int32 Cell, int32 R)
	{
		int32 Count = 0;
		const int32 Cx = Cell % N, Cy = Cell / N;
		for (int32 Dy = -R; Dy <= R; ++Dy)
		{
			for (int32 Dx = -R; Dx <= R; ++Dx)
			{
				const int32 X = Cx + Dx, Y = Cy + Dy;
				if (X >= 0 && Y >= 0 && X < N && Y < N)
				{
					const TArray<int32>* List = CellItems.Find(Y * N + X);
					Count += List ? List->Num() : 0;
				}
			}
		}
		return Count;
	};
	int32 Best = INDEX_NONE;
	float BestScore = -1.f;
	int32 Candidates = 0;
	for (float R = 600.f; R <= 15000.f && Candidates < 14; R += 300.f)
	{
		const int32 Steps = FMath::Clamp(FMath::RoundToInt(2.f * UE_PI * R / 600.f), 8, 64);
		for (int32 s = 0; s < Steps && Candidates < 14; ++s)
		{
			const float A = 2.f * UE_PI * s / Steps;
			const int32 Cell = CellOf(Origin + FVector(FMath::Cos(A) * R, FMath::Sin(A) * R, 0.f));
			if (Cell == INDEX_NONE || EvaluateCell(Cell) != FuelBurnable || GroundZ[Cell] < SandLine + 60.f)
			{
				continue;
			}
			if (Patch(Cell, 3) < 0.85f)
			{
				continue;
			}
			++Candidates;
			const float Score = PlantsNear(Cell, 6) - R / 2000.f;
			if (Score > BestScore)
			{
				BestScore = Score;
				Best = Cell;
			}
		}
	}
	if (Best == INDEX_NONE)
	{
		return false;
	}
	OutSpot = CellCenter(Best);
	UE_LOG(LogSoulsFire, Display, TEXT("FireDemo: spot %s, %.0f m from the start, %d plants within 8 m"), *OutSpot.ToCompactString(),
		FVector::Dist2D(OutSpot, Origin) / 100.f, PlantsNear(Best, 6));
	DemoOrigin = Origin;
	return true;
}

void USoulsFireSubsystem::TickDemo(float DeltaTime)
{
	UWorld* World = GetWorld();
	DemoClock += DeltaTime;
	if (!bDemoIgnited)
	{
		float Warmup = 12.f;
		FParse::Value(FCommandLine::Get(), TEXT("FireDemoWarmup="), Warmup);
		bool bCompiling = false;
#if WITH_EDITOR
		bCompiling = GShaderCompilingManager && GShaderCompilingManager->GetNumRemainingJobs() > 0;
#endif
		if (DemoClock < Warmup || (bCompiling && DemoClock < Warmup + 120.f))
		{
			return;
		}
		if (!PickDemoSpot(DemoSpot))
		{
			UE_LOG(LogSoulsFire, Error, TEXT("FireDemo: no burnable spot near the start"));
			UE_LOG(LogSoulsFire, Display, TEXT("FIRE_DEMO_DONE failed"));
			bDemo = false;
			FPlatformMisc::RequestExit(false, TEXT("FireDemo"));
			return;
		}
		// Frame it from the start's side (usually lower, open ground), looking down on the patch.
		FVector Away = (DemoOrigin - DemoSpot).GetSafeNormal2D();
		if (Away.IsNearlyZero())
		{
			Away = FVector(-1.f, 0.f, 0.f);
		}
		FVector CamPos = DemoSpot + Away * 2600.f + FVector(0.f, 0.f, 1300.f);
		FHitResult Hit;
		if (World->LineTraceSingleByChannel(Hit, FVector(CamPos.X, CamPos.Y, 40000.f), FVector(CamPos.X, CamPos.Y, -5000.f), ECC_Visibility))
		{
			CamPos.Z = FMath::Max(CamPos.Z, Hit.ImpactPoint.Z + 500.f);
		}
		const FRotator Look = (DemoSpot + FVector(0.f, 0.f, 100.f) - CamPos).Rotation();
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		DemoCamera = World->SpawnActor<ACameraActor>(CamPos, Look, Params);
		if (APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0))
		{
			if (PC->MyHUD)
			{
				PC->MyHUD->bShowHUD = false;
			}
			if (DemoCamera)
			{
				DemoCamera->GetCameraComponent()->bConstrainAspectRatio = false;
				DemoCamera->GetCameraComponent()->SetFieldOfView(65.f);
				PC->SetViewTarget(DemoCamera);
			}
		}
		Ignite(DemoSpot, 200.f);
		bDemoIgnited = true;
		DemoIgniteAt = DemoClock;
		UE_LOG(LogSoulsFire, Display, TEXT("FireDemo: ignited at %s (camera %s)"), *DemoSpot.ToCompactString(), *CamPos.ToCompactString());
		return;
	}

	const float T = DemoClock - DemoIgniteAt;
	auto Counts = [&](const TCHAR* Tag)
	{
		int32 Burning = 0;
		for (int32 Index : ActiveItems)
		{
			Burning += Items[Index].IgniteTime >= 0.f && Clock >= Items[Index].IgniteTime ? 1 : 0;
		}
		UE_LOG(LogSoulsFire, Display, TEXT("FireDemo: %s t=%.1fs burning=%d active=%d burnt=%d plantsBurning=%d plantsConsumed=%d flames=%d smoke=%d lights=%d burningAtSpot=%d wind=(%.2f,%.2f)"),
			Tag, T, BurningCells, Active.Num(), BurntCells, Burning, ConsumedItems, FlamesShown, SmokeShown, LightsLit,
			FSoulsFire::IsBurningAt(World, DemoSpot) ? 1 : 0, WindNow.X, WindNow.Y);
	};
	// UNR-T75: the wide shots as before, plus close-ups at the fire front (low, a few metres off it) by day
	// and, after the sky is turned to night (21:00, held), the same again so the fire can be judged in the dark.
	enum class EDemo : uint8 { Wide, CloseOn, CloseShot, WideOn, Night };
	struct FEvent
	{
		float Time;
		EDemo What;
	};
	static const FEvent Events[] = {
		{ 0.5f, EDemo::Wide }, { 3.f, EDemo::Wide }, { 6.f, EDemo::Wide },
		{ 7.5f, EDemo::CloseOn }, { 7.8f, EDemo::CloseShot }, { 8.6f, EDemo::CloseShot }, { 8.8f, EDemo::WideOn },
		{ 9.f, EDemo::Wide }, { 12.f, EDemo::Wide },
		{ 13.f, EDemo::Night },
		{ 15.f, EDemo::Wide },
		{ 16.f, EDemo::CloseOn }, { 16.3f, EDemo::CloseShot }, { 17.2f, EDemo::CloseShot }, { 17.4f, EDemo::WideOn },
		{ 18.f, EDemo::Wide }, { 21.f, EDemo::Wide }, { 25.f, EDemo::Wide }, { 30.f, EDemo::Wide },
	};
	APlayerController* Player = UGameplayStatics::GetPlayerController(World, 0);
	while (DemoEvent < (int32)UE_ARRAY_COUNT(Events) && T >= Events[DemoEvent].Time)
	{
		const FEvent& Event = Events[DemoEvent++];
		const bool bNight = DemoEvent > 10;
		switch (Event.What)
		{
		case EDemo::Wide:
		case EDemo::CloseShot:
		{
			const bool bClose = Event.What == EDemo::CloseShot;
			const FString Name = FString::Printf(TEXT("FireDemo_%02d_%02ds%s%s"), DemoShot + 1, FMath::RoundToInt(Event.Time),
				bClose ? TEXT("_close") : TEXT(""), bNight ? TEXT("_night") : TEXT(""));
			const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Screenshots") / (Name + TEXT(".png")));
			FScreenshotRequest::RequestScreenshot(Path, false, false);
			Counts(*Name);
			++DemoShot;
			break;
		}
		case EDemo::CloseOn:
		{
			// Low, 7 m back from the strongest burning, on the side the wide camera looks from.
			const FVector Front = BurningCentroid();
			FVector Away = (DemoOrigin - Front).GetSafeNormal2D();
			if (Away.IsNearlyZero())
			{
				Away = FVector(-1.f, 0.f, 0.f);
			}
			FVector Eye = Front + Away * 700.f + FVector(0.f, 0.f, 220.f);
			FHitResult Hit;
			if (World->LineTraceSingleByChannel(Hit, FVector(Eye.X, Eye.Y, Eye.Z + 3000.f), FVector(Eye.X, Eye.Y, Eye.Z - 3000.f), ECC_Visibility))
			{
				Eye.Z = FMath::Max(Eye.Z, Hit.ImpactPoint.Z + 160.f);
			}
			const FRotator Look = (Front + FVector(0.f, 0.f, 110.f) - Eye).Rotation();
			if (!DemoCloseCamera)
			{
				FActorSpawnParameters Params;
				Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				DemoCloseCamera = World->SpawnActor<ACameraActor>(Eye, Look, Params);
				if (DemoCloseCamera)
				{
					DemoCloseCamera->GetCameraComponent()->bConstrainAspectRatio = false;
					DemoCloseCamera->GetCameraComponent()->SetFieldOfView(60.f);
				}
			}
			if (DemoCloseCamera)
			{
				DemoCloseCamera->SetActorLocationAndRotation(Eye, Look);
				if (Player)
				{
					Player->SetViewTarget(DemoCloseCamera);
				}
			}
			UE_LOG(LogSoulsFire, Display, TEXT("FireDemo: close camera at %s on the front at %s"), *Eye.ToCompactString(), *Front.ToCompactString());
			break;
		}
		case EDemo::WideOn:
			if (Player && DemoCamera)
			{
				Player->SetViewTarget(DemoCamera);
			}
			break;
		case EDemo::Night:
			if (AIslandDayNight* Sky = AIslandDayNight::Get(World))
			{
				Sky->SetTimeOfDay(21.f, true);
				UE_LOG(LogSoulsFire, Display, TEXT("FireDemo: night (21:00)"));
			}
			else
			{
				UE_LOG(LogSoulsFire, Warning, TEXT("FireDemo: no day/night actor, the night shots are lit as the level is"));
			}
			break;
		}
	}
	if (T >= DemoNextLog)
	{
		Counts(TEXT("tick"));
		DemoNextLog += 2.f;
	}
	if (T > 32.f)
	{
		Counts(TEXT("end"));
		UE_LOG(LogSoulsFire, Display, TEXT("FIRE_DEMO_DONE burnt=%d consumed=%d"), BurntCells, ConsumedItems);
		bDemo = false;
		FPlatformMisc::RequestExit(false, TEXT("FireDemo"));
	}
}

// ------------------------------------------------------------------------------------------------
// Editor library
// ------------------------------------------------------------------------------------------------

float USoulsFireLibrary::GetFireFieldHalfExtent()
{
	return SoulsFireTuning::Half;
}

UTexture2D* USoulsFireLibrary::CreateFireFieldTexture(const FString& PackagePath, int32 Resolution)
{
#if WITH_EDITOR
	const int32 Size = FMath::Clamp(Resolution, 16, 4096);
	TArray<FColor> Blank;
	Blank.SetNumZeroed(Size * Size);
	const FString AssetName = FPackageName::GetLongPackageAssetName(PackagePath);
	UPackage* Package = CreatePackage(*PackagePath);
	Package->FullyLoad();
	UTexture2D* Texture = FindObject<UTexture2D>(Package, *AssetName);
	const bool bCreated = Texture == nullptr;
	if (bCreated)
	{
		Texture = NewObject<UTexture2D>(Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
	}
	Texture->PreEditChange(nullptr);
	Texture->Source.Init(Size, Size, 1, 1, TSF_BGRA8, reinterpret_cast<const uint8*>(Blank.GetData()));
	Texture->SRGB = false;
	Texture->CompressionSettings = TC_VectorDisplacementmap; // uncompressed BGRA8, so UpdateTextureRegions can write it
	Texture->MipGenSettings = TMGS_NoMipmaps;
	Texture->NeverStream = true;
	Texture->VirtualTextureStreaming = false;
	Texture->Filter = TF_Bilinear;
	Texture->AddressX = TA_Clamp;
	Texture->AddressY = TA_Clamp;
	Texture->PostEditChange();
	Package->MarkPackageDirty();
	if (bCreated)
	{
		FAssetRegistryModule::AssetCreated(Texture);
	}
	return Texture;
#else
	return nullptr;
#endif
}
