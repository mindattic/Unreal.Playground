# Environment art credits

Downloaded 2026-11-03 directly from the packs' official GitHub repositories (no login, no EULA gate).
Each pack's own `LICENSE.txt` is kept next to its files.

| Pack | Author | Source | License |
|---|---|---|---|
| KayKit : Dungeon Remastered 1.0 | Kay Lousberg (www.kaylousberg.com) | https://github.com/KayKit-Game-Assets/KayKit-Dungeon-Remastered-1.0 (also https://kaylousberg.itch.io/kaykit-dungeon-remastered) | CC0 1.0 — http://creativecommons.org/publicdomain/zero/1.0/ |
| KayKit : Medieval Hexagon Pack 1.0 | Kay Lousberg (www.kaylousberg.com) | https://github.com/KayKit-Game-Assets/KayKit-Medieval-Hexagon-Pack-1.0 (also https://kaylousberg.itch.io/kaykit-medieval-hexagon) | CC0 1.0 — http://creativecommons.org/publicdomain/zero/1.0/ |

Crediting Kay Lousberg is appreciated but not required.

## Island art (tools/build_island.py, UNR-T12)

Downloaded 2026-11-03 without a login by `tools/fetch_island_art.py` (it re-downloads and MD5-checks
everything, and keeps only the maps the build uses). All CC0 1.0, no attribution required; given anyway.

| Asset(s) | Author | Source | License |
|---|---|---|---|
| Textures `sparse_grass`, `coast_sand_01`, `aerial_beach_01`, `damp_beach_sand`, `seaside_rock`, `rocky_trail` (`PolyHaven/Textures/`) | Poly Haven artists (Rob Tuytel, Dimitrios Savva, Charlotte Baglioni and others; per-asset credit on each page) | https://polyhaven.com/textures (e.g. https://polyhaven.com/a/aerial_beach_01) | CC0 1.0 |
| Models `rock_moss_set_01`, `rock_moss_set_02`, `boulder_01`, `rock_07`, `rock_face_02`, `mountainside`, `modular_fort_01`, `dead_tree_trunk`, `island_tree_02`, `shrub_02`, `fern_02`, `dandelion_01` (`PolyHaven/Models/`) | Poly Haven artists (per-asset credit on each page) | https://polyhaven.com/models (e.g. https://polyhaven.com/a/modular_fort_01) | CC0 1.0 |
| Textures `Grass004`, `Grass003` (`AmbientCG/`) | Lennart Demes / ambientCG | https://ambientcg.com/view?id=Grass004, https://ambientcg.com/view?id=Grass003 | CC0 1.0 |
| `Generated/` (water normal map, noise texture, grass clump meshes) | made in-repo by `tools/generate_island_art.py` | this repository | same as the repo |

License text: https://creativecommons.org/publicdomain/zero/1.0/
## Wildlife (tools/convert_critters.py + tools/build_island.py, UNR-T36/T37)

Downloaded 2026-11-04 without a login. Originals are kept under `Critters/Source/`; `Critters/SM_*.obj`
are baked from them by `tools/convert_critters.py` (Blender 4.4+). All CC0 1.0, no attribution required; given anyway.

| Asset | Author | Source | License |
|---|---|---|---|
| `Critters/Source/crab/` (`crab.blend`, `crab.png`; low poly crab, rigged, idle + walk) -> `SM_Crab.obj` | methodical pixel | https://opengameart.org/content/crab-low-poly-animated-3d-model (`crab_0.zip`) | CC0 1.0 |
| `Critters/Source/frog.glb` ("Frog", animated) -> `SM_Frog.obj` | Quaternius | https://poly.pizza/m/9Z2V8fpazF | CC0 1.0 |
| `Critters/Source/fish.glb` ("Fish", animated) -> `SM_Fish.obj` (pond fish and the dead fish on the beach) | Quaternius | https://poly.pizza/m/XWl86YFtpF | CC0 1.0 |

## Ship graveyard (tools/convert_ships.py + tools/build_island.py, UNR-T63)

Downloaded 2026-11-04 without a login (the site's free "Continue without donating" download). The originals
used are kept under `Ships/Source/` with the pack's own `License.txt`; `Ships/SM_*.obj` are baked from them by
`tools/convert_ships.py` (Blender 4.4+). CC0 1.0, no attribution required; given anyway.

| Asset | Author | Source | License |
|---|---|---|---|
| Pirate Kit 2.1: `ship-large`, `ship-medium`, `ship-small`, `ship-ghost`, `ship-wreck`, `mast-ropes`, `boat-row-large`, `tool-paddle`, `barrel`, `crate`, `crate-bottles`, `platform-planks` (`.glb`) and `Textures/colormap.png` -> the wrecks, the rowboat, its oar and the washed-up wreckage | Kenney (www.kenney.nl) | https://kenney.nl/assets/pirate-kit (`kenney_pirate-kit.zip`) | CC0 1.0 |

## Sea shells (tools/convert_shells.py + tools/build_island.py, UNR-T61)

Downloaded 2026-11-04 without a login from poly.pizza's static model store. The originals are kept under
`Shells/Source/`; `Shells/SM_Shell_*.obj` (+ the `shell_*.png` textures, downscaled to 256 px) are baked from them by
`tools/convert_shells.py` (Blender 4.4+). Three of the sources are **CC-BY 3.0: attribution required** - keep these
lines in the game's credits.

| Asset | Author | Source | License |
|---|---|---|---|
| `Shells/Source/mussel-open.glb` ("Mussel Open") -> `SM_Shell_Mussel.obj` | Kenney (www.kenney.nl) | https://poly.pizza/m/eWHAc3Pq8z | CC0 1.0 |
| `Shells/Source/seashell.glb` ("Seashell": a scallop valve and a spire shell) -> `SM_Shell_Scallop.obj`, `SM_Shell_Whelk.obj` | Poly by Google | https://poly.pizza/m/5ovn4mnRejL | CC-BY 3.0 - https://creativecommons.org/licenses/by/3.0/ |
| `Shells/Source/clam_65ObbG0O7Ze.glb` ("Clam", one valve kept) -> `SM_Shell_Cockle.obj`, `SM_Shell_CockleUp.obj` | Poly by Google | https://poly.pizza/m/65ObbG0O7Ze | CC-BY 3.0 - https://creativecommons.org/licenses/by/3.0/ |
| `Shells/Source/clam_7UsYZIZ_rhU.glb` ("Clam", meat removed) -> `SM_Shell_Clam.obj` | Poly by Google | https://poly.pizza/m/7UsYZIZ_rhU | CC-BY 3.0 - https://creativecommons.org/licenses/by/3.0/ |

## Walking mech (tools/convert_mech.py + tools/build_island.py, UNR-T68)

Downloaded 2026-11-04 without a login. The original used (`George.blend`, rigged and animated, with its texture and
the pack's own `License.txt`) is kept under `Mech/Source/`; `Mech/SM_Mech_*.obj` (13 rigid parts, gun arms posed,
autocannon barrels and missile pods added), `Mech/Mech_Texture.png` and `Mech/mech_rig.json` are baked from it by
`tools/convert_mech.py` (Blender 4.4+). CC0 1.0, no attribution required; given anyway.

| Asset | Author | Source | License |
|---|---|---|---|
| Animated Mech Pack (March 2021): `George.blend` + `George_Texture.png` -> the walking mech | Quaternius (quaternius.com) | https://opengameart.org/content/animated-mech-pack (`animated_mech_pack_-_march_2021.zip`) | CC0 1.0 - https://creativecommons.org/publicdomain/zero/1.0/ |

## Star fighter and drones (tools/convert_fighter.py + tools/build_island.py, UNR-T69)

Downloaded 2026-11-04 without a login. The originals used (`Striker.blend` + `Striker_Blue.png`, `Bob.blend` +
`Bob_Orange.png`, unmodified) are kept under `Fighter/Source/` with a `License.txt` recording the licence shown on the
download page (the zip has none of its own). `Fighter/SM_Fighter_*.obj` (hull with an added cockpit tub and seat, the
cockpit glass as a hinged canopy, three added landing legs, and the drone), `Fighter/Fighter_Texture.png`,
`Fighter/Drone_Texture.png` and `Fighter/fighter_rig.json` are baked from them by `tools/convert_fighter.py`
(Blender 4.4+). CC0 1.0, no attribution required; given anyway.

| Asset | Author | Source | License |
|---|---|---|---|
| Ultimate Spaceships Pack (May 2021): `Striker` -> the player's star fighter; `Bob` -> the AI drone fighters | Quaternius (quaternius.com) | https://opengameart.org/content/lowpoly-spaceships-pack (`ultimate_spaceships_-_may_2021.zip`) | CC0 1.0 - https://creativecommons.org/publicdomain/zero/1.0/ |
