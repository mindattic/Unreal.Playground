"""Blender (4.4+) script: normalise the bestiary's CC0 characters into UE-ready skinned GLBs (UNR-T64/T67).

  python -I tools/fetch_bestiary.py      # raw sources -> Saved/BestiarySource (no login)
  blender -b --factory-startup --disable-autoexec --python tools/convert_bestiary.py [-- --preview]

Writes SourceArt/Characters/Bestiary/<Type>/<Type>.glb (and <Type>_Weapon.glb where the model
brings its own weapon). tools/build_bestiary.py (editor Python) imports them, retargets the knight's
whole Manny kit onto each and builds BP_SoulsEnemy_<Type> (and the Ranger player body).

Every output gets the same treatment so one UE pipeline fits all:
  - bones renamed to the UE5 mannequin names (pelvis, spine_01, clavicle_l, upperarm_l, lowerarm_l,
    hand_l, index_01_l.., thigh_l, calf_l, foot_l, neck_01, head) so tools/build_bestiary.py can
    copy IK_Manny's retarget chains by name, and the C++ sword/shield sockets (hand_r / hand_l) exist;
  - Quaternius' older "CharacterArmature" rig is repaired: its IK feet hang off Root (re-parented
    under the shins), it has no hand bone (one is added at the wrist and the palm reweighted to it),
    and the IK pole targets are dropped;
  - the Modular Outfits (Ranger / Peasant) ship without a head: the Universal Base Character's head,
    eyes and brows are cut at the neck and grafted on (plus a hairstyle);
  - every character faces the same way as the UAL mannequin (-Y in Blender, left hand on +X), stands
    with its feet on the origin, is HEIGHT metres tall, and carries no animation (the kit is
    retargeted from Manny in UE); 4K textures are halved to 2K.
--preview renders each result (front and side) to Saved/bestiary_<Type>.png.
"""
import math
import os
import sys

import bmesh
import bpy
import mathutils

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SRC = os.path.join(ROOT, "Saved", "BestiarySource")
OUT = os.path.join(ROOT, "SourceArt", "Characters", "Bestiary")
PREVIEW = "--preview" in sys.argv
ONLY = [a for a in sys.argv[sys.argv.index("--") + 1:] if not a.startswith("--")] if "--" in sys.argv else []

UBC = os.path.join(SRC, "UniversalBaseCharacters", "Universal Base Characters[Standard]")
OUTFITS = os.path.join(SRC, "ModularOutfitsFantasy", "Modular Character Outfits - Fantasy[Standard]",
                       "Exports", "glTF (Godot-Unreal)", "Outfits")
HAIR = os.path.join(UBC, "Hairstyles", "Rigged to Head Bone", "glTF (Godot -Unreal)")
HEAD_MALE = os.path.join(UBC, "Base Characters", "Godot - UE", "Superhero_Male_FullBody.gltf")
HEAD_FEMALE = os.path.join(UBC, "Base Characters", "Godot - UE", "Superhero_Female_FullBody.gltf")

# Type: source file, rig family, head donor (outfits), hair, weapon object, standing height (m).
TYPES = {
    "Ranger": dict(src=os.path.join(OUTFITS, "Male_Ranger.gltf"), rig="ue", head=HEAD_MALE, hair="Hair_Beard", height=1.84),
    "Huntress": dict(src=os.path.join(OUTFITS, "Female_Ranger.gltf"), rig="ue", head=HEAD_FEMALE, hair=None, height=1.76),
    "Hollow": dict(src=os.path.join(OUTFITS, "Male_Peasant.gltf"), rig="ue", head=HEAD_MALE, hair="Hair_Long", height=1.78),
    "Skeleton": dict(src=os.path.join(SRC, "Skeleton.glb"), rig="quat", weapon="Weapon_Dagger", height=1.80),
    "Brute": dict(src=os.path.join(SRC, "Orc.glb"), rig="quat", weapon="Orc_Weapon", height=1.80),
    "Zombie": dict(src=os.path.join(SRC, "Zombie.glb"), rig="quat", height=1.80),
    "Imp": dict(src=os.path.join(SRC, "Demon.glb"), rig="quat", height=1.80),
}

QUAT_RENAME = {"Root": "root", "Body": "body", "Hips": "pelvis", "Abdomen": "spine_01", "Torso": "spine_02",
               "Neck": "neck_01", "Head": "head"}
for _s, _u in (("L", "l"), ("R", "r")):
    QUAT_RENAME.update({"Shoulder.%s" % _s: "clavicle_%s" % _u, "UpperArm.%s" % _s: "upperarm_%s" % _u,
                        "LowerArm.%s" % _s: "lowerarm_%s" % _u, "UpperLeg.%s" % _s: "thigh_%s" % _u,
                        "LowerLeg.%s" % _s: "calf_%s" % _u, "Foot.%s" % _s: "foot_%s" % _u})
    for _f, _uf in (("Index", "index"), ("Middle", "middle"), ("Pinky", "pinky"), ("Ring", "ring"), ("Thumb", "thumb")):
        for _i in (1, 2, 3):
            QUAT_RENAME["%s%d.%s" % (_f, _i, _s)] = "%s_%02d_%s" % (_uf, _i, _u)


def log(msg):
    print("BESTIARY " + msg, flush=True)


def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)


def import_gltf(path):
    before = set(bpy.data.objects)
    bpy.ops.import_scene.gltf(filepath=path)
    return [o for o in bpy.data.objects if o not in before]


def armature_of(objs):
    return next(o for o in objs if o.type == "ARMATURE")


def bake_parents(objs):
    """Clear every parent chain into world transforms, apply them, and drop the importer's helper empties."""
    for o in objs:
        if o.type == "MESH" and o.name.startswith("Icosphere") and not o.vertex_groups:
            bpy.data.objects.remove(o, do_unlink=True)
    objs = [o for o in bpy.data.objects]
    arm = next((o for o in objs if o.type == "ARMATURE"), None)
    for o in objs:
        if o.type == "EMPTY":
            continue
        mw = o.matrix_world.copy()
        if o.parent is not None and o.parent.type == "EMPTY":
            o.parent = None
            o.matrix_world = mw
    for o in [o for o in bpy.data.objects if o.type == "EMPTY"]:
        bpy.data.objects.remove(o, do_unlink=True)
    bpy.ops.object.select_all(action="DESELECT")
    for o in bpy.data.objects:
        if o.type in ("MESH", "ARMATURE"):
            o.select_set(True)
    bpy.context.view_layer.objects.active = arm
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    return arm


def edit(arm):
    bpy.ops.object.select_all(action="DESELECT")
    arm.select_set(True)
    bpy.context.view_layer.objects.active = arm
    bpy.ops.object.mode_set(mode="EDIT")
    return arm.data.edit_bones


def done_edit():
    bpy.ops.object.mode_set(mode="OBJECT")


def skinned(arm):
    return [o for o in bpy.data.objects if o.type == "MESH" and o.find_armature() == arm]


def repair_quaternius(arm):
    """CharacterArmature -> UE names; feet under the shins; a real hand bone; no IK helpers."""
    for pb in arm.pose.bones:
        for c in list(pb.constraints):
            pb.constraints.remove(c)
    bones = edit(arm)
    for name in [b.name for b in bones]:
        if name.startswith("PoleTarget"):
            bones.remove(bones[name])
    for old, new in QUAT_RENAME.items():
        if old in bones:
            bones[old].name = new
    for side in ("l", "r"):
        foot, calf = bones.get("foot_" + side), bones.get("calf_" + side)
        if foot and calf:
            foot.use_connect = False
            foot.parent = calf
        lower = bones["lowerarm_" + side]
        firsts = [bones[n] for n in ("index_01_" + side, "middle_01_" + side, "pinky_01_" + side, "ring_01_" + side) if n in bones]
        hand = bones.new("hand_" + side)
        hand.head = lower.tail.copy()
        knuckles = sum((b.head for b in firsts), mathutils.Vector()) / max(len(firsts), 1)
        hand.tail = knuckles if (knuckles - hand.head).length > 1e-4 else hand.head + (lower.tail - lower.head) * 0.3
        hand.roll = lower.roll
        hand.parent = lower
        for b in bones:
            if b.parent == lower and b.name != hand.name and (b.name.split("_")[0] in ("index", "middle", "pinky", "ring", "thumb")):
                b.parent = hand
    arm_axes = {side: (bones["lowerarm_" + side].head.copy(), bones["lowerarm_" + side].tail.copy()) for side in ("l", "r")}
    done_edit()
    # Palm: whatever the forearm carries past the wrist now follows the hand.
    for mesh in skinned(arm):
        groups = {g.name: g for g in mesh.vertex_groups}
        for old, new in QUAT_RENAME.items():
            if old in groups and new not in groups:
                groups[old].name = new
        groups = {g.name: g for g in mesh.vertex_groups}
        for side, (a, b) in arm_axes.items():
            src = groups.get("lowerarm_" + side)
            if src is None:
                continue
            dst = groups.get("hand_" + side) or mesh.vertex_groups.new(name="hand_" + side)
            axis = b - a
            length2 = axis.length_squared
            for v in mesh.data.vertices:
                w = next((g.weight for g in v.groups if g.group == src.index), 0.0)
                if w <= 0.0:
                    continue
                t = (mesh.matrix_world @ v.co - a).dot(axis) / length2
                share = min(max((t - 0.9) / 0.12, 0.0), 1.0)
                if share > 0.0:
                    src.add([v.index], w * (1.0 - share), "REPLACE")
                    dst.add([v.index], w * share, "ADD")


def rename_head(arm):
    bones = edit(arm)
    if "Head" in bones:
        bones["Head"].name = "head"
    done_edit()
    for mesh in skinned(arm):
        g = mesh.vertex_groups.get("Head")
        if g:
            g.name = "head"


def graft(arm, donor_path, keep, min_weight=0.5):
    """Cut the meshes of donor_path down to what is weighted to `keep` bones and skin them to arm."""
    objs = import_gltf(donor_path)
    helpers = [o.name for o in objs if o.type in ("ARMATURE", "EMPTY")]
    donor = armature_of(objs)
    delta = arm.matrix_world @ arm.data.bones["head"].head_local - donor.matrix_world @ donor.data.bones["Head"].head_local
    for o in [o for o in objs if o.type == "MESH"]:
        if not o.vertex_groups:
            bpy.data.objects.remove(o, do_unlink=True)
            continue
        idx = {g.index for g in o.vertex_groups if g.name in keep}
        cut = {v.index for v in o.data.vertices if sum(g.weight for g in v.groups if g.group in idx) < min_weight}
        doomed = len(cut)
        bm = bmesh.new()
        bm.from_mesh(o.data)
        bm.verts.ensure_lookup_table()
        bmesh.ops.delete(bm, geom=[bm.verts[i] for i in cut], context="VERTS")
        bm.to_mesh(o.data)
        bm.free()
        if not o.data.vertices:
            bpy.data.objects.remove(o, do_unlink=True)
            continue
        mw = o.matrix_world.copy()
        o.parent = None
        o.matrix_world = mathutils.Matrix.Translation(delta) @ mw
        for m in o.modifiers:
            if m.type == "ARMATURE":
                m.object = arm
        o.parent = arm
        o.matrix_parent_inverse = arm.matrix_world.inverted()
        g = o.vertex_groups.get("Head")
        if g:
            g.name = "head"
        log("  grafted %s (%d verts kept, %d cut)" % (o.name, len(o.data.vertices), doomed))
    for name in helpers:
        if name in bpy.data.objects:
            bpy.data.objects.remove(bpy.data.objects[name], do_unlink=True)


def facing(arm):
    """Forward (Blender space) from the shoulders: cross(left - right, up)."""
    b = arm.data.bones
    left = arm.matrix_world @ b["upperarm_l"].head_local
    right = arm.matrix_world @ b["upperarm_r"].head_local
    f = (left - right).cross(mathutils.Vector((0, 0, 1)))
    f.z = 0.0
    return f.normalized()


def normalise(arm, height, weapon=None):
    """Face -Y (as the UAL mannequin does), feet on the origin, `height` tall (the weapon doesn't count)."""
    f = facing(arm)
    yaw = math.atan2(-1.0, 0.0) - math.atan2(f.y, f.x)
    everything = [arm] + [o for o in bpy.data.objects if o.type == "MESH" and (o.parent == arm or o.find_armature() == arm)]
    meshes = [o for o in skinned(arm) if o.name != weapon]
    pivot = mathutils.Matrix.Rotation(yaw, 4, "Z")
    for o in everything:
        if o.parent is None:
            o.matrix_world = pivot @ o.matrix_world
    bpy.ops.object.select_all(action="DESELECT")
    for o in everything:
        o.select_set(True)
    bpy.context.view_layer.objects.active = arm
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    lo = mathutils.Vector((1e9, 1e9, 1e9))
    hi = -lo
    for m in meshes:
        for v in m.data.vertices:
            w = m.matrix_world @ v.co
            lo = mathutils.Vector(map(min, lo, w))
            hi = mathutils.Vector(map(max, hi, w))
    s = height / max(hi.z - lo.z, 1e-4)
    shift = mathutils.Vector((-(lo.x + hi.x) * 0.5, -(lo.y + hi.y) * 0.5, -lo.z))
    for o in everything:
        if o.parent is None:
            o.matrix_world = mathutils.Matrix.Scale(s, 4) @ mathutils.Matrix.Translation(shift) @ o.matrix_world
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    log("  turned %.0f deg, scaled x%.3f (was %.2f m tall)" % (math.degrees(yaw), s, hi.z - lo.z))


def extract_weapon(name, arm, out_path):
    """The model's own weapon as a static GLB: grip at the origin, blade/haft along +Z, edge along +X."""
    w = bpy.data.objects.get(name)
    if w is None:
        log("  no weapon %s" % name)
        return None
    deps = bpy.context.evaluated_depsgraph_get()
    ev = w.evaluated_get(deps)
    pts = [w.matrix_world @ v.co for v in ev.to_mesh().vertices]
    ev.to_mesh_clear()
    centre = sum(pts, mathutils.Vector()) / len(pts)
    # Long axis by power iteration on the covariance.
    cov = [[sum((p[i] - centre[i]) * (p[j] - centre[j]) for p in pts) for j in range(3)] for i in range(3)]
    axis = mathutils.Vector((0.3, 0.5, 0.8))
    for _ in range(50):
        axis = mathutils.Vector([sum(cov[i][j] * axis[j] for j in range(3)) for i in range(3)]).normalized()
    ts = [(p - centre).dot(axis) for p in pts]
    lo, hi = min(ts), max(ts)
    # The head (blade, axe bit) is the end with the wider cross-section; the grip is at the other end.
    def spread(sel):
        return max(((p - centre) - axis * (p - centre).dot(axis)).length for p, t in zip(pts, ts) if sel(t))
    span = hi - lo
    head_hi = spread(lambda t: t > hi - span * 0.3) >= spread(lambda t: t < lo + span * 0.3)
    if not head_hi:
        axis, lo, hi = -axis, -hi, -lo
    grip = centre + axis * (lo + span * 0.1)
    # Edge: the widest direction across the head.
    head_pts = [p for p in pts if (p - centre).dot(axis) > hi - span * 0.3]
    far = max(head_pts, key=lambda p: ((p - grip) - axis * (p - grip).dot(axis)).length)
    edge = ((far - grip) - axis * (far - grip).dot(axis)).normalized()
    y = axis.cross(edge)
    basis = mathutils.Matrix((edge, y, axis)).transposed().to_4x4()
    basis.translation = grip
    to_local = basis.inverted()
    obj = bpy.data.objects.new("Weapon", w.data.copy())
    bpy.context.scene.collection.objects.link(obj)
    obj.matrix_world = to_local @ w.matrix_world
    obj.vertex_groups.clear()
    for m in list(obj.modifiers):
        obj.modifiers.remove(m)
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    bpy.ops.export_scene.gltf(filepath=out_path, export_format="GLB", use_selection=True, export_animations=False,
                              export_skins=False, export_yup=True)
    log("  weapon %s: %.2f m long -> %s" % (name, span, os.path.basename(out_path)))
    bpy.data.objects.remove(obj, do_unlink=True)
    bpy.data.objects.remove(w, do_unlink=True)
    return out_path


def halve_textures(limit=2048):
    for img in bpy.data.images:
        if img.size[0] > limit or img.size[1] > limit:
            f = limit / max(img.size[0], img.size[1])
            img.scale(int(img.size[0] * f), int(img.size[1] * f))
            img.pack()


def preview(name):
    scene = bpy.context.scene
    scene.render.engine = "BLENDER_WORKBENCH"
    scene.display.shading.color_type = "TEXTURE"
    scene.display.shading.light = "STUDIO"
    scene.render.resolution_x, scene.render.resolution_y = 900, 900
    scene.render.film_transparent = False
    world = bpy.data.worlds.new("w") if not scene.world else scene.world
    scene.world = world
    cam_data = bpy.data.cameras.new("cam")
    cam_data.type = "ORTHO"
    cam_data.ortho_scale = 4.4
    cam = bpy.data.objects.new("cam", cam_data)
    scene.collection.objects.link(cam)
    scene.camera = cam
    saved = os.path.join(ROOT, "Saved")
    os.makedirs(saved, exist_ok=True)
    for tag, loc, rot in (("front", (0, -6, 1.0), (math.radians(90), 0, 0)),
                          ("side", (6, 0, 1.0), (math.radians(90), 0, math.radians(90)))):
        cam.location, cam.rotation_euler = loc, rot
        scene.render.filepath = os.path.join(saved, "bestiary_%s_%s.png" % (name, tag))
        bpy.ops.render.render(write_still=True)


def convert(name, spec):
    log("%s <- %s" % (name, os.path.relpath(spec["src"], SRC)))
    reset()
    import_gltf(spec["src"])
    arm = bake_parents(list(bpy.data.objects))
    if arm.animation_data:
        arm.animation_data.action = None
    for a in list(bpy.data.actions):
        bpy.data.actions.remove(a)
    for pb in arm.pose.bones:
        pb.matrix_basis = mathutils.Matrix.Identity(4)
    if spec["rig"] == "quat":
        repair_quaternius(arm)
    else:
        rename_head(arm)
        graft(arm, spec["head"], {"Head", "neck_01"}, 0.6)
        if spec.get("hair"):
            graft(arm, os.path.join(HAIR, spec["hair"] + ".gltf"), {"Head", "neck_01"}, 0.01)
    folder = os.path.join(OUT, name)
    os.makedirs(folder, exist_ok=True)
    normalise(arm, spec["height"], spec.get("weapon"))
    weapon = None
    if spec.get("weapon"):
        weapon = extract_weapon(spec["weapon"], arm, os.path.join(folder, name + "_Weapon.glb"))
    for o in [o for o in bpy.data.objects if o.type == "MESH" and o.find_armature() != arm]:
        log("  dropping unskinned %s" % o.name)
        bpy.data.objects.remove(o, do_unlink=True)
    halve_textures()
    # One skinned mesh per character (UE's glTF import then makes exactly one SkeletalMesh).
    parts = skinned(arm)
    if len(parts) > 1:
        bpy.ops.object.select_all(action="DESELECT")
        for o in parts:
            o.select_set(True)
        bpy.context.view_layer.objects.active = parts[0]
        bpy.ops.object.join()
    skinned(arm)[0].name = name
    arm.name = "Armature"
    bpy.ops.object.select_all(action="DESELECT")
    for o in [arm] + skinned(arm):
        o.select_set(True)
    out = os.path.join(folder, name + ".glb")
    bpy.ops.export_scene.gltf(filepath=out, export_format="GLB", use_selection=True, export_animations=False,
                              export_skins=True, export_yup=True, export_image_format="JPEG",
                              export_jpeg_quality=90)
    bones = [b.name for b in arm.data.bones]
    log("  -> %s (%d bones; hands %s; %.1f MB)" % (os.path.relpath(out, ROOT), len(bones),
                                                  all(b in bones for b in ("hand_l", "hand_r", "foot_l", "foot_r", "head")),
                                                  os.path.getsize(out) / 1e6))
    if PREVIEW:
        preview(name)
    return weapon


def main():
    for name, spec in TYPES.items():
        if ONLY and name not in ONLY:
            continue
        convert(name, spec)
    log("CONVERT_BESTIARY_OK")


main()
