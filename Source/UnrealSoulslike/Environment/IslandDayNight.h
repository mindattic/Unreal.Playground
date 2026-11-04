#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Subsystems/WorldSubsystem.h"
#include "IslandDayNight.generated.h"

class ADirectionalLight;
class ASkyLight;
class AExponentialHeightFog;
class APostProcessVolume;
class UDirectionalLightComponent;
class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMesh;

// Runs the island's day/night cycle (UNR-T39). Finds the level's sun (DirectionalLight), SkyLight, height fog
// and unbound PostProcessVolume and, as the in-game clock turns, swings the sun through a full day, keeps its
// colour and brightness in step (a dim, warm low sun; a whiter noon), brings up a cool moon opposite it at
// night with a field of stars, and leans the sky light, fog colour and exposure toward a readable moonlit night.
// The SkyAtmosphere and the real-time sky light follow the sun and moon on their own.
//
// Placed by tools/build_island.py (which also hands it the star mesh and material); a level without one gets
// one spawned at begin play by UIslandDayNightSpawner. The clock is server-authoritative and replicated.
//
// Command line:
//   -TimeOfDay=<hours>       start at that hour (0-24) instead of StartHours
//   -FreezeTimeOfDay         stop the clock
//   -DayLengthMinutes=<m>    real minutes per in-game day
//   -DayNightShots           screenshots dawn, noon, dusk, twilight and night into Saved/DayNightShots, then quits
//   -NoDayNight              no cycle at all (the level's static lighting as built)
UCLASS()
class UNREALSOULSLIKE_API AIslandDayNight : public AActor
{
	GENERATED_BODY()

public:
	AIslandDayNight();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// The first cycle actor in the world, if any.
	static AIslandDayNight* Get(const UObject* WorldContextObject);

	// Current in-game hour, 0..24.
	UFUNCTION(BlueprintPure, Category = "DayNight")
	float GetHours() const;

	// 0 in daylight, 1 in full night (sun 10+ degrees below the horizon). Handy for torches and fires.
	UFUNCTION(BlueprintPure, Category = "DayNight")
	float GetNightAmount() const;

	// Sun elevation above the horizon, degrees.
	UFUNCTION(BlueprintPure, Category = "DayNight")
	float GetSunElevation() const;

	// Server only: jump the clock (and optionally hold it there).
	UFUNCTION(BlueprintCallable, Category = "DayNight")
	void SetTimeOfDay(float Hours, bool bFreeze);

	// Hour the level starts at: late afternoon, the sun low in the west.
	UPROPERTY(EditAnywhere, Category = "DayNight|Clock", meta = (ClampMin = "0", ClampMax = "24"))
	float StartHours = 16.5f;

	UPROPERTY(EditAnywhere, Category = "DayNight|Clock", meta = (ClampMin = "0.1"))
	float RealMinutesPerDay = 2.f;

	// Seconds between sun moves. Every move invalidates the cached sun shadows, so it is stepped, not continuous.
	UPROPERTY(EditAnywhere, Category = "DayNight|Clock", meta = (ClampMin = "0"))
	float UpdateInterval = 0.15f;

	// Compass yaw of sunrise; the sun sets opposite. 150 sets it at yaw 330, where the island's sun always sat.
	UPROPERTY(EditAnywhere, Category = "DayNight|Sun")
	float SunriseYaw = 150.f;

	// Highest the sun climbs at noon (Hebridean summer), degrees.
	UPROPERTY(EditAnywhere, Category = "DayNight|Sun")
	float NoonElevation = 50.f;

	// Sun illuminance at noon, lux. A low sun gets much less (about 0.6x of this at 17 degrees).
	UPROPERTY(EditAnywhere, Category = "DayNight|Sun")
	float SunNoonIntensity = 6.5f;

	UPROPERTY(EditAnywhere, Category = "DayNight|Sun")
	float SunHorizonTemperature = 2700.f;

	UPROPERTY(EditAnywhere, Category = "DayNight|Sun")
	float SunNoonTemperature = 5700.f;

	UPROPERTY(EditAnywhere, Category = "DayNight|Moon")
	float MoonIntensity = 0.6f;

	UPROPERTY(EditAnywhere, Category = "DayNight|Moon")
	FLinearColor MoonColor = FLinearColor(0.55f, 0.68f, 1.f);

	// The moon runs opposite the sun, turned this far round the compass.
	UPROPERTY(EditAnywhere, Category = "DayNight|Moon")
	float MoonYawOffset = 25.f;

	// Sky light intensity in daylight / at night (the night value lifts the moonlit sky capture so it reads).
	UPROPERTY(EditAnywhere, Category = "DayNight|Sky")
	float DaySkyIntensity = 1.15f;

	UPROPERTY(EditAnywhere, Category = "DayNight|Sky")
	float NightSkyIntensity = 2.2f;

	UPROPERTY(EditAnywhere, Category = "DayNight|Sky")
	FLinearColor NightSkyTint = FLinearColor(0.55f, 0.65f, 1.f);

	// Exposure compensation (EV) added at full night on top of the post process volume's own.
	UPROPERTY(EditAnywhere, Category = "DayNight|Sky")
	float NightExposureBoost = 1.6f;

	UPROPERTY(EditAnywhere, Category = "DayNight|Fog")
	FLinearColor NightFogInscattering = FLinearColor(0.012f, 0.018f, 0.035f);

	UPROPERTY(EditAnywhere, Category = "DayNight|Fog")
	FLinearColor DuskFogDirectional = FLinearColor(0.55f, 0.3f, 0.14f);

	// Stars: instanced StarMesh with StarMaterial (needs a scalar parameter "Brightness"). Either missing = no stars.
	UPROPERTY(EditAnywhere, Category = "DayNight|Stars")
	TObjectPtr<UStaticMesh> StarMesh;

	UPROPERTY(EditAnywhere, Category = "DayNight|Stars")
	TObjectPtr<UMaterialInterface> StarMaterial;

	UPROPERTY(EditAnywhere, Category = "DayNight|Stars", meta = (ClampMin = "0", ClampMax = "4000"))
	int32 StarCount = 1400;

	UPROPERTY(EditAnywhere, Category = "DayNight|Stars")
	float StarDistance = 8000000.f;

	UPROPERTY(EditAnywhere, Category = "DayNight|Stars")
	float StarBrightness = 6.f;

protected:
	UPROPERTY(VisibleAnywhere, Category = "DayNight")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, Category = "DayNight")
	TObjectPtr<UDirectionalLightComponent> Moon;

	UPROPERTY(VisibleAnywhere, Category = "DayNight")
	TObjectPtr<UInstancedStaticMeshComponent> Stars;

private:
	void FindSceneLights();
	void BuildStars();
	void Apply(float Hours);
	FVector SunDirection(float Hours) const;
	double ClockNow() const;
	void TickShots(float DeltaSeconds);

	// Replicated clock: Hours = BaseHours + (server time - BaseTime) * rate.
	UPROPERTY(Replicated)
	float BaseHours = 16.5f;

	UPROPERTY(Replicated)
	double BaseTime = 0.0;

	UPROPERTY(Replicated)
	float HoursPerSecond = 0.02f;

	UPROPERTY(Replicated)
	bool bClockFrozen = false;

	TWeakObjectPtr<ADirectionalLight> Sun;
	TWeakObjectPtr<ASkyLight> Sky;
	TWeakObjectPtr<AExponentialHeightFog> Fog;
	TWeakObjectPtr<APostProcessVolume> Post;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> StarMID;

	FLinearColor DayFogInscattering = FLinearColor(0.24f, 0.30f, 0.37f);
	FLinearColor DayFogDirectional = FLinearColor(0.42f, 0.36f, 0.3f);
	float BaseExposureBias = 0.f;
	float LastSkyCaptureElevation = 1000.f;
	float SinceUpdate = 0.f;
	float LastAppliedHours = -1.f;
	float SunElevation = 0.f;
	bool bSunShown = true;
	bool bMoonShown = false;
	bool bStarsShown = false;

	// -DayNightShots
	bool bShots = false;
	int32 ShotIndex = 0;
	float ShotClock = 0.f;
	float ShotPhaseStart = 0.f;
	bool bShotWarm = false;
	bool bShotFramed = false;
	bool bShotTaken = false;
};

// Spawns an AIslandDayNight in Lvl_Isle when the level has none (so the cycle works before the map is rebuilt).
UCLASS()
class UNREALSOULSLIKE_API UIslandDayNightSpawner : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
};
