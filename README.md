# Unreal.Soulslike

A vertical slice of an Elden-Ring-style action game in Unreal Engine 5.8. You wash up on an
isolated Hebridean island, fight through Hollow knights and take on two bosses: the Callanish
Warden in the stone circle and Morrigan of the Mire, a shade who melts into shadow.

## What's in it

- **The island.** A real Landscape about 400 m across in an open ocean, with five biomes along
  the route:
  - machair dunes with marram grass
  - leafy, wind-bent woodland with ferns and wildflowers
  - a ruined keep
  - a misty peat marsh with pools
  - heather highland rising to a Callanish-style stone circle on the clifftop

  There are cliffs, sea stacks and an offshore islet with a broch, a Site of Grace and loot. The
  sky has a low sun, volumetric fog and Lumen lighting. All the art is CC0 (Poly Haven, ambientCG,
  Quaternius); credits are under `SourceArt/`.
- **Combat.** The player and every enemy use the same kit, built on Epic's Manny with retargeted
  CC0 animation:
  - a three-swing combo and a heavy attack; each swing steps in toward the foe ahead
  - a roll with i-frames, and a sprint
  - no stamina: every move is always available
  - a timed parry, then a riposte, and backstabs
  - poise interrupts and stance breaks
  - hitstop and camera kick on hits
  - flasks
  - lock-on
- **Grapple.** Hook a glinting point on a ledge to be hauled up onto it. Hook a smaller enemy to
  yank it to your blade, dazed. Hook a big one to pull yourself in for a falling cut.
- **A bestiary.** Besides the Hollow knights, the island has six kinds of foe, each a rigged CC0 model
  with habits to match. They are posted by biome:
  - Hollow villagers swarm you in the woods: frail and quick, they flee when hurt and call the
    rest of the pack.
  - A hooded huntress also hunts the woods. She rolls clear of your swings.
  - Skeletons hold the ruined keep and the standing-stone avenue. They shamble, never flinch and
    never fear.
  - The peat-drowned lurch out of the marsh and lunge to grab and bite. Roll or step back to slip
    the grab.
  - Fen imps leap at you from range in the marsh.
  - Club-wielding brutes walk the heather moor. They are slow, crush you with heavy blows and are
    hard to stagger.

  The player is a hooded ranger rather than the bare mannequin. Sources and licences are in
  `SourceArt/Characters/`.
- **Brace and impale.** Leapers crouch and spring at you from range. Brace your blade while one is
  in the air and it's run through, held aloft and slides off dead. If you don't brace, you're
  knocked flat.
- **Surprise is rewarded.** An enemy you hit or startle while it's unaware freezes in shock: it
  takes extra damage and is open to a critical. Ambush kills pay half again in runes and spread
  fear. Shaken enemies hesitate and miss their swings; terrified ones flee or cower.
- **Stealth.** Crouching or crawling makes you far harder to see and all but silent, so you can
  creep up behind an enemy that hasn't noticed you and backstab it. Walking (not jogging) works
  too if you stay out of its sight. Unaware enemies patrol around their posts. When two meet, they
  stop to talk, nodding or shaking their heads, then go their separate ways.
- **Swimming and exploration.** You can swim the sea and the marsh pools as long as you like.
  Golden Seeds (+1 flask), a Crimson Tear (stronger flasks) and
  rune purses wait on grapple ledges, in the marsh and on the islet.
- **A walking mech.** A two-legged war machine stands parked in the dunes off the path. Walk up
  and press D-pad up / F to climb into its cockpit. The left stick / WASD drives the legs
  (throttle and turn, with weight and inertia); the right stick / mouse twists the torso up to
  100 degrees off the legs and aims the gun arms (push up to aim up). RT / LMB fires the twin
  autocannons, LT / RMB a missile salvo; both build heat, and an overheat locks them out. X / F
  walks the legs round to face where the torso points (the torso keeps its aim), A / X is a full
  stop, R3 / V swaps to the cockpit view, and D-pad up / E climbs out. Every step is procedural and
  shakes the ground. Enemies come for it and hack at its legs; at zero armour it's wrecked, you're
  thrown clear, and it reboots after a while.
- **Fire.** Torches and grass fires burn with flames rendered from a real fire simulation (Blender
  Mantaflow, baked to flipbooks by `tools/bake_fire_flipbooks.py`), with embers, smoke, heat haze and
  flickering light; no fire meshes. A live 3D gas simulation (Unreal's free Niagara Fluids) for the torches
  nearest the camera is built in but off: on UE 5.8 it corrupts memory within seconds, even Epic's stock
  fire (`souls.Fire.Fluid 1` turns it on at your own risk; `-TorchDemo -FluidFire` shoots it). A field
  fire races downwind through grass and heather, jumps ahead on blown embers, climbs into bushes and
  trees, throws up thick smoke columns and leaves charred ground behind.
- **Weather.** A prevailing wind veers and gusts, and the clouds drift with it. Every few minutes a rain
  front rolls in: the clouds thicken and darken, the light dims and rain sweeps round you, splashing on
  the ground and rippling the water. The ground, rocks and leaves turn dark and glossy, then dry off once
  it clears. Gusts tear leaves off the trees and tumble them downwind, and the grass and foliage sway with
  the wind. The rain's hiss and the wind's roar are synthesised in code.
- **Elden Ring trappings.**
  - Sites of Grace: rest to refill, which also brings the enemies back.
  - Runes, which you lose when you die.
  - Boss bars, YOU DIED, GREAT ENEMY FELLED.
  - CC0 sound and boss music.

## Controls

| Action | Gamepad | Mouse / keyboard |
|---|---|---|
| Move / look | L-stick / R-stick | WASD / mouse |
| Attack (also ripostes and backstabs; swings a torch in hand) | X | LMB |
| Raise the shield (hold); raise it just as a blow lands to parry; brace against a leaper | LB | RMB |
| Dodge roll | B | E |
| Jump | A | Space |
| Run (toggle) | L3 | Shift |
| Crouch (toggle) | R3 | Ctrl / C |
| Flask | RB | R |
| Rest at grace / pick up / take a torch; elsewhere, whistle | D-pad up | F |

The keyboard mirrors the controller exactly. Y, LT and RT do nothing.

## Build and run

Requires Unreal Engine 5.8 (`tools/find-engine.ps1` finds an installed engine) and Visual Studio
with the C++ game workload.

```powershell
./tools/build.ps1      # compile, then build the knight, the shadow boss and the island in the editor
./tools/run.ps1        # play Lvl_Isle
./tools/package.ps1 -Only Cpp   # standalone Builds\Cpp\Windows\Unreal.Soulslike.Cpp.exe
```

Content is generated by the scripts in `tools/` from `SourceArt/`: `Content/` is not
committed.

Run these with `-RenderOffscreen` to check a build without anyone at the controls:
- `-KnightDemo` performs every move and mechanic against real enemies and screenshots each one.
- `-IslandTour` screenshots each Site of Grace and boss arena.
- `-ShadowBossTest -ShadowBossDemo` stages the shade's fight.
- `-StealthDemo` dives prone, crawls, gets up, then crouch-walks up behind a sentry and backstabs it.
- `-PatrolDemo` watches two patrolling enemies meet, talk and part.
- `-InputTest` presses real controller buttons through the player controller (crouch, crouch-walk,
  sprint on R3 and L3, roll, prone, crawl, get up, a light attack's reach) and logs `Input check:` PASS/FAIL.
- `-HandShots` takes close shots of the sword fist and the shield arm, at rest and on guard.
- `-BloodDemo` splashes a light, a heavy and a critical hit in front of the player under a fixed
  camera, shoots each mid-spurt (`BloodDemo_1_Light` … `BloodDemo_3_Critical_Drops`) and the blood
  on the ground after (`BloodDemo_4_Decals_Side`, `BloodDemo_5_Decals_Top`), logs `BloodDemo:` lines
  ending in `BLOOD_DEMO_OK`, then quits.
- `-MechDemo` boards the parked mech (or one it drops beside you) and drives it through the real
  input mappings: walks (both feet must step), turns, twists the torso to its stop, X and F bring the
  legs round to the torso, the right stick and mouse pitch the aim, RT fires the autocannons and LT a
  missile salvo at an enemy, A and the X key full-stop it with the throttle held, an enemy hacks at it,
  then E climbs out and A jumps the knight again. It shoots each beat (`Mech_1_parked` …
  `Mech_15_disembarked`), logs `MechDemo:` PASS/FAIL lines ending in `MECH_DEMO_DONE`, then quits.
- `-BestiaryShots` shows each bestiary type one at a time in front of a fixed camera. It shoots each
  one idle, winding up, striking and in a heavy swing (`Bestiary_<Type>_1_Idle` … `_4_Heavy`), logs
  each type's numbers (`BestiaryShots:`) ending in `BESTIARY_SHOTS_OK`, then quits.
- `-RangerRunShots` runs the player body straight ahead at a sprint under a camera that tracks
  alongside. At each stride extreme it shoots from the side (`RangerRun_Side_1..4`), then close on the
  feet (`RangerRun_Feet_1..4`). It logs `RangerRun:` lines with the stride and each foot's distance
  from its shin, ending in `RANGER_RUN_OK` (or `RANGER_RUN_FAIL`), then quits.
- `-TorchDemo` picks up, swings, drops and throws a torch, sees a torch-bearer fall and takes a wall torch,
  shooting each beat (`TorchDemo_*`), then shoots a fire close up (with the gas simulation too under
  `-FluidFire`), by day and at night (`TorchDemo_13_fire_fluid` … `TorchDemo_16_night_wielded`).
  Logs `Torch check:` PASS/FAIL. Add `-TimeOfDay=21` to shoot the whole demo at night.
- `-FireDemo` sets a grassy patch alight and shoots the spread from afar and up close at the front, by day
  and then at night (`FireDemo_NN_XXs[_close][_night]`), logging `FireDemo:` counts and wind, ending in
  `FIRE_DEMO_DONE`, then quits.
- `-WeatherShots` (into `Saved/WeatherShots`) shoots a clear sky, the clouds twice 8 s apart (they drift
  with the wind), a rain front arriving, rain, the wet ground, and leaves blowing off the nearest tree,
  logging `Weather shot` lines ending in `WEATHER_SHOTS_DONE`, then quits. `-WeatherRain` /
  `-WeatherClear` hold the weather; `-NoWeather` turns it off.

Screenshots land in `Saved/Screenshots`.
