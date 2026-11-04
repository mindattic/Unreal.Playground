#include "IslandDayNight.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/SkyLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMisc.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Math/RandomStream.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Net/UnrealNetwork.h"
#include "UnrealClient.h"
#include "UnrealSoulslike.h"

#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif

namespace IslandDN
{
	struct FShot
	{
		const TCHAR* Name;
		float Hours;
	};

	// Dawn (sun ~6 deg), noon, dusk (~6 deg, west), twilight (-10 deg: stars coming out), night (moon high).
	static const FShot Shots[] = {
		{ TEXT("DayNight_Dawn"), 6.6f },
		{ TEXT("DayNight_Noon"), 12.5f },
		{ TEXT("DayNight_Dusk"), 17.5f },
		{ TEXT("DayNight_Twilight"), 18.9f },
		{ TEXT("DayNight_Night"), 0.5f },
	};
	static constexpr float ShotWarmup = 14.f;
	static constexpr float ShotSettle = 5.f;

	static float ElevationDeg(const FVector& Dir)
	{
		return (float)FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(Dir.Z, -1.0, 1.0)));
	}

	static bool ShadersCompiling()
	{
#if WITH_EDITOR
		return GShaderCompilingManager && GShaderCompilingManager->GetNumRemainingJobs() > 0;
#else
		return false;
#endif
	}
}

AIslandDayNight::AIslandDayNight()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetNetUpdateFrequency(1.f);

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	Moon = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("Moon"));
	Moon->SetupAttachment(Root);
	Moon->SetMobility(EComponentMobility::Movable);
	Moon->SetUsingAbsoluteRotation(true);
	Moon->bAtmosphereSunLight = true;
	Moon->AtmosphereSunLightIndex = 1;
	Moon->Intensity = 0.f;
	Moon->LightSourceAngle = 0.55f;
	Moon->CastShadows = true;

	Stars = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Stars"));
	Stars->SetupAttachment(Root);
	Stars->SetMobility(EComponentMobility::Movable);
	Stars->SetUsingAbsoluteLocation(true);
	Stars->SetUsingAbsoluteRotation(true);
	Stars->SetUsingAbsoluteScale(true);
	Stars->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Stars->SetCanEverAffectNavigation(false);
	Stars->SetCastShadow(false);
	Stars->bCastDynamicShadow = false;
	Stars->bAffectDynamicIndirectLighting = false;
	Stars->bAffectDistanceFieldLighting = false;
	Stars->bVisibleInRayTracing = false;
	Stars->bVisibleInReflectionCaptures = false;
	Stars->bVisibleInRealTimeSkyCaptures = false;
	Stars->bReceivesDecals = false;
}

void AIslandDayNight::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AIslandDayNight, BaseHours);
	DOREPLIFETIME(AIslandDayNight, BaseTime);
	DOREPLIFETIME(AIslandDayNight, HoursPerSecond);
	DOREPLIFETIME(AIslandDayNight, bClockFrozen);
}

AIslandDayNight* AIslandDayNight::Get(const UObject* WorldContextObject)
{
	UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	if (!World)
	{
		return nullptr;
	}
	TActorIterator<AIslandDayNight> It(World);
	return It ? *It : nullptr;
}

void AIslandDayNight::BeginPlay()
{
	Super::BeginPlay();

	if (FParse::Param(FCommandLine::Get(), TEXT("NoDayNight")))
	{
		Stars->SetVisibility(false);
		Moon->SetVisibility(false);
		SetActorTickEnabled(false);
		UE_LOG(LogUnrealSoulslike, Log, TEXT("DayNight: off (-NoDayNight)"));
		return;
	}

	if (HasAuthority())
	{
		float Hours = StartHours;
		FParse::Value(FCommandLine::Get(), TEXT("TimeOfDay="), Hours);
		float Minutes = RealMinutesPerDay;
		FParse::Value(FCommandLine::Get(), TEXT("DayLengthMinutes="), Minutes);
		HoursPerSecond = 24.f / (FMath::Max(Minutes, 0.1f) * 60.f);
		BaseHours = FMath::Fmod(FMath::Fmod(Hours, 24.f) + 24.f, 24.f);
		BaseTime = ClockNow();
		bClockFrozen = FParse::Param(FCommandLine::Get(), TEXT("FreezeTimeOfDay"));
		bShots = FParse::Param(FCommandLine::Get(), TEXT("DayNightShots"));
	}

	FindSceneLights();
	BuildStars();
	Moon->SetVisibility(false);
	bMoonShown = false;
	Apply(GetHours());
	UE_LOG(LogUnrealSoulslike, Log, TEXT("DayNight: start %.2f h, %.1f real min/day%s, sun %s, sky %s, fog %s, post %s, %d stars, sun elevation %.1f"),
		GetHours(), 24.f / (HoursPerSecond * 60.f), bClockFrozen ? TEXT(" (frozen)") : TEXT(""),
		Sun.IsValid() ? TEXT("found") : TEXT("MISSING"), Sky.IsValid() ? TEXT("found") : TEXT("missing"),
		Fog.IsValid() ? TEXT("found") : TEXT("missing"), Post.IsValid() ? TEXT("found") : TEXT("missing"),
		Stars->GetInstanceCount(), SunElevation);
}

void AIslandDayNight::FindSceneLights()
{
	UWorld* World = GetWorld();
	for (TActorIterator<ADirectionalLight> It(World); It; ++It)
	{
		UDirectionalLightComponent* Light = It->GetComponent();
		if (!Light)
		{
			continue;
		}
		// Prefer the atmosphere's primary sun light; otherwise take the first directional light.
		if (!Sun.IsValid() || (Light->bAtmosphereSunLight && Light->AtmosphereSunLightIndex == 0))
		{
			Sun = *It;
		}
	}
	if (ADirectionalLight* SunActor = Sun.Get())
	{
		UDirectionalLightComponent* Light = SunActor->GetComponent();
		if (Light->Mobility != EComponentMobility::Movable)
		{
			Light->SetMobility(EComponentMobility::Movable);
		}
		Light->SetUseTemperature(true);
		if (!Light->bAtmosphereSunLight)
		{
			Light->SetAtmosphereSunLight(true);
			Light->SetAtmosphereSunLightIndex(0);
		}
	}
	if (TActorIterator<ASkyLight> SkyIt(World); SkyIt)
	{
		Sky = *SkyIt;
	}
	if (TActorIterator<AExponentialHeightFog> FogIt(World); FogIt)
	{
		Fog = *FogIt;
		if (UExponentialHeightFogComponent* FogComp = FogIt->GetComponent())
		{
			DayFogInscattering = FogComp->FogInscatteringLuminance;
			DayFogDirectional = FogComp->DirectionalInscatteringLuminance;
		}
	}
	for (TActorIterator<APostProcessVolume> It(World); It; ++It)
	{
		if (It->bUnbound)
		{
			Post = *It;
			BaseExposureBias = It->Settings.bOverride_AutoExposureBias ? It->Settings.AutoExposureBias : 0.f;
			break;
		}
	}
}

void AIslandDayNight::BuildStars()
{
	Stars->ClearInstances();
	if (!StarMesh || !StarMaterial || StarCount <= 0)
	{
		Stars->SetVisibility(false);
		bStarsShown = false;
		return;
	}
	Stars->SetStaticMesh(StarMesh);
	StarMID = UMaterialInstanceDynamic::Create(StarMaterial, this);
	Stars->SetMaterial(0, StarMID);
	Stars->SetWorldLocationAndRotation(FVector::ZeroVector, FQuat::Identity);

	const float MeshDiameter = FMath::Max(1.f, (float)StarMesh->GetBounds().SphereRadius * 2.f);
	FRandomStream Rng(9137);
	TArray<FTransform> Instances;
	Instances.Reserve(StarCount);
	for (int32 i = 0; i < StarCount; ++i)
	{
		// Uniform over the whole sphere: the sky turns, and the material fades stars out toward the horizon.
		const FVector Dir = Rng.GetUnitVector();
		const float AngularDeg = FMath::Lerp(0.045f, 0.11f, FMath::Square(Rng.GetFraction()));
		const float Diameter = StarDistance * FMath::Tan(FMath::DegreesToRadians(AngularDeg));
		Instances.Emplace(FQuat::Identity, Dir * StarDistance, FVector(Diameter / MeshDiameter));
	}
	Stars->AddInstances(Instances, false, false);
	Stars->SetVisibility(false);
	bStarsShown = false;
}

double AIslandDayNight::ClockNow() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return 0.0;
	}
	const AGameStateBase* GameState = World->GetGameState();
	return GameState ? GameState->GetServerWorldTimeSeconds() : World->GetTimeSeconds();
}

float AIslandDayNight::GetHours() const
{
	if (bClockFrozen)
	{
		return BaseHours;
	}
	const double Hours = BaseHours + (ClockNow() - BaseTime) * HoursPerSecond;
	return (float)FMath::Fmod(FMath::Fmod(Hours, 24.0) + 24.0, 24.0);
}

float AIslandDayNight::GetSunElevation() const
{
	return SunElevation;
}

float AIslandDayNight::GetNightAmount() const
{
	return 1.f - FMath::SmoothStep(-10.f, 0.f, SunElevation);
}

void AIslandDayNight::SetTimeOfDay(float Hours, bool bFreeze)
{
	if (!HasAuthority())
	{
		return;
	}
	BaseHours = FMath::Fmod(FMath::Fmod(Hours, 24.f) + 24.f, 24.f);
	BaseTime = ClockNow();
	bClockFrozen = bFreeze;
	Apply(BaseHours);
	SinceUpdate = 0.f;
}

FVector AIslandDayNight::SunDirection(float Hours) const
{
	// The sun runs a tilted great circle: rises at SunriseYaw at 06:00, peaks at NoonElevation at 12:00 (a
	// quarter turn round the compass from sunrise), sets opposite at 18:00, and is as far below at midnight.
	const float Theta = (Hours - 6.f) / 24.f * UE_TWO_PI;
	const float Phi = FMath::DegreesToRadians(NoonElevation);
	const FVector East = FRotator(0.f, SunriseYaw, 0.f).Vector();
	const FVector Side = FRotator(0.f, SunriseYaw + 90.f, 0.f).Vector();
	const FVector NoonDir = FVector::UpVector * FMath::Sin(Phi) + Side * FMath::Cos(Phi);
	return (East * FMath::Cos(Theta) + NoonDir * FMath::Sin(Theta)).GetSafeNormal();
}

void AIslandDayNight::Apply(float Hours)
{
	LastAppliedHours = Hours;
	const FVector SunDir = SunDirection(Hours);
	const float Elev = IslandDN::ElevationDeg(SunDir);
	SunElevation = Elev;
	const float Day = FMath::SmoothStep(-6.f, 12.f, Elev);
	const float Night = 1.f - FMath::SmoothStep(-12.f, 0.f, Elev);

	// Sun: dim and warm near the horizon, whiter and stronger toward noon, gone 10 degrees under.
	if (ADirectionalLight* SunActor = Sun.Get())
	{
		UDirectionalLightComponent* Light = SunActor->GetComponent();
		SunActor->SetActorRotation((-SunDir).Rotation());
		const float Ratio = (float)FMath::Clamp(SunDir.Z / FMath::Sin(FMath::DegreesToRadians((double)NoonElevation)), 0.0, 1.0);
		const float Intensity = SunNoonIntensity * FMath::Max(FMath::Sqrt(Ratio), 0.12f) * FMath::SmoothStep(-10.f, 0.f, Elev);
		Light->SetIntensity(Intensity);
		// The low sun's disc and its glare: much dimmer near the horizon (it blew out the whole west).
		const float Disc = FMath::Lerp(0.12f, 1.f, FMath::SmoothStep(2.f, 30.f, Elev));
		Light->SetAtmosphereSunDiskColorScale(FLinearColor(Disc, Disc, Disc, 1.f));
		Light->SetTemperature(FMath::Lerp(SunHorizonTemperature, SunNoonTemperature, FMath::SmoothStep(0.f, 35.f, Elev)));
		const bool bShow = Elev > -10.5f;
		if (bShow != bSunShown)
		{
			Light->SetVisibility(bShow);
			bSunShown = bShow;
		}
	}

	// Moon: opposite the sun (turned MoonYawOffset round), up while the sun is down.
	const FVector MoonDir = (-SunDir).RotateAngleAxis(MoonYawOffset, FVector::UpVector);
	const float MoonElev = IslandDN::ElevationDeg(MoonDir);
	const float MoonAmount = FMath::SmoothStep(-1.f, 12.f, MoonElev) * (1.f - FMath::SmoothStep(-4.f, 4.f, Elev));
	Moon->SetWorldRotation((-MoonDir).Rotation());
	Moon->SetIntensity(MoonIntensity * MoonAmount);
	Moon->SetLightColor(MoonColor);
	const bool bMoon = MoonAmount > 0.001f;
	if (bMoon != bMoonShown)
	{
		Moon->SetVisibility(bMoon);
		bMoonShown = bMoon;
	}

	// Sky light: the real-time capture follows the atmosphere; lift and cool it at night so it stays readable.
	if (ASkyLight* SkyActor = Sky.Get())
	{
		USkyLightComponent* SkyComp = SkyActor->GetLightComponent();
		SkyComp->SetIntensity(FMath::Lerp(NightSkyIntensity, DaySkyIntensity, Day));
		SkyComp->SetLightColor(FMath::Lerp(NightSkyTint, FLinearColor::White, Day));
		if (!SkyComp->bRealTimeCapture && FMath::Abs(Elev - LastSkyCaptureElevation) > 1.5f)
		{
			SkyComp->RecaptureSky();
			LastSkyCaptureElevation = Elev;
		}
	}

	// Fog: daylight colours by day, deep blue at night, an amber glow toward a low sun.
	if (AExponentialHeightFog* FogActor = Fog.Get())
	{
		UExponentialHeightFogComponent* FogComp = FogActor->GetComponent();
		FogComp->SetFogInscatteringColor(FMath::Lerp(NightFogInscattering, DayFogInscattering, Day));
		const FLinearColor Directional = FMath::Lerp(DuskFogDirectional, DayFogDirectional, FMath::SmoothStep(2.f, 25.f, Elev))
			* FMath::SmoothStep(-6.f, 4.f, Elev) + MoonColor * (0.03f * MoonAmount);
		FogComp->SetDirectionalInscatteringColor(Directional);
	}

	// Exposure: open up at night (manual exposure, so the scene would otherwise go black).
	if (APostProcessVolume* PostVolume = Post.Get())
	{
		PostVolume->Settings.bOverride_AutoExposureBias = true;
		PostVolume->Settings.AutoExposureBias = BaseExposureBias + NightExposureBoost * Night;
	}

	// Stars turn with the sky about the same axis as the sun and come out once it is well down.
	if (StarMID)
	{
		const float StarAmount = 1.f - FMath::SmoothStep(-14.f, -4.f, Elev);
		const bool bShow = StarAmount > 0.001f;
		if (bShow != bStarsShown)
		{
			Stars->SetVisibility(bShow);
			bStarsShown = bShow;
		}
		if (bShow)
		{
			StarMID->SetScalarParameterValue(TEXT("Brightness"), StarBrightness * StarAmount);
			const FVector East = FRotator(0.f, SunriseYaw, 0.f).Vector();
			const FVector Side = FRotator(0.f, SunriseYaw + 90.f, 0.f).Vector();
			const float Phi = FMath::DegreesToRadians(NoonElevation);
			const FVector NoonDir = FVector::UpVector * FMath::Sin(Phi) + Side * FMath::Cos(Phi);
			const FVector Axis = FVector::CrossProduct(East, NoonDir).GetSafeNormal();
			const float Theta = (Hours - 6.f) / 24.f * UE_TWO_PI;
			Stars->SetWorldRotation(FQuat(Axis, Theta));
		}
	}
}

void AIslandDayNight::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bShots)
	{
		TickShots(DeltaSeconds);
	}
	SinceUpdate += DeltaSeconds;
	if (SinceUpdate >= UpdateInterval)
	{
		SinceUpdate = 0.f;
		const float Hours = GetHours();
		if (!bClockFrozen || !FMath::IsNearlyEqual(Hours, LastAppliedHours))
		{
			Apply(Hours);
		}
	}
}

// -DayNightShots: after the world warms up, set each time of day in turn, let Lumen and the sky capture settle,
// screenshot it to Saved/DayNightShots/<name>.png, then quit.
void AIslandDayNight::TickShots(float DeltaSeconds)
{
	ShotClock += DeltaSeconds;
	if (!bShotWarm)
	{
		if (ShotClock < IslandDN::ShotWarmup || IslandDN::ShadersCompiling())
		{
			return;
		}
		bShotWarm = true;
		ShotPhaseStart = ShotClock;
	}
	const int32 NumShots = UE_ARRAY_COUNT(IslandDN::Shots);
	if (ShotIndex >= NumShots)
	{
		if (ShotClock - ShotPhaseStart > 2.f)
		{
			UE_LOG(LogUnrealSoulslike, Display, TEXT("DAYNIGHT_SHOTS_DONE"));
			bShots = false;
			FPlatformMisc::RequestExit(false, TEXT("DayNightShots"));
		}
		return;
	}
	const IslandDN::FShot& Shot = IslandDN::Shots[ShotIndex];
	if (!bShotFramed)
	{
		SetTimeOfDay(Shot.Hours, true);
		if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
		{
			if (PC->MyHUD)
			{
				PC->MyHUD->bShowHUD = false;
			}
			// Look a little up, between the noon sun and the sunset, so sky and ground both show.
			PC->SetControlRotation(FRotator(8.f, SunriseYaw + 145.f, 0.f));
		}
		bShotFramed = true;
		bShotTaken = false;
		ShotPhaseStart = ShotClock;
		return;
	}
	if (!bShotTaken && ShotClock - ShotPhaseStart >= IslandDN::ShotSettle && !IslandDN::ShadersCompiling())
	{
		const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("DayNightShots") / (FString(Shot.Name) + TEXT(".png")));
		FScreenshotRequest::RequestScreenshot(Path, false, false);
		UE_LOG(LogUnrealSoulslike, Display, TEXT("DayNight shot %s at %.2f h (sun elevation %.1f, night %.2f) -> %s"),
			Shot.Name, Shot.Hours, SunElevation, GetNightAmount(), *Path);
		bShotTaken = true;
		ShotPhaseStart = ShotClock;
		return;
	}
	if (bShotTaken && ShotClock - ShotPhaseStart > 1.f)
	{
		++ShotIndex;
		bShotFramed = false;
		ShotPhaseStart = ShotClock;
	}
}

void UIslandDayNightSpawner::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (!InWorld.IsGameWorld() || InWorld.GetNetMode() == NM_Client || FParse::Param(FCommandLine::Get(), TEXT("NoDayNight")))
	{
		return;
	}
	if (!InWorld.GetMapName().Contains(TEXT("Lvl_Isle")))
	{
		return;
	}
	TActorIterator<AIslandDayNight> Existing(&InWorld);
	if (Existing)
	{
		return;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	InWorld.SpawnActor<AIslandDayNight>(AIslandDayNight::StaticClass(), FTransform::Identity, Params);
	UE_LOG(LogUnrealSoulslike, Log, TEXT("DayNight: spawned a cycle actor (none placed in %s)"), *InWorld.GetMapName());
}
