#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SoulsCharacterRoster.generated.h"

class UInputMappingContext;
class UInputAction;
class UInputComponent;

// UNR-T85: an extensible roster of playable characters (the melee knight, the gun-wielding Ranger,
// and whatever joins them later) the player cycles through with . (next) and , (previous). Every
// roster member implements ISoulsPlayableCharacter, so this component never needs to know what kind
// of pawn it's switching to - it only hides the outgoing one, spawns-or-reuses the incoming one
// (parked, hidden, state intact, the same "hide, don't destroy" idea ASoulsMech::Board() already
// uses on the knight), and swaps Possess()/view target.
UCLASS(ClassGroup = (Soulslike), meta = (BlueprintSpawnableComponent))
class UNREALSOULSLIKE_API USoulsCharacterRosterComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USoulsCharacterRosterComponent();

	// Index 0 is adopted from whatever pawn the controller already possesses at BeginPlay (the
	// knight) rather than re-spawned. Append more classes here (or in a Blueprint child) as new
	// playable characters are built - nothing else in this component needs to change.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Roster")
	TArray<TSubclassOf<APawn>> RosterClasses;

	// Called by the owning controller's own SetupInputComponent() override, since this lives on a
	// component rather than a pawn and so has no SetupPlayerInputComponent of its own to hook.
	void BindInput(UInputComponent* PlayerInputComponent);

	UFUNCTION(BlueprintCallable, Category = "Roster")
	void CycleNext();

	UFUNCTION(BlueprintCallable, Category = "Roster")
	void CyclePrevious();

protected:
	virtual void BeginPlay() override;

private:
	// Idempotent: creates the mapping context/actions once, however it's first reached from (mirrors
	// ASoulsKnight::EnsureKnightInput()'s guard, since BeginPlay() and BindInput() can race).
	void EnsureRosterInput();
	void SwitchTo(int32 Index);
	APawn* EnsureSpawned(int32 Index, const FTransform& At);

	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> RosterMappingContext;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> NextAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> PreviousAction;

	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<APawn>> Instances;

	int32 CurrentIndex = 0;
};
