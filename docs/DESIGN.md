---
project: Unreal.Soulslike
layer: design
status: living
---

# Design — Unreal.Soulslike

## Pitch

A co-op souls-like demo. 1–4 players land on an isolated island in the Outer Hebrides and fight
their way to its ruined keep. No PvP; every rule below applies identically to every human player
and to AI enemies.

## Setting: the island

- **Place**: a fictional skerry off the Outer Hebrides — basalt cliffs, peat bog, standing stones,
  a sea mist that never fully lifts. Weather is a constant: wind, rain, and fog that cuts draw
  distance and hides enemies until they're close.
- **The crofts**: a cluster of abandoned stone crofts and an Iron Age broch near the landing beach
  — the demo's first area, mostly empty, teaching movement/dodge/light-attack against a couple of
  weak enemies (feral sheep-things, driven mad by whatever happened here).
  - A whole Unreal landscape, foliage pass, and handcrafted crofts/broch meshes are large,
    editor-authored content outside what this scaffold can generate; the brief above is the spec
    for whoever builds that level.
- **The keep**: a ruined clifftop fortification at the island's center — the demo's combat proving
  ground and its one mini-boss.

## Co-op model

- Session-based, drop-in: any connected player's `ASoulslikeCharacter` is spawned and ticked the
  same way, server-authoritative (listen server for the demo; dedicated server is a drop-in swap
  of the same `ASoulslikeGameMode`/`ASoulslikePlayerController`).
- No shared health pool, no friendly fire by default, no player-vs-player damage.
- Vitals (`UVitalityComponent`) and combat state (`UCombatComponent`) are both replicated per
  character, so every client renders every other player's health, stamina, heal charges, and
  current action (idle/attacking/blocking/parrying/dodging/healing/staggered) correctly.

## Combat rules

| Action | Input (gamepad) | Stamina cost | Effect |
|---|---|---|---|
| Move / look | Left stick / right stick | — | Standard third-person locomotion |
| Light attack | **X** | 12 | Fast swing, 18 damage, short recovery |
| Heavy attack | **Y** | 24 | Slow swing, 36 damage, longer recovery |
| Dodge roll | **A** | 20 | ~0.35s invulnerability window, repositions the character |
| Block | **LB** (hold) | per hit absorbed | Reduces incoming damage to 15% while held |
| Parry | **LT** (tap while blocking) | 6 | 0.3s window: a landed hit in this window is fully
negated and staggers the attacker instead |
| Heal | **RB** | — (consumes 1 of 3 charges) | 1.6s interruptible cast, restores health |

Getting hit cancels an attack or a heal in progress (`UCombatComponent::InterruptOnHit`), so both
are a real commitment, not a free action. Stamina regenerates after a short delay of not spending
it, so stamina management (not just health) is the core resource loop.

## What's implemented vs. what's left

**Implemented** (`Source/UnrealSoulslike/`): the full server-authoritative combat/vitality state
machine described above, an Enhanced-Input-ready third-person character, a co-op game mode and
player controller, and the config to wire the game mode in by default.

**Left for the Unreal Editor** (binary/editor-only content this scaffold cannot generate as text):
- The Enhanced Input assets themselves — an `Input Mapping Context` and one `Input Action` per row
  in the table above, assigned on a Blueprint child of `ASoulslikeCharacter` (or on the class
  defaults directly). The bindings above are the intended defaults for whoever creates them.
- A skeletal mesh, animation blueprint, and montages for locomotion, the roll, block/parry poses,
  light/heavy swings, and the heal cast.
- The island level itself: landscape, foliage, the crofts/broch area, and the keep, per the
  setting brief above, with `PlayerStart`s for co-op spawning and the enemies/mini-boss as
  `ACharacter` subclasses reusing `UVitalityComponent` (and `UCombatComponent` where an enemy
  should be parryable).
- Network transport setup for actual multiplayer sessions beyond a LAN listen server (Steam/EOS
  via `OnlineSubsystem`, currently left disabled in the `.uproject`).
