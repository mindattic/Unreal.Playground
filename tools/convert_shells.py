"""Blender (4.4+) script: bake the free sea shell sources into small static OBJ meshes for tools/build_island.py.

  blender -b --factory-startup --disable-autoexec --python tools/convert_shells.py [-- --preview]

Reads SourceArt/Environment/Shells/Source (see SourceArt/Environment/CREDITS.md). For each kind it keeps only
the shell (the clams' meat is cut away, the two shells in seashell.glb are split into a scallop and a whelk,
the cockle keeps one valve), lays it the way a shell rests on sand, scales it to real size in centimetres
(its longest horizontal side = the length below) with its bottom-centre at the origin, and writes
SourceArt/Environment/Shells/SM_Shell_<Kind>.obj (+ .mtl). Material slots are renamed <Kind>_Outer /
<Kind>_Inner; a slot drawn from a texture gets it saved beside the OBJ as shell_<image>.png (the .mtl's
map_Kd), which M_Shell uses for its banding. UE's OBJ import keeps Z up and +X forward (it only mirrors Y).
--preview renders a top and a side view of each to Saved/shells_<kind>.png.
"""
import math
import os
import sys

import bmesh
import bpy
import mathutils

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SRC = os.path.join(ROOT, "SourceArt", "Environment", "Shells", "Source")
OUT = os.path.join(ROOT, "SourceArt", "Environment", "Shells")
PREVIEW = "--preview" in sys.argv
TEX_SIZE = 256

# kind: (source, keep(part) -> bool, rotation applied before sizing (axis, degrees) or None,
#        longest horizontal side in cm, {source material: slot})
# A part is one connected piece of the source: {"centre", "size" (Vectors, source units), "mats" (names)}.
SHELLS = {
    # Common cockle: the clam's lower valve only (outer shell, inner face, rim), turned to lie convex side up.
    "Cockle": ("clam_65ObbG0O7Ze.glb", lambda p: p["centre"].z < 0.03 and p["size"].x > 1.0, ("X", 180.0), 4.0,
               {"lambert2SG": "Outer", "lambert3SG": "Inner"}),
    # The same valve lying hollow side up (rain-washed, showing its pale inside).
    "CockleUp": ("clam_65ObbG0O7Ze.glb", lambda p: p["centre"].z < 0.03 and p["size"].x > 1.0, None, 3.5,
                 {"lambert2SG": "Outer", "lambert3SG": "Inner"}),
    # Pale gaping clam (an empty pair of valves still hinged): the pink meat is dropped.
    "Clam": ("clam_7UsYZIZ_rhU.glb", lambda p: "Mat" in p["mats"], None, 5.0, {"Mat": "Outer"}),
    # The fan-shaped scallop valve from the seashell pair.
    "Scallop": ("seashell.glb", lambda p: p["centre"].x < -1.0, None, 8.0, {"lambert9SG": "Outer"}),
    # The spire shell from the seashell pair, lying on its side: a whelk.
    "Whelk": ("seashell.glb", lambda p: p["centre"].x > -1.0, ("Y", 90.0), 6.0, {"lambert10SG": "Outer"}),
    # Blue mussel, dead and gaping (Kenney's open mussel).
    "Mussel": ("mussel-open.glb", lambda p: True, None, 6.5, {"brownDarkest": "Outer", "brownLight": "Inner"}),
}


def load(src):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=os.path.join(SRC, src))


def join_world():
    """One bmesh of every mesh object in world space, plus its material list (by name)."""
    bm = bmesh.new()
    mats = []
    for o in [o for o in bpy.context.scene.objects if o.type == "MESH"]:
        me = o.data.copy()
        me.transform(o.matrix_world)
        remap = []
        for m in o.data.materials:
            if m not in mats:
                mats.append(m)
            remap.append(mats.index(m))
        offset = len(bm.faces)
        bm.from_mesh(me)
        bm.faces.ensure_lookup_table()
        for f in bm.faces[offset:]:
            if remap:
                f.material_index = remap[min(f.material_index, len(remap) - 1)]
        bpy.data.meshes.remove(me)
    return bm, mats


def parts(bm, mats):
    """Connected pieces as the source stores them (faces sharing a vertex). Positions are not welded: the clam's
    two valves meet at the hinge but are separate pieces, and only the piece-level split tells them apart."""
    bm.verts.ensure_lookup_table()
    bm.faces.ensure_lookup_table()
    key = {v.index: v.index for v in bm.verts}
    parent = {}

    def find(a):
        while parent.setdefault(a, a) != a:
            parent[a] = parent[parent[a]]
            a = parent[a]
        return a

    for f in bm.faces:
        ks = [key[v.index] for v in f.verts]
        for k in ks[1:]:
            ra, rb = find(ks[0]), find(k)
            if ra != rb:
                parent[ra] = rb
    groups = {}
    for f in bm.faces:
        groups.setdefault(find(key[f.verts[0].index]), []).append(f)
    out = []
    for faces in groups.values():
        pts = [v.co for f in faces for v in f.verts]
        plo = mathutils.Vector([min(p[i] for p in pts) for i in range(3)])
        phi = mathutils.Vector([max(p[i] for p in pts) for i in range(3)])
        names = {mats[f.material_index].name if f.material_index < len(mats) and mats[f.material_index] else "" for f in faces}
        out.append({"faces": faces, "centre": (plo + phi) * 0.5, "size": phi - plo, "mats": names})
    return out


def save_texture(mat, kind, slot):
    """The image feeding the material's base colour, saved beside the OBJ; returns its file name or None."""
    if not mat or not mat.use_nodes:
        return None
    bsdf = next((n for n in mat.node_tree.nodes if n.type == "BSDF_PRINCIPLED"), None)
    if not bsdf or not bsdf.inputs["Base Color"].is_linked:
        return None
    node = bsdf.inputs["Base Color"].links[0].from_node
    img = getattr(node, "image", None)
    if img is None:
        return None
    name = "shell_%s.png" % "".join(c for c in os.path.splitext(img.name)[0] if c.isalnum()).lower()
    path = os.path.join(OUT, name)
    if max(img.size) > TEX_SIZE:  # a few centimetres of shell: a small map is plenty
        img.scale(TEX_SIZE, TEX_SIZE)
    img.filepath_raw = path
    img.file_format = "PNG"
    img.save()
    return name


def bake(kind, src, keep, rot, length, slots):
    load(src)
    bm, mats = join_world()
    pieces = parts(bm, mats)
    kept = [p for p in pieces if keep(p)]
    if not kept:
        raise RuntimeError("%s: no part kept of %d" % (kind, len(pieces)))
    keep_faces = {f for p in kept for f in p["faces"]}
    bmesh.ops.delete(bm, geom=[f for f in bm.faces if f not in keep_faces], context="FACES")
    bmesh.ops.delete(bm, geom=[v for v in bm.verts if not v.link_faces], context="VERTS")

    # Material slots: rename to <Kind>_<Slot>; textured ones get their image written next to the OBJ.
    used = sorted({f.material_index for f in bm.faces})
    new_mats, remap = [], {}
    for i in used:
        src_mat = mats[i] if i < len(mats) else None
        slot = slots.get(src_mat.name if src_mat else "", "Outer")
        name = "%s_%s" % (kind, slot)
        mat = bpy.data.materials.get(name)
        if mat is None:
            mat = src_mat.copy() if src_mat else bpy.data.materials.new(name)
            mat.name = name
            tex = save_texture(mat, kind, slot)
            print("SHELL %s slot %s <- %s texture=%s" % (kind, name, src_mat.name if src_mat else None, tex))
            new_mats.append(mat)
        remap[i] = new_mats.index(mat)
    for f in bm.faces:
        f.material_index = remap[f.material_index]

    me = bpy.data.meshes.new("SM_Shell_%s" % kind)
    bm.to_mesh(me)
    bm.free()
    for m in new_mats:
        me.materials.append(m)
    for o in list(bpy.data.objects):
        bpy.data.objects.remove(o, do_unlink=True)
    obj = bpy.data.objects.new("SM_Shell_%s" % kind, me)
    bpy.context.scene.collection.objects.link(obj)

    if rot:
        me.transform(mathutils.Matrix.Rotation(math.radians(rot[1]), 4, rot[0]))
    xs = [v.co.x for v in me.vertices]
    ys = [v.co.y for v in me.vertices]
    zs = [v.co.z for v in me.vertices]
    s = length / max(1e-9, max(max(xs) - min(xs), max(ys) - min(ys)))
    cx, cy, z0 = (max(xs) + min(xs)) * 0.5, (max(ys) + min(ys)) * 0.5, min(zs)
    me.transform(mathutils.Matrix.Diagonal((s, s, s, 1.0)) @ mathutils.Matrix.Translation((-cx, -cy, -z0)))
    me.update()

    for o in bpy.data.objects:
        o.select_set(o == obj)
    bpy.context.view_layer.objects.active = obj
    try:
        bpy.ops.object.shade_smooth_by_angle(angle=math.radians(40.0))
    except Exception as exc:  # older Blender: flat shading is fine for these
        print("SHELL %s: smooth by angle failed: %s" % (kind, exc))
    return obj


def export(obj, kind):
    for o in bpy.data.objects:
        o.select_set(o == obj)
    bpy.context.view_layer.objects.active = obj
    path = os.path.join(OUT, "SM_Shell_%s.obj" % kind)
    bpy.ops.wm.obj_export(filepath=path, export_selected_objects=True, forward_axis="Y", up_axis="Z", global_scale=1.0,
                          export_materials=True, path_mode="STRIP", export_uv=True, export_normals=True,
                          export_triangulated_mesh=True, apply_modifiers=True, export_animation=False)
    return path


def preview(obj, kind):
    scene = bpy.context.scene
    d = obj.dimensions
    size = max(d.x, d.y, d.z) * 1.5
    cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
    cam.data.type = "ORTHO"
    cam.data.ortho_scale = size
    cam.data.clip_end = 10000.0
    scene.collection.objects.link(cam)
    scene.camera = cam
    scene.render.engine = "BLENDER_WORKBENCH"
    scene.display.shading.color_type = "TEXTURE"
    scene.render.resolution_x = scene.render.resolution_y = 300
    os.makedirs(os.path.join(ROOT, "Saved"), exist_ok=True)
    cam.location = (0, 0, 500)
    scene.render.filepath = os.path.join(ROOT, "Saved", "shells_%s.png" % kind.lower())
    bpy.ops.render.render(write_still=True)
    cam.location = (0, -500, d.z * 0.5)
    cam.rotation_euler = (math.radians(90), 0, 0)
    scene.render.filepath = os.path.join(ROOT, "Saved", "shells_%s_side.png" % kind.lower())
    bpy.ops.render.render(write_still=True)


def main():
    for kind, (src, keep, rot, length, slots) in SHELLS.items():
        obj = bake(kind, src, keep, rot, length, slots)
        d = obj.dimensions
        path = export(obj, kind)
        print("SHELL %s -> %s dims(cm)=(%.1f, %.1f, %.1f) tris=%d slots=%s" % (
            kind, path, d.x, d.y, d.z, sum(len(p.vertices) - 2 for p in obj.data.polygons), [m.name for m in obj.data.materials]))
        if PREVIEW:
            preview(obj, kind)
    print("SHELLS_OK")


main()
