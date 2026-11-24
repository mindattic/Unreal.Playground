#include "PlaygroundShots.h"
#include "MindAtticCapture.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace PlaygroundShots
{
	void Take(const FString& Name, bool bShowUI)
	{
		FScreenshotRequest::RequestScreenshot(Name, bShowUI, false);

		namespace Capture = MindAttic::Media::Capture;
		if (!Capture::Enabled()) return;
		const FString Tag = FPaths::GetBaseFilename(Name);
		Capture::Marker(Tag);
		Capture::Screenshot(Tag, Capture::Data().Str(TEXT("file"), Name).Bool(TEXT("ui"), bShowUI));
	}
}
