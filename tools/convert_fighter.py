"""Blender (4.4+) script: bake Quaternius's CC0 spaceships into the star fighter and its AI drones (UNR-T69).

  blender -b --factory-startup --disable-autoexec --python tools/convert_fighter.py [-- --preview]

Reads SourceArt/Environment/Fighter/Source/ (Ultimate Spaceships Pack, May 2021, see SourceArt/Environment/CREDITS.md):
  - Striker.blend + Striker_Blue.png -> the player's fighter, split into rigid parts so ASoulsFighter can animate them:
      Hull     everything but the cockpit glass, plus a cockpit tub and seat under the glass (UV'd onto dark/grey cells)
      Canopy   the cockpit glass (faces on the pack texture's black glass cell), pivot on its rear hinge so it tilts open
      GearN / GearL / GearR  added landing legs (strut, knee block, foot pad), pivot where each meets the belly so the
               code can fold them up into it
  - Bob.blend + Bob_Orange.png -> the drone fighter (one rigid mesh).
Both face +X with Z up, in centimetres (SCALE / DRONE_SCALE cm per pack unit), written to
SourceArt/Environment/Fighter/SM_Fighter_<Part>.obj / SM_Fighter_Drone.obj (+ .mtl) with Fighter_Texture.png and
Drone_Texture.png, and the hardpoints to fighter_rig.json (Unreal space, cm, the ship's origin) - the numbers in
Source/UnrealSoulslike/Souls/SoulsFighter.cpp (FighterRig) come from it.
UE's OBJ import keeps Z up and +X forward and mirrors Y, so a point (x, y, z) in Unreal space is written as (x, -y, z).
The pack models face -Y with +Z up, so their right side is -X: Unreal (x, y, z) = (-py, -px, pz) * scale.
--preview renders the assembled parts to Saved/fighter_preview_*.png.
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
SRC = os.path.join(ROOT, "SourceArt", "Environment", "Fighter", "Source")
OUT = os.path.join(ROOT, "SourceArt", "Environment", "Fighter")
PREVIEW = "--preview" in sys.argv
SCALE = 140.0        # cm per pack unit: the Striker comes out ~11.8 m long, ~7 m across
DRONE_SCALE = 85.0   # the drone (Bob) ~9 m across the wings, ~4.8 m long

# The cockpit glass: faces on this cell of Striker's texture, inside this box (pack space).
GLASS_UV = ((0.775, 0.55), (0.875, 0.88))
GLASS_BOX = ((-0.46, -1.75, -0.36), (0.46, 0.62, 0.75))
# Texture cells (Blender UV, v up) for the added hardware.
UV_DARK = ((0.80, 0.60), (0.85, 0.66))     # the black glass/cockpit cell: tub and pads
UV_PANEL = ((0.70, 0.30), (0.75, 0.36))    # mid-grey plating: struts, seat


def to_ue(p, scale=SCALE):
    """Blender pack space (forward -Y) -> Unreal space (cm, forward +X, right +Y)."""
    return (-p[1] * scale, -p[0] * scale, p[2] * scale)


def box(bm, uv, center, size, uv_rect):
    cx, cy, cz = center
    hx, hy, hz = (s * 0.5 for s in size)
    v = [bm.verts.new((cx + sx * hx, cy + sy * hy, cz + sz * hz)) for sx in (-1, 1) for sy in (-1, 1) for sz in (-1, 1)]
    idx = lambda sx, sy, sz: v[(sx > 0) * 4 + (sy > 0) * 2 + (sz > 0)]
    quads = (
        [idx(-1, -1, -1), idx(-1, -1, 1), idx(-1, 1, 1), idx(-1, 1, -1)],
        [idx(1, -1, -1), idx(1, 1, -1), idx(1, 1, 1), idx(1, -1, 1)],
        [idx(-1, -1, -1), idx(1, -1, -1), idx(1, -1, 1), idx(-1, -1, 1)],
        [idx(-1, 1, -1), idx(-1, 1, 1), idx(1, 1, 1), idx(1, 1, -1)],
        [idx(-1, -1, -1), idx(-1, 1, -1), idx(1, 1, -1), idx(1, -1, -1)],
        [idx(-1, -1, 1), idx(1, -1, 1), idx(1, 1, 1), idx(-1, 1, 1)],
    )
    (u0, v0), (u1, v1) = uv_rect
    for verts in quads:
        f = bm.faces.new(verts)
        for loop, (a, b) in zip(f.loops, ((u0, v0), (u1, v0), (u1, v1), (u0, v1))):
            loop[uv].uv = (a, b)


def tube(bm, uv, start, end, radius, uv_rect, sides=10):
    start, end = mathutils.Vector(start), mathutils.Vector(end)
    d = (end - start)
    length = d.length
    d.normalize()
    side = d.cross(mathutils.Vector((1, 0, 0)))
    if side.length < 1e-4:
        side = d.cross(mathutils.Vector((0, 1, 0)))
    side.normalize()
    up = side.cross(d).normalized()
    rings = []
    for t in (0.0, length):
        rings.append([bm.verts.new(start + d * t + (side * math.cos(2 * math.pi * i / sides) + up * math.sin(2 * math.pi * i / sides)) * radius)
                      for i in range(sides)])
    (u0, v0), (u1, v1) = uv_rect
    for i in range(sides):
        j = (i + 1) % sides
        f = bm.faces.new((rings[0][i], rings[0][j], rings[1][j], rings[1][i]))
        for loop, (a, b) in zip(f.loops, ((u0, v0), (u1, v0), (u1, v1), (u0, v1))):
            loop[uv].uv = (a, b)
    for ring, flip in ((rings[0], True), (rings[1], False)):
        f = bm.faces.new(list(reversed(ring)) if flip else ring)
        for loop in f.loops:
            loop[uv].uv = ((u0 + u1) * 0.5, (v0 + v1) * 0.5)


def load_ship(name):
    bpy.ops.wm.open_mainfile(filepath=os.path.join(SRC, name + ".blend"))
    ob = [o for o in bpy.context.scene.objects if o.type == "MESH"][0]
    dg = bpy.context.evaluated_depsgraph_get()
    ev = ob.evaluated_get(dg)
    mesh = ev.to_mesh().copy()
    ev.to_mesh_clear()
    mesh.transform(ob.matrix_world)
    return mesh


def split(mesh, pick):
    """Two bmeshes (picked faces, the rest) with their UVs."""
    uv_name = mesh.uv_layers.active.name
    src_uv = mesh.uv_layers.active.data
    out = []
    for _ in range(2):
        bm = bmesh.new()
        out.append((bm, bm.loops.layers.uv.new(uv_name), {}))
    for poly in mesh.polygons:
        bm, uv, vmap = out[0] if pick(mesh, poly, src_uv) else out[1]
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
    return out[0][:2], out[1][:2]


def write_part(bm, pivot, name, scale, mtl_name, texture):
    """Move the part about its pivot into Unreal space, export SM_<name>.obj + .mtl; returns the Blender object."""
    for v in bm.verts:
        p = v.co - mathutils.Vector(pivot)
        v.co = (-p.y * scale, p.x * scale, p.z * scale)  # Unreal (x, y, z) = (-py, -px, pz) * S, written (x, -y, z)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    mat = bpy.data.materials.get(mtl_name) or bpy.data.materials.new(mtl_name)
    me.materials.append(mat)
    obj = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(obj)
    for o in bpy.context.scene.objects:
        o.select_set(o == obj)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.wm.obj_export(filepath=os.path.join(OUT, name + ".obj"), export_selected_objects=True, forward_axis="Y", up_axis="Z",
                          global_scale=1.0, export_materials=True, path_mode="STRIP", export_uv=True, export_normals=True,
                          export_triangulated_mesh=True, apply_modifiers=False, export_animation=False)
    with open(os.path.join(OUT, name + ".mtl"), "w", encoding="utf-8", newline="\n") as f:
        f.write("# Quaternius Ultimate Spaceships Pack (CC0), baked by tools/convert_fighter.py\nnewmtl %s\nKd 1 1 1\nmap_Kd %s\n" % (mtl_name, texture))
    d = obj.dimensions
    print("FIGHTER part %-18s size=(%5.0f, %5.0f, %5.0f) tris=%d" % (name, d.x, d.y, d.z, sum(len(p.vertices) - 2 for p in me.polygons)))
    return obj


def lowest_near(mesh, x, y, r=0.3):
    zs = [v.co.z for v in mesh.vertices if abs(v.co.x - x) < r and abs(v.co.y - y) < r]
    return min(zs) if zs else None


def bake_fighter():
    mesh = load_ship("Striker")
    (lo, hi) = GLASS_BOX

    def is_glass(m, poly, src_uv):
        c = poly.center
        if not all(lo[i] <= c[i] <= hi[i] for i in range(3)):
            return False
        u = sum(src_uv[li].uv[0] for li in poly.loop_indices) / poly.loop_total
        v = sum(src_uv[li].uv[1] for li in poly.loop_indices) / poly.loop_total
        return GLASS_UV[0][0] <= u <= GLASS_UV[1][0] and GLASS_UV[0][1] <= v <= GLASS_UV[1][1]

    (glass_bm, glass_uv), (hull_bm, hull_uv) = split(mesh, is_glass)
    gverts = [v.co.copy() for v in glass_bm.verts]
    if not gverts:
        raise RuntimeError("no cockpit glass faces found (GLASS_UV / GLASS_BOX)")
    gmin = mathutils.Vector([min(v[i] for v in gverts) for i in range(3)])
    gmax = mathutils.Vector([max(v[i] for v in gverts) for i in range(3)])
    print("FIGHTER glass faces=%d min=%s max=%s" % (len(glass_bm.faces), tuple(round(c, 2) for c in gmin), tuple(round(c, 2) for c in gmax)))
    # The tub under the glass, and a seat in it.
    tub_c = ((gmin.x + gmax.x) * 0.5, (gmin.y + gmax.y) * 0.5, gmin.z + 0.02)
    box(hull_bm, hull_uv, tub_c, ((gmax.x - gmin.x) * 0.9, (gmax.y - gmin.y) * 0.92, 0.08), UV_DARK)
    seat = mathutils.Vector((0.0, gmin.y + (gmax.y - gmin.y) * 0.62, gmin.z + 0.05))
    box(hull_bm, hull_uv, (seat.x, seat.y + 0.14, seat.z + 0.32), (0.36, 0.1, 0.6), UV_PANEL)   # back rest (behind: +Y)
    box(hull_bm, hull_uv, (seat.x, seat.y, seat.z + 0.08), (0.36, 0.34, 0.12), UV_PANEL)          # cushion
    # Hinge: the glass's rear top edge.
    hinge = mathutils.Vector((0.0, gmax.y, max(v.z for v in gverts if v.y > gmax.y - 0.15)))

    # Landing legs, folding up into the belly.
    ground = min(v.co.z for v in mesh.vertices) - 0.32
    legs = {}
    for name, (x, y) in (("GearN", (0.0, -2.2)), ("GearL", (0.95, 0.9)), ("GearR", (-0.95, 0.9))):
        top = lowest_near(mesh, x, y)
        if top is None:
            top = lowest_near(mesh, x, y, 0.6)
        top = (top if top is not None else -0.5) + 0.06
        bm = bmesh.new()
        uv = bm.loops.layers.uv.new("UVMap")
        foot = (x, y, ground + 0.05)
        tube(bm, uv, (x, y, top), (x, y, ground + 0.12), 0.055, UV_PANEL)
        box(bm, uv, (x, y, (top + ground) * 0.5 + 0.08), (0.16, 0.2, 0.14), UV_PANEL)    # the knee block
        box(bm, uv, foot, (0.3, 0.42 if name == "GearN" else 0.5, 0.08), UV_DARK)       # the pad
        legs[name] = (bm, mathutils.Vector((x, y, top)))

    # Hardpoints (pack space).
    def extreme_front(pred):
        vs = [v.co for v in mesh.vertices if pred(v.co)]
        return min(vs, key=lambda c: c.y) if vs else mathutils.Vector((0, -4, 0))

    muzzles = [extreme_front(lambda c: c.x > 0.8), extreme_front(lambda c: c.x < -0.8)]  # left (+X), then right (-X)
    muzzles = [m + mathutils.Vector((0, -0.12, 0)) for m in muzzles]
    engines = [mathutils.Vector((0.34, 2.56, 0.12)), mathutils.Vector((-0.34, 2.56, 0.12)),
               mathutils.Vector((1.27, 1.7, -0.12)), mathutils.Vector((-1.27, 1.7, -0.12))]
    mn = [min(v.co[i] for v in mesh.vertices) for i in range(3)]
    mx = [max(v.co[i] for v in mesh.vertices) for i in range(3)]

    shutil.copyfile(os.path.join(SRC, "Striker_Blue.png"), os.path.join(OUT, "Fighter_Texture.png"))
    objects = {
        "Hull": write_part(hull_bm, (0, 0, 0), "SM_Fighter_Hull", SCALE, "Fighter", "Fighter_Texture.png"),
        "Canopy": write_part(glass_bm, hinge, "SM_Fighter_Canopy", SCALE, "Fighter", "Fighter_Texture.png"),
    }
    for name, (bm, pivot) in legs.items():
        objects[name] = write_part(bm, pivot, "SM_Fighter_" + name, SCALE, "Fighter", "Fighter_Texture.png")

    def cm(p):
        return [round(c, 1) for c in to_ue(p)]

    rig = {
        "scale_cm_per_unit": SCALE,
        "canopy_hinge": cm(hinge),
        "gear": {name: cm(pivot) for name, (_, pivot) in legs.items()},
        "gear_floor": round(ground * SCALE, 1),
        "muzzles": [cm(m) for m in muzzles],
        "engines": [cm(e) for e in engines],
        "seat": cm(seat),
        "bounds_min": [round(min(a, b), 1) for a, b in zip(to_ue(mn), to_ue(mx))],
        "bounds_max": [round(max(a, b), 1) for a, b in zip(to_ue(mn), to_ue(mx))],
    }
    mesh_name = mesh.name
    bpy.data.meshes.remove(bpy.data.meshes[mesh_name])
    return objects, rig


def bake_drone():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    mesh = load_ship("Bob")
    (_, _), (bm, _) = split(mesh, lambda m, p, uv: False)
    mn = [min(v.co[i] for v in mesh.vertices) for i in range(3)]
    mx = [max(v.co[i] for v in mesh.vertices) for i in range(3)]

    def extreme_front(pred):
        vs = [v.co for v in mesh.vertices if pred(v.co)]
        return min(vs, key=lambda c: c.y)

    muzzles = [extreme_front(lambda c: 0.3 < c.x < 1.5), extreme_front(lambda c: -1.5 < c.x < -0.3)]
    engine = mathutils.Vector((0.0, mx[1], (mn[2] + mx[2]) * 0.5))
    shutil.copyfile(os.path.join(SRC, "Bob_Orange.png"), os.path.join(OUT, "Drone_Texture.png"))
    obj = write_part(bm, (0, 0, 0), "SM_Fighter_Drone", DRONE_SCALE, "Drone", "Drone_Texture.png")
    rig = {
        "scale_cm_per_unit": DRONE_SCALE,
        "muzzles": [[round(c, 1) for c in to_ue(m, DRONE_SCALE)] for m in muzzles],
        "engine": [round(c, 1) for c in to_ue(engine, DRONE_SCALE)],
        "bounds_min": [round(min(a, b), 1) for a, b in zip(to_ue(mn, DRONE_SCALE), to_ue(mx, DRONE_SCALE))],
        "bounds_max": [round(max(a, b), 1) for a, b in zip(to_ue(mn, DRONE_SCALE), to_ue(mx, DRONE_SCALE))],
    }
    return obj, rig


def preview(rig):
    """Re-import the written OBJs, assemble them at their pivots (canopy half open) and render."""
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    placed = {}
    for name in ("Hull", "Canopy", "GearN", "GearL", "GearR", "Drone"):
        bpy.ops.wm.obj_import(filepath=os.path.join(OUT, "SM_Fighter_%s.obj" % name), forward_axis="Y", up_axis="Z")
        placed[name] = bpy.context.selected_objects[0]
    for name, obj in placed.items():
        if name == "Canopy":
            x, y, z = rig["fighter"]["canopy_hinge"]
            obj.location = (x, -y, z)
            obj.rotation_euler = (0, math.radians(-35), 0)
        elif name.startswith("Gear"):
            x, y, z = rig["fighter"]["gear"][name]
            obj.location = (x, -y, z)
        elif name == "Drone":
            obj.location = (0, -1400, 0)
    for img_name, mat_name in (("Fighter_Texture.png", "Fighter"), ("Drone_Texture.png", "Drone")):
        mat = bpy.data.materials.get(mat_name)
        if mat is None:
            continue
        mat.use_nodes = True
        tex = mat.node_tree.nodes.new("ShaderNodeTexImage")
        tex.image = bpy.data.images.load(os.path.join(OUT, img_name))
        bsdf = mat.node_tree.nodes.get("Principled BSDF")
        if bsdf:
            mat.node_tree.links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
    cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
    scene.collection.objects.link(cam)
    scene.camera = cam
    cam.data.clip_end = 100000
    sun = bpy.data.objects.new("sun", bpy.data.lights.new("sun", "SUN"))
    sun.rotation_euler = (0.7, 0.2, 0.9)
    scene.collection.objects.link(sun)
    scene.render.engine = "BLENDER_WORKBENCH"
    scene.display.shading.color_type = "TEXTURE"
    scene.render.resolution_x, scene.render.resolution_y = 900, 600
    os.makedirs(os.path.join(ROOT, "Saved"), exist_ok=True)
    floor = rig["fighter"]["gear_floor"]
    for view, loc, target in (("front", (1500, 1100, 300), (0, -500, floor + 150)), ("side", (0, 1900, floor + 200), (0, -400, floor + 150)),
                              ("back", (-1500, -900, 700), (0, -500, 0))):
        cam.location = loc
        cam.rotation_euler = (mathutils.Vector(target) - mathutils.Vector(loc)).to_track_quat("-Z", "Y").to_euler()
        scene.render.filepath = os.path.join(ROOT, "Saved", "fighter_preview_%s.png" % view)
        bpy.ops.render.render(write_still=True)


def main():
    _, fighter_rig = bake_fighter()
    _, drone_rig = bake_drone()
    rig = {"fighter": fighter_rig, "drone": drone_rig}
    with open(os.path.join(OUT, "fighter_rig.json"), "w", encoding="utf-8", newline="\n") as f:
        json.dump(rig, f, indent=2)
        f.write("\n")
    print("FIGHTER rig: %s" % json.dumps(rig))
    if PREVIEW:
        preview(rig)
    print("FIGHTER_OK")


main()
