#pragma once

#include "CoreMinimal.h"
#include "SoulsKnight.h"
#include "SoulsRanger.generated.h"

class UPointLightComponent;

// A gun-wielding alternative to the knight's sword (UNR-T85, the character roster): the exact same
// mesh, movement, dodge, block, parry, stamina and climbing as ASoulsKnight, but the attack entry
// points fire a hitscan weapon instead of swinging a blade. Reuses the knight's existing Sword
// socket (hand_r) for the gun prop and the knight's own Shield slot stays empty.
UCLASS()
class UNREALSOULSLIKE_API ASoulsRanger : public ASoulsKnight
{
	GENERATED_BODY()

public:
	ASoulsRanger();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ranger|Weapon")
	float FireCooldown = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ranger|Weapon")
	float LightFireDamage = 12.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ranger|Weapon")
	float HeavyFireDamage = 24.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ranger|Weapon")
	float WeaponRange = 8000.f;

	virtual void DoComboAttackStart() override;
	virtual void DoChargedAttackStart() override;
	virtual void OnBecomeActive() override;
	virtual void OnBecomeInactive() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	// Aims from the camera (so screen-center aiming feels right) but fires visually from the muzzle.
	FVector AimPoint() const;
	FVector MuzzleLocation() const;
	void Fire(float Damage);

	UPROPERTY(Transient)
	TObjectPtr<UPointLightComponent> MuzzleLight;

	float NextFireTime = 0.f;
	float MuzzleFlashT = 0.f;
};
