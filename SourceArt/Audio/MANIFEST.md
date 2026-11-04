# Audio: manifest

Every source is CC0 (credits in `CREDITS.md`). All files are **16-bit PCM `.wav`**, which Unreal
imports natively.

- **SFX** are **mono, 48 kHz**, so they can be spatialised as 3D sounds.
- **The ambience** is stereo, 48 kHz. **The music** is stereo, 44.1 kHz.
- Leading silence is trimmed. Each file is peak-normalised to **-1 dBFS** with TPDF dither.
- Several SFX are **layered composites**. They're built from the CC0 sources listed per row, with
  filters and pitch shifts noted. They were rendered in ffmpeg + numpy on 2026-11-03.
- Loudness differs by design: footsteps and UI sit quieter than hits. Set final levels with Sound
  Classes or Sound Cue volume, not by re-normalising.

Short source names used below (full URLs in the table at the end):

- **K-Impact**: Kenney Impact Sounds
- **K-RPG**: Kenney RPG Audio
- **MS-Tex**: medicinestorm, Medieval SFX: Weapon Textures (192 kHz field recordings)
- **MS-Imp**: medicinestorm, Medieval SFX: Weapon Impacts (192 kHz field recordings)
- **RD-RPG**: rubberduck, 80 CC0 RPG SFX
- **AD-RPG**: artisticdude, RPG Sound Pack

## Combat

| Role | File | Dur (s) | Built from | Licence |
|---|---|---|---|---|
| Sword swing (whoosh) | `sword_swing_01.wav` | 0.40 | MS-Tex `Katana Swing.wav`, take at 7.93 s. High-passed at 160 Hz to remove mic wind-thump, then light FFT denoise. | CC0 |
| | `sword_swing_02.wav` | 0.40 | MS-Tex `Katana Swing.wav`, take at 5.63 s. Same processing. | CC0 |
| | `sword_swing_03.wav` | 0.65 | MS-Tex `Seax Swing.wav`, take at 3.23 s. Same processing. | CC0 |
| Blade hits body/armour | `sword_hit_flesh_01.wav` | 0.62 | K-Impact `impactPunch_heavy_000`, plus RD-RPG `blade_01` (-3 dB), plus an MS-Imp `Mace Norse Sword Blade` armour tick (-16 dB) | CC0 |
| | `sword_hit_flesh_02.wav` | 0.58 | K-Impact `impactPunch_heavy_002`, plus K-RPG `knifeSlice2` (-4 dB), plus K-Impact `impactSoft_heavy_002` (-6 dB) | CC0 |
| | `sword_hit_flesh_03.wav` | 0.62 | K-Impact `impactPunch_heavy_004`, plus RD-RPG `blade_03` (-3 dB), plus K-Impact `impactPlate_medium_001` (-12 dB) | CC0 |
| Shield block (clang) | `shield_block_01.wav` | 0.60 | MS-Imp `Mace Norse Sword Blade` take at 5.2 s, plus K-Impact `impactWood_heavy_002` (-4 dB, wooden shield body), plus `impactPlate_heavy_001` (-7 dB) | CC0 |
| | `shield_block_02.wav` | 0.60 | MS-Imp `Mace Norse Sword Blade` take at 3.66 s, plus `impactWood_heavy_000`, plus `impactPlate_heavy_003` | CC0 |
| Parry (bright ring) | `parry_01.wav` | 0.75 | MS-Imp `Sabre Norse Sword Blade on Blade`: take at 12.46 s, with the take at 13.98 s (-5 dB) stacked on it for a longer ring | CC0 |
| Riposte (heavy stab) | `riposte_01.wav` | 0.68 | K-Impact `impactPunch_heavy_001` pitched down 0.75x, plus K-RPG `knifeSlice2` (-5 dB), plus `impactSoft_heavy_003` (-3 dB), plus a faint MS-Imp metal tick | CC0 |

## Movement and player state

| Role | File | Dur (s) | Built from | Licence |
|---|---|---|---|---|
| Footstep, grass | `footstep_grass_01.wav` … `_04.wav` | 0.16–0.18 | K-Impact `footstep_grass_000` … `_003`, high-passed at 45 Hz | CC0 |
| Roll | `roll_01.wav` | 0.75 | K-RPG `cloth1`, plus AD-RPG `chainmail1` (-14 dB), plus K-Impact `impactSoft_heavy_000` (-2 dB, at +0.16 s), plus K-RPG `cloth3` (-6 dB, at +0.30 s) | CC0 |
| Player hurt | `armor_hurt_01.wav` | 0.62 | Male pain grunt (TheBardOfBlasphemy, take at 6.9 s), plus K-Impact `impactPunch_medium_000` (-4 dB), plus AD-RPG `chainmail1` (-15 dB) | CC0 |
| | `armor_hurt_02.wav` | 0.67 | Same grunt file (take at 29.8 s), plus `impactPunch_medium_002`, plus `chainmail1` | CC0 |
| Death (body fall) | `death_01.wav` | 0.93 | K-Impact `impactSoft_heavy_003`, plus AD-RPG `chainmail2` (-10 dB), plus `impactPlate_heavy_002` (-7 dB, +0.17 s), plus `impactSoft_heavy_001` (-9 dB, +0.36 s settle). No vocal. | CC0 |
| Flask drink | `flask_drink_01.wav` | 2.05 | qubodup `bottle-open-05` (cork/glass, -6 dB), then owlishmedia `Drink_05` (gulps, lightly compressed) at +0.35 s | CC0 |
| Rest at grace | `grace_rest_01.wav` | 2.83 | spring-spring `health_restore.wav` (harp-like heal shimmer), high-passed at 140 Hz, with a short two-tap echo for air | CC0 |

## World, enemies, UI

| Role | File | Dur (s) | Built from | Licence |
|---|---|---|---|---|
| Enemy felled (low boom) | `enemy_felled_01.wav` | 1.79 | K-Impact `impactPlate_heavy_004` at 0.55x pitch, plus `impactSoft_heavy_002` at 0.6x, plus `impactPunch_heavy_003` at 0.65x, plus a low-passed (300 Hz) tail from trazzz123's deep monster roar | CC0 |
| Boss roar | `boss_roar_01.wav` | 2.58 | darsycho troll roar (take at 7.0 s) at 0.85x pitch, over trazzz123 `monster_roar.wav` (-5 dB, first 2.6 s) for sub weight | CC0 |
| UI confirm | `ui_confirm_01.wav` | 0.90 | pwl `bell_ding3.wav`, shortened to 0.9 s with a 0.35 s fade (a soft low bell, chosen over Kenney's brighter, more toy-like interface blips) | CC0 |
| Ambience: wind + surf loop | `ambient_wind_sea_loop.wav` | 40.0 (loop) | **Stereo.** bashar3a wind bed (high-passed, with a circular crossfade at the seam), plus ignasd `Wind`/`Wind2` howling gusts (-20 dB, panned), plus 14 overlapped real beach-wave hits (jasinski and transitking recordings via qubodup), placed and wrapped modulo 40 s. **Seamless loop**: the seam jump is 0.009 against a typical sample step of 0.09. | CC0 |
| Boss music loop | `music_boss_loop.wav` | 123.4 (loop) | **Stereo, 44.1 kHz.** Juhani Junkala (SubspaceAudio), *Epic Boss Battle [Seamlessly Looping]*: orchestral percussion and brass, "inspired by God of War". Only change: peak lowered 1 dB. Sample length is untouched, so it still loops. | CC0 |

**Unreal import tips**

- For the two loops, tick **Looping** on the Sound Wave.
- The ambience is stereo, so play it 2D (or as a non-spatialised ambient sound).
- Put the 3D SFX on an attenuation preset.
- Group variations (`_01` … `_04`) with a **Random** node in a Sound Cue (or MetaSounds), and add
  ±5% pitch randomisation for more variety.

## Not found / caveats

- **No CC0 sea-cliff surf loop of good quality was available on OpenGameArt**, so the ambience is
  built from single-wave recordings. Each wave is real, but the wash has a regular rhythm of about
  2.4–3.8 s. For a richer bed, replace it with a longer field recording later.
- **The boss music is energetic "epic boss battle" music** (big drums and brass), not slow dark
  ambient. It was chosen because it is orchestral and explicitly seamless. Two CC0 alternatives
  were downloaded but not used:
  - nene, *Fall of Nightmares [orchestral]*: choir, 88 s, not marked as a loop.
    https://opengameart.org/content/fall-of-nightmares-epic-orchestral
  - qubodup/yd, *Dark Shrine Loop*: dark synth ambient, 57 s loop.
    https://opengameart.org/content/dark-shrine-loop
- **The swing whooshes are real recordings**, but they were captured with heavy sub-bass wind on the
  mic. That's been filtered out, which leaves a mid-range "whoom" with a tonal blade sweep. If
  they feel light in-game, add an `impactSoft` layer at the hit frame rather than boosting the
  swing.
- These files were checked by spectrogram and level analysis, not by ear. Give them a listen pass
  before shipping.
