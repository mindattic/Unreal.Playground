#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SoulsPickup.generated.h"

class UStaticMeshComponent;
class UPointLightComponent;
class ASoulsKnight;

UENUM(BlueprintType)
enum class ESoulsPickupKind : uint8
{
	GoldenSeed,   // +1 flask
	CrimsonTear,  // flasks heal more
	RuneCache,    // a purse of runes
};

// Something glinting on the ground, worth the detour: walk up and press Interact to take it.
UCLASS()
class UNREALSOULSLIKE_API ASoulsPickup : public AActor
{
	GENERATED_BODY()

public:
	ASoulsPickup();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Pickup")
	TObjectPtr<UStaticMeshComponent> Mote;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Pickup")
	TObjectPtr<UPointLightComponent> Glow;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pickup")
	ESoulsPickupKind Kind = ESoulsPickupKind::RuneCache;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pickup")
	int32 Runes = 1500;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pickup")
	float InteractRadius = 220.f;

	FText GetItemName() const;
	void Collect(ASoulsKnight* Knight);

	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;

private:
	FVector Base = FVector::ZeroVector;
	float Phase = 0.f;
};
