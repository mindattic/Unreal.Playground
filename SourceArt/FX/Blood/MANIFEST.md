# Blood FX source art manifest (UNR-T56)

Imported by `tools/build_blood_fx.py` (editor Python) into `/Game/Souls/FX/Blood`; drawn by
`ASoulsBloodPool` (`Source/UnrealSoulslike/Souls/SoulsBlood.*`) through `FSoulsBlood::Splash`.
Credits and licence: `CREDITS.md`. Re-bake the atlases with `python -I tools/bake_blood_atlases.py`.

| File | Becomes | Used by |
|---|---|---|
| `T_BloodSpurtAtlas.png` (2048x1024, 4x2 cells, 8 frames; base of the splash at each cell's bottom centre) | `T_BloodSpurtAtlas` | `M_BloodSpurt`: the arterial spurt |
| `T_BloodMistAtlas.png` (1024x1024, 4x4 cells, 16 frames; cell 0 empty) | `T_BloodMistAtlas` | `M_BloodMist`: the red mist |
| `T_BloodDecals.png` (2048x2048, 2x2 splatter patches) | `T_BloodDecals` | `M_BloodDecal`: splats on the ground and walls |
| `src/overcrafted/splattersplash_0..5.png` | — (source of the spurt atlas) | |
| `src/sinestesia/blood_splash_4x4.png` | — (source of the mist atlas) | |
| `src/exilegl/blood.png` | — (source of the decal atlas) | |

Only each sheet's alpha (shape) and red/green (thick/thin, highlights) are used; the materials colour
the blood themselves. The droplets (`M_BloodDrop`) are procedural, no texture.
