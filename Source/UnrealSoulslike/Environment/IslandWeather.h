#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Subsystems/WorldSubsystem.h"
#include "IslandWeather.generated.h"

class AIslandWeather;
class UAudioComponent;
class UDirectionalLightComponent;
class UExponentialHeightFogComponent;
class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UMaterialParameterCollection;
class UPostProcessComponent;
class USoundWaveProcedural;
class UStaticMesh;
class UVolumetricCloudComponent;

UENUM(BlueprintType)
enum class EIslandWeatherMode : uint8
{
	Cycle, // clear spells and rain fronts take turns
	Rain,  // raining, always
	Clear, // never rains
};

// The island's weather (UNR-T79): a prevailing wind that veers and gusts, volumetric clouds that drift with it,
// and rain fronts every few minutes. A front thickens and darkens the clouds, dims the sun, thickens the fog and
// greys the picture (a separate post-process blend and a sun colour multiplier, so it sits on top of whatever
// AIslandDayNight is doing), then rain falls round the player: streaks, splash rings on whatever they hit,
// a hiss and the wind's roar (both synthesised here). Surfaces get wet (MPC_Weather.Wetness, read by the island's
// terrain, prop, foliage and grass materials: darker and glossier) and dry off after the front has passed.
// Wind tears leaves off the trees near the player and tumbles them downwind, more in the gusts; grass and
// foliage sway with it (MPC_Weather.WindDir / WindStrength / WindTime).
//
// The weather is a pure function of the replicated server clock (like the day/night clock), so every co-op
// player sees the same front at the same time; wetness, particles and audio are local.
//
// Wind API for fire, smoke and anything else: AIslandWeather::GetWind(WorldContext) is the wind right now,
// XY = direction * strength (strength 1 = the prevailing breeze, gusts and fronts up to MaxWindStrength), Z = 0.
// With no weather actor in the world it returns the old fixed prevailing wind (0.6, 0.8, 0).
//
// Placed by tools/build_island.py (build_weather, which also builds its materials and MPC_Weather); a Lvl_Isle
// without one gets one spawned at begin play by UIslandWeatherSubsystem.
//
// Command line:
//   -WeatherRain        rain, always          -WeatherClear      never rain
//   -WeatherSeed=<n>    vary the fronts' strengths
//   -CloudDrift=<x>     scale the clouds' drift speed (default CloudDriftScale)
//   -WeatherShots       screenshots clear sky, drifting clouds (two shots seconds apart), a front arriving, rain,
//                       wet ground and blown leaves into Saved/WeatherShots, logs WEATHER_SHOTS_DONE, then quits
//   -NoWeather          no weather at all
UCLASS()
class UNREALSOULSLIKE_API AIslandWeather : public AActor
{
	GENERATED_BODY()

public:
	AIslandWeather();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// The world's weather actor, if any.
	static AIslandWeather* Get(const UObject* WorldContextObject);

	// The wind now: XY = direction * strength (1 = prevailing breeze), Z = 0. Safe to call every frame.
	UFUNCTION(BlueprintPure, Category = "Weather", meta = (WorldContext = "WorldContextObject"))
	static FVector GetWind(const UObject* WorldContextObject);

	// Unit XY direction the wind blows toward.
	UFUNCTION(BlueprintPure, Category = "Weather", meta = (WorldContext = "WorldContextObject"))
	static FVector GetWindDirection(const UObject* WorldContextObject);

	UFUNCTION(BlueprintPure, Category = "Weather", meta = (WorldContext = "WorldContextObject"))
	static float GetWindStrength(const UObject* WorldContextObject);

	// 0 in a lull, 1 at the height of a gust.
	UFUNCTION(BlueprintPure, Category = "Weather", meta = (WorldContext = "WorldContextObject"))
	static float GetGust(const UObject* WorldContextObject);

	// 0 dry, 1 downpour.
	UFUNCTION(BlueprintPure, Category = "Weather", meta = (WorldContext = "WorldContextObject"))
	static float GetRainAmount(const UObject* WorldContextObject);

	// 0 dry ground, 1 soaked.
	UFUNCTION(BlueprintPure, Category = "Weather", meta = (WorldContext = "WorldContextObject"))
	static float GetWetness(const UObject* WorldContextObject);

	// 0 fair-weather clouds, 1 the full grey of a front.
	UFUNCTION(BlueprintPure, Category = "Weather", meta = (WorldContext = "WorldContextObject"))
	static float GetOvercast(const UObject* WorldContextObject);

	// Server only.
	UFUNCTION(BlueprintCallable, Category = "Weather")
	void SetWeatherMode(EIslandWeatherMode NewMode);

	FVector GetWindVector() const { return CurWindDir * CurWindStrength; }

	// --- Assets (set by build_island.py; loaded from their default paths when empty) ---
	UPROPERTY(EditAnywhere, Category = "Weather|Assets")
	TObjectPtr<UMaterialParameterCollection> ParameterCollection;

	UPROPERTY(EditAnywhere, Category = "Weather|Assets")
	TObjectPtr<UMaterialInterface> RainMaterial;

	UPROPERTY(EditAnywhere, Category = "Weather|Assets")
	TObjectPtr<UMaterialInterface> SplashMaterial;

	UPROPERTY(EditAnywhere, Category = "Weather|Assets")
	TObjectPtr<UMaterialInterface> LeafMaterial;

	UPROPERTY(EditAnywhere, Category = "Weather|Assets")
	TObjectPtr<UStaticMesh> RainMesh;

	UPROPERTY(EditAnywhere, Category = "Weather|Assets")
	TObjectPtr<UStaticMesh> PlaneMesh;

	// --- Cycle (seconds): clear, clouds building, rain, clearing; then again ---
	UPROPERTY(EditAnywhere, Category = "Weather|Cycle", meta = (ClampMin = "0"))
	float ClearSeconds = 120.f;

	UPROPERTY(EditAnywhere, Category = "Weather|Cycle", meta = (ClampMin = "1"))
	float BuildUpSeconds = 35.f;

	UPROPERTY(EditAnywhere, Category = "Weather|Cycle", meta = (ClampMin = "0"))
	float RainSeconds = 75.f;

	UPROPERTY(EditAnywhere, Category = "Weather|Cycle", meta = (ClampMin = "1"))
	float ClearingSeconds = 40.f;

	// Seconds of full rain to soak the ground / of dry weather to dry it.
	UPROPERTY(EditAnywhere, Category = "Weather|Cycle", meta = (ClampMin = "1"))
	float SoakSeconds = 25.f;

	UPROPERTY(EditAnywhere, Category = "Weather|Cycle", meta = (ClampMin = "1"))
	float DrySeconds = 150.f;

	// --- Wind ---
	// Prevailing direction (XY); the same the field fire always used.
	UPROPERTY(EditAnywhere, Category = "Weather|Wind")
	FVector2D PrevailingWind = FVector2D(0.6, 0.8);

	UPROPERTY(EditAnywhere, Category = "Weather|Wind")
	float WindVeerDegrees = 35.f;

	// A front swings the wind round this far as it arrives.
	UPROPERTY(EditAnywhere, Category = "Weather|Wind")
	float FrontVeerDegrees = 20.f;

	UPROPERTY(EditAnywhere, Category = "Weather|Wind")
	float BaseWindStrength = 1.f;

	UPROPERTY(EditAnywhere, Category = "Weather|Wind")
	float FrontWindBoost = 0.8f;

	UPROPERTY(EditAnywhere, Category = "Weather|Wind")
	float MaxWindStrength = 2.5f;

	// --- Clouds (m_SimpleVolumetricCloud parameters, found by name at begin play and logged) ---
	UPROPERTY(EditAnywhere, Category = "Weather|Clouds")
	float CloudCoverageBoost = 0.45f;

	UPROPERTY(EditAnywhere, Category = "Weather|Clouds")
	float CloudDensityBoost = 1.5f;

	// Cloud albedo multiplier at full overcast (dark rain clouds).
	UPROPERTY(EditAnywhere, Category = "Weather|Clouds")
	float CloudStormAlbedo = 0.3f;

	// Multiplies the cloud material's own wind speed and turns it to the prevailing wind.
	UPROPERTY(EditAnywhere, Category = "Weather|Clouds")
	float CloudDriftScale = 4.f;

	// --- Light (on top of the day/night cycle) ---
	UPROPERTY(EditAnywhere, Category = "Weather|Light")
	float SunOvercastDim = 0.3f;

	UPROPERTY(EditAnywhere, Category = "Weather|Light")
	float FogRainBoost = 1.8f;

	UPROPERTY(EditAnywhere, Category = "Weather|Light")
	FLinearColor OvercastTint = FLinearColor(0.74f, 0.77f, 0.84f);

	UPROPERTY(EditAnywhere, Category = "Weather|Light")
	float OvercastSaturation = 0.72f;

	// --- Rain ---
	UPROPERTY(EditAnywhere, Category = "Weather|Rain", meta = (ClampMin = "0", ClampMax = "8000"))
	int32 RainCount = 8000;

	// Box of rain kept round the camera (cm).
	UPROPERTY(EditAnywhere, Category = "Weather|Rain")
	FVector RainBox = FVector(3000.0, 3000.0, 1400.0);

	UPROPERTY(EditAnywhere, Category = "Weather|Rain")
	float RainFallSpeed = 950.f;

	// Sideways drift of the drops per unit of wind strength (cm/s).
	UPROPERTY(EditAnywhere, Category = "Weather|Rain")
	float RainWindDrift = 260.f;

	UPROPERTY(EditAnywhere, Category = "Weather|Rain")
	float RainStreakLength = 90.f;

	UPROPERTY(EditAnywhere, Category = "Weather|Rain")
	float RainStreakWidth = 1.8f;

	UPROPERTY(EditAnywhere, Category = "Weather|Rain")
	float RainDayBrightness = 0.85f;

	UPROPERTY(EditAnywhere, Category = "Weather|Rain")
	float RainNightBrightness = 0.06f;

	UPROPERTY(EditAnywhere, Category = "Weather|Rain", meta = (ClampMin = "0", ClampMax = "1000"))
	int32 SplashCount = 160;

	// Splashes per second in a downpour.
	UPROPERTY(EditAnywhere, Category = "Weather|Rain")
	float SplashRate = 420.f;

	UPROPERTY(EditAnywhere, Category = "Weather|Rain")
	float SplashRadius = 1400.f;

	// --- Leaves ---
	// Meshes whose name contains one of these are leafy trees.
	UPROPERTY(EditAnywhere, Category = "Weather|Leaves")
	TArray<FString> TreeMeshKeywords;

	UPROPERTY(EditAnywhere, Category = "Weather|Leaves", meta = (ClampMin = "0", ClampMax = "1000"))
	int32 LeafCount = 220;

	// Leaves torn off per second in a steady breeze / extra at the height of a gust.
	UPROPERTY(EditAnywhere, Category = "Weather|Leaves")
	float LeafRate = 3.f;

	UPROPERTY(EditAnywhere, Category = "Weather|Leaves")
	float LeafGustRate = 28.f;

	// Only trees this close to the camera shed leaves (cm).
	UPROPERTY(EditAnywhere, Category = "Weather|Leaves")
	float LeafRadius = 4500.f;

	// Leaf speed per unit of wind strength (cm/s).
	UPROPERTY(EditAnywhere, Category = "Weather|Leaves")
	float LeafWindSpeed = 450.f;

	UPROPERTY(EditAnywhere, Category = "Weather|Leaves")
	float LeafLife = 16.f;

	// --- Audio ---
	UPROPERTY(EditAnywhere, Category = "Weather|Audio")
	float RainVolume = 0.8f;

	UPROPERTY(EditAnywhere, Category = "Weather|Audio")
	float WindVolume = 0.5f;

protected:
	UPROPERTY(VisibleAnywhere, Category = "Weather")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, Category = "Weather")
	TObjectPtr<UInstancedStaticMeshComponent> RainDrops;

	UPROPERTY(VisibleAnywhere, Category = "Weather")
	TObjectPtr<UInstancedStaticMeshComponent> Splashes;

	UPROPERTY(VisibleAnywhere, Category = "Weather")
	TObjectPtr<UInstancedStaticMeshComponent> LeafCloud;

	UPROPERTY(VisibleAnywhere, Category = "Weather")
	TObjectPtr<UPostProcessComponent> WeatherPost;

	UPROPERTY(VisibleAnywhere, Category = "Weather")
	TObjectPtr<UAudioComponent> RainAudio;

	UPROPERTY(VisibleAnywhere, Category = "Weather")
	TObjectPtr<UAudioComponent> WindAudio;

private:
	struct FCanopy
	{
		FVector Center = FVector::ZeroVector;
		float Radius = 300.f;
		float Height = 200.f;
	};

	struct FLeaf
	{
		FVector Pos = FVector::ZeroVector;
		FVector Vel = FVector::ZeroVector;
		FQuat Rot = FQuat::Identity;
		FVector SpinAxis = FVector::UpVector;
		float Spin = 0.f;
		float Age = 0.f;
		float Life = 10.f;
		float GroundZ = -1.0e6f;
		float NextTrace = 0.f;
		float Size = 0.1f;
		float Phase = 0.f;
		uint8 State = 0; // 0 free, 1 in the air, 2 lying on the ground
	};

	struct FSplash
	{
		float Age = 1.f;
		float Life = 0.4f;
		bool bLive = false;
	};

	struct FShotOverride
	{
		bool bActive = false;
		float Overcast = 0.f;
		float Rain = 0.f;
		float Wet = 0.f;
		float Gust = -1.f;
	};

	double ClockNow() const;
	void EvaluateSky(double Now, float& OutOvercast, float& OutRain) const;
	void EvaluateWind(double Now, float OvercastNow, FVector& OutDir, float& OutStrength, float& OutGust) const;
	void LoadAssets();
	void SetupRain();
	void SetupSplashes();
	void SetupLeaves();
	void SetupAudio();
	void FindSceneActors();
	void SetupClouds();
	void GatherTrees();
	bool GetViewLocation(FVector& OutLocation) const;
	void TickVisuals(float DeltaSeconds);
	void UpdateMaterialParameters(float DeltaSeconds);
	void UpdateClouds();
	void UpdateLighting();
	void UpdateRain(const FVector& View);
	void UpdateSplashes(float DeltaSeconds, const FVector& View);
	void UpdateLeaves(float DeltaSeconds, const FVector& View);
	void UpdateAudio(float DeltaSeconds);
	float TraceGround(const FVector& From, float Down) const;
	void LogPhase();
	void TickShots(float DeltaSeconds);

	// Replicated weather clock.
	UPROPERTY(Replicated)
	EIslandWeatherMode Mode = EIslandWeatherMode::Cycle;

	UPROPERTY(Replicated)
	double CycleStart = 0.0;

	UPROPERTY(Replicated)
	int32 Seed = 1979;

	// State now.
	float CurOvercast = 0.f;
	float CurRain = 0.f;
	float CurWetness = 0.f;
	float CurGust = 0.f;
	float CurWindStrength = 1.f;
	FVector CurWindDir = FVector(0.6, 0.8, 0.0);
	float WindTimeOffset = 0.f;
	FVector RainOffset = FVector::ZeroVector;
	float RainBrightness = 0.5f;
	FShotOverride Forced;
	bool bVisuals = false;
	bool bOff = false;
	FString LastPhase;

	// Clouds.
	TWeakObjectPtr<UVolumetricCloudComponent> Cloud;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> CloudMID;

	FName CoverageParam;
	FName DensityParam;
	FName AlbedoParam;
	FName StormAlbedoParam;
	float BaseCoverage = 0.f;
	float BaseDensity = 1.f;
	FLinearColor BaseAlbedo = FLinearColor::White;
	FLinearColor BaseStormAlbedo = FLinearColor::White;
	float AppliedOvercast = -1.f;

	// Light and fog.
	TWeakObjectPtr<UDirectionalLightComponent> Sun;
	FLinearColor SunBaseColor = FLinearColor::White;
	TWeakObjectPtr<UExponentialHeightFogComponent> Fog;
	float FogBaseDensity = 0.f;
	float AppliedSunDim = -1.f;
	float AppliedFog = -1.f;

	// Rain.
	FVector RainAnchor = FVector(1.0e9, 0.0, 0.0);
	FVector2D RainStreakShape = FVector2D(0.0, 27.5);
	bool bRainShown = false;

	// Splashes.
	TArray<FSplash> SplashPool;
	TArray<FTransform> SplashXf;
	float SplashAccum = 0.f;
	float SplashPlaneSize = 100.f;
	int32 LiveSplashes = 0;

	// Leaves.
	TArray<FCanopy> Canopies;
	TArray<int32> NearCanopies;
	float NearRefresh = 0.f;
	TArray<FLeaf> LeafPool;
	TArray<FTransform> LeafXf;
	float LeafAccum = 0.f;
	float LeafPlaneSize = 100.f;
	int32 LiveLeaves = 0;

	// Audio.
	UPROPERTY(Transient)
	TObjectPtr<USoundWaveProcedural> RainWave;

	UPROPERTY(Transient)
	TObjectPtr<USoundWaveProcedural> WindWave;

	float RainVolNow = 0.f;
	float WindVolNow = 0.f;

	// -WeatherShots
	bool bShots = false;
	int32 ShotIndex = 0;
	float ShotClock = 0.f;
	float ShotPhaseStart = 0.f;
	float ShotYaw = 0.f;
	bool bShotWarm = false;
	bool bShotFramed = false;
	bool bShotTaken = false;
};

// Keeps track of the world's AIslandWeather (for the static getters) and spawns one in Lvl_Isle when the level
// has none (so the weather works before the map is rebuilt).
UCLASS()
class UNREALSOULSLIKE_API UIslandWeatherSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	TWeakObjectPtr<AIslandWeather> Weather;
};
