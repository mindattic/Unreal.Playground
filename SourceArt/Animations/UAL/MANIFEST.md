# Universal Animation Library 1 + 2 (Quaternius, CC0): manifest

Both packs are the free **Standard** editions. Each file is a male "UAL mannequin" mesh with its
animations baked into one skeleton. Credits are in `CREDITS.md`.

## Files

| File | Format | Clips | Rig | Notes |
|---|---|---|---|---|
| `UAL1/AL_Standard.fbx` | FBX 7.x binary (Blender export) | 45 | UE-style names, about 53 joints | The pack's "Unreal Engine" export. Takes match the Godot GLB of UAL1. |
| `UAL2/UAL2_Standard.glb` | glTF 2.0 binary | 43 | UE-style names, 65 joints | The pack's "Unreal-Godot" export. Y-up, metres (glTF), so it imports through Interchange. |
| `UAL2/Mannequin_F.glb` | glTF 2.0 binary | 0 | same 65-joint rig | Female mannequin mesh only. The pack says to retarget UAL2 clips onto it. |
| `UAL1/UnrealEngine_Setup.png`, `UAL2/Unreal_Setup.png` | PNG | | | Quaternius's import instructions (summarised below). |

### Rig

The joints follow UE5 mannequin naming:
`root, pelvis, spine_01-03, neck_01, Head, clavicle_l/r, upperarm_l/r, lowerarm_l/r, hand_l/r`
plus finger chains (`index_01-03_l/r`, `*_04_leaf_*`, middle, ring, pinky, thumb) and
`thigh, calf, foot, ball (+ball_leaf) _l/r`.

It is **not identical to UE5 Manny/Quinn**. The head bone is capitalised `Head`, and there are
no ik_* bones, twist or corrective bones, or metacarpals. Manny has about 89 bones; this rig has
65. So the clips need an **IK Retargeter** to play on SK_Mannequin. Because the names line up,
auto-mapping chains is near trivial. The alternative is to use the UAL mannequin mesh itself as
the character.

### Import settings (from Quaternius's setup sheets)

- Skeleton: **None** on import.
- Uncheck "Use T0 As Ref Pose" on the FBX, so the rig keeps its T-pose for retargeting.
- Import animations on. Use the default sample rate (30 fps) or "Use 30Hz to Bake Bone
  Animation" for the GLB. Turn on "Snap to Closest Frame".
- Switch bone compression from ACL to `DefaultAnimBoneCompressionSettings`. ACL causes jitter
  on some clips.
- `_RM` clips carry root motion. The plain versions are in place.

## Clip mapping

`[1]` = UAL1 (`AL_Standard.fbx`), `[2]` = UAL2 (`UAL2_Standard.glb`).

| Need | Candidate clips |
|---|---|
| idle | `Idle_Loop` [1]; `Idle_FoldArms_Loop` [2] |
| walk | `Walk_Loop` [1]; `Walk_Formal_Loop` [1] |
| run / jog | `Jog_Fwd_Loop` [1] |
| sprint | `Sprint_Loop` [1] |
| jump start / loop / land | `Jump_Start`, `Jump_Loop`, `Jump_Land` [1]; `NinjaJump_Start`, `NinjaJump_Idle_Loop`, `NinjaJump_Land` [2] |
| sword idle | `Sword_Idle` [1] |
| sword light attacks (combo) | `Sword_Regular_A`, `Sword_Regular_B`, `Sword_Regular_C` [2], each with a `*_Rec` recovery; `Sword_Regular_Combo` [2] (full chain); `Sword_Attack` / `Sword_Attack_RM` [1] |
| sword heavy / overhead | none dedicated. Closest: `Sword_Regular_C` [2] (finisher), `Sword_Dash_RM` [2] (lunge), `Shield_OneShot` [2] (shield bash) |
| block idle | `Sword_Block` [2]; `Idle_Shield_Loop` [2] |
| block hit / guard break | `Idle_Shield_Break` [2]; `Hit_Knockback` / `Hit_Knockback_RM` [2] |
| parry | none dedicated. Closest: `Sword_Block` [2] (short), `Shield_OneShot` [2] |
| dodge roll (forward) | `Roll`, `Roll_RM` [1] |
| roll, other directions | none. Rotate the actor before `Roll`, or use `Slide_Start` / `Slide_Loop` / `Slide_Exit` [2] |
| backstep / dodge | none. Closest: `Shield_Dash_RM`, `Sword_Dash_RM` [2] (forward dashes) |
| hit react | `Hit_Chest`, `Hit_Head` [1]; `Hit_Knockback`, `Hit_Knockback_RM` [2] |
| death | `Death01` [1] |
| drink / use item | `Consume` [2]; `Interact`, `PickUp_Table` [1] |
| climb / hang / mantle | `ClimbUp_1m_RM` [2] (1 m mantle). No hang or climb loop. |
| crouch / sneak | `Crouch_Idle_Loop`, `Crouch_Fwd_Loop` [1] |
| stealth stab / takedown | none. Closest: `Sword_Attack` [1], `Melee_Hook` [2] |
| swimming (the sea) | `Swim_Idle_Loop`, `Swim_Fwd_Loop` [1] |

### Full clip lists

**UAL1** (45): A_TPose, Crouch_Fwd_Loop, Crouch_Idle_Loop, Dance_Loop, Death01, Driving_Loop,
Fixing_Kneeling, Hit_Chest, Hit_Head, Idle_Loop, Idle_Talking_Loop, Idle_Torch_Loop, Interact,
Jog_Fwd_Loop, Jump_Land, Jump_Loop, Jump_Start, PickUp_Table, Pistol_Aim_Down,
Pistol_Aim_Neutral, Pistol_Aim_Up, Pistol_Idle_Loop, Pistol_Reload, Pistol_Shoot, Punch_Cross,
Punch_Jab, Push_Loop, Roll, Roll_RM, Sitting_Enter, Sitting_Exit, Sitting_Idle_Loop,
Sitting_Talking_Loop, Spell_Simple_Enter, Spell_Simple_Exit, Spell_Simple_Idle_Loop,
Spell_Simple_Shoot, Sprint_Loop, Swim_Fwd_Loop, Swim_Idle_Loop, Sword_Attack, Sword_Attack_RM,
Sword_Idle, Walk_Formal_Loop, Walk_Loop. (The UAL1 Godot GLB also lists Punch_Enter, which is
not found in the Unreal FBX.)

**UAL2** (43): A_TPose, Chest_Open, ClimbUp_1m_RM, Consume, Farm_Harvest, Farm_PlantSeed,
Farm_Watering, Hit_Knockback, Hit_Knockback_RM, Idle_FoldArms_Loop, Idle_Lantern_Loop,
Idle_No_Loop, Idle_Rail_Call, Idle_Rail_Loop, Idle_Shield_Break, Idle_Shield_Loop,
Idle_TalkingPhone_Loop, LayToIdle, Melee_Hook, Melee_Hook_Rec, NinjaJump_Idle_Loop,
NinjaJump_Land, NinjaJump_Start, OverhandThrow, Shield_Dash_RM, Shield_OneShot, Slide_Exit,
Slide_Loop, Slide_Start, Sword_Block, Sword_Dash_RM, Sword_Regular_A, Sword_Regular_A_Rec,
Sword_Regular_B, Sword_Regular_B_Rec, Sword_Regular_C, Sword_Regular_Combo, TreeChopping_Loop,
Walk_Carry_Loop, Yes, Zombie_Idle_Loop, Zombie_Scratch, Zombie_Walk_Fwd_Loop.
