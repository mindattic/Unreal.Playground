#include "SoulsGore.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

namespace SoulsGoreTuning
{
	constexpr int32 GoreMaxSprays = 30;      // oldest sprays (and their splats) go first
	constexpr float GoreGravity = 1350.f;
}

ASoulsGore::ASoulsGore()
{
	PrimaryActorTick.bCanEverTick = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	// Hard references, so the packaged game cooks them.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Shape(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	DropMesh = Sphere.Object;
	BaseMaterial = Shape.Object;
}

ASoulsGore* ASoulsGore::Spray(UWorld* World, const FVector& Location, const FVector& Direction, int32 Count, float Scale, float Spread)
{
	if (!World || Count <= 0)
	{
		return nullptr;
	}
	// Keep the world from drowning: retire the oldest sprays past the cap.
	TArray<ASoulsGore*> Existing;
	for (TActorIterator<ASoulsGore> It(World); It; ++It)
	{
		Existing.Add(*It);
	}
	if (Existing.Num() >= SoulsGoreTuning::GoreMaxSprays)
	{
		Existing.Sort([](const ASoulsGore& A, const ASoulsGore& B) { return A.Age > B.Age; });
		for (int32 Index = 0; Index <= Existing.Num() - SoulsGoreTuning::GoreMaxSprays; ++Index)
		{
			Existing[Index]->Destroy();
		}
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ASoulsGore* Gore = World->SpawnActor<ASoulsGore>(ASoulsGore::StaticClass(), Location, FRotator::ZeroRotator, Params);
	if (!Gore)
	{
		return nullptr;
	}
	Gore->SetLifeSpan(Gore->Life + 2.f);

	// Where the blood will land: the ground under the hit.
	float GroundZ = Location.Z - 100.f;
	FHitResult Hit;
	FCollisionQueryParams Query(SCENE_QUERY_STAT(GoreGround), false);
	for (TActorIterator<APawn> It(World); It; ++It)
	{
		Query.AddIgnoredActor(*It);
	}
	if (World->LineTraceSingleByChannel(Hit, Location, Location - FVector(0.f, 0.f, 600.f), ECC_Visibility, Query))
	{
		GroundZ = Hit.ImpactPoint.Z + 1.f;
	}

	// A few shades of blood, shared by the drops.
	for (const FLinearColor Shade : { FLinearColor(0.07f, 0.f, 0.002f), FLinearColor(0.035f, 0.f, 0.f), FLinearColor(0.12f, 0.004f, 0.004f) })
	{
		UMaterialInstanceDynamic* Tint = UMaterialInstanceDynamic::Create(Gore->BaseMaterial, Gore);
		Tint->SetVectorParameterValue(TEXT("Color"), Shade);
		Gore->Tints.Add(Tint);
	}

	const FVector Main = Direction.GetSafeNormal();
	for (int32 Index = 0; Index < Count; ++Index)
	{
		UStaticMeshComponent* Mesh = NewObject<UStaticMeshComponent>(Gore);
		Mesh->SetStaticMesh(Gore->DropMesh);
		Mesh->SetMaterial(0, Gore->Tints[Index % Gore->Tints.Num()]);
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->SetCastShadow(false);
		Mesh->SetupAttachment(Gore->GetRootComponent());
		Mesh->RegisterComponent();
		FVector Out = (Main + FMath::VRand() * Spread + FVector(0.f, 0.f, FMath::FRandRange(0.1f, 0.9f))).GetSafeNormal();
		FDrop Drop;
		Drop.Mesh = Mesh;
		// Mostly a fine mist of droplets, with a fat gout here and there.
		Drop.Size = (FMath::FRand() < 0.15f ? FMath::FRandRange(0.06f, 0.11f) : FMath::FRandRange(0.015f, 0.045f)) * Scale;
		Drop.Velocity = Out * FMath::FRandRange(250.f, 950.f) * FMath::Sqrt(Scale);
		Drop.GroundZ = GroundZ;
		Mesh->SetWorldLocation(Location + FMath::VRand() * 6.f);
		Mesh->SetWorldScale3D(FVector(Drop.Size));
		Gore->Drops.Add(Drop);
		Gore->Meshes.Add(Mesh);
	}
	return Gore;
}

void ASoulsGore::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Age += DeltaSeconds;
	bool bAllLanded = true;
	for (FDrop& Drop : Drops)
	{
		UStaticMeshComponent* Mesh = Drop.Mesh;
		if (!Mesh)
		{
			continue;
		}
		if (!Drop.bLanded)
		{
			bAllLanded = false;
			Drop.Velocity.Z -= SoulsGoreTuning::GoreGravity * DeltaSeconds;
			Drop.Velocity *= 1.f - 0.6f * DeltaSeconds;
			FVector Location = Mesh->GetComponentLocation() + Drop.Velocity * DeltaSeconds;
			if (Location.Z <= Drop.GroundZ)
			{
				// Splat: flattened into a puddle on the ground, spreading a touch.
				Location.Z = Drop.GroundZ;
				Drop.bLanded = true;
				Mesh->SetWorldRotation(FRotator(0.f, FMath::FRandRange(0.f, 360.f), 0.f));
			}
			else
			{
				// Stretched along its flight.
				const float Stretch = FMath::Clamp(Drop.Velocity.Size() / 500.f, 1.f, 2.5f);
				Mesh->SetWorldRotation(Drop.Velocity.Rotation());
				Mesh->SetWorldScale3D(FVector(Drop.Size * Stretch, Drop.Size, Drop.Size));
			}
			Mesh->SetWorldLocation(Location);
		}
		if (Drop.bLanded && Drop.SplatGrow < 1.f)
		{
			Drop.SplatGrow = FMath::Min(Drop.SplatGrow + DeltaSeconds * 4.f, 1.f);
			const float Wide = Drop.Size * FMath::Lerp(1.f, 2.6f, Drop.SplatGrow);
			Mesh->SetWorldScale3D(FVector(Wide * 1.2f, Wide, Drop.Size * 0.08f));
		}
	}
	// The splats soak away at the end.
	if (Age > Life - 3.f)
	{
		const float Fade = FMath::Clamp((Life - Age) / 3.f, 0.f, 1.f);
		for (FDrop& Drop : Drops)
		{
			if (Drop.Mesh && Drop.bLanded)
			{
				const float Wide = Drop.Size * 2.6f * Fade;
				Drop.Mesh->SetWorldScale3D(FVector(Wide * 1.2f, Wide, Drop.Size * 0.08f));
			}
		}
	}
	if (Age >= Life)
	{
		Destroy();
	}
	// Once everything has landed, tick slowly: only the fade is left to do.
	SetActorTickInterval(bAllLanded && Age < Life - 3.f ? 0.5f : 0.f);
}
