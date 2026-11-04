#include "SoulsPickup.h"
#include "SoulsGameMode.h"
#include "SoulsKnight.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

ASoulsPickup::ASoulsPickup()
{
	PrimaryActorTick.bCanEverTick = true;

	Mote = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mote"));
	RootComponent = Mote;
	Mote->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mote->SetCastShadow(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (Sphere.Succeeded())
	{
		Mote->SetStaticMesh(Sphere.Object);
	}
	Mote->SetRelativeScale3D(FVector(0.07f));

	Glow = CreateDefaultSubobject<UPointLightComponent>(TEXT("Glow"));
	Glow->SetupAttachment(Mote);
	Glow->SetIntensity(1200.f);
	Glow->SetAttenuationRadius(260.f);
	Glow->SetCastShadows(false);
	Glow->SetLightColor(FLinearColor(1.f, 0.93f, 0.75f));
}

void ASoulsPickup::BeginPlay()
{
	Super::BeginPlay();
	if (UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Souls/FX/M_PickupGlow.M_PickupGlow")))
	{
		Mote->SetMaterial(0, Material);
	}
	if (Kind == ESoulsPickupKind::CrimsonTear)
	{
		Glow->SetLightColor(FLinearColor(1.f, 0.35f, 0.3f));
	}
	Base = GetActorLocation();
	Phase = FMath::FRand() * 6.f;
}

FText ASoulsPickup::GetItemName() const
{
	switch (Kind)
	{
	case ESoulsPickupKind::GoldenSeed:
		return NSLOCTEXT("Souls", "GoldenSeed", "Golden Seed");
	case ESoulsPickupKind::CrimsonTear:
		return NSLOCTEXT("Souls", "CrimsonTear", "Crimson Tear");
	default:
		return FText::Format(NSLOCTEXT("Souls", "RuneCache", "Islander's Rune Purse ({0})"), FText::AsNumber(Runes));
	}
}

void ASoulsPickup::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Phase += DeltaSeconds;
	SetActorLocation(Base + FVector(0.f, 0.f, 6.f * FMath::Sin(Phase * 2.2f)));
	Glow->SetIntensity(900.f + 500.f * FMath::Sin(Phase * 4.f));

	ASoulsKnight* Player = Cast<ASoulsKnight>(UGameplayStatics::GetPlayerPawn(this, 0));
	if (!Player || Player->IsDead())
	{
		return;
	}
	const bool bNear = FVector::Dist(Player->GetActorLocation(), GetActorLocation()) < InteractRadius;
	if (bNear && Player->GetNearbyPickup() != this)
	{
		Player->SetNearbyPickup(this);
	}
	else if (!bNear && Player->GetNearbyPickup() == this)
	{
		Player->SetNearbyPickup(nullptr);
	}
}

void ASoulsPickup::Collect(ASoulsKnight* Knight)
{
	if (!Knight)
	{
		return;
	}
	ASoulsGameMode* GameMode = GetWorld()->GetAuthGameMode<ASoulsGameMode>();
	switch (Kind)
	{
	case ESoulsPickupKind::GoldenSeed:
		Knight->MaxFlasks += 1;
		Knight->Flasks += 1;
		if (GameMode)
		{
			++GameMode->GoldenSeeds;
		}
		break;
	case ESoulsPickupKind::CrimsonTear:
		Knight->FlaskHealing += 15.f;
		if (GameMode)
		{
			++GameMode->CrimsonTears;
		}
		break;
	default:
		if (GameMode)
		{
			GameMode->Runes += Runes;
			GameMode->LastRunesGained = Runes;
			GameMode->LastRunesTime = GetWorld()->GetTimeSeconds();
		}
		break;
	}
	if (GameMode)
	{
		GameMode->ShowToast(FText::Format(NSLOCTEXT("Souls", "Acquired", "{0} acquired"), GetItemName()));
		if (GameMode->PickupSound)
		{
			UGameplayStatics::PlaySound2D(this, GameMode->PickupSound, 0.8f);
		}
	}
	Knight->SetNearbyPickup(nullptr);
	Destroy();
}
