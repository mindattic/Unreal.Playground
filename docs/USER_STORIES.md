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
| UNR-T12 | 2026-11-03 | feature | closed | Playable third-person game with Elden Ring-like combat: lock-on, telegraphed armed enemy knights, parry + riposte, backstab, poise/stagger, boss with health bar, Site of Grace checkpoint/respawn, real island environment (landscape, ocean, sky), packaged standalone exe | 7d79c01 |
| UNR-T13 | 2026-11-03 | feature | closed | Grapple: hook predetermined grapple points to pull up to ledges; reel small enemies in to you; reel yourself in to large enemies (boss) into an aerial strike | e4e3c6d |
| UNR-T14 | 2026-11-03 | feature | closed | Brace & impale: leaping enemies pounce from range; bracing your weapon in the window impales them mid-air, they roll off dead | a2732d8 |
| UNR-T15 | 2026-11-03 | feature | closed | Surprise & fear: enemies ambushed while unaware are shocked (frozen, take bonus damage, open to crits); surprise kills spread fear and confusion through nearby enemies (cower, flee, falter) so surprise is rewarded | 3b6adda |
| UNR-T16 | 2026-11-03 | feature | closed | Shadow boss: a boss that melts into a creeping shadow pool, slides across the ground and erupts behind or beneath the player | 111de3b |
| UNR-T17 | 2026-11-03 | feature | closed | Swimming + biomes: swim with animations and stamina (drown only when exhausted), greenery/foliage, distinct biomes (beach, grassland/woods, rocky highland, marsh) and exploration routes across the island | a054ff8 |
| UNR-T18 | 2026-11-03 | feature | closed | Integrate grapple, brace/impale leapers, ambush shock + fear and swimming into the knight, enemy AI, game mode, HUD and content pipeline, with -KnightDemo coverage | 3b6adda |
| UNR-T19 | 2026-11-03 | feature | closed | Exploration rewards: glinting pickups placed off the beaten path (Golden Seed +1 flask, Crimson Tear stronger flasks, rune caches) with pick-up prompt and toast | 9f812ad |
| UNR-T20 | 2026-11-03 | docs | closed | README describes the vertical slice: island biomes, combat, grapple, brace/impale, ambush fear, swimming, exploration, bosses, controls, build/run/verify | be3d0ff |
| UNR-T21 | 2026-11-04 | fix | closed | Leaving a boss arena kept that boss's fight (and bar) active elsewhere; abandoning the fight should end it and reset the boss | 7d79c01 |
| UNR-T22 | 2026-11-04 | fix | closed | Packaged exe dies at start: the dotted project name Unreal.Soulslike.uproject makes the cooker write shader archives under Unreal.Soulslike/ while staging packs Unreal/, so no material shaders ship; rename to UnrealSoulslike.uproject | 13e103a |
| UNR-T23 | 2026-11-04 | fix | closed | Sword and shield attached to the wrong hands | — |
| UNR-T24 | 2026-11-04 | feature | closed | LB guard doubles as parry when timed: pressing guard right before a hit parries | — |
| UNR-T25 | 2026-11-04 | fix | closed | Gamepad: A jumps (unbound today), B rolls on press, tap L3 starts sprinting | — |
| UNR-T26 | 2026-11-04 | feature | closed | Enemies patrol; when two patrollers meet they stop, chat, then part ways | — |
| UNR-T27 | 2026-11-04 | feature | closed | Stealth stance: tap B standing still to crouch, hold B to dive prone and army-crawl, B again to stand; crouch/prone cut enemy sight and hearing so you can sneak up and backstab | — |
| UNR-T28 | 2026-11-04 | fix | closed | Crouch (tap B) does nothing in real play | — |
| UNR-T29 | 2026-11-04 | fix | closed | Sprint (tap L3/Ctrl) does nothing in real play | — |
| UNR-T30 | 2026-11-04 | fix | closed | Light attack has no reach | — |
| UNR-T31 | 2026-11-04 | fix | closed | Sword hand not gripping, shield upside down | — |
| UNR-T32 | 2026-11-04 | fix | closed | One-sided rock meshes invisible from behind | — |
| UNR-T33 | 2026-11-04 | fix | closed | Standard Elden Ring pad layout: L3 run, R3 lock-on, B roll | — |
| UNR-T34 | 2026-11-04 | feature | closed | Combat clash: enemy swings meet the player's guard | — |
| UNR-T35 | 2026-11-04 | feature | closed | Grapple: fill in animations and timing | — |
| UNR-T36 | 2026-11-04 | feature | closed | Beach life: scuttling crabs and dead fish washed up on the shore | — |
| UNR-T37 | 2026-11-04 | feature | closed | Pond life: frogs that jump into the water, fish swimming in the ponds | — |
| UNR-T38 | 2026-11-04 | feature | closed | Boulders strewn across the land; high cliffs built from properly aligned boulders | — |
| UNR-T39 | 2026-11-04 | feature | closed | Day/night cycle; dimmer setting sun in the west | — |
| UNR-T40 | 2026-11-04 | feature | closed | Torches: carried lit by enemies, wall torches, pick up, wield, throw | — |
| UNR-T41 | 2026-11-04 | feature | closed | Field fire: open flame ignites grass, spreads, burns models away | — |
| UNR-T42 | 2026-11-04 | feature | closed | Grapple + umbrella traversal: slingshot up, catch the wind, glide | — |
| UNR-T43 | 2026-11-04 | feature | closed | Tall grass to hide in | — |
| UNR-T44 | 2026-11-04 | feature | closed | D-pad up whistles: nearby enemies come over curiously | — |
| UNR-T45 | 2026-11-04 | feature | open | Grapple: fill in throw/reel/haul/land animation beats and timing (reopens UNR-T35) | — |
| UNR-T46 | 2026-11-04 | fix | closed | Rein in the pad: X = the one normal attack, LB shield, B roll, A jump, L3 run, R3 crouch; Y/LT/RT unbound | — |
| UNR-T47 | 2026-11-04 | fix | closed | Keyboard mirrors the pad: LMB attack, RMB shield/parry, E roll, Space jump | — |
| UNR-T48 | 2026-11-04 | fix | closed | Remove the umbrella (and grapple slingshot) completely | — |
| UNR-T49 | 2026-11-04 | fix | closed | Dodge roll keeps the player's facing unless the stick is pressed | — |
| UNR-T50 | 2026-11-04 | feature | closed | Ludicrous gore: absurd blood sprays on every hit | — |
| UNR-T51 | 2026-11-04 | feature | closed | Natural torch flames from a free fire particle pack | — |
| UNR-T52 | 2026-11-04 | feature | closed | Dodge roll cancels an attack less than 50% committed | — |
| UNR-T53 | 2026-11-04 | fix | closed | Day/night cycle 10x faster (2-minute day) | — |
| UNR-T54 | 2026-11-04 | feature | closed | X attack faster, and a second X chains the cross-slash back | — |
| UNR-T55 | 2026-11-04 | fix | closed | World collision uses the meshes' polygons, not boxes or spheres | — |
| UNR-T56 | 2026-11-04 | feature | closed | Real blood and guts splash VFX from a free pack | — |
| UNR-T57 | 2026-11-04 | feature | closed | Sword hits come from the blade actually intersecting the enemy | — |
| UNR-T58 | 2026-11-04 | feature | open | Every run of the game records video via MindAttic.Capture, windowed 1920x1080 | — |
| UNR-T59 | 2026-11-04 | fix | closed | Remove stamina completely: every move always available | — |
| UNR-T60 | 2026-11-04 | fix | closed | Deepest night is dark purple with visibility, never pitch black | — |
| UNR-T61 | 2026-11-04 | feature | open | Beach dressing: pebbles, shells, more dead fish along the shore | — |
| UNR-T62 | 2026-11-04 | feature | closed | More boulders, ruins and bushes across the island | — |
| UNR-T63 | 2026-11-04 | feature | open | Ship graveyard round the landing harbour; one rowboat dragged up the beach where you came ashore | — |
| UNR-T64 | 2026-11-04 | feature | open | High-quality free rigged characters instead of the Unreal mannequin | — |
| UNR-T65 | 2026-11-04 | fix | closed | Hit lands mid-swing as the blade crosses, not after the swing ends | — |
| UNR-T66 | 2026-11-04 | fix | closed | After dying, roll is lost and attacks move to RT: the knight's input mapping is gone on respawn | — |
| UNR-T67 | 2026-11-04 | feature | open | Every free high-quality rigged non-player model becomes an enemy type with its own AI | — |
| UNR-T68 | 2026-11-04 | feature | open | Operable mech you can climb into: legs and an independently rotating torso | — |
| UNR-T69 | 2026-11-04 | feature | open | Star fighter parked on the land with full dogfighting flight | — |
| UNR-T70 | 2026-11-04 | feature | open | Grappling gun: zip to a ledge and launch off it (Arkham-style grapple boost) | — |
| UNR-T71 | 2026-11-04 | feature | open | Shadow melt: become a shadowy blob that crawls over land and up building walls | — |
