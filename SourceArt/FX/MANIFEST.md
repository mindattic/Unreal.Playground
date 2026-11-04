# FX source art manifest

Imported by `tools/build_torch_fire.py` (editor Python) into `/Game/Souls/FX/Fire`. Credits and licence:
`CREDITS.md`.

| File | Becomes | Used by |
|---|---|---|
| `KenneyParticlePack/T_FlameAtlas.png` | `T_FlameAtlas` (greyscale mask, 2x2 sub-UV) | `M_TorchFlame` → `NS_TorchFlame`, `NS_TorchFlameGround` |
| `KenneyParticlePack/smoke_04.png` | `T_Smoke` (greyscale mask) | `M_TorchSmoke` → `NS_TorchSmoke` |
| `KenneyParticlePack/flame_01..04.png` | — (sources of the atlas, kept for re-baking) | |

The embers (`M_TorchEmber` → `NS_TorchEmbers`) are a procedural round dot, no texture.

The Niagara systems are lightweight (stateless) emitters duplicated from the engine's
`/Niagara/DefaultAssets/Templates/Systems/FountainLightweight` and tuned through
`unreal.SoulsFxLibrary` (Source/UnrealSoulslike/Souls/SoulsFxLibrary.*), since stateless emitter
modules aren't reachable from Python directly.
