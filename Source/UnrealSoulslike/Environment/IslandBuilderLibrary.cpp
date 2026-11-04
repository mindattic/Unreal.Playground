#include "IslandBuilderLibrary.h"

#include "Async/ParallelFor.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Landscape.h"
#include "LandscapeDataAccess.h"
#include "LandscapeInfo.h"
#include "Materials/MaterialInterface.h"
#include "Math/UnrealMathUtility.h"
#include "UObject/Package.h"

#include "GameFramework/PhysicsVolume.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Components/BrushComponent.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "Builders/CubeBuilder.h"
#include "ActorFactories/ActorFactory.h"
#endif

namespace IslandTerrain
{
	static FIslandLayout GLayout;
	static TArray<float> GPadHeights; // metres, one per layout pad

	// Landscape vertical scale: local height units are 1/128, so 100 gives 0.78 cm steps and +-256 m.
	static constexpr float LandscapeZScale = 100.f;

	static float Noise(const FVector2D& P, int32 Seed)
	{
		return FMath::PerlinNoise2D(P + FVector2D(Seed * 17.317, Seed * -9.731));
	}

	static float Fbm(const FVector2D& P, int32 Octaves, int32 Seed)
	{
		float Sum = 0.f, Amp = 0.5f, Freq = 1.f, Norm = 0.f;
		for (int32 i = 0; i < Octaves; ++i)
		{
			Sum += Amp * Noise(P * Freq, Seed + i * 13);
			Norm += Amp;
			Amp *= 0.5f;
			Freq *= 2.03f;
		}
		return Sum / Norm;
	}

	static float Smooth(float A, float B, float X)
	{
		if (A == B)
		{
			return X < A ? 0.f : 1.f;
		}
		const float T = FMath::Clamp((X - A) / (B - A), 0.f, 1.f);
		return T * T * (3.f - 2.f * T);
	}

	static float PathDistanceMetres(const FVector2D& Pm)
	{
		const TArray<FVector2D>& Path = GLayout.Path;
		if (Path.Num() == 0)
		{
			return 1e6f;
		}
		float Best = 1e9f;
		for (int32 i = 0; i + 1 < Path.Num(); ++i)
		{
			const FVector2D A = Path[i] / 100.0, B = Path[i + 1] / 100.0;
			const FVector2D AB = B - A;
			const double Len2 = AB.SizeSquared();
			const double T = Len2 > 0 ? FMath::Clamp(((Pm - A) | AB) / Len2, 0.0, 1.0) : 0.0;
			Best = FMath::Min(Best, (float)FVector2D::Distance(Pm, A + AB * T));
		}
		if (Path.Num() == 1)
		{
			Best = (float)FVector2D::Distance(Pm, Path[0] / 100.0);
		}
		return Best;
	}

	struct FSample
	{
		float Height = 0.f; // metres
		float Path = 0.f;
		float Dirt = 0.f;
		float Cliff = 0.f;
		float Marsh = 0.f;
		float Wood = 0.f;
		float Heath = 0.f;
		float Pool = 0.f; // metres of water above the ground (marsh pools)
	};

	static FSample Evaluate(float Xcm, float Ycm, bool bPads)
	{
		const FIslandLayout& L = GLayout;
		const int32 Seed = L.Seed;
		const FVector2D P(Xcm, Ycm);
		const FVector2D Pm = P / 100.0;

		// Coastline: a noise-warped ellipse, plus a finer wiggle for coves and points.
		const FVector2D Warp(Fbm(Pm / 140.0, 3, Seed), Fbm(Pm / 140.0 + FVector2D(31.7, 11.3), 3, Seed + 5));
		FVector2D Q(Xcm / L.RadiusX, Ycm / L.RadiusY);
		Q += Warp * 0.32;
		const float D = (float)Q.Size() + 0.06f * Fbm(Pm / 30.0, 3, Seed + 9);
		const float MeanRadiusM = 0.5f * (L.RadiusX + L.RadiusY) / 100.f;
		const float S = (1.f - D) * MeanRadiusM; // ~ signed metres from the coast, + inland

		const FVector2D Dir = P.GetSafeNormal();
		const float CliffNoise = 0.18f * Noise(Pm / 50.0, Seed + 61);
		// The cliff coast is picked by bearing; fade it out inland so bearings never meet in a seam mid-island.
		const float Cliff = Smooth(0.3f, 0.7f, (float)(Dir | L.CliffDirection.GetSafeNormal()) + CliffNoise) * (1.f - Smooth(55.f, 150.f, S));
		const float Beach = Smooth(0.5f, 0.9f, (float)(Dir | L.BeachDirection.GetSafeNormal()));

		// Gentle shore: sea floor, flat wet sand, beach, dune bank, rolling machair.
		const float BeachWidth = FMath::Lerp(12.f, 34.f, Beach);
		float Shore;
		if (S < 0.f)
		{
			Shore = FMath::Max(-(0.07f * -S + 0.0016f * S * S), -34.f);
		}
		else
		{
			const float Inland = FMath::Max(0.f, S - BeachWidth);
			Shore = 1.9f * Smooth(0.f, BeachWidth, S) + 2.4f * Smooth(0.f, 12.f, Inland) + 8.5f * Smooth(0.f, 150.f, Inland);
		}
		const float InlandFade = Smooth(BeachWidth * 0.7f, BeachWidth + 30.f, S);
		const float Rolling = Fbm(Pm / 120.0, 4, Seed + 21) * 4.5f * InlandFade;
		const float Bumps = Fbm(Pm / 16.0, 3, Seed + 33) * 0.55f * InlandFade;
		// Low hummocks (rocky knolls are dressed with rock meshes instead of carved into the height field).
		const float Knoll = FMath::Max(0.f, Fbm(Pm / 34.0, 2, Seed + 41)) * 2.2f * InlandFade;

		// Cliff coast: sea floor drops hard, a near-vertical face, then a high moor.
		const float CliffTop = L.CliffHeight / 100.f + Fbm(Pm / 55.0, 3, Seed + 51) * 3.f;
		const float Face = Smooth(-6.f, 6.f, S + 3.f * Noise(Pm / 9.0, Seed + 71));
		const float Underwater = -12.f - 0.12f * FMath::Max(0.f, -S);
		const float CliffProfile = FMath::Lerp(FMath::Max(Underwater, -34.f), CliffTop, Face) + 5.f * Smooth(0.f, 120.f, S);
		const float CliffFade = Smooth(-2.f, 25.f, S);

		float Smooth0 = FMath::Lerp(Shore + Rolling, CliffProfile + Rolling * 0.5f * CliffFade, Cliff);

		// Peat marsh: a flat, slightly hummocky basin.
		const float MarshD = (float)FVector2D::Distance(Pm, L.MarshCenter / 100.0) + 9.f * Noise(Pm / 26.0, Seed + 101);
		const float MarshR = L.MarshRadius / 100.f;
		// The shadow-boss clearing sits at marsh level but stays firm turf (no bog colour, reeds or pools).
		const float ClearDist = (float)FVector2D::Distance(Pm, L.ClearingCenter / 100.0);
		const float MarshShape = (1.f - Smooth(MarshR * 0.6f, MarshR, MarshD)) * Smooth(5.f, 20.f, S);
		const float Marsh = MarshShape * Smooth(L.ClearingRadius / 100.f, L.ClearingRadius / 100.f + 7.f, ClearDist);
		Smooth0 = FMath::Lerp(Smooth0, L.MarshLevel / 100.f + 0.25f * Noise(Pm / 7.0, Seed + 103), MarshShape);

		const float Detail = (Bumps + Knoll * (1.f - 0.5f * Cliff)) * (1.f - 0.85f * MarshShape);
		float Height = Smooth0 + Detail;

		// Route: keep the walking line smooth.
		const float PathD = PathDistanceMetres(Pm);
		const float PathW = L.PathWidth / 100.f;
		const float PathMask = 1.f - Smooth(PathW, PathW * 3.f, PathD);
		Height = FMath::Lerp(Height, Smooth0 + Bumps * 0.25f, PathMask);

		// Marsh pools: noise blobs carved below the water line, kept off the causeway and the clearing.
		const float ClearD = (float)FVector2D::Distance(Pm, L.ClearingCenter / 100.0);
		const float ClearR = L.ClearingRadius / 100.f;
		const float PoolNoise = Fbm(Pm / 16.0, 3, Seed + 107) + 0.35f * Noise(Pm / 5.0, Seed + 109);
		float PoolMask = Smooth(0.06f, 0.16f, PoolNoise) * Smooth(0.75f, 0.95f, MarshShape)
			* Smooth(PathW * 1.4f, PathW * 2.6f, PathD) * Smooth(ClearR + 2.f, ClearR + 9.f, ClearD);
		Height = FMath::Lerp(Height, L.PoolLevel / 100.f - 1.2f, PoolMask);

		// Hill with the flat arena on top.
		const float HillD = (float)FVector2D::Distance(Pm, L.HillCenter / 100.0);
		const float ArenaR = L.ArenaRadius / 100.f;
		const float HillShape = Smooth(L.HillRadius / 100.f, ArenaR, HillD);
		const float HillNoise = Fbm(Pm / 20.0, 3, Seed + 81) * 2.5f * HillShape * (1.f - HillShape) * 4.f;
		Height = FMath::Lerp(Height, L.ArenaHeight / 100.f, HillShape) + HillNoise;

		float Dirt = 0.f;
		if (bPads)
		{
			for (int32 i = 0; i < L.Pads.Num(); ++i)
			{
				const FVector& Pad = L.Pads[i];
				const float R = (float)Pad.Z / 100.f;
				const float PadD = (float)FVector2D::Distance(Pm, FVector2D(Pad.X, Pad.Y) / 100.0);
				const float PadMask = 1.f - Smooth(R, R + 22.f, PadD);
				if (PadMask > 0.f && GPadHeights.IsValidIndex(i))
				{
					Height = FMath::Lerp(Height, GPadHeights[i], PadMask);
					Dirt = FMath::Max(Dirt, PadMask * Smooth(-0.3f, 0.4f, Noise(Pm / 3.0, Seed + 91) + 0.3f - PadD / R * 0.6f));
				}
			}
		}
		// The arena is turf worn down to dirt toward its centre, where the fights happen.
		const float ArenaDirt = (1.f - Smooth(ArenaR * 0.15f, ArenaR * 0.7f, HillD + 4.f * Noise(Pm / 5.0, Seed + 95)));
		Dirt = FMath::Max(Dirt, ArenaDirt * 0.75f);

		// Offshore islets: small domes with a sand rim rising out of the sea floor.
		for (const FVector& Islet : L.Islets)
		{
			const float R = (float)Islet.Z / 100.f;
			const float Di = (float)FVector2D::Distance(Pm, FVector2D(Islet.X, Islet.Y) / 100.0) + 4.f * Noise(Pm / 12.0, Seed + 111);
			const float Si = R - Di;
			const float IsletH = Si < 0.f ? -(0.12f * -Si + 0.004f * Si * Si)
				: 1.2f * Smooth(0.f, 6.f, Si) + (L.IsletHeight / 100.f - 1.2f) * Smooth(4.f, R * 0.75f, Si) + Bumps * Smooth(4.f, 10.f, Si);
			Height = FMath::Max(Height, IsletH);
		}

		FSample Out;
		Out.Height = Height;
		Out.Marsh = Marsh;
		Out.Pool = FMath::Max(0.f, L.PoolLevel / 100.f - Height) * Marsh;
		const float WoodD = (float)FVector2D::Distance(Pm, L.WoodCenter / 100.0) + 14.f * Noise(Pm / 30.0, Seed + 113);
		Out.Wood = (1.f - Smooth(L.WoodRadius / 100.f * 0.5f, L.WoodRadius / 100.f, WoodD)) * Smooth(12.f, 30.f, S) * (1.f - Marsh);
		Out.Heath = Smooth(13.f, 19.f, Height + 3.f * Noise(Pm / 18.0, Seed + 117)) * (1.f - HillShape * Smooth(-0.1f, 0.1f, ArenaR + 6.f - HillD))
			* (1.f - Out.Wood) * (1.f - Marsh);
		Out.Path = PathMask * Smooth(-0.6f, 0.2f, Noise(Pm / 2.5, Seed + 97) - PathD / FMath::Max(PathW, 0.01f) * 0.5f + 0.4f);
		Out.Dirt = Dirt;
		Out.Cliff = Cliff * Smooth(-8.f, 0.f, S) * (1.f - Smooth(15.f, 40.f, S));
		return Out;
	}

	static int32 VertsPerSide()
	{
		return FMath::Max(1, GLayout.ComponentsPerSide) * 126 + 1;
	}
}

void UIslandBuilderLibrary::SetIslandLayout(const FIslandLayout& Layout)
{
	using namespace IslandTerrain;
	GLayout = Layout;
	GPadHeights.Reset();
	for (const FVector& Pad : GLayout.Pads)
	{
		GPadHeights.Add(Evaluate((float)Pad.X, (float)Pad.Y, false).Height);
	}
}

float UIslandBuilderLibrary::GetIslandHeight(float X, float Y)
{
	return IslandTerrain::Evaluate(X, Y, true).Height * 100.f;
}

FVector UIslandBuilderLibrary::GetIslandNormal(float X, float Y)
{
	const float E = 50.f;
	const float HX = GetIslandHeight(X + E, Y) - GetIslandHeight(X - E, Y);
	const float HY = GetIslandHeight(X, Y + E) - GetIslandHeight(X, Y - E);
	return FVector(-HX, -HY, 2.f * E).GetSafeNormal();
}

float UIslandBuilderLibrary::GetPathDistance(float X, float Y)
{
	return IslandTerrain::PathDistanceMetres(FVector2D(X, Y) / 100.0) * 100.f;
}

float UIslandBuilderLibrary::GetLandscapeExtent()
{
	return 0.5f * (IslandTerrain::VertsPerSide() - 1) * IslandTerrain::GLayout.QuadSize;
}

ALandscape* UIslandBuilderLibrary::CreateIslandLandscape(UObject* WorldContextObject, UMaterialInterface* Material)
{
#if WITH_EDITOR
	using namespace IslandTerrain;
	UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	if (!World)
	{
		return nullptr;
	}

	const int32 Verts = VertsPerSide();
	const float Quad = GLayout.QuadSize;
	const float Extent = GetLandscapeExtent();

	TArray<uint16> Heights;
	Heights.SetNumUninitialized(Verts * Verts);
	ParallelFor(Verts, [&](int32 Y)
	{
		for (int32 X = 0; X < Verts; ++X)
		{
			const float WX = -Extent + X * Quad;
			const float WY = -Extent + Y * Quad;
			const float Hcm = Evaluate(WX, WY, true).Height * 100.f;
			Heights[Y * Verts + X] = LandscapeDataAccess::GetTexHeight(Hcm / LandscapeZScale);
		}
	});

	ALandscape* Landscape = World->SpawnActor<ALandscape>(FVector(-Extent, -Extent, 0.f), FRotator::ZeroRotator);
	if (!Landscape)
	{
		return nullptr;
	}
	Landscape->LandscapeMaterial = Material;
	Landscape->SetActorRelativeScale3D(FVector(Quad, Quad, LandscapeZScale));
	Landscape->SetActorLabel(TEXT("IslandLandscape"));

	TMap<FGuid, TArray<uint16>> HeightDataPerLayer;
	HeightDataPerLayer.Add(FGuid(), MoveTemp(Heights));
	TMap<FGuid, TArray<FLandscapeImportLayerInfo>> MaterialLayerDataPerLayer;
	MaterialLayerDataPerLayer.Add(FGuid(), TArray<FLandscapeImportLayerInfo>());

	Landscape->Import(FGuid::NewGuid(), 0, 0, Verts - 1, Verts - 1, 2, 63, HeightDataPerLayer, nullptr,
		MaterialLayerDataPerLayer, ELandscapeImportAlphamapType::Additive, TArrayView<const FLandscapeLayer>());

	if (ULandscapeInfo* Info = Landscape->GetLandscapeInfo())
	{
		Info->UpdateLayerInfoMap(Landscape);
	}
	return Landscape;
#else
	return nullptr;
#endif
}

FVector4 UIslandBuilderLibrary::GetIslandBiome(float X, float Y)
{
	const IslandTerrain::FSample S = IslandTerrain::Evaluate(X, Y, true);
	return FVector4(S.Marsh, S.Wood, S.Heath, FMath::Clamp(S.Pool / 1.5f, 0.f, 1.f));
}

namespace IslandTerrain
{
	// Writes Pixels (N x N, over the landscape extent) into a BGRA8 texture asset at PackagePath.
	static UTexture2D* WriteTexture(const FString& PackagePath, int32 N, const TArray<FColor>& Pixels)
	{
#if WITH_EDITOR
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
		Texture->Source.Init(N, N, 1, 1, TSF_BGRA8, reinterpret_cast<const uint8*>(Pixels.GetData()));
		Texture->SRGB = false;
		Texture->CompressionSettings = TC_VectorDisplacementmap;
		Texture->MipGenSettings = TMGS_FromTextureGroup;
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

	static uint8 To8(float V)
	{
		return (uint8)FMath::RoundToInt(FMath::Clamp(V, 0.f, 1.f) * 255.f);
	}
}

UTexture2D* UIslandBuilderLibrary::CreateIslandBiomeTexture(const FString& PackagePath, int32 Resolution)
{
	using namespace IslandTerrain;
	const int32 N = FMath::Clamp(Resolution, 64, 4096);
	const float Extent = GetLandscapeExtent();
	TArray<FColor> Pixels;
	Pixels.SetNumUninitialized(N * N);
	ParallelFor(N, [&](int32 Y)
	{
		for (int32 X = 0; X < N; ++X)
		{
			const FSample S = Evaluate(-Extent + (X + 0.5f) / N * 2.f * Extent, -Extent + (Y + 0.5f) / N * 2.f * Extent, true);
			Pixels[Y * N + X] = FColor(To8(S.Marsh), To8(S.Wood), To8(S.Heath), To8(S.Pool / 1.5f));
		}
	});
	return WriteTexture(PackagePath, N, Pixels);
}

AActor* UIslandBuilderLibrary::CreateInstancedMeshes(UObject* WorldContextObject, UStaticMesh* Mesh, const TArray<FTransform>& Instances,
	const FString& Label, const FString& Folder, bool bCollision, float CullDistance)
{
#if WITH_EDITOR
	UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	if (!World || !Mesh)
	{
		return nullptr;
	}
	AActor* Actor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity);
	UHierarchicalInstancedStaticMeshComponent* Comp = NewObject<UHierarchicalInstancedStaticMeshComponent>(Actor, TEXT("Instances"), RF_Transactional);
	Comp->SetStaticMesh(Mesh);
	Comp->SetMobility(EComponentMobility::Static);
	Comp->SetCollisionEnabled(bCollision ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	if (bCollision)
	{
		Comp->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	}
	if (CullDistance > 0.f)
	{
		Comp->InstanceEndCullDistance = (int32)CullDistance;
		Comp->InstanceStartCullDistance = (int32)(CullDistance * 0.8f);
	}
	Actor->SetRootComponent(Comp);
	Actor->AddInstanceComponent(Comp);
	Comp->RegisterComponent();
	Comp->AddInstances(Instances, false, true);
	Actor->SetActorLabel(Label);
	Actor->SetFolderPath(FName(*Folder));
	return Actor;
#else
	return nullptr;
#endif
}

AActor* UIslandBuilderLibrary::CreateWaterVolume(UObject* WorldContextObject, FVector Center, FVector Extent, const FString& Label)
{
#if WITH_EDITOR
	UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	if (!World)
	{
		return nullptr;
	}
	APhysicsVolume* Volume = World->SpawnActor<APhysicsVolume>(Center, FRotator::ZeroRotator);
	if (!Volume)
	{
		return nullptr;
	}
	UCubeBuilder* Builder = NewObject<UCubeBuilder>();
	Builder->X = Extent.X * 2.f;
	Builder->Y = Extent.Y * 2.f;
	Builder->Z = Extent.Z * 2.f;
	UActorFactory::CreateBrushForVolumeActor(Volume, Builder);
	Volume->bWaterVolume = true;
	Volume->FluidFriction = 0.6f;
	Volume->SetActorLabel(Label);
	Volume->PostEditChange();
	Volume->GetBrushComponent()->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
	return Volume;
#else
	return nullptr;
#endif
}

UTexture2D* UIslandBuilderLibrary::CreateIslandMaskTexture(const FString& PackagePath, int32 Resolution)
{
#if WITH_EDITOR
	using namespace IslandTerrain;
	const int32 N = FMath::Clamp(Resolution, 64, 4096);
	const float Extent = GetLandscapeExtent();

	TArray<FColor> Pixels;
	Pixels.SetNumUninitialized(N * N);
	ParallelFor(N, [&](int32 Y)
	{
		for (int32 X = 0; X < N; ++X)
		{
			const float WX = -Extent + (X + 0.5f) / N * 2.f * Extent;
			const float WY = -Extent + (Y + 0.5f) / N * 2.f * Extent;
			const FSample S = Evaluate(WX, WY, true);
			const auto To8 = [](float V) { return (uint8)FMath::RoundToInt(FMath::Clamp(V, 0.f, 1.f) * 255.f); };
			Pixels[Y * N + X] = FColor(To8(S.Path), To8(S.Dirt), To8(-S.Height / 10.f), To8(S.Cliff));
		}
	});

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
	Texture->Source.Init(N, N, 1, 1, TSF_BGRA8, reinterpret_cast<const uint8*>(Pixels.GetData()));
	Texture->SRGB = false;
	Texture->CompressionSettings = TC_VectorDisplacementmap;
	Texture->MipGenSettings = TMGS_FromTextureGroup;
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
