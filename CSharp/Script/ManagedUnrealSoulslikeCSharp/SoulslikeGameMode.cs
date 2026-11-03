using UnrealSharp;
using UnrealSharp.Attributes;
using UnrealSharp.Core.Attributes;
using UnrealSharp.CoreUObject;
using UnrealSharp.Engine;

namespace ManagedUnrealSoulslikeCSharp;

// Mirrors ASoulslikeGameMode: every player lands on the beach (one of the island's player
// starts), the island is spawned if the map doesn't hold one, and a dead player respawns on the
// beach after RespawnDelay with full vitals and flasks. No PvP.
//
// C# differences forced by reflection (behaviour is the same):
//  - StartPlay (C++ virtual) -> BeginPlay: the pawn already exists and nothing ticks until after;
//  - FinishRestartPlayer (C++ virtual) -> the OnRestartPlayer event, which the engine raises at the
//    end of FinishRestartPlayer, re-applying the pawn's keep-facing rotation;
//  - the respawn timer carries a controller argument, which SystemLibrary.SetTimer can't, so pending
//    respawns are kept here and released from Tick on game time (same clock as FTimerManager).
[UClass]
public partial class ASoulslikeGameMode : AGameModeBase
{
    public ASoulslikeGameMode()
    {
        DefaultPawnClass = typeof(ASoulslikeCharacter);
        PlayerControllerClass = typeof(ASoulslikePlayerController);
        HUDClass = typeof(ASoulslikeHUD);
        UseSeamlessTravel = true;
        RespawnDelay = 6.0f;
    }

    [UProperty(PropertyFlags.EditAnywhere), Category("Soulslike")]
    public partial float RespawnDelay { get; set; }

    private int _nextStartIndex;
    private readonly List<(AController Player, double DueTime)> _pendingRespawns = [];

    public override void BeginPlay()
    {
        if (UGameplayStatics.GetActorOfClass<AHebridesIsland>() == null)
        {
            SpawnActor<AHebridesIsland>(typeof(AHebridesIsland), new FTransform());
        }
        base.BeginPlay();
    }

    public override APawn SpawnDefaultPawnFor(AController newPlayer, AActor startSpot)
    {
        HebridesTerrain data = HebridesTerrain.Get();
        FVector location;
        FRotator rotation;
        if (data.IsValid())
        {
            FVector2D start = data.PlayerStarts[_nextStartIndex++ % data.PlayerStarts.Count];
            location = SoulslikeMath.Add(data.ToWorld(start), new FVector(0.0, 0.0, 120.0));
            FVector toKeep = SoulslikeMath.Sub(data.ToWorld(data.Keep), location);
            rotation = new FRotator(0.0, SoulslikeMath.YawOf(toKeep), 0.0);
        }
        else
        {
            // C++ falls back to the engine default (the start spot's transform).
            location = startSpot != null ? startSpot.ActorLocation : new FVector(0.0, 0.0, 0.0);
            rotation = startSpot != null ? startSpot.ActorRotation : new FRotator(0.0, 0.0, 0.0);
        }

        APawn pawn = SpawnActor<APawn>(DefaultPawnClass, new FTransform(rotation, location),
            ESpawnActorCollisionHandlingMethod.AdjustIfPossibleButAlwaysSpawn, Instigator, null);
        if (pawn != null && newPlayer != null)
        {
            newPlayer.ControlRotation = rotation;
        }
        return pawn!;
    }

    // With no PlayerStart the engine passes a zero rotation; keep the pawn's keep-facing one.
    public override void OnRestartPlayer(AController newPlayer)
    {
        APawn? pawn = newPlayer?.ControlledPawn;
        if (pawn != null)
        {
            newPlayer!.ControlRotation = new FRotator(pawn.ActorRotation.Pitch, pawn.ActorRotation.Yaw, 0.0);
        }
    }

    public void NotifyPlayerDied(AController? player)
    {
        if (player == null)
        {
            return;
        }
        _pendingRespawns.Add((player, UGameplayStatics.TimeSeconds + RespawnDelay));
    }

    public override void Tick(float deltaSeconds)
    {
        base.Tick(deltaSeconds);

        double now = UGameplayStatics.TimeSeconds;
        for (int i = _pendingRespawns.Count - 1; i >= 0; i--)
        {
            if (_pendingRespawns[i].DueTime <= now)
            {
                AController player = _pendingRespawns[i].Player;
                _pendingRespawns.RemoveAt(i);
                Respawn(player);
            }
        }
    }

    private void Respawn(AController? controller)
    {
        if (controller == null || controller.IsDestroyed)
        {
            return;
        }
        APawn? oldPawn = controller.ControlledPawn;
        if (oldPawn != null)
        {
            controller.UnPossess();
            oldPawn.DestroyActor();
        }
        RestartPlayer(controller);
    }
}
