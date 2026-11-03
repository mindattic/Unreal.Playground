#include "SoulsHUD.h"
#include "SoulsKnight.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"

void ASoulsHUD::Bar(float X, float Y, float Width, float Height, float Fraction, const FLinearColor& Fill)
{
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.6f), X - 2.f, Y - 2.f, Width + 4.f, Height + 4.f);
	DrawRect(FLinearColor(0.08f, 0.08f, 0.08f, 0.8f), X, Y, Width, Height);
	DrawRect(Fill, X, Y, Width * FMath::Clamp(Fraction, 0.f, 1.f), Height);
}

void ASoulsHUD::CentredText(const FString& Text, float Y, const FLinearColor& Color, float Scale)
{
	UFont* Font = GEngine->GetLargeFont();
	float Width = 0.f, Height = 0.f;
	GetTextSize(Text, Width, Height, Font, Scale);
	DrawText(Text, Color, (Canvas->ClipX - Width) * 0.5f, Y, Font, Scale);
}

void ASoulsHUD::DrawHUD()
{
	Super::DrawHUD();
	const ASoulsKnight* Knight = Cast<ASoulsKnight>(GetOwningPawn());
	if (!Canvas || !Knight)
	{
		return;
	}

	const float X = 48.f, Y = 40.f;
	Bar(X, Y, 360.f, 12.f, Knight->GetHealthFraction(), FLinearColor(0.6f, 0.05f, 0.04f));
	Bar(X, Y + 22.f, 300.f, 8.f, Knight->GetStaminaFraction(), FLinearColor(0.16f, 0.5f, 0.2f));
	for (int32 i = 0; i < Knight->MaxFlasks; ++i)
	{
		const bool bFull = i < Knight->Flasks;
		DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.6f), X + i * 24.f - 2.f, Y + 42.f, 18.f, 26.f);
		DrawRect(bFull ? FLinearColor(0.95f, 0.55f, 0.15f) : FLinearColor(0.18f, 0.18f, 0.18f), X + i * 24.f, Y + 44.f, 14.f, 22.f);
	}

	const float Now = GetWorld()->GetTimeSeconds();
	if (Knight->GetHealthFraction() <= 0.f)
	{
		if (DeathTime < 0.f)
		{
			DeathTime = Now;
		}
		const float Alpha = FMath::Clamp((Now - DeathTime) / 1.2f, 0.f, 1.f);
		DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.55f * Alpha), 0.f, Canvas->ClipY * 0.42f, Canvas->ClipX, Canvas->ClipY * 0.16f);
		CentredText(TEXT("YOU DIED"), Canvas->ClipY * 0.45f, FLinearColor(0.6f, 0.04f, 0.03f, Alpha), 3.f);
	}
	else
	{
		DeathTime = -1.f;
	}

	if (Now < 20.f)
	{
		static const TCHAR* Lines[] = {
			TEXT("Light attack  X / LMB      Heavy attack  Y / F"),
			TEXT("Dodge roll  B / Shift      Jump  A / Space"),
			TEXT("Block (hold)  LB / RMB     Heal  RB / R"),
		};
		float LineY = Canvas->ClipY - 110.f;
		for (const TCHAR* Line : Lines)
		{
			DrawText(Line, FLinearColor(0.85f, 0.82f, 0.72f, FMath::Clamp((20.f - Now) / 3.f, 0.f, 1.f)), 48.f, LineY, GEngine->GetSmallFont(), 1.3f);
			LineY += 24.f;
		}
	}
}
