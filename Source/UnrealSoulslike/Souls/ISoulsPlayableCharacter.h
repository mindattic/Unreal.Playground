#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "ISoulsPlayableCharacter.generated.h"

UINTERFACE(BlueprintType)
class UNREALSOULSLIKE_API USoulsPlayableCharacter : public UInterface
{
	GENERATED_BODY()
};

// Implemented by every pawn the character roster (UNR-T85, USoulsCharacterRosterComponent) can
// cycle to with , and . : the knight, the Ranger, and whatever plays alongside them later. The
// roster never needs to know what kind of pawn it's switching to, only that it answers to these two.
class UNREALSOULSLIKE_API ISoulsPlayableCharacter
{
	GENERATED_BODY()

public:
	// Just took over as the controller's pawn and view target: show itself, restore movement/collision.
	virtual void OnBecomeActive() = 0;

	// About to be switched away from: hide and freeze in place (not destroyed), so it parks safely
	// with its own state intact until the roster switches back to it.
	virtual void OnBecomeInactive() = 0;
};
