#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SiteOfGrace.generated.h"

class UPointLightComponent;
class UStaticMeshComponent;
class UNiagaraComponent;
class ASoulsKnight;

// A checkpoint: a ring of golden light. Walk up and press Interact to rest: health, stamina and
// flasks refill, the island's enemies return, and death brings you back here.
UCLASS()
class UNREALSOULSLIKE_API ASiteOfGrace : public AActor
{
	GENERATED_BODY()

public:
	ASiteOfGrace();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grace")
	TObjectPtr<UStaticMeshComponent> Core;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grace")
	TObjectPtr<UStaticMeshComponent> Halo;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grace")
	TObjectPtr<UPointLightComponent> Light;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grace")
	float InteractRadius = 260.f;

	bool bDiscovered = false;

	void Rest(ASoulsKnight* Knight);
	FTransform GetRespawnTransform() const;

	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;

private:
	float Phase = 0.f;
};
