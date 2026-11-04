#include "SoulsHUD.h"
#include "SoulsEnemy.h"
#include "SoulsGameMode.h"
#include "SoulsKnight.h"
#include "SiteOfGrace.h"
#include "GrapplePoint.h"
#include "SoulsPickup.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Fonts/CompositeFont.h"
#include "Misc/Paths.h"

namespace
{
	const FLinearColor HudGold(0.78f, 0.66f, 0.42f);
	const FLinearColor HudParchment(0.86f, 0.82f, 0.72f);
	const FLinearColor HudHealthRed(0.58f, 0.06f, 0.05f);
	const FLinearColor HudStaminaGreen(0.2f, 0.5f, 0.22f);
	const FLinearColor HudTrailYellow(0.85f, 0.7f, 0.3f);
}

void ASoulsHUD::BeginPlay()
{
	Super::BeginPlay();
	// A serif title face (Cinzel, OFL) built at runtime from the staged .ttf; the engine font otherwise.
	const FString FontFile = FPaths::ProjectContentDir() / TEXT("Souls/Fonts/Cinzel.ttf");
	if (FPaths::FileExists(FontFile))
	{
		TitleFont = NewObject<UFont>(this, TEXT("F_Title"));
		TitleFont->FontCacheType = EFontCacheType::Runtime;
		TitleFont->LegacyFontSize = 64;
		TitleFont->GetMutableInternalCompositeFont().DefaultTypeface.Fonts.Add(
			FTypefaceEntry(TEXT("Regular"), FontFile, EFontHinting::Default, EFontLoadingPolicy::LazyLoad));
	}
}

float ASoulsHUD::Ui() const
{
	return Canvas ? Canvas->ClipY / 1080.f : 1.f;
}

void ASoulsHUD::Bar(float X, float Y, float Width, float Height, float Fraction, float Trail, const FLinearColor& Fill)
{
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.55f), X - 3.f, Y - 3.f, Width + 6.f, Height + 6.f);
	DrawRect(FLinearColor(HudGold.R, HudGold.G, HudGold.B, 0.55f), X - 1.f, Y - 1.f, Width + 2.f, 1.f);
	DrawRect(FLinearColor(HudGold.R, HudGold.G, HudGold.B, 0.35f), X - 1.f, Y + Height, Width + 2.f, 1.f);
	DrawRect(FLinearColor(0.05f, 0.05f, 0.05f, 0.85f), X, Y, Width, Height);
	if (Trail > Fraction)
	{
		DrawRect(HudTrailYellow, X + Width * Fraction, Y, Width * (Trail - Fraction), Height);
	}
	DrawRect(Fill, X, Y, Width * FMath::Clamp(Fraction, 0.f, 1.f), Height);
	DrawRect(FLinearColor(1.f, 1.f, 1.f, 0.08f), X, Y, Width * FMath::Clamp(Fraction, 0.f, 1.f), Height * 0.4f);
}

void ASoulsHUD::Text(const FString& String, float X, float Y, const FLinearColor& Color, float Scale, bool bCentred, UFont* Font)
{
	UFont* Use = Font ? Font : GEngine->GetLargeFont();
	float W = 0.f, H = 0.f;
	GetTextSize(String, W, H, Use, Scale);
	const float DrawX = bCentred ? X - W * 0.5f : X;
	DrawText(String, FLinearColor(0.f, 0.f, 0.f, Color.A * 0.6f), DrawX + 2.f, Y + 2.f, Use, Scale);
	DrawText(String, Color, DrawX, Y, Use, Scale);
}

void ASoulsHUD::DrawHUD()
{
	Super::DrawHUD();
	if (!Canvas)
	{
		return;
	}
	const float Now = GetWorld()->GetTimeSeconds();
	const float Delta = FMath::Clamp(Now - LastDrawTime, 0.f, 0.1f);
	LastDrawTime = Now;
	const float S = Ui();

	ASoulsKnight* Knight = Cast<ASoulsKnight>(GetOwningPawn());
	const ASoulsGameMode* GameMode = GetWorld()->GetAuthGameMode<ASoulsGameMode>();

	if (Knight && !Knight->IsDead())
	{
		// Vitals, top-left. Bars lengthen with the pool, as in the games.
		const float X = 60.f * S, Y = 50.f * S;
		const float Health = Knight->GetHealthFraction();
		PlayerTrail = Health > PlayerTrail ? Health : FMath::FInterpTo(PlayerTrail, Health, Delta, (Now - Knight->GetLastDamagedTime()) > 0.7f ? 3.f : 0.f);
		StaminaShown = FMath::FInterpTo(StaminaShown, Knight->GetStaminaFraction(), Delta, 18.f);
		Bar(X, Y, Knight->MaxHealth * 4.2f * S, 13.f * S, Health, PlayerTrail, HudHealthRed);
		Bar(X, Y + 26.f * S, Knight->MaxStamina * 3.4f * S, 9.f * S, StaminaShown, StaminaShown, HudStaminaGreen);

		// Flask, bottom-left.
		const float FX = 70.f * S, FY = Canvas->ClipY - 150.f * S;
		DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.45f), FX - 8.f * S, FY - 8.f * S, 76.f * S, 96.f * S);
		DrawRect(FLinearColor(HudGold.R, HudGold.G, HudGold.B, 0.4f), FX - 8.f * S, FY - 8.f * S, 76.f * S, 1.f);
		const FLinearColor FlaskColor = Knight->Flasks > 0 ? FLinearColor(0.95f, 0.55f, 0.12f) : FLinearColor(0.2f, 0.2f, 0.2f);
		DrawRect(FLinearColor(0.75f, 0.72f, 0.65f, 0.9f), FX + 24.f * S, FY + 2.f * S, 12.f * S, 12.f * S);
		DrawRect(FlaskColor, FX + 14.f * S, FY + 14.f * S, 32.f * S, 46.f * S);
		DrawRect(FLinearColor(1.f, 1.f, 1.f, 0.18f), FX + 17.f * S, FY + 18.f * S, 6.f * S, 36.f * S);
		Text(FString::FromInt(Knight->Flasks), FX + 52.f * S, FY + 52.f * S, HudParchment, 1.0f * S, false);

		// Interaction prompt.
		const ASoulsPickup* Pickup = Knight->GetNearbyPickup();
		if (Pickup || Knight->GetNearbyGrace())
		{
			const FString Prompt = Pickup ? FString::Printf(TEXT("Pick up  %s      [ E ]  /  D-pad Up"), *Pickup->GetItemName().ToString())
				: FString(TEXT("Rest at Site of Grace      [ E ]  /  D-pad Up"));
			DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.55f), Canvas->ClipX * 0.5f - 320.f * S, Canvas->ClipY * 0.74f - 10.f * S, 640.f * S, 50.f * S);
			Text(Prompt, Canvas->ClipX * 0.5f, Canvas->ClipY * 0.74f, HudParchment, 1.1f * S, true);
		}

		DrawLockOn(Knight);

		// Grapple: a hook glyph on whatever the grapple would catch.
		if (const AActor* Hook = Knight->GetGrappleCandidate())
		{
			const bool bPoint = Hook->IsA<AGrapplePoint>();
			const FVector Screen = Project(Hook->GetActorLocation() + FVector(0.f, 0.f, bPoint ? 0.f : 60.f), false);
			if (Screen.Z > 0.f)
			{
				const float R = 11.f * S;
				const FLinearColor Teal(0.5f, 0.95f, 0.85f, 0.95f);
				DrawLine(Screen.X, Screen.Y - R, Screen.X + R, Screen.Y, Teal, 2.f);
				DrawLine(Screen.X + R, Screen.Y, Screen.X, Screen.Y + R, Teal, 2.f);
				DrawLine(Screen.X, Screen.Y + R, Screen.X - R, Screen.Y, Teal, 2.f);
				DrawLine(Screen.X - R, Screen.Y, Screen.X, Screen.Y - R, Teal, 2.f);
				Text(TEXT("RT / G"), Screen.X, Screen.Y + R + 4.f * S, FLinearColor(Teal.R, Teal.G, Teal.B, 0.8f), 0.6f * S, true);
			}
		}

		// Something is leaping at you: brace!
		for (TActorIterator<ASoulsKnight> It(GetWorld()); It; ++It)
		{
			if (It->IsPouncing() && It->Team != Knight->Team && FVector::Dist(It->GetActorLocation(), Knight->GetActorLocation()) < 1400.f)
			{
				const float Pulse = 0.65f + 0.35f * FMath::Sin(Now * 18.f);
				Text(TEXT("BRACE   [ LB / LT ]"), Canvas->ClipX * 0.5f, Canvas->ClipY * 0.8f, FLinearColor(1.f, 0.85f, 0.5f, Pulse), 0.8f * S, true, TitleFont ? TitleFont.Get() : nullptr);
				break;
			}
		}

		// Shock and fear over enemies' heads.
		for (TActorIterator<ASoulsEnemy> It(GetWorld()); It; ++It)
		{
			const ASoulsEnemy* Enemy = *It;
			if (Enemy->IsDead() || FVector::Dist(Enemy->GetActorLocation(), Knight->GetActorLocation()) > 2500.f)
			{
				continue;
			}
			const TCHAR* Mark = nullptr;
			FLinearColor MarkColor;
			if (Enemy->IsShocked())
			{
				Mark = TEXT("!");
				MarkColor = FLinearColor(1.f, 0.82f, 0.3f);
			}
			else if (Enemy->IsTerrified())
			{
				Mark = TEXT("!!");
				MarkColor = FLinearColor(0.7f, 0.75f, 1.f);
			}
			else if (Enemy->IsShaken())
			{
				Mark = TEXT("?");
				MarkColor = FLinearColor(0.75f, 0.78f, 0.95f, 0.8f);
			}
			// Two patrollers passing the time: the one talking gets a murmur over its head.
			const ASoulsEnemyController* Mind = Cast<ASoulsEnemyController>(Enemy->GetController());
			if (!Mark && Mind && Mind->IsSpeaking())
			{
				static const TCHAR* Murmurs[] = { TEXT("."), TEXT(".."), TEXT("...") };
				Mark = Murmurs[static_cast<int32>(Now * 3.f) % 3];
				MarkColor = FLinearColor(0.92f, 0.88f, 0.78f, 0.85f);
			}
			if (Mark)
			{
				const FVector Screen = Project(Enemy->GetActorLocation() + FVector(0.f, 0.f, 150.f * Enemy->GetActorScale3D().Z), false);
				if (Screen.Z > 0.f)
				{
					const float Bob = 3.f * FMath::Sin(Now * 6.f);
					Text(Mark, Screen.X, Screen.Y - 20.f * S + Bob, MarkColor, 0.55f * S, true, TitleFont ? TitleFont.Get() : nullptr);
				}
			}
		}

		// Thin health bars over hurt (or targeted) enemies, as in the games.
		for (TActorIterator<ASoulsEnemy> It(GetWorld()); It; ++It)
		{
			const ASoulsEnemy* Enemy = *It;
			if (Enemy->bIsBoss || Enemy->IsDead() || FVector::Dist(Enemy->GetActorLocation(), Knight->GetActorLocation()) > 3000.f)
			{
				continue;
			}
			const bool bTargeted = Knight->GetLockTarget() == Enemy;
			if (!bTargeted && (Enemy->GetHealthFraction() >= 1.f || Now - Enemy->GetLastDamagedTime() > 10.f))
			{
				continue;
			}
			const FVector Screen = Project(Enemy->GetActorLocation() + FVector(0.f, 0.f, 125.f * Enemy->GetActorScale3D().Z), false);
			if (Screen.Z <= 0.f)
			{
				continue;
			}
			const float W = 110.f * S, H = 5.f * S;
			DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.6f), Screen.X - W * 0.5f - 1.f, Screen.Y - 1.f, W + 2.f, H + 2.f);
			DrawRect(HudHealthRed, Screen.X - W * 0.5f, Screen.Y, W * Enemy->GetHealthFraction(), H);
		}

		if (Now < HintUntil)
		{
			const float Alpha = FMath::Clamp((HintUntil - Now) / 3.f, 0.f, 1.f) * 0.9f;
			static const TCHAR* Lines[] = {
				TEXT("Light  X / LMB     Heavy  Y / F     Guard  LB / RMB  (raise it just in time to parry)"),
				TEXT("B / Shift:  roll on the move,  crouch standing still,  hold to dive prone     Sprint  tap L3 / Ctrl     Jump  A"),
				TEXT("Flask  RB / R     Lock on  R3 / MMB / Q     Parry  LT / C"),
				TEXT("Grapple  RT / G     Brace a leaping foe  LB / LT     Strike the unaware for an ambush"),
			};
			float LineY = Canvas->ClipY - 170.f * S;
			for (const TCHAR* Line : Lines)
			{
				Text(Line, Canvas->ClipX - 60.f * S - 760.f * S, LineY, FLinearColor(HudParchment.R, HudParchment.G, HudParchment.B, Alpha), 0.8f * S, false);
				LineY += 30.f * S;
			}
		}
	}

	if (GameMode)
	{
		// Runes, bottom-right.
		const FString Runes = FString::Printf(TEXT("%d"), GameMode->Runes);
		const float RX = Canvas->ClipX - 70.f * S, RY = Canvas->ClipY - 70.f * S;
		DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.45f), RX - 230.f * S, RY - 8.f * S, 240.f * S, 46.f * S);
		DrawRect(FLinearColor(HudGold.R, HudGold.G, HudGold.B, 0.5f), RX - 230.f * S, RY - 8.f * S, 240.f * S, 1.f);
		float W = 0.f, H = 0.f;
		GetTextSize(Runes, W, H, GEngine->GetLargeFont(), 1.1f * S);
		Text(Runes, RX - W, RY, HudParchment, 1.1f * S, false);
		DrawRect(HudGold, RX - 220.f * S, RY + 8.f * S, 14.f * S, 14.f * S);
		if (Now - GameMode->LastAmbushKillTime < 2.5f)
		{
			Text(TEXT("Ambush"), RX - 230.f * S, RY - 40.f * S,
				FLinearColor(1.f, 0.82f, 0.4f, FMath::Clamp(2.5f - (Now - GameMode->LastAmbushKillTime), 0.f, 1.f)), 0.45f * S, false, TitleFont ? TitleFont.Get() : nullptr);
		}
		if (Now - GameMode->LastRunesTime < 2.5f && GameMode->LastRunesGained > 0)
		{
			Text(FString::Printf(TEXT("+%d"), GameMode->LastRunesGained), RX, RY - 40.f * S,
				FLinearColor(HudGold.R, HudGold.G, HudGold.B, FMath::Clamp(2.5f - (Now - GameMode->LastRunesTime), 0.f, 1.f)), 1.f * S, false);
		}
		// Item toast, centre-low.
		const float ToastAge = Now - GameMode->ToastTime;
		if (ToastAge >= 0.f && ToastAge < 3.f)
		{
			const float Alpha = FMath::Min(FMath::Clamp(ToastAge / 0.3f, 0.f, 1.f), FMath::Clamp((3.f - ToastAge) / 0.6f, 0.f, 1.f));
			DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.55f * Alpha), Canvas->ClipX * 0.5f - 300.f * S, Canvas->ClipY * 0.66f - 8.f * S, 600.f * S, 52.f * S);
			Text(GameMode->ToastText.ToString(), Canvas->ClipX * 0.5f, Canvas->ClipY * 0.66f, FLinearColor(0.95f, 0.85f, 0.6f, Alpha), 0.5f * S, true, TitleFont ? TitleFont.Get() : nullptr);
		}
		DrawBoss(Delta);
		DrawBanner(Now);
	}
}

void ASoulsHUD::DrawLockOn(const ASoulsKnight* Knight)
{
	const ASoulsKnight* Target = Knight->GetLockTarget();
	if (!Target)
	{
		return;
	}
	const FVector Chest = Target->GetActorLocation() + FVector(0.f, 0.f, 25.f * Target->GetActorScale3D().Z);
	const FVector Screen = Project(Chest, false);
	if (Screen.Z <= 0.f)
	{
		return;
	}
	const float S = Ui();
	const float R = 7.f * S;
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.5f), Screen.X - R - 2.f, Screen.Y - R - 2.f, 2.f * R + 4.f, 2.f * R + 4.f);
	DrawRect(FLinearColor(1.f, 0.97f, 0.9f, 0.95f), Screen.X - R, Screen.Y - R, 2.f * R, 2.f * R);
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.6f), Screen.X - R * 0.4f, Screen.Y - R * 0.4f, R * 0.8f, R * 0.8f);
	// Stance, as a thin bar under the mark once it's been dented.
	const float Stance = Target->GetStanceFraction();
	if (Stance > 0.02f)
	{
		DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.6f), Screen.X - 30.f * S, Screen.Y + 16.f * S, 60.f * S, 4.f * S);
		DrawRect(HudTrailYellow, Screen.X - 30.f * S, Screen.Y + 16.f * S, 60.f * S * Stance, 4.f * S);
	}
}

void ASoulsHUD::DrawBoss(float DeltaSeconds)
{
	const ASoulsGameMode* GameMode = GetWorld()->GetAuthGameMode<ASoulsGameMode>();
	const ASoulsEnemy* Boss = GameMode ? GameMode->GetActiveBoss() : nullptr;
	if (!Boss)
	{
		BossTrail = 1.f;
		return;
	}
	const float S = Ui();
	const float Width = Canvas->ClipX * 0.56f;
	const float X = (Canvas->ClipX - Width) * 0.5f;
	const float Y = Canvas->ClipY - 120.f * S;
	const float Health = Boss->GetHealthFraction();
	const float Since = GetWorld()->GetTimeSeconds() - Boss->GetLastDamagedTime();
	BossTrail = Health > BossTrail ? Health : FMath::FInterpTo(BossTrail, Health, DeltaSeconds, Since > 0.8f ? 2.5f : 0.f);
	Text(Boss->DisplayName.ToString(), X, Y - 52.f * S, HudParchment, (TitleFont ? 0.4f : 1.0f) * S, false, TitleFont);
	Bar(X, Y, Width, 14.f * S, Health, BossTrail, HudHealthRed);
}

void ASoulsHUD::DrawBanner(float Now)
{
	const ASoulsGameMode* GameMode = GetWorld()->GetAuthGameMode<ASoulsGameMode>();
	const float Age = Now - GameMode->BannerTime;
	if (Age < 0.f || Age > GameMode->BannerDuration)
	{
		return;
	}
	const float FadeIn = FMath::Clamp(Age / 0.8f, 0.f, 1.f);
	const float FadeOut = FMath::Clamp((GameMode->BannerDuration - Age) / 1.f, 0.f, 1.f);
	const float Alpha = FMath::Min(FadeIn, FadeOut);
	const float S = Ui();
	const float CY = Canvas->ClipY * 0.5f;

	// A soft dark band across the screen, denser in the middle.
	const float BandH = 150.f * S;
	for (int32 i = 0; i < 12; ++i)
	{
		const float T = i / 11.f;
		const float RowAlpha = (1.f - FMath::Abs(T - 0.5f) * 2.f);
		DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.62f * Alpha * RowAlpha), 0.f, CY - BandH * 0.5f + T * BandH, Canvas->ClipX, BandH / 12.f + 1.f);
	}
	// YOU DIED creeps larger the way it does in the games.
	const float Grow = 1.f + 0.06f * FMath::Clamp(Age / GameMode->BannerDuration, 0.f, 1.f);
	const FLinearColor Color(GameMode->BannerColor.R, GameMode->BannerColor.G, GameMode->BannerColor.B, Alpha);
	UFont* Font = TitleFont ? TitleFont.Get() : GEngine->GetLargeFont();
	const float Scale = (TitleFont ? 1.45f : 3.2f) * S * Grow;
	float W = 0.f, H = 0.f;
	GetTextSize(GameMode->BannerText.ToString(), W, H, Font, Scale);
	DrawText(GameMode->BannerText.ToString(), Color, Canvas->ClipX * 0.5f - W * 0.5f, CY - H * 0.5f, Font, Scale);
}
