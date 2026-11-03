#include "SoulslikePlayerController.h"
#include "Misc/CommandLine.h"
#include "UnrealClient.h"
#include "TimerManager.h"

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
}

void ASoulslikePlayerController::TakeAutoShot()
{
	FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("AutoShot_%03d"), ShotIndex++), true, false);
}

void ASoulslikePlayerController::QuitGame()
{
	ConsoleCommand(TEXT("quit"));
}
