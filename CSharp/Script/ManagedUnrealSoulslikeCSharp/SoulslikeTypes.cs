using UnrealSharp.Attributes;

namespace ManagedUnrealSoulslikeCSharp;

// Mirrors Source/UnrealSoulslike/Public/SoulslikeTypes.h.

[UEnum]
public enum ECombatState : byte
{
    Idle,
    Attacking,
    Blocking,
    Parrying,
    Dodging,
    Healing,
    Staggered,
    Dead,
}

[UEnum]
public enum EAttackType : byte
{
    Light,
    Heavy,
}

[UEnum]
public enum EAwareness : byte
{
    Unaware,
    Suspicious,
    Alerted,
}

public static class SoulslikeTeam
{
    public const byte Players = 0;
    public const byte Skeletons = 1;
}

public static class SoulslikeTags
{
    public const string Mergeable = "Mergeable";
}
