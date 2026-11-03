#include "SoulslikeHUD.h"
#include "SoulslikeCharacter.h"
#include "SkeletonEnemy.h"
#include "Components/CombatComponent.h"
#include "Components/VitalityComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "EngineUtils.h"

namespace
{
	const FLinearColor Backing(0.f, 0.f, 0.f, 0.55f);
	const FLinearColor HealthRed(0.62f, 0.06f, 0.05f);
	const FLinearColor StaminaGreen(0.2f, 0.55f, 0.22f);
	const FLinearColor Flask(0.95f, 0.55f, 0.15f);
	const FLinearColor Parchment(0.86f, 0.82f, 0.7f);
}

void ASoulslikeHUD::DrawBar(float X, float Y, float Width, float Height, float Fraction, const FLinearColor& Color)
{
	DrawRect(Backing, X - 2.f, Y - 2.f, Width + 4.f, Height + 4.f);
	DrawRect(Color, X, Y, Width * FMath::Clamp(Fraction, 0.f, 1.f), Height);
}

void ASoulslikeHUD::DrawCentredText(const FString& Text, float Y, const FLinearColor& Color, float Scale)
{
	UFont* Font = GEngine->GetLargeFont();
	float Width = 0.f, Height = 0.f;
	GetTextSize(Text, Width, Height, Font, Scale);
	DrawText(Text, Color, (Canvas->ClipX - Width) * 0.5f, Y, Font, Scale);
}

void ASoulslikeHUD::DrawHUD()
{
	Super::DrawHUD();
	if (!Canvas)
	{
		return;
	}

	const ASoulslikeCharacter* Player = Cast<ASoulslikeCharacter>(GetOwningPawn());
	const float Now = GetWorld()->GetTimeSeconds();

	if (Player)
	{
		DrawPlayerVitals(Player);
		DrawEnemyBars(Player);

		if (Player->IsAlive() && Player->FindExecutionTarget())
		{
			DrawCentredText(TEXT("Execute  [X / LMB]"), Canvas->ClipY * 0.62f, Parchment, 1.2f);
		}

		if (!Player->IsAlive())
		{
			if (DeathTime < 0.f)
			{
				DeathTime = Now;
			}
			const float Alpha = FMath::Clamp((Now - DeathTime) / 1.5f, 0.f, 1.f);
			DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.6f * Alpha), 0.f, Canvas->ClipY * 0.4f, Canvas->ClipX, Canvas->ClipY * 0.18f);
			DrawCentredText(TEXT("YOU DIED"), Canvas->ClipY * 0.43f, FLinearColor(0.65f, 0.05f, 0.04f, Alpha), 3.5f);
		}
		else
		{
			DeathTime = -1.f;
		}
	}

	if (VictoryTime >= 0.f && Now - VictoryTime < 8.f)
	{
		const float Alpha = FMath::Clamp((Now - VictoryTime) / 1.f, 0.f, 1.f) * FMath::Clamp((8.f - (Now - VictoryTime)) / 1.5f, 0.f, 1.f);
		DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.55f * Alpha), 0.f, Canvas->ClipY * 0.4f, Canvas->ClipX, Canvas->ClipY * 0.18f);
		DrawCentredText(TEXT("LAIRD VANQUISHED"), Canvas->ClipY * 0.43f, FLinearColor(0.9f, 0.75f, 0.35f, Alpha), 3.f);
	}

	if (Now < 25.f)
	{
		DrawControlsHint();
	}
}

void ASoulslikeHUD::DrawPlayerVitals(const ASoulslikeCharacter* Player)
{
	const UVitalityComponent* V = Player->Vitality;
	const float X = 40.f, Y = 40.f;
	DrawBar(X, Y, V->MaxHealth * 3.f, 14.f, V->Health / V->MaxHealth, HealthRed);
	DrawBar(X, Y + 24.f, V->MaxStamina * 3.f, 10.f, V->Stamina / V->MaxStamina, StaminaGreen);

	for (int32 i = 0; i < V->MaxHealCharges; ++i)
	{
		const bool bFull = i < V->HealCharges;
		DrawRect(Backing, X + i * 26.f - 2.f, Y + 46.f, 20.f, 26.f);
		DrawRect(bFull ? Flask : FLinearColor(0.2f, 0.2f, 0.2f, 0.8f), X + i * 26.f, Y + 48.f, 16.f, 22.f);
	}
}

void ASoulslikeHUD::DrawEnemyBars(const ASoulslikeCharacter* Player)
{
	UFont* SmallFont = GEngine->GetMediumFont();
	const ASkeletonEnemy* Boss = nullptr;

	for (TActorIterator<ASkeletonEnemy> It(GetWorld()); It; ++It)
	{
		const ASkeletonEnemy* Skeleton = *It;
		const float Distance = FVector::Dist(Skeleton->GetActorLocation(), Player->GetActorLocation());

		if (Skeleton->bIsBoss)
		{
			if (Skeleton->IsAlive() && Distance < 3200.f)
			{
				Boss = Skeleton;
				bBossSeen = true;
			}
			else if (!Skeleton->IsAlive() && bBossSeen && VictoryTime < 0.f)
			{
				VictoryTime = GetWorld()->GetTimeSeconds();
			}
			continue;
		}

		if (!Skeleton->IsAlive() || Distance > 2500.f)
		{
			continue;
		}

		const FVector Head = Skeleton->GetActorLocation() + FVector(0.f, 0.f, 120.f * Skeleton->GetActorScale3D().Z);
		const FVector Screen = Project(Head, true);
		if (Screen.Z <= 0.f)
		{
			continue;
		}

		const UVitalityComponent* V = Skeleton->Vitality;
		if (V->Health < V->MaxHealth)
		{
			DrawBar(Screen.X - 40.f, Screen.Y, 80.f, 6.f, V->Health / V->MaxHealth, HealthRed);
		}

		if (Skeleton->Awareness == EAwareness::Alerted)
		{
			DrawText(TEXT("!"), FLinearColor(0.9f, 0.15f, 0.1f), Screen.X - 4.f, Screen.Y - 30.f, SmallFont, 1.6f);
		}
		else if (Skeleton->Awareness == EAwareness::Suspicious)
		{
			const FLinearColor Amber(0.95f, 0.75f, 0.2f, 0.4f + 0.6f * Skeleton->Suspicion);
			DrawText(TEXT("?"), Amber, Screen.X - 4.f, Screen.Y - 30.f, SmallFont, 1.6f);
		}
	}

	if (Boss)
	{
		const float Width = Canvas->ClipX * 0.5f;
		const float X = (Canvas->ClipX - Width) * 0.5f;
		const float Y = Canvas->ClipY - 70.f;
		DrawText(Boss->DisplayName.ToString(), Parchment, X, Y - 24.f, SmallFont, 1.f);
		DrawBar(X, Y, Width, 12.f, Boss->Vitality->Health / Boss->Vitality->MaxHealth, HealthRed);
	}
}

void ASoulslikeHUD::DrawControlsHint()
{
	static const TCHAR* Lines[] = {
		TEXT("Move: L-stick / WASD     Look: R-stick / mouse"),
		TEXT("Light attack: X / LMB     Heavy attack: Y / F"),
		TEXT("Dodge: A / Space     Block (hold): LB / RMB     Parry: LT / Q"),
		TEXT("Heal: RB / R"),
	};
	UFont* Font = GEngine->GetSmallFont();
	float Y = Canvas->ClipY - 150.f;
	for (const TCHAR* Line : Lines)
	{
		DrawText(Line, Parchment, 40.f, Y, Font, 1.2f);
		Y += 20.f;
	}
}
