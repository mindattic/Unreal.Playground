#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SoulsGlider.generated.h"

class ASoulsKnight;
class AGrapplePoint;
class UAnimMontage;
class UEnhancedInputComponent;
class UInputAction;
class UInputMappingContext;
class UMaterialInterface;
class UProceduralMeshComponent;
class USceneComponent;
class UStaticMesh;
class UStaticMeshComponent;

// Where the knight is in the slingshot-and-umbrella traversal (UNR-T42).
UENUM(BlueprintType)
enum class EGlideState : uint8
{
	Idle,     // on foot, or an ordinary fall
	Reel,     // RT held through a ledge grapple: reeled to a hang under the hook, tension building
	Hang,     // hanging taut under the hook, tension building until RT is let go
	Flung,    // slung up and past the hook, umbrella still furled
	Gliding,  // umbrella open: a slow, steerable descent that drifts with the wind
};

// The knight's umbrella and the grapple slingshot that sends it aloft.
// - Hold RT through a ledge grapple: instead of being set down on the ledge you're reeled to a hang
//   under the hook and the line pulls taut. Let go of RT and the tension flings you up and past it.
// - In the air, press A (or keep it held from the fling) to open the umbrella: gravity hands over to a
//   gentle sink, the stick steers, a prevailing wind drifts you, and actors tagged GlideUpdraft lift you.
// - It closes on landing, on B, on X/Y (attacks), when you're hit, or when you grapple again.
// The component binds its own (non-consuming) Enhanced Input actions on A, B, X/Y and RT alongside
// the knight's, drives UCharacterMovementComponent (gravity scale, air control, velocity) and builds
// a procedural umbrella that rides furled across the back and opens over the head.
UCLASS(ClassGroup = (Souls), meta = (BlueprintSpawnableComponent))
class UNREALSOULSLIKE_API USoulsGlider : public UActorComponent
{
	GENERATED_BODY()

public:
	USoulsGlider();

	// ---- Slingshot ----
	// Tension builds from 0 to 1 over this long while RT is held through the reel and hang.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glider|Slingshot")
	float TensionChargeTime = 1.1f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glider|Slingshot")
	float ReelSpeed = 2300.f;

	// The hang point sits this far below the hook and this far out from the ledge.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glider|Slingshot")
	float HangDrop = 175.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glider|Slingshot")
	float HangOutset = 70.f;

	// At full tension the line stretches you this much lower before the fling.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glider|Slingshot")
	float TensionStretch = 55.f;

	// Launch velocity (cm/s) at no tension and at full tension: straight up, and on past the hook.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glider|Slingshot")
	float LaunchUpMin = 950.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glider|Slingshot")
	float LaunchUpMax = 1750.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glider|Slingshot")
	float LaunchForwardMin = 250.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glider|Slingshot")
	float LaunchForwardMax = 620.f;

	// ---- Glide ----
	// Steady sink rate under the open umbrella (cm/s).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glider|Glide")
	float GlideSinkSpeed = 170.f;

	// Airspeed the stick steers toward (the wind is added on top).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glider|Glide")
	float GlideSpeed = 620.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glider|Glide")
	float GlideMaxSpeed = 1100.f;

	// How quickly the velocity follows the stick (FInterpTo speed).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glider|Glide")
	float GlideSteerRate = 1.4f;

	// How hard the canopy brakes a rise or a plunge back to the sink rate.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glider|Glide")
	float CanopyBrakeRate = 2.6f;

	// Won't open closer to the ground than this.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glider|Glide")
	float MinOpenClearance = 160.f;

	// The prevailing wind: direction (taken from an AWindDirectionalSource in the level when there is
	// one) and strength in cm/s, with a slow gust.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glider|Wind")
	FVector WindDirection = FVector(0.92f, 0.38f, 0.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glider|Wind")
	float WindSpeed = 170.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glider|Wind")
	float WindGust = 0.3f;

	// Actors tagged with this lift a glider above them (campfires, chimneys, cliff updrafts).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glider|Wind")
	FName UpdraftTag = TEXT("GlideUpdraft");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glider|Wind")
	float UpdraftRadius = 450.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glider|Wind")
	float UpdraftHeight = 3000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glider|Wind")
	float UpdraftLift = 450.f;

	// ---- Animation (empty = the knight's own montages) ----
	// Hanging on the taut line: a paused frame of the throw (arm reaching up the line).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glider|Animation")
	TObjectPtr<UAnimMontage> HangMontage;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glider|Animation")
	float HangPoseFraction = 0.45f;

	// The fling's beat: the roll, sped up into a tuck.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glider|Animation")
	TObjectPtr<UAnimMontage> LaunchMontage;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glider|Animation")
	float LaunchBeatTime = 0.5f;

	// Dangling under the canopy: the swim idle (treading legs) looped slowly; rate 0 holds a frame.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glider|Animation")
	TObjectPtr<UAnimMontage> GlideMontage;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glider|Animation")
	float GlidePoseFraction = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glider|Animation")
	float GlidePoseRate = 0.45f;

	// ---- Umbrella model ----
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glider|Umbrella")
	bool bShowFurled = true;

	// Relative to the capsule: open over the head, furled diagonally across the back.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glider|Umbrella")
	FTransform OpenPose = FTransform(FRotator(-8.f, 0.f, 10.f), FVector(10.f, 22.f, 30.f), FVector(1.f));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glider|Umbrella")
	FTransform FurledPose = FTransform(FRotator(12.f, 0.f, 30.f), FVector(-26.f, 18.f, -30.f), FVector(0.8f));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glider|Umbrella")
	float ShaftLength = 140.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glider|Umbrella")
	float CanopyRadius = 105.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glider|Umbrella")
	float CanopyDepth = 42.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glider|Umbrella")
	FLinearColor CanopyColor = FLinearColor(0.30f, 0.035f, 0.03f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Glider|Umbrella")
	FLinearColor CanopyTrimColor = FLinearColor(0.045f, 0.04f, 0.035f);

	// ---- Hooks for ASoulsKnight ----
	// True while RT is held and no slingshot is running: the knight's ledge grapple should hand over.
	bool WantsSlingshot() const;

	// Take over a ledge grapple (call after the knight has ended its own grapple state).
	bool BeginSlingshot(AGrapplePoint* Point);

	// Input (bound automatically; public so tests and the knight can drive them too).
	void SetTensionHeld(bool bHeld);
	void TensionPressed() { SetTensionHeld(true); }
	void TensionReleased() { SetTensionHeld(false); }
	void OpenPressed();
	void OpenReleased();
	void ClosePressed();

	bool OpenUmbrella();
	void CloseUmbrella();
	// Drop everything (death, respawn, cutscene): slingshot cancelled, umbrella furled, movement restored.
	void Abort();

	UFUNCTION(BlueprintPure, Category = "Glider")
	EGlideState GetGlideState() const { return State; }

	UFUNCTION(BlueprintPure, Category = "Glider")
	float GetTension() const { return Tension; }

	UFUNCTION(BlueprintPure, Category = "Glider")
	bool IsGliding() const { return State == EGlideState::Gliding; }

	UFUNCTION(BlueprintPure, Category = "Glider")
	bool IsSlingshotting() const { return State == EGlideState::Reel || State == EGlideState::Hang; }

	FVector GetWind() const;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual void BeginPlay() override;

private:
	void SetState(EGlideState NewState);
	void TakeMovement();
	void RestoreMovement();
	void TickSlingshot(float DeltaTime);
	void Launch();
	void TickGlide(float DeltaTime);
	void DrawLine();
	float GroundClearance() const;

	void EnsureInput();
	void BuildUmbrella();
	void UpdateUmbrella(float DeltaTime);
	void PlayPose(UAnimMontage* Montage, float StartFraction, float Rate, bool bLoop);
	void StopPose();
	void PlayCue(float Volume, float Pitch) const;

	// -GlideDemo
	void TickDemo(float DeltaTime);
	void DemoShot(const TCHAR* Name) const;
	void DemoPhase(int32 Phase);

	TWeakObjectPtr<ASoulsKnight> Knight;

	EGlideState State = EGlideState::Idle;
	float StateTime = 0.f;
	float Tension = 0.f;
	bool bTensionHeld = false;
	bool bOpenHeld = false;
	TWeakObjectPtr<AGrapplePoint> SlingPoint;
	FVector SlingHook = FVector::ZeroVector;
	FVector SlingAnchor = FVector::ZeroVector;
	FVector SlingForward = FVector::ForwardVector;

	bool bMovementTaken = false;
	float SavedGravityScale = 1.f;
	float SavedAirControl = 0.35f;

	TArray<TWeakObjectPtr<AActor>> Updrafts;

	// Input
	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> GlideContext;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> OpenAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> CloseAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> TensionAction;

	TWeakObjectPtr<UEnhancedInputComponent> BoundInput;
	float InputCheckTime = 0.f;

	// Umbrella
	UPROPERTY(Transient)
	TObjectPtr<USceneComponent> UmbrellaRoot;

	UPROPERTY(Transient)
	TObjectPtr<UProceduralMeshComponent> Canopy;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> Shaft;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> Handle;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> Finial;

	UPROPERTY()
	TObjectPtr<UStaticMesh> CylinderMesh;

	UPROPERTY()
	TObjectPtr<UStaticMesh> SphereMesh;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> ShapeMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> PoseMontage;

	float OpenAlpha = 0.f;
	float SwayClock = 0.f;

	// -GlideDemo
	bool bDemo = false;
	int32 DemoStep = 0;
	float DemoClock = 0.f;
	float DemoStepTime = 0.f;
	float DemoLogTime = 0.f;
	int32 DemoCandidate = 0;
	TArray<TWeakObjectPtr<AGrapplePoint>> DemoPoints;
	TArray<FVector> DemoStands;
	FVector DemoGlideStart = FVector::ZeroVector;
	float DemoGlideStartTime = 0.f;
	float DemoApexClearance = 0.f;
	float DemoLaunchSpeed = 0.f;
	FVector DemoSteer = FVector::ZeroVector;
	bool bDemoShotTaken = false;
};
