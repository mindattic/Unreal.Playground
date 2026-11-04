# Characters manifest

`Knight/` and `Skeletons/` are the KayKit pack (see `SourceArt/MANIFEST.md`). `Bestiary/` holds the
bestiary and the Ranger player body (UNR-T64/T67).

## Bestiary: how it's made

```
python -I tools/fetch_bestiary.py      # raw CC0 sources -> Saved/BestiarySource (not committed)
blender -b --factory-startup --disable-autoexec --python tools/convert_bestiary.py [-- --preview [Type ...]]
```

`tools/convert_bestiary.py` writes the committed GLBs below. `tools/build_bestiary.py` (editor Python,
run by `tools/build.ps1` after the knight) imports them, retargets the knight's whole Manny kit onto
each (every montage, plus the template's locomotion anim blueprint and a death clip), and builds
`/Game/Souls/Bestiary/BP_SoulsEnemy_<Type>`.

Every GLB has the same layout:
- **One skinned mesh** on one armature, with no animation.
- **Facing:** it faces the same way as the UAL mannequin.
- **Position and size:** feet are on the origin, and it stands at the height given below. Bigger or
  smaller types get that from the actor scale in C++, not from the mesh.
- **Bone names:** **UE5 mannequin bone names** (`root, pelvis, spine_01.., neck_01, head, clavicle_l,
  upperarm_l, lowerarm_l, hand_l, index_01_l.., thigh_l, calf_l, foot_l`, plus the `_r` side).
  `build_bestiary.py` copies IK_Manny's retarget chains by name, and the C++ sword and shield sockets
  (`hand_r` / `hand_l`) exist on every rig.
- **Weapons:** a `<Type>_Weapon.glb` is a static mesh with the grip at the origin, the haft along +Z
  and the edge along +X. The script fits it into the fist by hand anatomy (knuckle line, finger
  direction), carrying over the knight sword's tuned Manny fit.

| Path | What | Rig / notes |
|---|---|---|
| `Bestiary/Ranger/Ranger.glb` | **Player body**: hooded leather ranger. Male Ranger outfit, Universal Base Character head, beard. 1.84 m. | UAL/UBC humanoid rig (65 bones, already mannequin-named, so the UAL clips fit natively). 2K PBR textures (JPEG). |
| `Bestiary/Huntress/Huntress.glb` | Enemy: hooded huntress. Female Ranger outfit, female head. 1.76 m. | Same rig. Uses the knight sword (no shield). |
| `Bestiary/Hollow/Hollow.glb` | Enemy: hollow villager. Male Peasant outfit, head, long hair. 1.78 m. | Same rig. Borrows the skeleton's cleaver at 0.5x. |
| `Bestiary/Skeleton/Skeleton.glb` + `Skeleton_Weapon.glb` | Enemy: skeleton with skull, pauldrons and cleaver. 1.80 m. | Quaternius "CharacterArmature", repaired (below). |
| `Bestiary/Brute/Brute.glb` + `Brute_Weapon.glb` | Enemy: green orc with a club. 1.80 m, scaled 1.3x in game. | CharacterArmature, repaired. |
| `Bestiary/Zombie/Zombie.glb` | Enemy: drowned zombie. 1.80 m. | CharacterArmature plus tongue and eyelid bones, repaired. Fights with an invisible blade (claws). |
| `Bestiary/Imp/Imp.glb` | Enemy: blue imp with a cudgel in its mesh. 1.80 m, scaled 0.85x in game. | CharacterArmature, repaired. Invisible blade along the cudgel. |

**CharacterArmature repair** (older Quaternius rig):
- **Feet:** the IK feet hung off `Root`; they're now re-parented under the shins.
- **Hands:** the rig had no hand bone, with the fingers hanging off the forearm. A `hand_l`/`hand_r`
  is added at the wrist, and the palm (the forearm's weights past 90% of its length) is reweighted to it.
- **Removed:** the IK pole targets and the constraints.
- **Spine:** it has only two spine bones (`spine_01`, `spine_02`). Manny's longer spine chain is
  mapped onto them.

**Outfit heads.** The outfits ship headless; they're meant to be worn by a Universal Base Character.
The Standard (free) pack has only the Superhero bodies. Their head, eyes and brows are cut at the neck
(vertices weighted at least 60% to `Head`/`neck_01`) and grafted onto the outfit's armature, offset by
the difference between the two `Head` bones.
