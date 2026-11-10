#include "SoulsKnight.h"
#include "SoulsEnemy.h"
#include "SoulsGameMode.h"
#include "SiteOfGrace.h"
#include "GrapplePoint.h"
#include "SoulsPickup.h"
#include "SoulsSparks.h"
#include "SoulsBlood.h"
#include "SoulsFire.h"
#include "SoulsTorch.h"
#include "SoulsMech.h"
#include "SoulsFighter.h"
#include "Components/ShadowMergeComponent.h"
#include "SoulsWallRun.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Camera/CameraComponent.h"
#include "CollisionQueryParams.h"
#include "CombatDamageable.h"
#include "CombatLifeBar.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "Engine/DamageEvents.h"
#include "Engine/Engine.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputAction.h"
#include "InputKeyEventArgs.h"
#include "InputMappingContext.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundWaveProcedural.h"
#include "TimerManager.h"
#include "UnrealClient.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"
#include "UnrealSoulslike.h"

namespace
{
	const FName KSwordSocket(TEXT("hand_r"));
	const FName KShieldSocket(TEXT("hand_l"));
	constexpr float KBlockWalkSpeed = 230.f;
	constexpr float KSprintSpeed = 760.f;
	constexpr float KBladeNearMiss = 45.f;   // a blade passing this close to a body still strikes it
	constexpr float KPlayerSlashSpeed = 1.4f;   // the X slash, vs 0.9 for an enemy heavy
	constexpr float KBackSlashAt = 0.55f;       // a queued back-slash starts this far into the slash
	constexpr float KSprintStaminaPerSecond = 14.f;
	// Raising the guard this close before a blow lands turns it into a parry.
	constexpr float KGuardParryWindow = 0.22f;
	// Holding B this long drops you prone instead of rolling or crouching.
	constexpr float KParryCost = 10.f;
	constexpr float KLockRange = 2200.f;
	constexpr float KLockBreakRange = 2900.f;
	constexpr float KCriticalRange = 240.f;
	constexpr float KCriticalDuration = 1.7f;
	constexpr float KParriedDuration = 2.4f;
	constexpr float KStanceBrokenDuration = 2.8f;
	constexpr float KPoiseRecoverDelay = 2.5f;
	constexpr float KStanceRecoverDelay = 4.f;
	constexpr float KStanceRecoverPerSecond = 12.f;
}

ASoulsKnight::ASoulsKnight()
{
	PrimaryActorTick.bCanEverTick = true;

	Sword = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Sword"));
	Sword->SetupAttachment(GetMesh(), KSwordSocket);
	Sword->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	Shield = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Shield"));
	Shield->SetupAttachment(GetMesh(), KShieldSocket);
	Shield->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	GrappleLine = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("GrappleLine"));
	GrappleLine->SetupAttachment(RootComponent);
	GrappleLine->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GrappleLine->SetCastShadow(false);
	GrappleLine->SetVisibility(false);
	GrappleLine->SetUsingAbsoluteLocation(true);
	GrappleLine->SetUsingAbsoluteRotation(true);
	GrappleLine->SetUsingAbsoluteScale(true);
	ShadowMelt = CreateDefaultSubobject<UShadowMergeComponent>(TEXT("ShadowMelt"));
	ShadowMelt->bAnySurface = true;
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ShadowBlobMaterial(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	ShadowMelt->BlobBaseMaterial = ShadowBlobMaterial.Object;
	ShadowMelt->CrawlSpeed = 520.f;
	ShadowMelt->MinEnergyToEnter = 0.f;
	WallRun = CreateDefaultSubobject<USoulsWallRunComponent>(TEXT("WallRun"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> LineMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (LineMesh.Succeeded())
	{
		GrappleLine->SetStaticMesh(LineMesh.Object);
	}

	RespawnTime = 5.f;
	DeathCameraDistance = 520.f;
	bUseControllerRotationYaw = false;
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	Movement->bOrientRotationToMovement = true;
	Movement->RotationRate = FRotator(0.f, 720.f, 0.f);
	Movement->MaxWalkSpeed = RunSpeed;
	Movement->BrakingDecelerationWalking = 1600.f;
	Movement->MaxSwimSpeed = 330.f;
	Movement->Buoyancy = 1.04f;
}

void ASoulsKnight::BeginPlay()
{
	// ACombatCharacter::BeginPlay resets HP to MaxHP and colours the floating bar, so set both first.
	MaxHP = MaxHealth;
	LifeBarColor = Team == 0 ? FLinearColor(0.6f, 0.05f, 0.04f) : FLinearColor(0.55f, 0.04f, 0.03f);
	Super::BeginPlay();

	// Keepsakes found on the island outlast death.
	if (Team == 0)
	{
		if (const ASoulsGameMode* GameMode = GetWorld()->GetAuthGameMode<ASoulsGameMode>())
		{
			MaxFlasks += GameMode->GoldenSeeds;
			FlaskHealing += 15.f * GameMode->CrimsonTears;
		}
	}
	Stamina = MaxStamina;
	Flasks = MaxFlasks;
	Poise = MaxPoise;
	GetCharacterMovement()->MaxWalkSpeed = RunSpeed;

	ApplyBodyMaterials(BodyMaterials);
	BaseMeshZ = GetMesh()->GetRelativeLocation().Z;
	if (UMaterialInterface* LineMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Souls/FX/M_GrappleLine.M_GrappleLine")))
	{
		GrappleLine->SetMaterial(0, LineMaterial);
	}
	// The player's vitals are on the HUD; enemies show a bar over the head only once hurt.
	if (UWidgetComponent* FloatingBar = FindComponentByClass<UWidgetComponent>())
	{
		FloatingBar->SetVisibility(false);
		FloatingBar->SetRelativeLocation(FVector(0.f, 0.f, 120.f));
	}

	DefaultCameraDistance = 430.f;
	GetCameraBoom()->TargetArmLength = DefaultCameraDistance;
	GetCameraBoom()->SocketOffset = FVector(0.f, 0.f, 55.f);
	GetCameraBoom()->CameraLagSpeed = 12.f;
	GetCameraBoom()->CameraRotationLagSpeed = 14.f;

	if (Team == 0 && FParse::Param(FCommandLine::Get(), TEXT("KnightDemo")))
	{
		RunVerificationDemo();
	}
	if (Team == 0 && FParse::Param(FCommandLine::Get(), TEXT("StealthDemo")))
	{
		RunStealthDemo();
	}
	if (Team == 0 && FParse::Param(FCommandLine::Get(), TEXT("InputTest")))
	{
		// -RespawnFirst: die once, and run the test on the knight that respawns (UNR-T66).
		static bool bRespawnTestKilled = false;
		if (FParse::Param(FCommandLine::Get(), TEXT("RespawnFirst")) && !bRespawnTestKilled)
		{
			bRespawnTestKilled = true;
			const TWeakObjectPtr<ASoulsKnight> WeakThis(this);
			FTimerHandle Die;
			GetWorldTimerManager().SetTimer(Die, [WeakThis]()
			{
				if (WeakThis.IsValid())
				{
					UE_LOG(LogUnrealSoulslike, Log, TEXT("Input check: dying first, to test the respawned knight"));
					WeakThis->LoseHealth(100000.f, nullptr);
				}
			}, 1.f, false);
		}
		else
		{
			RunInputTest();
		}
	}
	if (Team == 0 && FParse::Param(FCommandLine::Get(), TEXT("HandShots")))
	{
		RunHandShots();
	}
	if (Team == 0 && FParse::Param(FCommandLine::Get(), TEXT("ChainShots")))
	{
		RunChainShots();
	}
	if (Team == 0 && FParse::Param(FCommandLine::Get(), TEXT("GrappleDemo")))
	{
		RunGrappleDemo();
	}
	if (Team == 0 && FParse::Param(FCommandLine::Get(), TEXT("ShadowDemo")))
	{
		RunShadowDemo();
	}
	if (Team == 0 && FParse::Param(FCommandLine::Get(), TEXT("ClashDemo")))
	{
		RunClashDemo();
	}
	if (Team == 0 && FParse::Param(FCommandLine::Get(), TEXT("WhistleDemo")))
	{
		RunWhistleDemo();
	}
#if !UE_BUILD_SHIPPING
	// -SwordXform=x,y,z,pitch,yaw,roll[,scale] (and -ShieldXform=) to fit weapons without rebuilding content.
	for (const TPair<const TCHAR*, UStaticMeshComponent*> Override : { TPair<const TCHAR*, UStaticMeshComponent*>(TEXT("SwordXform="), Sword.Get()),
		TPair<const TCHAR*, UStaticMeshComponent*>(TEXT("ShieldXform="), Shield.Get()) })
	{
		FString Value;
		TArray<FString> Parts;
		if (FParse::Value(FCommandLine::Get(), Override.Key, Value, false) && Value.ParseIntoArray(Parts, TEXT(","), true) >= 6)
		{
			Override.Value->SetRelativeLocationAndRotation(
				FVector(FCString::Atof(*Parts[0]), FCString::Atof(*Parts[1]), FCString::Atof(*Parts[2])),
				FRotator(FCString::Atof(*Parts[3]), FCString::Atof(*Parts[4]), FCString::Atof(*Parts[5])));
			if (Parts.Num() >= 7)
			{
				Override.Value->SetRelativeScale3D(FVector(FCString::Atof(*Parts[6])));
			}
		}
	}
#endif
}

// -KnightDemo: walks through the move set against a real enemy and screenshots each beat
// (Saved/Screenshots), so a build can be checked without anyone at the controls.
void ASoulsKnight::RunVerificationDemo()
{
	bVerificationDemo = true;
	GEngine->Exec(GetWorld(), TEXT("r.MotionBlurQuality 0"));
	struct FStep { float Time; TFunction<void(ASoulsKnight*)> Do; };
	auto NearestFoe = [](ASoulsKnight* K) -> ASoulsKnight*
	{
		ASoulsKnight* Best = nullptr;
		for (TActorIterator<ASoulsKnight> It(K->GetWorld()); It; ++It)
		{
			if (It->Team != K->Team && !It->IsDead() && (!Best || K->GetDistanceTo(*It) < K->GetDistanceTo(Best)))
			{
				Best = *It;
			}
		}
		return Best;
	};
	// Scripted scenes stage the knight at its start and bring the nearest foe to it, so the shots
	// never wander off the edge of a small test map.
	const FVector Origin = GetActorLocation();
	const FVector OriginFwd = GetActorForwardVector().GetSafeNormal2D();
	auto Approach = [NearestFoe, Origin, OriginFwd](ASoulsKnight* K, float Distance)
	{
		K->SetActorLocation(Origin, false, nullptr, ETeleportType::TeleportPhysics);
		K->SetActorRotation(OriginFwd.Rotation());
		if (ASoulsKnight* Foe = NearestFoe(K))
		{
			Foe->SetActorLocation(Origin + OriginFwd * Distance + FVector(0.f, 0.f, 5.f), false, nullptr, ETeleportType::TeleportPhysics);
			Foe->FaceTowards(Origin);
		}
		if (K->Controller)
		{
			K->Controller->SetControlRotation(FRotator(-12.f, K->GetActorRotation().Yaw + 35.f, 0.f));
		}
	};
	// Close front and side views to check how the sword and shield sit in the hands.
	auto ViewFrom = [](ASoulsKnight* K, float YawOffset, float Distance)
	{
		const bool bClose = Distance < 400.f;
		K->GetCameraBoom()->TargetArmLength = Distance;
		K->GetCameraBoom()->SocketOffset = FVector(0.f, 0.f, bClose ? 0.f : 55.f);
		K->GetCameraBoom()->bEnableCameraLag = !bClose;
		K->GetCameraBoom()->bEnableCameraRotationLag = !bClose;
		if (K->Controller)
		{
			K->Controller->SetControlRotation(FRotator(bClose ? 0.f : -8.f, K->GetActorRotation().Yaw + YawOffset, 0.f));
		}
	};
	const TArray<FStep> Steps = {
		{ 0.3f, [ViewFrom](ASoulsKnight* K) { ViewFrom(K, 180.f, 170.f); } },
		{ 0.6f, [](ASoulsKnight* K) { K->Shot(TEXT("00a_front")); } },
		{ 0.7f, [ViewFrom](ASoulsKnight* K) { ViewFrom(K, 90.f, 170.f); } },
		{ 1.0f, [](ASoulsKnight* K) { K->Shot(TEXT("00b_left_side")); } },
		{ 1.1f, [ViewFrom](ASoulsKnight* K) { ViewFrom(K, -90.f, 170.f); } },
		{ 1.4f, [](ASoulsKnight* K) { K->Shot(TEXT("00c_right_side")); } },
		{ 1.5f, [ViewFrom](ASoulsKnight* K) { ViewFrom(K, 0.f, K->DefaultCameraDistance); } },
		{ 2.0f, [](ASoulsKnight* K) { K->Shot(TEXT("00_idle")); } },
		{ 2.5f, [](ASoulsKnight* K) { K->DoComboAttackStart(); } },
		{ 2.75f, [](ASoulsKnight* K) { K->Shot(TEXT("01_swing_a")); K->DoComboAttackStart(); } },
		{ 3.3f, [](ASoulsKnight* K) { K->Shot(TEXT("02_swing_b")); K->DoComboAttackStart(); } },
		{ 4.0f, [](ASoulsKnight* K) { K->Shot(TEXT("03_swing_c")); } },
		{ 6.0f, [](ASoulsKnight* K) { K->DoChargedAttackStart(); } },
		{ 6.8f, [](ASoulsKnight* K) { K->Shot(TEXT("04_heavy")); } },
		{ 8.5f, [](ASoulsKnight* K) { K->DodgeToward(K->GetActorRightVector()); } },
		{ 8.75f, [](ASoulsKnight* K) { K->Shot(TEXT("05_roll")); } },
		{ 10.0f, [](ASoulsKnight* K) { K->BlockPressed(); } },
		{ 10.8f, [](ASoulsKnight* K) { K->Shot(TEXT("06_block")); } },
		{ 11.5f, [](ASoulsKnight* K) { K->BlockReleased(); K->LoseHealth(FMath::Min(50.f, K->CurrentHP - 30.f), nullptr); } },
		{ 12.0f, [](ASoulsKnight* K) { K->HealPressed(); } },
		{ 12.8f, [](ASoulsKnight* K) { K->Shot(TEXT("07_heal")); } },
		{ 14.5f, [](ASoulsKnight* K) { K->Jump(); } },
		{ 14.8f, [](ASoulsKnight* K) { K->Shot(TEXT("08_jump")); } },
		{ 16.0f, [Approach](ASoulsKnight* K) { Approach(K, 600.f); K->ToggleLockOn(); } },
		{ 17.0f, [](ASoulsKnight* K) { K->Shot(TEXT("09_lockon")); } },
		{ 18.0f, [](ASoulsKnight* K) { K->ParryPressed(); } },
		{ 18.3f, [](ASoulsKnight* K) { K->Shot(TEXT("10_parry")); } },
		{ 20.0f, [NearestFoe](ASoulsKnight* K) { if (ASoulsKnight* Foe = NearestFoe(K)) { Foe->GetParried(K); } } },
		{ 20.3f, [Approach](ASoulsKnight* K) { Approach(K, 150.f); K->DoComboAttackStart(); } },
		{ 20.9f, [](ASoulsKnight* K) { K->Shot(TEXT("11_riposte")); } },
		{ 24.0f, [](ASoulsKnight* K) { K->Shot(TEXT("12_fight")); } },
		{ 27.0f, [](ASoulsKnight* K) { K->Shot(TEXT("13_fight")); } },
		{ 30.0f, [](ASoulsKnight* K) { K->Shot(TEXT("14_fight")); } },
		// Grapple to a hook point.
		{ 31.0f, [](ASoulsKnight* K)
			{
				K->LockTarget = nullptr;
				for (TActorIterator<AGrapplePoint> It(K->GetWorld()); It; ++It)
				{
					K->SetActorLocation(It->GetActorLocation() - It->GetActorForwardVector() * 900.f - FVector(0.f, 0.f, 330.f), false, nullptr, ETeleportType::TeleportPhysics);
					if (K->Controller)
					{
						K->Controller->SetControlRotation((It->GetActorLocation() - K->GetFollowCamera()->GetComponentLocation()).Rotation());
					}
					break;
				}
			} },
		{ 31.3f, [](ASoulsKnight* K) { K->GrapplePressed(); } },
		{ 31.6f, [](ASoulsKnight* K) { K->Shot(TEXT("15_grapple_fly")); } },
		{ 33.0f, [](ASoulsKnight* K) { K->Shot(TEXT("16_grapple_land")); } },
		// Reel a foe in.
		{ 34.0f, [Approach, NearestFoe](ASoulsKnight* K) { Approach(K, 1000.f); K->LockTarget = NearestFoe(K); } },
		{ 34.3f, [](ASoulsKnight* K) { K->GrapplePressed(); } },
		{ 34.65f, [](ASoulsKnight* K) { K->Shot(TEXT("17_reel")); } },
		{ 35.3f, [](ASoulsKnight* K) { K->Shot(TEXT("18_reeled")); K->DoComboAttackStart(); } },
		{ 36.0f, [](ASoulsKnight* K) { K->Shot(TEXT("19_reel_crit")); } },
		// A foe pounces: brace and impale it.
		{ 38.0f, [NearestFoe, Approach](ASoulsKnight* K)
			{
				K->LockTarget = nullptr;
				Approach(K, 800.f);
				if (ASoulsKnight* Foe = NearestFoe(K))
				{
					Foe->Interrupt();
					Foe->EndAction();
					if (K->Controller)
					{
						K->Controller->SetControlRotation(FRotator(-8.f, K->GetActorRotation().Yaw + 55.f, 0.f));
					}
				}
			} },
		// Let the foe settle on the ground first (leapers only spring from their feet).
		{ 38.6f, [NearestFoe](ASoulsKnight* K) { if (ASoulsKnight* Foe = NearestFoe(K)) { Foe->BeginPounce(K); } } },
		{ 39.05f, [](ASoulsKnight* K) { K->ParryPressed(); } },
		{ 39.5f, [](ASoulsKnight* K) { K->Shot(TEXT("20_pounce")); } },
		{ 40.05f, [](ASoulsKnight* K) { K->Shot(TEXT("21_impaled")); } },
		{ 41.0f, [](ASoulsKnight* K) { K->Shot(TEXT("22_rolled_off")); } },
		// Ambush: a foe caught by surprise freezes; terror spreads.
		{ 42.0f, [Approach, NearestFoe](ASoulsKnight* K) { Approach(K, 170.f); if (ASoulsKnight* Foe = NearestFoe(K)) { Foe->Shock(2.5f); } } },
		{ 42.4f, [](ASoulsKnight* K) { K->Shot(TEXT("23_shocked")); K->DoComboAttackStart(); } },
		{ 43.2f, [](ASoulsKnight* K) { K->Shot(TEXT("24_ambush_crit")); } },
		{ 45.0f, [Approach, NearestFoe](ASoulsKnight* K) { Approach(K, 500.f); if (ASoulsKnight* Foe = NearestFoe(K)) { Foe->AddFear(80.f); } } },
		{ 46.5f, [](ASoulsKnight* K) { K->Shot(TEXT("25_terrified")); } },
		// Something glinting off the path: the nearest one to the start.
		{ 48.0f, [Origin](ASoulsKnight* K)
			{
				ASoulsPickup* Best = nullptr;
				for (TActorIterator<ASoulsPickup> It(K->GetWorld()); It; ++It)
				{
					if (!Best || FVector::Dist(It->GetActorLocation(), Origin) < FVector::Dist(Best->GetActorLocation(), Origin))
					{
						Best = *It;
					}
				}
				if (Best)
				{
					const FVector Back = (Origin - Best->GetActorLocation()).GetSafeNormal2D();
					K->SetActorLocation(Best->GetActorLocation() + Back * 140.f + FVector(0.f, 0.f, 70.f), false, nullptr, ETeleportType::TeleportPhysics);
					K->FaceTowards(Best->GetActorLocation());
					if (K->Controller)
					{
						K->Controller->SetControlRotation(FRotator(-20.f, K->GetActorRotation().Yaw, 0.f));
					}
				}
			} },
		{ 48.6f, [](ASoulsKnight* K) { K->Shot(TEXT("26_pickup")); K->InteractPressed(); } },
		{ 49.2f, [](ASoulsKnight* K) { K->Shot(TEXT("27_acquired")); } },
		// Into the sea behind the start: it should swim, not walk the seabed.
		{ 51.0f, [Origin, OriginFwd](ASoulsKnight* K)
			{
				// The nearest spot whose seabed lies well below the waterline (z = 0).
				FVector Deep = Origin - OriginFwd * 3500.f;
				bool bFound = false;
				for (float Radius = 1500.f; Radius < 20000.f && !bFound; Radius += 1000.f)
				{
					for (int32 Step = 0; Step < 24 && !bFound; ++Step)
					{
						const FVector Probe = Origin + FRotator(0.f, Step * 15.f, 0.f).Vector() * Radius;
						FHitResult Hit;
						const bool bHit = K->GetWorld()->LineTraceSingleByChannel(Hit, FVector(Probe.X, Probe.Y, 3000.f), FVector(Probe.X, Probe.Y, -5000.f), ECC_Visibility);
						UE_LOG(LogUnrealSoulslike, Verbose, TEXT("Swim probe r=%.0f step=%d hit=%d z=%.0f by %s"), Radius, Step, bHit ? 1 : 0, bHit ? Hit.ImpactPoint.Z : 0.f,
							bHit && Hit.GetActor() ? *Hit.GetActor()->GetName() : TEXT("-"));
						if (bHit && Hit.ImpactPoint.Z < -250.f)
						{
							Deep = FVector(Probe.X, Probe.Y, 0.f);
							bFound = true;
						}
					}
				}
				UE_LOG(LogUnrealSoulslike, Log, TEXT("Swim check: deep water %s at %s"), bFound ? TEXT("found") : TEXT("NOT found"), *Deep.ToString());
				K->SetActorLocation(FVector(Deep.X, Deep.Y, 120.f), false, nullptr, ETeleportType::TeleportPhysics);
				K->SetActorRotation((Origin - Deep).GetSafeNormal2D().Rotation());
				if (K->Controller)
				{
					K->Controller->SetControlRotation(FRotator(-15.f, K->GetActorRotation().Yaw + 150.f, 0.f));
				}
			} },

		{ 54.0f, [](ASoulsKnight* K)
			{
				UE_LOG(LogUnrealSoulslike, Log, TEXT("Swim check: swimming=%d mode=%d z=%.0f stamina=%.0f"), K->IsSwimming() ? 1 : 0,
					static_cast<int32>(K->GetCharacterMovement()->MovementMode.GetValue()), K->GetActorLocation().Z, K->Stamina);
				K->Shot(TEXT("28_swim"));
			} },
	};
	for (const FStep& Step : Steps)
	{
		FTimerHandle Handle;
		const TWeakObjectPtr<ASoulsKnight> WeakThis(this);
		const TFunction<void(ASoulsKnight*)> Do = Step.Do;
		GetWorldTimerManager().SetTimer(Handle, [WeakThis, Do]()
		{
			if (WeakThis.IsValid())
			{
				Do(WeakThis.Get());
			}
		}, Step.Time, false);
	}
}

void ASoulsKnight::RunStealthDemo()
{
	GEngine->Exec(GetWorld(), TEXT("r.MotionBlurQuality 0"));
	struct FStep { float Time; TFunction<void(ASoulsKnight*)> Do; };
	// Watch from the side, close and low.
	auto SideView = [](ASoulsKnight* K)
	{
		K->GetCameraBoom()->TargetArmLength = 380.f;
		K->GetCameraBoom()->SocketOffset = FVector(0.f, 0.f, 10.f);
		if (K->Controller)
		{
			K->Controller->SetControlRotation(FRotator(-12.f, K->GetActorRotation().Yaw + 70.f, 0.f));
		}
	};
	const TArray<FStep> Steps = {
		{ 1.0f, [SideView](ASoulsKnight* K)
			{
				// The nearest ordinary foe becomes a sentry looking the other way.
				ASoulsEnemy* Best = nullptr;
				for (TActorIterator<ASoulsEnemy> It(K->GetWorld()); It; ++It)
				{
					if (!It->bIsBoss && !It->IsDead() && (!Best || K->GetDistanceTo(*It) < K->GetDistanceTo(Best)))
					{
						Best = *It;
					}
				}
				if (!Best)
				{
					UE_LOG(LogUnrealSoulslike, Warning, TEXT("Stealth check: no foe to sneak up on"));
					return;
				}
				Best->PatrolRadius = 0.f;
				Best->GetCharacterMovement()->StopMovementImmediately();
				K->DemoVictim = Best;
				const FVector Behind = -Best->GetActorForwardVector().GetSafeNormal2D();
				K->SetActorLocation(Best->GetActorLocation() + Behind * 900.f + FVector(0.f, 0.f, 40.f), false, nullptr, ETeleportType::TeleportPhysics);
				K->FaceTowards(Best->GetActorLocation());
				SideView(K);
				K->DiveProne();
			} },
		{ 1.8f, [](ASoulsKnight* K) { K->Shot(TEXT("Stealth_1_prone")); } },
		{ 2.0f, [](ASoulsKnight* K) { K->DemoMove = K->GetActorForwardVector(); } },
		{ 3.4f, [](ASoulsKnight* K) { K->Shot(TEXT("Stealth_2_crawl")); } },
		{ 3.6f, [](ASoulsKnight* K) { K->DemoMove = FVector::ZeroVector; K->GetUpFromProne(); } },
		{ 3.9f, [](ASoulsKnight* K) { K->Shot(TEXT("Stealth_3_getting_up")); } },
		{ 5.0f, [](ASoulsKnight* K) { K->SetStance(EKnightStance::Crouched); } },
		{ 5.6f, [](ASoulsKnight* K) { K->Shot(TEXT("Stealth_4_crouch")); K->bDemoStalk = true; K->DemoStalkStart = K->GetWorld()->GetTimeSeconds(); } },
	};
	for (const FStep& Step : Steps)
	{
		FTimerHandle Handle;
		const TWeakObjectPtr<ASoulsKnight> WeakThis(this);
		const TFunction<void(ASoulsKnight*)> Do = Step.Do;
		GetWorldTimerManager().SetTimer(Handle, [WeakThis, Do]() { if (WeakThis.IsValid()) { Do(WeakThis.Get()); } }, Step.Time, false);
	}
}

void ASoulsKnight::TickStealthDemo()
{
	if (!DemoMove.IsNearlyZero())
	{
		AddMovementInput(DemoMove, 1.f);
	}
	ASoulsKnight* Victim = DemoVictim.Get();
	if (!bDemoStalk || !Victim)
	{
		return;
	}
	const float Now = GetWorld()->GetTimeSeconds();
	const ASoulsEnemyController* Mind = Cast<ASoulsEnemyController>(Victim->GetController());
	const bool bNoticed = Mind && Mind->GetMind() != EEnemyMind::Idle;
	const FVector Spot = Victim->GetActorLocation() - Victim->GetActorForwardVector().GetSafeNormal2D() * 150.f;
	const float Distance = FVector::Dist2D(GetActorLocation(), Victim->GetActorLocation());
	if (bNoticed || Now - DemoStalkStart > 20.f)
	{
		UE_LOG(LogUnrealSoulslike, Warning, TEXT("Stealth check: FAILED (noticed=%d, distance=%.0f)"), bNoticed ? 1 : 0, Distance);
		bDemoStalk = false;
		Shot(TEXT("Stealth_5_backstab"));
		return;
	}
	if (Distance > 200.f)
	{
		AddMovementInput((Spot - GetActorLocation()).GetSafeNormal2D(), 1.f);
		return;
	}
	bDemoStalk = false;
	FaceTowards(Victim->GetActorLocation());
	DoComboAttackStart();
	UE_LOG(LogUnrealSoulslike, Log, TEXT("Stealth check: reached %.0f cm behind an unaware foe; action=%d (critical=%d)"), Distance,
		static_cast<int32>(Action), Action == EKnightAction::Critical ? 1 : 0);
	FTimerHandle Handle;
	const TWeakObjectPtr<ASoulsKnight> WeakThis(this);
	GetWorldTimerManager().SetTimer(Handle, [WeakThis]() { if (WeakThis.IsValid()) { WeakThis->Shot(TEXT("Stealth_5_backstab")); } }, 0.7f, false);
}

void ASoulsKnight::InputTestKey(const FKey& Key, bool bDown)
{
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		PC->InputKey(FInputKeyEventArgs::CreateSimulated(Key, bDown ? IE_Pressed : IE_Released, bDown ? 1.f : 0.f));
	}
}

void ASoulsKnight::RunInputTest()
{
	GEngine->Exec(GetWorld(), TEXT("r.MotionBlurQuality 0"));
	struct FStep { float Time; TFunction<void(ASoulsKnight*)> Do; };
	auto Check = [](const TCHAR* What, bool bPass, const FString& Detail)
	{
		UE_LOG(LogUnrealSoulslike, Log, TEXT("Input check: %s %s (%s)"), What, bPass ? TEXT("PASS") : TEXT("FAIL"), *Detail);
	};
	auto Side = [](ASoulsKnight* K)
	{
		K->GetCameraBoom()->TargetArmLength = 420.f;
		if (K->Controller)
		{
			K->Controller->SetControlRotation(FRotator(-12.f, K->GetActorRotation().Yaw + 75.f, 0.f));
		}
	};
	auto Tap = [](ASoulsKnight* K, const FKey& Key, float Hold)
	{
		K->InputTestKey(Key, true);
		FTimerHandle Handle;
		const TWeakObjectPtr<ASoulsKnight> WeakK(K);
		K->GetWorldTimerManager().SetTimer(Handle, [WeakK, Key]() { if (WeakK.IsValid()) { WeakK->InputTestKey(Key, false); } }, Hold, false);
	};
	const FKey B = EKeys::Gamepad_FaceButton_Right;
	// The foe's health and the knight's spot when the light attack is pressed.
	const TSharedRef<TPair<float, FVector>> Probe = MakeShared<TPair<float, FVector>>(0.f, FVector::ZeroVector);
	const TArray<FStep> Steps = {
		{ 1.0f, [Side](ASoulsKnight* K) { Side(K); } },
		{ 1.5f, [Tap, B, Probe](ASoulsKnight* K) { Probe->Key = K->GetActorRotation().Yaw; Tap(K, B, 0.1f); } },
		{ 1.62f, [Check](ASoulsKnight* K) { Check(TEXT("B standing still rolls"), K->Action == EKnightAction::Roll, FString::Printf(TEXT("action=%d"), (int32)K->Action)); K->Shot(TEXT("Input_1_roll")); } },
		{ 2.3f, [Check, Probe](ASoulsKnight* K)
			{
				const float Turned = FMath::Abs(FRotator::NormalizeAxis(K->GetActorRotation().Yaw - Probe->Key));
				Check(TEXT("a roll with no stick keeps the facing"), Turned < 5.f, FString::Printf(TEXT("turned %.1f deg"), Turned));
			} },
		{ 2.4f, [](ASoulsKnight* K) { K->InputTestKey(EKeys::W, true); } },
		{ 3.0f, [Tap, B](ASoulsKnight* K) { Tap(K, B, 0.1f); } },
		{ 3.12f, [Check](ASoulsKnight* K) { Check(TEXT("B on the move rolls"), K->Action == EKnightAction::Roll, FString::Printf(TEXT("action=%d"), (int32)K->Action)); K->Shot(TEXT("Input_2_roll")); } },
		{ 3.3f, [Check](ASoulsKnight* K)
			{
				const float Off = FMath::Abs(FRotator::NormalizeAxis(K->GetActorRotation().Yaw - K->GetVelocity().Rotation().Yaw));
				Check(TEXT("a roll with the stick faces the stick"), Off < 10.f, FString::Printf(TEXT("off %.1f deg"), Off));
			} },
		{ 3.9f, [Tap](ASoulsKnight* K) { Tap(K, EKeys::Gamepad_LeftThumbstick, 0.08f); } },
		{ 4.9f, [Check](ASoulsKnight* K) { Check(TEXT("tap L3 sprints"), K->bSprinting && K->GetVelocity().Size2D() > 650.f, FString::Printf(TEXT("sprinting=%d speed=%.0f"), K->bSprinting, K->GetVelocity().Size2D())); K->Shot(TEXT("Input_3_sprint")); } },
		{ 5.0f, [](ASoulsKnight* K) { K->InputTestKey(EKeys::W, false); } },
		{ 5.8f, [Check](ASoulsKnight* K) { Check(TEXT("letting go of the stick ends the sprint"), !K->bSprintToggled, FString::Printf(TEXT("toggled=%d"), K->bSprintToggled)); } },
		{ 6.0f, [](ASoulsKnight* K) { K->InputTestKey(EKeys::Gamepad_LeftShoulder, true); } },
		{ 6.5f, [Check](ASoulsKnight* K) { Check(TEXT("hold LB guards"), K->bBlocking, FString::Printf(TEXT("blocking=%d"), K->bBlocking)); K->Shot(TEXT("Input_4_guard")); } },
		{ 6.7f, [](ASoulsKnight* K) { K->InputTestKey(EKeys::Gamepad_LeftShoulder, false); } },
		{ 7.2f, [Tap](ASoulsKnight* K) { Tap(K, EKeys::Gamepad_FaceButton_Bottom, 0.15f); } },
		{ 7.45f, [Check](ASoulsKnight* K) { Check(TEXT("A jumps"), K->GetCharacterMovement()->IsFalling(), FString::Printf(TEXT("vz=%.0f"), K->GetVelocity().Z)); K->Shot(TEXT("Input_5_jump")); } },
		{ 8.4f, [Tap](ASoulsKnight* K) { Tap(K, EKeys::Gamepad_RightThumbstick, 0.08f); } },
		{ 8.8f, [Check](ASoulsKnight* K) { Check(TEXT("tap R3 crouches"), K->Stance == EKnightStance::Crouched, FString::Printf(TEXT("stance=%d"), (int32)K->Stance)); K->Shot(TEXT("Input_6_crouch")); } },
		{ 9.0f, [Tap](ASoulsKnight* K) { Tap(K, EKeys::Gamepad_RightThumbstick, 0.08f); } },
		{ 9.4f, [Check](ASoulsKnight* K) { Check(TEXT("tap R3 again stands"), K->Stance == EKnightStance::Standing, FString::Printf(TEXT("stance=%d"), (int32)K->Stance)); } },
		{ 9.8f, [Tap](ASoulsKnight* K) { Tap(K, EKeys::Gamepad_FaceButton_Top, 0.08f); } },
		{ 10.3f, [Check](ASoulsKnight* K) { Check(TEXT("Y does nothing"), K->Action == EKnightAction::None && K->GrappleMode == EGrappleMode::None && !K->bBlocking, FString::Printf(TEXT("action=%d grapple=%d"), (int32)K->Action, (int32)K->GrappleMode)); } },
		// Reach: a foe (looking away) 3.3 m ahead; one tap of X must step in and land the blow.
		{ 13.5f, [Side](ASoulsKnight* K)
			{
				ASoulsEnemy* Best = nullptr;
				for (TActorIterator<ASoulsEnemy> It(K->GetWorld()); It; ++It)
				{
					if (!It->bIsBoss && !It->IsDead() && (!Best || K->GetDistanceTo(*It) < K->GetDistanceTo(Best)))
					{
						Best = *It;
					}
				}
				if (!Best)
				{
					return;
				}
				Best->PatrolRadius = 0.f;
				K->DemoVictim = Best;
				const FVector Ahead = K->GetActorForwardVector().GetSafeNormal2D();
				Best->SetActorLocation(K->GetActorLocation() + Ahead * 330.f + FVector(0.f, 0.f, 20.f), false, nullptr, ETeleportType::TeleportPhysics);
				Best->SetActorRotation(Ahead.Rotation());
				Best->GetCharacterMovement()->StopMovementImmediately();
				Side(K);
			} },
		{ 14.0f, [Tap, Probe](ASoulsKnight* K)
			{
				if (K->DemoVictim.IsValid())
				{
					Probe->Key = K->DemoVictim->GetHealth();
					Probe->Value = K->GetActorLocation();
				}
				Tap(K, EKeys::Gamepad_FaceButton_Left, 0.08f);
			} },
		{ 14.1f, [Check](ASoulsKnight* K) { Check(TEXT("X is the one normal attack"), K->Action == EKnightAction::HeavyAttack, FString::Printf(TEXT("action=%d"), (int32)K->Action)); } },
		{ 14.6f, [](ASoulsKnight* K) { K->Shot(TEXT("Input_7_attack")); } },
		{ 15.0f, [](ASoulsKnight* K) { K->Shot(TEXT("Input_7b_blood")); } },
		{ 15.8f, [](ASoulsKnight* K) { K->Shot(TEXT("Input_7c_splats")); } },
		{ 16.0f, [Check, Probe](ASoulsKnight* K)
			{
				const ASoulsKnight* Foe = K->DemoVictim.Get();
				Check(TEXT("the attack reaches a foe 3.3 m away"), Foe && Foe->GetHealth() < Probe->Key,
					FString::Printf(TEXT("hp %.0f -> %.0f, stepped %.0f cm"), Probe->Key, Foe ? Foe->GetHealth() : -1.f, FVector::Dist2D(Probe->Value, K->GetActorLocation())));
				K->DemoVictim = nullptr;
			} },
		// The keyboard mirrors the pad.
		{ 17.0f, [Tap](ASoulsKnight* K) { Tap(K, EKeys::E, 0.1f); } },
		{ 17.12f, [Check](ASoulsKnight* K) { Check(TEXT("E rolls"), K->Action == EKnightAction::Roll, FString::Printf(TEXT("action=%d"), (int32)K->Action)); } },
		{ 18.0f, [](ASoulsKnight* K) { K->InputTestKey(EKeys::RightMouseButton, true); } },
		{ 18.4f, [Check](ASoulsKnight* K) { Check(TEXT("RMB raises the shield"), K->bBlocking, FString::Printf(TEXT("blocking=%d"), K->bBlocking)); } },
		{ 18.5f, [](ASoulsKnight* K) { K->InputTestKey(EKeys::RightMouseButton, false); } },
		{ 18.9f, [Tap](ASoulsKnight* K) { Tap(K, EKeys::SpaceBar, 0.15f); } },
		{ 19.15f, [Check](ASoulsKnight* K) { Check(TEXT("Space jumps"), K->GetCharacterMovement()->IsFalling(), FString::Printf(TEXT("vz=%.0f"), K->GetVelocity().Z)); } },
		{ 20.2f, [Tap](ASoulsKnight* K) { Tap(K, EKeys::LeftControl, 0.08f); } },
		{ 20.5f, [Check](ASoulsKnight* K) { Check(TEXT("Ctrl crouches"), K->Stance == EKnightStance::Crouched, FString::Printf(TEXT("stance=%d"), (int32)K->Stance)); } },
		{ 20.7f, [Tap](ASoulsKnight* K) { Tap(K, EKeys::LeftShift, 0.08f); K->InputTestKey(EKeys::W, true); } },
		{ 21.7f, [Check](ASoulsKnight* K) { Check(TEXT("Shift runs"), K->bSprinting, FString::Printf(TEXT("speed=%.0f"), K->GetVelocity().Size2D())); K->InputTestKey(EKeys::W, false); } },
		{ 22.4f, [Tap](ASoulsKnight* K) { Tap(K, EKeys::LeftMouseButton, 0.08f); } },
		{ 22.55f, [Check](ASoulsKnight* K) { Check(TEXT("LMB attacks"), K->Action == EKnightAction::HeavyAttack, FString::Printf(TEXT("action=%d"), (int32)K->Action)); } },
		{ 24.6f, [Tap](ASoulsKnight* K) { Tap(K, EKeys::Gamepad_FaceButton_Left, 0.08f); } },
		{ 24.8f, [Tap, B](ASoulsKnight* K) { Tap(K, B, 0.08f); } },
		{ 24.95f, [Check](ASoulsKnight* K)
			{
				Check(TEXT("B early in a swing rolls out of it"), K->Action == EKnightAction::Roll && K->GetWorldTimerManager().GetTimerRemaining(K->HitTimer) <= 0.f,
					FString::Printf(TEXT("action=%d hitTimer=%.2f"), (int32)K->Action, K->GetWorldTimerManager().GetTimerRemaining(K->HitTimer)));
			} },
		{ 26.0f, [](ASoulsKnight* K) { K->Shot(TEXT("Input_8_done")); } },
	};
	for (const FStep& Step : Steps)
	{
		FTimerHandle Handle;
		const TWeakObjectPtr<ASoulsKnight> WeakThis(this);
		const TFunction<void(ASoulsKnight*)> Do = Step.Do;
		GetWorldTimerManager().SetTimer(Handle, [WeakThis, Do]() { if (WeakThis.IsValid()) { Do(WeakThis.Get()); } }, Step.Time, false);
	}
}

void ASoulsKnight::RunHandShots()
{
	GEngine->Exec(GetWorld(), TEXT("r.MotionBlurQuality 0"));
	GetCameraBoom()->bEnableCameraLag = false;
	GetCameraBoom()->bEnableCameraRotationLag = false;
	struct FView { float Time; float Yaw; float Pitch; float Distance; float Height; const TCHAR* Name; };
	const FView Views[] = {
		{ 4.0f, 180.f, -5.f, 150.f, -75.f, TEXT("Hand_1_front") },
		{ 4.6f, -130.f, -5.f, 120.f, -75.f, TEXT("Hand_2_sword_hand") },
		{ 5.2f, -90.f, 0.f, 120.f, -75.f, TEXT("Hand_3_right") },
		{ 5.8f, 130.f, -5.f, 120.f, -65.f, TEXT("Hand_4_shield_front") },
		{ 6.4f, 90.f, 0.f, 140.f, -65.f, TEXT("Hand_5_left") },
		{ 7.0f, 0.f, -10.f, 170.f, -55.f, TEXT("Hand_6_back") },
		{ 8.0f, 180.f, -5.f, 220.f, -20.f, TEXT("Hand_7_guard_front") },
		{ 8.6f, 140.f, -5.f, 220.f, -20.f, TEXT("Hand_8_guard_left") },
	};
	FTimerHandle Guard;
	const TWeakObjectPtr<ASoulsKnight> WeakGuard(this);
	GetWorldTimerManager().SetTimer(Guard, [WeakGuard]() { if (WeakGuard.IsValid()) { WeakGuard->BlockPressed(); } }, 7.3f, false);
	for (const FView& View : Views)
	{
		const TWeakObjectPtr<ASoulsKnight> WeakThis(this);
		const FView Copy = View;
		FTimerHandle Aim, Snap;
		GetWorldTimerManager().SetTimer(Aim, [WeakThis, Copy]()
		{
			if (ASoulsKnight* K = WeakThis.Get())
			{
				K->GetCameraBoom()->TargetArmLength = Copy.Distance;
				K->GetCameraBoom()->SocketOffset = FVector(0.f, 0.f, Copy.Height);
				if (K->Controller)
				{
					K->Controller->SetControlRotation(FRotator(Copy.Pitch, K->GetActorRotation().Yaw + Copy.Yaw, 0.f));
				}
			}
		}, Copy.Time - 0.25f, false);
		GetWorldTimerManager().SetTimer(Snap, [WeakThis, Copy]() { if (WeakThis.IsValid()) { WeakThis->Shot(Copy.Name); } }, Copy.Time, false);
	}
}

namespace
{
	ASoulsEnemy* ClashNearestFoe(ASoulsKnight* K)
	{
		ASoulsEnemy* Best = nullptr;
		for (TActorIterator<ASoulsEnemy> It(K->GetWorld()); It; ++It)
		{
			if (!It->bIsBoss && !It->IsDead() && !It->bCanPounce && (!Best || K->GetDistanceTo(*It) < K->GetDistanceTo(Best)))
			{
				Best = *It;
			}
		}
		return Best;
	}
}

void ASoulsKnight::RunClashDemo()
{
	GEngine->Exec(GetWorld(), TEXT("r.MotionBlurQuality 0"));
	GetCameraBoom()->bEnableCameraLag = false;
	GetCameraBoom()->bEnableCameraRotationLag = false;
	const TWeakObjectPtr<ASoulsKnight> WeakThis(this);
	FTimerHandle Setup;
	GetWorldTimerManager().SetTimer(Setup, [WeakThis]()
	{
		ASoulsKnight* K = WeakThis.Get();
		ASoulsEnemy* Foe = K ? ClashNearestFoe(K) : nullptr;
		if (!Foe)
		{
			UE_LOG(LogUnrealSoulslike, Warning, TEXT("Clash check: no foe"));
			return;
		}
		K->DemoVictim = Foe;
		Foe->PatrolRadius = 0.f;
		Foe->CurrentHP = Foe->MaxHP * 5.f;
		const FVector Ahead = K->GetActorForwardVector().GetSafeNormal2D();
		Foe->SetActorLocation(K->GetActorLocation() + Ahead * 650.f + FVector(0.f, 0.f, 20.f), false, nullptr, ETeleportType::TeleportPhysics);
		Foe->SetActorRotation((-Ahead).Rotation());
		Foe->HomeLocation = Foe->GetActorLocation();
		if (ASoulsEnemyController* FoeMind = Cast<ASoulsEnemyController>(Foe->GetController()))
		{
			FoeMind->ForceEngage(K);
		}
		UE_LOG(LogUnrealSoulslike, Log, TEXT("Clash check: %s engages from %.0f cm"), *Foe->GetName(), K->GetDistanceTo(Foe));
		K->BlockPressTime = -100.f;
		K->bBlockHeld = true;
		K->SetBlocking(true);
		K->GetCameraBoom()->TargetArmLength = 430.f;
		if (K->Controller)
		{
			K->Controller->SetControlRotation(FRotator(-8.f, K->GetActorRotation().Yaw + 70.f, 0.f));
		}
	}, 1.5f, false);
	// A burst of frames through the exchange; the impacts land somewhere in here.
	for (int32 Index = 0; Index < 28; ++Index)
	{
		FTimerHandle Snap;
		GetWorldTimerManager().SetTimer(Snap, [WeakThis, Index]()
		{
			if (WeakThis.IsValid())
			{
				ASoulsKnight* K = WeakThis.Get();
				if (const ASoulsKnight* Foe = K->DemoVictim.Get())
				{
					const FVector ToFoe = (Foe->GetActorLocation() - K->GetActorLocation()).GetSafeNormal2D();
					if (K->Controller)
					{
						K->Controller->SetControlRotation(FRotator(-10.f, ToFoe.Rotation().Yaw + 55.f, 0.f));
					}
					UE_LOG(LogUnrealSoulslike, Log, TEXT("Clash frame %d: foe %.0f cm, action %d, dz %.0f"), Index, K->GetDistanceTo(Foe),
						static_cast<int32>(Foe->GetAction()), Foe->GetActorLocation().Z - K->GetActorLocation().Z);
				}
				K->Shot(*FString::Printf(TEXT("Clash_%02d"), Index));
			}
		}, 3.f + Index * 0.2f, false);
	}
	FTimerHandle ToParry;
	GetWorldTimerManager().SetTimer(ToParry, [WeakThis]()
	{
		if (ASoulsKnight* K = WeakThis.Get())
		{
			K->bBlockHeld = false;
			K->SetBlocking(false);
			K->bClashParry = true;
			UE_LOG(LogUnrealSoulslike, Log, TEXT("Clash check: guard held, %d blows blocked"), K->ClashBlocks);
		}
	}, 9.f, false);
	FTimerHandle Done;
	GetWorldTimerManager().SetTimer(Done, [WeakThis]()
	{
		if (ASoulsKnight* K = WeakThis.Get())
		{
			UE_LOG(LogUnrealSoulslike, Log, TEXT("Clash check: blocked=%d parried=%d %s"), K->ClashBlocks, K->ClashParries,
				K->ClashBlocks > 0 && K->ClashParries > 0 ? TEXT("PASS") : TEXT("FAIL"));
			K->bClashParry = false;
			K->Shot(TEXT("Clash_done"));
		}
	}, 17.f, false);
}

void ASoulsKnight::TickClashDemo()
{
	ASoulsKnight* Foe = DemoVictim.Get();
	if (!bClashParry || !Foe || Foe->IsDead())
	{
		return;
	}
	// Raise the shield just as the blade comes down.
	const float Left = GetWorldTimerManager().GetTimerRemaining(Foe->HitTimer);
	// The foe's blade goes live mid-swing: raise the shield just before it does.
	const float ToBlade = Foe->BladeLiveFrom - GetWorld()->GetTimeSeconds();
	if (Foe->IsAttacking() && Left > 0.f && ToBlade < 0.1f && ToBlade > -0.05f && !bBlocking)
	{
		UE_LOG(LogUnrealSoulslike, Verbose, TEXT("Clash demo: guard up at %.2f s before the strike"), Left);
		BlockPressed();
		const TWeakObjectPtr<ASoulsKnight> WeakThis(this);
		FTimerHandle Snap, Lower;
		GetWorldTimerManager().SetTimer(Snap, [WeakThis]()
		{
			if (WeakThis.IsValid())
			{
				WeakThis->Shot(*FString::Printf(TEXT("Clash_parry_%d"), WeakThis->ClashParries));
			}
		}, Left + 0.05f, false);
		GetWorldTimerManager().SetTimer(Lower, [WeakThis]() { if (WeakThis.IsValid()) { WeakThis->BlockReleased(); } }, 0.5f, false);
	}
}

void ASoulsKnight::RunWhistleDemo()
{
	GEngine->Exec(GetWorld(), TEXT("r.MotionBlurQuality 0"));
	const TWeakObjectPtr<ASoulsKnight> WeakThis(this);
	const TSharedRef<float> StartDistance = MakeShared<float>(0.f);
	FTimerHandle Setup, Call, Check, Late;
	GetWorldTimerManager().SetTimer(Setup, [WeakThis, StartDistance]()
	{
		ASoulsKnight* K = WeakThis.Get();
		ASoulsEnemy* Foe = K ? ClashNearestFoe(K) : nullptr;
		if (!Foe)
		{
			return;
		}
		K->DemoVictim = Foe;
		// Behind it and well back, out of its sight.
		const FVector Behind = -Foe->GetActorForwardVector().GetSafeNormal2D();
		K->SetActorLocation(Foe->GetActorLocation() + Behind * 1400.f + FVector(0.f, 0.f, 60.f), false, nullptr, ETeleportType::TeleportPhysics);
		K->FaceTowards(Foe->GetActorLocation());
		if (K->Controller)
		{
			K->Controller->SetControlRotation(FRotator(-10.f, K->GetActorRotation().Yaw, 0.f));
		}
		*StartDistance = FVector::Dist2D(K->GetActorLocation(), Foe->GetActorLocation());
	}, 1.5f, false);
	GetWorldTimerManager().SetTimer(Call, [WeakThis]()
	{
		if (WeakThis.IsValid())
		{
			WeakThis->InteractPressed();
			WeakThis->Shot(TEXT("Whistle_1"));
		}
	}, 2.5f, false);
	GetWorldTimerManager().SetTimer(Check, [WeakThis]() { if (WeakThis.IsValid()) { WeakThis->Shot(TEXT("Whistle_2_coming")); } }, 6.5f, false);
	GetWorldTimerManager().SetTimer(Late, [WeakThis, StartDistance]()
	{
		ASoulsKnight* K = WeakThis.Get();
		const ASoulsKnight* Foe = K ? K->DemoVictim.Get() : nullptr;
		if (!Foe)
		{
			return;
		}
		const float Distance = FVector::Dist2D(K->GetActorLocation(), Foe->GetActorLocation());
		const ASoulsEnemyController* Mind = Cast<ASoulsEnemyController>(Foe->GetController());
		UE_LOG(LogUnrealSoulslike, Log, TEXT("Whistle check: %.0f -> %.0f cm (curious=%d, mind=%d) %s"), *StartDistance, Distance,
			Mind && Mind->IsCurious() ? 1 : 0, Mind ? static_cast<int32>(Mind->GetMind()) : -1,
			Distance < *StartDistance - 400.f ? TEXT("PASS") : TEXT("FAIL"));
		K->Shot(TEXT("Whistle_3"));
	}, 10.f, false);
}

void ASoulsKnight::RunShadowDemo()
{
	GEngine->Exec(GetWorld(), TEXT("r.MotionBlurQuality 0"));
	GetCameraBoom()->bEnableCameraLag = false;
	const TWeakObjectPtr<ASoulsKnight> WeakThis(this);
	const TSharedRef<float> StartZ = MakeShared<float>(0.f);
	auto Log = [](const FString& Text) { UE_LOG(LogUnrealSoulslike, Log, TEXT("Shadow check: %s"), *Text); };
	FTimerHandle Setup, Melt, Crawl, Shot1, Shot2, Stop, Rise, Done;
	GetWorldTimerManager().SetTimer(Setup, [WeakThis, StartZ, Log]()
	{
		ASoulsKnight* K = WeakThis.Get();
		AGrapplePoint* Best = nullptr;
		for (TActorIterator<AGrapplePoint> It(K->GetWorld()); It; ++It)
		{
			if (!Best || K->GetDistanceTo(*It) < K->GetDistanceTo(Best))
			{
				Best = *It;
			}
		}
		if (!Best)
		{
			Log(TEXT("no wall to climb FAIL"));
			return;
		}
		// Four metres out from the wall under a grapple hook, facing it.
		const FVector Out = -Best->GetActorForwardVector().GetSafeNormal2D();
		FVector Spot = Best->GetActorLocation() + Out * 450.f;
		FHitResult Ground;
		if (K->GetWorld()->LineTraceSingleByChannel(Ground, Spot + FVector(0.f, 0.f, 3000.f), Spot - FVector(0.f, 0.f, 6000.f), ECC_Visibility))
		{
			Spot = Ground.ImpactPoint + FVector(0.f, 0.f, 100.f);
		}
		K->SetActorLocation(Spot, false, nullptr, ETeleportType::TeleportPhysics);
		K->SetActorRotation(FRotator(0.f, (-Out).Rotation().Yaw, 0.f));
		if (K->Controller)
		{
			K->Controller->SetControlRotation(FRotator(-15.f, (-Out).Rotation().Yaw, 0.f));
		}
	}, 3.f, false);
	GetWorldTimerManager().SetTimer(Melt, [WeakThis, Log]()
	{
		if (ASoulsKnight* K = WeakThis.Get())
		{
			K->InputTestKey(EKeys::Gamepad_LeftTrigger, true);
			K->InputTestKey(EKeys::Gamepad_LeftTrigger, false);
			K->Shot(TEXT("Shadow_1_melted"));
		}
	}, 3.8f, false);
	GetWorldTimerManager().SetTimer(Crawl, [WeakThis, StartZ, Log]()
	{
		if (ASoulsKnight* K = WeakThis.Get())
		{
			Log(FString::Printf(TEXT("melted %s"), K->ShadowMelt && K->ShadowMelt->IsMerged() ? TEXT("PASS") : TEXT("FAIL")));
			*StartZ = K->GetActorLocation().Z;
			K->InputTestKey(EKeys::W, true);
		}
	}, 4.2f, false);
	GetWorldTimerManager().SetTimer(Shot1, [WeakThis]() { if (WeakThis.IsValid()) { WeakThis->Shot(TEXT("Shadow_2_crawl")); } }, 5.2f, false);
	GetWorldTimerManager().SetTimer(Shot2, [WeakThis]() { if (WeakThis.IsValid()) { WeakThis->Shot(TEXT("Shadow_3_wall")); } }, 7.2f, false);
	GetWorldTimerManager().SetTimer(Stop, [WeakThis, StartZ, Log]()
	{
		if (ASoulsKnight* K = WeakThis.Get())
		{
			K->InputTestKey(EKeys::W, false);
			const float Climbed = K->GetActorLocation().Z - *StartZ;
			Log(FString::Printf(TEXT("crawled up %.0f cm %s"), Climbed, Climbed > 150.f ? TEXT("PASS") : TEXT("FAIL")));
		}
	}, 8.5f, false);
	GetWorldTimerManager().SetTimer(Rise, [WeakThis]()
	{
		if (ASoulsKnight* K = WeakThis.Get())
		{
			K->InputTestKey(EKeys::Gamepad_LeftTrigger, true);
			K->InputTestKey(EKeys::Gamepad_LeftTrigger, false);
		}
	}, 9.f, false);
	GetWorldTimerManager().SetTimer(Done, [WeakThis, Log]()
	{
		if (ASoulsKnight* K = WeakThis.Get())
		{
			Log(FString::Printf(TEXT("LT again rises %s"), K->ShadowMelt && !K->ShadowMelt->IsMerged() ? TEXT("PASS") : TEXT("FAIL")));
			K->Shot(TEXT("Shadow_4_risen"));
		}
	}, 10.f, false);
}

void ASoulsKnight::RunGrappleDemo()
{
	GEngine->Exec(GetWorld(), TEXT("r.MotionBlurQuality 0"));
	GetCameraBoom()->bEnableCameraLag = false;
	struct FGrappleProbe { float StartZ = 0.f; float PeakZ = -1e9f; bool bLaunched = false; bool bGlided = false; float GlideStart = 0.f; };
	const TSharedRef<FGrappleProbe> Probe = MakeShared<FGrappleProbe>();
	const TWeakObjectPtr<ASoulsKnight> WeakThis(this);
	auto Log = [](const FString& Text) { UE_LOG(LogUnrealSoulslike, Log, TEXT("Grapple check: %s"), *Text); };
	auto Tap = [](ASoulsKnight* K, const FKey& Key, float Hold)
	{
		K->InputTestKey(Key, true);
		FTimerHandle Handle;
		const TWeakObjectPtr<ASoulsKnight> WeakK(K);
		K->GetWorldTimerManager().SetTimer(Handle, [WeakK, Key]() { if (WeakK.IsValid()) { WeakK->InputTestKey(Key, false); } }, Hold, false);
	};
	FTimerHandle Setup;
	GetWorldTimerManager().SetTimer(Setup, [WeakThis, Probe, Log]()
	{
		ASoulsKnight* K = WeakThis.Get();
		AGrapplePoint* Best = nullptr;
		for (TActorIterator<AGrapplePoint> It(K->GetWorld()); It; ++It)
		{
			if (!Best || K->GetDistanceTo(*It) < K->GetDistanceTo(Best))
			{
				Best = *It;
			}
		}
		if (!Best)
		{
			Log(TEXT("no grapple point FAIL"));
			return;
		}
		// Stand 12 m out from the hook, at ground level, looking at it.
		const FVector Out = -Best->GetActorForwardVector().GetSafeNormal2D();
		FVector Spot = Best->GetActorLocation() + Out * 1200.f;
		FHitResult Ground;
		if (K->GetWorld()->LineTraceSingleByChannel(Ground, Spot + FVector(0.f, 0.f, 3000.f), Spot - FVector(0.f, 0.f, 6000.f), ECC_Visibility))
		{
			Spot = Ground.ImpactPoint + FVector(0.f, 0.f, 100.f);
		}
		K->SetActorLocation(Spot, false, nullptr, ETeleportType::TeleportPhysics);
		const FVector ToHook = Best->GetActorLocation() - K->GetActorLocation();
		K->SetActorRotation(FRotator(0.f, ToHook.Rotation().Yaw, 0.f));
		if (K->Controller)
		{
			K->Controller->SetControlRotation(ToHook.Rotation());
		}
		Probe->StartZ = K->GetActorLocation().Z;
		Log(FString::Printf(TEXT("standing %.0f cm from %s"), ToHook.Size(), *Best->GetName()));
	}, 3.f, false);
	FTimerHandle Fire;
	GetWorldTimerManager().SetTimer(Fire, [WeakThis, Tap]() { if (WeakThis.IsValid()) { Tap(WeakThis.Get(), EKeys::Gamepad_RightTrigger, 0.1f); WeakThis->Shot(TEXT("Grapple_1_fire")); } }, 3.6f, false);
	// Watch the flight: note the launch, press A at the apex, follow the glide down.
	FTimerHandle Watch;
	GetWorldTimerManager().SetTimer(Watch, [WeakThis, Probe, Log, Tap]()
	{
		ASoulsKnight* K = WeakThis.Get();
		if (!K)
		{
			return;
		}
		const float Now = K->GetWorld()->GetTimeSeconds();
		const float Z = K->GetActorLocation().Z;
		if (!Probe->bLaunched && Now - K->GrappleLaunchTime < 0.2f)
		{
			Probe->bLaunched = true;
			Log(FString::Printf(TEXT("zipped up and launched PASS (%.0f cm above the start)"), Z - Probe->StartZ));
			K->Shot(TEXT("Grapple_2_launch"));
		}
		if (Probe->bLaunched && !Probe->bGlided)
		{
			Probe->PeakZ = FMath::Max(Probe->PeakZ, Z);
			if (K->GetVelocity().Z < 0.f)
			{
				Tap(K, EKeys::Gamepad_FaceButton_Bottom, 0.1f);
				Probe->bGlided = true;
				Probe->GlideStart = Now;
				Log(FString::Printf(TEXT("apex %.0f cm above the start; A to glide"), Probe->PeakZ - Probe->StartZ));
			}
		}
		if (Probe->bGlided && K->bGliding && Now - Probe->GlideStart > 1.2f && Now - Probe->GlideStart < 1.3f)
		{
			Log(FString::Printf(TEXT("gliding PASS (sink %.0f cm/s, air %.0f cm/s)"), -K->GetVelocity().Z, K->GetVelocity().Size2D()));
			K->Shot(TEXT("Grapple_3_glide"));
		}
	}, 0.05f, true, 3.7f);
	FTimerHandle Done;
	GetWorldTimerManager().SetTimer(Done, [WeakThis, Probe, Log]()
	{
		if (ASoulsKnight* K = WeakThis.Get())
		{
			Log(FString::Printf(TEXT("done: launched=%d glided=%d gliding=%d grounded=%d %s"), Probe->bLaunched, Probe->bGlided, K->bGliding,
				K->GetCharacterMovement()->IsMovingOnGround(), Probe->bLaunched && Probe->bGlided ? TEXT("PASS") : TEXT("FAIL")));
			K->Shot(TEXT("Grapple_4_done"));
		}
	}, 14.f, false);
}

void ASoulsKnight::RunChainShots()
{
	GEngine->Exec(GetWorld(), TEXT("r.MotionBlurQuality 0"));
	GetCameraBoom()->bEnableCameraLag = false;
	GetCameraBoom()->bEnableCameraRotationLag = false;
	const TWeakObjectPtr<ASoulsKnight> WeakThis(this);
	FTimerHandle Aim, First, Second;
	GetWorldTimerManager().SetTimer(Aim, [WeakThis]()
	{
		if (ASoulsKnight* K = WeakThis.Get())
		{
			K->GetCameraBoom()->TargetArmLength = 380.f;
			K->GetCameraBoom()->SocketOffset = FVector(0.f, 0.f, 20.f);
			if (K->Controller)
			{
				K->Controller->SetControlRotation(FRotator(-5.f, K->GetActorRotation().Yaw - 90.f, 0.f));
			}
		}
	}, 8.f, false);
	GetWorldTimerManager().SetTimer(First, [WeakThis]()
	{
		if (WeakThis.IsValid())
		{
			// Slow motion for -ChainMeasure, so the blade can be sampled finely through the swing.
			if (FParse::Param(FCommandLine::Get(), TEXT("ChainMeasure")))
			{
				GEngine->Exec(WeakThis->GetWorld(), TEXT("slomo 0.2"));
			}
			WeakThis->AttackPressed();
		}
	}, 8.5f, false);
	GetWorldTimerManager().SetTimer(Second, [WeakThis]() { if (WeakThis.IsValid()) { WeakThis->AttackPressed(); } }, 8.75f, false);
	for (int32 Index = 0; Index < 24; ++Index)
	{
		FTimerHandle Snap;
		GetWorldTimerManager().SetTimer(Snap, [WeakThis, Index]()
		{
			if (ASoulsKnight* K = WeakThis.Get())
			{
				UE_LOG(LogUnrealSoulslike, Log, TEXT("Chain frame %d: action %d swing %d progress %.2f"), Index, static_cast<int32>(K->Action), K->SwingIndex, K->GetActionProgress());
				K->Shot(*FString::Printf(TEXT("Chain_%02d"), Index));
			}
		}, 8.5f + Index * 0.08f, false);
	}
}

void ASoulsKnight::Shot(const TCHAR* Name)
{
	FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("Knight_%s"), Name), true, false);
}

// ---------------------------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------------------------

void ASoulsKnight::EnsureKnightInput()
{
	if (KnightMappingContext)
	{
		return;
	}

	// One set of actions and one mapping context for every knight the game ever makes (UNR-T66): a
	// per-pawn context outlived its pawn after a death - still registered with the input subsystem, it
	// kept consuming every key for actions nobody listened to, so the respawned knight had no controls.
	static TMap<FName, UInputAction*> SharedActions;
	static UInputMappingContext* SharedContext = nullptr;
	auto MakeAction = [](const TCHAR* Name)
	{
		if (UInputAction** Found = SharedActions.Find(FName(Name)))
		{
			return *Found;
		}
		UInputAction* NewAction = NewObject<UInputAction>(GetTransientPackage(), Name);
		NewAction->ValueType = EInputActionValueType::Boolean;
		NewAction->AddToRoot();
		SharedActions.Add(FName(Name), NewAction);
		return NewAction;
	};
	DodgeAction = MakeAction(TEXT("IA_KnightDodge"));
	BlockAction = MakeAction(TEXT("IA_KnightBlock"));
	HealAction = MakeAction(TEXT("IA_KnightHeal"));
	ParryAction = MakeAction(TEXT("IA_KnightParry"));
	LockOnAction = MakeAction(TEXT("IA_KnightLockOn"));
	InteractAction = MakeAction(TEXT("IA_KnightInteract"));
	GrappleAction = MakeAction(TEXT("IA_KnightGrapple"));
	SprintAction = MakeAction(TEXT("IA_KnightSprint"));
	KnightJumpAction = MakeAction(TEXT("IA_KnightJump"));
	AttackAction = MakeAction(TEXT("IA_KnightAttack"));
	ShadowAction = MakeAction(TEXT("IA_KnightShadow"));
	CrouchAction = MakeAction(TEXT("IA_KnightCrouch"));

	if (SharedContext)
	{
		KnightMappingContext = SharedContext;
		return;
	}
	KnightMappingContext = NewObject<UInputMappingContext>(GetTransientPackage(), TEXT("IMC_Knight"));
	KnightMappingContext->AddToRoot();
	SharedContext = KnightMappingContext;
	// The combat template's map has no jump at all, so the knight carries its own.
	KnightMappingContext->MapKey(KnightJumpAction, EKeys::Gamepad_FaceButton_Bottom);
	KnightMappingContext->MapKey(KnightJumpAction, EKeys::SpaceBar);
	KnightMappingContext->MapKey(SprintAction, EKeys::Gamepad_LeftThumbstick);
	KnightMappingContext->MapKey(SprintAction, EKeys::LeftShift);
	KnightMappingContext->MapKey(DodgeAction, EKeys::Gamepad_FaceButton_Right);
	KnightMappingContext->MapKey(DodgeAction, EKeys::E);
	KnightMappingContext->MapKey(BlockAction, EKeys::Gamepad_LeftShoulder);
	KnightMappingContext->MapKey(BlockAction, EKeys::RightMouseButton);
	KnightMappingContext->MapKey(HealAction, EKeys::Gamepad_RightShoulder);
	KnightMappingContext->MapKey(HealAction, EKeys::R);
	KnightMappingContext->MapKey(CrouchAction, EKeys::Gamepad_RightThumbstick);
	KnightMappingContext->MapKey(CrouchAction, EKeys::LeftControl);
	KnightMappingContext->MapKey(CrouchAction, EKeys::C);
	KnightMappingContext->MapKey(AttackAction, EKeys::Gamepad_FaceButton_Left);
	KnightMappingContext->MapKey(AttackAction, EKeys::LeftMouseButton);
	KnightMappingContext->MapKey(InteractAction, EKeys::Gamepad_DPad_Up);
	KnightMappingContext->MapKey(InteractAction, EKeys::F);
	// The shadow melt (UNR-T71).
	KnightMappingContext->MapKey(ShadowAction, EKeys::Gamepad_LeftTrigger);
	KnightMappingContext->MapKey(ShadowAction, EKeys::Q);
	// The grappling gun (UNR-T70).
	KnightMappingContext->MapKey(GrappleAction, EKeys::Gamepad_RightTrigger);
	KnightMappingContext->MapKey(GrappleAction, EKeys::G);
}

void ASoulsKnight::OnBecomeActive()
{
	SetActorHiddenInGame(false);
	SetActorEnableCollision(true);
	GetCharacterMovement()->SetMovementMode(MOVE_Walking);
}

void ASoulsKnight::OnBecomeInactive()
{
	GetCharacterMovement()->StopMovementImmediately();
	GetCharacterMovement()->DisableMovement();
	SetActorEnableCollision(false);
	SetActorHiddenInGame(true);
}

void ASoulsKnight::NotifyControllerChanged()
{
	Super::NotifyControllerChanged();
	if (const APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		EnsureKnightInput();
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
		{
			Subsystem->AddMappingContext(KnightMappingContext, 1);
		}
	}
}

void ASoulsKnight::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	EnsureKnightInput();
	if (UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		Input->BindAction(DodgeAction, ETriggerEvent::Started, this, &ASoulsKnight::DodgePressed);
		Input->BindAction(DodgeAction, ETriggerEvent::Completed, this, &ASoulsKnight::DodgeReleased);
		Input->BindAction(SprintAction, ETriggerEvent::Started, this, &ASoulsKnight::SprintPressed);
		Input->BindAction(KnightJumpAction, ETriggerEvent::Started, this, &ASoulsKnight::JumpPressed);
		Input->BindAction(KnightJumpAction, ETriggerEvent::Completed, this, &ASoulsKnight::JumpReleased);
		Input->BindAction(BlockAction, ETriggerEvent::Started, this, &ASoulsKnight::BlockPressed);
		Input->BindAction(BlockAction, ETriggerEvent::Completed, this, &ASoulsKnight::BlockReleased);
		Input->BindAction(HealAction, ETriggerEvent::Started, this, &ASoulsKnight::HealPressed);
		Input->BindAction(ParryAction, ETriggerEvent::Started, this, &ASoulsKnight::ParryPressed);
		Input->BindAction(LockOnAction, ETriggerEvent::Started, this, &ASoulsKnight::ToggleLockOn);
		Input->BindAction(InteractAction, ETriggerEvent::Started, this, &ASoulsKnight::InteractPressed);
		Input->BindAction(GrappleAction, ETriggerEvent::Started, this, &ASoulsKnight::GrapplePressed);
		Input->BindAction(AttackAction, ETriggerEvent::Started, this, &ASoulsKnight::AttackPressed);
		Input->BindAction(ShadowAction, ETriggerEvent::Started, this, &ASoulsKnight::ShadowPressed);
		Input->BindAction(CrouchAction, ETriggerEvent::Started, this, &ASoulsKnight::CrouchToggle);
	}
}

// ---------------------------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------------------------

bool ASoulsKnight::SpendStamina(float Cost)
{
	// No stamina (UNR-T59): every move is always available.
	return true;
}

FVector ASoulsKnight::DesiredDirection() const
{
	const FVector Input = GetLastMovementInputVector().GetSafeNormal2D();
	return Input.IsNearlyZero() ? GetActorForwardVector().GetSafeNormal2D() : Input;
}

FVector ASoulsKnight::AttackDirection() const
{
	if (const ASoulsKnight* Target = LockTarget.Get())
	{
		return (Target->GetActorLocation() - GetActorLocation()).GetSafeNormal2D();
	}
	if (const AActor* Target = TrackTarget.Get())
	{
		return (Target->GetActorLocation() - GetActorLocation()).GetSafeNormal2D();
	}
	return DesiredDirection();
}

void ASoulsKnight::FaceTowards(const FVector& Location)
{
	const FVector Dir = (Location - GetActorLocation()).GetSafeNormal2D();
	if (!Dir.IsNearlyZero())
	{
		SetActorRotation(FRotator(0.f, Dir.Rotation().Yaw, 0.f));
	}
}

void ASoulsKnight::PlaySound(USoundBase* Sound, float Volume) const
{
	if (Sound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, Sound, GetActorLocation(), Volume, FMath::FRandRange(0.94f, 1.06f));
	}
}

void ASoulsKnight::PlayRandomSound(const TArray<TObjectPtr<USoundBase>>& Sounds, float Volume) const
{
	if (!Sounds.IsEmpty())
	{
		PlaySound(Sounds[FMath::RandRange(0, Sounds.Num() - 1)], Volume);
	}
}

void ASoulsKnight::KickCamera(float Strength)
{
	if (IsPlayerControlled())
	{
		CameraKick = FMath::Max(CameraKick, Strength);
	}
}

void ASoulsKnight::Hitstop(float Seconds)
{
	CustomTimeDilation = 0.02f;
	const TWeakObjectPtr<ASoulsKnight> WeakThis(this);
	GetWorldTimerManager().SetTimer(HitstopTimer, [WeakThis]()
	{
		if (WeakThis.IsValid())
		{
			WeakThis->CustomTimeDilation = 1.f;
		}
	}, Seconds, false);
}

// ---------------------------------------------------------------------------------------------
// Movement
// ---------------------------------------------------------------------------------------------

void ASoulsKnight::DoMove(float Right, float Forward)
{
	// Melted: the stick crawls the shadow over the ground and up the walls.
	if (ShadowMelt && ShadowMelt->IsMerged())
	{
		ShadowMelt->AddMergeInput(FVector2D(Right, Forward));
		return;
	}
	if (IsDead() || IsBusy())
	{
		return;
	}
	Super::DoMove(Right, Forward);
}

void ASoulsKnight::DoLook(float Yaw, float Pitch)
{
	// Locked on, the camera follows the target; the stick is free again once unlocked.
	if (!LockTarget.IsValid())
	{
		Super::DoLook(Yaw, Pitch);
	}
}

bool ASoulsKnight::CanJumpInternal_Implementation() const
{
	return !IsBusy() && !bBlocking && Super::CanJumpInternal_Implementation();
}

// ---------------------------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------------------------

void ASoulsKnight::PlayAction(EKnightAction NewAction, UAnimMontage* Montage, float Duration, float StartFraction)
{
	GetWorldTimerManager().ClearTimer(ActionTimer);
	GetWorldTimerManager().ClearTimer(HitTimer);
	GetWorldTimerManager().ClearTimer(WhooshTimer);
	// Any real action brings a crouching or prone knight back to its feet.
	if (NewAction != EKnightAction::StanceShift && Stance != EKnightStance::Standing)
	{
		SetStance(EKnightStance::Standing);
	}
	if (bTalking)
	{
		SetTalking(false);
	}
	Action = NewAction;
	ActionElapsed = 0.f;
	ActionDuration = Duration;
	bComboQueued = false;
	bSprinting = false;

	if (UAnimInstance* Anim = GetMesh()->GetAnimInstance())
	{
		if (Montage)
		{
			const float Length = Montage->GetPlayLength();
			const float Rate = Duration > 0.f ? Length * (1.f - StartFraction) / Duration : 1.f;
			Anim->Montage_Play(Montage, Rate, EMontagePlayReturnType::MontageLength, Length * StartFraction);
		}
	}
	GetWorldTimerManager().SetTimer(ActionTimer, this, &ASoulsKnight::EndAction, FMath::Max(Duration, 0.05f), false);
}

void ASoulsKnight::EndAction()
{
	Action = EKnightAction::None;
	bComboQueued = false;
	CriticalVictim = nullptr;
	if (bBlockHeld)
	{
		SetBlocking(true);
	}
}

void ASoulsKnight::Interrupt()
{
	if (GrappleMode != EGrappleMode::None)
	{
		EndGrapple(false);
	}
	GetWorldTimerManager().ClearTimer(PounceTimer);
	bPounceAirborne = false;
	GetWorldTimerManager().ClearTimer(HealTimer);
	GetWorldTimerManager().ClearTimer(HitTimer);
	GetWorldTimerManager().ClearTimer(WhooshTimer);
	GetWorldTimerManager().ClearTimer(WindupTimer);
	GetWorldTimerManager().ClearTimer(StrikeTimer);
	GetWorldTimerManager().ClearTimer(RecoilTimer);
	GetWorldTimerManager().ClearTimer(BladeTimer);
	BladeLiveUntil = -1.f;
	LungeRemaining = 0.f;
	SetBlocking(false);
	if (UAnimInstance* Anim = GetMesh()->GetAnimInstance())
	{
		Anim->Montage_Stop(0.1f);
	}
}

void ASoulsKnight::DoComboAttackStart()
{
	if (IsDead() || IsSwimming() || SwordComboMontages.IsEmpty() || GetUpFirst())
	{
		return;
	}
	if (Action == EKnightAction::LightAttack)
	{
		bComboQueued = true;
		return;
	}
	if (IsBusy() || GetCharacterMovement()->IsFalling())
	{
		return;
	}
	if (TryCritical())
	{
		ASoulsTorch::DropWielded(this); // the riposte/backstab draws the sword (UNR-T40)
		return;
	}
	if (!SpendStamina(LightAttackCost))
	{
		return;
	}
	SetBlocking(false);
	StartSwing(0);
}

void ASoulsKnight::StartSwing(int32 Index)
{
	UAnimMontage* Montage = SwordComboMontages[Index];
	SwingIndex = Index;
	bHeavySwing = false;
	HitThisSwing.Reset();
	BladeNear.Reset();
	bBladeTracked = false;
	SetActorRotation(FRotator(0.f, AttackDirection().Rotation().Yaw, 0.f));
	const float Duration = Montage ? Montage->GetPlayLength() / AttackSpeed : 0.6f;
	PlayAction(EKnightAction::LightAttack, Montage, Duration);
	float HitTime = Duration * SwingHitFraction;
	if (IsPlayerControlled() || !Montage)
	{
		StartLunge(170.f, 0.f, HitTime * 0.9f);
	}
	else
	{
		// The foe reads as a foe: the sword comes up slowly, then falls fast, stepping into reach.
		HitTime = ApplyWindup(Montage, AttackSpeed, SwingHitFraction, false);
		StartLunge(110.f, HitTime * 0.45f, HitTime * 0.5f);
	}
	GetWorldTimerManager().SetTimer(HitTimer, this, &ASoulsKnight::SwingHit, HitTime, false);
	// The blade cuts through the middle of the swing (UNR-T65): from about a fifth of the way in for
	// the player (the downstroke after an enemy's slow raise) to just past the strike moment.
	OpenBlade(IsPlayerControlled() ? Duration * 0.2f : HitTime * 0.62f, HitTime + 0.06f);
	const TWeakObjectPtr<ASoulsKnight> WeakThis(this);
	GetWorldTimerManager().SetTimer(WhooshTimer, [WeakThis]()
	{
		if (WeakThis.IsValid())
		{
			WeakThis->PlayRandomSound(WeakThis->SwingSounds, 0.7f);
		}
	}, FMath::Max(HitTime - 0.12f, 0.01f), false);
	NotifyEnemiesOfIncomingAttack();
}

void ASoulsKnight::DoChargedAttackStart()
{
	if (IsDead() || IsSwimming() || IsBusy() || !HeavyAttackMontage || GetCharacterMovement()->IsFalling() || GetUpFirst())
	{
		return;
	}
	// UNR-T40: a torch in the sword hand is thrown instead (a riposte/backstab still draws the sword).
	if (ASoulsTorch* Torch = ASoulsTorch::GetWielded(this))
	{
		if (TryCritical())
		{
			Torch->Drop();
			return;
		}
		if (!SpendStamina(HeavyAttackCost * 0.5f))
		{
			return;
		}
		SetBlocking(false);
		SetActorRotation(FRotator(0.f, AttackDirection().Rotation().Yaw, 0.f));
		const float Duration = ThrowMontage ? ThrowMontage->GetPlayLength() / AttackSpeed : 0.8f;
		PlayAction(EKnightAction::HeavyAttack, ThrowMontage, Duration);
		const TWeakObjectPtr<ASoulsTorch> WeakTorch(Torch);
		const TWeakObjectPtr<ASoulsKnight> WeakTarget(LockTarget);
		GetWorldTimerManager().SetTimer(HitTimer, [WeakTorch, WeakTarget]()
		{
			if (WeakTorch.IsValid())
			{
				WeakTorch->Throw(WeakTarget.Get());
			}
		}, Duration * 0.42f, false); // HitTimer: a stagger or another action cancels the throw
		return;
	}
	if (TryCritical() || !SpendStamina(HeavyAttackCost))
	{
		return;
	}
	StartHeavy();
}

void ASoulsKnight::StartHeavy()
{
	SetBlocking(false);
	bHeavySwing = true;
	HitThisSwing.Reset();
	BladeNear.Reset();
	bBladeTracked = false;
	SetActorRotation(FRotator(0.f, AttackDirection().Rotation().Yaw, 0.f));
	// The player's one slash is quick (about half again as fast as an enemy's heavy).
	const float Duration = HeavyAttackMontage->GetPlayLength() / (AttackSpeed * (IsPlayerControlled() ? KPlayerSlashSpeed : 0.9f));
	PlayAction(EKnightAction::HeavyAttack, HeavyAttackMontage, Duration);
	float HitTime = Duration * 0.5f;
	if (IsPlayerControlled())
	{
		StartLunge(220.f, 0.f, HitTime * 0.9f);
	}
	else
	{
		HitTime = ApplyWindup(HeavyAttackMontage, AttackSpeed * 0.9f, 0.5f, true);
		StartLunge(140.f, HitTime * 0.5f, HitTime * 0.45f);
	}
	GetWorldTimerManager().SetTimer(HitTimer, this, &ASoulsKnight::SwingHit, HitTime, false);
	OpenBlade(IsPlayerControlled() ? Duration * 0.2f : HitTime * 0.62f, HitTime + 0.06f);
	const TWeakObjectPtr<ASoulsKnight> WeakThis(this);
	GetWorldTimerManager().SetTimer(WhooshTimer, [WeakThis]()
	{
		if (WeakThis.IsValid())
		{
			WeakThis->PlayRandomSound(WeakThis->SwingSounds, 0.9f);
		}
	}, FMath::Max(HitTime - 0.12f, 0.01f), false);
	NotifyEnemiesOfIncomingAttack();
}

float ASoulsKnight::ApplyWindup(UAnimMontage* Montage, float BaseRate, float HitFraction, bool bHeavy)
{
	UAnimInstance* Anim = GetMesh()->GetAnimInstance();
	const float Length = Montage->GetPlayLength();
	const float PreHit = Length * HitFraction;
	const float Raise = PreHit * 0.6f;
	// Shaken foes dither even longer over the raise.
	const float SlowRate = BaseRate * (bHeavy ? 0.38f : 0.5f) * (IsShaken() ? 0.8f : 1.f);
	const float FastRate = BaseRate * (bHeavy ? 1.35f : 1.25f);
	const float RaiseTime = Raise / SlowRate;
	const float StrikeTime = (PreHit - Raise) / FastRate;
	const float HitTime = RaiseTime + StrikeTime;
	const float Total = HitTime + (Length - PreHit) / BaseRate;
	if (Anim)
	{
		Anim->Montage_SetPlayRate(Montage, SlowRate);
	}
	const TWeakObjectPtr<ASoulsKnight> WeakThis(this);
	const TWeakObjectPtr<UAnimMontage> WeakMontage(Montage);
	auto SetRate = [WeakThis, WeakMontage](float Rate)
	{
		if (WeakThis.IsValid() && WeakMontage.IsValid())
		{
			if (UAnimInstance* A = WeakThis->GetMesh()->GetAnimInstance())
			{
				A->Montage_SetPlayRate(WeakMontage.Get(), Rate);
			}
		}
	};
	GetWorldTimerManager().SetTimer(WindupTimer, [SetRate, FastRate]() { SetRate(FastRate); }, RaiseTime, false);
	GetWorldTimerManager().SetTimer(StrikeTimer, [SetRate, BaseRate]() { SetRate(BaseRate); }, FMath::Max(HitTime - 0.01f, 0.01f), false);
	ActionDuration = Total;
	GetWorldTimerManager().SetTimer(ActionTimer, this, &ASoulsKnight::EndAction, Total, false);
	// A heavy blow announces itself: a glint runs up the blade as it rises.
	if (bHeavy && Sword)
	{
		FTimerHandle Glint;
		GetWorldTimerManager().SetTimer(Glint, [WeakThis]()
		{
			if (WeakThis.IsValid() && WeakThis->Sword)
			{
				const UStaticMeshComponent* Blade = WeakThis->Sword;
				ASoulsSparks::Burst(WeakThis->GetWorld(), Blade->GetComponentLocation() + Blade->GetUpVector() * 70.f * WeakThis->GetActorScale3D().X,
					FVector::UpVector, 5, 0.6f, 1.f, FLinearColor(0.9f, 0.95f, 1.f));
			}
		}, RaiseTime * 0.6f, false);
	}
	return HitTime;
}

void ASoulsKnight::StartLunge(float MaxDistance, float Delay, float Window)
{
	LungeRemaining = 0.f;
	// Soft targeting: the locked foe (or, for the AI, its mark), else the nearest one ahead.
	const FVector Origin = GetActorLocation();
	const FVector Aim = GetActorForwardVector().GetSafeNormal2D();
	ASoulsKnight* Mark = IsPlayerControlled() ? LockTarget.Get() : Cast<ASoulsKnight>(TrackTarget.Get());
	if (!Mark)
	{
		float BestScore = TNumericLimits<float>::Max();
		for (TActorIterator<ASoulsKnight> It(GetWorld()); It; ++It)
		{
			if (It->Team == Team || It->IsDead() || *It == this)
			{
				continue;
			}
			const FVector To = (It->GetActorLocation() - Origin) * FVector(1.f, 1.f, 0.f);
			const float Distance = To.Size();
			const float Dot = FVector::DotProduct(Aim, To / FMath::Max(Distance, 1.f));
			if (Distance < 520.f && Dot > 0.45f && FMath::Abs(It->GetActorLocation().Z - Origin.Z) < 200.f)
			{
				const float Score = Distance * (2.f - Dot);
				if (Score < BestScore)
				{
					BestScore = Score;
					Mark = *It;
				}
			}
		}
	}
	float Distance = MaxDistance * 0.35f;
	LungeDirection = Aim;
	if (Mark)
	{
		const FVector To = (Mark->GetActorLocation() - Origin) * FVector(1.f, 1.f, 0.f);
		LungeDirection = To.GetSafeNormal();
		SetActorRotation(FRotator(0.f, LungeDirection.Rotation().Yaw, 0.f));
		// Close to a comfortable sword's length (about 1.3 m between the two bodies), no further.
		const float Gap = To.Size() - GetCapsuleComponent()->GetScaledCapsuleRadius() - Mark->GetCapsuleComponent()->GetScaledCapsuleRadius()
			- 55.f * GetActorScale3D().X;
		Distance = FMath::Clamp(Gap, 0.f, MaxDistance);
	}
	Window = FMath::Max(Window, 0.1f);
	LungeDelay = Delay;
	LungeRemaining = Window;
	LungeSpeed = Distance / Window;
}

void ASoulsKnight::GuardImpact(const FVector& ToAttacker, bool bHeavy)
{
	// Feet slide back under the blow.
	GetCharacterMovement()->AddImpulse(-ToAttacker * (bHeavy ? 560.f : 320.f), true);
	// The shield jolts: drop the held guard pose back a little and snap it up again.
	UAnimInstance* Anim = GetMesh()->GetAnimInstance();
	if (Anim && BlockMontage && Anim->Montage_IsActive(BlockMontage))
	{
		const float Length = BlockMontage->GetPlayLength();
		const float Rate = bHeavy ? 1.3f : 1.8f;
		Anim->Montage_SetPosition(BlockMontage, Length * (bHeavy ? 0.08f : 0.16f));
		Anim->Montage_Resume(BlockMontage);
		Anim->Montage_SetPlayRate(BlockMontage, Rate);
		const TWeakObjectPtr<ASoulsKnight> WeakThis(this);
		GetWorldTimerManager().SetTimer(BlockHoldTimer, [WeakThis]()
		{
			if (WeakThis.IsValid() && WeakThis->bBlocking)
			{
				if (UAnimInstance* HeldAnim = WeakThis->GetMesh()->GetAnimInstance())
				{
					HeldAnim->Montage_Pause(WeakThis->BlockMontage);
				}
			}
		}, Length * (bHeavy ? 0.27f : 0.19f) / Rate, false);
	}
	// Steel on steel at the shield's face.
	const FVector Face = Shield ? Shield->GetComponentLocation() + ToAttacker * 25.f : GetActorLocation() + ToAttacker * 50.f + FVector(0.f, 0.f, 40.f);
	ASoulsSparks::Burst(GetWorld(), Face, ToAttacker + FVector(0.f, 0.f, 0.3f), bHeavy ? 16 : 10, bHeavy ? 1.3f : 1.f);
}

void ASoulsKnight::RecoilFromGuard(bool bHeavy)
{
	const FVector Back = -GetActorForwardVector().GetSafeNormal2D();
	GetCharacterMovement()->AddImpulse(Back * (bHeavy ? 140.f : 260.f), true);
	LungeRemaining = 0.f;
	if (bHeavy)
	{
		// A heavy blow drives on through the guard.
		return;
	}
	// The blade bounces off the shield: the swing runs backwards a beat, then carries on, late.
	UAnimInstance* Anim = GetMesh()->GetAnimInstance();
	UAnimMontage* Montage = Anim ? Anim->GetCurrentActiveMontage() : nullptr;
	if (!Montage)
	{
		return;
	}
	Anim->Montage_SetPlayRate(Montage, -0.9f);
	const TWeakObjectPtr<ASoulsKnight> WeakThis(this);
	const TWeakObjectPtr<UAnimMontage> WeakMontage(Montage);
	const float Rate = AttackSpeed;
	GetWorldTimerManager().SetTimer(RecoilTimer, [WeakThis, WeakMontage, Rate]()
	{
		if (WeakThis.IsValid() && WeakMontage.IsValid())
		{
			if (UAnimInstance* A = WeakThis->GetMesh()->GetAnimInstance())
			{
				A->Montage_SetPlayRate(WeakMontage.Get(), Rate);
			}
		}
	}, 0.16f, false);
	// The bounce costs time: hold the action open a little longer.
	const float Left = GetWorldTimerManager().GetTimerRemaining(ActionTimer);
	if (Left > 0.f)
	{
		ActionDuration += 0.3f;
		GetWorldTimerManager().SetTimer(ActionTimer, this, &ASoulsKnight::EndAction, Left + 0.3f, false);
	}
}

void ASoulsKnight::SwingHit()
{
	if (ASoulsTorch::HandleSwingHit(this, bHeavySwing))
	{
		return; // swinging a torch (UNR-T40)
	}
	// The strike moment itself: the blade window (OpenBlade) does the cutting, mid-swing.
}

void ASoulsKnight::OpenBlade(float From, float Until)
{
	const float Now = GetWorld()->GetTimeSeconds();
	BladeLiveFrom = Now + FMath::Max(From, 0.f);
	BladeLiveUntil = Now + FMath::Max(Until, From + 0.05f);
	bBladeTracked = false;
	GetWorldTimerManager().SetTimer(BladeTimer, this, &ASoulsKnight::CloseBlade, BladeLiveUntil - Now, false);
}

void ASoulsKnight::CloseBlade()
{
	// Last look at the blade, then anything it brushed past within a hand's breadth is struck at the
	// blade's nearest point - still inside the swing, before it finishes.
	SweepBlade();
	if (bHeavySwing)
	{
		MeleeSweep(HeavyDamage, 34.f, 32.f, true);
	}
	else
	{
		MeleeSweep(LightDamage, 14.f, 12.f, false);
	}
	BladeNear.Reset();
	bBladeTracked = false;
	BladeLiveUntil = -1.f;
}

bool ASoulsKnight::GetBladeSegment(FVector& OutBase, FVector& OutTip) const
{
	if (!Sword || !Sword->GetStaticMesh() || !Sword->IsVisible())
	{
		return false;
	}
	// Blade along the mesh's +Z: from just above the guard to the tip.
	const FBox Box = Sword->GetStaticMesh()->GetBoundingBox();
	const FTransform& Xf = Sword->GetComponentTransform();
	const float Length = Box.Max.Z - Box.Min.Z;
	OutBase = Xf.TransformPosition(FVector(0.f, 0.f, Box.Min.Z + 0.22f * Length));
	OutTip = Xf.TransformPosition(FVector(0.f, 0.f, Box.Max.Z));
	return true;
}

void ASoulsKnight::SweepBlade()
{
	FVector Base, Tip;
	if (!GetBladeSegment(Base, Tip))
	{
		return;
	}
	if (!bBladeTracked)
	{
		BladePrevBase = Base;
		BladePrevTip = Tip;
		bBladeTracked = true;
	}
	const float Damage = bHeavySwing ? HeavyDamage : LightDamage;
	const float PoiseHit = bHeavySwing ? 34.f : 14.f;
	const float StanceHit = bHeavySwing ? 32.f : 12.f;
	const float Radius = 7.f * GetActorScale3D().X;

	// Sweep a few points along the blade from where they were last frame to where they are now,
	// against bodies (each limb's collision shape, or the capsule where a mesh has none).
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_Pawn);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(KnightBlade), false, this);
	Params.bReturnPhysicalMaterial = false;
	for (int32 Sample = 0; Sample <= 4; ++Sample)
	{
		const float T = Sample / 4.f;
		const FVector From = FMath::Lerp(BladePrevBase, BladePrevTip, T);
		const FVector To = FMath::Lerp(Base, Tip, T);
		TArray<FHitResult> Hits;
		GetWorld()->SweepMultiByObjectType(Hits, From, To + (To - From).GetSafeNormal() * 0.1f, FQuat::Identity, Objects,
			FCollisionShape::MakeSphere(Radius), Params);
		for (const FHitResult& Hit : Hits)
		{
			ASoulsKnight* Victim = Cast<ASoulsKnight>(Hit.GetActor());
			if (!Victim || Victim->Team == Team || Victim->IsDead() || HitThisSwing.Contains(Victim))
			{
				continue;
			}
			HitThisSwing.Add(Victim);
			const FVector Impact = Hit.bStartPenetrating ? FMath::ClosestPointOnSegment(Victim->GetActorLocation(), Base, Tip) : FVector(Hit.ImpactPoint);
			ResolveStrike(Victim, Impact, Damage, PoiseHit, StanceHit, bHeavySwing);
		}
	}

	// Track how close the blade came to every foe nearby, for the near-miss pass.
	for (TActorIterator<ASoulsKnight> It(GetWorld()); It; ++It)
	{
		ASoulsKnight* Other = *It;
		if (Other == this || Other->Team == Team || Other->IsDead() || HitThisSwing.Contains(Other)
			|| FVector::DistSquared(Other->GetActorLocation(), GetActorLocation()) > FMath::Square(600.f * GetActorScale3D().X))
		{
			continue;
		}
		float CapRadius = 0.f, CapHalf = 0.f;
		Other->GetCapsuleComponent()->GetScaledCapsuleSize(CapRadius, CapHalf);
		const FVector Up = FVector(0.f, 0.f, CapHalf - CapRadius);
		FVector OnBlade, OnBody;
		FMath::SegmentDistToSegment(Base, Tip, Other->GetActorLocation() - Up, Other->GetActorLocation() + Up, OnBlade, OnBody);
		const float Gap = FVector::Dist(OnBlade, OnBody) - CapRadius;
		TPair<float, FVector>* Near = BladeNear.Find(Other);
		if (!Near || Gap < Near->Key)
		{
			BladeNear.Add(Other, TPair<float, FVector>(Gap, OnBlade));
		}
	}
	BladePrevBase = Base;
	BladePrevTip = Tip;
}

void ASoulsKnight::ResolveStrike(ASoulsKnight* Victim, const FVector& Impact, float Damage, float PoiseDamage, float StanceHit, bool bHeavy)
{
	const EMeleeResult Result = Victim->ReceiveMelee(this, Damage, PoiseDamage, StanceHit, bHeavy, Impact);
	UE_LOG(LogUnrealSoulslike, Verbose, TEXT("Blade: %s struck %s -> result %d"), *GetName(), *Victim->GetName(), static_cast<int32>(Result));
	if (Result == EMeleeResult::Hit)
	{
		Hitstop(bHeavy ? 0.11f : 0.065f);
		KickCamera(bHeavy ? 0.6f : 0.3f);
	}
	else if (Result == EMeleeResult::Blocked)
	{
		Hitstop(bHeavy ? 0.09f : 0.08f);
		KickCamera(0.35f);
		RecoilFromGuard(bHeavy);
	}
}

void ASoulsKnight::ShadowPressed()
{
	// Melt into the shadow underfoot, or rise out of it.
	if (!ShadowMelt || IsDead() || (!ShadowMelt->IsMerged() && (IsBusy() || IsSwimming() || GetCharacterMovement()->IsFalling())))
	{
		return;
	}
	if (bGliding)
	{
		EndGlide();
	}
	SetStance(EKnightStance::Standing);
	bSprintToggled = false;
	ShadowMelt->ToggleMerge();
	UE_LOG(LogUnrealSoulslike, Log, TEXT("Shadow: %s"), ShadowMelt->IsMerged() ? TEXT("melted") : TEXT("risen"));
}

void ASoulsKnight::AttackPressed()
{
	if (ShadowMelt && ShadowMelt->IsMerged())
	{
		ShadowMelt->ToggleMerge(); // rise out of the shadow to strike
		return;
	}
	if (ASoulsTorch::GetWielded(this))
	{
		DoComboAttackStart(); // the torch swing (SwingHit hands it to the torch)
		return;
	}
	// X again mid-slash: queue the cross-slash back from where this one ends.
	if (Action == EKnightAction::HeavyAttack && bHeavySwing)
	{
		bComboQueued = true;
		return;
	}
	if (Action == EKnightAction::LightAttack)
	{
		return; // the back-slash ends the string
	}
	DoChargedAttackStart();
}

void ASoulsKnight::CrouchToggle()
{
	if (IsDead() || IsSwimming() || GetUpFirst())
	{
		return;
	}
	if (IsBusy() || GetCharacterMovement()->IsFalling())
	{
		return;
	}
	SetStance(Stance == EKnightStance::Crouched ? EKnightStance::Standing : EKnightStance::Crouched);
	UE_LOG(LogTemp, Log, TEXT("Stance: %s"), Stance == EKnightStance::Crouched ? TEXT("crouched") : TEXT("standing"));
}

void ASoulsKnight::DodgePressed()
{
	if (bGliding)
	{
		EndGlide(); // drop out of the glide
		return;
	}
	// B: the roll, on press (Elden Ring). Prone or crouched, it gets you up instead.
	if (IsDead() || GetUpFirst())
	{
		return;
	}
	if (Stance != EKnightStance::Standing)
	{
		SetStance(EKnightStance::Standing);
	}
	const FVector Stick = GetCharacterMovement()->GetCurrentAcceleration().GetSafeNormal2D();
	bSprintToggled = false;
	// No stick: a backstep away from what you face.
	// No stick: roll straight on along the way you face, and still face it after.
	DodgeToward(!Stick.IsNearlyZero() ? Stick : GetActorForwardVector());
}

void ASoulsKnight::DodgeReleased()
{
}

void ASoulsKnight::SprintPressed()
{
	// Tap L3 to break into a sprint; it lasts until you stop, guard, act or run out of stamina.
	if (GetUpFirst())
	{
		return;
	}
	SetStance(EKnightStance::Standing);
	bSprintToggled = !bSprintToggled;
	SprintPressTime = GetWorld()->GetTimeSeconds();
	UE_LOG(LogTemp, Log, TEXT("Sprint: %s"), bSprintToggled ? TEXT("on") : TEXT("off"));
}

void ASoulsKnight::JumpPressed()
{
	// On a wall (UNR-T77), A kicks off it; that comes before the glide.
	if (WallRun && WallRun->TryWallJump())
	{
		return;
	}
	if (bGliding)
	{
		EndGlide();
		return;
	}
	if (GetCharacterMovement()->IsFalling() && bGrappleLaunched && !IsSwimming() && !IsDead())
	{
		StartGlide();
		return;
	}
	if (IsDead() || IsSwimming() || GetUpFirst())
	{
		return;
	}
	SetStance(EKnightStance::Standing);
	Jump();
}

void ASoulsKnight::JumpReleased()
{
	StopJumping();
}

void ASoulsKnight::DodgeToward(const FVector& Direction)
{
	if (IsDead() || IsSwimming() || GetCharacterMovement()->IsFalling() || !RollMontage)
	{
		return;
	}
	// A swing less than half done (the blade not yet down) can be rolled out of, and so can its
	// recovery; the moment of the strike itself can't.
	const bool bEarlySwing = IsAttacking() && ActionElapsed < ActionDuration * 0.5f;
	const bool bCancelable = Action == EKnightAction::None || bEarlySwing
		|| (IsAttacking() && ActionElapsed > ActionDuration * 0.65f);
	if (!bCancelable || !SpendStamina(RollCost))
	{
		return;
	}
	if (bEarlySwing)
	{
		// Called off: the blow never lands.
		GetWorldTimerManager().ClearTimer(HitTimer);
		GetWorldTimerManager().ClearTimer(WhooshTimer);
		GetWorldTimerManager().ClearTimer(WindupTimer);
		GetWorldTimerManager().ClearTimer(StrikeTimer);
		GetWorldTimerManager().ClearTimer(BladeTimer);
		BladeLiveUntil = -1.f;
		LungeRemaining = 0.f;
		LungeDelay = 0.f;
	}
	SetBlocking(false);
	RollDirection = Direction.GetSafeNormal2D().IsNearlyZero() ? GetActorForwardVector() : Direction.GetSafeNormal2D();
	SetActorRotation(FRotator(0.f, RollDirection.Rotation().Yaw, 0.f));
	PlayAction(EKnightAction::Roll, RollMontage, RollDuration);
	PlaySound(RollSound, 0.6f);
}

void ASoulsKnight::BlockPressed()
{
	bBlockHeld = true;
	if (GetUpFirst())
	{
		return;
	}
	SetStance(EKnightStance::Standing);
	// Something leaping at you: the guard button sets the blade instead.
	if (IsPlayerControlled() && !IsDead() && !IsSwimming() && !IsBusy() && FindIncomingPouncer())
	{
		StartBrace();
		return;
	}
	if (!IsBusy() && !IsSwimming())
	{
		BlockPressTime = GetWorld()->GetTimeSeconds();
		SetBlocking(true);
	}
}

void ASoulsKnight::BlockReleased()
{
	bBlockHeld = false;
	SetBlocking(false);
}

void ASoulsKnight::SetBlocking(bool bNewBlocking)
{
	if (bBlocking == bNewBlocking || (bNewBlocking && IsDead()))
	{
		return;
	}
	bBlocking = bNewBlocking;
	GetCharacterMovement()->MaxWalkSpeed = bBlocking ? KBlockWalkSpeed : RunSpeed;
	UAnimInstance* Anim = GetMesh()->GetAnimInstance();
	if (!Anim || !BlockMontage)
	{
		return;
	}
	GetWorldTimerManager().ClearTimer(BlockHoldTimer);
	if (bBlocking)
	{
		Anim->Montage_Play(BlockMontage, 1.6f);
		// Freeze on the raised-guard frame for as long as block is held.
		const TWeakObjectPtr<ASoulsKnight> WeakThis(this);
		GetWorldTimerManager().SetTimer(BlockHoldTimer, [WeakThis]()
		{
			if (WeakThis.IsValid() && WeakThis->bBlocking)
			{
				if (UAnimInstance* HeldAnim = WeakThis->GetMesh()->GetAnimInstance())
				{
					HeldAnim->Montage_Pause(WeakThis->BlockMontage);
				}
			}
		}, BlockMontage->GetPlayLength() * 0.35f / 1.6f, false);
	}
	else
	{
		Anim->Montage_Stop(0.2f, BlockMontage);
	}
}

void ASoulsKnight::ParryPressed()
{
	if (GetUpFirst())
	{
		return;
	}
	// Something leaping at you: set the blade instead of the shield.
	if (!IsDead() && !IsSwimming() && (!IsBusy() || Action == EKnightAction::Parry) && FindIncomingPouncer())
	{
		StartBrace();
		return;
	}
	if (IsDead() || IsSwimming() || IsBusy() || GetCharacterMovement()->IsFalling() || !ParryMontage || !SpendStamina(KParryCost))
	{
		return;
	}
	SetBlocking(false);
	if (LockTarget.IsValid())
	{
		FaceTowards(LockTarget->GetActorLocation());
	}
	PlayAction(EKnightAction::Parry, ParryMontage, 0.8f);
}

bool ASoulsKnight::IsInParryWindow() const
{
	return Action == EKnightAction::Parry && ActionElapsed >= ParryWindowStart && ActionElapsed <= ParryWindowEnd;
}

void ASoulsKnight::HealPressed()
{
	if (IsDead() || IsSwimming() || IsBusy() || Flasks <= 0 || CurrentHP >= MaxHP || !HealMontage || GetUpFirst())
	{
		return;
	}
	ASoulsTorch::DropWielded(this); // UNR-T40
	--Flasks;
	SetBlocking(false);
	const float Duration = HealMontage->GetPlayLength();
	PlayAction(EKnightAction::Heal, HealMontage, Duration);
	GetWorldTimerManager().SetTimer(HealTimer, this, &ASoulsKnight::FinishHeal, Duration * 0.55f, false);
}

void ASoulsKnight::FinishHeal()
{
	if (Action == EKnightAction::Heal)
	{
		PlaySound(DrinkSound, 0.8f);
		CurrentHP = FMath::Min(MaxHP, CurrentHP + FlaskHealing);
		if (LifeBarWidget)
		{
			LifeBarWidget->SetLifePercentage(GetHealthFraction());
		}
	}
}

void ASoulsKnight::ApplyBodyMaterials(const TArray<TObjectPtr<UMaterialInterface>>& Materials)
{
	for (int32 i = 0; i < Materials.Num() && i < GetMesh()->GetNumMaterials(); ++i)
	{
		if (Materials[i])
		{
			GetMesh()->SetMaterial(i, Materials[i]);
		}
	}
}

void ASoulsKnight::SetNearbyGrace(ASiteOfGrace* Grace)
{
	NearbyGrace = Grace;
}

void ASoulsKnight::SetNearbyPickup(ASoulsPickup* Pickup)
{
	NearbyPickup = Pickup;
}

void ASoulsKnight::InteractPressed()
{
	if (ASoulsPickup* Pickup = NearbyPickup.Get())
	{
		if (!IsDead() && !IsBusy())
		{
			Pickup->Collect(this);
			return;
		}
	}
	if (!IsDead() && !IsBusy() && ASoulsTorch::TryPickUp(this))
	{
		return; // UNR-T40: take a torch off a wall or the ground
	}
	if (ASiteOfGrace* Grace = NearbyGrace.Get())
	{
		if (!IsDead() && !IsBusy())
		{
			Grace->Rest(this);
		}
		return;
	}
	if (!IsDead() && !IsBusy() && ASoulsMech::TryBoard(this))
	{
		return; // UNR-T68: climb into a parked mech in reach
	}
	if (!IsDead() && !IsBusy() && ASoulsFighter::TryBoard(this))
	{
		return; // UNR-T69: climb into the parked star fighter in reach
	}
	Whistle();
}

void ASoulsKnight::Whistle()
{
	const float Now = GetWorld()->GetTimeSeconds();
	if (IsDead() || IsSwimming() || Now - LastWhistleTime < 1.5f)
	{
		return;
	}
	LastWhistleTime = Now;
	// Two notes, up then down, synthesised on the spot.
	USoundWaveProcedural* Wave = NewObject<USoundWaveProcedural>(this);
	constexpr int32 KWhistleRate = 22050;
	Wave->SetSampleRate(KWhistleRate);
	Wave->NumChannels = 1;
	Wave->Duration = 0.9f;
	Wave->SoundGroup = SOUNDGROUP_Default;
	Wave->bLooping = false;
	TArray<int16> Samples;
	Samples.SetNumZeroed(static_cast<int32>(KWhistleRate * 0.9f));
	double Phase = 0.0;
	for (int32 Index = 0; Index < Samples.Num(); ++Index)
	{
		const float T = static_cast<float>(Index) / KWhistleRate;
		float Freq = 0.f;
		float Env = 0.f;
		if (T < 0.32f)
		{
			const float U = T / 0.32f;
			Freq = FMath::Lerp(1350.f, 2250.f, U * U);
			Env = FMath::Min(U * 8.f, 1.f) * FMath::Min((1.f - U) * 10.f, 1.f);
		}
		else if (T > 0.4f && T < 0.88f)
		{
			const float U = (T - 0.4f) / 0.48f;
			Freq = FMath::Lerp(2050.f, 1250.f, U);
			Env = FMath::Min(U * 8.f, 1.f) * FMath::Min((1.f - U) * 6.f, 1.f);
		}
		Freq *= 1.f + 0.012f * FMath::Sin(T * 2.f * PI * 6.f);
		Phase += 2.0 * PI * Freq / KWhistleRate;
		const float Breath = FMath::FRandRange(-0.06f, 0.06f);
		Samples[Index] = static_cast<int16>(FMath::Clamp((FMath::Sin(Phase) * 0.9f + Breath) * Env, -1.f, 1.f) * 12000.f);
	}
	Wave->QueueAudio(reinterpret_cast<const uint8*>(Samples.GetData()), Samples.Num() * sizeof(int16));
	UGameplayStatics::PlaySoundAtLocation(this, Wave, GetActorLocation() + FVector(0.f, 0.f, 60.f), 0.7f);

	int32 Heard = 0;
	for (TActorIterator<ASoulsEnemyController> It(GetWorld()); It; ++It)
	{
		const APawn* Foe = It->GetPawn();
		if (Foe && FVector::Dist(Foe->GetActorLocation(), GetActorLocation()) < 2200.f && It->GetMind() == EEnemyMind::Idle)
		{
			It->HearWhistle(GetActorLocation());
			++Heard;
		}
	}
	UE_LOG(LogUnrealSoulslike, Log, TEXT("Whistle: %d foes in earshot"), Heard);
}

void ASoulsKnight::RestoreAll()
{
	Interrupt();
	GetWorldTimerManager().ClearTimer(ActionTimer);
	Action = EKnightAction::None;
	CurrentHP = MaxHP;
	Stamina = MaxStamina;
	Flasks = MaxFlasks;
	Poise = MaxPoise;
	StanceDamage = 0.f;
	if (LifeBarWidget)
	{
		LifeBarWidget->SetLifePercentage(1.f);
	}
}

// ---------------------------------------------------------------------------------------------
// Melee resolution
// ---------------------------------------------------------------------------------------------

void ASoulsKnight::MeleeSweep(float Damage, float PoiseDamage, float StanceHit, bool bHeavy)
{
	UE_LOG(LogUnrealSoulslike, Verbose, TEXT("Blade: %s near-miss pass, %d tracked"), *GetName(), BladeNear.Num());
	// Fear makes the hand shake: a shaken foe's swing can go wide.
	if (Team != 0 && IsShaken() && FMath::FRand() < (IsTerrified() ? 0.5f : 0.3f))
	{
		return;
	}
	const float Scale = GetActorScale3D().X;
	const float Reach = SwordReach * Scale;
	const FVector Origin = GetActorLocation();
	const FVector Forward = GetActorForwardVector().GetSafeNormal2D();

	TArray<FOverlapResult> Overlaps;
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_Pawn);
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(KnightSweep), false, this);
	GetWorld()->OverlapMultiByObjectType(Overlaps, Origin, FQuat::Identity, Objects, FCollisionShape::MakeSphere(Reach + 60.f), Params);

	bool bLanded = false;
	for (const FOverlapResult& Overlap : Overlaps)
	{
		AActor* Other = Overlap.GetActor();
		if (!Other || Other == this || HitThisSwing.Contains(Other))
		{
			continue;
		}
		FVector ToOther = Other->GetActorLocation() - Origin;
		ToOther.Z = 0.f;
		float Radius = 0.f, HalfHeight = 0.f;
		Other->GetSimpleCollisionCylinder(Radius, HalfHeight);
		const float Distance = ToOther.Size();
		if (Distance > Reach + Radius || FMath::Abs(Other->GetActorLocation().Z - Origin.Z) > 160.f * Scale + HalfHeight)
		{
			UE_LOG(LogUnrealSoulslike, Verbose, TEXT("Blade: %s out of reach (%.0f cm, dz %.0f)"), *Other->GetName(), Distance, Other->GetActorLocation().Z - Origin.Z);
			continue;
		}
		// A forward arc of about 150 degrees; anything hugging the knight counts too.
		if (Distance > Radius + 40.f && FVector::DotProduct(Forward, ToOther / FMath::Max(Distance, 1.f)) < 0.25f)
		{
			UE_LOG(LogUnrealSoulslike, Verbose, TEXT("Blade: %s outside the arc (dot %.2f)"), *Other->GetName(), FVector::DotProduct(Forward, ToOther / FMath::Max(Distance, 1.f)));
			continue;
		}
		const FVector Impact = Other->GetActorLocation() - ToOther.GetSafeNormal() * Radius + FVector(0.f, 0.f, 40.f);

		if (ASoulsKnight* Knight = Cast<ASoulsKnight>(Other))
		{
			if (Knight->Team == Team || Knight->IsDead())
			{
				continue;
			}
			// With a sword in hand the blade decides (UNR-T57): bodies it crossed were struck as it
			// crossed them; here only a near miss (the blade within a hand's breadth) still lands, at
			// the blade's nearest point. Bladeless swings keep the old reach test.
			FVector BladeImpact = Impact;
			FVector BladeBase, BladeTip;
			if (GetBladeSegment(BladeBase, BladeTip))
			{
				const TPair<float, FVector>* Near = BladeNear.Find(Knight);
				UE_LOG(LogUnrealSoulslike, Verbose, TEXT("Blade: %s passed %s at %.0f cm"), *GetName(), *Knight->GetName(), Near ? Near->Key : -1.f);
				if (!Near || Near->Key > KBladeNearMiss * Scale)
				{
					continue;
				}
				BladeImpact = Near->Value;
			}
			HitThisSwing.Add(Other);
			ResolveStrike(Knight, BladeImpact, Damage, PoiseDamage, StanceHit, bHeavy);
		}
		else if (ICombatDamageable* Damageable = Cast<ICombatDamageable>(Other))
		{
			// Crates, dummies and the template's brawlers (they count health in single hits).
			HitThisSwing.Add(Other);
			Damageable->ApplyDamage(bHeavy ? 2.f : 1.f, this, Impact, ToOther.GetSafeNormal() * 300.f + FVector(0.f, 0.f, 200.f));
			bLanded = true;
		}
	}

	if (bLanded)
	{
		Hitstop(bHeavy ? 0.11f : 0.065f);
		KickCamera(bHeavy ? 0.6f : 0.3f);
	}
}

ASoulsKnight::EMeleeResult ASoulsKnight::ReceiveMelee(ASoulsKnight* Attacker, float Damage, float InPoiseDamage, float StanceHit, bool bHeavy, const FVector& ImpactPoint)
{
	if (IsDead() || Action == EKnightAction::Crippled)
	{
		return EMeleeResult::Ignored;
	}
	if (Action == EKnightAction::Roll && ActionElapsed >= RollInvulnerableStart && ActionElapsed <= RollInvulnerableEnd)
	{
		return EMeleeResult::Dodged;
	}

	const FVector ToAttacker = (Attacker->GetActorLocation() - GetActorLocation()).GetSafeNormal2D();
	const bool bFacing = FVector::DotProduct(GetActorForwardVector(), ToAttacker) > 0.2f;

	// Ambushed (never saw it coming, or still reeling from the shock): no guard, extra damage,
	// and the surprise freezes it in place.
	const ASoulsEnemyController* Mind = Cast<ASoulsEnemyController>(GetController());
	const bool bUnaware = Mind && Mind->GetMind() == EEnemyMind::Idle;
	if (bUnaware || IsShocked())
	{
		Damage *= 1.5f;
		bKilledByAmbush = Damage >= CurrentHP;
		PlayRandomSound(HitSounds, 1.f);
		ReceivedDamage(Damage, ImpactPoint, -ToAttacker);
		FSoulsBlood::Splash(this, ImpactPoint, -ToAttacker, 2.5f);
		LoseHealth(Damage, Attacker);
		if (!IsDead() && bUnaware)
		{
			Shock(1.9f);
		}
		Hitstop(0.12f);
		KickCamera(0.6f);
		Attacker->KickCamera(0.6f);
		return EMeleeResult::Hit;
	}

	// A guard raised just before the blow lands is a parry (players only; the AI blocks plainly).
	const bool bTimedGuard = bBlocking && IsPlayerControlled()
		&& GetWorld()->GetTimeSeconds() - BlockPressTime <= KGuardParryWindow;
	UE_LOG(LogUnrealSoulslike, Verbose, TEXT("Melee on %s: blocking=%d facing=%d since guard %.2f"), *GetName(), bBlocking, bFacing,
		GetWorld()->GetTimeSeconds() - BlockPressTime);
	if (bFacing && (IsInParryWindow() || bTimedGuard))
	{
		PlaySound(ParrySound, 1.f);
		ReceivedDamage(0.f, ImpactPoint, -ToAttacker);
		Attacker->GetParried(this);
		++ClashParries;
		UE_LOG(LogUnrealSoulslike, Log, TEXT("Clash: %s parried %s"), *GetName(), *Attacker->GetName());
		ASoulsSparks::Burst(GetWorld(), (Shield ? Shield->GetComponentLocation() : GetActorLocation()) + ToAttacker * 30.f,
			ToAttacker + FVector(0.f, 0.f, 0.5f), 22, 1.6f, 1.f, FLinearColor(1.f, 0.85f, 0.55f));
		Hitstop(0.14f);
		KickCamera(0.7f);
		return EMeleeResult::Parried;
	}

	if (bBlocking && bFacing)
	{
		PlayRandomSound(BlockSounds, 0.9f);
		ReceivedDamage(0.f, ImpactPoint, -ToAttacker);
		LoseHealth(Damage * BlockDamageFraction, Attacker);
		GuardImpact(ToAttacker, bHeavy);
		++ClashBlocks;
		UE_LOG(LogUnrealSoulslike, Log, TEXT("Clash: %s blocked %s (%s)"), *GetName(), *Attacker->GetName(), bHeavy ? TEXT("heavy") : TEXT("light"));
		Hitstop(bHeavy ? 0.1f : 0.08f);
		KickCamera(bHeavy ? 0.6f : 0.4f);
		return EMeleeResult::Blocked;
	}

	PlayRandomSound(HitSounds, 1.f);
	if (IsPlayerControlled())
	{
		PlayRandomSound(HurtSounds, 0.8f);
	}
	ReceivedDamage(Damage, ImpactPoint, -ToAttacker);
	// Ludicrous gore: a fountain per hit, more for a heavy blow.
	FSoulsBlood::Splash(this, ImpactPoint, -ToAttacker, bHeavy ? 2.f : 1.f);
	LoseHealth(Damage, Attacker);
	if (!IsDead())
	{
		TakePoiseAndStance(InPoiseDamage, StanceHit);
		if (Action == EKnightAction::Staggered)
		{
			GetCharacterMovement()->AddImpulse(-ToAttacker * (bHeavy ? 650.f : 380.f), true);
		}
	}
	Hitstop(bHeavy ? 0.11f : 0.065f);
	KickCamera(bHeavy ? 0.9f : 0.5f);
	return EMeleeResult::Hit;
}

void ASoulsKnight::LoseHealth(float Amount, AActor* Source)
{
	// The demo stands still for its shots; enemies only scratch it so it lives to take them all.
	if (bVerificationDemo && Source)
	{
		Amount = FMath::Min(Amount, FMath::Max(CurrentHP - 30.f, 0.f));
	}
	if (IsDead() || Amount <= 0.f)
	{
		return;
	}
	CurrentHP -= Amount;
	LastDamagedTime = GetWorld()->GetTimeSeconds();
	if (LifeBarWidget)
	{
		LifeBarWidget->SetLifePercentage(GetHealthFraction());
	}
	if (ASoulsEnemyController* AI = Cast<ASoulsEnemyController>(GetController()))
	{
		AI->OnDamaged(Source);
	}
	if (CurrentHP <= 0.f)
	{
		UE_LOG(LogUnrealSoulslike, Log, TEXT("%s felled by %s (%.0f damage)"), *GetName(), Source ? *Source->GetName() : TEXT("the world"), Amount);
		CurrentHP = 0.f;
		// Felled: one last gush in every direction.
		FSoulsBlood::Splash(this, GetActorLocation() + FVector(0.f, 0.f, 40.f), FVector::UpVector, 3.f);
		HandleDeath();
	}
}

void ASoulsKnight::TakePoiseAndStance(float InPoiseDamage, float StanceHit)
{
	Poise -= InPoiseDamage;
	StanceDamage += StanceHit;
	if (MaxStance > 0.f && StanceDamage >= MaxStance)
	{
		// Stance broken: down on one knee, open to a critical.
		StanceDamage = 0.f;
		Poise = MaxPoise;
		Interrupt();
		PlayAction(EKnightAction::Parried, GuardBreakMontage ? GuardBreakMontage.Get() : HitMontage.Get(), KStanceBrokenDuration);
		PlaySound(ParrySound, 0.6f);
	}
	else if (Poise <= 0.f)
	{
		Poise = MaxPoise;
		Interrupt();
		PlayAction(EKnightAction::Staggered, HitMontage, 0.6f);
	}
}

void ASoulsKnight::GetParried(ASoulsKnight* Parrier)
{
	if (IsDead())
	{
		return;
	}
	Interrupt();
	PlayAction(EKnightAction::Parried, GuardBreakMontage ? GuardBreakMontage.Get() : HitMontage.Get(), KParriedDuration);
	Hitstop(0.14f);
}

bool ASoulsKnight::TryCritical()
{
	ASoulsKnight* Best = nullptr;
	bool bBestIsRiposte = false;
	float BestDistance = TNumericLimits<float>::Max();
	const FVector Facing = AttackDirection();
	for (TActorIterator<ASoulsKnight> It(GetWorld()); It; ++It)
	{
		ASoulsKnight* Other = *It;
		if (Other == this || Other->Team == Team || Other->IsDead() || Other->Action == EKnightAction::Crippled)
		{
			continue;
		}
		const FVector ToOther = (Other->GetActorLocation() - GetActorLocation()).GetSafeNormal2D();
		const float Distance = FVector::Dist2D(Other->GetActorLocation(), GetActorLocation());
		if (Distance > KCriticalRange * Other->GetActorScale3D().X || FVector::DotProduct(Facing, ToOther) < 0.4f)
		{
			continue;
		}
		const bool bRiposte = Other->IsOpenToCritical();
		const bool bBackstab = !bRiposte && Other->bCanBeBackstabbed && !Other->IsBusy()
			&& FVector::DotProduct(Other->GetActorForwardVector().GetSafeNormal2D(), ToOther) > 0.55f;
		if ((bRiposte || bBackstab) && Distance < BestDistance)
		{
			Best = Other;
			bBestIsRiposte = bRiposte;
			BestDistance = Distance;
		}
	}
	if (!Best || !CriticalMontage)
	{
		return false;
	}

	SetBlocking(false);
	// Step into the strike: in front of a reeling foe, or right behind an unaware one.
	const FVector Approach = bBestIsRiposte
		? Best->GetActorForwardVector().GetSafeNormal2D()
		: -Best->GetActorForwardVector().GetSafeNormal2D();
	FVector Spot = Best->GetActorLocation() + Approach * (110.f * Best->GetActorScale3D().X);
	Spot.Z = GetActorLocation().Z;
	SetActorLocation(Spot, true);
	FaceTowards(Best->GetActorLocation());
	CriticalVictim = Best;
	Best->ReceiveCritical(this, KCriticalDuration);
	PlayAction(EKnightAction::Critical, CriticalMontage, KCriticalDuration);
	GetWorldTimerManager().SetTimer(HitTimer, this, &ASoulsKnight::LandCritical, KCriticalDuration * 0.45f, false);
	return true;
}

void ASoulsKnight::ReceiveCritical(ASoulsKnight* Attacker, float Duration)
{
	Interrupt();
	GetWorldTimerManager().ClearTimer(ActionTimer);
	FaceTowards(Attacker->GetActorLocation());
	PlayAction(EKnightAction::Crippled, nullptr, Duration + 0.5f);
}

void ASoulsKnight::LandCritical()
{
	ASoulsKnight* Victim = CriticalVictim.Get();
	if (!Victim || Victim->IsDead())
	{
		return;
	}
	PlaySound(CriticalSound, 1.f);
	PlayRandomSound(HitSounds, 1.f);
	const FVector Impact = Victim->GetActorLocation() + FVector(0.f, 0.f, 50.f);
	Victim->ReceivedDamage(CriticalDamage, Impact, GetActorForwardVector());
	FSoulsBlood::Splash(this, Impact, GetActorForwardVector() + FVector(0.f, 0.f, 0.4f), 3.f);
	Victim->LoseHealth(CriticalDamage, this);
	if (!Victim->IsDead() && Victim->StaggerMontage)
	{
		if (UAnimInstance* Anim = Victim->GetMesh()->GetAnimInstance())
		{
			Anim->Montage_Play(Victim->StaggerMontage, 1.f);
		}
	}
	Hitstop(0.2f);
	Victim->Hitstop(0.2f);
	KickCamera(1.f);
}

void ASoulsKnight::NotifyDanger(const FVector& DangerLocation, AActor* DangerSource)
{
	if (ASoulsEnemyController* AI = Cast<ASoulsEnemyController>(GetController()))
	{
		AI->OnDanger(DangerSource);
	}
}

// Damage from anything that isn't a knight's sword (the template's brawlers, hazards).
float ASoulsKnight::TakeDamage(float Damage, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	if (IsDead() || (Action == EKnightAction::Roll && ActionElapsed >= RollInvulnerableStart && ActionElapsed <= RollInvulnerableEnd))
	{
		return 0.f;
	}
	const bool bFacing = DamageCauser
		&& FVector::DotProduct(GetActorForwardVector(), (DamageCauser->GetActorLocation() - GetActorLocation()).GetSafeNormal2D()) > 0.2f;
	if (bBlocking && bFacing)
	{
		PlayRandomSound(BlockSounds, 0.9f);
		Stamina -= 20.f;
		Damage *= BlockDamageFraction;
	}
	else
	{
		PlayRandomSound(HitSounds, 1.f);
		TakePoiseAndStance(15.f, 10.f);
	}
	LoseHealth(Damage, DamageCauser);
	return Damage;
}

void ASoulsKnight::ApplyDamage(float Damage, AActor* DamageCauser, const FVector& DamageLocation, const FVector& DamageImpulse)
{
	// The template's sources deal damage on a 5-hit-point scale.
	FDamageEvent DamageEvent;
	const float Actual = TakeDamage(Damage * 12.f, DamageEvent, nullptr, DamageCauser);
	if (Actual > 0.f)
	{
		GetCharacterMovement()->AddImpulse(DamageImpulse.GetSafeNormal2D() * 300.f, true);
		ReceivedDamage(Actual, DamageLocation, DamageImpulse.GetSafeNormal());
	}
}

void ASoulsKnight::HandleDeath()
{
	GetWorldTimerManager().ClearTimer(ActionTimer);
	GetWorldTimerManager().ClearTimer(HitTimer);
	GetWorldTimerManager().ClearTimer(HealTimer);
	GetWorldTimerManager().ClearTimer(BlockHoldTimer);
	GetWorldTimerManager().ClearTimer(WhooshTimer);
	GetWorldTimerManager().ClearTimer(PounceTimer);
	if (GrappleMode != EGrappleMode::None)
	{
		EndGrapple(false);
	}
	CustomTimeDilation = 1.f;
	Action = EKnightAction::None;
	bBlocking = false;
	LockTarget = nullptr;
	PlaySound(DeathSound, 1.f);
	if (UWidgetComponent* FloatingBar = FindComponentByClass<UWidgetComponent>())
	{
		FloatingBar->SetVisibility(false);
	}
	if (ASoulsGameMode* GameMode = GetWorld()->GetAuthGameMode<ASoulsGameMode>())
	{
		GameMode->KnightDied(this);
	}
	if (AController* AI = GetController(); AI && !IsPlayerControlled())
	{
		AI->StopMovement();
	}
	Super::HandleDeath();
}

void ASoulsKnight::FellOutOfWorld(const UDamageType& DmgType)
{
	if (!IsDead())
	{
		CurrentHP = 0.f;
		if (ASoulsGameMode* GameMode = GetWorld()->GetAuthGameMode<ASoulsGameMode>())
		{
			GameMode->KnightDied(this);
		}
	}
	Super::FellOutOfWorld(DmgType);
}

// ---------------------------------------------------------------------------------------------
// Lock-on
// ---------------------------------------------------------------------------------------------

ASoulsKnight* ASoulsKnight::FindLockTarget() const
{
	const AController* PC = GetController();
	if (!PC)
	{
		return nullptr;
	}
	const FVector ViewDir = PC->GetControlRotation().Vector().GetSafeNormal2D();
	ASoulsKnight* Best = nullptr;
	float BestScore = TNumericLimits<float>::Max();
	for (TActorIterator<ASoulsKnight> It(GetWorld()); It; ++It)
	{
		ASoulsKnight* Other = *It;
		if (Other == this || Other->Team == Team || !Other->CanBeLockedOn())
		{
			continue;
		}
		const FVector To = Other->GetActorLocation() - GetActorLocation();
		const float Distance = To.Size();
		const float Dot = FVector::DotProduct(ViewDir, To.GetSafeNormal2D());
		if (Distance > KLockRange || Dot < 0.3f)
		{
			continue;
		}
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(LockOn), false, this);
		Params.AddIgnoredActor(Other);
		if (GetWorld()->LineTraceSingleByChannel(Hit, GetActorLocation() + FVector(0, 0, 60), Other->GetActorLocation() + FVector(0, 0, 60), ECC_Visibility, Params))
		{
			continue;
		}
		const float Score = (1.f - Dot) * 2500.f + Distance;
		if (Score < BestScore)
		{
			BestScore = Score;
			Best = Other;
		}
	}
	return Best;
}

void ASoulsKnight::ToggleLockOn()
{
	if (LockTarget.IsValid())
	{
		LockTarget = nullptr;
		return;
	}
	LockTarget = FindLockTarget();
	if (!LockTarget.IsValid() && Controller)
	{
		// Nothing to lock onto: recentre the camera behind the knight, like the games do.
		Controller->SetControlRotation(FRotator(-12.f, GetActorRotation().Yaw, 0.f));
	}
}

void ASoulsKnight::UpdateLockOn(float DeltaSeconds)
{
	ASoulsKnight* Target = LockTarget.Get();
	if (Target && (!Target->CanBeLockedOn() || GetDistanceTo(Target) > KLockBreakRange))
	{
		// The target fell: hop to the next one in view, if any.
		LockTarget = Target->IsDead() ? FindLockTarget() : nullptr;
		Target = LockTarget.Get();
	}
	if (!Target || !Controller)
	{
		return;
	}
	const FVector Eye = GetActorLocation() + FVector(0.f, 0.f, 70.f);
	const FVector Aim = Target->GetActorLocation() + FVector(0.f, 0.f, 20.f * Target->GetActorScale3D().Z);
	FRotator Desired = (Aim - Eye).Rotation();
	Desired.Pitch = FMath::Clamp(Desired.Pitch - 12.f, -35.f, 5.f);
	Controller->SetControlRotation(FMath::RInterpTo(Controller->GetControlRotation(), Desired, DeltaSeconds, 9.f));

	// Squared up to the target when standing, guarding or parrying; free to run around it.
	if ((Action == EKnightAction::None || Action == EKnightAction::Parry) && GetVelocity().Size2D() < 30.f)
	{
		const float Yaw = (Target->GetActorLocation() - GetActorLocation()).Rotation().Yaw;
		SetActorRotation(FMath::RInterpTo(GetActorRotation(), FRotator(0.f, Yaw, 0.f), DeltaSeconds, 10.f));
	}
}

// ---------------------------------------------------------------------------------------------
// Tick
// ---------------------------------------------------------------------------------------------

void ASoulsKnight::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	ActionElapsed += DeltaSeconds;
	const float Now = GetWorld()->GetTimeSeconds();
	UCharacterMovementComponent* Movement = GetCharacterMovement();

	if (bVerificationDemo && Controller && !bDemoCameraSet)
	{
		Controller->SetControlRotation(FRotator(-10.f, GetActorRotation().Yaw + 130.f, 0.f));
		bDemoCameraSet = true;
	}

	if (IsDead())
	{
		return;
	}
	TickStealthDemo();
	TickGlide(DeltaSeconds);
	// Handed back by a vehicle (or anything else) with movement switched off: a visible, free,
	// living player knight always gets its feet back (UNR-T73).
	if (IsPlayerControlled() && !IsDead() && !IsHidden() && !GetAttachParentActor()
		&& GetCharacterMovement()->MovementMode == MOVE_None && !(ShadowMelt && ShadowMelt->IsMerged()))
	{
		GetCharacterMovement()->SetMovementMode(MOVE_Falling);
	}
	// Back on the ground (or in the water): the grapple launch is spent.
	if (bGrappleLaunched && !bGliding && GetWorld()->GetTimeSeconds() - GrappleLaunchTime > 0.3f
		&& (GetCharacterMovement()->IsMovingOnGround() || IsSwimming()))
	{
		bGrappleLaunched = false;
	}
	// Verification: how fast the blade tip moves through a swing (-LogCmds="LogUnrealSoulslike Verbose").
	if (IsPlayerControlled() && IsAttacking() && UE_LOG_ACTIVE(LogUnrealSoulslike, Verbose))
	{
		FVector TipBase, Tip;
		if (GetBladeSegment(TipBase, Tip))
		{
			static FVector LastTip = FVector::ZeroVector;
			UE_LOG(LogUnrealSoulslike, Verbose, TEXT("Swing %d progress %.2f tip speed %.0f"), static_cast<int32>(Action), GetActionProgress(),
				FVector::Dist(Tip, LastTip) / FMath::Max(DeltaSeconds, 0.001f));
			LastTip = Tip;
		}
	}
	// The blade is live just before and after the strike moment: sweep it against bodies.
	{
		const bool bLive = IsAttacking() && !ASoulsTorch::GetWielded(this) && Now >= BladeLiveFrom && Now <= BladeLiveUntil;
		if (bLive)
		{
			SweepBlade();
		}
		else if (!IsAttacking())
		{
			bBladeTracked = false;
		}
	}
	// Standing in a burning field hurts.
	if (!IsDead())
	{
		const FVector Feet = GetActorLocation() - FVector(0.f, 0.f, GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
		const float Flame = FSoulsFire::FlameIntensityAt(this, Feet);
		if (Flame > 0.2f)
		{
			LoseHealth(BurnDamagePerSecond * Flame * DeltaSeconds, nullptr);
		}
	}
	TickClashDemo();

	// The player's slash, with X pressed again, flows into the back-slash from its end point.
	if (Action == EKnightAction::HeavyAttack && bHeavySwing && bComboQueued && IsPlayerControlled()
		&& ActionElapsed >= ActionDuration * KBackSlashAt && SwordComboMontages.Num() > 1 && SpendStamina(LightAttackCost))
	{
		StartSwing(1);
	}

	if (Action == EKnightAction::LightAttack && bComboQueued && ActionElapsed >= ActionDuration * ComboChainFraction
		&& SwingIndex + 1 < SwordComboMontages.Num() && SpendStamina(LightAttackCost))
	{
		StartSwing(SwingIndex + 1);
	}

	if (Action == EKnightAction::Roll)
	{
		const float T = FMath::Clamp(ActionElapsed / FMath::Max(ActionDuration, 0.01f), 0.f, 1.f);
		const float Speed = RollSpeed * (1.f - 0.75f * T * T);
		Movement->Velocity = FVector(RollDirection.X * Speed, RollDirection.Y * Speed, Movement->Velocity.Z);
	}

	// The swing's step-in.
	if (LungeDelay > 0.f)
	{
		LungeDelay -= DeltaSeconds;
	}
	else if (LungeRemaining > 0.f)
	{
		LungeRemaining -= DeltaSeconds;
		if (IsAttacking() && Movement->IsMovingOnGround())
		{
			// Moved directly (the attack clips may carry root motion that would override a velocity):
			// a swept step that stops against a body and slides along walls and slopes.
			const FVector Step = LungeDirection * LungeSpeed * DeltaSeconds;
			FHitResult Hit;
			Movement->SafeMoveUpdatedComponent(Step, GetActorQuat(), true, Hit);
			if (Hit.IsValidBlockingHit() && !Cast<APawn>(Hit.GetActor()))
			{
				const FVector Rest = FVector::VectorPlaneProject(Step * (1.f - Hit.Time), Hit.Normal);
				FHitResult Slide;
				Movement->SafeMoveUpdatedComponent(Rest, GetActorQuat(), true, Slide);
			}
		}
		else
		{
			LungeRemaining = 0.f;
		}
	}

	// Attackers keep turning toward their mark through the wind-up, then commit.
	if (IsAttacking() && GetActionProgress() < 0.32f)
	{
		const AActor* Mark = LockTarget.IsValid() ? static_cast<AActor*>(LockTarget.Get()) : TrackTarget.Get();
		if (Mark)
		{
			const float Yaw = (Mark->GetActorLocation() - GetActorLocation()).Rotation().Yaw;
			SetActorRotation(FMath::RInterpConstantTo(GetActorRotation(), FRotator(0.f, Yaw, 0.f), DeltaSeconds, 280.f));
		}
	}

	if (IsPlayerControlled())
	{
		UpdateLockOn(DeltaSeconds);

		// Tapped L3: sprint until the stick is released, the guard goes up or stamina runs dry.
		const FVector Vel = GetVelocity();
		const bool bMoving = Vel.Size2D() > 50.f;
		const bool bStickHeld = !Movement->GetCurrentAcceleration().IsNearlyZero();
		// Tapped before moving off, the sprint waits a moment for the stick.
		if (bSprintToggled && ((!bStickHeld && (bSprinting || Now - SprintPressTime > 1.5f)) || Stamina <= 0.f || bBlocking))
		{
			bSprintToggled = false;
		}
		bSprinting = bSprintToggled && Stance == EKnightStance::Standing && !IsBusy() && !bBlocking && bMoving && Stamina > 0.f;
		Movement->MaxWalkSpeed = Stance == EKnightStance::Prone ? CrawlSpeed
			: Stance == EKnightStance::Crouched ? CrouchSpeed
			: bBlocking ? KBlockWalkSpeed : (bSprinting ? KSprintSpeed : RunSpeed);
		if (bSprinting)
		{
			Stamina -= KSprintStaminaPerSecond * DeltaSeconds;
			StaminaIdleTime = 0.f;
		}

		// Footsteps by distance covered.
		if (Movement->IsMovingOnGround() && Vel.Size2D() > 120.f)
		{
			StepDistance += Vel.Size2D() * DeltaSeconds;
			if (StepDistance > (bSprinting ? 210.f : 165.f))
			{
				StepDistance = 0.f;
				PlayRandomSound(FootstepSounds, Stance == EKnightStance::Standing ? 0.28f : 0.1f);
			}
		}

		// Camera kick on impacts.
		CameraKick = FMath::FInterpTo(CameraKick, 0.f, DeltaSeconds, 9.f);
		GetFollowCamera()->SetRelativeLocation(CameraKick > 0.01f
			? FVector(0.f, FMath::FRandRange(-1.f, 1.f), FMath::FRandRange(-1.f, 1.f)) * 7.f * CameraKick
			: FVector::ZeroVector);
	}

	TickGrapple(DeltaSeconds);
	UpdateGrappleLine();
	TickPounce(DeltaSeconds);
	TickSwimming(DeltaSeconds);
	TickStance(DeltaSeconds);
	TickFear(DeltaSeconds);
	if (IsPlayerControlled() && Now - GrappleCandidateCheck > 0.1f)
	{
		GrappleCandidateCheck = Now;
		GrappleCandidate = (IsBusy() || IsSwimming()) ? nullptr : FindGrappleTarget();
	}

	// No stamina (UNR-T59): whatever drained it this frame (swimming, the Shade's puddles), it's full.
	Stamina = MaxStamina;
	StaminaIdleTime = 0.f;

	if (Now - LastDamagedTime > KPoiseRecoverDelay)
	{
		Poise = MaxPoise;
	}
	if (Now - LastDamagedTime > KStanceRecoverDelay && StanceDamage > 0.f)
	{
		StanceDamage = FMath::Max(0.f, StanceDamage - KStanceRecoverPerSecond * DeltaSeconds);
	}
}
