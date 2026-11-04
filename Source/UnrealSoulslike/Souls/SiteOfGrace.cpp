#include "SiteOfGrace.h"
#include "SoulsGameMode.h"
#include "SoulsKnight.h"
#include "CombatPlayerController.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

ASiteOfGrace::ASiteOfGrace()
{
	PrimaryActorTick.bCanEverTick = true;

	Core = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Core"));
	RootComponent = Core;
	Core->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Core->SetCastShadow(false);

	Halo = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Halo"));
	Halo->SetupAttachment(Core);
	Halo->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Halo->SetCastShadow(false);

	Light = CreateDefaultSubobject<UPointLightComponent>(TEXT("Light"));
	Light->SetupAttachment(Core);
	Light->SetLightColor(FLinearColor(1.f, 0.72f, 0.3f));
	Light->SetIntensity(9000.f);
	Light->SetAttenuationRadius(900.f);
	Light->SetRelativeLocation(FVector(0.f, 0.f, 90.f));
	Light->SetCastShadows(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (Sphere.Succeeded())
	{
		Core->SetStaticMesh(Sphere.Object);
	}
	if (Cylinder.Succeeded())
	{
		Halo->SetStaticMesh(Cylinder.Object);
	}
	Core->SetRelativeScale3D(FVector(0.22f));
	Halo->SetRelativeScale3D(FVector(0.12f / 0.22f, 0.12f / 0.22f, 9.f / 0.22f));
	Halo->SetRelativeLocation(FVector(0.f, 0.f, 0.f));
}

void ASiteOfGrace::BeginPlay()
{
	Super::BeginPlay();
	// The glow material is built with the rest of the content (tools/build_souls_character.py).
	if (UMaterialInterface* Glow = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Souls/FX/M_GraceGlow.M_GraceGlow")))
	{
		Core->SetMaterial(0, Glow);
	}
	if (UMaterialInterface* Beam = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Souls/FX/M_GraceBeam.M_GraceBeam")))
	{
		Halo->SetMaterial(0, Beam);
	}
	else
	{
		Halo->SetVisibility(false);
	}
	Phase = FMath::FRand() * 6.f;
}

void ASiteOfGrace::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Phase += DeltaSeconds;
	Core->SetRelativeLocation(FVector::ZeroVector);
	Light->SetIntensity(8000.f + 2500.f * FMath::Sin(Phase * 2.1f) + 900.f * FMath::Sin(Phase * 7.3f));
	Core->AddLocalRotation(FRotator(0.f, 40.f * DeltaSeconds, 0.f));

	ASoulsKnight* Player = Cast<ASoulsKnight>(UGameplayStatics::GetPlayerPawn(this, 0));
	if (!Player || Player->IsDead())
	{
		return;
	}
	const bool bNear = FVector::Dist(Player->GetActorLocation(), GetActorLocation()) < InteractRadius;
	if (bNear && Player->GetNearbyGrace() != this)
	{
		Player->SetNearbyGrace(this);
		if (!bDiscovered)
		{
			bDiscovered = true;
			if (ASoulsGameMode* GameMode = GetWorld()->GetAuthGameMode<ASoulsGameMode>())
			{
				GameMode->GraceDiscovered(this);
			}
		}
	}
	else if (!bNear && Player->GetNearbyGrace() == this)
	{
		Player->SetNearbyGrace(nullptr);
	}
}

FTransform ASiteOfGrace::GetRespawnTransform() const
{
	const FVector Offset = GetActorForwardVector() * 150.f;
	return FTransform(GetActorRotation(), GetActorLocation() + Offset + FVector(0.f, 0.f, 20.f));
}

void ASiteOfGrace::Rest(ASoulsKnight* Knight)
{
	if (!Knight)
	{
		return;
	}
	if (ASoulsGameMode* GameMode = GetWorld()->GetAuthGameMode<ASoulsGameMode>())
	{
		GameMode->RestAtGrace(this, Knight);
	}
}
