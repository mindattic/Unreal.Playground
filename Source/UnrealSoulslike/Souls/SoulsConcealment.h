#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "SoulsConcealment.generated.h"

class ATallGrassPatch;

// Cover that hides a target from enemy eyes, independent of the target's own stance (UNR-T43).
UCLASS()
class UNREALSOULSLIKE_API USoulsConcealment : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// Multiplier on the sight range at which an enemy can pick Target out: 1 = in the open, smaller = hidden
	// (inside tall grass, 0.25 by default). Multiply it into the target's visibility scale.
	UFUNCTION(BlueprintPure, Category = "Souls|Stealth")
	static float GetConcealment(const AActor* Target);

	// True when Target stands in cover (any concealment below 1).
	UFUNCTION(BlueprintPure, Category = "Souls|Stealth")
	static bool IsConcealed(const AActor* Target);

	// The patch of tall grass Target is in, if any (the most concealing one when patches overlap).
	static ATallGrassPatch* FindCover(const AActor* Target);
};
