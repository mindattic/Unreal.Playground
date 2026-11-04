#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "SoulsFxLibrary.generated.h"

// UNR-T51. Editor helper for tools/build_torch_fire.py: tunes a Niagara system made of one
// lightweight (stateless) emitter, e.g. a duplicate of the engine's FountainLightweight template.
// Stateless emitter modules aren't reachable from Python, so this sets them through reflection.
//
// Target names: "Emitter" (the stateless emitter itself: SpawnInfos, FixedBounds, ...),
// "SpriteRenderer" (its sprite renderer: Material, SubImageSize, SortMode, ...), or a module's short
// class name ("InitializeParticle" for UNiagaraStatelessModule_InitializeParticle, "AddVelocity", ...).
// Paths are property names, dotted into structs, with [i] into arrays ("SpawnInfos[0].Rate").
// Every function is editor-only (does nothing and fails in a packaged game).
UCLASS()
class UNREALSOULSLIKE_API USoulsFxLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// "<Module> on|off" for every module of the system's stateless emitter, plus "renderer <Class>".
	UFUNCTION(BlueprintCallable, Category = "FX")
	static TArray<FString> ListFxModules(UObject* System);

	// Sets any property from its text form ("True", "4", "Rate", "(X=2,Y=2)", "/Game/A/M.M").
	UFUNCTION(BlueprintCallable, Category = "FX")
	static bool SetFxValue(UObject* System, const FString& Target, const FString& Path, const FString& Text);

	// The property's text form (empty when it can't be found).
	UFUNCTION(BlueprintCallable, Category = "FX")
	static FString GetFxValue(UObject* System, const FString& Target, const FString& Path);

	// Sets a Niagara stateless distribution (FNiagaraDistribution*). Mode and Values:
	//   UniformConstant [v]                      NonUniformConstant [c0..cN-1]
	//   UniformRange    [min, max]               NonUniformRange    [min0..minN-1, max0..maxN-1]
	//   UniformCurve    [t, v, t, v, ...]        NonUniformCurve / ColorGradient [t, c0..cN-1, t, ...]
	// (N = the distribution's channel count; curve times are the particle's normalized age, 0..1.)
	UFUNCTION(BlueprintCallable, Category = "FX")
	static bool SetFxDistribution(UObject* System, const FString& Target, const FString& Path, const FString& Mode, const TArray<float>& Values);

	// After the edits: refresh the emitter, its modules and renderers, recompile and wait. Save after.
	UFUNCTION(BlueprintCallable, Category = "FX")
	static bool FinishFx(UObject* System);
};
