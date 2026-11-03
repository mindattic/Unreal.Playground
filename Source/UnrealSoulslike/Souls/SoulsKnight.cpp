#include "SoulsKnight.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "TimerManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "UnrealClient.h"
#include "Components/WidgetComponent.h"
#include "Engine/Engine.h"
#include "GameFramework/SpringArmComponent.h"

namespace
{
	const FName SwordSocket(TEXT("hand_r"));
	const FName ShieldSocket(TEXT("hand_l"));
	constexpr float WalkSpeed = 500.f;
	constexpr float BlockWalkSpeed = 220.f;
	constexpr float BlockStaminaPerHit = 20.f;
	constexpr float HitStunSeconds = 0.35f;
}

ASoulsKnight::ASoulsKnight()
{
	PrimaryActorTick.bCanEverTick = true;

	Sword = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Sword"));
	Sword->SetupAttachment(GetMesh(), SwordSocket);
	Sword->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	Shield = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Shield"));
	Shield->SetupAttachment(GetMesh(), ShieldSocket);
	Shield->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	MeleeTraceDistance = 140.f;
	MeleeTraceRadius = 70.f;
	GetCharacterMovement()->MaxWalkSpeed = WalkSpeed;
}

void ASoulsKnight::BeginPlay()
{
	Super::BeginPlay();
	Stamina = MaxStamina;
	Flasks = MaxFlasks;
	// The HUD shows the knight's own health; the template's floating bar would sit on his chest.
	if (UWidgetComponent* FloatingBar = FindComponentByClass<UWidgetComponent>())
	{
		FloatingBar->SetVisibility(false);
	}
	// Souls-style camera: further out than the template's brawler framing.
	DefaultCameraDistance = 400.f;
	GetCameraBoom()->TargetArmLength = DefaultCameraDistance;
	GetCameraBoom()->SocketOffset = FVector(0.f, 0.f, 40.f);
	if (FParse::Param(FCommandLine::Get(), TEXT("KnightDemo")))
	{
		RunVerificationDemo();
	}
#if !UE_BUILD_SHIPPING
	// -SwordXform=x,y,z,pitch,yaw,roll (and -ShieldXform=) to fit weapons to the hands without
	// rebuilding content.
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

// -KnightDemo: performs every move once and screenshots it mid-motion (Saved/Screenshots), so a
// build can be checked without anyone at the controls.
void ASoulsKnight::RunVerificationDemo()
{
	bVerificationDemo = true;
	GEngine->Exec(GetWorld(), TEXT("r.MotionBlurQuality 0"));
	GetCameraBoom()->bEnableCameraLag = false;
	GetCameraBoom()->bEnableCameraRotationLag = false;
	struct FStep { float Time; TFunction<void(ASoulsKnight*)> Do; };
	const TArray<FStep> Steps = {
		{ 1.2f, [](ASoulsKnight* K) { K->GetCameraBoom()->TargetArmLength = 160.f; K->GetCameraBoom()->SocketOffset = FVector(0.f, 0.f, -40.f); } },
		{ 1.6f, [](ASoulsKnight* K) { K->Shot(TEXT("00_hand")); } },
		{ 1.8f, [](ASoulsKnight* K) { K->GetCameraBoom()->TargetArmLength = 400.f; K->GetCameraBoom()->SocketOffset = FVector(0.f, 0.f, 40.f); } },
		{ 2.0f, [](ASoulsKnight* K) { K->Shot(TEXT("00_idle")); } },
		{ 2.5f, [](ASoulsKnight* K) { K->DoComboAttackStart(); } },
		{ 2.68f, [](ASoulsKnight* K) { K->Shot(TEXT("01_swing_a")); K->DoComboAttackStart(); } },
		{ 3.08f, [](ASoulsKnight* K) { K->Shot(TEXT("02_swing_b")); K->DoComboAttackStart(); } },
		{ 3.62f, [](ASoulsKnight* K) { K->Shot(TEXT("03_swing_c")); } },
		{ 6.0f, [](ASoulsKnight* K) { K->DoChargedAttackStart(); } },
		{ 6.7f, [](ASoulsKnight* K) { K->Shot(TEXT("04_heavy")); } },
		{ 8.5f, [](ASoulsKnight* K) { K->DodgePressed(); } },
		{ 8.75f, [](ASoulsKnight* K) { K->Shot(TEXT("05_roll")); } },
		{ 10.0f, [](ASoulsKnight* K) { K->BlockPressed(); } },
		{ 10.8f, [](ASoulsKnight* K) { K->Shot(TEXT("06_block")); } },
		{ 11.5f, [](ASoulsKnight* K) { K->BlockReleased(); K->CurrentHP = K->MaxHP * 0.5f; } },
		{ 12.0f, [](ASoulsKnight* K) { K->HealPressed(); } },
		{ 12.8f, [](ASoulsKnight* K) { K->Shot(TEXT("07_heal")); } },
		{ 15.0f, [](ASoulsKnight* K) { K->Jump(); } },
		{ 15.3f, [](ASoulsKnight* K) { K->Shot(TEXT("08_jump")); } },
		{ 17.0f, [](ASoulsKnight* K) { K->SpawnSparringEnemy(); } },
		{ 19.0f, [](ASoulsKnight* K) { K->Shot(TEXT("09_enemy")); } },
		{ 19.2f, [](ASoulsKnight* K) { K->FaceSparringEnemy(); K->DoComboAttackStart(); } },
		{ 19.4f, [](ASoulsKnight* K) { K->Shot(TEXT("10_hit")); K->DoComboAttackStart(); } },
		{ 19.9f, [](ASoulsKnight* K) { K->DoComboAttackStart(); } },
		{ 21.5f, [](ASoulsKnight* K) { K->Shot(TEXT("11_fight")); } },
		{ 23.0f, [](ASoulsKnight* K) { K->Shot(TEXT("12_fight")); } },
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

void ASoulsKnight::SpawnSparringEnemy()
{
	UClass* EnemyClass = LoadClass<APawn>(nullptr, TEXT("/Game/Variant_Combat/Blueprints/AI/BP_CombatEnemy.BP_CombatEnemy_C"));
	if (!EnemyClass)
	{
		return;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	const FVector Location = GetActorLocation() + GetActorForwardVector() * 450.f;
	SparringEnemy = GetWorld()->SpawnActor<APawn>(EnemyClass, Location, (GetActorLocation() - Location).Rotation(), Params);
	if (SparringEnemy)
	{
		SparringEnemy->SpawnDefaultController();
	}
}

void ASoulsKnight::FaceSparringEnemy()
{
	if (SparringEnemy)
	{
		SetActorRotation(FRotator(0.f, (SparringEnemy->GetActorLocation() - GetActorLocation()).Rotation().Yaw, 0.f));
	}
}

void ASoulsKnight::Shot(const TCHAR* Name)
{
	FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("Knight_%s"), Name), true, false);
}

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

	KnightMappingContext = NewObject<UInputMappingContext>(this, TEXT("IMC_Knight"));
	KnightMappingContext->MapKey(DodgeAction, EKeys::Gamepad_FaceButton_Right);
	KnightMappingContext->MapKey(DodgeAction, EKeys::LeftShift);
	KnightMappingContext->MapKey(BlockAction, EKeys::Gamepad_LeftShoulder);
	KnightMappingContext->MapKey(BlockAction, EKeys::RightMouseButton);
	KnightMappingContext->MapKey(HealAction, EKeys::Gamepad_RightShoulder);
	KnightMappingContext->MapKey(HealAction, EKeys::R);
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
	EnsureKnightInput();
	if (const APlayerController* PC = Cast<APlayerController>(GetController()))
	{
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
		Input->BindAction(BlockAction, ETriggerEvent::Started, this, &ASoulsKnight::BlockPressed);
		Input->BindAction(BlockAction, ETriggerEvent::Completed, this, &ASoulsKnight::BlockReleased);
		Input->BindAction(HealAction, ETriggerEvent::Started, this, &ASoulsKnight::HealPressed);
	}
}

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

void ASoulsKnight::DoMove(float Right, float Forward)
{
	if (CurrentHP <= 0.f || (IsBusy() && Action != EKnightAction::Roll))
	{
		return;
	}
	if (Action == EKnightAction::Roll)
	{
		return;
	}
	Super::DoMove(Right, Forward);
}

bool ASoulsKnight::CanJumpInternal_Implementation() const
{
	return !IsBusy() && !bBlocking && Super::CanJumpInternal_Implementation();
}

void ASoulsKnight::PlayAction(EKnightAction NewAction, UAnimMontage* Montage, float Duration)
{
	GetWorldTimerManager().ClearTimer(ActionTimer);
	GetWorldTimerManager().ClearTimer(HitTimer);
	Action = NewAction;
	ActionElapsed = 0.f;
	ActionDuration = Duration;
	bComboQueued = false;

	if (UAnimInstance* Anim = GetMesh()->GetAnimInstance())
	{
		if (Montage)
		{
			const float Length = Montage->GetPlayLength();
			Anim->Montage_Play(Montage, Duration > 0.f ? Length / Duration : 1.f);
		}
	}
	GetWorldTimerManager().SetTimer(ActionTimer, this, &ASoulsKnight::EndAction, FMath::Max(Duration, 0.05f), false);
}

void ASoulsKnight::EndAction()
{
	Action = EKnightAction::None;
	bComboQueued = false;
	if (bBlockHeld)
	{
		SetBlocking(true);
	}
}

void ASoulsKnight::DoComboAttackStart()
{
	if (CurrentHP <= 0.f || SwordComboMontages.IsEmpty())
	{
		return;
	}
	if (Action == EKnightAction::LightAttack)
	{
		bComboQueued = true;
		return;
	}
	if (IsBusy() || GetCharacterMovement()->IsFalling() || !SpendStamina(LightAttackCost))
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
	SetActorRotation(FRotator(0.f, DesiredDirection().Rotation().Yaw, 0.f));
	MeleeDamage = LightDamage;
	const float Duration = Montage ? Montage->GetPlayLength() / 0.8f : 0.6f;
	PlayAction(EKnightAction::LightAttack, Montage, Duration);
	GetWorldTimerManager().SetTimer(HitTimer, this, &ASoulsKnight::SwingHit, Duration * SwingHitFraction, false);
	NotifyEnemiesOfIncomingAttack();
}

void ASoulsKnight::DoChargedAttackStart()
{
	if (CurrentHP <= 0.f || IsBusy() || !HeavyAttackMontage || GetCharacterMovement()->IsFalling() || !SpendStamina(HeavyAttackCost))
	{
		return;
	}
	StartHeavy();
}

void ASoulsKnight::StartHeavy()
{
	SetBlocking(false);
	SetActorRotation(FRotator(0.f, DesiredDirection().Rotation().Yaw, 0.f));
	MeleeDamage = HeavyDamage;
	const float Duration = HeavyAttackMontage->GetPlayLength() / 0.9f;
	PlayAction(EKnightAction::HeavyAttack, HeavyAttackMontage, Duration);
	GetWorldTimerManager().SetTimer(HitTimer, this, &ASoulsKnight::SwingHit, Duration * 0.5f, false);
	NotifyEnemiesOfIncomingAttack();
}

void ASoulsKnight::SwingHit()
{
	DoAttackTrace(SwordSocket);
}

void ASoulsKnight::DodgePressed()
{
	if (CurrentHP <= 0.f || GetCharacterMovement()->IsFalling() || !RollMontage)
	{
		return;
	}
	// Recovery frames of a swing can be rolled out of; the swing itself can't.
	const bool bCancelable = Action == EKnightAction::None
		|| ((Action == EKnightAction::LightAttack || Action == EKnightAction::HeavyAttack) && ActionElapsed > ActionDuration * 0.7f);
	if (!bCancelable || !SpendStamina(RollCost))
	{
		return;
	}
	SetBlocking(false);
	RollDirection = DesiredDirection();
	SetActorRotation(FRotator(0.f, RollDirection.Rotation().Yaw, 0.f));
	PlayAction(EKnightAction::Roll, RollMontage, RollDuration);
}

void ASoulsKnight::BlockPressed()
{
	bBlockHeld = true;
	if (!IsBusy())
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
	if (bBlocking == bNewBlocking || (bNewBlocking && CurrentHP <= 0.f))
	{
		return;
	}
	bBlocking = bNewBlocking;
	GetCharacterMovement()->MaxWalkSpeed = bBlocking ? BlockWalkSpeed : WalkSpeed;
	UAnimInstance* Anim = GetMesh()->GetAnimInstance();
	if (!Anim || !BlockMontage)
	{
		return;
	}
	GetWorldTimerManager().ClearTimer(BlockHoldTimer);
	if (bBlocking)
	{
		Anim->Montage_Play(BlockMontage, 1.f);
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
		}, BlockMontage->GetPlayLength() * 0.35f, false);
	}
	else
	{
		Anim->Montage_Stop(0.2f, BlockMontage);
	}
}

void ASoulsKnight::HealPressed()
{
	if (CurrentHP <= 0.f || IsBusy() || Flasks <= 0 || CurrentHP >= MaxHP || !HealMontage)
	{
		return;
	}
	--Flasks;
	SetBlocking(false);
	const float Duration = HealMontage->GetPlayLength();
	PlayAction(EKnightAction::Heal, HealMontage, Duration);
	GetWorldTimerManager().SetTimer(HealTimer, this, &ASoulsKnight::FinishHeal, Duration * 0.6f, false);
}

void ASoulsKnight::FinishHeal()
{
	if (Action == EKnightAction::Heal)
	{
		ApplyHealing(FlaskHealing, this);
	}
}

float ASoulsKnight::TakeDamage(float Damage, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	if (CurrentHP <= 0.f)
	{
		return 0.f;
	}

	if (Action == EKnightAction::Roll && ActionElapsed >= RollInvulnerableStart && ActionElapsed <= RollInvulnerableEnd)
	{
		return 0.f;
	}

	const bool bFacingAttacker = DamageCauser
		&& FVector::DotProduct(GetActorForwardVector(), (DamageCauser->GetActorLocation() - GetActorLocation()).GetSafeNormal2D()) > 0.2f;
	if (bBlocking && bFacingAttacker)
	{
		Damage *= BlockDamageFraction;
		Stamina -= BlockStaminaPerHit;
		StaminaIdleTime = 0.f;
		if (Stamina <= 0.f)
		{
			// Guard broken: the shield drops and the knight reels.
			SetBlocking(false);
			PlayAction(EKnightAction::Staggered, HitMontage, 0.8f);
		}
		return Super::TakeDamage(Damage, DamageEvent, EventInstigator, DamageCauser);
	}

	GetWorldTimerManager().ClearTimer(HealTimer);
	SetBlocking(false);
	PlayAction(EKnightAction::Staggered, nullptr, HitStunSeconds);
	return Super::TakeDamage(Damage, DamageEvent, EventInstigator, DamageCauser);
}

void ASoulsKnight::HandleDeath()
{
	GetWorldTimerManager().ClearTimer(ActionTimer);
	GetWorldTimerManager().ClearTimer(HitTimer);
	GetWorldTimerManager().ClearTimer(HealTimer);
	GetWorldTimerManager().ClearTimer(BlockHoldTimer);
	Action = EKnightAction::None;
	bBlocking = false;
	Super::HandleDeath();
}

void ASoulsKnight::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	ActionElapsed += DeltaSeconds;

	if (bVerificationDemo && Controller && !bDemoCameraSet)
	{
		Controller->SetControlRotation(FRotator(-10.f, GetActorRotation().Yaw + 110.f, 0.f));
		bDemoCameraSet = true;
	}

	if (Action == EKnightAction::LightAttack && bComboQueued && ActionElapsed >= ActionDuration * ComboChainFraction
		&& SwingIndex + 1 < SwordComboMontages.Num() && SpendStamina(LightAttackCost))
	{
		StartSwing(SwingIndex + 1);
	}

	if (Action == EKnightAction::Roll)
	{
		const float T = FMath::Clamp(ActionElapsed / FMath::Max(ActionDuration, 0.01f), 0.f, 1.f);
		const float Speed = RollSpeed * (1.f - 0.7f * T * T);
		UCharacterMovementComponent* Movement = GetCharacterMovement();
		Movement->Velocity = FVector(RollDirection.X * Speed, RollDirection.Y * Speed, Movement->Velocity.Z);
	}

	if (!IsBusy())
	{
		StaminaIdleTime += DeltaSeconds;
		if (StaminaIdleTime >= StaminaRegenDelay)
		{
			const float Rate = bBlocking ? StaminaRegenPerSecond * 0.3f : StaminaRegenPerSecond;
			Stamina = FMath::Min(MaxStamina, Stamina + Rate * DeltaSeconds);
		}
	}
}
