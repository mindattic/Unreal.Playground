---
codex: 1
project: Unreal.Soulslike
code: UNR
layer: stories
status: living
updated: 2026-11-03
---
<!-- GENERATED from MindAttic.Tickets/data/Tickets.db by `tickets db render`. Do not edit: this file is rewritten from the database; change stories and tickets with the tickets CLI. -->

# Unreal.Soulslike — User Stories
> ✅ done (shipped & tested) · 🟡 partial · ⬜ planned. Every ✅ cites the test.

## Priority backlog
_Nothing recorded yet._

## Tickets
> Permanent log of every change and fix — this repo's ticket system. Rows are append-only: never
> delete or reword one; only move its Status (open → closed / wont-do) and fill in Ref. Status is
> a word, not a glyph, so tickets don't count toward the story index. Keep this section last.

| ID | Date | Type | Status | Summary | Ref |
|---|---|---|---|---|---|
| UNR-T1 | 2026-11-03 | docs | closed | Start the permanent ticket log in the ticket database | — |
| UNR-T2 | 2026-11-03 | feature | closed | Scaffold co-op souls-like demo: third-person combat on isolated Hebrides island (dodge roll, block, parry, X/Y light/heavy attacks, RB heal) | b21f31c |
| UNR-T3 | 2026-11-03 | feature | open | Playable demo: terrain-shaped Hebrides island surrounded by water, free meshes, working battle system, skeleton enemies that attack the player | — |
| UNR-T4 | 2026-11-03 | feature | open | Jump + free climbing (BotW/Genshin/Palworld style): climb any steep surface with stamina drain, climb-jump, ledge mantle, so challenges can be approached from any angle | — |
| UNR-T5 | 2026-11-03 | feature | open | Stealth execution: skeletons have perception (vision cone, hearing, unaware/suspicious/alerted); attacking an unaware skeleton from behind executes it instantly | — |
| UNR-T6 | 2026-11-03 | feature | open | Shadow merge (Ereban: Shadow Legacy / ALBW wall-merge): become a black shadow splotch that crawls along walls, ground and ceilings while in shadow, costs shadow energy, ejects in light | — |
| UNR-T7 | 2026-11-03 | feature | open | Merge-able premade buildings and brick walls half-buried in sand; a skeleton-guarded bridge over the sea loch that can be crossed unseen merged into it as a shadow blob | — |
| UNR-T8 | 2026-11-03 | feature | open | Build the same game twice for an informed choice: C++ (Unreal.Soulslike.Cpp.exe) and C# via UnrealSharp (Unreal.Soulslike.CSharp.exe), identical gameplay, plus a -Benchmark run comparing frame time, memory, build/iteration time and code size | — |
| UNR-T9 | 2026-11-03 | feature | closed | Controls for traversal: A jump / B dodge / R3 merge (Space/Shift/E on KB+M), player + HUD wired to the climb and shadow-merge components, jump/fall animation | 34f370b |
| UNR-T10 | 2026-11-03 | fix | open | Static review fixes before first build: unity-build MergeableTag redefinition, Mesh shadowing, missing includes/RenderCore dep, boss max health not replicated, host climb stamina-out leaves movement broken, dodge launch client-only, PIE kit component recreate, respawn facing | — |
| UNR-T11 | 2026-11-03 | fix | closed | Knight shows untextured bind pose: FBX animations and textures never imported/saved; player must idle, run, jump, swing sword and dodge roll with proper animations (user priority 2026-11-03) | 7d19478 |
| UNR-T12 | 2026-11-03 | feature | open | Playable third-person game with Elden Ring-like combat: lock-on, telegraphed armed enemy knights, parry + riposte, backstab, poise/stagger, boss with health bar, Site of Grace checkpoint/respawn, real island environment (landscape, ocean, sky), packaged standalone exe | — |
