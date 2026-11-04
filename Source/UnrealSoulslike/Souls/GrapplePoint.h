#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GrapplePoint.generated.h"

class UStaticMeshComponent;
class UPointLightComponent;

// A hook-point on a ledge, branch or ruin top. Aim at it and press Grapple to be hauled up and set
// down on the ledge: the actor's forward vector points from the lip onto the walkable top.
UCLASS()
class UNREALSOULSLIKE_API AGrapplePoint : public AActor
{
	GENERATED_BODY()

public:
	AGrapplePoint();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grapple")
	TObjectPtr<UStaticMeshComponent> Anchor;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grapple")
	TObjectPtr<UPointLightComponent> Glint;

	// How far onto the ledge (along forward) the player is set down.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grapple")
	float LandingDepth = 170.f;

	// Where a character's capsule centre should end up (ledge top, found by a downward trace).
	FVector GetLandingLocation(float CapsuleHalfHeight) const;

	// The grappling gun (UNR-T70): an invisible, short-lived anchor on any ledge the player aims at.
	void MakeLedgeAnchor(const FVector& Ground);

	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;

private:
	FVector LandingGround = FVector::ZeroVector;
	bool bHasLanding = false;
	float Phase = 0.f;
};
