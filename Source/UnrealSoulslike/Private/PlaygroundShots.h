#pragma once

#include "CoreMinimal.h"

// Every demo mode's screenshot (UNR-T123): the engine's own shot into Saved/Screenshots (or the mode's folder, which the
// scripts read) as before, and while MindAttic capture runs also a capture shot (with its sidecar) and a marker, so the
// shot has a chapter in the recording.
namespace PlaygroundShots
{
	/// `Name` is a file name or path as FScreenshotRequest::RequestScreenshot takes it; capture is tagged with its base name.
	void Take(const FString& Name, bool bShowUI);
}
