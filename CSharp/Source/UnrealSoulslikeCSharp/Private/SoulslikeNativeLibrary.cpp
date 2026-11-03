#include "SoulslikeNativeLibrary.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "HAL/PlatformMemory.h"
#include "HAL/PlatformTime.h"
#include "Misc/Paths.h"
#include "RenderCore.h"
#include "UnrealClient.h"

void USoulslikeNativeLibrary::SeedRandom(int32 Seed)
{
	FMath::RandInit(Seed);
}

float USoulslikeNativeLibrary::ReadGameThreadMs()
{
	return FPlatformTime::ToMilliseconds(GGameThreadTime);
}

float USoulslikeNativeLibrary::ReadPeakUsedPhysicalMB()
{
	return FPlatformMemory::GetStats().PeakUsedPhysical / (1024.0 * 1024.0);
}

void USoulslikeNativeLibrary::RequestScreenshot(const FString& Name)
{
	FScreenshotRequest::RequestScreenshot(Name, true, false);
}

FString USoulslikeNativeLibrary::ResolveSavedDir()
{
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir());
}

FString USoulslikeNativeLibrary::ResolveContentDir()
{
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir());
}

UFont* USoulslikeNativeLibrary::EngineSmallFont()
{
	return GEngine ? GEngine->GetSmallFont() : nullptr;
}

UFont* USoulslikeNativeLibrary::EngineMediumFont()
{
	return GEngine ? GEngine->GetMediumFont() : nullptr;
}

UFont* USoulslikeNativeLibrary::EngineLargeFont()
{
	return GEngine ? GEngine->GetLargeFont() : nullptr;
}
