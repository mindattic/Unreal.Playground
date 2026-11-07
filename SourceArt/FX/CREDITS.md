# FX art credits

## Fire flipbooks (tools/bake_fire_flipbooks.py, UNR-T75)

Made in this repository; no downloaded or paid art. A Mantaflow gas simulation (fire + smoke, wavelet
noise up-res) in Blender 4.4, rendered with Cycles volumes and packed into looping 8x8 sub-UV sheets by
`tools/bake_fire_flipbooks.py`.

| Asset(s) | Author | Source | License |
|---|---|---|---|
| `Fire/T_FireFlipbook.png` (4096x4096, 64 frames of 512: R flame, G hottest gas, B soot, A coverage) | Unreal.Playground (generated) | `tools/bake_fire_flipbooks.py --sim fire` | CC0 1.0 (http://creativecommons.org/publicdomain/zero/1.0/) |
| `Fire/T_SmokeFlipbook.png` (2048x2048, 64 frames of 256: lit grey smoke, A opacity) | Unreal.Playground (generated) | `tools/bake_fire_flipbooks.py --sim smoke` | CC0 1.0 |

The hero gas fire (`NS_FluidFire`) is a copy of Epic's `Grid3D_Gas_Fire` template from the NiagaraFluids
plugin that ships with Unreal Engine 5.8 (Engine/Plugins/FX/NiagaraFluids). It is engine content, used
under the Unreal Engine EULA like the rest of the engine; nothing of it is redistributed in this repository.

## Torch fire (tools/build_torch_fire.py, UNR-T51; retired by UNR-T75)

The UNR-T51 cartoon sprites are no longer used (UNR-T75 replaced them with the simulated flipbooks
above); the files are kept for reference.

Downloaded 2026-11-04 without a login from the OpenGameArt mirror of Kenney's pack:
https://opengameart.org/sites/default/files/kenney_particlePack.zip (9 760 286 bytes,
MD5 `1a265f128c39783e586bbb441d17bbb9`; the same pack is at https://kenney.nl/assets/particle-pack).
The pack's own `License.txt` is kept next to the files.

| Asset(s) | Author | Source | License |
|---|---|---|---|
| `KenneyParticlePack/flame_01..04.png`, `smoke_04.png` (512x512 grey on black, from the pack's `PNG/`) | Kenney Vleugels (www.kenney.nl) | https://opengameart.org/content/particle-pack-80-sprites | CC0 1.0 — http://creativecommons.org/publicdomain/zero/1.0/ |
| `KenneyParticlePack/T_FlameAtlas.png` (1024x1024, 2x2 sub-UV sheet: flame_01, flame_02 / flame_03, flame_04, greyscale) | made in-repo from the four flame sprites above | this repository | CC0 1.0 (derived) |

Crediting Kenney is appreciated but not required.
