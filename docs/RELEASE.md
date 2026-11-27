---
layer: release
status: living
---
# Unreal.Playground — Release charter

> The one finish line this repo is working toward. Every open ticket in this repo sits under one of the epics
> below, and every epic is part of the milestone. Work that does not fit is out of scope until this milestone is
> reached (owner, 2026-11-27, MAT-T319).

## 1. What this is
An Elden Ring-style action game vertical slice in Unreal Engine 5.8 (C++): wash up on a Hebridean island, fight
through its bestiary and fell two bosses. It is also the owner's test bed for gameplay mechanics.

## 2. The milestone: Unreal.Playground playable polished vertical slice {#milestone}
- **Milestone:** MS-12, "Unreal.Playground playable polished vertical slice"
- **Published means:** an internal build on `R:\MindAttic\Apps\Unreal.Playground\Release` (Shipping) and `\Debug`
  (DebugGame), deployed by `run.ps1`. Not shipped to a store.
- **The natural stopping point:** the systems are already built (combat, grapple, stealth, bestiary, two bosses,
  Sites of Grace, weather, fire, mech). What is missing is the frame around them: a title screen, a pause menu, an
  ending once both bosses fall, a clean playthrough of the Release package, and code and docs that match the game.
  With those, the slice is a complete thing you can hand to someone, and adding more mechanics stops.

## 3. Done when (acceptance checks)
1. build — the UnrealSoulslike C++ editor and game targets build clean on main (`tools/build.ps1`).
2. exists — the Shipping package at `R:\MindAttic\Apps\Unreal.Playground\Release` starts.
3. signoff — the owner plays the Release package from the title screen through both bosses to the ending.

## 4. Epics, in priority order
| Priority | Epic (ticket) | What it delivers | Features |
|---|---|---|---|
| 900 | UNR-T129 Complete game loop | Title, pause and ending, so it plays start to finish | UNR-T133 Title and pause menus (990), UNR-T134 Ending (980), UNR-T135 Playthrough pass (970) |
| 800 | UNR-T130 Combat and enemy polish | Grapple animation beats; the Silver Soldier enemy | UNR-T136 Grapple animation (890), UNR-T137 Silver Soldier enemy (880) |
| 700 | UNR-T131 Stability and release | No known bugs; packages on R: that start and hold frame rate | UNR-T138 Bug fixing (790), UNR-T139 Release packages (780) |
| 600 | UNR-T132 Code and docs cleanup | Pre-pivot code retired; DESIGN.md matches the game | UNR-T140 Retire pre-pivot code (690), UNR-T141 Docs match the game (680) |

Tasks: UNR-T142 title screen, UNR-T143 pause menu, UNR-T144 ending after both bosses, UNR-T145 full Release
playthrough; UNR-T45 grapple beats; UNR-T84 Silver Soldier (UNR-T99 convert, UNR-T109 retarget and BP, UNR-T110
subclass and credit); UNR-T115 exhaustive bughunt; UNR-T127 packages on R:, UNR-T146 Shipping performance baseline;
UNR-T147 remove unused pre-pivot classes; UNR-T148 rewrite DESIGN.md.

## 5. Out of scope (not before this milestone)
A new request that lands here is filed `proposed`, never approved automatically.
- The C# (UnrealSharp) twin and the C++ vs C# benchmark comparison (`CSharp/`, `tools/compare.ps1`) — frozen as is;
  the slice is the C++ build.
- New playable characters (the bunny mascot and its Mario-style moveset) — a different genre from a souls slice.
- New traversal or vehicle systems beyond what already ships (mech, grapple, wall run, shadow melt, swimming).
- New biomes, bosses or enemy types beyond the Silver Soldier.
- Co-op and dedicated-server work; the slice is judged single-player.
- Live Niagara Fluids fire (`souls.Fire.Fluid 1`) — corrupts memory on UE 5.8; the baked flipbook fire is the fire.
- Soaks, nightly runs and ever-growing demo flags.

## 6. Current verified state
- Lvl_Isle island with five biomes, sea, islet and broch (7e9d719, eeb1f38, 31eca47).
- Elden Ring combat kit: combo, heavy, roll, parry/riposte, backstab, poise, lock-on, flasks, no stamina (91f38f1,
  d996e96, b7ba134).
- Two bosses, Callanish Warden and Morrigan of the Mire, with boss bars and GREAT ENEMY FELLED (91f38f1, 111de3b).
- Stealth crouch/crawl and patrols (f3cd2d1, d01a822); grapple (3b6adda, efb7298); six-type bestiary and Ranger
  body (091f47d); mech (41bd8b9); wall running (5cb0068); shadow melt (0b7fad6); fire (2ff3a41); weather (a1980d2).
- In-game capture and preview sets (fb10105, 266da3f); shared run menu and package deploy (13e3fa8).
- Offscreen verification flags (`-KnightDemo`, `-MechDemo`, `-BestiaryShots` ...) listed in README.

## 7. Decision record
- 2026-11-27 — charter drafted from README, DESIGN.md, the git log and the open tickets; Legion vote: Approve,
  9 of 9 voters (plurality quorum reached, consensus 1.0); no dissent.
- Triage: 35 open → done 21, duplicate 0, wont-do 7, kept 7; new: 4 epics, 9 features, 7 tasks (UNR-T128).
