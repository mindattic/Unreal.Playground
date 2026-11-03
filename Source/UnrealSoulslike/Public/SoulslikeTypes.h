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
	Parried,
	Dodging,
	Healing,
	Staggered,
};

UENUM(BlueprintType)
enum class EAttackType : uint8
{
	Light,
	Heavy,
};
