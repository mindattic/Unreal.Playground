# Unreal.Soulslike

A co-op souls-like demo built in Unreal Engine: third-person combat on an isolated island in the
Outer Hebrides, playable solo or with friends dropped into the same world.

## Status

This repo currently holds the gameplay-code scaffold: a C++ module with a networked combat and
vitality system, ready to open in the Unreal Editor. It does not yet include a map, meshes,
animations, or the Enhanced Input data assets — those are authored in-editor (see
[docs/DESIGN.md](docs/DESIGN.md) for what's left and the intended control scheme).

## Combat

- **Dodge roll** — a short burst of invulnerability, costs stamina.
- **Block** — hold to cut incoming damage down to a fraction; costs stamina per hit absorbed.
- **Parry** — a precise window within a block that negates damage entirely and staggers the
  attacker.
- **Light / heavy attacks** — fast low-damage swing vs. slow high-damage swing, each with its own
  stamina cost and recovery time.
- **Heal** — a limited number of charges (estus-style), each a short interruptible cast that
  restores health.

All of it is server-authoritative and replicated, so every co-op player and AI enemy sees the same
state.

## Setting

An isolated island off the Outer Hebrides: weather-beaten cliffs, an abandoned broch and crofts,
and a single ruined keep at its center holding whatever drove the island's people out. See
[docs/DESIGN.md](docs/DESIGN.md) for the full pitch.

## Opening the project

Requires Unreal Engine 5.5 (or update `EngineAssociation` in `Unreal.Soulslike.uproject` to match
your installed version). Right-click the `.uproject` file and "Generate Visual Studio project
files", then open and build.
