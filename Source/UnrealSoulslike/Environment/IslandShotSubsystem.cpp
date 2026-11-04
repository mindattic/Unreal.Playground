#include "IslandShotSubsystem.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/World.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMisc.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogIslandShots, Log, All);

bool UIslandShotSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	FString File;
	return FParse::Value(FCommandLine::Get(), TEXT("IslandShots="), File) && Super::ShouldCreateSubsystem(Outer);
}

void UIslandShotSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (!InWorld.IsGameWorld())
	{
		return;
	}
	FString File;
	FParse::Value(FCommandLine::Get(), TEXT("IslandShots="), File);
	if (!FParse::Value(FCommandLine::Get(), TEXT("IslandShotDir="), ShotDir))
	{
		ShotDir = FPaths::ProjectSavedDir() / TEXT("IslandShots");
	}
	FParse::Value(FCommandLine::Get(), TEXT("IslandShotWarmup="), Warmup);
	FParse::Value(FCommandLine::Get(), TEXT("IslandShotSettle="), Settle);

	TArray<FString> Lines;
	if (!FFileHelper::LoadFileToStringArray(Lines, *File))
	{
		UE_LOG(LogIslandShots, Error, TEXT("Cannot read shot list %s"), *File);
		return;
	}
	for (const FString& Raw : Lines)
	{
		TArray<FString> Parts;
		Raw.TrimStartAndEnd().ParseIntoArrayWS(Parts);
		if (Parts.Num() < 2 || Parts[0].StartsWith(TEXT("#")))
		{
			continue;
		}
		FShot Shot;
		Shot.Name = Parts[0];
		if (Parts.Num() >= 6)
		{
			Shot.bPlayerView = false;
			Shot.Location = FVector(FCString::Atof(*Parts[1]), FCString::Atof(*Parts[2]), FCString::Atof(*Parts[3]));
			Shot.Rotation = FRotator(FCString::Atof(*Parts[4]), FCString::Atof(*Parts[5]), 0.f);
			if (Parts.Num() >= 7)
			{
				Shot.Fov = FCString::Atof(*Parts[6]);
			}
		}
		Shots.Add(Shot);
	}
	bActive = Shots.Num() > 0;
	UE_LOG(LogIslandShots, Display, TEXT("%d shots queued -> %s"), Shots.Num(), *ShotDir);
}

void UIslandShotSubsystem::Frame(const FShot& Shot)
{
	UWorld* World = GetWorld();
	APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0);
	if (!PC)
	{
		return;
	}
	if (PC->MyHUD)
	{
		PC->MyHUD->bShowHUD = Shot.bPlayerView;
	}
	if (Shot.bPlayerView)
	{
		if (APawn* Pawn = PC->GetPawn())
		{
			PC->SetViewTarget(Pawn);
		}
		return;
	}
	if (!Camera)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Camera = World->SpawnActor<ACameraActor>(Shot.Location, Shot.Rotation, Params);
		Camera->GetCameraComponent()->bConstrainAspectRatio = false;
	}
	Camera->SetActorLocationAndRotation(Shot.Location, Shot.Rotation);
	Camera->GetCameraComponent()->SetFieldOfView(Shot.Fov);
	PC->SetViewTarget(Camera);
}

void UIslandShotSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	Clock += DeltaTime;

	if (!bWarm)
	{
		bool bCompiling = false;
#if WITH_EDITOR
		bCompiling = GShaderCompilingManager && GShaderCompilingManager->GetNumRemainingJobs() > 0;
#endif
		if (Clock < Warmup || bCompiling)
		{
			return;
		}
		bWarm = true;
		PhaseStart = Clock;
	}

	if (Index >= Shots.Num())
	{
		if (Clock - PhaseStart > 2.f)
		{
			UE_LOG(LogIslandShots, Display, TEXT("ISLAND_SHOTS_DONE"));
			bActive = false;
			FPlatformMisc::RequestExit(false, TEXT("IslandShots"));
		}
		return;
	}

	const FShot& Shot = Shots[Index];
	if (!bFramed)
	{
		Frame(Shot);
		bFramed = true;
		PhaseStart = Clock;
		return;
	}
	bool bCompiling = false;
#if WITH_EDITOR
	bCompiling = GShaderCompilingManager && GShaderCompilingManager->GetNumRemainingJobs() > 0;
#endif
	if (!bRequested && Clock - PhaseStart >= Settle && !bCompiling)
	{
		const FString Path = FPaths::ConvertRelativePathToFull(ShotDir / (Shot.Name + TEXT(".png")));
		FScreenshotRequest::RequestScreenshot(Path, false, false);
		UE_LOG(LogIslandShots, Display, TEXT("Shot %s -> %s"), *Shot.Name, *Path);
		bRequested = true;
		PhaseStart = Clock;
		return;
	}
	if (bRequested && Clock - PhaseStart >= 1.5f)
	{
		++Index;
		bFramed = false;
		bRequested = false;
		PhaseStart = Clock;
	}
}

TStatId UIslandShotSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UIslandShotSubsystem, STATGROUP_Tickables);
}
