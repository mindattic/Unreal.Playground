using UnrealSharp.Attributes;
using UnrealSharp.Engine;

namespace ManagedUnrealSoulslikeCSharp;

// TODO(UNR-T8): NOT PORTED YET - stub of AHebridesIsland (terrain, sea, sky, ruins, buried kit
// pieces, the loch bridge and enemy spawning; still changing under UNR-T7). The ported game mode
// spawns it exactly as the C++ one does; until the port lands it builds nothing.
[UClass]
public partial class AHebridesIsland : AActor
{
    public AHebridesIsland()
    {
        Replicates = true;
    }
}
