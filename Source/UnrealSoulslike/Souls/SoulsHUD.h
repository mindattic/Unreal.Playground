#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "SoulsHUD.generated.h"

class UFont;
class ASoulsKnight;

// Elden-Ring-style screen furniture: health and stamina bars with a lagging damage trail, flasks,
// runes, the lock-on mark, the boss bar, prompts and the big centre banners (YOU DIED and friends).
UCLASS()
class UNREALSOULSLIKE_API ASoulsHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

protected:
	virtual void BeginPlay() override;

private:
	void Bar(float X, float Y, float Width, float Height, float Fraction, float Trail, const FLinearColor& Fill);
	void Text(const FString& String, float X, float Y, const FLinearColor& Color, float Scale, bool bCentred, UFont* Font = nullptr);
	void DrawBanner(float Now);
	void DrawLockOn(const ASoulsKnight* Knight);
	void DrawBoss(float DeltaSeconds);
	float Ui() const;

	UPROPERTY(Transient)
	TObjectPtr<UFont> TitleFont;

	float PlayerTrail = 1.f;
	float StaminaShown = 1.f;
	float BossTrail = 1.f;
	float LastDrawTime = 0.f;
	float HintUntil = 25.f;
};
