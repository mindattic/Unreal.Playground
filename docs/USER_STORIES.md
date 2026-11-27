---
codex: 1
project: Unreal.Playground
code: UNR
layer: stories
status: living
updated: 2026-11-03
---
<!-- GENERATED from MindAttic.Tickets/data/Tickets.db by `tickets db render`. Do not edit: this file is rewritten from the database; change stories and tickets with the tickets CLI. -->

# Unreal.Playground — User Stories
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
| UNR-T3 | 2026-11-03 | feature | closed | Playable demo: terrain-shaped Hebrides island surrounded by water, free meshes, working battle system, skeleton enemies that attack the player | already done: 2255c58, 91f38f1, 7e9d719 |
| UNR-T4 | 2026-11-03 | feature | closed | Jump + free climbing (BotW/Genshin/Palworld style): climb any steep surface with stamina drain, climb-jump, ledge mantle, so challenges can be approached from any angle | already done: 8322259 |
| UNR-T5 | 2026-11-03 | feature | closed | Stealth execution: skeletons have perception (vision cone, hearing, unaware/suspicious/alerted); attacking an unaware skeleton from behind executes it instantly | already done: ff085d2, f3cd2d1 |
| UNR-T6 | 2026-11-03 | feature | closed | Shadow merge (Ereban: Shadow Legacy / ALBW wall-merge): become a black shadow splotch that crawls along walls, ground and ceilings while in shadow, costs shadow energy, ejects in light | already done: 7272448, 0b7fad6 |
| UNR-T7 | 2026-11-03 | feature | closed | Merge-able premade buildings and brick walls half-buried in sand; a skeleton-guarded bridge over the sea loch that can be crossed unseen merged into it as a shadow blob | already done: 395e730 |
| UNR-T8 | 2026-11-03 | feature | wont-do | Build the same game twice for an informed choice: C++ (Unreal.Soulslike.Cpp.exe) and C# via UnrealSharp (Unreal.Soulslike.CSharp.exe), identical gameplay, plus a -Benchmark run comparing frame time, memory, build/iteration time and code size | portfolio reset: C# twin out of scope (C++ build done in 13e3fa8) |
| UNR-T9 | 2026-11-03 | feature | closed | Controls for traversal: A jump / B dodge / R3 merge (Space/Shift/E on KB+M), player + HUD wired to the climb and shadow-merge components, jump/fall animation | 34f370b |
| UNR-T10 | 2026-11-03 | fix | closed | Static review fixes before first build: unity-build MergeableTag redefinition, Mesh shadowing, missing includes/RenderCore dep, boss max health not replicated, host climb stamina-out leaves movement broken, dodge launch client-only, PIE kit component recreate, respawn facing | already done: 8b7c318 |
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
| UNR-T58 | 2026-11-04 | feature | closed | Every run of the game records video via MindAttic.Capture, windowed 1920x1080 | already done: f964cd4, bbb8b50, fb10105 |
| UNR-T59 | 2026-11-04 | fix | closed | Remove stamina completely: every move always available | — |
| UNR-T60 | 2026-11-04 | fix | closed | Deepest night is dark purple with visibility, never pitch black | — |
| UNR-T61 | 2026-11-04 | feature | closed | Beach dressing: pebbles, shells, more dead fish along the shore | — |
| UNR-T62 | 2026-11-04 | feature | closed | More boulders, ruins and bushes across the island | — |
| UNR-T63 | 2026-11-04 | feature | closed | Ship graveyard round the landing harbour; one rowboat dragged up the beach where you came ashore | — |
| UNR-T64 | 2026-11-04 | feature | closed | High-quality free rigged characters instead of the Unreal mannequin | — |
| UNR-T65 | 2026-11-04 | fix | closed | Hit lands mid-swing as the blade crosses, not after the swing ends | — |
| UNR-T66 | 2026-11-04 | fix | closed | After dying, roll is lost and attacks move to RT: the knight's input mapping is gone on respawn | — |
| UNR-T67 | 2026-11-04 | feature | closed | Every free high-quality rigged non-player model becomes an enemy type with its own AI | — |
| UNR-T68 | 2026-11-04 | feature | closed | Operable mech you can climb into: legs and an independently rotating torso | already done: 41bd8b9 |
| UNR-T69 | 2026-11-04 | feature | closed | Star fighter parked on the land with full dogfighting flight | — |
| UNR-T70 | 2026-11-04 | feature | closed | Grappling gun: zip to a ledge and launch off it (Arkham-style grapple boost) | — |
| UNR-T71 | 2026-11-04 | feature | closed | Shadow melt: become a shadowy blob that crawls over land and up building walls | — |
| UNR-T72 | 2026-11-04 | fix | closed | Ranger: feet detached from the body, run animation barely moves | — |
| UNR-T73 | 2026-11-04 | fix | closed | Mech: one leg dead, inverted pitch; X/F turns legs to the torso, LT/RT fire, A or X key stops | — |
| UNR-T74 | 2026-11-04 | fix | closed | Day starts at dawn; a full day takes 5 minutes | — |
| UNR-T75 | 2026-11-04 | fix | closed | Fire looks bad: torches and field fire need convincing flames | — |
| UNR-T76 | 2026-11-04 | fix | closed | Glide only after a grapple launch | — |
| UNR-T77 | 2026-11-04 | feature | closed | Wall running (Titanfall-style) plus long practice walls | — |
| UNR-T78 | 2026-11-04 | fix | closed | Night never pitch black: at most a dark, perfectly playable purple | — |
| UNR-T79 | 2026-11-04 | feature | closed | Weather: rain clouds that drift across the sky and bring rain; wind that blows leaves from the trees | — |
| UNR-T80 | 2026-11-04 | fix | closed | Dawn was the darkest moment of the day and night was a bright lilac: hold the night lift until the sun is 25 deg up and halve the night boost | — |
| UNR-T81 | 2026-11-04 | fix | closed | Star fighter: up/down pitch controls inverted the wrong way; flip them | — |
| UNR-T82 | 2026-11-04 | fix | closed | Capture stops after ~1:40: lossless 1080p60 encoder falls 60 s behind; record at hq quality | — |
| UNR-T83 | 2026-11-04 | fix | closed | Play captures film the desktop region, not the game window, and the 2 GB RAM valve stops them | — |
| UNR-T84 | 2026-11-05 | feature | open | Add Silver Soldier (Sketchfab CC-BY) as a new bestiary enemy type | — |
| UNR-T85 | 2026-11-05 | feature | closed | Character roster: cycle between melee knight and gun-wielding Ranger with , and . | — |
| UNR-T86 | 2026-11-05 | feature | wont-do | Mario-style movement moveset for the new bunny-mascot playable character | portfolio reset: bunny mascot outside the souls slice |
| UNR-T87 | 2026-11-06 | feature | closed | Adopt the shared off-by-default --headless launch mode (Unreal.Shared) | — |
| UNR-T88 | 2026-11-07 | chore | closed | Rename Unreal.Soulslike to Unreal.Playground: it is a testing ground for gameplay mechanics | — |
| UNR-T89 | 2026-11-10 | chore | closed | Sync .agent/ fleet-wide commands and regenerate docs/USER_STORIES.md to catch up with closed tickets | — |
| UNR-T90 | 2026-11-10 | chore | closed | Adopt the shared docs-site pipeline: turn on GitHub Pages | — |
| UNR-T91 | 2026-11-17 | docs | closed | Update USER_STORIES.md | — |
| UNR-T92 | 2026-11-18 | chore | closed | Add SilverSoldier | — |
| UNR-T93 | 2026-11-22 | feature | closed | Build terrain-shaped Hebrides island surrounded by water with free meshes. | already done: 2255c58, 7e9d719 (Lvl_Isle island) |
| UNR-T94 | 2026-11-22 | feature | closed | Add skeleton perception: vision cone, hearing, unaware/suspicious/alerted states. | already done: ff085d2, f3cd2d1 (perception/stealth) |
| UNR-T95 | 2026-11-22 | feature | closed | Attacking an unaware skeleton from behind executes it instantly. | already done: ff085d2, f3cd2d1 (executions/backstab) |
| UNR-T96 | 2026-11-22 | feature | closed | Premade buildings and brick walls half-buried in sand that are merge-able. | already done: 395e730 (buried merge-able ruins) |
| UNR-T97 | 2026-11-22 | feature | closed | Build the game as C++ target Unreal.Soulslike.Cpp.exe. | already done: tools/package.ps1, 13e3fa8 (C++ package) |
| UNR-T98 | 2026-11-22 | feature | closed | Mech actor with legs driven by left stick, interact to climb in and out. | already done: 41bd8b9 (mech) |
| UNR-T99 | 2026-11-22 | feature | open | Blender bone rename to UE5 Manny convention and anim strip via convert pipeline. | — |
| UNR-T100 | 2026-11-22 | feature | closed | Become shadow splotch crawling along walls, ground and ceilings while in shadow. | already done: 7272448, 0b7fad6 (shadow merge/melt) |
| UNR-T101 | 2026-11-22 | feature | closed | Shadow energy cost and ejection in light, plus UI/feedback. | already done: 7272448 (ShadowMergeComponent energy) |
| UNR-T102 | 2026-11-22 | feature | closed | Implement working player battle system. | already done: 91f38f1 (Elden Ring combat) |
| UNR-T103 | 2026-11-22 | feature | closed | Skeleton enemies that attack the player, placed on the island. | already done: 2255c58, 91f38f1 (enemy AI) |
| UNR-T104 | 2026-11-22 | feature | closed | Skeleton-guarded bridge over sea loch crossable unseen by merging as shadow blob. | already done: 395e730, fc758c8 (guarded loch bridge) |
| UNR-T105 | 2026-11-22 | feature | wont-do | Build identical gameplay in C# via UnrealSharp as Unreal.Soulslike.CSharp.exe. | portfolio reset: C# twin out of scope |
| UNR-T106 | 2026-11-22 | feature | wont-do | Add -Benchmark run comparing frame time, memory, build/iteration time and code size. | portfolio reset: C# vs C++ comparison out of scope |
| UNR-T107 | 2026-11-22 | feature | closed | Independently rotating torso aimed by player. | already done: 41bd8b9 (mech torso) |
| UNR-T108 | 2026-11-22 | feature | closed | Weapons, cockpit/third-person camera, footstep weight and shake. | already done: 41bd8b9, fa9ff09 (mech weapons/camera) |
| UNR-T109 | 2026-11-22 | feature | open | IK Retargeter and BP_SoulsEnemy_SilverSoldier build. | — |
| UNR-T110 | 2026-11-22 | feature | open | New subclass with tuned stats, Bestiary_SilverSoldier tag wiring, CC-BY credit in CREDITS.md. | — |
| UNR-T111 | 2026-11-22 | feature | wont-do | Add bunny mascot character to roster with Quaternius mesh/rig and locomotion animations. | portfolio reset: bunny mascot outside the souls slice |
| UNR-T112 | 2026-11-22 | feature | wont-do | Long jump, backflip, triple jump and dive jump abilities. | portfolio reset: bunny moveset outside the souls slice |
| UNR-T113 | 2026-11-22 | feature | wont-do | Vertical tree/wall climbing extending SoulsWallRun.cpp and belly slide. | portfolio reset: bunny moveset outside the souls slice |
| UNR-T114 | 2026-11-22 | docs | closed | 2026-11-22 morning | — |
| UNR-T115 | 2026-11-24 | fix | open | Hunt the whole repo for real bugs with no time limit and fix every confirmed one | — |
| UNR-T116 | 2026-11-24 | chore | closed | Unreal.Playground scripts name MindAttic.Media.Capture | children done |
| UNR-T117 | 2026-11-24 | chore | closed | Unreal.Playground run.bat (~5) and tools/run.ps1 (~8) comments say MindAttic.Media.Capture (R:\MindAttic\Capture output path unchanged) | 77c242e |
| UNR-T118 | 2026-11-24 | feature | closed | Unreal.Playground records in-game through the MindAttic.Media.Capture.Unreal plugin, and its demo screenshots go through capture into preview sets | children done |
| UNR-T119 | 2026-11-24 | feature | closed | Unreal.Playground's run.ps1/run.bat runs record through the plugin inside the game (-capture video hq) instead of an external capture record watcher | children done |
| UNR-T120 | 2026-11-24 | feature | closed | Every Unreal.Playground demo/screenshot mode also takes its shots through capture (sidecars, markers in the recording) and can publish them as a preview set | children done |
| UNR-T121 | 2026-11-24 | feature | closed | Unreal.Playground enables the MindAttic.Media.Capture.Unreal plugin (uproject plugin dir, Build.cs, settings, Playground.capture.json) and raises player-death/boss-defeated events; UBT editor build passes | a917aacc9cd304afcdf5bcfe0bbe604ce1b37012 |
| UNR-T122 | 2026-11-24 | chore | closed | Unreal.Playground tools/run.ps1 passes -capture video hq to player 1 instead of running an external capture record --hwnd watcher; -NoCapture kept | fb10105b2a3009dc3473b0c32ff97156c751d858 |
| UNR-T123 | 2026-11-24 | feature | closed | Unreal.Playground PlaygroundShots::Take replaces ~20 FScreenshotRequest call sites; shots also go through capture with sidecar and marker while it runs | 1b9cc27 |
| UNR-T124 | 2026-11-24 | feature | closed | Unreal.Playground -preview opens a preview set of a demo mode's shots at quit; README Capture how-to | 266da3f |
| UNR-T125 | 2026-11-24 | fix | closed | tools/run.ps1 -Players >1: $map?listen expands as the variable $map? (empty), so player 1 launches with no map; use ${map}?listen | 952514b |
| UNR-T126 | 2026-11-24 | docs | closed | tools/run.ps1, run.bat and README name R:\MindAttic\Media\Capture\Unreal.Playground | 6e59f37ba0c7e6961c8815818e7a372cdfdadff9 |
| UNR-T127 | 2026-11-26 | feature | open | Unreal.Playground: adopt (or confirm) the shared run menu over deploy-lib.ps1 Publish-Unreal, run Redeploy, and confirm the DebugGame (Debug) and Shipping (Release) packages land and start at R:\MindAttic\Apps\Unreal.Playground\Debug and \Release; repoint anything in the repo still naming an old package path. Outcome: both packages exist under R: and launch | — |
| UNR-T128 | 2026-11-27 | chore | open | Portfolio reset (MAT-T324): release charter (docs/RELEASE.md, one milestone), bible, and triage of every open ticket into epic>feature>task for: Unreal.Playground, Unreal.Shared | — |
| UNR-T129 | 2026-11-27 | feature | open | Title screen, pause menu and an ending, so the slice plays start to finish | — |
| UNR-T130 | 2026-11-27 | feature | open | Finish the grapple animation and land the Silver Soldier bestiary enemy | — |
| UNR-T131 | 2026-11-27 | feature | open | No known bugs, and DebugGame/Shipping packages on R: that start and hold frame rate | — |
| UNR-T132 | 2026-11-27 | feature | open | Retire the pre-pivot KayKit-era code and make docs/DESIGN.md describe the game as it is | — |
| UNR-T133 | 2026-11-27 | feature | open | A title screen to start or quit, and a pause menu in play | — |
| UNR-T134 | 2026-11-27 | feature | open | Felling both bosses ends the slice with a closing screen and credits | — |
| UNR-T135 | 2026-11-27 | feature | open | A full start-to-ending playthrough of the Release package, with its issues fixed | — |
| UNR-T136 | 2026-11-27 | feature | open | Throw, reel, haul and land beats on the base grapple | — |
| UNR-T137 | 2026-11-27 | feature | open | The owner-supplied Silver Soldier as a seventh bestiary type | — |
| UNR-T138 | 2026-11-27 | feature | open | Every confirmed bug in the repo found and fixed | — |
| UNR-T139 | 2026-11-27 | feature | open | DebugGame and Shipping packages on R:\MindAttic\Apps\Unreal.Playground that start and run smoothly | — |
| UNR-T140 | 2026-11-27 | feature | open | Remove code from the abandoned KayKit skerry design that the shipping game no longer uses | — |
| UNR-T141 | 2026-11-27 | feature | open | DESIGN.md and README describe the slice as it plays | — |
| UNR-T142 | 2026-11-27 | feature | open | A title screen at boot (New Game / Quit) over a slow island flyover; New Game drops the player on the beach. Skipped under --headless and every -XxxDemo/-XxxShots flag so automation still boots straight into Lvl_Isle | — |
| UNR-T143 | 2026-11-27 | feature | open | Start / Esc opens a pause menu: Resume, Rest at last Site of Grace, Quit to title, Quit game; pad and keyboard navigable, game paused while open | — |
| UNR-T144 | 2026-11-27 | feature | open | When both the Callanish Warden and Morrigan of the Mire are felled, play an ending: a closing banner, a credits roll built from SourceArt CREDITS files, then return to the title screen | — |
| UNR-T145 | 2026-11-27 | chore | open | Play the Shipping package with -capture from title to ending (both bosses), and file a fix task under this feature for every crash, soft-lock, broken prompt or visual bug found | — |
| UNR-T146 | 2026-11-27 | chore | open | Run the Shipping package with -Benchmark along the route, write the frame-time and memory numbers to docs/PERFORMANCE.md, and file a fix task under this feature for any hitch over 50 ms | — |
| UNR-T147 | 2026-11-27 | chore | open | Remove the KayKit-era Soulslike*/HebridesIsland/SkeletonEnemy classes, their components and the unused Platforming/SideScrolling template variants where nothing in Lvl_Isle, Souls* or the tools references them (SoulsKnight still uses some: keep those or move them); check Config GlobalDefaultGameMode; build clean | — |
| UNR-T148 | 2026-11-27 | docs | open | Rewrite docs/DESIGN.md for the current slice: Lvl_Isle biomes, the Manny knight kit, bestiary, both bosses, Sites of Grace and runes, controls as bound; drop the KayKit skerry, co-op and C# twin sections | — |
