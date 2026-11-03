#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "SoulslikeCharacter.generated.h"

class USpringArmComponent;
class UCameraComponent;
class UCombatComponent;
class UVitalityComponent;
class UInputMappingContext;
class UInputAction;
struct FInputActionValue;

UCLASS()
class UNREALSOULSLIKE_API ASoulslikeCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	ASoulslikeCharacter();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Soulslike|Camera")
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Soulslike|Camera")
	TObjectPtr<UCameraComponent> FollowCamera;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Soulslike|Combat")
	TObjectPtr<UCombatComponent> Combat;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Soulslike|Combat")
	TObjectPtr<UVitalityComponent> Vitality;

	// Assigned on a Blueprint child (or these class defaults) in the editor: Enhanced Input
	// assets aren't text-editable config, so the demo ships the gameplay code and documents
	// the intended gamepad scheme in docs/DESIGN.md instead of guessing binary .uasset bytes.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Soulslike|Input")
	TObjectPtr<UInputMappingContext> DefaultMappingContext;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Soulslike|Input")
	TObjectPtr<UInputAction> MoveAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Soulslike|Input")
	TObjectPtr<UInputAction> LookAction;

	// Default scheme: A = Dodge Roll
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Soulslike|Input")
	TObjectPtr<UInputAction> DodgeAction;

	// Default scheme: LB (hold) = Block
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Soulslike|Input")
	TObjectPtr<UInputAction> BlockAction;

	// Default scheme: LT (tap while blocking) = Parry
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Soulslike|Input")
	TObjectPtr<UInputAction> ParryAction;

	// Default scheme: X = Light Attack
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Soulslike|Input")
	TObjectPtr<UInputAction> LightAttackAction;

	// Default scheme: Y = Heavy Attack
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Soulslike|Input")
	TObjectPtr<UInputAction> HeavyAttackAction;

	// Default scheme: RB = Heal
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Soulslike|Input")
	TObjectPtr<UInputAction> HealAction;

	virtual float TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

protected:
	virtual void BeginPlay() override;
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

	void OnMove(const FInputActionValue& Value);
	void OnLook(const FInputActionValue& Value);
	void OnDodge(const FInputActionValue& Value);
	void OnBlockStarted(const FInputActionValue& Value);
	void OnBlockCompleted(const FInputActionValue& Value);
	void OnParry(const FInputActionValue& Value);
	void OnLightAttack(const FInputActionValue& Value);
	void OnHeavyAttack(const FInputActionValue& Value);
	void OnHeal(const FInputActionValue& Value);

private:
	FVector2D LastMoveInput;
};
