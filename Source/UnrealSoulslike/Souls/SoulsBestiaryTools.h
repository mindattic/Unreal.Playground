#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "SoulsBestiaryTools.generated.h"

class UAnimSequence;

// Editor helpers tools/build_bestiary.py calls on the animation it retargets onto the bestiary and the
// Ranger (UNR-T72).
UCLASS()
class UNREALSOULSLIKE_API USoulsBestiaryTools : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// The template's locomotion blueprint (ABP_Manny_Combat) runs Epic's CR_Mannequin_FootIK, which
	// plants each foot where Manny's ik_foot_l / ik_foot_r bones say it is. Epic's clips animate those
	// bones to follow the feet, but a retargeted clip on another rig has no such tracks, so the foot IK
	// held the Ranger's feet at their rest spot: feet torn off the shins and a stride that barely moved.
	// This writes ik_foot_root (its reference pose) and ik_foot_l / ik_foot_r (exactly where foot_l /
	// foot_r are, key by key) into the clip. The rig needs those three bones (tools/convert_bestiary.py
	// adds them, with ik_foot_l/r parented to ik_foot_root). Returns the bones written (0 = nothing to do).
	UFUNCTION(BlueprintCallable, Category = "Souls|Editor")
	static int32 BakeFootIKBones(UAnimSequence* Sequence);
};
