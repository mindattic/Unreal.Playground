# FX source art manifest

Imported by `tools/build_torch_fire.py` (editor Python) into `/Game/Souls/FX/Fire`. Credits and licence:
`CREDITS.md`.

| File | Becomes | Used by |
|---|---|---|
| `Fire/T_FireFlipbook.png` | `T_FireFlipbook` (sRGB colour, linear alpha, BC7; 8x8 sub-UV) | `M_FireFlipbook` → `NS_TorchFlame`, `NS_TorchFlameGround`, `NS_FieldFire` |
| `Fire/T_SmokeFlipbook.png` | `T_SmokeFlipbook` (sRGB colour, linear alpha; 8x8 sub-UV) | `M_FireSmokeLit` → `NS_TorchSmoke`, `NS_FieldSmoke`; noise for `M_FireHaze` → `NS_FireHaze` |
| `KenneyParticlePack/*` | — (UNR-T51, no longer imported) | |

Both sheets are baked by `tools/bake_fire_flipbooks.py` (Blender 4.4, headless; see its header) and
committed, so a fresh clone needs no Blender. Re-bake:

```
blender -b --factory-startup -P tools/bake_fire_flipbooks.py -- --sim fire
blender -b --factory-startup -P tools/bake_fire_flipbooks.py -- --sim smoke
blender -b --factory-startup -P tools/bake_fire_flipbooks.py -- --stage pack
```

(one Blender process per simulation: a second Mantaflow bake in the same process sometimes fails.)

The embers (`M_TorchEmber` → `NS_TorchEmbers`, `NS_FieldEmbers`) are a procedural streak, no texture.

The sprite systems are lightweight (stateless) emitters duplicated from the engine's
`/Niagara/DefaultAssets/Templates/Systems/FountainLightweight` and tuned through
`unreal.SoulsFxLibrary` (Source/UnrealSoulslike/Souls/SoulsFxLibrary.*), since stateless emitter
modules aren't reachable from Python directly. `NS_FluidFire` is a copy of
`/NiagaraFluids/Templates/Gas/3D/Systems/Grid3D_Gas_Fire` (the NiagaraFluids plugin, enabled in the
.uproject), sized and fed at run time by `Source/UnrealSoulslike/Souls/SoulsTorch.cpp`.
