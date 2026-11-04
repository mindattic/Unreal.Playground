"""Blender (4.4+) script: bake the CC0 critter sources into static OBJ meshes for tools/build_island.py.

  blender -b --factory-startup --disable-autoexec --python tools/convert_critters.py [-- --preview]

Reads SourceArt/Environment/Critters/Source (see SourceArt/Environment/CREDITS.md), poses each rig in
a resting frame, bakes the skinned mesh, turns it to face +X with Z up, scales it to life size in
centimetres with its bottom-centre at the origin, and writes SourceArt/Environment/Critters/SM_<Name>.obj
(+ .mtl). UE's OBJ import keeps Z up and +X forward (it only mirrors Y), so the game moves each critter
along +X. The crab.blend needs Blender 4.4 or newer (it was saved by 4.4). --preview also renders a top
view of each result to Saved/critters_<name>.png with a red marker on +X, to check the facing.
"""
import math
import os
import shutil
import sys

import bpy
import mathutils

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SRC = os.path.join(ROOT, "SourceArt", "Environment", "Critters", "Source")
OUT = os.path.join(ROOT, "SourceArt", "Environment", "Critters")
PREVIEW = "--preview" in sys.argv

# name: (source, action to pose from (None = rest pose), frame, yaw to face +X (degrees), length along X in cm)
CRITTERS = {
    "Crab": ("crab/crab.blend", "idle", 1, 90.0, 12.0),
    "Frog": ("frog.glb", "FrogArmature|Frog_Idle", 0, 90.0, 13.0),
    "Fish": ("fish.glb", None, 0, 90.0, 32.0),
}


def load(src):
    path = os.path.join(SRC, src)
    if path.endswith(".blend"):
        bpy.ops.wm.open_mainfile(filepath=path, load_ui=False)
    else:
        bpy.ops.wm.read_factory_settings(use_empty=True)
        bpy.ops.import_scene.gltf(filepath=path)


def pose(action_name, frame):
    for arm in [o for o in bpy.data.objects if o.type == "ARMATURE"]:
        if arm.animation_data is None:
            arm.animation_data_create()
        if action_name is None:
            arm.animation_data.action = None
            for pb in arm.pose.bones:
                pb.matrix_basis = mathutils.Matrix.Identity(4)
            continue
        act = next((a for a in bpy.data.actions if a.name == action_name or a.name.startswith(action_name + "_")), None)
        if act is None:
            raise RuntimeError("no action %s (have %s)" % (action_name, [a.name for a in bpy.data.actions]))
        arm.animation_data.action = act
        try:  # Blender 4.4 slotted actions
            if hasattr(arm.animation_data, "action_slot") and act.slots:
                arm.animation_data.action_slot = act.slots[0]
        except Exception:
            pass
    bpy.context.scene.frame_set(frame)


def bake():
    """One new mesh object holding every visible mesh as evaluated (posed), in world space."""
    dg = bpy.context.evaluated_depsgraph_get()
    import bmesh
    bm = bmesh.new()
    mats = []
    # Skip bone-display shapes (the glTF importer adds an Icosphere in its glTF_not_exported collection).
    shapes = {pb.custom_shape for a in bpy.data.objects if a.type == "ARMATURE" for pb in a.pose.bones if pb.custom_shape}
    meshes = [o for o in bpy.context.scene.objects if o.type == "MESH" and not o.hide_render and o not in shapes
              and not any(c.name.startswith("glTF_not_exported") for c in o.users_collection)]
    print("CRITTER meshes: %s" % [o.name for o in meshes])
    for o in meshes:
        ev = o.evaluated_get(dg)
        me = ev.to_mesh()
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
        ev.to_mesh_clear()
    out = bpy.data.meshes.new("baked")
    bm.to_mesh(out)
    bm.free()
    for m in mats:
        out.materials.append(m)
    for o in list(bpy.data.objects):
        bpy.data.objects.remove(o, do_unlink=True)
    obj = bpy.data.objects.new("baked", out)
    bpy.context.scene.collection.objects.link(obj)
    return obj


def normalise(obj, yaw, length):
    me = obj.data
    me.transform(mathutils.Matrix.Rotation(math.radians(yaw), 4, "Z"))
    xs = [v.co.x for v in me.vertices]
    ys = [v.co.y for v in me.vertices]
    zs = [v.co.z for v in me.vertices]
    s = length / max(1e-6, max(xs) - min(xs))
    cx, cy, z0 = (max(xs) + min(xs)) * 0.5, (max(ys) + min(ys)) * 0.5, min(zs)
    me.transform(mathutils.Matrix.Diagonal((s, s, s, 1.0)) @ mathutils.Matrix.Translation((-cx, -cy, -z0)))
    me.update()
    d = obj.dimensions
    return (round(d.x, 1), round(d.y, 1), round(d.z, 1))


def export(obj, name):
    obj.name = obj.data.name = "SM_%s" % name
    for o in bpy.data.objects:
        o.select_set(o == obj)
    bpy.context.view_layer.objects.active = obj
    path = os.path.join(OUT, "SM_%s.obj" % name)
    bpy.ops.wm.obj_export(filepath=path, export_selected_objects=True, forward_axis="Y", up_axis="Z", global_scale=1.0,
                          export_materials=True, path_mode="STRIP", export_uv=True, export_normals=True,
                          export_triangulated_mesh=True, apply_modifiers=True, export_animation=False)
    return path


def preview(obj, name):
    scene = bpy.context.scene
    d = obj.dimensions
    size = max(d.x, d.y) * 1.4
    bpy.ops.mesh.primitive_cube_add(size=size * 0.06, location=(size * 0.55, 0, 0))
    marker = bpy.context.active_object
    mat = bpy.data.materials.new("marker")
    mat.diffuse_color = (1, 0, 0, 1)
    marker.data.materials.append(mat)
    cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
    cam.data.type = "ORTHO"
    cam.data.ortho_scale = size * 1.3
    cam.location = (0, 0, 1000)
    scene.collection.objects.link(cam)
    scene.camera = cam
    scene.render.engine = "BLENDER_WORKBENCH"
    scene.display.shading.color_type = "MATERIAL"
    scene.render.resolution_x = scene.render.resolution_y = 400
    os.makedirs(os.path.join(ROOT, "Saved"), exist_ok=True)
    scene.render.filepath = os.path.join(ROOT, "Saved", "critters_%s.png" % name.lower())
    bpy.ops.render.render(write_still=True)
    # and a side view (camera on -Y looking +Y, so +X is to the right)
    cam.location = (0, -1000, d.z * 0.5)
    cam.rotation_euler = (math.radians(90), 0, 0)
    scene.render.filepath = os.path.join(ROOT, "Saved", "critters_%s_side.png" % name.lower())
    bpy.ops.render.render(write_still=True)


def main():
    for name, (src, action, frame, yaw, length) in CRITTERS.items():
        load(src)
        pose(action, frame)
        obj = bake()
        dims = normalise(obj, yaw, length)
        path = export(obj, name)
        # The baked .mtl names its texture by bare file name: keep the texture next to the OBJ.
        tex = os.path.join(os.path.dirname(os.path.join(SRC, src)), "%s.png" % name.lower())
        if os.path.exists(tex):
            shutil.copyfile(tex, os.path.join(OUT, os.path.basename(tex)))
        print("CRITTER %s -> %s dims(cm)=%s tris=%d" % (name, path, dims, sum(len(p.vertices) - 2 for p in obj.data.polygons)))
        if PREVIEW:
            preview(obj, name)
    print("CRITTERS_OK")


main()
