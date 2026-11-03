using ManagedUnrealSoulslikeCSharp.Components;
using UnrealSharp.Attributes;
using UnrealSharp.Core.Attributes;
using UnrealSharp.Engine;

namespace ManagedUnrealSoulslikeCSharp;

// TODO(UNR-T8): NOT PORTED YET - stub of ASoulslikeCharacter (SoulslikeCharacter.h/.cpp), which is
// still changing in the C++ build (climbing UNR-T4, shadow merge UNR-T6). It only carries what the
// ported GameMode/HUD reference: the knight visual, the player team, the respawn hook, and
// placeholders for execution and shadow merge. No camera, input, climbing or merge yet, so the C#
// build isn't playable until this is ported 1:1. See CSharp/PORT_STATUS.md.
[UClass]
public partial class ASoulslikeCharacter : ASoulslikeCombatant
{
    public ASoulslikeCharacter()
    {
        DisplayName = new UnrealSharp.Core.FText("Knight");
        ExecutionRange = 220.0f;
    }

    [UProperty(DefaultComponent = true), Category("Soulslike|Traversal")]
    public partial UShadowMergeComponent ShadowMerge { get; set; }

    [UProperty(PropertyFlags.EditAnywhere | PropertyFlags.BlueprintReadOnly), Category("Soulslike|Combat")]
    public partial float ExecutionRange { get; set; }

    public override void ConstructionScript()
    {
        base.ConstructionScript();
        Combat.Team = SoulslikeTeam.Players;
    }

    public override void BeginPlay()
    {
        base.BeginPlay();
        ApplyVisual(SoulslikeAssets.Get().Knight);
    }

    // TODO(UNR-T8): port FindExecutionTarget from SoulslikeCharacter.cpp.
    public ASkeletonEnemy? FindExecutionTarget() => null;

    public override bool IsConcealed() => ShadowMerge != null && ShadowMerge.IsMerged();

    protected override void OnDied()
    {
        if (World.GameMode is ASoulslikeGameMode gameMode)
        {
            gameMode.NotifyPlayerDied(Controller);
        }
    }
}
