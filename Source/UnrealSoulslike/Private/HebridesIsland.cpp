#include "HebridesIsland.h"
#include "SoulslikeData.h"
#include "SkeletonEnemy.h"
#include "ProceduralMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogHebridesIsland, Log, All);

namespace
{
	const TCHAR* StoneMaterialPath = TEXT("/Game/Materials/M_Stone.M_Stone");
	const TCHAR* WaterMaterialPath = TEXT("/Game/Materials/M_Water.M_Water");
	const TCHAR* TerrainMaterialPath = TEXT("/Game/Materials/M_Terrain.M_Terrain");
	const TCHAR* FallbackTerrainMaterialPath = TEXT("/Engine/EngineDebugMaterials/VertexColorMaterial.VertexColorMaterial");

	// Vertex colours reach the shader without sRGB decoding, so store linear values.
	FColor Linear(uint8 R, uint8 G, uint8 B)
	{
		return FLinearColor::FromSRGBColor(FColor(R, G, B)).ToFColor(false);
	}

	FColor LerpColor(const FColor& A, const FColor& B, float T)
	{
		T = FMath::Clamp(T, 0.f, 1.f);
		return FColor(
			FMath::RoundToInt(FMath::Lerp<float>(A.R, B.R, T)),
			FMath::RoundToInt(FMath::Lerp<float>(A.G, B.G, T)),
			FMath::RoundToInt(FMath::Lerp<float>(A.B, B.B, T)));
	}

	float Hash01(int32 X, int32 Y)
	{
		uint32 H = static_cast<uint32>(X) * 374761393u + static_cast<uint32>(Y) * 668265263u;
		H = (H ^ (H >> 13)) * 1274126177u;
		return (H ^ (H >> 16)) / 4294967295.f;
	}

	UInstancedStaticMeshComponent* MakeInstances(AActor* Owner, USceneComponent* Root, const TCHAR* Name, UStaticMesh* Mesh)
	{
		UInstancedStaticMeshComponent* Instances = Owner->CreateDefaultSubobject<UInstancedStaticMeshComponent>(Name);
		Instances->SetupAttachment(Root);
		Instances->SetStaticMesh(Mesh);
		Instances->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		Instances->SetMobility(EComponentMobility::Static);
		return Instances;
	}
}

AHebridesIsland::AHebridesIsland()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	bAlwaysRelevant = true;

	USceneComponent* SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SceneRoot->SetMobility(EComponentMobility::Static);
	RootComponent = SceneRoot;

	Terrain = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Terrain"));
	Terrain->SetupAttachment(SceneRoot);
	Terrain->bUseAsyncCooking = false;
	Terrain->bUseComplexAsSimpleCollision = true;
	Terrain->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Plane(TEXT("/Engine/BasicShapes/Plane.Plane"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));

	Sea = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Sea"));
	Sea->SetupAttachment(SceneRoot);
	Sea->SetStaticMesh(Plane.Object);
	Sea->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Sea->SetCastShadow(false);
	Sea->SetRelativeScale3D(FVector(3000.f, 3000.f, 1.f));

	StoneBlocks = MakeInstances(this, SceneRoot, TEXT("StoneBlocks"), Cube.Object);
	Megaliths = MakeInstances(this, SceneRoot, TEXT("Megaliths"), Cube.Object);
	Thatch = MakeInstances(this, SceneRoot, TEXT("Thatch"), Cube.Object);
	Towers = MakeInstances(this, SceneRoot, TEXT("Towers"), Cylinder.Object);
	Boulders = MakeInstances(this, SceneRoot, TEXT("Boulders"), Sphere.Object);

	Sun = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("Sun"));
	Sun->SetupAttachment(SceneRoot);
	Sun->SetMobility(EComponentMobility::Movable);
	Sun->SetRelativeRotation(FRotator(-24.f, 150.f, 0.f));
	Sun->SetIntensity(7.f);
	Sun->SetLightColor(FLinearColor(1.f, 0.93f, 0.84f));
	Sun->SetAtmosphereSunLight(true);
	Sun->DynamicShadowDistanceMovableLight = 20000.f;

	SkyAtmosphere = CreateDefaultSubobject<USkyAtmosphereComponent>(TEXT("SkyAtmosphere"));
	SkyAtmosphere->SetupAttachment(SceneRoot);

	SkyLight = CreateDefaultSubobject<USkyLightComponent>(TEXT("SkyLight"));
	SkyLight->SetupAttachment(SceneRoot);
	SkyLight->SetMobility(EComponentMobility::Movable);
	SkyLight->bRealTimeCapture = true;
	SkyLight->SetIntensity(1.f);

	Mist = CreateDefaultSubobject<UExponentialHeightFogComponent>(TEXT("Mist"));
	Mist->SetupAttachment(SceneRoot);
	Mist->SetFogDensity(0.035f);
	Mist->SetFogHeightFalloff(0.12f);
	Mist->SetStartDistance(800.f);
	Mist->SetFogInscatteringColor(FLinearColor(0.42f, 0.47f, 0.52f));
	Mist->SetVolumetricFog(true);
}

void AHebridesIsland::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	BuildIsland();
}

void AHebridesIsland::BeginPlay()
{
	Super::BeginPlay();
	if (!bBuilt)
	{
		BuildIsland();
	}
	if (HasAuthority() && bSpawnEnemies)
	{
		SpawnEnemies();
	}
}

void AHebridesIsland::BuildIsland()
{
	if (!FHebridesTerrain::Get().IsValid())
	{
		UE_LOG(LogHebridesIsland, Error, TEXT("No heightmap: run tools/generate_heightmap.py (Content/Terrain)."));
		return;
	}
	BuildTerrain();
	BuildSea();
	BuildRuins();
	bBuilt = true;
}

UMaterialInterface* AHebridesIsland::MakeTinted(const TCHAR* MaterialPath, const FLinearColor& Color)
{
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, MaterialPath);
	if (!Base)
	{
		Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	}
	UMaterialInstanceDynamic* Tinted = UMaterialInstanceDynamic::Create(Base, this);
	Tinted->SetVectorParameterValue(TEXT("Color"), Color);
	return Tinted;
}

void AHebridesIsland::BuildTerrain()
{
	const FHebridesTerrain& Data = FHebridesTerrain::Get();
	const int32 N = Data.Resolution;
	const float Half = Data.WorldSize() * 0.5f;
	const float Cell = FHebridesTerrain::CellSize;

	const FColor SeaFloor = Linear(118, 108, 84);
	const FColor Sand = Linear(214, 200, 160);
	const FColor Machair = Linear(104, 132, 64);
	const FColor MachairLight = Linear(132, 146, 78);
	const FColor Heather = Linear(98, 78, 96);
	const FColor Peat = Linear(84, 74, 62);
	const FColor Rock = Linear(122, 120, 114);

	TArray<FVector> Vertices;
	TArray<FVector> Normals;
	TArray<FVector2D> UV0;
	TArray<FColor> Colors;
	TArray<int32> Triangles;
	Vertices.Reserve(N * N);
	Normals.Reserve(N * N);
	UV0.Reserve(N * N);
	Colors.Reserve(N * N);
	Triangles.Reserve((N - 1) * (N - 1) * 6);

	for (int32 Y = 0; Y < N; ++Y)
	{
		for (int32 X = 0; X < N; ++X)
		{
			const float Z = Data.GridZ(X, Y);
			Vertices.Add(FVector(X * Cell - Half, Y * Cell - Half, Z));
			UV0.Add(FVector2D(X * 0.5f, Y * 0.5f));

			const float DzDx = (Data.GridZ(X + 1, Y) - Data.GridZ(X - 1, Y)) / (2.f * Cell);
			const float DzDy = (Data.GridZ(X, Y + 1) - Data.GridZ(X, Y - 1)) / (2.f * Cell);
			const FVector Normal = FVector(-DzDx, -DzDy, 1.f).GetSafeNormal();
			Normals.Add(Normal);

			const float Above = Z / FHebridesTerrain::HeightScale;
			const float Noise = Hash01(X, Y);
			const float SlopeDegrees = FMath::RadiansToDegrees(FMath::Acos(Normal.Z));
			FColor Color;
			if (Above < 0.f)
			{
				Color = LerpColor(Sand, SeaFloor, -Above * 20.f);
			}
			else if (Above < 0.012f)
			{
				Color = Sand;
			}
			else if (Above < 0.09f)
			{
				Color = LerpColor(Machair, MachairLight, Noise);
			}
			else if (Above < 0.22f)
			{
				Color = LerpColor(Heather, Peat, Noise);
			}
			else
			{
				Color = Rock;
			}
			if (SlopeDegrees > 38.f && Above > 0.f)
			{
				Color = LerpColor(Color, Rock, (SlopeDegrees - 38.f) / 10.f);
			}
			Colors.Add(Color);
		}
	}

	// (i, i+N, i+1) faces +Z with Unreal's winding convention.
	for (int32 Y = 0; Y < N - 1; ++Y)
	{
		for (int32 X = 0; X < N - 1; ++X)
		{
			const int32 I = Y * N + X;
			Triangles.Append({ I, I + N, I + 1, I + 1, I + N, I + N + 1 });
		}
	}

	Terrain->ClearAllMeshSections();
	Terrain->CreateMeshSection(0, Vertices, Triangles, Normals, UV0, Colors, TArray<FProcMeshTangent>(), true);

	UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, TerrainMaterialPath);
	if (!Material)
	{
		Material = LoadObject<UMaterialInterface>(nullptr, FallbackTerrainMaterialPath);
	}
	Terrain->SetMaterial(0, Material);
}

void AHebridesIsland::BuildSea()
{
	Sea->SetMaterial(0, MakeTinted(WaterMaterialPath, FLinearColor(0.012f, 0.045f, 0.06f)));
}

void AHebridesIsland::AddBox(UInstancedStaticMeshComponent* Instances, const FVector& Centre, const FVector& Size, const FRotator& Rotation)
{
	Instances->AddInstance(FTransform(Rotation, Centre, Size / 100.f), true);
}

void AHebridesIsland::BuildRuins()
{
	for (UInstancedStaticMeshComponent* Instances : { StoneBlocks.Get(), Megaliths.Get(), Thatch.Get(), Towers.Get(), Boulders.Get() })
	{
		Instances->ClearInstances();
	}
	StoneBlocks->SetMaterial(0, MakeTinted(StoneMaterialPath, FLinearColor(0.32f, 0.31f, 0.29f)));
	Towers->SetMaterial(0, MakeTinted(StoneMaterialPath, FLinearColor(0.30f, 0.29f, 0.27f)));
	Megaliths->SetMaterial(0, MakeTinted(StoneMaterialPath, FLinearColor(0.18f, 0.19f, 0.2f)));
	Thatch->SetMaterial(0, MakeTinted(StoneMaterialPath, FLinearColor(0.28f, 0.2f, 0.09f)));
	Boulders->SetMaterial(0, MakeTinted(StoneMaterialPath, FLinearColor(0.25f, 0.26f, 0.24f)));

	const FHebridesTerrain& Data = FHebridesTerrain::Get();
	FRandomStream Random(Seed);
	for (const FVector2D& Croft : Data.Crofts)
	{
		BuildCroft(Croft, Random);
	}
	BuildBroch(Data.Broch, Random);
	BuildStandingStones(Data.StandingStones, Random);
	BuildKeep(Data.Keep, Random);
	ScatterBoulders(Random);
}

void AHebridesIsland::BuildCroft(const FVector2D& UV, FRandomStream& Random)
{
	const FHebridesTerrain& Data = FHebridesTerrain::Get();
	const FVector Base = Data.ToWorld(UV);
	const FRotator Facing(0.f, Random.FRandRange(0.f, 360.f), 0.f);
	const float Length = 900.f, Width = 500.f, Height = 230.f, Thick = 60.f;

	auto Wall = [&](float LocalX, float LocalY, float SizeX, float SizeY)
	{
		const FVector Offset = Facing.RotateVector(FVector(LocalX, LocalY, 0.f));
		FVector Centre = Base + Offset;
		Centre.Z = Data.SampleZ(Data.ToUV(Centre).X, Data.ToUV(Centre).Y) + Height * 0.5f - 40.f;
		AddBox(StoneBlocks, Centre, FVector(SizeX, SizeY, Height), Facing);
	};

	const float DoorHalf = 75.f;
	const float FrontPiece = Length * 0.5f - DoorHalf;
	Wall(0.f, Width * 0.5f, Length, Thick);
	Wall(-(DoorHalf + FrontPiece * 0.5f), -Width * 0.5f, FrontPiece, Thick);
	Wall(DoorHalf + FrontPiece * 0.5f, -Width * 0.5f, FrontPiece, Thick);
	Wall(-Length * 0.5f, 0.f, Thick, Width);
	Wall(Length * 0.5f, 0.f, Thick, Width);

	const float RoofPitch = 35.f;
	const float SlabWidth = Width * 0.62f;
	const float RoofBase = Base.Z + Height - 40.f + FMath::Tan(FMath::DegreesToRadians(RoofPitch)) * Width * 0.25f;
	for (const float Side : { 1.f, -1.f })
	{
		const FRotator SlabRotation(0.f, Facing.Yaw, RoofPitch * Side);
		FVector Centre = Base + Facing.RotateVector(FVector(0.f, Side * Width * 0.25f, 0.f));
		Centre.Z = RoofBase;
		AddBox(Thatch, Centre, FVector(Length + 80.f, SlabWidth, 40.f), SlabRotation);
	}

	FVector Chimney = Base + Facing.RotateVector(FVector(Length * 0.5f, 0.f, 0.f));
	Chimney.Z = Base.Z + Height + 140.f;
	AddBox(StoneBlocks, Chimney, FVector(90.f, 90.f, 260.f), Facing);
}

void AHebridesIsland::BuildBroch(const FVector2D& UV, FRandomStream& Random)
{
	const FHebridesTerrain& Data = FHebridesTerrain::Get();
	const FVector Centre = Data.ToWorld(UV);
	const FVector ToBeach = Data.ToWorld(Data.LandingBeach) - Centre;
	const float DoorAngle = FMath::Atan2(ToBeach.Y, ToBeach.X);
	const float Radius = 650.f, Thick = 170.f;
	const int32 Segments = 30;

	for (int32 i = 0; i < Segments; ++i)
	{
		const float Angle = 2.f * PI * i / Segments;
		if (FMath::Abs(FMath::FindDeltaAngleRadians(Angle, DoorAngle)) < 0.18f)
		{
			continue;
		}
		const float Height = Random.FRandRange(450.f, 1050.f);
		FVector Position = Centre + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * Radius;
		const FVector2D PUV = Data.ToUV(Position);
		Position.Z = Data.SampleZ(PUV.X, PUV.Y) + Height * 0.5f - 60.f;
		const float SegmentLength = 2.f * PI * Radius / Segments * 1.15f;
		AddBox(StoneBlocks, Position, FVector(SegmentLength, Thick, Height), FRotator(0.f, FMath::RadiansToDegrees(Angle) + 90.f, 0.f));
	}

	for (int32 i = 0; i < 10; ++i)
	{
		const float Angle = Random.FRandRange(0.f, 2.f * PI);
		const float Distance = Random.FRandRange(Radius + 150.f, Radius + 700.f);
		FVector Position = Centre + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * Distance;
		const FVector2D PUV = Data.ToUV(Position);
		Position.Z = Data.SampleZ(PUV.X, PUV.Y) + 30.f;
		AddBox(StoneBlocks, Position, FVector(Random.FRandRange(80.f, 180.f), Random.FRandRange(60.f, 120.f), 70.f),
			FRotator(Random.FRandRange(-10.f, 10.f), Random.FRandRange(0.f, 360.f), Random.FRandRange(-10.f, 10.f)));
	}
}

void AHebridesIsland::BuildStandingStones(const FVector2D& UV, FRandomStream& Random)
{
	const FHebridesTerrain& Data = FHebridesTerrain::Get();
	const FVector Centre = Data.ToWorld(UV);

	auto Stone = [&](const FVector& At, float Height, float Yaw)
	{
		FVector Position = At;
		const FVector2D PUV = Data.ToUV(Position);
		Position.Z = Data.SampleZ(PUV.X, PUV.Y) + Height * 0.5f - 50.f;
		AddBox(Megaliths, Position, FVector(50.f, 110.f, Height), FRotator(Random.FRandRange(-6.f, 6.f), Yaw, Random.FRandRange(-6.f, 6.f)));
	};

	const int32 Count = 13;
	for (int32 i = 0; i < Count; ++i)
	{
		const float Angle = 2.f * PI * i / Count;
		Stone(Centre + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * 900.f, Random.FRandRange(280.f, 460.f), FMath::RadiansToDegrees(Angle));
	}
	Stone(Centre, 560.f, Random.FRandRange(0.f, 180.f));

	// A Callanish-style avenue running north (-Y).
	for (int32 k = 0; k < 5; ++k)
	{
		for (const float Side : { -300.f, 300.f })
		{
			Stone(Centre + FVector(Side, -1300.f - k * 500.f, 0.f), Random.FRandRange(220.f, 360.f), 0.f);
		}
	}
}

void AHebridesIsland::BuildKeep(const FVector2D& UV, FRandomStream& Random)
{
	const FHebridesTerrain& Data = FHebridesTerrain::Get();
	const FVector Centre = Data.ToWorld(UV);
	const FVector ToBeach = Data.ToWorld(Data.LandingBeach) - Centre;
	const FRotator Facing(0.f, ToBeach.Rotation().Yaw, 0.f);
	const float Size = 3200.f, Thick = 200.f, Segment = 400.f, GateHalf = 350.f;

	auto Ground = [&](const FVector& At)
	{
		const FVector2D PUV = Data.ToUV(At);
		return Data.SampleZ(PUV.X, PUV.Y);
	};

	// Sides in local space: +X faces the beach and holds the gate.
	const FVector2D SideNormals[] = { FVector2D(1, 0), FVector2D(-1, 0), FVector2D(0, 1), FVector2D(0, -1) };
	for (const FVector2D& SideNormal : SideNormals)
	{
		const FVector2D Along(-SideNormal.Y, SideNormal.X);
		for (float T = -Size * 0.5f + Segment * 0.5f; T < Size * 0.5f; T += Segment)
		{
			if (SideNormal.X > 0.5f && FMath::Abs(T) < GateHalf)
			{
				continue;
			}
			const FVector2D Local2D = SideNormal * Size * 0.5f + Along * T;
			FVector Position = Centre + Facing.RotateVector(FVector(Local2D.X, Local2D.Y, 0.f));
			const bool bBroken = Random.FRand() < 0.15f;
			const float Height = bBroken ? Random.FRandRange(150.f, 300.f) : 700.f + Random.FRandRange(-250.f, 450.f);
			Position.Z = Ground(Position) + Height * 0.5f - 80.f;
			const float Yaw = Facing.Yaw + FMath::RadiansToDegrees(FMath::Atan2(Along.Y, Along.X));
			AddBox(StoneBlocks, Position, FVector(Segment + 10.f, Thick, Height), FRotator(0.f, Yaw, 0.f));
		}
	}

	int32 Corner = 0;
	for (const FVector2D& Local2D : { FVector2D(1, 1), FVector2D(1, -1), FVector2D(-1, 1), FVector2D(-1, -1) })
	{
		FVector Position = Centre + Facing.RotateVector(FVector(Local2D.X, Local2D.Y, 0.f) * Size * 0.5f);
		const float Height = Corner++ == 3 ? 800.f : 1500.f;
		Position.Z = Ground(Position) + Height * 0.5f - 80.f;
		AddBox(Towers, Position, FVector(760.f, 760.f, Height), FRotator::ZeroRotator);
	}

	for (const float Side : { -1.f, 1.f })
	{
		FVector Position = Centre + Facing.RotateVector(FVector(Size * 0.5f, Side * (GateHalf + 250.f), 0.f));
		Position.Z = Ground(Position) + 600.f - 80.f;
		AddBox(StoneBlocks, Position, FVector(500.f, 500.f, 1200.f), Facing);
	}

	// Broken inner hall walls ringing the boss arena (centre radius ~900 stays clear).
	for (int32 i = 0; i < 7; ++i)
	{
		const float Angle = Random.FRandRange(0.f, 2.f * PI);
		FVector Position = Centre + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * Random.FRandRange(1000.f, 1300.f);
		const float Height = Random.FRandRange(200.f, 500.f);
		Position.Z = Ground(Position) + Height * 0.5f - 60.f;
		AddBox(StoneBlocks, Position, FVector(Random.FRandRange(250.f, 600.f), 150.f, Height), FRotator(0.f, FMath::RadiansToDegrees(Angle) + 90.f, 0.f));
	}
}

void AHebridesIsland::ScatterBoulders(FRandomStream& Random)
{
	const FHebridesTerrain& Data = FHebridesTerrain::Get();

	TArray<TPair<FVector2D, float>> Keepouts = {
		{ Data.Keep, 0.08f }, { Data.Broch, 0.04f }, { Data.StandingStones, 0.06f }, { Data.LandingBeach, 0.07f },
	};
	for (const FVector2D& Croft : Data.Crofts)
	{
		Keepouts.Add({ Croft, 0.035f });
	}

	int32 Placed = 0;
	for (int32 Attempt = 0; Attempt < 600 && Placed < 160; ++Attempt)
	{
		const FVector2D UV(Random.FRandRange(0.12f, 0.88f), Random.FRandRange(0.12f, 0.88f));
		const float Z = Data.SampleZ(UV.X, UV.Y);
		if (Z < 0.02f * FHebridesTerrain::HeightScale)
		{
			continue;
		}
		bool bBlocked = false;
		for (const TPair<FVector2D, float>& Keepout : Keepouts)
		{
			bBlocked |= FVector2D::Distance(UV, Keepout.Key) < Keepout.Value;
		}
		if (bBlocked)
		{
			continue;
		}

		const FVector Size(Random.FRandRange(60.f, 400.f), Random.FRandRange(60.f, 400.f), Random.FRandRange(40.f, 220.f));
		FVector Position = Data.ToWorld(UV);
		Position.Z += Size.Z * 0.2f;
		AddBox(Boulders, Position, Size, FRotator(0.f, Random.FRandRange(0.f, 360.f), 0.f));
		++Placed;
	}
}

void AHebridesIsland::SpawnEnemies()
{
	const FHebridesTerrain& Data = FHebridesTerrain::Get();
	UWorld* World = GetWorld();
	FRandomStream Random(Seed + 1);

	for (int32 i = 0; i < Data.SkeletonSpawns.Num(); ++i)
	{
		const FVector Location = Data.ToWorld(Data.SkeletonSpawns[i]) + FVector(0.f, 0.f, 110.f);
		const FTransform Transform(FRotator(0.f, Random.FRandRange(0.f, 360.f), 0.f), Location);
		ASkeletonEnemy* Skeleton = World->SpawnActorDeferred<ASkeletonEnemy>(ASkeletonEnemy::StaticClass(), Transform, this, nullptr, ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
		if (!Skeleton)
		{
			continue;
		}
		Skeleton->VisualIndex = i % 3;
		Skeleton->BlockChance = Skeleton->VisualIndex == 0 ? 0.3f : 0.f;
		Skeleton->FinishSpawning(Transform);
	}

	const FVector BossLocation = Data.ToWorld(Data.Boss) + FVector(0.f, 0.f, 170.f);
	const FVector ToBeach = Data.ToWorld(Data.LandingBeach) - BossLocation;
	const FTransform BossTransform(FRotator(0.f, ToBeach.Rotation().Yaw, 0.f), BossLocation, FVector(1.5f));
	if (ASkeletonEnemy* Boss = World->SpawnActorDeferred<ASkeletonEnemy>(ASkeletonEnemy::StaticClass(), BossTransform, this, nullptr, ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn))
	{
		Boss->ConfigureAsBoss();
		Boss->FinishSpawning(BossTransform);
	}
	UE_LOG(LogHebridesIsland, Log, TEXT("Spawned %d skeletons and the boss"), Data.SkeletonSpawns.Num());
}
