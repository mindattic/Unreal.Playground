using UnrealSharp.Attributes;
using UnrealSharp.Engine;

namespace ManagedUnrealSoulslikeCSharp.Components;

// TODO(UNR-T8): NOT PORTED YET - stub of UShadowMergeComponent (UNR-T6, still changing in C++).
// Exposes only the read API the ported HUD uses, reporting "never merged, full energy".
[UClass]
public partial class UShadowMergeComponent : UActorComponent
{
    public bool IsMerged() => false;

    public bool CanMergeNow() => false;

    public float GetEnergyFraction() => 1.0f;
}
