---
project: Unreal.Playground
layer: design
status: living
---

# Design: Unreal.Playground

## Pitch

A co-op souls-like demo on a fictional skerry in the Outer Hebrides. One to four players land on
the beach and take the island from its skeletons, ending at the ruined keep and the Hollow Laird.
You can fight through, climb around, or slip past unseen as a shadow. There's no PvP, and every
rule applies the same way to every player.

It's also a test bed. The same game is built twice, in C++ and in C# (UnrealSharp), so the two
can be compared on real numbers (UNR-T8).

## The island

- Shape: a 257×257 heightmap from `tools/generate_heightmap.py`, at 3 m per cell. The island is
  about 460 m across and peaks at about 45 m. It has a ragged coast, two headlands, a sea loch
  winding in from the west, low machair and heather moor, and a rocky bowl in the middle that
  holds the keep.
- Data: `Content/Terrain/HebridesHeightmap.r16` (raw uint16) and `HebridesLandmarks.json`. The
  landmarks file holds UV positions for the beach, player starts, crofts, broch, standing stones,
  keep, boss, skeleton spawns, and the bridge and its guards. Every machine builds identical
  terrain from these files, so co-op clients match the server exactly.
- Areas, from the landing:
  1. **Landing beach and dunes**: player starts and respawn point. Cottages and wall runs lie
     half-buried in the sand, and all of them are merge-able.
  2. **Crofts and field walls**: the first skeletons, and the first chance to practise stealth.
  3. **The loch bridge**: a stone bridge crowded with skeletons. You can fight across it, climb
     around, or merge into the bridge and crawl along it unseen.
  4. **Standing stones**: a Callanish-style circle with an avenue running north.
  5. **The broch**: a ruined Iron Age tower with a doorway facing the beach.
  6. **The keep**: walls, towers and a gate facing the beach, with the boss arena inside.
- Weather: an open sea out to the horizon, a low northern sun, a sky atmosphere, a real-time sky
  light, and heavy volumetric mist that hides skeletons until they're close.

## Co-op model

- Listen server for the demo. A dedicated server would use the same game mode.
- Combat state, vitals, awareness, climbing and merge state all replicate. Damage, stamina,
  executions and enemy AI are decided by the server.
- A dead player respawns on the beach after 6 seconds with full health and flasks.
  Skeletons don't respawn.

## Combat

| Action | Pad | KB/M | Cost | Rule |
|---|---|---|---|---|
| Light attack | X | LMB | 12 st | 0.25 s wind-up, then one hit sweep (18 dmg) |
| Heavy attack | Y | F | 24 st | 0.5 s wind-up (36 dmg) |
| Dodge | B | Shift | 20 st | 0.35 s i-frames |
| Block (hold) | LB | RMB | 1 st per damage | 85% reduction; running out of stamina breaks your guard |
| Parry | LT | Q | 6 st | 0.3 s window; a hit landing in it is negated and staggers the attacker for 1.2 s |
| Heal | RB | R | 1 of 3 flasks | 1.6 s cast; getting hit cancels it |

- A clean hit stuns for 0.4 s, so getting hit interrupts attacks and heals.
- The boss can't be hit-stunned. Only a parry or a guard break opens it up.
- Attacks soft-lock onto the nearest enemy within 4.5 m.

## Skeletons

- Perception: a 60° sight cone out to 22 m, with line of sight required. They hear you within
  4.5 m if you're running or attacking, and notice you instantly within 1.2 m.
- A suspicion meter builds while they sense you. Past 35% they're suspicious (?) and investigate;
  at 100% they're alerted (!) and alert every skeleton within 12 m. They lose interest 8 seconds
  after losing sight of you.
- Fighting: telegraphed attacks (0.55 s / 0.85 s wind-ups), and shield-bearers sometimes block.
  They leash back home 35 m from their spawn.
- Stealth execution (UNR-T5): you can execute an unaware skeleton from any side, or a suspicious
  one from behind. It dies silently. The boss can't be executed.

## Traversal

- **Jump and free climbing (UNR-T4)**: you can climb any surface steeper than a walkable floor,
  including cliffs, walls and stones. Climbing drains stamina, A climb-jumps, reaching a ledge
  mantles you up, and B lets go.
- **Shadow merge (UNR-T6)**: press R3 or E next to a merge-able surface (stone, brick, the
  bridge) or a surface in shadow, and you become a black splotch. The splotch crawls across
  floors, walls and ceilings and from one connected surface to the next, and nothing can see or
  hear you. It drains shadow energy, faster in light, and you're forced out when the energy runs
  out. A light attack next to an unaware skeleton pops you out straight into an execution.

## Art

- Characters: the CC0 KayKit Adventurers knight and KayKit Skeletons (warrior, minion, rogue;
  the boss is a 1.5× warrior). They share one 41-bone rig, so one animation set drives them all.
  Animations play directly in C++ (single-node), with no Animation Blueprint.
- Environment: the CC0 KayKit Dungeon Remastered walls, gates, stairs, pillars and bridge
  blocks, and KayKit Medieval Hexagon cottages, towers and ruins. If any of these are missing,
  engine primitive shapes are used instead, so the game always runs.
- Credits: `SourceArt/CREDITS.md` and `SourceArt/Environment/CREDITS.md`.

## Pipeline

1. `python -I tools/generate_heightmap.py` builds the terrain data and the preview image.
2. `tools/build.ps1` compiles the editor target. It then runs `tools/build_content.py` in the
   editor, which imports the art, creates the materials (and input assets for the C# twin),
   builds the map and writes `CharacterAssets.json` and `EnvironmentAssets.json`.
3. `tools/run.ps1 [-Players N]` plays the game, solo or as co-op on one machine.
4. `tools/package.ps1` produces `Builds/Cpp/Unreal.Playground.Cpp.exe` and
   `Builds/CSharp/Unreal.Playground.CSharp.exe`.
5. `tools/compare.ps1` runs both builds with `-DemoBot -Benchmark=60` and writes
   `docs/COMPARISON.md`.
