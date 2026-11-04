"""Blender (4.4+) script: bake Kenney's CC0 Pirate Kit ships into wrecked static OBJ meshes for tools/build_island.py.

  blender -b --factory-startup --disable-autoexec --python tools/convert_ships.py [-- --preview]

Reads SourceArt/Environment/Ships/Source (see SourceArt/Environment/CREDITS.md). For each output it joins
the model's parts, turns it to face +X (bow) with Z up, scales it to life size in centimetres (a galleon is
~40 m long), wrecks it (tattered sails, flags struck, optionally broken in two at a ragged cut into a bow or a
stern piece), puts its bottom-centre at the origin and writes SourceArt/Environment/Ships/SM_<Name>.obj (+ .mtl).
Faces are split into material slots by part: Wood (hull, masts, rigging, trim; the kit's palette texture),
Cloth (sails) and Weed (the grass tufts growing on the kit's own wreck). UE's OBJ import keeps Z up and +X
forward (it only mirrors Y). --preview renders a side and a top view of each to Saved/ships_<name>.png.
"""
import math
import os
import sys

import bpy
import bmesh
import mathutils

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SRC = os.path.join(ROOT, "SourceArt", "Environment", "Ships", "Source")
OUT = os.path.join(ROOT, "SourceArt", "Environment", "Ships")
PREVIEW = "--preview" in sys.argv

# The kit's ships lie along Y with the bow (bowsprit) at -Y and the stern castle at +Y; BOW_YAW turns -Y to +X.
BOW_YAW = 90.0
GALLEON = 4200.0 / 13.1  # cm per kit unit: the large ship (13.1 units long) becomes a 42 m galleon

# name: (source, scale (cm per unit) or ("length", cm), yaw to face +X, wreck: None | "whole" | "bow" | "stern",
#        drop object-name prefixes, beam squeeze (Y scale factor))
SHIPS = {
    "Wreck_Large": ("ship-large.glb", GALLEON, BOW_YAW, "whole", ("flag",), 0.82),
    "Wreck_Medium": ("ship-medium.glb", ("length", 3500.0), BOW_YAW, "whole", ("flag",), 0.82),
    "Wreck_Small": ("ship-small.glb", ("length", 2700.0), BOW_YAW, "whole", ("flag",), 0.85),
    "Wreck_Ghost": ("ship-ghost.glb", ("length", 3800.0), BOW_YAW, "whole", ("flag",), 0.82),
    "Wreck_Hulk": ("ship-wreck.glb", ("length", 3800.0), BOW_YAW, "whole", ("flag",), 0.82),
    "Wreck_Large_Bow": ("ship-large.glb", GALLEON, BOW_YAW, "bow", ("flag",), 0.82),
    "Wreck_Large_Stern": ("ship-large.glb", GALLEON, BOW_YAW, "stern", ("flag",), 0.82),
    "Wreck_Medium_Bow": ("ship-medium.glb", ("length", 3500.0), BOW_YAW, "bow", ("flag",), 0.82),
    "Wreck_Medium_Stern": ("ship-medium.glb", ("length", 3500.0), BOW_YAW, "stern", ("flag",), 0.82),
    "Wreck_Mast": ("mast-ropes.glb", GALLEON, 0.0, "whole", ("flag",), 1.0),
    # The landing boat: hull only (the kit's shipped oars are dropped; one loose oar lies beside it).
    "Rowboat": ("boat-row-large.glb", ("length", 390.0), BOW_YAW, None, ("paddles",), 1.0),
    "Oar": ("tool-paddle.glb", ("length", 250.0), None, None, (), 1.0),
    "Barrel": ("barrel.glb", 72.0, 0.0, None, (), 1.0),
    "Crate": ("crate.glb", 95.0, 0.0, None, (), 1.0),
    "CrateBottles": ("crate-bottles.glb", 95.0, 0.0, None, (), 1.0),
    "Planks": ("platform-planks.glb", 105.0, 0.0, None, (), 1.0),
}
SLOTS = ("Wood", "Cloth", "Weed")


def load(src):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=os.path.join(SRC, src))


def slot_of(obj_name):
    n = obj_name.lower()
    if n.startswith(("sail", "flag")):
        return 1
    if n.startswith("grass"):
        return 2
    return 0


def join(drop):
    """One mesh in world space with Wood/Cloth/Weed slots by source part; dropped parts left out."""
    dg = bpy.context.evaluated_depsgraph_get()
    bm = bmesh.new()
    kept = []
    for o in [o for o in bpy.context.scene.objects if o.type == "MESH"]:
        if any(o.name.lower().startswith(p) for p in drop):
            continue
        kept.append(o.name)
        ev = o.evaluated_get(dg)
        me = ev.to_mesh()
        me.transform(o.matrix_world)
        offset = len(bm.faces)
        bm.from_mesh(me)
        bm.faces.ensure_lookup_table()
        for f in bm.faces[offset:]:
            f.material_index = slot_of(o.name)
        ev.to_mesh_clear()
    print("SHIP parts: %s" % kept)
    return bm


def long_axis(bm):
    xs = [v.co.x for v in bm.verts]
    ys = [v.co.y for v in bm.verts]
    return "x" if max(xs) - min(xs) >= max(ys) - min(ys) else "y"


def noise(x, y, z, seed):
    """Cheap deterministic value in [-1, 1]."""
    return math.sin(x * 12.9898 + y * 78.233 + z * 37.719 + seed * 4.1) * 0.5 + \
        math.sin(x * 3.1 - y * 2.3 + z * 5.7 + seed) * 0.5


def tatter(bm, seed):
    """Rot the sails: subdivide the cloth and drop about half of it, more towards the foot and the edges."""
    cloth = [f for f in bm.faces if f.material_index == 1]
    if not cloth:
        return
    edges = list({e for f in cloth for e in f.edges})
    bmesh.ops.subdivide_edges(bm, edges=edges, cuts=5, use_grid_fill=True)
    bm.faces.ensure_lookup_table()
    cloth = [f for f in bm.faces if f.material_index == 1]
    zs = [v.co.z for f in cloth for v in f.verts]
    z0, z1 = min(zs), max(zs)
    doomed = []
    for f in cloth:
        c = f.calc_center_median()
        low = 1.0 - (c.z - z0) / max(1e-6, z1 - z0)  # 1 at the foot of the lowest sail
        n = noise(c.x * 0.9, c.y * 0.9, c.z * 1.4, seed) * 0.5 + 0.5
        if n < 0.22 + 0.3 * low:
            doomed.append(f)
    bmesh.ops.delete(bm, geom=doomed, context="FACES")
    bmesh.ops.delete(bm, geom=[v for v in bm.verts if not v.link_faces], context="VERTS")


def break_ship(bm, keep, seed):
    """Break the hull in two at a ragged cut a little aft of midships; keep the bow (+X) or the stern part.
    The low-poly hull is first sliced into small cells around the cut, so dropping whole cells leaves a ragged,
    splintered edge instead of long slivers of the kit's big faces."""
    xs = [v.co.x for v in bm.verts]
    zs = [v.co.z for v in bm.verts]
    x0, x1 = min(xs), max(xs)
    z0, z1 = min(zs), max(zs)
    cut = x0 + (x1 - x0) * 0.47
    amp = (x1 - x0) * 0.06
    step = (x1 - x0) * 0.015
    k = -int(amp * 1.7 / step)
    while cut + k * step < cut + amp * 1.7:
        geom = list(bm.verts) + list(bm.edges) + list(bm.faces)
        bmesh.ops.bisect_plane(bm, geom=geom, plane_co=(cut + k * step, 0.0, 0.0), plane_no=(1.0, 0.0, 0.0))
        k += 1
    for i in range(1, 16):
        geom = list(bm.verts) + list(bm.edges) + list(bm.faces)
        bmesh.ops.bisect_plane(bm, geom=geom, plane_co=(0.0, 0.0, z0 + (z1 - z0) * 0.45 * i / 16), plane_no=(0.0, 0.0, 1.0))
    bm.faces.ensure_lookup_table()
    doomed = []
    for f in bm.faces:
        c = f.calc_center_median()
        line = cut + amp * noise(0.0, c.y * 0.02, c.z * 0.015, seed) + amp * 0.6 * math.sin(c.z * 0.004 + seed)
        if (c.x < line) if keep == "bow" else (c.x > line):
            doomed.append(f)
    bmesh.ops.delete(bm, geom=doomed, context="FACES")
    bmesh.ops.delete(bm, geom=[v for v in bm.verts if not v.link_faces], context="VERTS")
    # Nothing left hanging in the air by the cut: drop splinters and severed mast tops near it.
    deck = z0 + (z1 - z0) * 0.45
    loose = []
    for part in islands(bm):
        cs = [f.calc_center_median() for f in part]
        cx = sum(c.x for c in cs) / len(cs)
        low = min(v.co.z for f in part for v in f.verts)
        if abs(cx - cut) < amp * 2.5 and (len(part) < 60 or low > deck):
            loose.extend(part)
    bmesh.ops.delete(bm, geom=loose, context="FACES")
    bmesh.ops.delete(bm, geom=[v for v in bm.verts if not v.link_faces], context="VERTS")


def islands(bm):
    """Face sets touching at a common corner position (the kit's flat-shaded meshes are split at every edge,
    so connectivity is by position, not by shared vertices)."""
    faces = list(bm.faces)
    parent = list(range(len(faces)))

    def root(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i

    owner = {}
    for i, f in enumerate(faces):
        for v in f.verts:
            key = (round(v.co.x, 1), round(v.co.y, 1), round(v.co.z, 1))
            if key in owner:
                a, b = root(i), root(owner[key])
                if a != b:
                    parent[a] = b
            else:
                owner[key] = i
    groups = {}
    for i, f in enumerate(faces):
        groups.setdefault(root(i), []).append(f)
    return list(groups.values())


def bake(name, src, scale, yaw, wreck, drop, beam):
    load(src)
    bm = join(drop)
    if yaw is None:  # small props: turn the longest horizontal axis to X
        yaw = -90.0 if long_axis(bm) == "y" else 0.0
    bmesh.ops.rotate(bm, verts=bm.verts, cent=(0, 0, 0), matrix=mathutils.Matrix.Rotation(math.radians(yaw), 3, "Z"))
    if name == "Oar":  # the kit's paddle stands upright: lay it down along X, blade at +X
        bmesh.ops.rotate(bm, verts=bm.verts, cent=(0, 0, 0), matrix=mathutils.Matrix.Rotation(math.radians(90.0), 3, "Y"))
        bmesh.ops.rotate(bm, verts=bm.verts, cent=(0, 0, 0), matrix=mathutils.Matrix.Rotation(math.radians(90.0), 3, "X"))  # blade flat
    if isinstance(scale, tuple):
        xs = [v.co.x for v in bm.verts]
        scale = scale[1] / max(1e-6, max(xs) - min(xs))
    bmesh.ops.scale(bm, vec=(scale, scale * beam, scale), verts=bm.verts)
    seed = sum(ord(c) for c in name)
    if wreck:
        tatter(bm, seed)
    if wreck in ("bow", "stern"):
        break_ship(bm, wreck, seed)
    xs = [v.co.x for v in bm.verts]
    ys = [v.co.y for v in bm.verts]
    zs = [v.co.z for v in bm.verts]
    bmesh.ops.translate(bm, verts=bm.verts, vec=(-(max(xs) + min(xs)) * 0.5, -(max(ys) + min(ys)) * 0.5, -min(zs)))
    me = bpy.data.meshes.new("SM_" + name)
    bm.to_mesh(me)
    bm.free()
    for o in list(bpy.data.objects):
        bpy.data.objects.remove(o, do_unlink=True)
    used = sorted({p.material_index for p in me.polygons})
    for i, s in enumerate(SLOTS):
        me.materials.append(bpy.data.materials.get(s) or bpy.data.materials.new(s))
    obj = bpy.data.objects.new("SM_" + name, me)
    bpy.context.scene.collection.objects.link(obj)
    d = obj.dimensions
    return obj, (round(d.x), round(d.y), round(d.z)), [SLOTS[i] for i in used]


MTL = """# Kenney Pirate Kit (CC0), baked by tools/convert_ships.py
newmtl Wood
Kd 0.42 0.3 0.2
map_Kd colormap.png

newmtl Cloth
Kd 0.62 0.6 0.55

newmtl Weed
Kd 0.12 0.16 0.07
"""


def export(obj, name):
    for o in bpy.data.objects:
        o.select_set(o == obj)
    bpy.context.view_layer.objects.active = obj
    path = os.path.join(OUT, "SM_%s.obj" % name)
    bpy.ops.wm.obj_export(filepath=path, export_selected_objects=True, forward_axis="Y", up_axis="Z", global_scale=1.0,
                          export_materials=True, path_mode="STRIP", export_uv=True, export_normals=True,
                          export_triangulated_mesh=True, apply_modifiers=True, export_animation=False)
    # Our own .mtl: slot colours for the build's tints and the palette texture by bare name, next to the OBJ.
    with open(os.path.join(OUT, "SM_%s.mtl" % name), "w", encoding="utf-8", newline="\n") as f:
        f.write(MTL)
    return path


def preview(obj, name):
    scene = bpy.context.scene
    d = obj.dimensions
    size = max(d.x, d.y, d.z) * 1.25
    cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
    cam.data.type = "ORTHO"
    cam.data.ortho_scale = size
    scene.collection.objects.link(cam)
    scene.camera = cam
    scene.render.engine = "BLENDER_WORKBENCH"
    scene.display.shading.color_type = "MATERIAL"
    scene.render.resolution_x = scene.render.resolution_y = 400
    os.makedirs(os.path.join(ROOT, "Saved"), exist_ok=True)
    cols = {"Wood": (0.45, 0.3, 0.18, 1), "Cloth": (0.9, 0.9, 0.85, 1), "Weed": (0.1, 0.5, 0.1, 1)}
    for m in obj.data.materials:
        m.diffuse_color = cols.get(m.name, (1, 0, 1, 1))
    # side view: camera on -Y looking +Y, so +X (the bow) is to the right
    cam.location = (0, -size * 4, d.z * 0.5)
    cam.rotation_euler = (math.radians(90), 0, 0)
    cam.data.clip_end = size * 10
    scene.render.filepath = os.path.join(ROOT, "Saved", "ships_%s_side.png" % name.lower())
    bpy.ops.render.render(write_still=True)
    cam.location = (0, 0, size * 4)
    cam.rotation_euler = (0, 0, 0)
    scene.render.filepath = os.path.join(ROOT, "Saved", "ships_%s_top.png" % name.lower())
    bpy.ops.render.render(write_still=True)


def main():
    import shutil
    shutil.copyfile(os.path.join(SRC, "Textures", "colormap.png"), os.path.join(OUT, "colormap.png"))
    only = [a for a in sys.argv[sys.argv.index("--") + 1:] if not a.startswith("--")] if "--" in sys.argv else []
    for name, spec in SHIPS.items():
        if only and name not in only:
            continue
        obj, dims, slots = bake(name, *spec)
        path = export(obj, name)
        print("SHIP %s -> %s dims(cm)=%s slots=%s tris=%d" % (name, path, dims, slots,
                                                            sum(len(p.vertices) - 2 for p in obj.data.polygons)))
        if PREVIEW:
            preview(obj, name)
    print("SHIPS_OK")


main()
