#pragma once

#include "CoreMinimal.h"
#include "SoulsEnemy.h"
#include "SoulsShadowBoss.generated.h"

class UDecalComponent;
class UPointLightComponent;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class ASoulsShadowPuddle;

// What the Shade does once it has melted into the ground.
UENUM()
enum class EShadeMelt : uint8
{
	Flank,    // slide around the player and rise behind or beside them with a rising slash
	Under,    // lunge under the player, bubble, and burst up in place (a dodge check)
	Escape,   // slip away from pressure, then come round to flank
};

UENUM()
enum class EShadeState : uint8
{
	Solid,
	Melting,
	Gliding,
	Bubbling,
	Rising,
};

// Morrigan of the Mire, Shade of the Fen: a slender black knight who fights with the shared sword
// kit while solid (combos, heavies; parryable, stance-breakable, riposte-able) and melts into a
// creeping pool of shadow to cross the arena, escape pressure and rise behind the player. The pool
// can't be hit or locked on. Below half health she melts more often and leaves lingering shadow
// puddles that slow the player and sap stamina.
UCLASS()
class UNREALSOULSLIKE_API ASoulsShadowBoss : public ASoulsBoss
{
	GENERATED_BODY()

public:
	ASoulsShadowBoss();

	// ---- Look (filled by tools/build_shadow_boss.py) ----
	// Translucent overlay over the armour: a faint violet rim when solid, a black silhouette as she melts.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shade|Look")
	TObjectPtr<UMaterialInterface> ShadeOverlayMaterial;

	// Deferred decal: the creeping shadow pool (scalar params Pulse, Opacity).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shade|Look")
	TObjectPtr<UMaterialInterface> PoolMaterial;

	// Translucent smoke for the wisps that curl off the pool.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shade|Look")
	TObjectPtr<UMaterialInterface> WispMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shade|Look")
	TObjectPtr<USoundBase> MeltSound;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shade|Look")
	TObjectPtr<USoundBase> EruptSound;

	// ---- Tuning ----
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shade")
	float MeltTime = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shade")
	float GlideSpeed = 1150.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shade")
	float BubbleTime = 0.55f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shade")
	float RiseTime = 0.3f;

	// Seconds between melts (min..max), solid phase and phase two.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shade")
	FVector2D MeltCooldown = FVector2D(6.f, 9.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shade")
	FVector2D PhaseTwoMeltCooldown = FVector2D(3.f, 5.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shade")
	float GeyserRadius = 240.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shade")
	float PuddleLifetime = 12.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shade")
	float PuddleRadius = 210.f;

	// The player's run speed inside a puddle, and stamina sapped per second.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shade")
	float PuddleSlow = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shade")
	float PuddleStaminaDrain = 16.f;

	EShadeState GetShadeState() const { return State; }
	bool IsShadow() const { return State != EShadeState::Solid; }

	// Melt now (AI decision, or the -ShadowBossDemo script).
	bool BeginMelt(EShadeMelt Kind);

	virtual bool CanBeLockedOn() const override;
	virtual void Tick(float DeltaSeconds) override;
	virtual float TakeDamage(float Damage, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;
	virtual void ApplyDamage(float Damage, AActor* DamageCauser, const FVector& DamageLocation, const FVector& DamageImpulse) override;
	virtual void HandleDeath() override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void ThinkShade(float DeltaSeconds);
	void TickMelting(float DeltaSeconds);
	void TickGliding(float DeltaSeconds);
	void TickBubbling(float DeltaSeconds);
	void TickRising(float DeltaSeconds);
	void Erupt();
	void FinishRise();

	// Visual blend from solid (0) to fully sunk into the pool (1).
	void ApplySink(float Alpha);
	void SetShade(float Darkness);
	void SetPoolVisible(bool bVisible);
	void SetBodyVisible(bool bVisible);
	void UpdatePoolLook(float DeltaSeconds, float Intensity, float Pulse);
	void UpdateWisps(float DeltaSeconds, float Intensity, float Rise);
	void BurstWisps();
	FVector GroundUnder(const FVector& Location) const;
	FVector GlideGoal() const;
	void SlideTowards(const FVector& Goal, float Speed, float DeltaSeconds);

	void DropPuddle(const FVector& Location);
	void UpdatePuddles(float DeltaSeconds);
	void ReleaseSlowedKnights();

	void RunVerificationDemo();
	void StartDemoTimeline();
	FTimerHandle DemoWarmTimer;
	FVector DemoCameraAxis = FVector::ForwardVector;
	ASoulsKnight* Foe() const;

	UPROPERTY(VisibleAnywhere, Category = "Shade")
	TObjectPtr<UDecalComponent> PoolDecal;

	UPROPERTY(VisibleAnywhere, Category = "Shade")
	TObjectPtr<UPointLightComponent> PoolLight;

	UPROPERTY(VisibleAnywhere, Category = "Shade")
	TArray<TObjectPtr<UStaticMeshComponent>> Wisps;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> PoolMID;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> OverlayMID;

	UPROPERTY(Transient)
	TArray<TObjectPtr<ASoulsShadowPuddle>> Puddles;

	struct FWisp
	{
		FVector Offset = FVector::ZeroVector;
		float Age = 0.f;
		float Life = 1.f;
		float Size = 1.f;
		float Speed = 100.f;
	};
	TArray<FWisp> WispState;

	// Knights slowed by puddles, with the run speed to give back.
	TMap<TWeakObjectPtr<ASoulsKnight>, float> SlowedKnights;
	TArray<TWeakObjectPtr<ASoulsKnight>> RelockKnights;

	EShadeState State = EShadeState::Solid;
	EShadeMelt MeltKind = EShadeMelt::Flank;
	float StateTime = 0.f;
	float GlideTime = 0.f;
	float StallTime = 0.f;
	float NextMeltTime = 0.f;
	float NextThinkTime = 0.f;
	float FlankSide = 1.f;
	float PoolPulse = 0.f;
	float CurrentShade = 0.f;
	float Sink = 0.f;
	int32 ComboMeltCheckedSwing = -1;
	bool bWasPhaseTwo = false;
	bool bDemo = false;
	FVector EscapeGoal = FVector::ZeroVector;
	FVector BubbleSpot = FVector::ZeroVector;
	FVector BaseMeshLocation = FVector::ZeroVector;
	FVector BaseMeshScale = FVector::OneVector;
	TArray<float> RecentHits;
	float SeenDamageTime = -100.f;
};

// A lingering slick of shadow left behind in phase two: slows whoever wades through it and saps
// their stamina (the Shade does the slowing; the puddle only draws itself and fades).
UCLASS()
class UNREALSOULSLIKE_API ASoulsShadowPuddle : public AActor
{
	GENERATED_BODY()

public:
	ASoulsShadowPuddle();

	void Init(UMaterialInterface* Material, float InRadius, float InLifetime);
	virtual void Tick(float DeltaSeconds) override;

	bool Contains(const FVector& Location) const;
	bool IsFading() const { return Age > Lifetime - 1.2f; }
	void FadeNow();

	float Radius = 200.f;

private:
	UPROPERTY(VisibleAnywhere, Category = "Shade")
	TObjectPtr<UDecalComponent> Decal;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> MID;

	float Age = 0.f;
	float Lifetime = 10.f;
	float Seed = 0.f;
};
