#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "SoulslikeTypes.h"
#include "SoulslikeCombatant.generated.h"

class UCombatComponent;
class UVitalityComponent;
class UAnimSequence;
class UStaticMeshComponent;
struct FSoulslikeVisual;

// Everything a fighter on the island shares, player or skeleton: combat + vitality components,
// the imported KayKit mesh with its weapon props, state-driven animation (single-node playback,
// no Anim Blueprint needed), damage routing, death and drowning.
UCLASS(Abstract)
class UNREALSOULSLIKE_API ASoulslikeCombatant : public ACharacter
{
	GENERATED_BODY()

public:
	ASoulslikeCombatant();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Soulslike")
	TObjectPtr<UCombatComponent> Combat;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Soulslike")
	TObjectPtr<UVitalityComponent> Vitality;

	// Shown only when the imported art is missing, so the demo still plays without it.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Soulslike")
	TObjectPtr<UStaticMeshComponent> PlaceholderBody;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Soulslike")
	FText DisplayName;

	// The imported mesh is scaled so its bounds are this tall, whatever units it came in.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Soulslike|Visual")
	float TargetMeshHeight = 185.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Soulslike|Visual")
	float MeshYawOffset = -90.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Soulslike|Movement")
	float RunSpeed = 520.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Soulslike|Movement")
	float BlockingSpeed = 220.f;

	UFUNCTION(BlueprintPure, Category = "Soulslike")
	bool IsAlive() const;

	UFUNCTION(BlueprintPure, Category = "Soulslike")
	uint8 GetTeam() const;

	// True while this combatant can't be seen or heard at all (e.g. merged into a wall).
	virtual bool IsConcealed() const { return false; }

	virtual void Tick(float DeltaSeconds) override;
	virtual float TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

protected:
	virtual void BeginPlay() override;

	// Swap in the imported skeletal mesh + props; safe to call on every machine.
	void ApplyVisual(const FSoulslikeVisual& Visual);

	UFUNCTION()
	void HandleDeath();

	// Server-side hook after death bookkeeping (players respawn, skeletons despawn).
	virtual void OnDied() {}

	// Rotate the actor to face Target over time (server for AI, owning client for players).
	void FaceTowards(const FVector& Target, float DeltaSeconds, float DegreesPerSecond);

	// Lets a subclass take over animation (climbing, merged); NAME_None = use combat state.
	virtual FName GetAnimOverride(bool& bOutLoop, float& OutDuration) const { return NAME_None; }

private:
	void UpdateAnimation();
	UAnimSequence* FindAnim(FName Key) const;
	FName ChooseAnim(bool& bOutLoop, float& OutDuration) const;

	UPROPERTY(Transient)
	TMap<FName, TObjectPtr<UAnimSequence>> Anims;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> AttachedProps;

	FName CurrentAnim;
	uint8 CurrentSerial = 255;
	bool bHasSkeletalMesh = false;
	bool bDeathHandled = false;
	float SubmergedTime = 0.f;
};
