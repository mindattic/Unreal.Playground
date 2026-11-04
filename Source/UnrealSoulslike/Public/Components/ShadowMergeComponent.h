#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/NetSerialization.h"
#include "ShadowMergeComponent.generated.h"

class ACharacter;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class AHebridesIsland;

// Ereban / A-Link-Between-Worlds style shadow merge: the player sinks into a merge-able surface
// (components tagged "Mergeable": stone, brick, the bridge) or into shadow, becomes a black
// splotch that crawls along walls, floors and ceilings and across connected surfaces, and can't
// be seen or heard while merged. Costs shadow energy; light and empty energy force it out.
UCLASS(ClassGroup = (Soulslike), meta = (BlueprintSpawnableComponent))
class UNREALSOULSLIKE_API UShadowMergeComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UShadowMergeComponent();

	UFUNCTION(BlueprintPure, Category = "ShadowMerge")
	bool IsMerged() const { return bMerged; }

	// 0..1, for the HUD.
	UFUNCTION(BlueprintPure, Category = "ShadowMerge")
	float GetEnergyFraction() const;

	// True when a merge would succeed right now (HUD prompt).
	UFUNCTION(BlueprintPure, Category = "ShadowMerge")
	bool CanMergeNow() const;

	// Merge input: enter when possible, leave when merged.
	void ToggleMerge();

	// Move input while merged, camera-relative like normal movement.
	void AddMergeInput(const FVector2D& Input);

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShadowMerge")
	float MergeReach = 150.f;

	// The knight's shadow melt (UNR-T71): any surface will do, in sun or shade, for as long as you like.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShadowMerge")
	bool bAnySurface = false;

	// Material the blob is drawn with (tinted near-black through its "Color" parameter).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShadowMerge")
	TObjectPtr<UMaterialInterface> BlobBaseMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShadowMerge")
	float CrawlSpeed = 350.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShadowMerge|Energy")
	float MaxEnergy = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShadowMerge|Energy")
	float MinEnergyToEnter = 15.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShadowMerge|Energy")
	float DrainPerSecond = 7.f;

	// Drain on a merge-able surface that sits in sunlight.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShadowMerge|Energy")
	float LitDrainPerSecond = 14.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShadowMerge|Energy")
	float RegenPerSecond = 15.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShadowMerge|Energy")
	float RegenDelay = 1.f;

	// How far a shadow test looks toward the sun for an occluder.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ShadowMerge")
	float SunTraceLength = 20000.f;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(ReplicatedUsing = OnRep_Merged)
	bool bMerged = false;

	UPROPERTY(Replicated)
	float Energy = 100.f;

	// Where the splotch sits; the owner drives its own copy, everyone else follows these.
	UPROPERTY(ReplicatedUsing = OnRep_MergePose)
	FVector_NetQuantize MergePoint;

	UPROPERTY(ReplicatedUsing = OnRep_MergePose)
	FVector_NetQuantizeNormal MergeNormal;

	UFUNCTION()
	void OnRep_Merged();

	UFUNCTION()
	void OnRep_MergePose();

	UFUNCTION(Server, Reliable)
	void ServerEnterMerge(FVector_NetQuantize Point, FVector_NetQuantizeNormal Normal);

	UFUNCTION(Server, Reliable)
	void ServerExitMerge();

	UFUNCTION(Server, Unreliable)
	void ServerUpdatePose(FVector_NetQuantize Point, FVector_NetQuantizeNormal Normal);

	UFUNCTION(Client, Reliable)
	void ClientExitedAt(FVector_NetQuantize Location);

private:
	ACharacter* GetCharacter() const;
	bool IsOwnerAlive() const;
	bool IsLocallyOwned() const;
	FVector TowardSun() const;
	bool IsInShadow(const FVector& Point) const;
	bool IsAllowedSurface(const FHitResult& Hit) const;
	bool Trace(const FVector& Start, const FVector& End, FHitResult& OutHit) const;
	bool FindMergeSurface(FHitResult& OutHit) const;
	bool FindCrawlStep(const FVector& Direction, float Distance, FHitResult& OutHit) const;
	void ApplyMergedState(bool bNowMerged);
	void SetPose(const FVector& Point, const FVector& Normal);
	void ExitMerge();
	void TickOwnerCrawl(float DeltaTime);
	void TickServerEnergy(float DeltaTime);
	void UpdateBlob();

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> Blob;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> BlobMaterial;

	mutable TWeakObjectPtr<AHebridesIsland> CachedIsland;
	mutable bool bCachedCanMerge = false;
	mutable float CanMergeCheckedAt = -1.f;

	FVector SurfacePoint = FVector::ZeroVector;
	FVector SurfaceNormal = FVector::UpVector;
	FVector PendingEnterPoint = FVector::ZeroVector;
	FVector PendingEnterNormal = FVector::UpVector;
	FVector2D PendingInput = FVector2D::ZeroVector;
	FVector LastSentPoint = FVector::ZeroVector;
	ECollisionEnabled::Type SavedCollision = ECollisionEnabled::QueryAndPhysics;
	float SendTimer = 0.f;
	float SurfaceCheckTimer = 0.f;
	float TimeSinceUnmerged = 0.f;
	bool bSpotLit = false;
	bool bSpotMergeable = true;
};
