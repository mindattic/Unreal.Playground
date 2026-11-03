#pragma once

#include "CoreMinimal.h"
#include "SoulslikeTypes.generated.h"

UENUM(BlueprintType)
enum class ECombatState : uint8
{
	Idle,
	Attacking,
	Blocking,
	Parrying,
	Dodging,
	Healing,
	Staggered,
	Dead,
};

UENUM(BlueprintType)
enum class EAttackType : uint8
{
	Light,
	Heavy,
};

namespace SoulslikeTeam
{
	constexpr uint8 Players = 0;
	constexpr uint8 Skeletons = 1;
}

namespace SoulslikeTags
{
	// Component tag on surfaces a shadow merge can enter (stone, brick, the bridge).
	inline const FName Mergeable(TEXT("Mergeable"));
}
