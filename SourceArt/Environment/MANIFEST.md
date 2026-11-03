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
