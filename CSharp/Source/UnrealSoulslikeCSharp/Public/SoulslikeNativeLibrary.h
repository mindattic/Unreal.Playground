#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "SoulslikeNativeLibrary.generated.h"

class UFont;

// Reflection gaps of the C# twin: engine state the C++ game reads directly (globals and
// non-UObject APIs) exposed as UFUNCTIONs so the C# code can make the identical calls.
// Names avoid a "Get" prefix so UnrealSharp keeps them as methods instead of turning them into
// properties. Every function here is listed in CSharp/PORT_STATUS.md.
UCLASS(meta = (ScriptName = "SoulslikeNative"))
class UNREALSOULSLIKECSHARP_API USoulslikeNativeLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// FMath::RandInit: seeds the stream FMath::FRand / KismetMathLibrary RandomFloat read from.
	UFUNCTION(BlueprintCallable, Category = "Soulslike|Native")
	static void SeedRandom(int32 Seed);

	// GGameThreadTime of the last frame, in milliseconds.
	UFUNCTION(BlueprintCallable, Category = "Soulslike|Native")
	static float ReadGameThreadMs();

	// FPlatformMemory::GetStats().PeakUsedPhysical, in megabytes.
	UFUNCTION(BlueprintCallable, Category = "Soulslike|Native")
	static float ReadPeakUsedPhysicalMB();

	// FScreenshotRequest::RequestScreenshot(Name, bShowUI = true, bAddFilenameSuffix = false).
	UFUNCTION(BlueprintCallable, Category = "Soulslike|Native")
	static void RequestScreenshot(const FString& Name);

	// FPaths::ProjectSavedDir(), the same folder the C++ build writes its benchmark into.
	UFUNCTION(BlueprintCallable, Category = "Soulslike|Native")
	static FString ResolveSavedDir();

	// FPaths::ProjectContentDir(), where the terrain and character data files live.
	UFUNCTION(BlueprintCallable, Category = "Soulslike|Native")
	static FString ResolveContentDir();

	// GEngine->GetSmallFont / GetMediumFont / GetLargeFont, the fonts the C++ HUD draws with.
	UFUNCTION(BlueprintCallable, Category = "Soulslike|Native")
	static UFont* EngineSmallFont();

	UFUNCTION(BlueprintCallable, Category = "Soulslike|Native")
	static UFont* EngineMediumFont();

	UFUNCTION(BlueprintCallable, Category = "Soulslike|Native")
	static UFont* EngineLargeFont();
};
