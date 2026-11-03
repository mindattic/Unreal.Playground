#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "SoulslikeHUD.generated.h"

// Canvas-drawn HUD (no UMG assets): vitals, flasks, enemy bars with awareness marks (? / !),
// the boss bar, YOU DIED, the victory banner and a controls hint.
UCLASS()
class UNREALSOULSLIKE_API ASoulslikeHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

private:
	void DrawBar(float X, float Y, float Width, float Height, float Fraction, const FLinearColor& Color);
	void DrawCentredText(const FString& Text, float Y, const FLinearColor& Color, float Scale);
	void DrawPlayerVitals(const class ASoulslikeCharacter* Player);
	void DrawEnemyBars(const class ASoulslikeCharacter* Player);
	void DrawControlsHint();

	float DeathTime = -1.f;
	float VictoryTime = -1.f;
	bool bBossSeen = false;
};
