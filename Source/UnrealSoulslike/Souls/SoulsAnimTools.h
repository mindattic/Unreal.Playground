#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "SoulsAnimTools.generated.h"

class UAnimSequence;

// Editor helpers the content build (tools/build_souls_character.py) calls on retargeted animations.
UCLASS()
class UNREALSOULSLIKE_API USoulsAnimTools : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// Pins each bone, for the whole clip, to its reference rotation turned by the matching offset
	// (the clip's own translation and scale stay). Used to close the fists round the sword hilt and
	// the shield grip, which the source library animates with open hands. Returns the bones changed.
	UFUNCTION(BlueprintCallable, Category = "Souls|Editor")
	static int32 BakeBoneOffsets(UAnimSequence* Sequence, const TArray<FName>& Bones, const TArray<FRotator>& Offsets);
};
