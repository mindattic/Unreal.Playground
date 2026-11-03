# SourceArt manifest

Raw source files for import into `Content/`. All are KayKit (CC0, see `CREDITS.md`).

## Format and rig

- **FBX 7.4 binary**, **Y-up**, **UnitScaleFactor 100** (authored in meters; Unreal's FBX importer
  converts to cm and Z-up).
- Characters are about **2.2 m tall** (stylised, big heads). Set the capsule half-height to about 110,
  or scale the mesh down about 0.85 to fit the default 88 half-height capsule.
- **One shared rig.** The Knight and every skeleton use the same 41-joint KayKit rig, with identical
  bone names in identical order: `root, hips, spine, chest, upperarm.l/.r, lowerarm.l/.r, wrist.l/.r,
  hand.l/.r, handslot.l/.r, head, upperleg/lowerleg/foot/toes .l/.r` plus IK/control helper bones.
  Import the Knight first, then import each skeleton onto the Knight's `USkeleton` so they share
  animations.
- Weapon sockets: attach weapon props to bones **`handslot.r`** (weapon) and **`handslot.l`**
  (shield or off-hand).
- Each character uses one 1024x1024 gradient-atlas texture (`knight_texture.png` /
  `skeleton_texture.png`).
- **Animations are embedded in each character FBX** as takes; there is no separate animation file.
  Knight.fbx has 76 takes. Each Skeleton_*.fbx has 95 takes: the same 76 plus skeleton-only clips like
  `Idle_Combat`, `Spawn_Ground_Skeletons`, `Skeletons_Awaken_Standing`, `Taunt`, `Running_C`,
  `Walking_D_Skeletons`.

## Files

| Path | What | Notes |
|---|---|---|
| `Characters/Knight/Knight.fbx` | Player character + 76 animation takes | Skinned parts: Knight_Body/Head/ArmLeft/ArmRight/LegLeft/LegRight. **Rigid meshes parented to bones and embedded in the file:** Knight_Helmet (head), Knight_Cape (chest), 1H_Sword + 2H_Sword (handslot.r), 1H_Sword_Offhand + Badge/Rectangle/Round/Spike_Shield (handslot.l). On import these all merge into one skeletal mesh, so every sword and all four shields show at once. Either hide the unwanted sections, or turn off "Import Meshes in Bone Hierarchy" and attach the props from `Props/Knight/` instead. |
| `Characters/Knight/knight_texture.png` | Knight atlas | |
| `Characters/Skeletons/Skeleton_Warrior.fbx` | Enemy: armored skeleton + 95 takes | Helmet is rigid on `head`; everything else is skinned. No weapon included. |
| `Characters/Skeletons/Skeleton_Minion.fbx` | Enemy: basic skeleton + 95 takes | No weapon included. |
| `Characters/Skeletons/Skeleton_Rogue.fbx` | Enemy: hooded skeleton + 95 takes | No weapon included. |
| `Characters/Skeletons/skeleton_texture.png` | Shared skeleton atlas | |
| `Props/Knight/sword_1handed.fbx`, `shield_round.fbx` | Standalone static meshes for the knight's sword and shield | Attach to `handslot.r` / `handslot.l`. |
| `Props/Skeletons/Skeleton_Blade.fbx`, `Skeleton_Axe.fbx` | Enemy weapons (static) | `handslot.r` |
| `Props/Skeletons/Skeleton_Shield_Small_A.fbx`, `Skeleton_Shield_Large_A.fbx` | Enemy shields (static) | `handslot.l` |

## Animation mapping (take names, identical in all four character FBXs unless noted)

| Ours | Take | Length | Note |
|---|---|---|---|
| idle | `Idle` | 1.07 s | Skeletons also have `Idle_Combat` (4.27 s) |
| walk | `Walking_A` | 1.07 s | |
| run | `Running_A` | 0.80 s | |
| light_attack | `1H_Melee_Attack_Slice_Diagonal` | 1.00 s | Alternates: `1H_Melee_Attack_Slice_Horizontal`, `1H_Melee_Attack_Stab` |
| heavy_attack | `2H_Melee_Attack_Chop` | 1.63 s | Overhead. Alternate: `1H_Melee_Attack_Chop` (1.07 s) |
| block | `Blocking` | 1.07 s | Loop while held. `Block` is the raise-shield move. |
| block_hit | `Block_Hit` | 1.07 s | `Block_Attack` (shield bash) works as the parry riposte |
| dodge_roll | `Dodge_Forward` | 0.40 s | **No true roll exists**; this is a quick dodge step. Directional: `Dodge_Backward` / `Dodge_Left` / `Dodge_Right` |
| hit | `Hit_A` | 0.67 s | Alternate `Hit_B` |
| death | `Death_A` | 0.80 s | `Death_A_Pose` is the held end pose. Skeletons: `Death_C_Skeletons` (collapse into a pile) |
| heal | `Use_Item` | 1.60 s | Alternates: `Interact`, `Cheer` |
| enemy spawn (optional) | `Spawn_Ground_Skeletons` / `Skeletons_Awaken_Standing` | 3.57 s / 1.00 s | Skeleton FBXs only |
| enemy taunt (optional) | `Taunt` | 1.03 s | Skeleton FBXs only |
