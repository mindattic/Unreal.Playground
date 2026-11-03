#include "SoulslikeCharacter.h"
#include "Components/CombatComponent.h"
#include "Components/VitalityComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputActionValue.h"

ASoulslikeCharacter::ASoulslikeCharacter()
{
	bUseControllerRotationYaw = false;
	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator(0.f, 500.f, 0.f);

	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = 400.f;
	CameraBoom->bUsePawnControlRotation = true;

	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;

	Combat = CreateDefaultSubobject<UCombatComponent>(TEXT("Combat"));
	Vitality = CreateDefaultSubobject<UVitalityComponent>(TEXT("Vitality"));
}

void ASoulslikeCharacter::BeginPlay()
{
	Super::BeginPlay();

	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
		{
			if (DefaultMappingContext)
			{
				Subsystem->AddMappingContext(DefaultMappingContext, 0);
			}
		}
	}
}

void ASoulslikeCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!EnhancedInput)
	{
		return;
	}

	if (MoveAction) EnhancedInput->BindAction(MoveAction, ETriggerEvent::Triggered, this, &ASoulslikeCharacter::OnMove);
	if (LookAction) EnhancedInput->BindAction(LookAction, ETriggerEvent::Triggered, this, &ASoulslikeCharacter::OnLook);
	if (DodgeAction) EnhancedInput->BindAction(DodgeAction, ETriggerEvent::Started, this, &ASoulslikeCharacter::OnDodge);
	if (BlockAction)
	{
		EnhancedInput->BindAction(BlockAction, ETriggerEvent::Started, this, &ASoulslikeCharacter::OnBlockStarted);
		EnhancedInput->BindAction(BlockAction, ETriggerEvent::Completed, this, &ASoulslikeCharacter::OnBlockCompleted);
	}
	if (ParryAction) EnhancedInput->BindAction(ParryAction, ETriggerEvent::Started, this, &ASoulslikeCharacter::OnParry);
	if (LightAttackAction) EnhancedInput->BindAction(LightAttackAction, ETriggerEvent::Started, this, &ASoulslikeCharacter::OnLightAttack);
	if (HeavyAttackAction) EnhancedInput->BindAction(HeavyAttackAction, ETriggerEvent::Started, this, &ASoulslikeCharacter::OnHeavyAttack);
	if (HealAction) EnhancedInput->BindAction(HealAction, ETriggerEvent::Started, this, &ASoulslikeCharacter::OnHeal);
}

void ASoulslikeCharacter::OnMove(const FInputActionValue& Value)
{
	if (!Combat || !Combat->CanAct())
	{
		return;
	}

	const FVector2D Input = Value.Get<FVector2D>();
	LastMoveInput = Input;

	if (Controller)
	{
		const FRotator YawRotation(0.f, Controller->GetControlRotation().Yaw, 0.f);
		const FVector Forward = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
		const FVector Right = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);
		AddMovementInput(Forward, Input.Y);
		AddMovementInput(Right, Input.X);
	}
}

void ASoulslikeCharacter::OnLook(const FInputActionValue& Value)
{
	const FVector2D Input = Value.Get<FVector2D>();
	AddControllerYawInput(Input.X);
	AddControllerPitchInput(Input.Y);
}

void ASoulslikeCharacter::OnDodge(const FInputActionValue& Value)
{
	if (!Combat)
	{
		return;
	}

	Combat->TryDodge(LastMoveInput);

	if (IsLocallyControlled())
	{
		FVector LaunchDirection = GetActorForwardVector();
		if (!LastMoveInput.IsNearlyZero() && Controller)
		{
			const FRotator YawRotation(0.f, Controller->GetControlRotation().Yaw, 0.f);
			const FVector Forward = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
			const FVector Right = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);
			LaunchDirection = (Forward * LastMoveInput.Y + Right * LastMoveInput.X).GetSafeNormal();
		}
		LaunchCharacter(LaunchDirection * 900.f, true, false);
	}
}

void ASoulslikeCharacter::OnBlockStarted(const FInputActionValue& Value)
{
	if (Combat)
	{
		Combat->StartBlocking();
	}
}

void ASoulslikeCharacter::OnBlockCompleted(const FInputActionValue& Value)
{
	if (Combat)
	{
		Combat->StopBlocking();
	}
}

void ASoulslikeCharacter::OnParry(const FInputActionValue& Value)
{
	if (Combat)
	{
		Combat->TryParry();
	}
}

void ASoulslikeCharacter::OnLightAttack(const FInputActionValue& Value)
{
	if (Combat)
	{
		Combat->TryLightAttack();
	}
}

void ASoulslikeCharacter::OnHeavyAttack(const FInputActionValue& Value)
{
	if (Combat)
	{
		Combat->TryHeavyAttack();
	}
}

void ASoulslikeCharacter::OnHeal(const FInputActionValue& Value)
{
	if (Combat)
	{
		Combat->TryHeal();
	}
}

float ASoulslikeCharacter::TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	const float Applied = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);

	if (HasAuthority() && Vitality && Combat)
	{
		const float Resolved = Combat->ResolveIncomingDamage(Applied, DamageCauser);
		if (Resolved > 0.f)
		{
			Vitality->ApplyDamage(Resolved, DamageCauser);
			Combat->InterruptOnHit();
		}
	}

	return Applied;
}
