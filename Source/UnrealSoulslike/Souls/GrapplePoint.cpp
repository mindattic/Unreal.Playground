#include "GrapplePoint.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

AGrapplePoint::AGrapplePoint()
{
	PrimaryActorTick.bCanEverTick = true;

	Anchor = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Anchor"));
	RootComponent = Anchor;
	Anchor->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Anchor->SetCastShadow(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (Sphere.Succeeded())
	{
		Anchor->SetStaticMesh(Sphere.Object);
	}
	Anchor->SetRelativeScale3D(FVector(0.16f));

	Glint = CreateDefaultSubobject<UPointLightComponent>(TEXT("Glint"));
	Glint->SetupAttachment(Anchor);
	Glint->SetLightColor(FLinearColor(0.45f, 0.85f, 0.75f));
	Glint->SetIntensity(1800.f);
	Glint->SetAttenuationRadius(380.f);
	Glint->SetCastShadows(false);
}

void AGrapplePoint::BeginPlay()
{
	Super::BeginPlay();
	if (UMaterialInterface* Glow = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Souls/FX/M_GrappleGlow.M_GrappleGlow")))
	{
		Anchor->SetMaterial(0, Glow);
	}
	// Find the walkable top of the ledge the hook overlooks.
	const FVector Over = GetActorLocation() + GetActorForwardVector().GetSafeNormal2D() * LandingDepth;
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(GrappleLanding), false, this);
	if (GetWorld()->LineTraceSingleByChannel(Hit, Over + FVector(0.f, 0.f, 300.f), Over - FVector(0.f, 0.f, 1500.f), ECC_Visibility, Params))
	{
		LandingGround = Hit.ImpactPoint;
		bHasLanding = true;
	}
	Phase = FMath::FRand() * 5.f;
}

FVector AGrapplePoint::GetLandingLocation(float CapsuleHalfHeight) const
{
	const FVector Ground = bHasLanding ? LandingGround : GetActorLocation() + GetActorForwardVector().GetSafeNormal2D() * LandingDepth;
	return Ground + FVector(0.f, 0.f, CapsuleHalfHeight + 8.f);
}

void AGrapplePoint::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Phase += DeltaSeconds;
	Glint->SetIntensity(1500.f + 700.f * FMath::Sin(Phase * 3.f));
}
