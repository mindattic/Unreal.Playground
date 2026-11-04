#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SoulsSparks.generated.h"

class UStaticMesh;
class UStaticMeshComponent;
class UMaterialInterface;
class UPointLightComponent;

// A short burst of glowing bits and a flash of light: steel on steel (a blow caught on a shield,
// a parry) or the glint of a blade about to fall. Lives a fraction of a second, then removes itself.
UCLASS()
class UNREALSOULSLIKE_API ASoulsSparks : public AActor
{
	GENERATED_BODY()

public:
	ASoulsSparks();

	// Count bits flung out around Direction (spread 0..1), Scale sizes bits and flash.
	static ASoulsSparks* Burst(UWorld* World, const FVector& Location, const FVector& Direction, int32 Count, float Scale,
		float Spread = 0.8f, const FLinearColor& FlashColor = FLinearColor(1.f, 0.72f, 0.35f));

	virtual void Tick(float DeltaSeconds) override;

private:
	UPROPERTY()
	TObjectPtr<UStaticMesh> SparkMesh;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> SparkMaterial;

	UPROPERTY()
	TObjectPtr<UPointLightComponent> Flash;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> Bits;

	TArray<FVector> Velocities;
	float Age = 0.f;
	float Life = 0.32f;
	float FlashIntensity = 0.f;
	float BitScale = 1.f;
};
