#include "SoulslikeGameMode.h"
#include "SoulslikeCharacter.h"
#include "SoulslikePlayerController.h"

ASoulslikeGameMode::ASoulslikeGameMode()
{
	DefaultPawnClass = ASoulslikeCharacter::StaticClass();
	PlayerControllerClass = ASoulslikePlayerController::StaticClass();
	bUseSeamlessTravel = true;
}
