#include "SoulslikeCharacter.h"
#include "SoulslikeData.h"
#include "SoulslikeGameMode.h"
#include "SkeletonEnemy.h"
#include "TimerManager.h"
#include "Components/CombatComponent.h"
#include "Components/VitalityComponent.h"
#include "Components/ClimbComponent.h"
#include "Components/ShadowMergeComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "InputActionValue.h"
#include "EngineUtils.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Controller.h"
#include "Engine/LocalPlayer.h"

ASoulslikeCharacter::ASoulslikeCharacter()
{
	DisplayName = NSLOCTEXT("Soulslike", "Knight", "Knight");

	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = 380.f;
	CameraBoom->SocketOffset = FVector(0.f, 0.f, 60.f);
	CameraBoom->bUsePawnControlRotation = true;
	CameraBoom->bEnableCameraLag = true;
	CameraBoom->CameraLagSpeed = 12.f;

	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;

	Climb = CreateDefaultSubobject<UClimbComponent>(TEXT("Climb"));
	ShadowMerge = CreateDefaultSubobject<UShadowMergeComponent>(TEXT("ShadowMerge"));

	GetCharacterMovement()->JumpZVelocity = 560.f;
	GetCharacterMovement()->AirControl = 0.35f;

	Combat->Team = SoulslikeTeam::Players;
	MouseSensitivity = 2.5f;
}

bool ASoulslikeCharacter::IsOnFoot() const
{
	return !Climb->IsClimbing() && !ShadowMerge->IsMerged();
}

bool ASoulslikeCharacter::IsConcealed() const
{
	return ShadowMerge && ShadowMerge->IsMerged();
}

FName ASoulslikeCharacter::GetAnimOverride(bool& bOutLoop, float& OutDuration) const
{
	return Climb && Climb->IsClimbing() ? Climb->GetAnimOverride(bOutLoop, OutDuration) : NAME_None;
}

void ASoulslikeCharacter::OnJump(const FInputActionValue& Value)
{
	if (IsAlive() && !ShadowMerge->IsMerged() && (Combat->CanAct() || Climb->IsClimbing()))
	{
		Climb->HandleJumpPressed();
	}
}

void ASoulslikeCharacter::OnMerge(const FInputActionValue& Value)
{
	if (IsAlive() && (Combat->CanAct() || ShadowMerge->IsMerged()) && !Climb->IsClimbing())
	{
		ShadowMerge->ToggleMerge();
	}
}

void ASoulslikeCharacter::BeginPlay()
{
	Super::BeginPlay();
	ApplyVisual(FSoulslikeAssets::Get().Knight);
	bDemoBot = FParse::Param(FCommandLine::Get(), TEXT("DemoBot"));
}

void ASoulslikeCharacter::EnsureDefaultInput()
{
	if (DefaultMappingContext)
	{
		return;
	}

	auto MakeAction = [this](const TCHAR* Name, EInputActionValueType Type)
	{
		UInputAction* Action = NewObject<UInputAction>(this, Name);
		Action->ValueType = Type;
		return Action;
	};

	MoveAction = MakeAction(TEXT("IA_Move"), EInputActionValueType::Axis2D);
	LookMouseAction = MakeAction(TEXT("IA_LookMouse"), EInputActionValueType::Axis2D);
	LookStickAction = MakeAction(TEXT("IA_LookStick"), EInputActionValueType::Axis2D);
	DodgeAction = MakeAction(TEXT("IA_Dodge"), EInputActionValueType::Boolean);
	BlockAction = MakeAction(TEXT("IA_Block"), EInputActionValueType::Boolean);
	ParryAction = MakeAction(TEXT("IA_Parry"), EInputActionValueType::Boolean);
	LightAttackAction = MakeAction(TEXT("IA_LightAttack"), EInputActionValueType::Boolean);
	HeavyAttackAction = MakeAction(TEXT("IA_HeavyAttack"), EInputActionValueType::Boolean);
	HealAction = MakeAction(TEXT("IA_Heal"), EInputActionValueType::Boolean);
	JumpAction = MakeAction(TEXT("IA_Jump"), EInputActionValueType::Boolean);
	MergeAction = MakeAction(TEXT("IA_Merge"), EInputActionValueType::Boolean);

	UInputMappingContext* Context = NewObject<UInputMappingContext>(this, TEXT("IMC_Soulslike"));

	Context->MapKey(MoveAction, EKeys::Gamepad_Left2D).Modifiers.Add(NewObject<UInputModifierDeadZone>(this));
	Context->MapKey(MoveAction, EKeys::D);
	Context->MapKey(MoveAction, EKeys::A).Modifiers.Add(NewObject<UInputModifierNegate>(this));
	Context->MapKey(MoveAction, EKeys::W).Modifiers.Add(NewObject<UInputModifierSwizzleAxis>(this));
	{
		FEnhancedActionKeyMapping& Back = Context->MapKey(MoveAction, EKeys::S);
		Back.Modifiers.Add(NewObject<UInputModifierSwizzleAxis>(this));
		Back.Modifiers.Add(NewObject<UInputModifierNegate>(this));
	}

	Context->MapKey(LookMouseAction, EKeys::Mouse2D);
	Context->MapKey(LookStickAction, EKeys::Gamepad_Right2D).Modifiers.Add(NewObject<UInputModifierDeadZone>(this));

	Context->MapKey(JumpAction, EKeys::Gamepad_FaceButton_Bottom);
	Context->MapKey(JumpAction, EKeys::SpaceBar);
	Context->MapKey(DodgeAction, EKeys::Gamepad_FaceButton_Right);
	Context->MapKey(DodgeAction, EKeys::LeftShift);
	Context->MapKey(MergeAction, EKeys::Gamepad_RightThumbstick);
	Context->MapKey(MergeAction, EKeys::E);
	Context->MapKey(BlockAction, EKeys::Gamepad_LeftShoulder);
	Context->MapKey(BlockAction, EKeys::RightMouseButton);
	Context->MapKey(ParryAction, EKeys::Gamepad_LeftTrigger);
	Context->MapKey(ParryAction, EKeys::Q);
	Context->MapKey(LightAttackAction, EKeys::Gamepad_FaceButton_Left);
	Context->MapKey(LightAttackAction, EKeys::LeftMouseButton);
	Context->MapKey(HeavyAttackAction, EKeys::Gamepad_FaceButton_Top);
	Context->MapKey(HeavyAttackAction, EKeys::F);
	Context->MapKey(HealAction, EKeys::Gamepad_RightShoulder);
	Context->MapKey(HealAction, EKeys::R);

	DefaultMappingContext = Context;
}

void ASoulslikeCharacter::PawnClientRestart()
{
	EnsureDefaultInput();
	Super::PawnClientRestart();

	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
		{
			Subsystem->ClearAllMappings();
			Subsystem->AddMappingContext(DefaultMappingContext, 0);
		}
	}
}

void ASoulslikeCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	EnsureDefaultInput();

	UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!Input)
	{
		return;
	}

	Input->BindAction(MoveAction, ETriggerEvent::Triggered, this, &ASoulslikeCharacter::OnMove);
	Input->BindAction(MoveAction, ETriggerEvent::Completed, this, &ASoulslikeCharacter::OnMove);
	Input->BindAction(LookMouseAction, ETriggerEvent::Triggered, this, &ASoulslikeCharacter::OnLookMouse);
	Input->BindAction(LookStickAction, ETriggerEvent::Triggered, this, &ASoulslikeCharacter::OnLookStick);
	Input->BindAction(JumpAction, ETriggerEvent::Started, this, &ASoulslikeCharacter::OnJump);
	Input->BindAction(MergeAction, ETriggerEvent::Started, this, &ASoulslikeCharacter::OnMerge);
	Input->BindAction(DodgeAction, ETriggerEvent::Started, this, &ASoulslikeCharacter::OnDodge);
	Input->BindAction(BlockAction, ETriggerEvent::Started, this, &ASoulslikeCharacter::OnBlockStarted);
	Input->BindAction(BlockAction, ETriggerEvent::Completed, this, &ASoulslikeCharacter::OnBlockCompleted);
	Input->BindAction(ParryAction, ETriggerEvent::Started, this, &ASoulslikeCharacter::OnParry);
	Input->BindAction(LightAttackAction, ETriggerEvent::Started, this, &ASoulslikeCharacter::OnLightAttack);
	Input->BindAction(HeavyAttackAction, ETriggerEvent::Started, this, &ASoulslikeCharacter::OnHeavyAttack);
	Input->BindAction(HealAction, ETriggerEvent::Started, this, &ASoulslikeCharacter::OnHeal);
}

void ASoulslikeCharacter::OnMove(const FInputActionValue& Value)
{
	LastMoveInput = Value.Get<FVector2D>();
	if (!IsAlive())
	{
		return;
	}
	if (ShadowMerge->IsMerged())
	{
		ShadowMerge->AddMergeInput(LastMoveInput);
	}
	else if (Climb->IsClimbing())
	{
		Climb->AddClimbInput(LastMoveInput);
	}
	else if (Combat->CanAct())
	{
		MoveInCameraSpace(LastMoveInput);
	}
}

void ASoulslikeCharacter::MoveInCameraSpace(const FVector2D& Input)
{
	if (!Controller)
	{
		return;
	}
	const FRotator Yaw(0.f, Controller->GetControlRotation().Yaw, 0.f);
	AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::X), Input.Y);
	AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y), Input.X);
}

void ASoulslikeCharacter::AddLook(float YawDegrees, float PitchDegrees)
{
	if (!Controller)
	{
		return;
	}
	FRotator Rotation = Controller->GetControlRotation();
	Rotation.Yaw += YawDegrees;
	Rotation.Pitch = FMath::Clamp(FRotator::NormalizeAxis(Rotation.Pitch + PitchDegrees), -60.f, 35.f);
	Controller->SetControlRotation(Rotation);
}

void ASoulslikeCharacter::OnLookMouse(const FInputActionValue& Value)
{
	const FVector2D Input = Value.Get<FVector2D>();
	AddLook(Input.X * MouseSensitivity, Input.Y * MouseSensitivity);
}

void ASoulslikeCharacter::OnLookStick(const FInputActionValue& Value)
{
	const FVector2D Input = Value.Get<FVector2D>();
	const float Step = StickDegreesPerSecond * GetWorld()->GetDeltaSeconds();
	AddLook(Input.X * Step, Input.Y * Step);
}

void ASoulslikeCharacter::OnDodge(const FInputActionValue& Value)
{
	if (Climb->IsClimbing())
	{
		Climb->Release();
		return;
	}
	DodgeLocal();
}

void ASoulslikeCharacter::DodgeLocal()
{
	if (!IsAlive() || !IsOnFoot() || !Combat->CanAct() || !Vitality->HasStamina(Combat->DodgeStaminaCost))
	{
		return;
	}
	FVector Direction = GetActorForwardVector();
	if (!LastMoveInput.IsNearlyZero() && Controller)
	{
		const FRotator Yaw(0.f, Controller->GetControlRotation().Yaw, 0.f);
		Direction = (FRotationMatrix(Yaw).GetUnitAxis(EAxis::X) * LastMoveInput.Y + FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y) * LastMoveInput.X).GetSafeNormal();
	}
	SetActorRotation(FRotator(0.f, Direction.Rotation().Yaw, 0.f));
	LaunchCharacter(Direction * Combat->DodgeLaunchSpeed, true, false);
	Combat->TryDodge(Direction);
}

void ASoulslikeCharacter::OnBlockStarted(const FInputActionValue& Value)
{
	if (IsOnFoot())
	{
		Combat->StartBlocking();
	}
}

void ASoulslikeCharacter::OnBlockCompleted(const FInputActionValue& Value)
{
	Combat->StopBlocking();
}

void ASoulslikeCharacter::OnParry(const FInputActionValue& Value)
{
	if (IsOnFoot())
	{
		Combat->TryParry();
	}
}

void ASoulslikeCharacter::OnLightAttack(const FInputActionValue& Value)
{
	LightAttackOrExecute();
}

void ASoulslikeCharacter::LightAttackOrExecute()
{
	if (!IsAlive() || !Combat->CanAct() || Climb->IsClimbing())
	{
		return;
	}
	ASkeletonEnemy* Victim = FindExecutionTarget();
	if (ShadowMerge->IsMerged())
	{
		// Shadow strike: only an execution can pull you out of the wall mid-attack.
		if (!Victim)
		{
			return;
		}
		ShadowMerge->ToggleMerge();
	}
	if (Victim)
	{
		SetActorRotation(FRotator(0.f, (Victim->GetActorLocation() - GetActorLocation()).Rotation().Yaw, 0.f));
		ServerExecute(Victim);
		return;
	}
	SoftLockTowardsEnemy();
	Combat->TryLightAttack();
}

ASkeletonEnemy* ASoulslikeCharacter::FindExecutionTarget() const
{
	ASkeletonEnemy* Best = nullptr;
	float BestDistSq = FMath::Square(ExecutionRange);
	for (TActorIterator<ASkeletonEnemy> It(GetWorld()); It; ++It)
	{
		ASkeletonEnemy* Skeleton = *It;
		const FVector ToSkeleton = Skeleton->GetActorLocation() - GetActorLocation();
		const float DistSq = ToSkeleton.SizeSquared2D();
		if (DistSq < BestDistSq && FMath::Abs(ToSkeleton.Z) < 150.f && Skeleton->CanBeExecutedBy(this))
		{
			BestDistSq = DistSq;
			Best = Skeleton;
		}
	}
	return Best;
}

void ASoulslikeCharacter::ServerExecute_Implementation(ASkeletonEnemy* Target)
{
	if (!Target || !IsAlive() || !Combat->CanAct() || !Target->CanBeExecutedBy(this)
		|| FVector::Dist2D(Target->GetActorLocation(), GetActorLocation()) > ExecutionRange * 1.5f)
	{
		return;
	}

	SetActorRotation(FRotator(0.f, (Target->GetActorLocation() - GetActorLocation()).Rotation().Yaw, 0.f));
	Combat->StartExecution();
	Target->Combat->ApplyStagger(Combat->ExecutionDuration);

	FTimerHandle Handle;
	const TWeakObjectPtr<ASkeletonEnemy> WeakTarget(Target);
	const TWeakObjectPtr<ASoulslikeCharacter> WeakSelf(this);
	GetWorldTimerManager().SetTimer(Handle, [WeakTarget, WeakSelf]()
	{
		if (WeakTarget.IsValid())
		{
			WeakTarget->ReceiveExecution(WeakSelf.Get());
		}
	}, 0.45f, false);
}

void ASoulslikeCharacter::OnHeavyAttack(const FInputActionValue& Value)
{
	if (!IsOnFoot())
	{
		return;
	}
	if (IsAlive() && Combat->CanAct())
	{
		SoftLockTowardsEnemy();
	}
	Combat->TryHeavyAttack();
}

void ASoulslikeCharacter::OnHeal(const FInputActionValue& Value)
{
	if (IsOnFoot())
	{
		Combat->TryHeal();
	}
}

ASoulslikeCombatant* ASoulslikeCharacter::FindNearestEnemy(float MaxRange) const
{
	ASoulslikeCombatant* Best = nullptr;
	float BestDistSq = FMath::Square(MaxRange);
	for (TActorIterator<ASoulslikeCombatant> It(GetWorld()); It; ++It)
	{
		ASoulslikeCombatant* Other = *It;
		if (Other == this || Other->GetTeam() == GetTeam() || !Other->IsAlive())
		{
			continue;
		}
		const float DistSq = FVector::DistSquared(Other->GetActorLocation(), GetActorLocation());
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			Best = Other;
		}
	}
	return Best;
}

void ASoulslikeCharacter::SoftLockTowardsEnemy()
{
	if (const ASoulslikeCombatant* Enemy = FindNearestEnemy(SoftLockRange))
	{
		const FVector ToEnemy = Enemy->GetActorLocation() - GetActorLocation();
		SetActorRotation(FRotator(0.f, ToEnemy.Rotation().Yaw, 0.f));
	}
}

void ASoulslikeCharacter::OnDied()
{
	if (ASoulslikeGameMode* GameMode = GetWorld()->GetAuthGameMode<ASoulslikeGameMode>())
	{
		GameMode->NotifyPlayerDied(GetController());
	}
}

void ASoulslikeCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bDemoBot && IsLocallyControlled())
	{
		TickDemoBot(DeltaSeconds);
	}
}

// -DemoBot: plays the character by itself (seek, fight, heal) so a run can be verified unattended.
void ASoulslikeCharacter::TickDemoBot(float DeltaSeconds)
{
	DemoBotCooldown -= DeltaSeconds;
	if (!IsAlive() || !Controller)
	{
		return;
	}

	const ASoulslikeCombatant* Target = FindNearestEnemy(20000.f);
	if (!Target)
	{
		return;
	}

	const FVector ToTarget = Target->GetActorLocation() - GetActorLocation();
	Controller->SetControlRotation(FRotator(-18.f, ToTarget.Rotation().Yaw - 25.f, 0.f));

	if (!Combat->CanAct())
	{
		return;
	}

	if (Vitality->Health < Vitality->MaxHealth * 0.4f && Vitality->HasHealCharge() && DemoBotCooldown <= 0.f)
	{
		Combat->TryHeal();
		DemoBotCooldown = 2.f;
		return;
	}

	if (ToTarget.Size2D() > 200.f)
	{
		AddMovementInput(ToTarget.GetSafeNormal2D(), 1.f);
		return;
	}

	if (DemoBotCooldown <= 0.f)
	{
		SoftLockTowardsEnemy();
		const float Roll = FMath::FRand();
		if (Roll < 0.6f || FindExecutionTarget())
		{
			LightAttackOrExecute();
		}
		else if (Roll < 0.8f)
		{
			Combat->TryHeavyAttack();
		}
		else
		{
			LastMoveInput = FVector2D(1.f, -1.f).GetSafeNormal();
			DodgeLocal();
		}
		DemoBotCooldown = FMath::FRandRange(0.7f, 1.1f);
	}
}
