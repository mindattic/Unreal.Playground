#include "SoulsSparks.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

ASoulsSparks::ASoulsSparks()
{
	PrimaryActorTick.bCanEverTick = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	Flash = CreateDefaultSubobject<UPointLightComponent>(TEXT("Flash"));
	Flash->SetupAttachment(GetRootComponent());
	Flash->SetCastShadows(false);
	Flash->SetAttenuationRadius(450.f);
	Flash->SetIntensity(0.f);

	// Hard references, so the packaged game cooks them.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Glow(TEXT("/Game/Souls/FX/M_GraceGlow.M_GraceGlow"));
	SparkMesh = Sphere.Object;
	SparkMaterial = Glow.Object;
}

ASoulsSparks* ASoulsSparks::Burst(UWorld* World, const FVector& Location, const FVector& Direction, int32 Count, float Scale,
	float Spread, const FLinearColor& FlashColor)
{
	if (!World)
	{
		return nullptr;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ASoulsSparks* Sparks = World->SpawnActor<ASoulsSparks>(ASoulsSparks::StaticClass(), Location, FRotator::ZeroRotator, Params);
	if (!Sparks)
	{
		return nullptr;
	}
	Sparks->SetLifeSpan(1.f);
	Sparks->BitScale = Scale;
	Sparks->FlashIntensity = 9000.f * Scale;
	Sparks->Flash->SetLightColor(FlashColor);
	Sparks->Flash->SetIntensity(Sparks->FlashIntensity);
	const FVector Main = Direction.GetSafeNormal();
	for (int32 Index = 0; Index < Count; ++Index)
	{
		UStaticMeshComponent* Bit = NewObject<UStaticMeshComponent>(Sparks);
		Bit->SetStaticMesh(Sparks->SparkMesh);
		Bit->SetMaterial(0, Sparks->SparkMaterial);
		Bit->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Bit->SetCastShadow(false);
		Bit->SetupAttachment(Sparks->GetRootComponent());
		Bit->RegisterComponent();
		FVector Out = (Main + FMath::VRand() * Spread).GetSafeNormal();
		if (Out.IsNearlyZero())
		{
			Out = FMath::VRand();
		}
		const FVector Velocity = Out * FMath::FRandRange(350.f, 900.f) * FMath::Sqrt(Scale) + FVector(0.f, 0.f, 150.f);
		Bit->SetWorldRotation(Velocity.Rotation());
		Bit->SetWorldScale3D(FVector(0.09f, 0.012f, 0.012f) * Scale);
		Sparks->Bits.Add(Bit);
		Sparks->Velocities.Add(Velocity);
	}
	return Sparks;
}

void ASoulsSparks::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Age += DeltaSeconds;
	const float T = FMath::Clamp(Age / Life, 0.f, 1.f);
	Flash->SetIntensity(FlashIntensity * FMath::Square(1.f - FMath::Min(T * 2.2f, 1.f)));
	for (int32 Index = 0; Index < Bits.Num(); ++Index)
	{
		FVector& Velocity = Velocities[Index];
		Velocity += FVector(0.f, 0.f, -1400.f) * DeltaSeconds;
		Velocity *= 1.f - 2.5f * DeltaSeconds;
		UStaticMeshComponent* Bit = Bits[Index];
		Bit->SetWorldLocationAndRotation(Bit->GetComponentLocation() + Velocity * DeltaSeconds, Velocity.Rotation());
		const float Stretch = FMath::Clamp(Velocity.Size() / 600.f, 0.4f, 1.6f);
		Bit->SetWorldScale3D(FVector(0.09f * Stretch, 0.012f, 0.012f) * BitScale * (1.f - T));
	}
	if (Age >= Life)
	{
		Destroy();
	}
}
