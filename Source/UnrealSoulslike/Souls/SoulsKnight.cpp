#include "SoulsKnight.h"
#include "SoulsEnemy.h"
#include "SoulsGameMode.h"
#include "SiteOfGrace.h"
#include "GrapplePoint.h"
#include "SoulsPickup.h"
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
#include "InputMappingContext.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Sound/SoundBase.h"
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
	constexpr float KSprintStaminaPerSecond = 14.f;
	constexpr float KDodgeHoldToSprint = 0.28f;
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
	const TArray<FStep> Steps = {
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
					Foe->BeginPounce(K);
					if (K->Controller)
					{
						K->Controller->SetControlRotation(FRotator(-8.f, K->GetActorRotation().Yaw + 70.f, 0.f));
					}
				}
			} },
		{ 38.45f, [](ASoulsKnight* K) { K->ParryPressed(); } },
		{ 38.9f, [](ASoulsKnight* K) { K->Shot(TEXT("20_pounce")); } },
		{ 39.45f, [](ASoulsKnight* K) { K->Shot(TEXT("21_impaled")); } },
		{ 40.4f, [](ASoulsKnight* K) { K->Shot(TEXT("22_rolled_off")); } },
		// Ambush: a foe caught by surprise freezes; terror spreads.
		{ 42.0f, [Approach, NearestFoe](ASoulsKnight* K) { Approach(K, 170.f); if (ASoulsKnight* Foe = NearestFoe(K)) { Foe->Shock(2.5f); } } },
		{ 42.4f, [](ASoulsKnight* K) { K->Shot(TEXT("23_shocked")); K->DoComboAttackStart(); } },
		{ 43.2f, [](ASoulsKnight* K) { K->Shot(TEXT("24_ambush_crit")); } },
		{ 45.0f, [Approach, NearestFoe](ASoulsKnight* K) { Approach(K, 500.f); if (ASoulsKnight* Foe = NearestFoe(K)) { Foe->AddFear(80.f); } } },
		{ 46.5f, [](ASoulsKnight* K) { K->Shot(TEXT("25_terrified")); } },
		// Something glinting off the path.
		{ 48.0f, [](ASoulsKnight* K)
			{
				for (TActorIterator<ASoulsPickup> It(K->GetWorld()); It; ++It)
				{
					const FVector Spot = It->GetActorLocation() - K->GetActorForwardVector() * 150.f;
					K->SetActorLocation(FVector(Spot.X, Spot.Y, K->GetActorLocation().Z), false, nullptr, ETeleportType::TeleportPhysics);
					K->FaceTowards(It->GetActorLocation());
					if (K->Controller)
					{
						K->Controller->SetControlRotation(FRotator(-20.f, K->GetActorRotation().Yaw, 0.f));
					}
					break;
				}
			} },
		{ 48.6f, [](ASoulsKnight* K) { K->Shot(TEXT("26_pickup")); K->InteractPressed(); } },
		{ 49.2f, [](ASoulsKnight* K) { K->Shot(TEXT("27_acquired")); } },
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

	auto MakeAction = [this](const TCHAR* Name)
	{
		UInputAction* NewAction = NewObject<UInputAction>(this, Name);
		NewAction->ValueType = EInputActionValueType::Boolean;
		return NewAction;
	};
	DodgeAction = MakeAction(TEXT("IA_KnightDodge"));
	BlockAction = MakeAction(TEXT("IA_KnightBlock"));
	HealAction = MakeAction(TEXT("IA_KnightHeal"));
	ParryAction = MakeAction(TEXT("IA_KnightParry"));
	LockOnAction = MakeAction(TEXT("IA_KnightLockOn"));
	InteractAction = MakeAction(TEXT("IA_KnightInteract"));
	GrappleAction = MakeAction(TEXT("IA_KnightGrapple"));

	KnightMappingContext = NewObject<UInputMappingContext>(this, TEXT("IMC_Knight"));
	KnightMappingContext->MapKey(DodgeAction, EKeys::Gamepad_FaceButton_Right);
	KnightMappingContext->MapKey(DodgeAction, EKeys::LeftShift);
	KnightMappingContext->MapKey(BlockAction, EKeys::Gamepad_LeftShoulder);
	KnightMappingContext->MapKey(BlockAction, EKeys::RightMouseButton);
	KnightMappingContext->MapKey(HealAction, EKeys::Gamepad_RightShoulder);
	KnightMappingContext->MapKey(HealAction, EKeys::R);
	KnightMappingContext->MapKey(ParryAction, EKeys::Gamepad_LeftTrigger);
	KnightMappingContext->MapKey(ParryAction, EKeys::C);
	KnightMappingContext->MapKey(LockOnAction, EKeys::Gamepad_RightThumbstick);
	KnightMappingContext->MapKey(LockOnAction, EKeys::MiddleMouseButton);
	KnightMappingContext->MapKey(LockOnAction, EKeys::Q);
	KnightMappingContext->MapKey(InteractAction, EKeys::Gamepad_DPad_Up);
	KnightMappingContext->MapKey(InteractAction, EKeys::E);
	KnightMappingContext->MapKey(GrappleAction, EKeys::Gamepad_RightTrigger);
	KnightMappingContext->MapKey(GrappleAction, EKeys::G);
	KnightMappingContext->MapKey(GrappleAction, EKeys::ThumbMouseButton);
	// Light/heavy reuse the template's actions; these guarantee X/Y and LMB/F whatever its map says.
	if (ComboAttackAction)
	{
		KnightMappingContext->MapKey(ComboAttackAction, EKeys::Gamepad_FaceButton_Left);
		KnightMappingContext->MapKey(ComboAttackAction, EKeys::LeftMouseButton);
	}
	if (ChargedAttackAction)
	{
		KnightMappingContext->MapKey(ChargedAttackAction, EKeys::Gamepad_FaceButton_Top);
		KnightMappingContext->MapKey(ChargedAttackAction, EKeys::F);
	}
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
		Input->BindAction(BlockAction, ETriggerEvent::Started, this, &ASoulsKnight::BlockPressed);
		Input->BindAction(BlockAction, ETriggerEvent::Completed, this, &ASoulsKnight::BlockReleased);
		Input->BindAction(HealAction, ETriggerEvent::Started, this, &ASoulsKnight::HealPressed);
		Input->BindAction(ParryAction, ETriggerEvent::Started, this, &ASoulsKnight::ParryPressed);
		Input->BindAction(LockOnAction, ETriggerEvent::Started, this, &ASoulsKnight::ToggleLockOn);
		Input->BindAction(InteractAction, ETriggerEvent::Started, this, &ASoulsKnight::InteractPressed);
		Input->BindAction(GrappleAction, ETriggerEvent::Started, this, &ASoulsKnight::GrapplePressed);
	}
}

// ---------------------------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------------------------

bool ASoulsKnight::SpendStamina(float Cost)
{
	if (Stamina <= 0.f)
	{
		return false;
	}
	// Souls rule: any stamina left lets the action through, even if it drains below zero.
	Stamina -= Cost;
	StaminaIdleTime = 0.f;
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
	SetBlocking(false);
	if (UAnimInstance* Anim = GetMesh()->GetAnimInstance())
	{
		Anim->Montage_Stop(0.1f);
	}
}

void ASoulsKnight::DoComboAttackStart()
{
	if (IsDead() || IsSwimming() || SwordComboMontages.IsEmpty())
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
	if (TryCritical() || !SpendStamina(LightAttackCost))
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
	SetActorRotation(FRotator(0.f, AttackDirection().Rotation().Yaw, 0.f));
	const float Duration = Montage ? Montage->GetPlayLength() / AttackSpeed : 0.6f;
	PlayAction(EKnightAction::LightAttack, Montage, Duration);
	GetWorldTimerManager().SetTimer(HitTimer, this, &ASoulsKnight::SwingHit, Duration * SwingHitFraction, false);
	const TWeakObjectPtr<ASoulsKnight> WeakThis(this);
	GetWorldTimerManager().SetTimer(WhooshTimer, [WeakThis]()
	{
		if (WeakThis.IsValid())
		{
			WeakThis->PlayRandomSound(WeakThis->SwingSounds, 0.7f);
		}
	}, FMath::Max(Duration * SwingHitFraction - 0.12f, 0.01f), false);
	NotifyEnemiesOfIncomingAttack();
}

void ASoulsKnight::DoChargedAttackStart()
{
	if (IsDead() || IsSwimming() || IsBusy() || !HeavyAttackMontage || GetCharacterMovement()->IsFalling())
	{
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
	SetActorRotation(FRotator(0.f, AttackDirection().Rotation().Yaw, 0.f));
	const float Duration = HeavyAttackMontage->GetPlayLength() / (AttackSpeed * 0.9f);
	PlayAction(EKnightAction::HeavyAttack, HeavyAttackMontage, Duration);
	GetWorldTimerManager().SetTimer(HitTimer, this, &ASoulsKnight::SwingHit, Duration * 0.5f, false);
	const TWeakObjectPtr<ASoulsKnight> WeakThis(this);
	GetWorldTimerManager().SetTimer(WhooshTimer, [WeakThis]()
	{
		if (WeakThis.IsValid())
		{
			WeakThis->PlayRandomSound(WeakThis->SwingSounds, 0.9f);
		}
	}, FMath::Max(Duration * 0.5f - 0.12f, 0.01f), false);
	NotifyEnemiesOfIncomingAttack();
}

void ASoulsKnight::SwingHit()
{
	if (bHeavySwing)
	{
		MeleeSweep(HeavyDamage, 34.f, 32.f, true);
	}
	else
	{
		MeleeSweep(LightDamage, 14.f, 12.f, false);
	}
}

void ASoulsKnight::DodgePressed()
{
	// Tap to roll (on release), hold to sprint.
	bDodgeHeld = true;
	DodgePressTime = GetWorld()->GetTimeSeconds();
}

void ASoulsKnight::DodgeReleased()
{
	const bool bWasTap = bDodgeHeld && GetWorld()->GetTimeSeconds() - DodgePressTime < KDodgeHoldToSprint;
	bDodgeHeld = false;
	bSprinting = false;
	if (bWasTap)
	{
		FVector Direction = GetLastMovementInputVector().GetSafeNormal2D();
		if (Direction.IsNearlyZero())
		{
			// No stick: roll away from whatever we face (a backstep, Souls style, but rolling).
			Direction = LockTarget.IsValid() ? -AttackDirection() : GetActorForwardVector();
		}
		DodgeToward(Direction);
	}
}

void ASoulsKnight::DodgeToward(const FVector& Direction)
{
	if (IsDead() || IsSwimming() || GetCharacterMovement()->IsFalling() || !RollMontage)
	{
		return;
	}
	// The recovery frames of a swing can be rolled out of; the swing itself can't.
	const bool bCancelable = Action == EKnightAction::None
		|| (IsAttacking() && ActionElapsed > ActionDuration * 0.65f);
	if (!bCancelable || !SpendStamina(RollCost))
	{
		return;
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
	if (!IsBusy() && !IsSwimming())
	{
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
	if (IsDead() || IsSwimming() || IsBusy() || Flasks <= 0 || CurrentHP >= MaxHP || !HealMontage)
	{
		return;
	}
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
	if (ASiteOfGrace* Grace = NearbyGrace.Get())
	{
		if (!IsDead() && !IsBusy())
		{
			Grace->Rest(this);
		}
	}
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
			continue;
		}
		// A forward arc of about 150 degrees; anything hugging the knight counts too.
		if (Distance > Radius + 40.f && FVector::DotProduct(Forward, ToOther / FMath::Max(Distance, 1.f)) < 0.25f)
		{
			continue;
		}
		const FVector Impact = Other->GetActorLocation() - ToOther.GetSafeNormal() * Radius + FVector(0.f, 0.f, 40.f);

		if (ASoulsKnight* Knight = Cast<ASoulsKnight>(Other))
		{
			if (Knight->Team == Team || Knight->IsDead())
			{
				continue;
			}
			HitThisSwing.Add(Other);
			const EMeleeResult Result = Knight->ReceiveMelee(this, Damage, PoiseDamage, StanceHit, bHeavy, Impact);
			if (Result == EMeleeResult::Hit)
			{
				bLanded = true;
			}
			else if (Result == EMeleeResult::Blocked)
			{
				Hitstop(0.06f);
				KickCamera(0.25f);
			}
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

	if (bFacing && IsInParryWindow())
	{
		PlaySound(ParrySound, 1.f);
		ReceivedDamage(0.f, ImpactPoint, -ToAttacker);
		Attacker->GetParried(this);
		Hitstop(0.14f);
		KickCamera(0.7f);
		return EMeleeResult::Parried;
	}

	if (bBlocking && bFacing)
	{
		PlayRandomSound(BlockSounds, 0.9f);
		ReceivedDamage(0.f, ImpactPoint, -ToAttacker);
		Stamina -= InPoiseDamage * 1.4f;
		StaminaIdleTime = 0.f;
		LoseHealth(Damage * BlockDamageFraction, Attacker);
		if (!IsDead() && Stamina <= 0.f)
		{
			// Guard broken: the shield is knocked aside and the knight reels, open to a riposte.
			Interrupt();
			PlayAction(EKnightAction::Parried, GuardBreakMontage ? GuardBreakMontage.Get() : HitMontage.Get(), 1.6f);
		}
		Hitstop(0.06f);
		KickCamera(0.3f);
		return EMeleeResult::Blocked;
	}

	PlayRandomSound(HitSounds, 1.f);
	if (IsPlayerControlled())
	{
		PlayRandomSound(HurtSounds, 0.8f);
	}
	ReceivedDamage(Damage, ImpactPoint, -ToAttacker);
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

		// Hold dodge to sprint.
		const FVector Vel = GetVelocity();
		const bool bMoving = Vel.Size2D() > 50.f;
		bSprinting = bDodgeHeld && !IsBusy() && !bBlocking && bMoving && Stamina > 0.f
			&& Now - DodgePressTime >= KDodgeHoldToSprint;
		Movement->MaxWalkSpeed = bBlocking ? KBlockWalkSpeed : (bSprinting ? KSprintSpeed : RunSpeed);
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
				PlayRandomSound(FootstepSounds, 0.28f);
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
	TickFear(DeltaSeconds);
	if (IsPlayerControlled() && Now - GrappleCandidateCheck > 0.1f)
	{
		GrappleCandidateCheck = Now;
		GrappleCandidate = (IsBusy() || IsSwimming()) ? nullptr : FindGrappleTarget();
	}

	if (!IsBusy() && !bSprinting)
	{
		StaminaIdleTime += DeltaSeconds;
		if (StaminaIdleTime >= StaminaRegenDelay)
		{
			const float Rate = bBlocking ? StaminaRegenPerSecond * 0.35f : StaminaRegenPerSecond;
			Stamina = FMath::Min(MaxStamina, Stamina + Rate * DeltaSeconds);
		}
	}

	if (Now - LastDamagedTime > KPoiseRecoverDelay)
	{
		Poise = MaxPoise;
	}
	if (Now - LastDamagedTime > KStanceRecoverDelay && StanceDamage > 0.f)
	{
		StanceDamage = FMath::Max(0.f, StanceDamage - KStanceRecoverPerSecond * DeltaSeconds);
	}
}
