# Weapons: manifest

Both models are CC0 (credits in `CREDITS.md`). Measurements below come from parsing each file's
vertex data. "File axes" means the axes inside the file, before UE's import conversion.

## Sword: `Sword/KnightSword.obj` (+ `KnightSword_BaseColor.png`)

- **Model**: Knight's Sword, a realistic medieval arming sword with a straight crossguard and a
  disc pommel. See `preview.jpg`.
- **Format**: Wavefront OBJ (Blender 2.78 export). It has 388 vertices, UVs, and one object, `Cube.003`.
  - There is **no `mtllib`/`usemtl`**. Assign `KnightSword_BaseColor.png` (1024², sRGB) by hand.
  - It has no normal or metal maps. Metallic ≈ 1 on the blade and roughness ≈ 0.35 work fine.
- **Axes and units**: Blender's default OBJ export, Y-up with -Z forward, in meters (Blender units).
  The blade runs along file **+Z**.
- **Bounds**:

  | Axis | Range | Notes |
  |---|---|---|
  | X | ±0.037 | Blade/guard thickness |
  | Y | ±0.152 | Crossguard width |
  | Z | -0.876 … +0.877 | Total length **1.75 units** |

- **Layout along Z**:
  - pommel ≈ -0.88 … -0.82
  - grip ≈ -0.82 … -0.68 (centre ≈ **-0.75**)
  - crossguard ≈ -0.68 … -0.60
  - blade -0.60 → tip at +0.877
- **The origin is mid-blade, not at the grip.** To hold it, offset by about +0.75 along the blade
  axis so the grip centre lands on the hand.
- **Scale**: as authored it's 1.75 m, which is very long. A one-handed arming sword is about
  0.9–1.0 m, so scale by **≈0.55** (UE imports 1 unit as 100 cm; apply 0.55 at import or on the
  component).

## Shield: `Shield/KiteShield.fbx` (+ two texture sets)

- **Model**: Kite Shield 1, a realistic heater/kite shield with a metal rim and studs. See
  `preview.jpg`.
- **Format**: binary FBX 7.4, one model/material named "Kite Shield 1", 258 vertices.
  - Lcl/Pre/Geometric transforms are all identity.
  - No textures are referenced inside the FBX, so hook them up by hand.
- **Axes and units**: UpAxis = +Y, FrontAxis = +Z, UnitScaleFactor = 1.0 (centimetres).
- **Bounds**:

  | Axis | Range | Notes |
  |---|---|---|
  | X | ±17.7 | Width **35.4 cm** |
  | Y | -30.1 … +28.9 | Height **59 cm**, point at -Y |
  | Z | -0.6 … +6.4 | Front face bulges toward **+Z**; the back is near Z≈0 |

- **The origin is at the centre of the back face.** That's the spot to attach to `hand_l` or
  `lowerarm_l`. For a full-size knight's shield (about 70–80 cm) scale by **≈1.25**.
- **Textures** (1024², two interchangeable sets, same UVs):
  - `Textures_Wood/`: brown planks with a steel cross and steel rim.
  - `Textures_Steel/`: engraved ornate steel face.
  - Files in each set:
    - `KiteShield_BaseColor.png`: albedo, sRGB.
    - `KiteShield_NRM.png`: normal map, OpenGL/Unity convention. If the lighting looks inverted,
      flip green.
    - `KiteShield_AO.png`: ambient occlusion, greyscale.
    - `KiteShield_MET.png`: Unity-style metallic-smoothness. **Metallic in RGB, smoothness in A.**
      UE roughness = `1 - A`. Import it linear (not sRGB).
