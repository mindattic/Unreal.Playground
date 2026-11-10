#include "SoulsRanger.h"
#include "Components/PointLightComponent.h"
#include "Engine/DamageEvents.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "CollisionQueryParams.h"

namespace
{
	constexpr float KMuzzleFlashDuration = 0.06f;
	constexpr float KMuzzleFlashIntensity = 20000.f;
}

ASoulsRanger::ASoulsRanger()
{
	DisplayName = NSLOCTEXT("Souls", "Ranger", "Ranger");

	MuzzleLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("MuzzleLight"));
	MuzzleLight->SetupAttachment(Sword);
	MuzzleLight->SetRelativeLocation(FVector(40.f, 0.f, 0.f));
	MuzzleLight->SetIntensity(0.f);
	MuzzleLight->SetLightColor(FLinearColor(1.f, 0.75f, 0.35f));
	MuzzleLight->SetAttenuationRadius(300.f);
	MuzzleLight->SetCastShadows(false);
}

void ASoulsRanger::OnBecomeActive()
{
	Super::OnBecomeActive();
	NextFireTime = 0.f;
	MuzzleFlashT = 0.f;
	if (MuzzleLight)
	{
		MuzzleLight->SetIntensity(0.f);
	}
}

void ASoulsRanger::OnBecomeInactive()
{
	Super::OnBecomeInactive();
	if (MuzzleLight)
	{
		MuzzleLight->SetIntensity(0.f);
	}
}

void ASoulsRanger::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (MuzzleFlashT > 0.f)
	{
		MuzzleFlashT = FMath::Max(MuzzleFlashT - DeltaSeconds, 0.f);
		if (MuzzleLight)
		{
			MuzzleLight->SetIntensity(KMuzzleFlashIntensity * (MuzzleFlashT / KMuzzleFlashDuration));
		}
	}
}

FVector ASoulsRanger::AimPoint() const
{
	if (const APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		FVector CamLocation;
		FRotator CamRotation;
		PC->GetPlayerViewPoint(CamLocation, CamRotation);
		const FVector End = CamLocation + CamRotation.Vector() * WeaponRange;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(RangerAim), false, this);
		FHitResult Hit;
		if (GetWorld()->LineTraceSingleByChannel(Hit, CamLocation, End, ECC_Visibility, Params))
		{
			return Hit.Location;
		}
		return End;
	}
	return GetActorLocation() + GetActorForwardVector() * WeaponRange;
}

FVector ASoulsRanger::MuzzleLocation() const
{
	return MuzzleLight ? MuzzleLight->GetComponentLocation() : GetActorLocation();
}

void ASoulsRanger::Fire(float Damage)
{
	const FVector Muzzle = MuzzleLocation();
	const FVector Dir = (AimPoint() - Muzzle).GetSafeNormal();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(RangerFire), false, this);
	FHitResult Hit;
	if (GetWorld()->LineTraceSingleByChannel(Hit, Muzzle, Muzzle + Dir * WeaponRange, ECC_Visibility, Params))
	{
		if (ASoulsKnight* Victim = Cast<ASoulsKnight>(Hit.GetActor()))
		{
			FDamageEvent DamageEvent;
			Victim->TakeDamage(Damage, DamageEvent, GetController(), this);
		}
	}
	MuzzleFlashT = KMuzzleFlashDuration;
	if (MuzzleLight)
	{
		MuzzleLight->SetIntensity(KMuzzleFlashIntensity);
	}
}

void ASoulsRanger::DoComboAttackStart()
{
	const float Now = GetWorld()->GetTimeSeconds();
	if (IsDead() || IsSwimming() || Now < NextFireTime)
	{
		return;
	}
	NextFireTime = Now + FireCooldown;
	PlayAction(EKnightAction::LightAttack, nullptr, 0.15f);
	Fire(LightFireDamage);
}

void ASoulsRanger::DoChargedAttackStart()
{
	const float Now = GetWorld()->GetTimeSeconds();
	if (IsDead() || IsSwimming() || Now < NextFireTime)
	{
		return;
	}
	NextFireTime = Now + FireCooldown * 1.5f;
	PlayAction(EKnightAction::HeavyAttack, nullptr, 0.22f);
	Fire(HeavyFireDamage);
}
