#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ClimbComponent.generated.h"

// Breath-of-the-Wild-style free climbing for the player: any steep surface can be climbed,
// draining stamina; jump while climbing is a climb-jump; reaching a ledge mantles up; running
// out of stamina lets go.
UCLASS(ClassGroup = (Soulslike), meta = (BlueprintSpawnableComponent))
class UNREALSOULSLIKE_API UClimbComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UClimbComponent();

	UFUNCTION(BlueprintPure, Category = "Climb")
	bool IsClimbing() const { return bClimbing; }

	// Jump input: a climb-jump while climbing, otherwise the character's normal jump.
	void HandleJumpPressed();

	// Move input while climbing, in wall space: X = sideways, Y = up/down.
	void AddClimbInput(const FVector2D& Input);

	// Let go of the wall (dodge input while climbing).
	void Release();

	// Animation the owner should play while climbing (key into CharacterAssets.json "anims");
	// NAME_None when the component isn't overriding the owner's animation.
	FName GetAnimOverride(bool& bOutLoop, float& OutDuration) const;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	UPROPERTY(Replicated)
	bool bClimbing = false;
};
