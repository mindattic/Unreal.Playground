#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "SoulsHUD.generated.h"

// Souls-style vitals for the knight (health, stamina, flasks), YOU DIED, and a controls hint.
UCLASS()
class UNREALSOULSLIKE_API ASoulsHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

private:
	void Bar(float X, float Y, float Width, float Height, float Fraction, const FLinearColor& Fill);
	void CentredText(const FString& Text, float Y, const FLinearColor& Color, float Scale);

	float DeathTime = -1.f;
};
