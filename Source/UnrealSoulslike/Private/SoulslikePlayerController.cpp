#include "SoulslikePlayerController.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "RenderCore.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformMemory.h"
#include "HAL/PlatformTime.h"
#include "UnrealClient.h"
#include "TimerManager.h"

namespace
{
	constexpr float BenchmarkWarmup = 5.f;

	float Percentile(TArray<float> Values, float P)
	{
		if (Values.IsEmpty())
		{
			return 0.f;
		}
		Values.Sort();
		return Values[FMath::Clamp(FMath::FloorToInt(P * (Values.Num() - 1)), 0, Values.Num() - 1)];
	}

	float Mean(const TArray<float>& Values)
	{
		double Sum = 0.0;
		for (const float Value : Values)
		{
			Sum += Value;
		}
		return Values.IsEmpty() ? 0.f : static_cast<float>(Sum / Values.Num());
	}
}

ASoulslikePlayerController::ASoulslikePlayerController()
{
	PrimaryActorTick.bCanEverTick = true;
}

void ASoulslikePlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (!IsLocalController())
	{
		return;
	}

	SetInputMode(FInputModeGameOnly());
	bShowMouseCursor = false;

	float Interval = 0.f;
	if (FParse::Value(FCommandLine::Get(), TEXT("AutoShot="), Interval) && Interval > 0.f)
	{
		GetWorldTimerManager().SetTimer(AutoShotHandle, this, &ASoulslikePlayerController::TakeAutoShot, Interval, true);
	}
	float QuitAfter = 0.f;
	if (FParse::Value(FCommandLine::Get(), TEXT("QuitAfter="), QuitAfter) && QuitAfter > 0.f)
	{
		GetWorldTimerManager().SetTimer(QuitHandle, this, &ASoulslikePlayerController::QuitGame, QuitAfter, false);
	}
	if (FParse::Value(FCommandLine::Get(), TEXT("Benchmark="), BenchmarkSeconds) && BenchmarkSeconds > 0.f)
	{
		FMath::RandInit(1745);
		FrameMs.Reserve(FMath::CeilToInt(BenchmarkSeconds * 240.f));
		GameThreadMs.Reserve(FrameMs.Max());
	}
}

void ASoulslikePlayerController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (BenchmarkSeconds <= 0.f || !IsLocalController())
	{
		return;
	}

	BenchmarkElapsed += DeltaSeconds;
	if (BenchmarkElapsed < BenchmarkWarmup)
	{
		return;
	}
	FrameMs.Add(DeltaSeconds * 1000.f);
	GameThreadMs.Add(FPlatformTime::ToMilliseconds(GGameThreadTime));
	if (BenchmarkElapsed >= BenchmarkWarmup + BenchmarkSeconds)
	{
		WriteBenchmark();
		BenchmarkSeconds = 0.f;
		QuitGame();
	}
}

void ASoulslikePlayerController::WriteBenchmark()
{
	const float AverageMs = Mean(FrameMs);
	const float P99Ms = Percentile(FrameMs, 0.99f);
	const FPlatformMemoryStats Memory = FPlatformMemory::GetStats();

	const FString Json = FString::Printf(TEXT(
		"{\n"
		"  \"language\": \"cpp\",\n"
		"  \"seconds\": %.1f,\n"
		"  \"frames\": %d,\n"
		"  \"avg_frame_ms\": %.3f,\n"
		"  \"p50_frame_ms\": %.3f,\n"
		"  \"p99_frame_ms\": %.3f,\n"
		"  \"avg_fps\": %.1f,\n"
		"  \"one_percent_low_fps\": %.1f,\n"
		"  \"avg_game_thread_ms\": %.3f,\n"
		"  \"p99_game_thread_ms\": %.3f,\n"
		"  \"peak_used_physical_mb\": %.1f\n"
		"}\n"),
		BenchmarkElapsed - BenchmarkWarmup, FrameMs.Num(), AverageMs, Percentile(FrameMs, 0.5f), P99Ms,
		AverageMs > 0.f ? 1000.f / AverageMs : 0.f, P99Ms > 0.f ? 1000.f / P99Ms : 0.f,
		Mean(GameThreadMs), Percentile(GameThreadMs, 0.99f), Memory.PeakUsedPhysical / (1024.0 * 1024.0));

	const FString Path = FPaths::ProjectSavedDir() / TEXT("Benchmark/benchmark-cpp.json");
	FFileHelper::SaveStringToFile(Json, *Path);
	UE_LOG(LogTemp, Display, TEXT("BENCHMARK_WRITTEN %s"), *Path);
}

void ASoulslikePlayerController::TakeAutoShot()
{
	FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("AutoShot_%03d"), ShotIndex++), true, false);
}

void ASoulslikePlayerController::QuitGame()
{
	ConsoleCommand(TEXT("quit"));
}
