"""Editor-Python (UE 5.8): the bestiary (UNR-T64/T67) and the Ranger player body.

Runs after tools/build_souls_character.py (it needs IK_Manny, the montages under /Game/Souls/Montages,
the weapons and BP_SoulsEnemy / BP_SoulsKnight) and tools/build_shadow_boss.py:

  UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=tools/build_bestiary.py -unattended -nop4 -nosplash

For every SourceArt/Characters/Bestiary/<Type>/<Type>.glb (made by tools/convert_bestiary.py):
  1. import it (glTF, Interchange) to /Game/Souls/Bestiary/<Type>/Mesh, and <Type>_Weapon.glb if any;
  2. IK_<Type>: IK_Manny's retarget chains copied by name (the GLBs use UE mannequin bone names, so
     only missing bones need a stand-in: spine_05 -> the top spine bone, ball_l -> foot_l, ...);
  3. RTG_Manny_To_<Type>, then one batch retarget of every montage the knight uses, the template's
     locomotion anim blueprint (ABP_Manny_Combat, with its blendspaces and sequences) and A_Death, all
     onto <Type> under /Game/Souls/Bestiary/<Type>/Anims (prefix "<Type>_"); the fists baked into the
     Manny clips (tools/bake_grip.py) come across with the finger chains;
  4. /Game/Souls/Bestiary/BP_SoulsEnemy_<Type>: BP_SoulsEnemy duplicated onto the C++ type
     (ASoulsSkeleton, ASoulsBrute, ... in Source/UnrealSoulslike/Souls/SoulsMonster.h, which hold the
     tuned numbers), with the mesh, anim class, every montage swapped for the retargeted one, no Manny
     armour tint, and the weapon fitted to the hand (see fit_to_hand).

The player: PLAYER_BODY ("Ranger", or "Manny" to keep Epic's mannequin; the env var SOULS_PLAYER_BODY
overrides it). BP_SoulsKnight switches to the Ranger only when every montage it uses, the anim blueprint
and the hand fits all came through; otherwise it stays on Manny with a WARNING.

Writes Saved/bestiary.log (ends BESTIARY_OK).
"""
import math
import os

import unreal

PLAYER_BODY = os.environ.get("SOULS_PLAYER_BODY", "Ranger")

PROJECT = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
LOG = open(os.path.join(PROJECT, "Saved", "bestiary.log"), "w", encoding="utf-8")
SRC = os.path.join(PROJECT, "SourceArt", "Characters", "Bestiary")
eal = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
ML = unreal.MathLibrary
APE = unreal.AnimPoseExtensions
problems = []

SOULS = "/Game/Souls"
BESTIARY = SOULS + "/Bestiary"
MANNY_MESH = "/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple"
IK_MANNY = SOULS + "/Rigs/IK_Manny"
SWORD = SOULS + "/Weapons/sword/KnightSword"
SHIELD = SOULS + "/Weapons/shield/KiteShield"
DEATH_ANIM = SOULS + "/Anims/A_Death"
KNIGHT_SWORD_GRIP_Z = -0.75   # KnightSword's grip along its +Z blade axis (mesh units, before the 55x scale)

# Type -> C++ class, weapon (own GLB / another type's / the knight's sword, invisible for claws and the
# imp's cudgel, which is part of its mesh), weapon scale, shield.
TYPES = {
    "Skeleton": dict(cls="SoulsSkeleton", weapon="own", weapon_scale=0.7),
    "Brute": dict(cls="SoulsBrute", weapon="own", weapon_scale=1.0),
    "Zombie": dict(cls="SoulsZombie", weapon="unseen", weapon_scale=28.0, grab="AM_Throw"),
    "Hollow": dict(cls="SoulsHollow", weapon="Skeleton", weapon_scale=0.5),
    "Huntress": dict(cls="SoulsHuntress", weapon="sword", weapon_scale=50.0),
    "Imp": dict(cls="SoulsImp", weapon="unseen", weapon_scale=40.0),
}
PLAYER_TYPES = ("Ranger",)

MONTAGE_PROPS = ("HeavyAttackMontage", "RollMontage", "BlockMontage", "HealMontage", "HitMontage", "ParryMontage",
                 "StaggerMontage", "GuardBreakMontage", "CriticalMontage", "SwimMontage", "SwimIdleMontage", "ThrowMontage",
                 "CrouchIdleMontage", "CrouchMoveMontage", "CrawlMontage", "GetUpMontage", "TalkMontage")
MONTAGE_ARRAYS = ("SwordComboMontages", "GestureMontages")


def log(msg):
    LOG.write(msg + "\n")
    LOG.flush()


def warn(msg):
    problems.append(msg)
    log("WARNING " + msg)


def ensure_dir(path):
    if not eal.does_directory_exist(path):
        eal.make_directory(path)


def assets_of(folder, cls):
    out = []
    for p in eal.list_assets(folder, recursive=True, include_folder=False):
        a = eal.load_asset(p)
        if isinstance(a, cls):
            out.append(a)
    return out


def import_glb(filename, dest):
    if eal.does_directory_exist(dest):
        eal.delete_directory(dest)
    ensure_dir(dest)
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", filename)
    task.set_editor_property("destination_path", dest)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("automated", True)
    task.set_editor_property("save", True)
    tools.import_asset_tasks([task])
    log("imported %s -> %d objects" % (os.path.basename(filename), len(task.get_editor_property("imported_object_paths") or [])))


def blueprint_cdo(path):
    name = path.rsplit("/", 1)[1]
    return unreal.get_default_object(unreal.load_object(None, "%s.%s_C" % (path, name)))


# ---------------------------------------------------------------------------------------------
# Reference-pose geometry (component space)
# ---------------------------------------------------------------------------------------------

class Pose:
    def __init__(self, mesh):
        self.pose = APE.get_reference_pose(mesh.get_editor_property("skeleton"))
        self.bones = set(str(b) for b in APE.get_bone_names(self.pose))

    def world(self, bone):
        return APE.get_ref_bone_pose(self.pose, bone, unreal.AnimPoseSpaces.WORLD)

    def at(self, bone):
        t = self.world(bone).translation
        return (t.x, t.y, t.z)


def vsub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def vdot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def vnorm(a):
    n = math.sqrt(vdot(a, a)) or 1.0
    return (a[0] / n, a[1] / n, a[2] / n)


def vec(a):
    return unreal.Vector(a[0], a[1], a[2])


def facing_yaw(pose):
    """Facing (deg, component space) from the shoulders; any consistent definition works, since only
    the difference between two rigs is used."""
    r = vsub(pose.at("upperarm_r"), pose.at("upperarm_l"))
    return math.degrees(math.atan2(-r[0], r[1]))


def rigid(t):
    return unreal.Transform(t.translation, t.rotation.rotator(), unreal.Vector(1.0, 1.0, 1.0))


def hand_frame(pose, side):
    """An anatomical frame on the hand: X along the fingers, Z across the knuckles (pinky -> index)."""
    hand = pose.at("hand_" + side)
    x = vnorm(vsub(pose.at("middle_01_" + side), hand))
    zp = vnorm(vsub(pose.at("index_01_" + side), pose.at("pinky_01_" + side)))
    d = vdot(x, zp)
    z = vnorm((zp[0] - x[0] * d, zp[1] - x[1] * d, zp[2] - x[2] * d))
    rot = ML.make_rot_from_xz(vec(x), vec(z))
    length = math.sqrt(vdot(vsub(pose.at("middle_01_" + side), hand), vsub(pose.at("middle_01_" + side), hand)))
    return unreal.Transform(vec(hand), rot, unreal.Vector(1.0, 1.0, 1.0)), length


def fit_to_hand(rel, side, src_pose, dst_pose):
    """Carry a fit tuned on Manny's hand (rel: relative to hand_<side>) to another rig's hand by matching
    the anatomy (fingers, knuckles) instead of the bones' local axes, which differ from rig to rig."""
    a_src, len_src = hand_frame(src_pose, side)
    a_dst, len_dst = hand_frame(dst_pose, side)
    world = ML.compose_transforms(rel, rigid(src_pose.world("hand_" + side)))
    local = ML.compose_transforms(world, ML.invert_transform(a_src))
    s = len_dst / max(len_src, 1e-3)
    p = local.translation
    local = unreal.Transform(unreal.Vector(p.x * s, p.y * s, p.z * s), local.rotation.rotator(), unreal.Vector(1.0, 1.0, 1.0))
    out = ML.compose_transforms(ML.compose_transforms(local, a_dst), ML.invert_transform(rigid(dst_pose.world("hand_" + side))))
    return out


def spec_transform(spec):
    return unreal.Transform(unreal.Vector(*spec["location"]), unreal.Rotator(*spec["rotation"]), unreal.Vector(1.0, 1.0, 1.0))


def manny_grip(sword_fit):
    """Where the knight sword's grip sits in Manny's hand_r (its blade +Z), from SWORD_TRANSFORM."""
    offset = unreal.Transform(unreal.Vector(0.0, 0.0, KNIGHT_SWORD_GRIP_Z * sword_fit["scale"]),
                              unreal.Rotator(0.0, 0.0, 0.0), unreal.Vector(1.0, 1.0, 1.0))
    return ML.compose_transforms(offset, spec_transform(sword_fit))


def knight_sword_on(grip, scale):
    """The knight sword's component transform that puts its grip on `grip` at this scale."""
    back = unreal.Transform(unreal.Vector(0.0, 0.0, -KNIGHT_SWORD_GRIP_Z * scale), unreal.Rotator(0.0, 0.0, 0.0),
                            unreal.Vector(1.0, 1.0, 1.0))
    return ML.compose_transforms(back, grip)


def load_manny_fit():
    """SWORD_TRANSFORM / SHIELD_TRANSFORM, parsed from build_souls_character.py (as refit_weapons.py does)."""
    fit = {}
    with open(os.path.join(PROJECT, "tools", "build_souls_character.py"), encoding="utf-8") as handle:
        for line in handle:
            if line.startswith(("SWORD_TRANSFORM =", "SHIELD_TRANSFORM =")):
                name, value = line.split("=", 1)
                fit[name.strip()] = eval(value.strip(), {"__builtins__": {}})
    return fit["SWORD_TRANSFORM"], fit["SHIELD_TRANSFORM"]


def place(component, mesh, rel, scale, materials=None):
    component.set_editor_property("static_mesh", mesh)
    component.set_editor_property("relative_location", rel.translation)
    component.set_editor_property("relative_rotation", rel.rotation.rotator())
    component.set_editor_property("relative_scale3d", unreal.Vector(scale, scale, scale))
    component.set_editor_property("override_materials", materials or [])


# ---------------------------------------------------------------------------------------------
# Rigs and retargeting
# ---------------------------------------------------------------------------------------------

def stand_in(bone, have):
    """A target bone for one of Manny's chain bones, or None if the rig has nothing like it."""
    if bone in have:
        return bone
    if bone.startswith(("ik_", "interaction", "center_of_mass")) or "twist" in bone or "_corrective" in bone:
        return None
    if bone.startswith("spine_"):
        spines = sorted(b for b in have if b.startswith("spine_0"))
        return spines[-1] if spines else None
    if bone.startswith("neck_"):
        return "neck_01" if "neck_01" in have else None
    if bone.startswith("ball_"):
        return "foot_" + bone[-1] if "foot_" + bone[-1] in have else None
    if "_metacarpal_" in bone:
        finger, _, side = bone.split("_")
        return stand_in("%s_01_%s" % (finger, side), have)
    parts = bone.split("_")
    if len(parts) == 3 and parts[1].isdigit():
        for n in range(int(parts[1]) - 1, 0, -1):
            cand = "%s_%02d_%s" % (parts[0], n, parts[2])
            if cand in have:
                return cand
    return None


def make_rig(name, mesh, manny_rig):
    path = "%s/Rigs/%s" % (SOULS, name)
    rig = eal.load_asset(path) if eal.does_asset_exist(path) else \
        tools.create_asset(name, SOULS + "/Rigs", unreal.IKRigDefinition, unreal.IKRigDefinitionFactory())
    c = unreal.IKRigController.get_controller(rig)
    c.set_skeletal_mesh(mesh)
    for chain in list(c.get_retarget_chains()):
        c.remove_retarget_chain(chain.get_editor_property("chain_name"))
    have = Pose(mesh).bones
    c.set_retarget_root("pelvis")
    mc = unreal.IKRigController.get_controller(manny_rig)
    made, skipped = [], []
    for chain in mc.get_retarget_chains():
        cname = chain.get_editor_property("chain_name")
        start = stand_in(str(mc.get_retarget_chain_start_bone(cname)), have)
        end = stand_in(str(mc.get_retarget_chain_end_bone(cname)), have)
        if not start or not end:
            skipped.append(str(cname))
            continue
        c.add_retarget_chain(cname, start, end, "")
        made.append("%s(%s..%s)" % (cname, start, end))
    eal.save_loaded_asset(rig)
    log("ik rig %s: %d chains %s; no stand-in for %s" % (name, len(made), made, skipped))
    return rig, len(made)


def make_retargeter(name, source_rig, target_rig):
    path = "%s/Rigs/%s" % (SOULS, name)
    rtg = eal.load_asset(path) if eal.does_asset_exist(path) else \
        tools.create_asset(name, SOULS + "/Rigs", unreal.IKRetargeter, unreal.IKRetargetFactory())
    c = unreal.IKRetargeterController.get_controller(rtg)
    try:
        c.remove_all_ops()
    except Exception:
        pass
    c.set_ik_rig(unreal.RetargetSourceOrTarget.SOURCE, source_rig)
    c.set_ik_rig(unreal.RetargetSourceOrTarget.TARGET, target_rig)
    try:
        c.add_default_ops()
    except Exception as exc:
        log("%s add_default_ops: %s" % (name, exc))
    try:
        c.assign_ik_rig_to_all_ops(unreal.RetargetSourceOrTarget.TARGET, target_rig)
    except Exception as exc:
        log("%s assign_ik_rig_to_all_ops: %s" % (name, exc))
    # Chain names were copied from IK_Manny, so the fuzzy match is an exact one.
    c.auto_map_chains(unreal.AutoMapChainType.FUZZY, True)
    try:
        c.auto_align_all_bones(unreal.RetargetSourceOrTarget.TARGET)
    except Exception as exc:
        log("%s auto_align_all_bones: %s" % (name, exc))
    eal.save_loaded_asset(rtg)
    return rtg


def batch_retarget(paths, source_mesh, target_mesh, rtg, prefix, dest):
    data = [eal.find_asset_data(p) for p in paths]
    if hasattr(unreal, "IKRetargetBatchOperationInputs"):
        inputs = unreal.IKRetargetBatchOperationInputs()
        for prop, value in (("assets_to_retarget", data), ("source_mesh", source_mesh), ("target_mesh", target_mesh),
                            ("ik_retarget_asset", rtg), ("prefix", prefix), ("target_path", dest), ("use_source_path", False),
                            ("include_referenced_assets", True), ("overwrite_existing_files", True)):
            inputs.set_editor_property(prop, value)
        return unreal.IKRetargetBatchOperation.run_batch_retarget(inputs)
    return unreal.IKRetargetBatchOperation.duplicate_and_retarget(
        assets_to_retarget=data, source_mesh=source_mesh, target_mesh=target_mesh, ik_retarget_asset=rtg,
        search="", replace="", prefix=prefix, suffix="", target_path=dest, use_source_path=False,
        include_referenced_assets=True, overwrite_existing_files=True)


def source_animation_assets(template_cdo):
    """Every montage the knight's blueprint uses, its anim blueprint, and the death clip."""
    paths = []
    for prop in MONTAGE_PROPS:
        m = template_cdo.get_editor_property(prop)
        if m:
            paths.append(m.get_path_name().split(".")[0])
    for prop in MONTAGE_ARRAYS:
        paths += [m.get_path_name().split(".")[0] for m in template_cdo.get_editor_property(prop) or [] if m]
    anim_class = template_cdo.get_editor_property("mesh").get_editor_property("anim_class")
    abp = anim_class.get_path_name().split(".")[0] if anim_class else None
    if abp:
        paths.append(abp)
    if eal.does_asset_exist(DEATH_ANIM):
        paths.append(DEATH_ANIM)
    # Throw doubles as the zombie's grab even where a blueprint doesn't use it.
    if eal.does_asset_exist(SOULS + "/Montages/AM_Throw"):
        paths.append(SOULS + "/Montages/AM_Throw")
    return sorted(set(paths)), abp


def retarget_kit(kind, mesh, template_cdo, manny, manny_rig):
    rig, chains = make_rig("IK_" + kind, mesh, manny_rig)
    if chains < 6:
        warn("%s: only %d retarget chains; animation will be poor" % (kind, chains))
    rtg = make_retargeter("RTG_Manny_To_" + kind, manny_rig, rig)
    dest = "%s/%s/Anims" % (BESTIARY, kind)
    if eal.does_directory_exist(dest):
        eal.delete_directory(dest)
    ensure_dir(dest)
    paths, abp = source_animation_assets(template_cdo)
    prefix = kind + "_"
    made = batch_retarget(paths, manny, mesh, rtg, prefix, dest)
    out = {}
    for a in eal.list_assets(dest, recursive=True, include_folder=False):
        asset = eal.load_asset(a)
        if asset is not None and asset.get_name().startswith(prefix):
            out[asset.get_name()[len(prefix):]] = asset
    log("%s: retargeted %d assets (%d returned) from %d sources" % (kind, len(out), len(made or []), len(paths)))
    anim_bp = out.get(abp.rsplit("/", 1)[1]) if abp else None
    if anim_bp is None:
        warn("%s: the anim blueprint %s didn't come through" % (kind, abp))
    else:
        unreal.BlueprintEditorLibrary.compile_blueprint(anim_bp)
        eal.save_loaded_asset(anim_bp)
    death = None
    if isinstance(out.get("A_Death"), unreal.AnimSequence):
        factory = unreal.AnimMontageFactory()
        factory.set_editor_property("source_animation", out["A_Death"])
        death = tools.create_asset(prefix + "AM_Death", dest, unreal.AnimMontage, factory)
        eal.save_loaded_asset(death)
    for asset in out.values():
        if isinstance(asset, unreal.AnimSequence):
            eal.save_loaded_asset(asset)
    return out, anim_bp, death


def swap_montages(cdo, template_cdo, kit, kind):
    """Every montage property the template sets -> the retargeted one of the same name. Returns the misses."""
    missing = []

    def counterpart(m):
        got = kit.get(m.get_name()) if m else None
        if m and not isinstance(got, unreal.AnimMontage):
            missing.append(m.get_name())
        return got if isinstance(got, unreal.AnimMontage) else None

    for prop in MONTAGE_PROPS:
        m = template_cdo.get_editor_property(prop)
        cdo.set_editor_property(prop, counterpart(m))
    for prop in MONTAGE_ARRAYS:
        cdo.set_editor_property(prop, [x for x in (counterpart(m) for m in template_cdo.get_editor_property(prop) or []) if x])
    if missing:
        warn("%s: no retargeted %s" % (kind, sorted(set(missing))))
    return missing


def set_body(cdo, mesh, anim_bp, yaw):
    comp = cdo.get_editor_property("mesh")
    comp.set_editor_property("skeletal_mesh_asset", mesh)
    if anim_bp is not None:
        comp.set_editor_property("anim_class", anim_bp.generated_class())
    rot = comp.get_editor_property("relative_rotation")
    comp.set_editor_property("relative_rotation", unreal.Rotator(rot.roll, rot.pitch, yaw))
    comp.set_editor_property("override_materials", [])
    cdo.set_editor_property("BodyMaterials", [])


def unseen_material():
    path = BESTIARY + "/M_BestiaryUnseen"
    mat = eal.load_asset(path) if eal.does_asset_exist(path) else \
        tools.create_asset("M_BestiaryUnseen", BESTIARY, unreal.Material, unreal.MaterialFactoryNew())
    MEL.delete_all_material_expressions(mat)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    zero = MEL.create_material_expression(mat, unreal.MaterialExpressionConstant, -300, 0)
    zero.set_editor_property("r", 0.0)
    MEL.connect_material_property(zero, "", unreal.MaterialProperty.MP_OPACITY_MASK)
    MEL.recompile_material(mat)
    eal.save_loaded_asset(mat)
    return mat


def import_character(kind):
    glb = os.path.join(SRC, kind, kind + ".glb")
    if not os.path.exists(glb):
        warn("%s: %s missing (run tools/convert_bestiary.py)" % (kind, glb))
        return None, None
    import_glb(glb, "%s/%s/Mesh" % (BESTIARY, kind))
    meshes = assets_of("%s/%s/Mesh" % (BESTIARY, kind), unreal.SkeletalMesh)
    if not meshes:
        warn("%s: no skeletal mesh imported" % kind)
        return None, None
    if len(meshes) > 1:
        warn("%s: %d skeletal meshes imported, using %s" % (kind, len(meshes), meshes[0].get_name()))
    mesh = meshes[0]
    weapon = None
    wglb = os.path.join(SRC, kind, kind + "_Weapon.glb")
    if os.path.exists(wglb):
        import_glb(wglb, "%s/%s/Weapon" % (BESTIARY, kind))
        weapon = (assets_of("%s/%s/Weapon" % (BESTIARY, kind), unreal.StaticMesh) or [None])[0]
    pose = Pose(mesh)
    need = [b for b in ("pelvis", "hand_l", "hand_r", "upperarm_l", "upperarm_r", "middle_01_l", "middle_01_r",
                        "index_01_l", "index_01_r", "pinky_01_l", "pinky_01_r") if b not in pose.bones]
    if need:
        warn("%s: bones missing %s" % (kind, need))
    physics = mesh.get_editor_property("physics_asset")
    log("%s: mesh %s, %d bones, physics asset %s, weapon %s" % (kind, mesh.get_path_name(), len(pose.bones),
                                                               physics.get_name() if physics else None,
                                                               weapon.get_name() if weapon else None))
    if physics is None:
        log("%s: no physics asset - it plays its death montage instead of ragdolling (ASoulsMonster::HandleDeath)" % kind)
    return mesh, weapon


# ---------------------------------------------------------------------------------------------
# Blueprints
# ---------------------------------------------------------------------------------------------

def build_type(kind, spec, ctx, weapons):
    mesh, own_weapon = import_character(kind)
    if mesh is None:
        return False
    kit, anim_bp, death = retarget_kit(kind, mesh, ctx["enemy_cdo"], ctx["manny"], ctx["manny_rig"])
    pose = Pose(mesh)
    yaw = ctx["manny_yaw"] + ctx["manny_facing"] - facing_yaw(pose)

    path = "%s/BP_SoulsEnemy_%s" % (BESTIARY, kind)
    if eal.does_asset_exist(path):
        eal.delete_asset(path)
    bp = eal.duplicate_asset(SOULS + "/BP_SoulsEnemy", path)
    unreal.BlueprintEditorLibrary.reparent_blueprint(bp, unreal.load_class(None, "/Script/UnrealSoulslike." + spec["cls"]))
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    cdo = blueprint_cdo(path)
    set_body(cdo, mesh, anim_bp, yaw)
    swap_montages(cdo, ctx["enemy_cdo"], kit, kind)
    if death is not None:
        cdo.set_editor_property("DeathMontage", death)
    if spec.get("grab") and isinstance(kit.get(spec["grab"]), unreal.AnimMontage):
        cdo.set_editor_property("GrabMontage", kit[spec["grab"]])

    # The weapon: its grip in the fist where Manny's sword grip sits, carried over by hand anatomy.
    grip = fit_to_hand(manny_grip(ctx["sword_fit"]), "r", ctx["manny_pose"], pose)
    choice = spec["weapon"]
    weapon_mesh = own_weapon if choice == "own" else weapons.get(choice)
    if choice in ("sword", "unseen"):
        place(cdo.get_editor_property("Sword"), ctx["sword"], knight_sword_on(grip, spec["weapon_scale"]), spec["weapon_scale"],
              [ctx["unseen"]] if choice == "unseen" else [])
    elif weapon_mesh is not None:
        # Our weapons: grip at the origin, haft +Z, edge +X; the knight sword's edges face +-Y.
        rel = ML.compose_transforms(unreal.Transform(unreal.Vector(0.0, 0.0, 0.0), unreal.Rotator(0.0, 0.0, 90.0),
                                                     unreal.Vector(1.0, 1.0, 1.0)), grip)
        place(cdo.get_editor_property("Sword"), weapon_mesh, rel, spec["weapon_scale"])
    else:
        warn("%s: no weapon mesh (%s)" % (kind, choice))
    shield = cdo.get_editor_property("Shield")
    shield.set_editor_property("static_mesh", None)
    cdo.set_editor_property("tags", ["Enemy", "Bestiary"])
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    eal.save_loaded_asset(bp)
    log("%s ready: yaw %.0f, %d combo montages, anim %s" % (path, yaw, len(cdo.get_editor_property("SwordComboMontages")),
                                                         anim_bp.get_name() if anim_bp else None))
    if own_weapon is not None:
        weapons[kind] = own_weapon
    return True


def build_player(kind, ctx):
    mesh, _ = import_character(kind)
    if mesh is None:
        return False
    knight_cdo = blueprint_cdo(SOULS + "/BP_SoulsKnight")
    kit, anim_bp, _ = retarget_kit(kind, mesh, knight_cdo, ctx["manny"], ctx["manny_rig"])
    pose = Pose(mesh)
    if PLAYER_BODY != kind:
        log("player stays on Manny (PLAYER_BODY=%s); %s built for a later switch" % (PLAYER_BODY, kind))
        return True
    # Only switch if the whole kit made it: anything missing and the knight stays Manny.
    probe = []
    for prop in MONTAGE_PROPS:
        m = knight_cdo.get_editor_property(prop)
        if m and not isinstance(kit.get(m.get_name()), unreal.AnimMontage):
            probe.append(m.get_name())
    for prop in MONTAGE_ARRAYS:
        probe += [m.get_name() for m in knight_cdo.get_editor_property(prop) or [] if m and not isinstance(kit.get(m.get_name()), unreal.AnimMontage)]
    if probe or anim_bp is None:
        warn("player stays on Manny: %s is missing %s%s" % (kind, sorted(set(probe)), "" if anim_bp else " and the anim blueprint"))
        return False
    try:
        grip = fit_to_hand(manny_grip(ctx["sword_fit"]), "r", ctx["manny_pose"], pose)
        sword_rel = knight_sword_on(grip, ctx["sword_fit"]["scale"])
        shield_rel = fit_to_hand(spec_transform(ctx["shield_fit"]), "l", ctx["manny_pose"], pose)
    except Exception as exc:
        warn("player stays on Manny: hand fit failed (%s)" % exc)
        return False
    bp = eal.load_asset(SOULS + "/BP_SoulsKnight")
    yaw = ctx["manny_yaw"] + ctx["manny_facing"] - facing_yaw(pose)
    set_body(knight_cdo, mesh, anim_bp, yaw)
    swap_montages(knight_cdo, knight_cdo, kit, kind)
    place(knight_cdo.get_editor_property("Sword"), ctx["sword"], sword_rel, ctx["sword_fit"]["scale"])
    place(knight_cdo.get_editor_property("Shield"), ctx["shield"], shield_rel, ctx["shield_fit"]["scale"])
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    eal.save_loaded_asset(bp)
    log("BP_SoulsKnight now wears %s (yaw %.0f; sword %s %s; shield %s %s)" % (
        kind, yaw, sword_rel.translation, sword_rel.rotation.rotator(), shield_rel.translation, shield_rel.rotation.rotator()))
    return True


def main():
    ensure_dir(BESTIARY)
    unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX true")
    manny = eal.load_asset(MANNY_MESH)
    manny_rig = eal.load_asset(IK_MANNY)
    if not (manny and manny_rig and eal.does_asset_exist(SOULS + "/BP_SoulsEnemy")):
        raise RuntimeError("run tools/build_souls_character.py first (Manny, IK_Manny, BP_SoulsEnemy)")
    enemy_cdo = blueprint_cdo(SOULS + "/BP_SoulsEnemy")
    manny_pose = Pose(manny)
    sword_fit, shield_fit = load_manny_fit()
    ctx = dict(manny=manny, manny_rig=manny_rig, enemy_cdo=enemy_cdo, manny_pose=manny_pose,
               manny_yaw=enemy_cdo.get_editor_property("mesh").get_editor_property("relative_rotation").yaw,
               manny_facing=facing_yaw(manny_pose), sword_fit=sword_fit, shield_fit=shield_fit,
               sword=eal.load_asset(SWORD), shield=eal.load_asset(SHIELD), unseen=unseen_material())
    log("manny: mesh yaw %.0f, facing %.0f, sword fit %s, shield fit %s" % (ctx["manny_yaw"], ctx["manny_facing"], sword_fit, shield_fit))
    weapons = {"sword": ctx["sword"]}
    # Types with their own weapon first, so the Hollow can borrow the skeleton's cleaver.
    built = []
    for kind in sorted(TYPES, key=lambda k: TYPES[k]["weapon"] != "own"):
        try:
            if build_type(kind, TYPES[kind], ctx, weapons):
                built.append(kind)
        except Exception as exc:
            import traceback
            warn("%s failed: %s\n%s" % (kind, exc, traceback.format_exc()))
    for kind in PLAYER_TYPES:
        try:
            build_player(kind, ctx)
        except Exception as exc:
            import traceback
            warn("player %s failed: %s\n%s" % (kind, exc, traceback.format_exc()))
    eal.save_directory(BESTIARY, only_if_is_dirty=True, recursive=True)
    log("bestiary: %s" % built)
    log("problems: %d" % len(problems))
    for p in problems:
        log("  - " + p)
    log("BESTIARY_OK")


try:
    main()
except Exception as exc:
    import traceback
    log("FAILED: %s\n%s" % (exc, traceback.format_exc()))
