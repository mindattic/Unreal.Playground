---
layer: bible
status: living
---
# Unreal.Playground — Project Bible

> Single source of truth for what Unreal.Playground IS, is NOT, and the rules that keep it coherent.
> README says how to build and run it; this says how to think about it. The finish line lives in
> `docs/RELEASE.md`.

## 1. The one sentence {#UNR-§1}
An Elden Ring-style action vertical slice in Unreal Engine 5.8: wash up on a Hebridean island, fight through its
creatures and fell two bosses.

## 2. The product promise {#UNR-§2}
- A single island (`/Game/Souls/Maps/Lvl_Isle`) walked from the beach through machair dunes, woodland, a ruined
  keep, a peat marsh and heather highland to a clifftop stone circle.
- Souls combat that reads well: telegraphed enemy swings, parry and riposte, backstabs, poise, hitstop, lock-on.
- Two bosses: the Callanish Warden in the stone circle and Morrigan of the Mire, a shade who melts into shadow.
- Elden Ring trappings: Sites of Grace, runes lost on death, YOU DIED, GREAT ENEMY FELLED.
- Every move is always available: there is no stamina.

## 3. What it is NOT {#UNR-§3}
- Not a full game: one island, one milestone (see `docs/RELEASE.md`).
- Not a C# showcase: the UnrealSharp twin under `CSharp/` is frozen; the slice is the C++ build.
- Not a co-op or PvP game for this milestone, though the early code was written for co-op.
- Not a platformer: the bunny-mascot moveset was dropped (2026-11-27).

## 4. Architecture canon {#UNR-§4}
```text
UnrealSoulslike.uproject  (UE 5.8, module UnrealSoulslike)
  Source/UnrealSoulslike/
    Souls/          the shipping game: knight, fighters, enemies, bestiary, bosses, grace, grapple,
                    torches, fire, blood, mech, wall run, roster, HUD, game mode
    Environment/    island builder, day/night, weather, screenshot director
    ThirdPerson/    Epic's Third Person template (Combat, Platforming, SideScrolling variants)
    Public/Private/ pre-pivot KayKit-era code (Soulslike*, HebridesIsland, SkeletonEnemy, climb and
                    shadow-merge components); some still used by SoulsKnight, the rest to retire (UNR-T147)
  tools/            build.ps1, run.ps1, package.ps1 + editor-Python content builders and asset converters
  SourceArt/        CC0 / CC-BY source art with CREDITS; Content/ is generated, not committed
Plugins (side by side under D:\Projects\MindAttic):
  ../Unreal.Shared/Plugins            MindAtticShared (--headless)
  ../MindAttic.Media.Capture/unreal   MindAtticMediaCapture (-capture video)
```

### 4.1 Domain model (the nouns)
Knight (the player; Ranger body), fighter, enemy, monster (bestiary type), boss, Site of Grace, rune, flask,
Golden Seed, Crimson Tear, grapple point, torch, mech.

### 4.2 Key services (the verbs)
- `tools/build.ps1`: compile the editor target, then run the editor-Python builders that make the knight, the
  bestiary, the shadow boss and the island.
- `tools/run.ps1`: play Lvl_Isle (with capture, `-Headless`, `-Preview`, demo flags through `-Extra`).
- `tools/package.ps1` / `run.ps1`: package DebugGame and Shipping and deploy them to `R:\MindAttic\Apps`.

## 5. The Laws {#UNR-§5}
Inherits `MindAttic.HouseRules.md` by reference.
1. `.uasset`/`.umap` are binary: never hand-write them; content comes from the `tools/` builders or the editor.
   {#UNR-LAW-1}
2. The player and every enemy share one combat kit; a rule applies the same way to both. {#UNR-LAW-2}
3. Art is CC0 or credited CC-BY, with its source and licence under `SourceArt/`. {#UNR-LAW-3}
4. Every mechanic has an offscreen check flag (`-XxxDemo`/`-XxxShots`) that screenshots it and logs PASS/FAIL;
   automation boots straight into Lvl_Isle. {#UNR-LAW-4}
5. Nothing new joins the slice unless it is in `docs/RELEASE.md`. {#UNR-LAW-5}

## 6. Verified state {#UNR-§6}
See `docs/RELEASE.md` §6 for what works today with commit evidence.

## 7. Active frontier {#UNR-§7}
`docs/RELEASE.md`: MS-12, a title screen, pause menu and ending around the existing slice, a clean Release
playthrough, the Silver Soldier enemy, and retiring pre-pivot code.

## 8. Quality bar {#UNR-§8}
A feature is done when the C++ targets build clean, it works in the Shipping package, its offscreen flag (if any)
passes, README describes it as it plays, and its commit names its ticket.

## 9. Glossary {#UNR-§9}
- **Pre-pivot**: the first design (KayKit art, a skerry, co-op, a skeleton-guarded bridge), replaced on 2026-11-03
  by the Elden Ring slice on Epic's Manny.
- **Slice**: the one island played start to finish.
- **Site of Grace**: rest point that refills flasks and respawns enemies.
