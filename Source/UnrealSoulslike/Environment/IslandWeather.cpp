#include "IslandWeather.h"

#include "Camera/PlayerCameraManager.h"
#include "CollisionQueryParams.h"
#include "Components/AudioComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/HitResult.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/HUD.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMisc.h"
#include "IslandDayNight.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "Math/RandomStream.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Net/UnrealNetwork.h"
#include "Sound/SoundWaveProcedural.h"
#include "UObject/ConstructorHelpers.h"
#include "UnrealClient.h"
#include "UnrealSoulslike.h"

#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif

namespace IslandWX
{
	static const TCHAR* MpcPath = TEXT("/Game/Island/Materials/MPC_Weather.MPC_Weather");
	static const TCHAR* RainMatPath = TEXT("/Game/Island/Materials/Weather/M_WeatherRain.M_WeatherRain");
	static const TCHAR* SplashMatPath = TEXT("/Game/Island/Materials/Weather/M_WeatherSplash.M_WeatherSplash");
	static const TCHAR* LeafMatPath = TEXT("/Game/Island/Materials/Weather/M_WeatherLeaf.M_WeatherLeaf");

	// The wind the field fire always used, for worlds without a weather actor.
	static const FVector DefaultWind(0.6, 0.8, 0.0);

	static constexpr int32 AudioRate = 22050;

	struct FShot
	{
		const TCHAR* Name;
		float Overcast;
		float Rain;
		float Wet;
		float Gust;     // < 0: the natural gusts
		float Pitch;    // camera pitch
		float YawFromWind; // camera yaw relative to the wind direction at the start of the shots
		float Settle;   // seconds between framing and the screenshot
		bool bTrees;    // stand by the nearest tree first
	};

	// Clear sky; the clouds twice, 8 s apart (same view, so the drift shows); a front arriving (looking upwind,
	// where it comes from); rain; the wet ground underfoot; leaves blowing off a tree in a gust.
	static const FShot Shots[] = {
		{ TEXT("Weather_Clear"), 0.f, 0.f, 0.f, -1.f, 6.f, 90.f, 5.f, false },
		{ TEXT("Weather_CloudsA"), 0.f, 0.f, 0.f, -1.f, 30.f, 90.f, 3.f, false },
		{ TEXT("Weather_CloudsB"), 0.f, 0.f, 0.f, -1.f, 30.f, 90.f, 8.f, false },
		{ TEXT("Weather_RainArriving"), 0.7f, 0.3f, 0.25f, -1.f, 12.f, 180.f, 7.f, false },
		{ TEXT("Weather_Rain"), 1.f, 1.f, 1.f, -1.f, -2.f, 90.f, 5.f, false },
		{ TEXT("Weather_WetGround"), 1.f, 0.6f, 1.f, -1.f, -40.f, 90.f, 3.f, false },
		{ TEXT("Weather_Leaves"), 0.15f, 0.f, 0.2f, 1.f, 10.f, 0.f, 8.f, true },
	};
	static constexpr float ShotWarmup = 14.f;

	static bool ShadersCompiling()
	{
#if WITH_EDITOR
		return GShaderCompilingManager && GShaderCompilingManager->GetNumRemainingJobs() > 0;
#else
		return false;
#endif
	}

	// Seamless loop: the tail (Fade samples past Length) is crossfaded into the head; normalised to Peak.
	static TArray<int16> FinishLoop(TArray<float>& Raw, int32 Length, int32 Fade, float Peak)
	{
		for (int32 i = 0; i < Fade; ++i)
		{
			const float T = (float)i / (float)Fade;
			Raw[i] = Raw[i] * T + Raw[Length + i] * (1.f - T);
		}
		float Max = 1.0e-4f;
		for (int32 i = 0; i < Length; ++i)
		{
			Max = FMath::Max(Max, FMath::Abs(Raw[i]));
		}
		TArray<int16> Out;
		Out.SetNumUninitialized(Length);
		const float Gain = Peak / Max * 32767.f;
		for (int32 i = 0; i < Length; ++i)
		{
			Out[i] = (int16)FMath::Clamp(Raw[i] * Gain, -32767.f, 32767.f);
		}
		return Out;
	}

	// Rain: a hiss (band of white noise), a soft low roar, and hundreds of droplet ticks a second.
	static const TArray<int16>& RainLoop()
	{
		static const TArray<int16> Loop = []()
		{
			const int32 Length = AudioRate * 5;
			const int32 Fade = AudioRate / 2;
			const int32 Total = Length + Fade;
			TArray<float> Raw;
			Raw.SetNumZeroed(Total);
			FRandomStream Rng(4242);
			float Lp1 = 0.f, Lp2 = 0.f, Lp3 = 0.f;
			for (int32 i = 0; i < Total; ++i)
			{
				const float W = Rng.FRandRange(-1.f, 1.f);
				Lp1 += (W - Lp1) * 0.45f;
				Lp2 += (W - Lp2) * 0.06f;
				Lp3 += (W - Lp3) * 0.012f;
				Raw[i] = (Lp1 - Lp2) * 0.5f + (Lp2 - Lp3) * 0.7f + Lp3 * 1.4f;
			}
			const int32 Drops = (Total / AudioRate) * 260;
			for (int32 d = 0; d < Drops; ++d)
			{
				const int32 Start = Rng.RandRange(0, Total - 1);
				const float Amp = FMath::Square(Rng.FRand()) * 0.9f + 0.05f;
				const float Decay = Rng.FRandRange(0.0015f, 0.005f) * AudioRate;
				const int32 Len = FMath::Min((int32)(Decay * 6.f), Total - Start);
				float Prev = 0.f;
				for (int32 k = 0; k < Len; ++k)
				{
					const float W = Rng.FRandRange(-1.f, 1.f);
					Raw[Start + k] += (W - Prev) * 0.5f * Amp * FMath::Exp(-(float)k / Decay);
					Prev = W;
				}
			}
			return FinishLoop(Raw, Length, Fade, 0.85f);
		}();
		return Loop;
	}

	// Wind: deep brown-noise roar breathing slowly, with a faint wandering whistle over it.
	static const TArray<int16>& WindLoop()
	{
		static const TArray<int16> Loop = []()
		{
			const int32 Length = AudioRate * 7;
			const int32 Fade = AudioRate;
			const int32 Total = Length + Fade;
			TArray<float> Roar, Whistle;
			Roar.SetNumZeroed(Total);
			Whistle.SetNumZeroed(Total);
			FRandomStream Rng(777);
			float Brown = 0.f, Lp = 0.f, Hp = 0.f, Y1 = 0.f, Y2 = 0.f;
			float MaxRoar = 1.0e-4f, MaxWhistle = 1.0e-4f;
			for (int32 i = 0; i < Total; ++i)
			{
				const float T = (float)i / AudioRate;
				const float W = Rng.FRandRange(-1.f, 1.f);
				Brown = Brown * 0.996f + W * 0.08f;
				Lp += (Brown - Lp) * 0.08f;
				Hp += (Lp - Hp) * 0.002f;
				const float Breath = 0.6f + 0.25f * FMath::Sin(T * 0.9f) + 0.15f * FMath::Sin(T * 2.3f + 1.f);
				Roar[i] = (Lp - Hp) * Breath;
				MaxRoar = FMath::Max(MaxRoar, FMath::Abs(Roar[i]));
				// Two-pole resonator on white noise, its pitch wandering.
				const float Freq = 480.f + 160.f * FMath::Sin(T * 0.7f) + 60.f * FMath::Sin(T * 2.9f);
				const float R = 0.996f;
				const float A1 = 2.f * R * FMath::Cos(2.f * PI * Freq / AudioRate);
				const float Y = A1 * Y1 - R * R * Y2 + W * 0.01f;
				Y2 = Y1;
				Y1 = Y;
				Whistle[i] = Y * (0.5f + 0.5f * FMath::Sin(T * 0.5f + 2.f));
				MaxWhistle = FMath::Max(MaxWhistle, FMath::Abs(Whistle[i]));
			}
			TArray<float> Raw;
			Raw.SetNumZeroed(Total);
			for (int32 i = 0; i < Total; ++i)
			{
				Raw[i] = Roar[i] / MaxRoar + 0.14f * Whistle[i] / MaxWhistle;
			}
			return FinishLoop(Raw, Length, Fade, 0.8f);
		}();
		return Loop;
	}

	static USoundWaveProcedural* MakeLoopWave(UObject* Outer, const TArray<int16>& (*Loop)())
	{
		USoundWaveProcedural* Wave = NewObject<USoundWaveProcedural>(Outer);
		Wave->SetSampleRate(AudioRate);
		Wave->NumChannels = 1;
		Wave->Duration = INDEFINITELY_LOOPING_DURATION;
		Wave->SoundGroup = SOUNDGROUP_Default;
		Wave->bLooping = false;
		Wave->OnSoundWaveProceduralUnderflow.BindLambda([Loop](USoundWaveProcedural* InWave, int32 SamplesRequired)
		{
			const TArray<int16>& Samples = Loop();
			InWave->QueueAudio(reinterpret_cast<const uint8*>(Samples.GetData()), Samples.Num() * sizeof(int16));
		});
		const TArray<int16>& Samples = Loop();
		Wave->QueueAudio(reinterpret_cast<const uint8*>(Samples.GetData()), Samples.Num() * sizeof(int16));
		return Wave;
	}

	static UInstancedStaticMeshComponent* MakeIsm(AActor* Owner, USceneComponent* Parent, const TCHAR* Name)
	{
		UInstancedStaticMeshComponent* Ism = Owner->CreateDefaultSubobject<UInstancedStaticMeshComponent>(Name);
		Ism->SetupAttachment(Parent);
		Ism->SetMobility(EComponentMobility::Movable);
		Ism->SetUsingAbsoluteLocation(true);
		Ism->SetUsingAbsoluteRotation(true);
		Ism->SetUsingAbsoluteScale(true);
		Ism->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Ism->SetCanEverAffectNavigation(false);
		Ism->SetCastShadow(false);
		Ism->bCastDynamicShadow = false;
		Ism->bAffectDynamicIndirectLighting = false;
		Ism->bAffectDistanceFieldLighting = false;
		Ism->bVisibleInRayTracing = false;
		Ism->bVisibleInReflectionCaptures = false;
		Ism->bVisibleInRealTimeSkyCaptures = false;
		Ism->bReceivesDecals = false;
		Ism->SetVisibility(false);
		return Ism;
	}

	// Picks a parameter: the exact name if the material has it, else the first whose name contains Part (and
	// none of Avoid).
	static FName PickParam(const TArray<FMaterialParameterInfo>& Infos, const TCHAR* Exact, const TCHAR* Part, const TCHAR* Avoid = nullptr)
	{
		for (const FMaterialParameterInfo& Info : Infos)
		{
			if (Info.Name == FName(Exact))
			{
				return Info.Name;
			}
		}
		for (const FMaterialParameterInfo& Info : Infos)
		{
			const FString Name = Info.Name.ToString();
			if (Name.Contains(Part) && (!Avoid || !Name.Contains(Avoid)))
			{
				return Info.Name;
			}
		}
		return NAME_None;
	}
}

AIslandWeather::AIslandWeather()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetNetUpdateFrequency(1.f);

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	RainDrops = IslandWX::MakeIsm(this, Root, TEXT("RainDrops"));
	RainDrops->BoundsScale = 2.5f;
	Splashes = IslandWX::MakeIsm(this, Root, TEXT("Splashes"));
	LeafCloud = IslandWX::MakeIsm(this, Root, TEXT("LeafCloud"));

	WeatherPost = CreateDefaultSubobject<UPostProcessComponent>(TEXT("WeatherPost"));
	WeatherPost->SetupAttachment(Root);
	WeatherPost->bUnbound = true;
	WeatherPost->Priority = 50.f;
	WeatherPost->BlendWeight = 0.f;

	RainAudio = CreateDefaultSubobject<UAudioComponent>(TEXT("RainAudio"));
	RainAudio->SetupAttachment(Root);
	RainAudio->bAutoActivate = false;
	RainAudio->bAllowSpatialization = false;
	RainAudio->bIsUISound = false;

	WindAudio = CreateDefaultSubobject<UAudioComponent>(TEXT("WindAudio"));
	WindAudio->SetupAttachment(Root);
	WindAudio->bAutoActivate = false;
	WindAudio->bAllowSpatialization = false;
	WindAudio->bIsUISound = false;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderFinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneFinder(TEXT("/Engine/BasicShapes/Plane.Plane"));
	RainMesh = CylinderFinder.Object;
	PlaneMesh = PlaneFinder.Object;

	TreeMeshKeywords.Add(TEXT("island_tree"));
}

void AIslandWeather::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AIslandWeather, Mode);
	DOREPLIFETIME(AIslandWeather, CycleStart);
	DOREPLIFETIME(AIslandWeather, Seed);
}

AIslandWeather* AIslandWeather::Get(const UObject* WorldContextObject)
{
	const UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	if (!World)
	{
		return nullptr;
	}
	const UIslandWeatherSubsystem* Sub = World->GetSubsystem<UIslandWeatherSubsystem>();
	return Sub ? Sub->Weather.Get() : nullptr;
}

FVector AIslandWeather::GetWind(const UObject* WorldContextObject)
{
	const AIslandWeather* Weather = Get(WorldContextObject);
	return Weather ? Weather->GetWindVector() : IslandWX::DefaultWind;
}

FVector AIslandWeather::GetWindDirection(const UObject* WorldContextObject)
{
	const AIslandWeather* Weather = Get(WorldContextObject);
	return Weather ? Weather->CurWindDir : IslandWX::DefaultWind;
}

float AIslandWeather::GetWindStrength(const UObject* WorldContextObject)
{
	const AIslandWeather* Weather = Get(WorldContextObject);
	return Weather ? Weather->CurWindStrength : 1.f;
}

float AIslandWeather::GetGust(const UObject* WorldContextObject)
{
	const AIslandWeather* Weather = Get(WorldContextObject);
	return Weather ? Weather->CurGust : 0.f;
}

float AIslandWeather::GetRainAmount(const UObject* WorldContextObject)
{
	const AIslandWeather* Weather = Get(WorldContextObject);
	return Weather ? Weather->CurRain : 0.f;
}

float AIslandWeather::GetWetness(const UObject* WorldContextObject)
{
	const AIslandWeather* Weather = Get(WorldContextObject);
	return Weather ? Weather->CurWetness : 0.f;
}

float AIslandWeather::GetOvercast(const UObject* WorldContextObject)
{
	const AIslandWeather* Weather = Get(WorldContextObject);
	return Weather ? Weather->CurOvercast : 0.f;
}

void AIslandWeather::SetWeatherMode(EIslandWeatherMode NewMode)
{
	if (HasAuthority())
	{
		Mode = NewMode;
		CycleStart = ClockNow();
	}
}

double AIslandWeather::ClockNow() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return 0.0;
	}
	const AGameStateBase* GameState = World->GetGameState();
	return GameState ? GameState->GetServerWorldTimeSeconds() : World->GetTimeSeconds();
}

void AIslandWeather::BeginPlay()
{
	Super::BeginPlay();

	if (UIslandWeatherSubsystem* Sub = GetWorld()->GetSubsystem<UIslandWeatherSubsystem>())
	{
		if (!Sub->Weather.IsValid())
		{
			Sub->Weather = this;
		}
	}

	if (FParse::Param(FCommandLine::Get(), TEXT("NoWeather")))
	{
		bOff = true;
		SetActorTickEnabled(false);
		UE_LOG(LogUnrealSoulslike, Log, TEXT("Weather: off (-NoWeather)"));
		return;
	}

	if (HasAuthority())
	{
		CycleStart = ClockNow();
		FParse::Value(FCommandLine::Get(), TEXT("WeatherSeed="), Seed);
		if (FParse::Param(FCommandLine::Get(), TEXT("WeatherRain")))
		{
			Mode = EIslandWeatherMode::Rain;
		}
		else if (FParse::Param(FCommandLine::Get(), TEXT("WeatherClear")))
		{
			Mode = EIslandWeatherMode::Clear;
		}
		bShots = FParse::Param(FCommandLine::Get(), TEXT("WeatherShots"));
	}
	FParse::Value(FCommandLine::Get(), TEXT("CloudDrift="), CloudDriftScale);

	const double Now = ClockNow();
	EvaluateSky(Now, CurOvercast, CurRain);
	EvaluateWind(Now, CurOvercast, CurWindDir, CurWindStrength, CurGust);
	CurWetness = Mode == EIslandWeatherMode::Rain ? 1.f : 0.f;

	bVisuals = GetNetMode() != NM_DedicatedServer;
	if (bVisuals)
	{
		LoadAssets();
		FindSceneActors();
		SetupClouds();
		SetupRain();
		SetupSplashes();
		GatherTrees();
		SetupLeaves();
		SetupAudio();
		UpdateMaterialParameters(0.f);
	}

	const TCHAR* ModeName = Mode == EIslandWeatherMode::Rain ? TEXT("rain") : Mode == EIslandWeatherMode::Clear ? TEXT("clear") : TEXT("cycle");
	UE_LOG(LogUnrealSoulslike, Log,
		TEXT("Weather: %s (seed %d; %.0f s clear, %.0f s build-up, %.0f s rain, %.0f s clearing), wind (%.2f, %.2f) x %.2f, MPC %s, rain %s/%d, splash %s, leaf %s, %d leafy trees, sun %s, fog %s, clouds %s, cloud drift x%.1f%s"),
		ModeName, Seed, ClearSeconds, BuildUpSeconds, RainSeconds, ClearingSeconds, CurWindDir.X, CurWindDir.Y, CurWindStrength,
		ParameterCollection ? TEXT("ok") : TEXT("MISSING"), RainMaterial ? TEXT("ok") : TEXT("MISSING"), RainDrops->GetInstanceCount(),
		SplashMaterial ? TEXT("ok") : TEXT("MISSING"), LeafMaterial ? TEXT("ok") : TEXT("MISSING"), Canopies.Num(),
		Sun.IsValid() ? TEXT("found") : TEXT("missing"), Fog.IsValid() ? TEXT("found") : TEXT("missing"),
		CloudMID ? TEXT("found") : TEXT("missing"), CloudDriftScale, bShots ? TEXT(", -WeatherShots") : TEXT(""));
}

void AIslandWeather::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (RainWave)
	{
		RainWave->OnSoundWaveProceduralUnderflow.Unbind();
	}
	if (WindWave)
	{
		WindWave->OnSoundWaveProceduralUnderflow.Unbind();
	}
	if (RainAudio)
	{
		RainAudio->Stop();
	}
	if (WindAudio)
	{
		WindAudio->Stop();
	}
	if (UDirectionalLightComponent* Light = Sun.Get())
	{
		Light->SetLightColor(SunBaseColor);
	}
	if (UExponentialHeightFogComponent* FogComp = Fog.Get())
	{
		FogComp->SetFogDensity(FogBaseDensity);
	}
	if (UWorld* World = GetWorld())
	{
		if (UIslandWeatherSubsystem* Sub = World->GetSubsystem<UIslandWeatherSubsystem>())
		{
			if (Sub->Weather.Get() == this)
			{
				Sub->Weather.Reset();
			}
		}
	}
	Super::EndPlay(EndPlayReason);
}

void AIslandWeather::LoadAssets()
{
	if (!ParameterCollection)
	{
		ParameterCollection = LoadObject<UMaterialParameterCollection>(nullptr, IslandWX::MpcPath, nullptr, LOAD_NoWarn);
	}
	if (!RainMaterial)
	{
		RainMaterial = LoadObject<UMaterialInterface>(nullptr, IslandWX::RainMatPath, nullptr, LOAD_NoWarn);
	}
	if (!SplashMaterial)
	{
		SplashMaterial = LoadObject<UMaterialInterface>(nullptr, IslandWX::SplashMatPath, nullptr, LOAD_NoWarn);
	}
	if (!LeafMaterial)
	{
		LeafMaterial = LoadObject<UMaterialInterface>(nullptr, IslandWX::LeafMatPath, nullptr, LOAD_NoWarn);
	}
}

void AIslandWeather::FindSceneActors()
{
	UWorld* World = GetWorld();
	for (TActorIterator<ADirectionalLight> It(World); It; ++It)
	{
		UDirectionalLightComponent* Light = Cast<UDirectionalLightComponent>(It->GetLightComponent());
		if (!Light)
		{
			continue;
		}
		// The atmosphere's primary sun, as AIslandDayNight picks it.
		if (!Sun.IsValid() || (Light->bAtmosphereSunLight && Light->AtmosphereSunLightIndex == 0))
		{
			Sun = Light;
		}
	}
	if (const UDirectionalLightComponent* Light = Sun.Get())
	{
		SunBaseColor = Light->GetLightColor();
	}
	if (TActorIterator<AExponentialHeightFog> FogIt(World); FogIt)
	{
		Fog = FogIt->GetComponent();
		if (const UExponentialHeightFogComponent* FogComp = Fog.Get())
		{
			FogBaseDensity = FogComp->FogDensity;
		}
	}
}

void AIslandWeather::SetupClouds()
{
	for (TActorIterator<AVolumetricCloud> It(GetWorld()); It; ++It)
	{
		if (UVolumetricCloudComponent* Comp = It->FindComponentByClass<UVolumetricCloudComponent>())
		{
			Cloud = Comp;
			break;
		}
	}
	UVolumetricCloudComponent* Comp = Cloud.Get();
	UMaterialInterface* Base = Comp ? Comp->GetMaterial() : nullptr;
	if (!Base)
	{
		UE_LOG(LogUnrealSoulslike, Warning, TEXT("Weather: no volumetric cloud material; clouds won't change"));
		return;
	}
	CloudMID = UMaterialInstanceDynamic::Create(Base, this);
	Comp->SetMaterial(CloudMID);
	const UMaterialInterface* AsInterface = CloudMID;

	TArray<FMaterialParameterInfo> Scalars, Vectors;
	TArray<FGuid> Ids;
	AsInterface->GetAllScalarParameterInfo(Scalars, Ids);
	AsInterface->GetAllVectorParameterInfo(Vectors, Ids);

	FString Listing;
	for (const FMaterialParameterInfo& Info : Scalars)
	{
		float Value = 0.f;
		AsInterface->GetScalarParameterValue(FHashedMaterialParameterInfo(Info), Value);
		Listing += FString::Printf(TEXT(" %s=%.4g"), *Info.Name.ToString(), Value);
	}
	for (const FMaterialParameterInfo& Info : Vectors)
	{
		FLinearColor Value = FLinearColor::Black;
		AsInterface->GetVectorParameterValue(FHashedMaterialParameterInfo(Info), Value);
		Listing += FString::Printf(TEXT(" %s=(%.3g,%.3g,%.3g,%.3g)"), *Info.Name.ToString(), Value.R, Value.G, Value.B, Value.A);
	}
	UE_LOG(LogUnrealSoulslike, Log, TEXT("Weather: cloud material %s parameters:%s"), *Base->GetName(), *Listing);

	CoverageParam = IslandWX::PickParam(Scalars, TEXT("Cloud_GlobalCoverage"), TEXT("Coverage"));
	DensityParam = IslandWX::PickParam(Scalars, TEXT("Cloud_GlobalDensity"), TEXT("Density"), TEXT("Conservative"));
	AlbedoParam = IslandWX::PickParam(Vectors, TEXT("Cloud_AlbedoColor"), TEXT("Albedo"), TEXT("Storm"));
	StormAlbedoParam = IslandWX::PickParam(Vectors, TEXT("Storm_AlbedoColor"), TEXT("StormAlbedo"));
	if (!CoverageParam.IsNone())
	{
		AsInterface->GetScalarParameterValue(FHashedMaterialParameterInfo(CoverageParam), BaseCoverage);
	}
	if (!DensityParam.IsNone())
	{
		AsInterface->GetScalarParameterValue(FHashedMaterialParameterInfo(DensityParam), BaseDensity);
	}
	if (!AlbedoParam.IsNone())
	{
		AsInterface->GetVectorParameterValue(FHashedMaterialParameterInfo(AlbedoParam), BaseAlbedo);
	}
	if (!StormAlbedoParam.IsNone())
	{
		AsInterface->GetVectorParameterValue(FHashedMaterialParameterInfo(StormAlbedoParam), BaseStormAlbedo);
	}

	// Drift: turn the material's own cloud wind to the prevailing wind and speed it up. Set once: the material
	// integrates wind * time itself, so changing it later would jump the whole sky.
	const FName WindVecParam = IslandWX::PickParam(Vectors, TEXT("CloudWind"), TEXT("Wind"));
	const FName WindScalarParam = IslandWX::PickParam(Scalars, TEXT("WindMultiplier"), TEXT("Wind"));
	const FVector2D Dir = PrevailingWind.GetSafeNormal();
	FString Drift = TEXT("none");
	if (!WindVecParam.IsNone())
	{
		FLinearColor Wind = FLinearColor::Black;
		AsInterface->GetVectorParameterValue(FHashedMaterialParameterInfo(WindVecParam), Wind);
		const float Speed = FMath::Sqrt(Wind.R * Wind.R + Wind.G * Wind.G);
		const float NewSpeed = (Speed > 1.0e-4f ? Speed : 0.1f) * CloudDriftScale;
		const FLinearColor NewWind((float)Dir.X * NewSpeed, (float)Dir.Y * NewSpeed, Wind.B, Wind.A);
		CloudMID->SetVectorParameterValue(WindVecParam, NewWind);
		Drift = FString::Printf(TEXT("%s (%.3g,%.3g,%.3g) -> (%.3g,%.3g,%.3g)"), *WindVecParam.ToString(), Wind.R, Wind.G, Wind.B, NewWind.R, NewWind.G, NewWind.B);
	}
	else if (!WindScalarParam.IsNone())
	{
		float Mult = 1.f;
		AsInterface->GetScalarParameterValue(FHashedMaterialParameterInfo(WindScalarParam), Mult);
		CloudMID->SetScalarParameterValue(WindScalarParam, Mult * CloudDriftScale);
		Drift = FString::Printf(TEXT("%s %.3g -> %.3g"), *WindScalarParam.ToString(), Mult, Mult * CloudDriftScale);
	}
	UE_LOG(LogUnrealSoulslike, Log, TEXT("Weather: clouds coverage %s (%.3g), density %s (%.3g), albedo %s, storm albedo %s, drift %s"),
		*CoverageParam.ToString(), BaseCoverage, *DensityParam.ToString(), BaseDensity, *AlbedoParam.ToString(), *StormAlbedoParam.ToString(), *Drift);
}

void AIslandWeather::SetupRain()
{
	RainDrops->ClearInstances();
	if (!RainMesh || !RainMaterial || RainCount <= 0)
	{
		return;
	}
	RainDrops->SetStaticMesh(RainMesh);
	RainDrops->SetMaterial(0, RainMaterial);
	const FBox Box = RainMesh->GetBoundingBox();
	const FVector Size = Box.GetSize();
	const float ScaleXY = RainStreakWidth / FMath::Max(1.f, (float)Size.X);
	const float ScaleZ = RainStreakLength / FMath::Max(1.f, (float)Size.Z);
	// Where the streak's middle sits relative to its pivot, and its half length (for the material's taper).
	RainStreakShape = FVector2D(Box.GetCenter().Z * ScaleZ, RainStreakLength * 0.5f);
	FRandomStream Rng(31337);
	TArray<FTransform> Instances;
	Instances.Reserve(RainCount);
	for (int32 i = 0; i < RainCount; ++i)
	{
		const FVector Local(Rng.FRand() * RainBox.X, Rng.FRand() * RainBox.Y, Rng.FRand() * RainBox.Z);
		Instances.Emplace(FQuat::Identity, Local, FVector(ScaleXY, ScaleXY, ScaleZ));
	}
	RainDrops->AddInstances(Instances, false, false, false);
	RainDrops->SetVisibility(false);
	bRainShown = false;
}

void AIslandWeather::SetupSplashes()
{
	Splashes->ClearInstances();
	SplashPool.Reset();
	SplashXf.Reset();
	if (!PlaneMesh || !SplashMaterial || SplashCount <= 0)
	{
		return;
	}
	Splashes->SetStaticMesh(PlaneMesh);
	Splashes->SetMaterial(0, SplashMaterial);
	Splashes->SetNumCustomDataFloats(1);
	SplashPlaneSize = FMath::Max(1.f, (float)PlaneMesh->GetBoundingBox().GetSize().X);
	SplashPool.SetNum(SplashCount);
	SplashXf.Init(FTransform(FQuat::Identity, FVector::ZeroVector, FVector(0.001)), SplashCount);
	Splashes->AddInstances(SplashXf, false, false, false);
	Splashes->SetVisibility(false);
}

void AIslandWeather::SetupLeaves()
{
	LeafCloud->ClearInstances();
	LeafPool.Reset();
	LeafXf.Reset();
	if (!PlaneMesh || !LeafMaterial || LeafCount <= 0)
	{
		return;
	}
	LeafCloud->SetStaticMesh(PlaneMesh);
	LeafCloud->SetMaterial(0, LeafMaterial);
	LeafPlaneSize = FMath::Max(1.f, (float)PlaneMesh->GetBoundingBox().GetSize().X);
	LeafPool.SetNum(LeafCount);
	LeafXf.Init(FTransform(FQuat::Identity, FVector::ZeroVector, FVector(0.001)), LeafCount);
	LeafCloud->AddInstances(LeafXf, false, false, false);
	LeafCloud->SetVisibility(false);
}

void AIslandWeather::GatherTrees()
{
	Canopies.Reset();
	if (TreeMeshKeywords.IsEmpty())
	{
		return;
	}
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		if (*It == this)
		{
			continue;
		}
		TInlineComponentArray<UStaticMeshComponent*> Comps(*It);
		for (const UStaticMeshComponent* Comp : Comps)
		{
			const UStaticMesh* Mesh = Comp ? Comp->GetStaticMesh() : nullptr;
			if (!Mesh)
			{
				continue;
			}
			const FString MeshName = Mesh->GetName();
			bool bTree = false;
			for (const FString& Key : TreeMeshKeywords)
			{
				bTree |= MeshName.Contains(Key);
			}
			if (!bTree)
			{
				continue;
			}
			// The canopy: the upper part of the mesh's box, round its middle.
			const FBox Box = Mesh->GetBoundingBox();
			const FVector Centre = Box.GetCenter();
			const FVector Ext = Box.GetExtent();
			const FVector LocalCanopy(Centre.X, Centre.Y, Box.Min.Z + (Box.Max.Z - Box.Min.Z) * 0.68);
			auto Add = [&](const FTransform& Xf)
			{
				FCanopy Canopy;
				Canopy.Center = Xf.TransformPosition(LocalCanopy);
				const float Scale = (float)Xf.GetMaximumAxisScale();
				Canopy.Radius = (float)FMath::Max(Ext.X, Ext.Y) * 0.55f * Scale;
				Canopy.Height = (float)Ext.Z * 0.45f * Scale;
				Canopies.Add(Canopy);
			};
			if (const UInstancedStaticMeshComponent* Ism = Cast<UInstancedStaticMeshComponent>(Comp))
			{
				const int32 Num = Ism->GetInstanceCount();
				for (int32 i = 0; i < Num; ++i)
				{
					FTransform Xf;
					if (Ism->GetInstanceTransform(i, Xf, true))
					{
						Add(Xf);
					}
				}
			}
			else
			{
				Add(Comp->GetComponentTransform());
			}
		}
	}
}

void AIslandWeather::SetupAudio()
{
	RainWave = IslandWX::MakeLoopWave(this, &IslandWX::RainLoop);
	WindWave = IslandWX::MakeLoopWave(this, &IslandWX::WindLoop);
	RainAudio->SetSound(RainWave);
	WindAudio->SetSound(WindWave);
	RainAudio->SetVolumeMultiplier(0.001f);
	WindAudio->SetVolumeMultiplier(0.001f);
	RainAudio->Play();
	WindAudio->Play();
}

void AIslandWeather::EvaluateSky(double Now, float& OutOvercast, float& OutRain) const
{
	if (Mode == EIslandWeatherMode::Rain)
	{
		OutOvercast = 1.f;
		OutRain = 1.f;
		return;
	}
	if (Mode == EIslandWeatherMode::Clear)
	{
		OutOvercast = 0.f;
		OutRain = 0.f;
		return;
	}
	const float Build = FMath::Max(BuildUpSeconds, 1.f);
	const float Clearing = FMath::Max(ClearingSeconds, 1.f);
	const float Period = ClearSeconds + Build + RainSeconds + Clearing;
	const double Elapsed = FMath::Max(0.0, Now - CycleStart);
	const int64 CycleIndex = (int64)FMath::FloorToDouble(Elapsed / Period);
	const float U = (float)(Elapsed - (double)CycleIndex * Period);
	float Env = 0.f;
	if (U < ClearSeconds)
	{
		Env = 0.f;
	}
	else if (U < ClearSeconds + Build)
	{
		Env = FMath::SmoothStep(0.f, 1.f, (U - ClearSeconds) / Build);
	}
	else if (U < ClearSeconds + Build + RainSeconds)
	{
		Env = 1.f;
	}
	else
	{
		Env = 1.f - FMath::SmoothStep(0.f, 1.f, (U - ClearSeconds - Build - RainSeconds) / Clearing);
	}
	// Every front is a little different: some a drizzle under grey, some a downpour under black.
	FRandomStream Rng(Seed + (int32)(CycleIndex % 100000));
	const float Peak = FMath::Lerp(0.65f, 1.f, Rng.FRand());
	OutOvercast = Env * FMath::Lerp(0.85f, 1.f, Peak);
	OutRain = FMath::SmoothStep(0.55f, 0.95f, Env) * Peak;
}

void AIslandWeather::EvaluateWind(double Now, float OvercastNow, FVector& OutDir, float& OutStrength, float& OutGust) const
{
	const float T = (float)FMath::Fmod(Now, 20000.0);
	const float BaseYaw = FMath::Atan2((float)PrevailingWind.Y, (float)PrevailingWind.X);
	const float Veer = FMath::DegreesToRadians(WindVeerDegrees * FMath::PerlinNoise1D(T * 0.035f + 3.7f) + FrontVeerDegrees * OvercastNow);
	OutDir = FVector(FMath::Cos(BaseYaw + Veer), FMath::Sin(BaseYaw + Veer), 0.f);
	const float Raw = FMath::PerlinNoise1D(T * 0.23f + 11.3f) * 0.7f + FMath::PerlinNoise1D(T * 0.71f + 5.1f) * 0.3f;
	OutGust = FMath::Clamp(Raw * 1.8f + 0.1f + 0.35f * OvercastNow, 0.f, 1.f);
	if (Forced.bActive && Forced.Gust >= 0.f)
	{
		OutGust = Forced.Gust;
	}
	OutStrength = FMath::Min((BaseWindStrength + FrontWindBoost * OvercastNow) * (0.7f + 0.8f * OutGust), MaxWindStrength);
}

void AIslandWeather::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bOff)
	{
		return;
	}
	if (bShots)
	{
		TickShots(DeltaSeconds);
	}
	const double Now = ClockNow();
	if (Forced.bActive)
	{
		CurOvercast = Forced.Overcast;
		CurRain = Forced.Rain;
	}
	else
	{
		EvaluateSky(Now, CurOvercast, CurRain);
	}
	EvaluateWind(Now, CurOvercast, CurWindDir, CurWindStrength, CurGust);

	if (Forced.bActive)
	{
		CurWetness = Forced.Wet;
	}
	else if (CurRain > 0.05f)
	{
		CurWetness = FMath::Min(1.f, CurWetness + DeltaSeconds * CurRain / SoakSeconds);
	}
	else
	{
		CurWetness = FMath::Max(0.f, CurWetness - DeltaSeconds / DrySeconds);
	}
	LogPhase();

	if (bVisuals)
	{
		TickVisuals(DeltaSeconds);
	}
}

void AIslandWeather::LogPhase()
{
	const TCHAR* Phase = CurRain > 0.05f ? TEXT("raining") : CurOvercast > 0.05f ? TEXT("clouds") : TEXT("clear");
	if (LastPhase != Phase)
	{
		UE_LOG(LogUnrealSoulslike, Log, TEXT("Weather: %s -> %s (overcast %.2f, rain %.2f, wet %.2f, wind (%.2f, %.2f) x %.2f, gust %.2f)"),
			LastPhase.IsEmpty() ? TEXT("start") : *LastPhase, Phase, CurOvercast, CurRain, CurWetness, CurWindDir.X, CurWindDir.Y, CurWindStrength, CurGust);
		LastPhase = Phase;
	}
}

bool AIslandWeather::GetViewLocation(FVector& OutLocation) const
{
	const APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
	if (PC && PC->PlayerCameraManager)
	{
		OutLocation = PC->PlayerCameraManager->GetCameraLocation();
		return true;
	}
	return false;
}

void AIslandWeather::TickVisuals(float DeltaSeconds)
{
	// Rain brightness follows daylight (the night's exposure boost lifts it again), dimmed under the clouds.
	float Night = 0.f;
	if (const AIslandDayNight* DayNight = AIslandDayNight::Get(this))
	{
		Night = DayNight->GetNightAmount();
	}
	RainBrightness = FMath::Lerp(RainDayBrightness, RainNightBrightness, Night) * (1.f - 0.3f * CurOvercast);

	// Drops fall and drift with the wind; the offset wraps by the box (the material wraps the same way).
	const FVector RainVel(CurWindDir.X * CurWindStrength * RainWindDrift, CurWindDir.Y * CurWindStrength * RainWindDrift, -RainFallSpeed);
	RainOffset += RainVel * DeltaSeconds;
	RainOffset.X = FMath::Fmod(RainOffset.X, RainBox.X);
	RainOffset.Y = FMath::Fmod(RainOffset.Y, RainBox.Y);
	RainOffset.Z = FMath::Fmod(RainOffset.Z, RainBox.Z);
	// Gusts quicken the sway: the materials run their wind clock at Time + WindTime.
	WindTimeOffset += (FMath::Min(CurWindStrength, MaxWindStrength) - 1.f) * 0.6f * DeltaSeconds;

	UpdateMaterialParameters(DeltaSeconds);
	UpdateClouds();
	UpdateLighting();
	FVector View;
	if (GetViewLocation(View))
	{
		UpdateRain(View);
		UpdateSplashes(DeltaSeconds, View);
		UpdateLeaves(DeltaSeconds, View);
	}
	UpdateAudio(DeltaSeconds);
}

void AIslandWeather::UpdateMaterialParameters(float DeltaSeconds)
{
	UWorld* World = GetWorld();
	UMaterialParameterCollectionInstance* Inst = (World && ParameterCollection) ? World->GetParameterCollectionInstance(ParameterCollection) : nullptr;
	if (!Inst)
	{
		return;
	}
	const float Strength = FMath::Min(CurWindStrength, MaxWindStrength);
	Inst->SetScalarParameterValue(TEXT("Wetness"), CurWetness);
	Inst->SetScalarParameterValue(TEXT("Rain"), CurRain);
	Inst->SetScalarParameterValue(TEXT("Overcast"), CurOvercast);
	Inst->SetScalarParameterValue(TEXT("WindStrength"), Strength);
	Inst->SetScalarParameterValue(TEXT("Gust"), CurGust);
	Inst->SetScalarParameterValue(TEXT("WindTime"), WindTimeOffset);
	Inst->SetScalarParameterValue(TEXT("RainBrightness"), RainBrightness);
	Inst->SetVectorParameterValue(TEXT("WindDir"), FLinearColor((float)CurWindDir.X, (float)CurWindDir.Y, Strength, CurGust));
	Inst->SetVectorParameterValue(TEXT("RainOffset"), FLinearColor((float)RainOffset.X, (float)RainOffset.Y, (float)RainOffset.Z, 0.f));
	Inst->SetVectorParameterValue(TEXT("RainBox"), FLinearColor((float)RainBox.X, (float)RainBox.Y, (float)RainBox.Z, 0.f));
	const float Fall = FMath::Max(RainFallSpeed, 1.f);
	Inst->SetVectorParameterValue(TEXT("RainSlant"), FLinearColor(
		(float)(-CurWindDir.X * Strength * RainWindDrift / Fall), (float)(-CurWindDir.Y * Strength * RainWindDrift / Fall), 0.f, 0.f));
	Inst->SetVectorParameterValue(TEXT("RainStreak"), FLinearColor((float)RainStreakShape.X, (float)RainStreakShape.Y, 0.f, 0.f));
}

void AIslandWeather::UpdateClouds()
{
	if (!CloudMID || FMath::Abs(CurOvercast - AppliedOvercast) < 0.002f)
	{
		return;
	}
	AppliedOvercast = CurOvercast;
	if (!CoverageParam.IsNone())
	{
		CloudMID->SetScalarParameterValue(CoverageParam, BaseCoverage + CloudCoverageBoost * CurOvercast);
	}
	if (!DensityParam.IsNone())
	{
		CloudMID->SetScalarParameterValue(DensityParam, BaseDensity * (1.f + CloudDensityBoost * CurOvercast));
	}
	const float Dark = FMath::Lerp(1.f, CloudStormAlbedo, CurOvercast);
	if (!AlbedoParam.IsNone())
	{
		const FLinearColor Albedo = BaseAlbedo * Dark;
		CloudMID->SetVectorParameterValue(AlbedoParam, FLinearColor(Albedo.R, Albedo.G, Albedo.B, BaseAlbedo.A));
	}
	if (!StormAlbedoParam.IsNone())
	{
		const FLinearColor Albedo = BaseStormAlbedo * Dark;
		CloudMID->SetVectorParameterValue(StormAlbedoParam, FLinearColor(Albedo.R, Albedo.G, Albedo.B, BaseStormAlbedo.A));
	}
}

void AIslandWeather::UpdateLighting()
{
	// The sun: AIslandDayNight drives its intensity and temperature; the overcast multiplies its colour.
	if (UDirectionalLightComponent* Light = Sun.Get())
	{
		const float Dim = FMath::Lerp(1.f, SunOvercastDim, CurOvercast);
		if (FMath::Abs(Dim - AppliedSunDim) > 0.004f)
		{
			Light->SetLightColor(SunBaseColor * Dim);
			AppliedSunDim = Dim;
		}
	}
	// Fog: AIslandDayNight owns its colours; the rain thickens it.
	if (UExponentialHeightFogComponent* FogComp = Fog.Get())
	{
		const float Density = FogBaseDensity * (1.f + 0.35f * CurOvercast + FogRainBoost * CurRain);
		if (FMath::Abs(Density - AppliedFog) > FogBaseDensity * 0.005f + 1.0e-6f)
		{
			FogComp->SetFogDensity(Density);
			AppliedFog = Density;
		}
	}
	// The picture: greyer and a little darker, blended in by its own post-process weight.
	WeatherPost->BlendWeight = CurOvercast;
	FPostProcessSettings& Settings = WeatherPost->Settings;
	Settings.bOverride_SceneColorTint = true;
	Settings.SceneColorTint = OvercastTint;
	Settings.bOverride_ColorSaturation = true;
	Settings.ColorSaturation = FVector4(OvercastSaturation, OvercastSaturation, OvercastSaturation * 1.05f, 1.f);
}

void AIslandWeather::UpdateRain(const FVector& View)
{
	const bool bShow = CurRain > 0.005f && RainDrops->GetInstanceCount() > 0;
	if (bShow != bRainShown)
	{
		RainDrops->SetVisibility(bShow);
		bRainShown = bShow;
	}
	if (!bShow)
	{
		return;
	}
	// The box of drops is centred on the view every frame. The material wraps each drop round the camera,
	// but the GPU culls instances by where they really are: a box snapped to whole cells sat half a cell
	// off the view, so the drops' real positions were mostly off screen and the rain never drew.
	const FVector Anchor = View - RainBox * 0.5;
	if (!Anchor.Equals(RainAnchor, 1.0))
	{
		RainDrops->SetWorldLocation(Anchor);
		RainAnchor = Anchor;
	}
}

float AIslandWeather::TraceGround(const FVector& From, float Down) const
{
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(IslandWeatherGround), false, this);
	if (const APawn* Pawn = UGameplayStatics::GetPlayerPawn(this, 0))
	{
		Params.AddIgnoredActor(Pawn);
	}
	if (GetWorld()->LineTraceSingleByChannel(Hit, From, From - FVector(0.0, 0.0, Down), ECC_Visibility, Params))
	{
		return (float)Hit.ImpactPoint.Z;
	}
	return -1.0e6f;
}

void AIslandWeather::UpdateSplashes(float DeltaSeconds, const FVector& View)
{
	if (SplashPool.IsEmpty())
	{
		return;
	}
	bool bXfDirty = false;
	LiveSplashes = 0;
	for (int32 i = 0; i < SplashPool.Num(); ++i)
	{
		FSplash& Splash = SplashPool[i];
		if (!Splash.bLive)
		{
			continue;
		}
		Splash.Age += DeltaSeconds / Splash.Life;
		if (Splash.Age >= 1.f)
		{
			Splash.bLive = false;
			SplashXf[i] = FTransform(FQuat::Identity, View, FVector(0.001));
			bXfDirty = true;
		}
		else
		{
			++LiveSplashes;
		}
	}

	// New splashes where drops land round the camera (dense near, thinning out).
	SplashAccum += DeltaSeconds * SplashRate * CurRain;
	int32 Budget = 14;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(IslandWeatherSplash), false, this);
	if (const APawn* Pawn = UGameplayStatics::GetPlayerPawn(this, 0))
	{
		Params.AddIgnoredActor(Pawn);
	}
	int32 Free = 0;
	while (SplashAccum >= 1.f && Budget-- > 0)
	{
		SplashAccum -= 1.f;
		while (Free < SplashPool.Num() && SplashPool[Free].bLive)
		{
			++Free;
		}
		if (Free >= SplashPool.Num())
		{
			break;
		}
		const float Angle = FMath::FRand() * UE_TWO_PI;
		const float Dist = SplashRadius * FMath::Sqrt(FMath::FRand());
		const FVector Top(View.X + FMath::Cos(Angle) * Dist, View.Y + FMath::Sin(Angle) * Dist, View.Z + 1500.0);
		FHitResult Hit;
		if (!GetWorld()->LineTraceSingleByChannel(Hit, Top, Top - FVector(0.0, 0.0, 4500.0), ECC_Visibility, Params))
		{
			continue;
		}
		const FVector Normal = Hit.ImpactNormal.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
		const FQuat Rot = FRotationMatrix::MakeFromZ(Normal).ToQuat() * FQuat(FVector::UpVector, FMath::FRand() * UE_TWO_PI);
		const float Diameter = FMath::FRandRange(14.f, 30.f);
		SplashXf[Free] = FTransform(Rot, Hit.ImpactPoint + Normal * 1.5, FVector(Diameter / SplashPlaneSize));
		FSplash& Splash = SplashPool[Free];
		Splash.bLive = true;
		Splash.Age = 0.f;
		Splash.Life = FMath::FRandRange(0.3f, 0.5f);
		bXfDirty = true;
		++LiveSplashes;
	}
	SplashAccum = FMath::Min(SplashAccum, 4.f);

	const bool bShow = LiveSplashes > 0;
	if (Splashes->IsVisible() != bShow)
	{
		Splashes->SetVisibility(bShow);
	}
	if (!bShow && !bXfDirty)
	{
		return;
	}
	if (bXfDirty)
	{
		Splashes->BatchUpdateInstancesTransforms(0, SplashXf, true, false, true);
	}
	const int32 Last = SplashPool.Num() - 1;
	for (int32 i = 0; i <= Last; ++i)
	{
		Splashes->SetCustomDataValue(i, 0, SplashPool[i].bLive ? SplashPool[i].Age : 1.f, i == Last);
	}
}

void AIslandWeather::UpdateLeaves(float DeltaSeconds, const FVector& View)
{
	if (LeafPool.IsEmpty())
	{
		return;
	}
	// Trees near enough to shed leaves into view.
	NearRefresh -= DeltaSeconds;
	if (NearRefresh <= 0.f)
	{
		NearRefresh = 1.f;
		NearCanopies.Reset();
		const float R2 = LeafRadius * LeafRadius;
		for (int32 i = 0; i < Canopies.Num(); ++i)
		{
			if (FVector::DistSquared2D(Canopies[i].Center, View) < R2)
			{
				NearCanopies.Add(i);
			}
		}
	}

	// Torn off at a trickle in a breeze, in flurries in the gusts.
	if (NearCanopies.IsEmpty())
	{
		LeafAccum = 0.f;
	}
	else
	{
		const float StrengthFactor = FMath::Clamp(CurWindStrength / 1.2f, 0.3f, 2.f);
		LeafAccum += DeltaSeconds * (LeafRate + LeafGustRate * CurGust * CurGust) * StrengthFactor;
	}
	int32 Budget = 6;
	int32 Free = 0;
	while (LeafAccum >= 1.f && Budget-- > 0 && !NearCanopies.IsEmpty())
	{
		LeafAccum -= 1.f;
		while (Free < LeafPool.Num() && LeafPool[Free].State != 0)
		{
			++Free;
		}
		if (Free >= LeafPool.Num())
		{
			break;
		}
		const FCanopy& Canopy = Canopies[NearCanopies[FMath::RandRange(0, NearCanopies.Num() - 1)]];
		const FVector Offset = FMath::VRand() * FMath::Sqrt(FMath::FRand());
		FLeaf& Leaf = LeafPool[Free];
		Leaf.Pos = Canopy.Center + FVector(Offset.X * Canopy.Radius, Offset.Y * Canopy.Radius, Offset.Z * Canopy.Height);
		Leaf.Vel = CurWindDir * (CurWindStrength * LeafWindSpeed * 0.3f) + FMath::VRand() * 40.0;
		Leaf.Rot = FQuat(FMath::VRand(), FMath::FRand() * UE_TWO_PI);
		Leaf.SpinAxis = FMath::VRand();
		Leaf.Spin = FMath::FRandRange(2.f, 8.f) * (FMath::RandBool() ? 1.f : -1.f);
		Leaf.Age = 0.f;
		Leaf.Life = LeafLife * FMath::FRandRange(0.7f, 1.2f);
		Leaf.GroundZ = TraceGround(Leaf.Pos + FVector(0.0, 0.0, 50.0), 8000.f);
		Leaf.NextTrace = FMath::FRandRange(0.4f, 0.8f);
		Leaf.Size = FMath::FRandRange(8.f, 13.f);
		Leaf.Phase = FMath::FRand() * UE_TWO_PI;
		Leaf.State = 1;
	}
	LeafAccum = FMath::Min(LeafAccum, 3.f);

	// Tumble downwind: drag toward the wind, flutter across it, sink slowly, lifted by the gusts.
	const FVector WindVel = CurWindDir * (CurWindStrength * LeafWindSpeed);
	const FVector Across(-CurWindDir.Y, CurWindDir.X, 0.0);
	const float Drag = FMath::Min(1.f, DeltaSeconds * 1.6f);
	const float FarSq = FMath::Square(LeafRadius * 1.4f);
	LiveLeaves = 0;
	for (int32 i = 0; i < LeafPool.Num(); ++i)
	{
		FLeaf& Leaf = LeafPool[i];
		if (Leaf.State == 0)
		{
			LeafXf[i] = FTransform(FQuat::Identity, View, FVector(0.001));
			continue;
		}
		Leaf.Age += DeltaSeconds;
		float Fade = 1.f;
		if (Leaf.State == 1)
		{
			const float Flutter = FMath::Sin(Leaf.Age * 5.3f + Leaf.Phase);
			FVector Target = WindVel + Across * (Flutter * 70.f);
			Target.Z = -75.0 + CurGust * 110.f * FMath::Sin(Leaf.Age * 1.7f + Leaf.Phase * 2.f) + 30.f * Flutter;
			Leaf.Vel += (Target - Leaf.Vel) * Drag;
			Leaf.Pos += Leaf.Vel * DeltaSeconds;
			Leaf.Rot = (FQuat(Leaf.SpinAxis, Leaf.Spin * DeltaSeconds) * Leaf.Rot).GetNormalized();
			Leaf.NextTrace -= DeltaSeconds;
			if (Leaf.NextTrace <= 0.f)
			{
				Leaf.NextTrace = 0.6f;
				Leaf.GroundZ = TraceGround(Leaf.Pos + FVector(0.0, 0.0, 60.0), 8000.f);
			}
			if (Leaf.Pos.Z <= Leaf.GroundZ + 1.5f)
			{
				// Landed: lie flat a few seconds, then wither away.
				Leaf.State = 2;
				Leaf.Pos.Z = Leaf.GroundZ + 1.0;
				Leaf.Rot = FQuat(FVector::UpVector, FMath::FRand() * UE_TWO_PI) * FQuat(FVector::ForwardVector, FMath::FRandRange(-0.2f, 0.2f));
				Leaf.Age = 0.f;
				Leaf.Life = FMath::FRandRange(3.f, 6.f);
			}
			Fade = FMath::Clamp((Leaf.Life - Leaf.Age) / 1.f, 0.f, 1.f);
		}
		else
		{
			Fade = FMath::Clamp((Leaf.Life - Leaf.Age) / 1.f, 0.f, 1.f);
		}
		if (Leaf.Age >= Leaf.Life || FVector::DistSquared2D(Leaf.Pos, View) > FarSq)
		{
			Leaf.State = 0;
			LeafXf[i] = FTransform(FQuat::Identity, View, FVector(0.001));
			continue;
		}
		++LiveLeaves;
		LeafXf[i] = FTransform(Leaf.Rot, Leaf.Pos, FVector(Leaf.Size * Fade / LeafPlaneSize, Leaf.Size * 0.62f * Fade / LeafPlaneSize, 1.f));
	}
	const bool bShow = LiveLeaves > 0;
	if (LeafCloud->IsVisible() != bShow)
	{
		LeafCloud->SetVisibility(bShow);
	}
	if (bShow)
	{
		LeafCloud->BatchUpdateInstancesTransforms(0, LeafXf, true, true, true);
	}
}

void AIslandWeather::UpdateAudio(float DeltaSeconds)
{
	const float RainTarget = RainVolume * FMath::Clamp(CurRain, 0.f, 1.f);
	const float Windy = FMath::Clamp(0.25f + 0.35f * (CurWindStrength - 1.f) + 0.4f * CurGust, 0.08f, 1.f);
	const float WindTarget = WindVolume * Windy;
	const float Follow = FMath::Min(1.f, DeltaSeconds * 2.f);
	RainVolNow += (RainTarget - RainVolNow) * Follow;
	WindVolNow += (WindTarget - WindVolNow) * Follow;
	if (RainAudio && RainWave)
	{
		RainAudio->SetVolumeMultiplier(FMath::Max(RainVolNow, 0.001f));
	}
	if (WindAudio && WindWave)
	{
		WindAudio->SetVolumeMultiplier(FMath::Max(WindVolNow, 0.001f));
		WindAudio->SetPitchMultiplier(0.88f + 0.3f * CurGust);
	}
}

// -WeatherShots: after the world warms up, force each weather in turn, frame the player's camera, let Lumen,
// the sky capture and the particles settle, screenshot to Saved/WeatherShots/<name>.png, then quit.
void AIslandWeather::TickShots(float DeltaSeconds)
{
	ShotClock += DeltaSeconds;
	if (!bShotWarm)
	{
		if (ShotClock < IslandWX::ShotWarmup || IslandWX::ShadersCompiling())
		{
			return;
		}
		bShotWarm = true;
		ShotPhaseStart = ShotClock;
		ShotYaw = FMath::RadiansToDegrees(FMath::Atan2((float)CurWindDir.Y, (float)CurWindDir.X));
		if (AIslandDayNight* DayNight = AIslandDayNight::Get(this))
		{
			DayNight->SetTimeOfDay(13.f, true);
		}
	}
	const int32 NumShots = UE_ARRAY_COUNT(IslandWX::Shots);
	if (ShotIndex >= NumShots)
	{
		if (ShotClock - ShotPhaseStart > 2.f)
		{
			UE_LOG(LogUnrealSoulslike, Display, TEXT("WEATHER_SHOTS_DONE"));
			bShots = false;
			FPlatformMisc::RequestExit(false, TEXT("WeatherShots"));
		}
		return;
	}
	const IslandWX::FShot& Shot = IslandWX::Shots[ShotIndex];
	APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
	if (!bShotFramed)
	{
		Forced.bActive = true;
		Forced.Overcast = Shot.Overcast;
		Forced.Rain = Shot.Rain;
		Forced.Wet = Shot.Wet;
		Forced.Gust = Shot.Gust;
		float Yaw = ShotYaw + Shot.YawFromWind;
		float Pitch = Shot.Pitch;
		if (Shot.bTrees && !Canopies.IsEmpty())
		{
			// Stand a few metres downwind of the nearest tree and look back up at its crown.
			APawn* Pawn = PC ? PC->GetPawn() : nullptr;
			const FVector From = Pawn ? Pawn->GetActorLocation() : FVector::ZeroVector;
			int32 Best = 0;
			for (int32 i = 1; i < Canopies.Num(); ++i)
			{
				if (FVector::DistSquared2D(Canopies[i].Center, From) < FVector::DistSquared2D(Canopies[Best].Center, From))
				{
					Best = i;
				}
			}
			const FCanopy& Canopy = Canopies[Best];
			const FVector Spot = Canopy.Center + CurWindDir * (Canopy.Radius + 700.0);
			const float Ground = TraceGround(FVector(Spot.X, Spot.Y, Canopy.Center.Z + 3000.0), 9000.f);
			const FVector Stand(Spot.X, Spot.Y, (Ground > -1.0e5f ? Ground : Canopy.Center.Z - Canopy.Height * 2.f) + 110.f);
			if (Pawn)
			{
				Pawn->SetActorLocation(Stand, false, nullptr, ETeleportType::TeleportPhysics);
			}
			const FVector Look = Canopy.Center - Stand;
			Yaw = FMath::RadiansToDegrees(FMath::Atan2((float)Look.Y, (float)Look.X));
			Pitch = FMath::Clamp(FMath::RadiansToDegrees(FMath::Atan2((float)Look.Z, (float)Look.Size2D())), -10.f, 35.f) * 0.6f;
			UE_LOG(LogUnrealSoulslike, Log, TEXT("Weather shots: standing at (%.0f, %.0f, %.0f) by the tree at (%.0f, %.0f, %.0f)"),
				Stand.X, Stand.Y, Stand.Z, Canopy.Center.X, Canopy.Center.Y, Canopy.Center.Z);
		}
		if (PC)
		{
			if (PC->MyHUD)
			{
				PC->MyHUD->bShowHUD = false;
			}
			PC->SetControlRotation(FRotator(Pitch, Yaw, 0.f));
		}
		bShotFramed = true;
		bShotTaken = false;
		ShotPhaseStart = ShotClock;
		return;
	}
	if (!bShotTaken && ShotClock - ShotPhaseStart >= Shot.Settle && !IslandWX::ShadersCompiling())
	{
		const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("WeatherShots") / (FString(Shot.Name) + TEXT(".png")));
		FScreenshotRequest::RequestScreenshot(Path, false, false);
		float Coverage = 0.f;
		if (CloudMID && !CoverageParam.IsNone())
		{
			const UMaterialInterface* AsInterface = CloudMID;
			AsInterface->GetScalarParameterValue(FHashedMaterialParameterInfo(CoverageParam), Coverage);
		}
		UE_LOG(LogUnrealSoulslike, Display,
			TEXT("Weather shot %s at %.1f s: overcast %.2f, rain %.2f, wet %.2f, wind (%.2f, %.2f) x %.2f, gust %.2f, cloud coverage %.3f, rain offset (%.0f, %.0f, %.0f), %d splashes, %d leaves -> %s"),
			Shot.Name, ShotClock, CurOvercast, CurRain, CurWetness, CurWindDir.X, CurWindDir.Y, CurWindStrength, CurGust, Coverage,
			RainOffset.X, RainOffset.Y, RainOffset.Z, LiveSplashes, LiveLeaves, *Path);
		bShotTaken = true;
		ShotPhaseStart = ShotClock;
		return;
	}
	if (bShotTaken && ShotClock - ShotPhaseStart > 1.f)
	{
		++ShotIndex;
		// The two cloud shots share one framing, so only the clock moves between them.
		const bool bSameView = ShotIndex < NumShots && IslandWX::Shots[ShotIndex].Pitch == Shot.Pitch
			&& IslandWX::Shots[ShotIndex].YawFromWind == Shot.YawFromWind && !IslandWX::Shots[ShotIndex].bTrees;
		bShotFramed = bSameView;
		bShotTaken = false;
		if (!bSameView)
		{
			ShotPhaseStart = ShotClock;
		}
	}
}

void UIslandWeatherSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (!InWorld.IsGameWorld() || InWorld.GetNetMode() == NM_Client || FParse::Param(FCommandLine::Get(), TEXT("NoWeather")))
	{
		return;
	}
	if (!InWorld.GetMapName().Contains(TEXT("Lvl_Isle")))
	{
		return;
	}
	TActorIterator<AIslandWeather> Existing(&InWorld);
	if (Existing)
	{
		return;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	InWorld.SpawnActor<AIslandWeather>(AIslandWeather::StaticClass(), FTransform::Identity, Params);
	UE_LOG(LogUnrealSoulslike, Log, TEXT("Weather: spawned a weather actor (none placed in %s)"), *InWorld.GetMapName());
}
