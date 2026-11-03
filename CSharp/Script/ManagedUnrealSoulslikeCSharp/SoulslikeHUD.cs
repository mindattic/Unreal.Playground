using ManagedUnrealSoulslikeCSharp.Components;
using UnrealSharp.Attributes;
using UnrealSharp.CoreUObject;
using UnrealSharp.Engine;
using UnrealSharp.UnrealSoulslikeCSharp;

namespace ManagedUnrealSoulslikeCSharp;

// Mirrors ASoulslikeHUD: Canvas-drawn vitals, flasks, enemy bars with awareness marks (? / !),
// the boss bar, YOU DIED, the victory banner and a controls hint.
// C++ overrides DrawHUD; C# overrides ReceiveDrawHUD, which AHUD::DrawHUD calls after the native
// pass with the canvas size (Canvas->ClipX/ClipY in the C++ build).
[UClass]
public partial class ASoulslikeHUD : AHUD
{
    private static readonly FLinearColor Backing = new(0.0f, 0.0f, 0.0f, 0.55f);
    private static readonly FLinearColor HealthRed = new(0.62f, 0.06f, 0.05f);
    private static readonly FLinearColor StaminaGreen = new(0.2f, 0.55f, 0.22f);
    private static readonly FLinearColor Flask = new(0.95f, 0.55f, 0.15f);
    private static readonly FLinearColor Parchment = new(0.86f, 0.82f, 0.7f);

    private static readonly string[] ControlLines =
    [
        "Move: L-stick / WASD     Look: R-stick / mouse",
        "Light attack: X / LMB     Heavy attack: Y / F",
        "Jump / climb-jump: A / Space     Dodge (let go when climbing): B / Shift",
        "Block (hold): LB / RMB     Parry: LT / Q     Heal: RB / R",
        "Shadow merge: R3 / E     Execute unaware skeletons: X / LMB",
    ];

    private float _deathTime = -1.0f;
    private float _victoryTime = -1.0f;
    private bool _bossSeen;
    private float _clipX;
    private float _clipY;

    private void DrawBar(float x, float y, float width, float height, float fraction, FLinearColor color)
    {
        DrawRect(Backing, x - 2.0f, y - 2.0f, width + 4.0f, height + 4.0f);
        DrawRect(color, x, y, width * SoulslikeMath.Clamp(fraction, 0.0f, 1.0f), height);
    }

    private void DrawCentredText(string text, float y, FLinearColor color, float scale)
    {
        UFont font = SoulslikeNative.EngineLargeFont();
        GetTextSize(text, out float width, out float _, font, scale);
        DrawText(text, color, (_clipX - width) * 0.5f, y, font, scale, false);
    }

    public override void ReceiveDrawHUD(int sizeX, int sizeY)
    {
        _clipX = sizeX;
        _clipY = sizeY;

        ASoulslikeCharacter? player = OwningPawn as ASoulslikeCharacter;
        float now = (float)UGameplayStatics.TimeSeconds;

        if (player != null)
        {
            DrawPlayerVitals(player);
            DrawEnemyBars(player);

            if (player.IsAlive() && player.FindExecutionTarget() != null)
            {
                DrawCentredText("Execute  [X / LMB]", _clipY * 0.62f, Parchment, 1.2f);
            }
            else if (player.IsAlive() && !player.ShadowMerge.IsMerged() && player.ShadowMerge.CanMergeNow())
            {
                DrawCentredText("Merge  [R3 / E]", _clipY * 0.62f, Parchment, 1.2f);
            }

            if (!player.IsAlive())
            {
                if (_deathTime < 0.0f)
                {
                    _deathTime = now;
                }
                float alpha = SoulslikeMath.Clamp((now - _deathTime) / 1.5f, 0.0f, 1.0f);
                DrawRect(new FLinearColor(0.0f, 0.0f, 0.0f, 0.6f * alpha), 0.0f, _clipY * 0.4f, _clipX, _clipY * 0.18f);
                DrawCentredText("YOU DIED", _clipY * 0.43f, new FLinearColor(0.65f, 0.05f, 0.04f, alpha), 3.5f);
            }
            else
            {
                _deathTime = -1.0f;
            }
        }

        if (_victoryTime >= 0.0f && now - _victoryTime < 8.0f)
        {
            float alpha = SoulslikeMath.Clamp((now - _victoryTime) / 1.0f, 0.0f, 1.0f) * SoulslikeMath.Clamp((8.0f - (now - _victoryTime)) / 1.5f, 0.0f, 1.0f);
            DrawRect(new FLinearColor(0.0f, 0.0f, 0.0f, 0.55f * alpha), 0.0f, _clipY * 0.4f, _clipX, _clipY * 0.18f);
            DrawCentredText("LAIRD VANQUISHED", _clipY * 0.43f, new FLinearColor(0.9f, 0.75f, 0.35f, alpha), 3.0f);
        }

        if (now < 25.0f)
        {
            DrawControlsHint();
        }
    }

    private void DrawPlayerVitals(ASoulslikeCharacter player)
    {
        UVitalityComponent v = player.Vitality;
        const float x = 40.0f, y = 40.0f;
        DrawBar(x, y, v.MaxHealth * 3.0f, 14.0f, v.Health / v.MaxHealth, HealthRed);
        DrawBar(x, y + 24.0f, v.MaxStamina * 3.0f, 10.0f, v.Stamina / v.MaxStamina, StaminaGreen);

        float energy = player.ShadowMerge.GetEnergyFraction();
        if (player.ShadowMerge.IsMerged() || energy < 1.0f)
        {
            DrawBar(x, y + 84.0f, 200.0f, 8.0f, energy, new FLinearColor(0.35f, 0.2f, 0.55f));
        }

        for (int i = 0; i < v.MaxHealCharges; i++)
        {
            bool full = i < v.HealCharges;
            DrawRect(Backing, x + i * 26.0f - 2.0f, y + 46.0f, 20.0f, 26.0f);
            DrawRect(full ? Flask : new FLinearColor(0.2f, 0.2f, 0.2f, 0.8f), x + i * 26.0f, y + 48.0f, 16.0f, 22.0f);
        }
    }

    private void DrawEnemyBars(ASoulslikeCharacter player)
    {
        UFont smallFont = SoulslikeNative.EngineMediumFont();
        ASkeletonEnemy? boss = null;

        UGameplayStatics.GetAllActorsOfClass<ASkeletonEnemy>(out IList<ASkeletonEnemy> skeletons);
        foreach (ASkeletonEnemy skeleton in skeletons)
        {
            double distance = SoulslikeMath.Dist(skeleton.ActorLocation, player.ActorLocation);

            if (skeleton.IsBoss)
            {
                if (skeleton.IsAlive() && distance < 3200.0)
                {
                    boss = skeleton;
                    _bossSeen = true;
                }
                else if (!skeleton.IsAlive() && _bossSeen && _victoryTime < 0.0f)
                {
                    _victoryTime = (float)UGameplayStatics.TimeSeconds;
                }
                continue;
            }

            if (!skeleton.IsAlive() || distance > 2500.0)
            {
                continue;
            }

            FVector head = SoulslikeMath.Add(skeleton.ActorLocation, new FVector(0.0, 0.0, 120.0 * skeleton.ActorScale3D.Z));
            FVector screen = Project(head, true);
            if (screen.Z <= 0.0)
            {
                continue;
            }
            float sx = (float)screen.X, sy = (float)screen.Y;

            UVitalityComponent v = skeleton.Vitality;
            if (v.Health < v.MaxHealth)
            {
                DrawBar(sx - 40.0f, sy, 80.0f, 6.0f, v.Health / v.MaxHealth, HealthRed);
            }

            if (skeleton.Awareness == EAwareness.Alerted)
            {
                DrawText("!", new FLinearColor(0.9f, 0.15f, 0.1f), sx - 4.0f, sy - 30.0f, smallFont, 1.6f, false);
            }
            else if (skeleton.Awareness == EAwareness.Suspicious)
            {
                var amber = new FLinearColor(0.95f, 0.75f, 0.2f, 0.4f + 0.6f * skeleton.Suspicion);
                DrawText("?", amber, sx - 4.0f, sy - 30.0f, smallFont, 1.6f, false);
            }
        }

        if (boss != null)
        {
            float width = _clipX * 0.5f;
            float x = (_clipX - width) * 0.5f;
            float y = _clipY - 70.0f;
            DrawText(boss.DisplayName.ToString(), Parchment, x, y - 24.0f, smallFont, 1.0f, false);
            DrawBar(x, y, width, 12.0f, boss.Vitality.Health / boss.Vitality.MaxHealth, HealthRed);
        }
    }

    private void DrawControlsHint()
    {
        UFont font = SoulslikeNative.EngineSmallFont();
        float y = _clipY - 150.0f;
        foreach (string line in ControlLines)
        {
            DrawText(line, Parchment, 40.0f, y, font, 1.2f, false);
            y += 20.0f;
        }
    }
}
