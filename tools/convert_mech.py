"""Blender (4.4+) script: bake Quaternius's CC0 "George" mech into rigid static-mesh parts for the walking mech (UNR-T68).

  blender -b --factory-startup --disable-autoexec --python tools/convert_mech.py [-- --preview]

Reads SourceArt/Environment/Mech/Source/George.blend (Animated Mech Pack, see SourceArt/Environment/CREDITS.md).
The model is skinned; ASoulsMech animates it procedurally instead, as a hierarchy of rigid parts, so the legs
can walk under a torso that twists on its own. The script:
  - poses the arms from the pack's T-pose into gun arms (upper arm hanging, forearm pointing forward),
  - adds an autocannon barrel under each forearm and a missile pod on each shoulder (UV'd onto grey/black cells of
    the pack's own texture),
  - splits the mesh into parts by each face's dominant bone (pelvis, torso, canopy, two arms, and per leg the thigh,
    shin, metatarsal and foot of the bird-legged three-segment leg),
  - turns it to face +X with Z up, scales it to centimetres (SCALE cm per pack unit: ~7.2 m to the antenna tips),
  - writes each part to SourceArt/Environment/Mech/SM_Mech_<Part>.obj (+ .mtl) with its pivot (its joint) at the
    origin, the texture as Mech_Texture.png, and the joint positions to mech_rig.json (Unreal space, cm, origin on
    the ground under the hips) - the numbers in Source/UnrealSoulslike/Souls/SoulsMech.cpp (MechRig) come from it.
UE's OBJ import keeps Z up and +X forward and mirrors Y, so a point (x, y, z) in Unreal space is written as
(x, -y, z). --preview renders the assembled parts to Saved/mech_preview_*.png.
"""
import json
import math
import os
import shutil
import sys

import bpy
import bmesh
import mathutils

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SRC = os.path.join(ROOT, "SourceArt", "Environment", "Mech", "Source")
OUT = os.path.join(ROOT, "SourceArt", "Environment", "Mech")
PREVIEW = "--preview" in sys.argv
SCALE = 110.0  # cm per pack unit

# Part -> the vertex groups (bones) whose faces it takes. Fingers and palms go with their arm.
PARTS = {
    "Pelvis": ("Torso", "Body", "PoleTarget.L", "PoleTarget.R"),
    "Torso": ("Chest", "Neck", "Shoulder.L", "Shoulder.R"),
    "Canopy": ("Head",),
    "ArmL": ("UpperArm.L", "LowerArm.L"),
    "ArmR": ("UpperArm.R", "LowerArm.R"),
    "ThighL": ("UpperLeg.L",), "ShinL": ("MidLeg.L",), "MetaL": ("LowerLeg.L",), "FootL": ("Foot.L", "FootBack.L"),
    "ThighR": ("UpperLeg.R",), "ShinR": ("MidLeg.R",), "MetaR": ("LowerLeg.R",), "FootR": ("Foot.R", "FootBack.R"),
}
HAND_WORDS = ("Palm", "Pinky", "Ring", "Index", "Thumb")

# Texture cells (Blender UV, v up) for the added hardware.
UV_BARREL = ((0.708, 0.595), (0.733, 0.685))   # the vertical grey ribs: wrap round the barrel
UV_DARK = ((0.29, 0.67), (0.35, 0.695))         # the black band: pod faces and the bore
UV_PANEL = ((0.345, 0.515), (0.395, 0.57))      # plain mid-grey plating


def part_of(group):
    for part, groups in PARTS.items():
        if group in groups:
            return part
    if any(w in group for w in HAND_WORDS):
        return "ArmL" if group.endswith(".L") else "ArmR"
    return "Torso"


def pose_arms(arm):
    """T-pose -> gun arms: upper arm down and a little out and forward, forearm level and pointing forward (-Y)."""
    bpy.context.view_layer.objects.active = arm
    bpy.ops.object.mode_set(mode="POSE")
    for side, sx in (("L", 1.0), ("R", -1.0)):
        for bone, want in ((f"UpperArm.{side}", (0.40 * sx, -0.22, -0.89)), (f"LowerArm.{side}", (-0.06 * sx, -1.0, 0.03))):
            pb = arm.pose.bones[bone]
            rest = pb.bone.matrix_local
            rest_dir = (pb.bone.tail_local - pb.bone.head_local).normalized()
            delta = rest_dir.rotation_difference(mathutils.Vector(want).normalized())
            head = pb.head.copy()  # posed head (follows the parent)
            m = delta.to_matrix().to_4x4() @ rest.to_3x3().to_4x4()
            m.translation = head
            pb.matrix = m
            bpy.context.view_layer.update()
    bpy.ops.object.mode_set(mode="OBJECT")


def box(bm, uv, center, size, uv_rect, front_rect=None, front_axis=None):
    """An axis-aligned box (Blender space); front_axis (e.g. '-y') gets front_rect UVs."""
    cx, cy, cz = center
    hx, hy, hz = (s * 0.5 for s in size)
    v = [bm.verts.new((cx + sx * hx, cy + sy * hy, cz + sz * hz)) for sx in (-1, 1) for sy in (-1, 1) for sz in (-1, 1)]
    idx = lambda sx, sy, sz: v[(sx > 0) * 4 + (sy > 0) * 2 + (sz > 0)]
    faces = {
        "-x": [idx(-1, -1, -1), idx(-1, -1, 1), idx(-1, 1, 1), idx(-1, 1, -1)],
        "+x": [idx(1, -1, -1), idx(1, 1, -1), idx(1, 1, 1), idx(1, -1, 1)],
        "-y": [idx(-1, -1, -1), idx(1, -1, -1), idx(1, -1, 1), idx(-1, -1, 1)],
        "+y": [idx(-1, 1, -1), idx(-1, 1, 1), idx(1, 1, 1), idx(1, 1, -1)],
        "-z": [idx(-1, -1, -1), idx(-1, 1, -1), idx(1, 1, -1), idx(1, -1, -1)],
        "+z": [idx(-1, -1, 1), idx(1, -1, 1), idx(1, 1, 1), idx(-1, 1, 1)],
    }
    out = []
    for key, verts in faces.items():
        f = bm.faces.new(verts)
        (u0, v0), (u1, v1) = front_rect if (front_axis == key and front_rect) else uv_rect
        for loop, (a, b) in zip(f.loops, ((u0, v0), (u1, v0), (u1, v1), (u0, v1))):
            loop[uv].uv = (a, b)
        out.append(f)
    return out


def tube(bm, uv, start, direction, length, radius, sides=12):
    """A capped cylinder from start along direction; the sides wrap UV_BARREL, the caps UV_DARK (the bore)."""
    d = mathutils.Vector(direction).normalized()
    side = d.cross(mathutils.Vector((0, 0, 1)))
    if side.length < 1e-4:
        side = d.cross(mathutils.Vector((1, 0, 0)))
    side.normalize()
    up = side.cross(d).normalized()
    start = mathutils.Vector(start)
    rings = []
    for t in (0.0, length):
        ring = []
        for i in range(sides):
            a = 2 * math.pi * i / sides
            ring.append(bm.verts.new(start + d * t + (side * math.cos(a) + up * math.sin(a)) * radius))
        rings.append(ring)
    (u0, v0), (u1, v1) = UV_BARREL
    for i in range(sides):
        j = (i + 1) % sides
        f = bm.faces.new((rings[0][i], rings[0][j], rings[1][j], rings[1][i]))
        for loop, (a, b) in zip(f.loops, ((u0 + (u1 - u0) * i / sides, v0), (u0 + (u1 - u0) * (i + 1) / sides, v0),
                                          (u0 + (u1 - u0) * (i + 1) / sides, v1), (u0 + (u1 - u0) * i / sides, v1))):
            loop[uv].uv = (a, b)
    (du0, dv0), (du1, dv1) = UV_DARK
    for ring, flip in ((rings[0], True), (rings[1], False)):
        f = bm.faces.new(list(reversed(ring)) if flip else ring)
        for loop in f.loops:
            loop[uv].uv = ((du0 + du1) * 0.5, (dv0 + dv1) * 0.5)


def to_ue(p):
    """Blender pack space (m, forward -Y) -> Unreal space (cm, forward +X, right +Y)."""
    return (-p[1] * SCALE, -p[0] * SCALE, p[2] * SCALE)


def bake():
    bpy.ops.wm.open_mainfile(filepath=os.path.join(SRC, "George.blend"))
    arm = bpy.data.objects["RobotArmature"]
    body = bpy.data.objects["George"]
    if arm.animation_data:
        arm.animation_data.action = None
    for pb in arm.pose.bones:
        pb.location = (0, 0, 0)
        pb.rotation_mode = "QUATERNION"
        pb.rotation_quaternion = (1, 0, 0, 0)
        pb.scale = (1, 1, 1)
    bpy.context.view_layer.update()
    pose_arms(arm)
    bones = {pb.name: (pb.head.copy(), pb.tail.copy()) for pb in arm.pose.bones}

    dg = bpy.context.evaluated_depsgraph_get()
    ev = body.evaluated_get(dg)
    mesh = ev.to_mesh()
    mesh.transform(body.matrix_world)
    names = [g.name for g in body.vertex_groups]
    src_verts = body.data.vertices
    assert len(mesh.vertices) == len(src_verts), "the modifiers changed the vertex count"
    dominant = []
    for v in src_verts:
        g = max(v.groups, key=lambda g: g.weight) if v.groups else None
        dominant.append(part_of(names[g.group]) if g else "Torso")

    # Pivots (pack space, posed).
    hip_c = (bones["UpperLeg.L"][0] + bones["UpperLeg.R"][0]) * 0.5
    pivots = {
        "Pelvis": hip_c,
        "Torso": mathutils.Vector((0.0, hip_c.y, bones["Chest"][0].z - 0.15)),
        "Canopy": bones["Head"][0],
        "ArmL": bones["UpperArm.L"][0], "ArmR": bones["UpperArm.R"][0],
    }
    for s in ("L", "R"):
        pivots["Thigh" + s] = bones["UpperLeg." + s][0]
        pivots["Shin" + s] = bones["MidLeg." + s][0]
        pivots["Meta" + s] = bones["LowerLeg." + s][0]
        pivots["Foot" + s] = bones["LowerLeg." + s][1]

    # One bmesh per part, faces by majority vote of their vertices' parts.
    uv_name = mesh.uv_layers.active.name
    parts = {}
    for name in PARTS:
        bm = bmesh.new()
        parts[name] = (bm, bm.loops.layers.uv.new(uv_name), {})
    src_uv = mesh.uv_layers.active.data
    for poly in mesh.polygons:
        votes = {}
        for vi in poly.vertices:
            votes[dominant[vi]] = votes.get(dominant[vi], 0) + 1
        part = max(votes, key=votes.get)
        bm, uv, vmap = parts[part]
        vs = []
        for vi in poly.vertices:
            if vi not in vmap:
                vmap[vi] = bm.verts.new(mesh.vertices[vi].co)
            vs.append(vmap[vi])
        try:
            f = bm.faces.new(vs)
        except ValueError:
            continue
        for loop, li in zip(f.loops, poly.loop_indices):
            loop[uv].uv = src_uv[li].uv
    ev.to_mesh_clear()

    # Gun arms: an autocannon under each forearm, its breech a block of plating.
    muzzles = {}
    for s in ("L", "R"):
        e, h = bones["LowerArm." + s]
        d = (h - e).normalized()
        down = mathutils.Vector((0, 0, -1))
        side = d.cross(mathutils.Vector((0, 0, 1))).normalized()
        under = (down - d * down.dot(d)).normalized()
        start = e + d * 0.35 + under * 0.36
        bm, uv, _ = parts["Arm" + s]
        box(bm, uv, start + d * 0.25, (0.36, 0.5, 0.3), UV_PANEL)  # roughly aligned: the forearm points along -Y
        tube(bm, uv, start + d * 0.45, d, 1.25, 0.075)
        tube(bm, uv, start + d * 1.55, d, 0.22, 0.11)  # muzzle brake
        muzzles[s] = start + d * 1.8
    # Missile pods on the shoulders, two-by-two tubes facing forward.
    launchers = []
    for s, sx in (("L", 1.0), ("R", -1.0)):
        c = mathutils.Vector((0.84 * sx, 0.3, 5.16))
        bm, uv, _ = parts["Torso"]
        box(bm, uv, c, (0.56, 0.9, 0.46), UV_PANEL, UV_DARK, "-y")
        for ox in (-0.13, 0.13):
            for oz in (-0.1, 0.1):
                launchers.append(c + mathutils.Vector((ox, -0.47, oz)))
    seat = bones["Head"][0] + mathutils.Vector((0, 0.05, -0.15))

    # Export each part about its pivot in Unreal space.
    with open(os.path.join(OUT, "Mech_Texture.png"), "wb") as f:
        f.write(open(os.path.join(SRC, "George_Texture.png"), "rb").read())
    mtl = "# Quaternius Animated Mech Pack 'George' (CC0), baked by tools/convert_mech.py\nnewmtl Mech\nKd 1 1 1\nmap_Kd Mech_Texture.png\n"
    objects = {}
    for name, (bm, uv, _) in parts.items():
        piv = pivots[name]
        for v in bm.verts:
            p = v.co - piv
            v.co = (-p.y * SCALE, p.x * SCALE, p.z * SCALE)  # Unreal (x, y, z) = (-py, -px, pz) * S, written (x, -y, z)
        bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
        me = bpy.data.meshes.new("SM_Mech_" + name)
        bm.to_mesh(me)
        bm.free()
        mat = bpy.data.materials.get("Mech") or bpy.data.materials.new("Mech")
        me.materials.append(mat)
        obj = bpy.data.objects.new("SM_Mech_" + name, me)
        bpy.context.scene.collection.objects.link(obj)
        objects[name] = obj
    for name, obj in objects.items():
        for o in bpy.context.scene.objects:
            o.select_set(o == obj)
        bpy.context.view_layer.objects.active = obj
        path = os.path.join(OUT, "SM_Mech_%s.obj" % name)
        bpy.ops.wm.obj_export(filepath=path, export_selected_objects=True, forward_axis="Y", up_axis="Z", global_scale=1.0,
                              export_materials=True, path_mode="STRIP", export_uv=True, export_normals=True,
                              export_triangulated_mesh=True, apply_modifiers=False, export_animation=False)
        with open(os.path.join(OUT, "SM_Mech_%s.mtl" % name), "w", encoding="utf-8", newline="\n") as f:
            f.write(mtl)
        d = obj.dimensions
        print("MECH part %-7s pivot(ue)=(%7.1f, %7.1f, %7.1f) size=(%4.0f, %4.0f, %4.0f) tris=%d" % (
            name, *to_ue(pivots[name]), d.x, d.y, d.z, sum(len(p.vertices) - 2 for p in obj.data.polygons)))

    def rel(p, part):
        a, b = to_ue(p), to_ue(pivots[part])
        return [round(a[i] - b[i], 1) for i in range(3)]

    foot_floor = {s: min((v.co.z for v in objects["Foot" + s].data.vertices), default=0.0) for s in ("L", "R")}
    rig = {
        "scale_cm_per_unit": SCALE,
        "pivots": {k: [round(c, 1) for c in to_ue(v)] for k, v in pivots.items()},
        "foot_floor": {k: round(v, 1) for k, v in foot_floor.items()},
        "muzzles_in_arm": {s: rel(muzzles[s], "Arm" + s) for s in ("L", "R")},
        "launchers_in_torso": [rel(p, "Torso") for p in launchers],
        "seat_in_torso": rel(seat, "Torso"),
    }
    with open(os.path.join(OUT, "mech_rig.json"), "w", encoding="utf-8", newline="\n") as f:
        json.dump(rig, f, indent=2)
        f.write("\n")
    print("MECH rig: %s" % json.dumps(rig))
    return objects, rig


def preview(objects, rig):
    """Assemble the parts at their pivots and render a 3/4 front and a side view."""
    scene = bpy.context.scene
    for name, obj in objects.items():
        x, y, z = rig["pivots"][name]
        obj.location = (x, -y, z)
    for o in list(scene.objects):
        if o.name not in [ob.name for ob in objects.values()]:
            bpy.data.objects.remove(o)
    img = bpy.data.images.load(os.path.join(OUT, "Mech_Texture.png"))
    mat = bpy.data.materials["Mech"]
    mat.use_nodes = True
    tex = mat.node_tree.nodes.new("ShaderNodeTexImage")
    tex.image = img
    mat.node_tree.links.new(tex.outputs["Color"], mat.node_tree.nodes["Principled BSDF"].inputs["Base Color"])
    cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
    scene.collection.objects.link(cam)
    scene.camera = cam
    cam.data.clip_end = 100000
    sun = bpy.data.objects.new("sun", bpy.data.lights.new("sun", "SUN"))
    sun.rotation_euler = (0.7, 0.2, 0.9)
    scene.collection.objects.link(sun)
    scene.render.engine = "BLENDER_WORKBENCH"
    scene.display.shading.color_type = "TEXTURE"
    scene.render.resolution_x = scene.render.resolution_y = 700
    os.makedirs(os.path.join(ROOT, "Saved"), exist_ok=True)
    for view, loc in (("front", (1300, -700, 520)), ("side", (0, -1500, 380)), ("back", (-1100, 650, 700))):
        cam.location = loc
        cam.rotation_euler = (mathutils.Vector((0, 0, 360)) - mathutils.Vector(loc)).to_track_quat("-Z", "Y").to_euler()
        scene.render.filepath = os.path.join(ROOT, "Saved", "mech_preview_%s.png" % view)
        bpy.ops.render.render(write_still=True)


def main():
    objects, rig = bake()
    if PREVIEW:
        preview(objects, rig)
    print("MECH_OK")


main()
