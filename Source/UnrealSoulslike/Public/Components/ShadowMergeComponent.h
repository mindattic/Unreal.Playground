#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ShadowMergeComponent.generated.h"

// Ereban / A-Link-Between-Worlds style shadow merge: the player sinks into a merge-able surface
// (components tagged "Mergeable": stone, brick, the bridge) or into shadow, becomes a black
// splotch that crawls along walls, floors and ceilings and across connected surfaces, and can't
// be seen or heard while merged. Costs shadow energy; light and empty energy force it out.
UCLASS(ClassGroup = (Soulslike), meta = (BlueprintSpawnableComponent))
class UNREALSOULSLIKE_API UShadowMergeComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UShadowMergeComponent();

	UFUNCTION(BlueprintPure, Category = "ShadowMerge")
	bool IsMerged() const { return bMerged; }

	// 0..1, for the HUD.
	UFUNCTION(BlueprintPure, Category = "ShadowMerge")
	float GetEnergyFraction() const;

	// True when a merge would succeed right now (HUD prompt).
	UFUNCTION(BlueprintPure, Category = "ShadowMerge")
	bool CanMergeNow() const;

	// Merge input: enter when possible, leave when merged.
	void ToggleMerge();

	// Move input while merged, camera-relative like normal movement.
	void AddMergeInput(const FVector2D& Input);

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	UPROPERTY(Replicated)
	bool bMerged = false;
};
