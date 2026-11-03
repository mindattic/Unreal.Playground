#pragma once

#include "CoreMinimal.h"
#include "SoulslikeCombatant.h"
#include "SoulslikeCharacter.generated.h"

class USpringArmComponent;
class UCameraComponent;
class UInputMappingContext;
class UInputAction;
struct FInputActionValue;

// The player's knight. Input is built at runtime (no editor data assets needed):
//   Gamepad: L-stick move, R-stick look, A dodge, LB block, LT parry, X light, Y heavy, RB heal
//   Keyboard/mouse: WASD, mouse, Space dodge, RMB block, Q parry, LMB light, F heavy, R heal
// Assigning DefaultMappingContext and the actions on a Blueprint child overrides all of it.
UCLASS()
class UNREALSOULSLIKE_API ASoulslikeCharacter : public ASoulslikeCombatant
{
	GENERATED_BODY()

public:
	ASoulslikeCharacter();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Soulslike|Camera")
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Soulslike|Camera")
	TObjectPtr<UCameraComponent> FollowCamera;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Soulslike|Input")
	TObjectPtr<UInputMappingContext> DefaultMappingContext;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Soulslike|Input")
	TObjectPtr<UInputAction> MoveAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Soulslike|Input")
	TObjectPtr<UInputAction> LookMouseAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Soulslike|Input")
	TObjectPtr<UInputAction> LookStickAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Soulslike|Input")
	TObjectPtr<UInputAction> DodgeAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Soulslike|Input")
	TObjectPtr<UInputAction> BlockAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Soulslike|Input")
	TObjectPtr<UInputAction> ParryAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Soulslike|Input")
	TObjectPtr<UInputAction> LightAttackAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Soulslike|Input")
	TObjectPtr<UInputAction> HeavyAttackAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Soulslike|Input")
	TObjectPtr<UInputAction> HealAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Soulslike|Input")
	float MouseSensitivity = 0.12f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Soulslike|Input")
	float StickDegreesPerSecond = 160.f;

	// Attacks snap toward the nearest enemy within this range (a light "soft lock").
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Soulslike|Combat")
	float SoftLockRange = 450.f;

	// Stealth executions reach this far.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Soulslike|Combat")
	float ExecutionRange = 220.f;

	// The skeleton a light-attack press would execute right now, if any (HUD prompt uses it).
	class ASkeletonEnemy* FindExecutionTarget() const;

	virtual void Tick(float DeltaSeconds) override;
	virtual void PawnClientRestart() override;

protected:
	virtual void BeginPlay() override;
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;
	virtual void OnDied() override;

	void OnMove(const FInputActionValue& Value);
	void OnLookMouse(const FInputActionValue& Value);
	void OnLookStick(const FInputActionValue& Value);
	void OnDodge(const FInputActionValue& Value);
	void OnBlockStarted(const FInputActionValue& Value);
	void OnBlockCompleted(const FInputActionValue& Value);
	void OnParry(const FInputActionValue& Value);
	void OnLightAttack(const FInputActionValue& Value);
	void OnHeavyAttack(const FInputActionValue& Value);
	void OnHeal(const FInputActionValue& Value);

	UFUNCTION(Server, Reliable)
	void ServerExecute(class ASkeletonEnemy* Target);

	// Light attack: an execution when one is available, else a normal swing.
	void LightAttackOrExecute();

private:
	void EnsureDefaultInput();
	void AddLook(float YawDegrees, float PitchDegrees);
	void MoveInCameraSpace(const FVector2D& Input);
	void DodgeLocal();
	void SoftLockTowardsEnemy();
	class ASoulslikeCombatant* FindNearestEnemy(float MaxRange) const;
	void TickDemoBot(float DeltaSeconds);

	FVector2D LastMoveInput = FVector2D::ZeroVector;
	bool bDemoBot = false;
	float DemoBotCooldown = 0.f;
};
