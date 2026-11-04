# Environment manifest

All files are binary FBX, **Y-up, authored in meters**, imported by `tools/build_content.py` as static
meshes (scene/unit conversion on, complex-as-simple collision) and listed by logical name in
`Content/Environment/EnvironmentAssets.json`. Sizes below were measured from each pack's OBJ/glTF
twin (X × Y(up) × Z, in meters). The game measures bounds at runtime anyway.

## KayKit_Dungeon (Dungeon Remastered) — 4 m modular grid, texture atlas `dungeon_texture.png`

| File | Logical name(s) | Size (m) | Notes |
|---|---|---|---|
| wall.fbx | `wall` | 4 × 4 × 1 | stone-brick wall, repeat along X; origin bottom-centre |
| wall_broken.fbx | `wall_broken` | 4 × 4 × 1 | ruined wall, same footprint as `wall` |
| wall_cracked.fbx | `wall_cracked` | 4 × 4 × 1.26 | cracked variant |
| wall_corner.fbx | `wall_corner` | 2.5 × 4 × 2.5 | corner post |
| wall_half.fbx | `wall_half` | 2 × 4 × 1 | half-length filler (origin at its left end, X 0..2) |
| wall_arched.fbx | `wall_arched` | 4 × 4 × 1 | wall with an arch |
| wall_doorway.fbx | `gate` | 4 × 4 × 1 | wall with a walk-through doorway |
| stairs.fbx | `stairs` | 5 × 5.1 × 4 | rises along Z (0..4) |
| pillar.fbx | `pillar`, `bridge_pillar` | 1.5 × 4 × 1.5 | square stone pillar |
| column.fbx | `column` | 0.7 × 1.4 × 0.7 | short column / bollard |
| floor_foundation_front_and_back.fbx | `bridge` | 2 × 2 × 2.2 | solid stone deck block with faced front/back; repeat along X to span a gap |
| barrier.fbx | `bridge_railing` | 4 × 1.1 × 0.5 | low parapet, repeat along X on both deck edges |
| floor_tile_large.fbx | `floor` | 4 × 0.15 × 4 | flagstone floor tile |

## KayKit_MedievalHexagon (Medieval Hexagon Pack) — miniature scale (hex tile ≈ 2 units), atlas `hexagons_medieval.png`

These are tabletop-scale; scale them up (≈ ×5–8) to real size.

| File | Logical name | Size (units) | Notes |
|---|---|---|---|
| building_home_A_green.fbx | `house_a` | 0.79 × 0.93 × 0.85 | small stone cottage, green roof |
| building_home_B_green.fbx | `house_b` | 0.88 × 1.28 × 1.10 | larger cottage |
| building_tower_A_green.fbx | `tower` | 0.99 × 2.19 × 1.15 | round stone tower |
| building_destroyed.fbx | `ruin` | 1.54 × 0.99 × 1.31 | collapsed building |
| building_bridge_A.fbx | `bridge_arched` | 1.33 × 1.25 × 1.92 | single arched stone bridge spanning Z; base dips 1 unit below origin |

Not in either pack: a modular bridge-deck piece designed as such (the foundation block above stands
in), and Hebridean blackhouses specifically (the cottages are generic stone).

## Island art (Poly Haven, ambientCG, generated) - used by `tools/build_island.py`

Imported under `/Game/Island` (textures, models, materials, the landscape grass type) and assembled
into `/Game/Souls/Maps/Lvl_Isle`. `PolyHaven/polyhaven_manifest.json` lists every file the fetcher
wrote. Model sizes are as imported into UE (cm, X x Y x Z); glTF pivots keep the authoring scene
offsets, which the build compensates for from each mesh's bounds.

### Terrain textures (2k JPG; `*_diff` sRGB, `*_nor_gl` OpenGL normals imported with green flipped, `*_arm` = AO/Rough/Metal, `*_disp` height)

| Set | Maps | Use in `M_IslandTerrain` |
|---|---|---|
| `AmbientCG/Grass004` | Color, NormalGL, Roughness, AO | main turf, two tiling scales |
| `AmbientCG/Grass003` | Color | olive variation; moss on prop tops (`M_IslandProp`) |
| `PolyHaven/Textures/sparse_grass` | diff | heather-brown patches |
| `PolyHaven/Textures/aerial_beach_01` | diff, nor_gl, arm, disp | wind-rippled beach sand (aerial scan of 30 m; tiled at 11 m) |
| `PolyHaven/Textures/coast_sand_01` | diff, nor_gl, arm, disp | pebbly sand mixed into the beach |
| `PolyHaven/Textures/damp_beach_sand` | diff | wet sand at the waterline |
| `PolyHaven/Textures/seaside_rock` | diff, nor_gl, arm, disp | steep slopes / cliff faces (bi-planar on steep ground) |
| `PolyHaven/Textures/rocky_trail` | diff, nor_gl, arm, disp | the route, ruin pads, worn arena |

### Models (glTF 2k, one static mesh per glTF node, Nanite on, complex-as-simple collision)

| Model | Meshes (size cm) | Use |
|---|---|---|
| `rock_moss_set_01` / `_02` | 13 separate mossy rocks, 100-340 wide | boulder clusters, shore rocks, skerries |
| `boulder_01` | 127 x 183 x 100 | boulders, outcrops |
| `rock_07` | 17 x 32 x 14 (a small slab) | stood on end and scaled ~x8/x20/x4 for the standing stones |
| `rock_face_02` | 271 x 213 x 247 | cliff cladding (x2-9), sea stacks, outcrops |
| `mountainside` | 1021 x 525 x 1052 | cliff cladding (x1.9-2.7), sea stacks |
| `modular_fort_01` | 20 kit pieces: walls (thin/thick/walkway), corners, gate, stairs, round tower (1584 x 1584 x 1350) | the ruins along the route, scaled x0.62 |
| `dead_tree_trunk` | 305 x 28 x 29 | driftwood on the start beach |

### Generated (`tools/generate_island_art.py`, deterministic)

| File | What |
|---|---|
| `T_WaterNormal.png` | 1024^2 tileable wave normal map (OpenGL green), panned at three scales by `M_Ocean` |
| `T_IslandNoise.png` | 1024^2 tileable fbm noise, three scales in R/G/B (macro variation, foam, moss) |
| `SM_GrassClump_A/B/C.obj` | blade-geometry grass clumps (154-280 tris, Z-up cm; UE's OBJ import keeps Z up and mirrors Y) for the landscape grass type `LGT_IslandTurf` |
### Biomes (UNR-T17)

| Model | Use |
|---|---|
| `island_tree_02` | wind-bent leafy trees in the woodland (HISM, Nanite, leaves masked by the fetched `leaves_alpha` map, light wind sway in `M_IslandFoliage`) |
| `shrub_02`, `fern_02` | bushes and ferns under the trees (alpha maps fetched separately; the glTFs reference JPGs without alpha) |
| `dandelion_01` | wildflowers across the grassland |
| `Generated/SM_Marram.obj`, `SM_Reeds.obj`, `SM_Heather.obj` | landscape grass types for the dunes, the marsh and the highland |
### Wildlife (UNR-T36/T37)

`Critters/Source/` holds the downloaded originals (see `CREDITS.md`). `tools/convert_critters.py` (run with
Blender 4.4+: `blender -b --factory-startup --disable-autoexec --python tools/convert_critters.py [-- --preview]`)
poses each rig at rest, bakes the skinned mesh and writes a static OBJ facing **+X**, Z up, in centimetres with its
bottom-centre at the origin. `tools/build_island.py` imports them to `/Game/Island/Models/Critters/<Name>` (no
collision, no Nanite), gives each `.mtl` slot an `M_Critter` instance, and places `ASoulsWildlife` actors.

| File | Size (cm, X x Y x Z) | Tris | Use |
|---|---|---|---|
| `Critters/SM_Crab.obj` (+ `crab.png` palette texture) | 12 x 17.3 x 6.2 | 832 | beach crabs: scuttle sideways, flee the player |
| `Critters/SM_Frog.obj` | 13 x 14.2 x 6 | 4920 | frogs on the marsh pool banks; leap into the water when approached (recoloured to a common frog) |
| `Critters/SM_Fish.obj` | 32 x 4.6 x 10.7 | 544 | fish swimming in the marsh pools; dead fish on their side along the tideline |
### Ship graveyard (UNR-T63)

`Ships/Source/` holds the Kenney Pirate Kit originals used (see `CREDITS.md`). `tools/convert_ships.py` (run with
Blender 4.4+: `blender -b --factory-startup --disable-autoexec --python tools/convert_ships.py [-- --preview] [Name ...]`)
joins each model's parts, turns the bow to **+X**, Z up, scales it to life size in centimetres (beam narrowed to
0.82 so the stubby kit hulls read as ships), wrecks it - flags struck, sails subdivided and rotted to tatters,
optionally broken in two at a ragged cut a little aft of midships - and writes a static OBJ with its bottom-centre
at the origin and slots `Wood` (the kit palette `colormap.png`), `Cloth` (sails) and `Weed` (the grass on the kit's
own wreck). `tools/build_island.py` (`build_ship_graveyard`) imports them to `/Game/Island/Models/Ships/<Name>`
(no Nanite; hulls collide complex-as-simple so they can be climbed, debris has no collision), gives every slot an
instance of `M_Shipwreck` (palette desaturated and tinted to dark old wood, streaked, soaked below ~3.5 m above
the sea, barnacles and weed at the waterline, slime under it, moss on the decks; two-sided for the open broken
hulls) and lays out the graveyard in the bay off the start beach.

| File | Size (cm, X x Y x Z) | Tris | Use |
|---|---|---|---|
| `SM_Wreck_Large.obj` | 4200 x 1262 x 3194 | 10181 | three-masted galleon: on its side in the shallows, sunk with masts out, on a reef far out |
| `SM_Wreck_Medium.obj` | 3500 x 1300 x 3290 | 10115 | sunk with masts out; listing on a reef |
| `SM_Wreck_Small.obj` | 2700 x 1252 x 3057 | 5284 | on its side in the shallows; on a reef |
| `SM_Wreck_Ghost.obj` | 3800 x 1411 x 3572 | 10068 | the kit's holed ghost ship: listing half-sunk; on a reef |
| `SM_Wreck_Hulk.obj` | 3800 x 1411 x 3572 | 2832 | the kit's grassed-over wreck: driven onto the sand; sunk |
| `SM_Wreck_Large_Bow.obj` / `_Stern.obj` | 2541 x 1157 x 3130 / 2226 x 1262 x 3194 | 4306 / 10283 | one galleon broken in two, the halves lying apart |
| `SM_Wreck_Medium_Bow.obj` / `_Stern.obj` | 2118 x 1300 x 3224 / 1855 x 1300 x 3104 | 5359 / 8602 | another broken ship: bow on its side, stern rearing up |
| `SM_Wreck_Mast.obj` | 1406 x 687 x 2529 | 3429 | lone masts standing out of the water over sunk hulls |
| `SM_Rowboat.obj` | 390 x 195 x 88 | 118 | the boat the player came ashore in (the kit's oars dropped) |
| `SM_Oar.obj` | 250 x 70 x 22 | 60 | one oar lying beside it (the kit's paddle, laid flat) |
| `SM_Barrel.obj`, `SM_Crate.obj`, `SM_CrateBottles.obj`, `SM_Planks.obj` | 96 x 96 x 88, 102 x 124 x 73, 102 x 124 x 100, 199 x 284 x 45 | 48-572 | wreckage along the tideline and flotsam round the wrecks |

### Sea shells (UNR-T61)

`Shells/Source/` holds the originals (see `CREDITS.md`). `tools/convert_shells.py` (run with Blender 4.4+:
`blender -b --factory-startup --disable-autoexec --python tools/convert_shells.py [-- --preview]`) keeps only the
shell (meat cut away; the seashell pair split into a scallop and a whelk; the cockle keeps one valve), lays each the
way it rests on sand, scales it to real size with its bottom-centre at the origin, and writes a static OBJ with slots
`<Kind>_Outer` / `<Kind>_Inner` (textured slots name a `shell_*.png`). `tools/build_island.py` (`build_shells`)
imports them to `/Game/Island/Models/Shells/<Kind>` (no Nanite, no collision body), gives every slot an instance of
`M_Shell` (two-sided; tint x the shell texture's banding, per-instance brightness variety, darker and glossier in
the last half metre above the sea) and scatters ~600 instanced shells (culled at 30 m): thickest along the
wrack line 5-55 cm above the sea, thinner up the beach, and six small drifts on the start beach.

| File | Size (cm, X x Y x Z) | Tris | Look |
|---|---|---|---|
| `SM_Shell_Cockle.obj` | 4.0 x 3.5 x 0.9 | 224 | one valve, convex up; sandy, banded |
| `SM_Shell_CockleUp.obj` | 3.5 x 3.0 x 0.8 | 224 | the same valve hollow side up, pale inside |
| `SM_Shell_Clam.obj` | 4.3 x 5.0 x 2.5 | 396 | gaping empty clam, chalky white |
| `SM_Shell_Mussel.obj` | 4.0 x 6.5 x 3.4 | 90 | open blue mussel: blue-black outside, pearly blue inside |
| `SM_Shell_Scallop.obj` | 8.0 x 6.4 x 1.0 | 968 | ribbed fan valve, pinkish |
| `SM_Shell_Whelk.obj` | 6.0 x 2.7 x 2.6 | 448 | spire shell lying on its side |
