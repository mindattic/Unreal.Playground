"""Editor-Python (UE 5.8): build the Hebrides island map /Game/Souls/Maps/Lvl_Isle.

Imports the CC0 Poly Haven art (tools/fetch_island_art.py) and the procedural art
(tools/generate_island_art.py) from SourceArt/Environment, builds the terrain / water / grass
materials, generates the Landscape through UIslandBuilderLibrary (Source/UnrealSoulslike/Environment),
then lights, dresses and marks up the level.

Run (after tools/build_souls_character.py, which makes /Game/Souls/BP_SoulsGameMode):
  UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<this file> -unattended -nop4 -nosplash
Writes Saved/island.log (the commandlet swallows print()) ending with ISLAND_OK.
Idempotent: imported assets are reused (set ISLAND_REIMPORT=1 to force re-import); materials,
grass types and the map are rebuilt every run.
"""
import math
import os
import random

import unreal

PROJECT = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
LOG = open(os.path.join(PROJECT, "Saved", "island.log"), "w", encoding="utf-8")
eal = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
problems = []

ROOT = "/Game/Island"
MAP = "/Game/Souls/Maps/Lvl_Isle"
GAME_MODE = "/Game/Souls/BP_SoulsGameMode.BP_SoulsGameMode_C"
ART = os.path.join(PROJECT, "SourceArt", "Environment")
PH_TEX = os.path.join(ART, "PolyHaven", "Textures")
PH_MOD = os.path.join(ART, "PolyHaven", "Models")
GEN = os.path.join(ART, "Generated")
REIMPORT = os.environ.get("ISLAND_REIMPORT") == "1"
STAGE = os.environ.get("ISLAND_STAGE", "all")

TEXTURE_SETS = ["sparse_grass", "coast_sand_01", "aerial_beach_01", "damp_beach_sand", "seaside_rock", "rocky_trail"]
ACG_SETS = ["Grass004", "Grass003"]
ACG_TEX = os.path.join(ART, "AmbientCG")
MODELS = ["rock_moss_set_01", "rock_moss_set_02", "boulder_01", "rock_07", "rock_face_02", "mountainside",
          "modular_fort_01", "dead_tree_trunk"]


def log(msg):
    LOG.write(msg + "\n")
    LOG.flush()


def warn(msg):
    problems.append(msg)
    log("WARNING " + msg)


def ensure_dir(path):
    if not eal.does_directory_exist(path):
        eal.make_directory(path)


def import_files(files, dest, replace=True):
    tasks = []
    for filename in files:
        task = unreal.AssetImportTask()
        task.set_editor_property("filename", filename)
        task.set_editor_property("destination_path", dest)
        task.set_editor_property("replace_existing", replace)
        task.set_editor_property("automated", True)
        task.set_editor_property("save", True)
        tasks.append(task)
    tools.import_asset_tasks(tasks)
    out = []
    for t in tasks:
        out.extend(str(p) for p in (t.get_editor_property("imported_object_paths") or []))
    return out


def assets_of(folder, cls):
    out = []
    for p in eal.list_assets(folder, recursive=True, include_folder=False):
        a = eal.load_asset(p)
        if isinstance(a, cls):
            out.append(a)
    return out


# ----------------------------------------------------------------------------------------------
# Import
# ----------------------------------------------------------------------------------------------

def texture_kind(name):
    if "_nor_" in name or "_NormalGL" in name or name.startswith("T_WaterNormal"):
        return "normal"
    if "_diff_" in name or name.endswith("_Color"):
        return "color"
    return "linear"


def configure_texture(tex):
    kind = texture_kind(tex.get_name())
    if kind == "normal":
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
        tex.set_editor_property("srgb", False)
        tex.set_editor_property("flip_green_channel", True)  # Poly Haven / generated maps are OpenGL (Y+)
    elif kind == "linear":
        tex.set_editor_property("srgb", False)
        if any(k in tex.get_name() for k in ("_disp_", "_Displacement", "_Roughness", "_AmbientOcclusion")):
            tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_GRAYSCALE)
        else:
            tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
    eal.save_loaded_asset(tex)


def import_textures():
    sets = {}
    for tid in TEXTURE_SETS:
        dest = "%s/Textures/%s" % (ROOT, tid)
        folder = os.path.join(PH_TEX, tid)
        files = sorted(os.path.join(folder, f) for f in os.listdir(folder) if f.endswith(".jpg"))
        if REIMPORT or not eal.does_directory_exist(dest) or len(eal.list_assets(dest)) < len(files):
            import_files(files, dest)
            for tex in assets_of(dest, unreal.Texture2D):
                configure_texture(tex)
        maps = {}
        for tex in assets_of(dest, unreal.Texture2D):
            n = tex.get_name()
            for key, tag in (("color", "_diff_"), ("normal", "_nor_gl_"), ("arm", "_arm_"), ("disp", "_disp_")):
                if tag in n:
                    maps[key] = tex
        sets[tid] = maps
        log("texture set %s: %s" % (tid, sorted(maps)))
    for aid in ACG_SETS:
        dest = "%s/Textures/%s" % (ROOT, aid)
        folder = os.path.join(ACG_TEX, aid)
        files = sorted(os.path.join(folder, f) for f in os.listdir(folder) if f.endswith(".jpg"))
        if REIMPORT or not eal.does_directory_exist(dest) or len(eal.list_assets(dest)) < len(files):
            import_files(files, dest)
            for tex in assets_of(dest, unreal.Texture2D):
                configure_texture(tex)
        maps = {}
        for tex in assets_of(dest, unreal.Texture2D):
            n = tex.get_name()
            for key, tag in (("color", "_Color"), ("normal", "_NormalGL"), ("rough", "_Roughness"), ("ao", "_AmbientOcclusion"),
                             ("disp", "_Displacement")):
                if n.endswith(tag):
                    maps[key] = tex
        sets[aid] = maps
        log("texture set %s: %s" % (aid, sorted(maps)))
    gen = "%s/Textures/Generated" % ROOT
    files = [os.path.join(GEN, f) for f in ("T_WaterNormal.png", "T_IslandNoise.png")]
    if REIMPORT or not eal.does_asset_exist(gen + "/T_WaterNormal"):
        import_files(files, gen)
        for tex in assets_of(gen, unreal.Texture2D):
            configure_texture(tex)
    sets["generated"] = {t.get_name(): t for t in assets_of(gen, unreal.Texture2D)}
    log("generated textures: %s" % sorted(sets["generated"]))
    return sets


def nanite_and_collision(mesh, nanite=True, complex_collision=True):
    try:
        ns = mesh.get_editor_property("nanite_settings")
        ns.set_editor_property("enabled", nanite)
        mesh.set_editor_property("nanite_settings", ns)
    except Exception as exc:
        warn("nanite on %s: %s" % (mesh.get_name(), exc))
    if complex_collision:
        body = mesh.get_editor_property("body_setup")
        if body:
            body.set_editor_property("collision_trace_flag", unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
    eal.save_loaded_asset(mesh)


def import_models():
    models = {}
    for mid in MODELS:
        dest = "%s/Models/%s" % (ROOT, mid)
        gltf = os.path.join(PH_MOD, mid, "%s_2k.gltf" % mid)
        if REIMPORT or not assets_of(dest, unreal.StaticMesh) if eal.does_directory_exist(dest) else True:
            if eal.does_directory_exist(dest):
                eal.delete_directory(dest)
            import_files([gltf], dest)
            for mesh in assets_of(dest, unreal.StaticMesh):
                foliage = mid in ("fern_02",)
                nanite_and_collision(mesh, nanite=True, complex_collision=not foliage)
        meshes = assets_of(dest, unreal.StaticMesh)
        models[mid] = meshes
        for m in meshes:
            b = m.get_bounding_box()
            log("model %s: %s size=(%.0f, %.0f, %.0f) min=(%.0f, %.0f, %.0f) tris=%s" % (
                mid, m.get_name(), b.max.x - b.min.x, b.max.y - b.min.y, b.max.z - b.min.z, b.min.x, b.min.y, b.min.z,
                m.get_num_triangles(0) if hasattr(m, "get_num_triangles") else "?"))
    dest = "%s/Models/Grass" % ROOT
    if REIMPORT or not assets_of(dest, unreal.StaticMesh) if eal.does_directory_exist(dest) else True:
        import_files([os.path.join(GEN, f) for f in ("SM_GrassClump_A.obj", "SM_GrassClump_B.obj", "SM_GrassClump_C.obj")], dest)
        for mesh in assets_of(dest, unreal.StaticMesh):
            nanite_and_collision(mesh, nanite=False, complex_collision=False)
            mesh.set_editor_property("body_setup", None)
            eal.save_loaded_asset(mesh)
    models["grass"] = assets_of(dest, unreal.StaticMesh)
    for m in models["grass"]:
        b = m.get_bounding_box()
        log("grass %s: size=(%.0f, %.0f, %.0f) min z=%.1f" % (m.get_name(), b.max.x - b.min.x, b.max.y - b.min.y, b.max.z - b.min.z, b.min.z))
    return models


# ----------------------------------------------------------------------------------------------
# Layout (centimetres; island centred on the origin, sea level Z = 0)
# ----------------------------------------------------------------------------------------------

HILL = (8600.0, 7600.0)
ARENA_RADIUS = 2900.0
ARENA_HEIGHT = 3000.0
RING_RADIUS = 2350.0
BEACH_DIR = (-0.86, -0.51)
CLIFF_DIR = (0.62, 0.78)
LIB = unreal.IslandBuilderLibrary


def v2(x, y):
    return unreal.Vector2D(x, y)


def make_layout(path=(), pads=()):
    lay = unreal.IslandLayout()
    lay.set_editor_property("seed", 7)
    lay.set_editor_property("radius_x", 20500.0)
    lay.set_editor_property("radius_y", 17000.0)
    lay.set_editor_property("cliff_direction", v2(*CLIFF_DIR))
    lay.set_editor_property("cliff_height", 1900.0)
    lay.set_editor_property("beach_direction", v2(*BEACH_DIR))
    lay.set_editor_property("hill_center", v2(*HILL))
    lay.set_editor_property("hill_radius", 8800.0)
    lay.set_editor_property("arena_radius", ARENA_RADIUS)
    lay.set_editor_property("arena_height", ARENA_HEIGHT)
    lay.set_editor_property("path", [v2(x, y) for x, y in path])
    lay.set_editor_property("path_width", 420.0)
    lay.set_editor_property("pads", [unreal.Vector(x, y, r) for x, y, r in pads])
    lay.set_editor_property("components_per_side", 8)
    lay.set_editor_property("quad_size", 60.0)
    return lay


def height(x, y):
    return LIB.get_island_height(x, y)


def normal_z(x, y):
    return LIB.get_island_normal(x, y).z


def lerp2(a, b, t):
    return (a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t)


class Route:
    def __init__(self, points):
        self.points = points
        self.cum = [0.0]
        for a, b in zip(points, points[1:]):
            self.cum.append(self.cum[-1] + math.hypot(b[0] - a[0], b[1] - a[1]))

    @property
    def length(self):
        return self.cum[-1]

    def at(self, t):
        """Point and unit direction at fraction t of the route length."""
        d = max(0.0, min(1.0, t)) * self.length
        for i in range(len(self.points) - 1):
            if d <= self.cum[i + 1] or i == len(self.points) - 2:
                seg = self.cum[i + 1] - self.cum[i]
                u = 0.0 if seg <= 0 else (d - self.cum[i]) / seg
                a, b = self.points[i], self.points[i + 1]
                dx, dy = b[0] - a[0], b[1] - a[1]
                n = math.hypot(dx, dy) or 1.0
                return lerp2(a, b, u), (dx / n, dy / n)
        return self.points[-1], (1.0, 0.0)

    def offset(self, t, side):
        (x, y), (dx, dy) = self.at(t)
        return (x - dy * side, y + dx * side)


def plan_layout():
    """Find the start beach on the real coastline, then lay the route, ruins pads and fights."""
    LIB.set_island_layout(make_layout())
    bx, by = BEACH_DIR
    n = math.hypot(bx, by)
    bx, by = bx / n, by / n
    start = None
    r = 6000.0
    while r < 30000.0:
        if height(bx * r, by * r) < 110.0:
            break
        start = (bx * r, by * r)
        r += 100.0
    log("start beach at (%.0f, %.0f) h=%.0f" % (start[0], start[1], height(*start)))

    sx, sy = start
    hx, hy = HILL
    dx, dy = hx - sx, hy - sy
    dist = math.hypot(dx, dy)
    ux, uy = dx / dist, dy / dist
    px, py = -uy, ux
    stops = [(0.0, 0.0), (0.12, 900.0), (0.27, 2400.0), (0.43, -1200.0), (0.58, -2600.0), (0.72, -400.0),
             (0.84, 1300.0), (0.93, 400.0), (1.0, 0.0)]
    pts = [(sx + dx * t + px * o, sy + dy * t + py * o) for t, o in stops]
    route = Route(pts)
    pads = [route.offset(0.25, 1500.0) + (1200.0,), route.offset(0.47, -1800.0) + (1700.0,),
            route.offset(0.70, 1500.0) + (1100.0,)]
    LIB.set_island_layout(make_layout(pts, pads))
    log("route %.0f m through %d points; pads %s" % (route.length / 100.0, len(pts), [(round(p[0]), round(p[1]), p[2]) for p in pads]))
    return route, pads


# ----------------------------------------------------------------------------------------------
# Material graph helpers
# ----------------------------------------------------------------------------------------------

E = unreal


class Graph:
    def __init__(self, mat):
        self.mat = mat
        self.count = 0

    def new(self, cls, **props):
        self.count += 1
        e = mel.create_material_expression(self.mat, cls, -400 - 260 * (self.count % 14), 160 * (self.count // 14))
        for k, v in props.items():
            e.set_editor_property(k, v)
        return e

    def val(self, v):
        if isinstance(v, (int, float)):
            return self.new(E.MaterialExpressionConstant, r=float(v))
        if isinstance(v, tuple) and v and isinstance(v[0], (int, float)):
            if len(v) == 2:
                return self.new(E.MaterialExpressionConstant2Vector, r=float(v[0]), g=float(v[1]))
            return self.new(E.MaterialExpressionConstant3Vector, constant=unreal.LinearColor(v[0], v[1], v[2], 1.0))
        return v

    def link(self, src, dst, dst_in=""):
        src = self.val(src)
        e, out = src if isinstance(src, tuple) else (src, "")
        if not mel.connect_material_expressions(e, out, dst, dst_in):
            warn("%s: could not connect %s.%s -> %s.%s" % (self.mat.get_name(), e.get_class().get_name(), out, dst.get_class().get_name(), dst_in))

    def op(self, cls, inputs, **props):
        e = self.new(cls, **props)
        for k, v in inputs.items():
            self.link(v, e, k)
        return e

    def mul(self, a, b): return self.op(E.MaterialExpressionMultiply, {"A": a, "B": b})
    def add(self, a, b): return self.op(E.MaterialExpressionAdd, {"A": a, "B": b})
    def sub(self, a, b): return self.op(E.MaterialExpressionSubtract, {"A": a, "B": b})
    def div(self, a, b): return self.op(E.MaterialExpressionDivide, {"A": a, "B": b})
    def lerp(self, a, b, t): return self.op(E.MaterialExpressionLinearInterpolate, {"A": a, "B": b, "Alpha": t})
    def sat(self, a): return self.op(E.MaterialExpressionSaturate, {"": a})
    def one_minus(self, a): return self.op(E.MaterialExpressionOneMinus, {"": a})
    def abs(self, a): return self.op(E.MaterialExpressionAbs, {"": a})
    def maxx(self, a, b): return self.op(E.MaterialExpressionMax, {"A": a, "B": b})
    def minx(self, a, b): return self.op(E.MaterialExpressionMin, {"A": a, "B": b})
    def smooth(self, lo, hi, x): return self.op(E.MaterialExpressionSmoothStep, {"Min": lo, "Max": hi, "Value": x})
    def append(self, a, b): return self.op(E.MaterialExpressionAppendVector, {"A": a, "B": b})
    def dot(self, a, b): return self.op(E.MaterialExpressionDotProduct, {"A": a, "B": b})
    def sine(self, a): return self.op(E.MaterialExpressionSine, {"": a}, period=6.2831853)
    def normalize(self, a): return self.op(E.MaterialExpressionNormalize, {"VectorInput": a})

    def mask(self, a, r=False, g=False, b=False, alpha=False):
        return self.op(E.MaterialExpressionComponentMask, {"": a}, r=r, g=g, b=b, a=alpha)

    def tex(self, texture, uv, kind="color", clamp=False):
        st = {"color": E.MaterialSamplerType.SAMPLERTYPE_COLOR, "normal": E.MaterialSamplerType.SAMPLERTYPE_NORMAL,
              "masks": E.MaterialSamplerType.SAMPLERTYPE_MASKS, "gray": E.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE,
              "linear": E.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR}[kind]
        src = E.SamplerSourceMode.SSM_CLAMP_WORLD_GROUP_SETTINGS if clamp else E.SamplerSourceMode.SSM_WRAP_WORLD_GROUP_SETTINGS
        e = self.new(E.MaterialExpressionTextureSample, texture=texture, sampler_type=st, sampler_source=src)
        self.link(uv, e, "UVs")
        return e

    def world(self): return self.new(E.MaterialExpressionWorldPosition)
    def time(self): return self.new(E.MaterialExpressionTime)
    def depth(self): return self.new(E.MaterialExpressionPixelDepth)

    def out(self, src, prop):
        src = self.val(src)
        e, o = src if isinstance(src, tuple) else (src, "")
        if not mel.connect_material_property(e, o, prop):
            warn("%s: could not connect to %s" % (self.mat.get_name(), prop))


def new_material(name, folder):
    path = "%s/%s" % (folder, name)
    ensure_dir(folder)
    if eal.does_asset_exist(path) and not eal.delete_asset(path):
        mat = eal.load_asset(path)
        mel.delete_all_material_expressions(mat)
        log("reusing material %s (%d expressions left)" % (name, mel.get_num_material_expressions(mat)))
    else:
        mat = tools.create_asset(name, folder, unreal.Material, unreal.MaterialFactoryNew())
    return mat, Graph(mat)


def finish_material(mat):
    mel.recompile_material(mat)
    eal.save_loaded_asset(mat)
    log("material %s: %d expressions" % (mat.get_name(), mel.get_num_material_expressions(mat)))


# ----------------------------------------------------------------------------------------------
# Materials
# ----------------------------------------------------------------------------------------------

def mask_uv(g, wp_xy, extent):
    return g.div(g.add(wp_xy, (extent, extent)), (2.0 * extent, 2.0 * extent))


def build_terrain_material(tex, mask_tex, grass_type):
    mat, g = new_material("M_IslandTerrain", ROOT + "/Materials")
    extent = LIB.get_landscape_extent()
    wp = g.world()
    wp_xy = g.mask(wp, r=True, g=True)
    wz = g.mask(wp, b=True)
    nz = g.mask(g.new(E.MaterialExpressionVertexNormalWS), b=True)
    depth = g.depth()
    gen = tex["generated"]

    mask = g.tex(mask_tex, mask_uv(g, wp_xy, extent), "linear", clamp=True)
    noise_macro = g.tex(gen["T_IslandNoise"], g.div(wp_xy, (9000.0, 9000.0)), "masks")
    noise_mid = g.tex(gen["T_IslandNoise"], g.div(wp_xy, (1400.0, 1400.0)), "masks")
    macro = (noise_macro, "R")
    mid = (noise_mid, "G")

    def layer(name, tile):
        t = tex[name]
        uv = g.div(wp_xy, (tile, tile))
        return {"color": (g.tex(t["color"], uv, "color"), "RGB"), "normal": (g.tex(t["normal"], uv, "normal"), "RGB"),
                "arm": g.tex(t["arm"], uv, "masks"), "disp": (g.tex(t["disp"], uv, "gray"), "R"), "uv": uv}

    def acg_layer(name, tile):
        t = tex[name]
        uv = g.div(wp_xy, (tile, tile))
        return {"color": (g.tex(t["color"], uv, "color"), "RGB"), "normal": (g.tex(t["normal"], uv, "normal"), "RGB"),
                "rough": (g.tex(t["rough"], uv, "gray"), "R"), "ao": (g.tex(t["ao"], uv, "gray"), "R"), "uv": uv}

    grass = acg_layer("Grass004", 340.0)
    grass_far = (g.tex(tex["Grass004"]["color"], g.div(wp_xy, (1470.0, 1470.0)), "color"), "RGB")
    olive = (g.tex(tex["Grass003"]["color"], g.div(wp_xy, (610.0, 610.0)), "color"), "RGB")
    heath = (g.tex(tex["sparse_grass"]["color"], g.div(wp_xy, (520.0, 520.0)), "color"), "RGB")
    sand = layer("aerial_beach_01", 1100.0)
    wet = (g.tex(tex["damp_beach_sand"]["color"], g.div(wp_xy, (460.0, 460.0)), "color"), "RGB")
    rock = layer("seaside_rock", 700.0)
    trail = layer("rocky_trail", 330.0)

    # Rock on steep faces: blend two side projections so cliffs don't smear.
    wx = g.mask(wp, r=True)
    wy = g.mask(wp, g=True)
    side_x = (g.tex(tex["seaside_rock"]["color"], g.div(g.append(wy, wz), (900.0, 900.0)), "color"), "RGB")
    side_y = (g.tex(tex["seaside_rock"]["color"], g.div(g.append(wx, wz), (900.0, 900.0)), "color"), "RGB")
    vn = g.new(E.MaterialExpressionVertexNormalWS)
    ax = g.abs(g.mask(vn, r=True))
    ay = g.abs(g.mask(vn, g=True))
    side = g.lerp(side_y, side_x, g.sat(g.div(ax, g.add(g.add(ax, ay), 0.001))))
    steep = g.smooth(0.85, 0.45, nz)
    rock_color = g.lerp(rock["color"], side, steep)

    # Grass: two scales of the turf, olive and heather-brown patches by noise, muted toward Hebridean greens.
    far_t = g.add(0.3, g.mul(g.smooth(1500.0, 7000.0, depth), 0.55))
    grass_c = g.lerp(grass["color"], grass_far, far_t)
    grass_c = g.lerp(grass_c, olive, g.mul(g.smooth(0.40, 0.70, macro), 0.65))
    grass_c = g.lerp(grass_c, heath, g.mul(g.smooth(0.60, 0.80, mid), 0.5))
    # Broad heather / dead-bracken patches so the moor reads as varied ground from a distance.
    heath_patch = g.smooth(0.56, 0.74, (noise_macro, "B"))
    grass_c = g.lerp(grass_c, g.mul(heath, (0.78, 0.62, 0.6)), g.mul(heath_patch, 0.55))
    lum = g.dot(grass_c, (0.3, 0.59, 0.11))
    grass_c = g.lerp(grass_c, lum, 0.1)
    grass_c = g.mul(grass_c, g.lerp((1.0, 1.0, 0.78), (0.88, 0.97, 0.8), mid))
    # Anti-tiling for sand and trail: a second, larger sample mixed in by noise.
    pebbles = (g.tex(tex["coast_sand_01"]["color"], g.div(wp_xy, (430.0, 430.0)), "color"), "RGB")
    sand_c = g.lerp(sand["color"], pebbles, g.mul(g.smooth(0.55, 0.85, mid), 0.6))
    sand_c = g.mul(sand_c, (1.02, 1.0, 0.96))
    trail2 = (g.tex(tex["rocky_trail"]["color"], g.div(wp_xy, (1090.0, 1090.0)), "color"), "RGB")
    trail_c = g.lerp(trail["color"], trail2, g.add(0.2, g.mul(macro, 0.5)))
    trail_c = g.lerp(trail_c, g.dot(trail_c, (0.3, 0.59, 0.11)), 0.45)
    trail_c = g.mul(trail_c, (0.82, 0.8, 0.76))

    # Weights.
    path_w = g.maxx((mask, "R"), g.mul((mask, "G"), g.smooth(0.3, 0.7, g.add(mid, 0.15))))
    path_a = g.smooth(0.42, 0.58, g.add(path_w, g.mul(g.sub(trail["disp"], 0.5), 0.6)))
    sand_line = g.add(240.0, g.mul(g.sub(macro, 0.5), 160.0))
    sand_w = g.one_minus(g.smooth(g.sub(sand_line, 60.0), sand_line, wz))
    sand_a = g.smooth(0.42, 0.58, g.add(sand_w, g.mul(g.sub(sand["disp"], 0.5), -0.5)))
    wet_a = g.one_minus(g.smooth(10.0, 70.0, wz))
    rock_w = g.maxx(g.smooth(0.84, 0.70, nz), g.mul((mask, "A"), g.smooth(0.97, 0.88, nz)))
    rock_a = g.smooth(0.40, 0.60, g.add(rock_w, g.mul(g.sub(rock["disp"], 0.5), 0.7)))

    def blend(key, grass_v, trail_v, sand_v, rock_v, wet_v=None):
        v = g.lerp(grass_v, trail_v, path_a)
        s = sand_v if wet_v is None else g.lerp(sand_v, wet_v, wet_a)
        v = g.lerp(v, s, sand_a)
        return g.lerp(v, rock_v, rock_a)

    base = blend("color", grass_c, trail_c, sand_c, rock_color, wet)
    # Gentle large-scale brightness variation breaks up any remaining repetition.
    base = g.mul(base, g.lerp(0.82, 1.1, macro))
    normal = blend("normal", grass["normal"], trail["normal"], sand["normal"], rock["normal"])
    rough = blend("rough", g.maxx(grass["rough"], 0.82), g.maxx((trail["arm"], "G"), 0.65), (sand["arm"], "G"), (rock["arm"], "G"),
                  g.mul((sand["arm"], "G"), 0.45))
    ao = blend("ao", grass["ao"], (trail["arm"], "R"), (sand["arm"], "R"), (rock["arm"], "R"))

    g.out(base, E.MaterialProperty.MP_BASE_COLOR)
    g.out(normal, E.MaterialProperty.MP_NORMAL)
    g.out(rough, E.MaterialProperty.MP_ROUGHNESS)
    g.out(ao, E.MaterialProperty.MP_AMBIENT_OCCLUSION)
    g.out(g.lerp(0.22, 0.4, g.maxx(sand_a, rock_a)), E.MaterialProperty.MP_SPECULAR)

    # Procedural grass: where the turf shows, minus path / sand / rock, patchy by noise.
    if grass_type is not None:
        grass_w = g.mul(g.mul(g.one_minus(g.smooth(0.12, 0.42, path_w)), g.one_minus(sand_a)), g.one_minus(rock_a))
        grass_w = g.mul(grass_w, g.smooth(0.15, 0.45, g.add(mid, g.mul(macro, 0.3))))
        go = g.new(E.MaterialExpressionLandscapeGrassOutput)
        gi = unreal.GrassInput()
        gi.set_editor_property("name", "Turf")
        gi.set_editor_property("grass_type", grass_type)
        go.set_editor_property("grass_types", [gi])
        g.link(grass_w, go, "Turf")
    finish_material(mat)
    return mat


def build_ocean_material(tex, mask_tex):
    mat, g = new_material("M_Ocean", ROOT + "/Materials")
    mat.set_editor_property("shading_model", E.MaterialShadingModel.MSM_SINGLE_LAYER_WATER)
    extent = LIB.get_landscape_extent()
    wp = g.world()
    wp_xy = g.mask(wp, r=True, g=True)
    t = g.time()
    nrm = tex["generated"]["T_WaterNormal"]
    noise = tex["generated"]["T_IslandNoise"]

    def pan(scale, speed):
        return g.add(g.div(wp_xy, (scale, scale)), g.mul(t, speed))

    n1 = g.tex(nrm, pan(2600.0, (0.010, 0.004)), "normal")
    n2 = g.tex(nrm, pan(820.0, (-0.016, 0.011)), "normal")
    n3 = g.tex(nrm, pan(9000.0, (0.002, 0.003)), "normal")
    xy = g.add(g.add(g.mask((n1, "RGB"), r=True, g=True), g.mul(g.mask((n2, "RGB"), r=True, g=True), 0.6)),
               g.mul(g.mask((n3, "RGB"), r=True, g=True), 1.2))
    far = g.smooth(2000.0, 60000.0, g.depth())
    xy = g.mul(xy, g.lerp(0.4, 0.12, far))
    normal = g.normalize(g.append(xy, 1.0))

    mask = g.tex(mask_tex, mask_uv(g, wp_xy, extent), "linear", clamp=True)
    water_depth = (mask, "B")  # 0..1 = 0..10 m
    foam_noise = (g.tex(noise, pan(700.0, (0.006, -0.004)), "masks"), "B")
    foam_noise2 = (g.tex(noise, pan(260.0, (-0.01, 0.007)), "masks"), "G")
    surge = g.add(0.5, g.mul(g.sine(g.sub(g.mul(t, 0.9), g.mul(water_depth, 40.0))), 0.5))
    shore = g.one_minus(g.smooth(0.0, 0.09, water_depth))
    edge = g.one_minus(g.smooth(0.0, 0.025, water_depth))
    foam = g.mul(shore, g.smooth(0.45, 0.75, g.add(g.mul(foam_noise, foam_noise2), g.mul(surge, 0.35))))
    foam = g.sat(g.add(foam, g.mul(edge, g.smooth(0.3, 0.6, foam_noise2))))
    g.out(g.mul(foam, (0.85, 0.88, 0.88)), E.MaterialProperty.MP_BASE_COLOR)
    g.out(g.lerp(0.06, 0.5, foam), E.MaterialProperty.MP_ROUGHNESS)
    g.out(0.0, E.MaterialProperty.MP_METALLIC)
    g.out(normal, E.MaterialProperty.MP_NORMAL)
    g.out(g.one_minus(g.mul(foam, 0.9)), E.MaterialProperty.MP_OPACITY)

    wo = g.new(E.MaterialExpressionSingleLayerWaterMaterialOutput)
    for names, value in ((("ScatteringCoefficients", "Scattering Coefficients"), (0.0002, 0.0009, 0.0011)),
                         (("AbsorptionCoefficients", "Absorption Coefficients"), (0.005, 0.0016, 0.0012)),
                         (("PhaseG", "Phase G"), 0.1),
                         (("ColorScaleBehindWater", "Color Scale Behind Water"), 1.0)):
        src = g.val(value)
        if not any(mel.connect_material_expressions(src, "", wo, n) for n in names):
            warn("ocean: no SLW input %s" % (names,))
    finish_material(mat)
    return mat


GRASS_NOISE = []


def build_grass_material():
    mat, g = new_material("M_GrassBlades", ROOT + "/Materials")
    mat.set_editor_property("shading_model", E.MaterialShadingModel.MSM_TWO_SIDED_FOLIAGE)
    mat.set_editor_property("two_sided", True)
    mat.set_editor_property("used_with_instanced_static_meshes", True)
    uv = g.new(E.MaterialExpressionTextureCoordinate)
    tip = g.sat(g.one_minus(g.mask(uv, g=True)))  # OBJ import flips V: root = 1
    rnd = g.new(E.MaterialExpressionPerInstanceRandom)
    wp = g.world()
    wp_xy = g.mask(wp, r=True, g=True)
    root_c = (0.018, 0.030, 0.008)
    tip_a = (0.11, 0.16, 0.035)
    tip_b = (0.20, 0.21, 0.06)
    tip_c = g.lerp(tip_a, tip_b, rnd)
    patch = (g.tex(GRASS_NOISE[0], g.div(wp_xy, (9000.0, 9000.0)), "masks"), "B")
    tip_c = g.lerp(tip_c, (0.17, 0.12, 0.07), g.mul(g.smooth(0.56, 0.74, patch), 0.6))
    color = g.lerp(root_c, tip_c, g.op(E.MaterialExpressionPower, {"Base": tip, "Exp": 0.7}))
    g.out(color, E.MaterialProperty.MP_BASE_COLOR)
    g.out(g.lerp(0.75, 0.5, tip), E.MaterialProperty.MP_ROUGHNESS)
    g.out(0.25, E.MaterialProperty.MP_SPECULAR)
    g.out(g.mul(color, 0.6), E.MaterialProperty.MP_SUBSURFACE_COLOR)
    g.out(g.lerp(0.35, 1.0, tip), E.MaterialProperty.MP_AMBIENT_OCCLUSION)
    # Wind: gusts travel across the island, blades sway by tip^2.
    t = g.time()
    phase = g.add(g.mul(t, 1.7), g.add(g.div(g.mask(wp, r=True), 420.0), g.div(g.mask(wp, g=True), 650.0)))
    gust = g.add(g.mul(g.sine(phase), 0.5), g.mul(g.sine(g.add(g.mul(phase, 2.3), g.mul(rnd, 6.0))), 0.25))
    sway = g.mul(g.mul(g.mul(tip, tip), g.add(gust, 0.6)), 7.0)
    g.out(g.append(g.append(sway, g.mul(sway, 0.35)), 0.0), E.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    finish_material(mat)
    return mat


def build_prop_master(tex):
    """Scanned-prop master: the asset's own PBR maps, a tint, and moss/turf creeping over upward faces
    so rocks, ruins and stones sit in the grass instead of on it."""
    mat, g = new_material("M_IslandProp", ROOT + "/Materials")
    mat.set_editor_property("used_with_nanite", True)
    mat.set_editor_property("used_with_instanced_static_meshes", True)
    gen = tex["generated"]
    uv = g.new(E.MaterialExpressionTextureCoordinate)

    def tparam(name, default, kind):
        st = {"color": E.MaterialSamplerType.SAMPLERTYPE_COLOR, "normal": E.MaterialSamplerType.SAMPLERTYPE_NORMAL,
              "masks": E.MaterialSamplerType.SAMPLERTYPE_MASKS}[kind]
        e = g.new(E.MaterialExpressionTextureSampleParameter2D, parameter_name=name, texture=default, sampler_type=st,
                  sampler_source=E.SamplerSourceMode.SSM_WRAP_WORLD_GROUP_SETTINGS)
        g.link(uv, e, "UVs")
        return e

    def sparam(name, value):
        return g.new(E.MaterialExpressionScalarParameter, parameter_name=name, default_value=value)

    base = tparam("Base", tex["seaside_rock"]["color"], "color")
    nrm = tparam("Normal", tex["seaside_rock"]["normal"], "normal")
    arm = tparam("ARM", tex["seaside_rock"]["arm"], "masks")
    tint = g.new(E.MaterialExpressionVectorParameter, parameter_name="Tint", default_value=unreal.LinearColor(1, 1, 1, 1))
    color = g.mul((base, "RGB"), (tint, "RGB"))
    ao = g.lerp(1.0, (arm, "R"), sparam("AOStrength", 1.0))
    rough = g.sat(g.mul((arm, "G"), sparam("RoughnessScale", 1.0)))

    wp = g.world()
    wp_xy = g.mask(wp, r=True, g=True)
    moss_tex = (g.tex(tex["Grass003"]["color"], g.div(wp_xy, (260.0, 260.0)), "color"), "RGB")
    noise = (g.tex(gen["T_IslandNoise"], g.div(wp_xy, (700.0, 700.0)), "masks"), "B")
    vnz = g.mask(g.new(E.MaterialExpressionVertexNormalWS), b=True)
    moss_w = g.smooth(0.45, 0.8, g.add(vnz, g.mul(g.sub(noise, 0.5), 0.7)))
    moss_w = g.mul(moss_w, sparam("Moss", 0.6))
    color = g.lerp(color, g.mul(moss_tex, (0.75, 0.82, 0.62)), moss_w)
    g.out(color, E.MaterialProperty.MP_BASE_COLOR)
    g.out(g.lerp((nrm, "RGB"), (0.0, 0.0, 1.0), g.mul(moss_w, 0.6)), E.MaterialProperty.MP_NORMAL)
    g.out(g.lerp(rough, 0.9, moss_w), E.MaterialProperty.MP_ROUGHNESS)
    g.out(ao, E.MaterialProperty.MP_AMBIENT_OCCLUSION)
    finish_material(mat)
    return mat


def prop_instance(master, name, maps, tint=(1, 1, 1), moss=0.6, ao_strength=1.0):
    folder = ROOT + "/Materials/Props"
    ensure_dir(folder)
    path = "%s/%s" % (folder, name)
    mi = eal.load_asset(path) if eal.does_asset_exist(path) else \
        tools.create_asset(name, folder, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mel.set_material_instance_parent(mi, master)
    for param, key in (("Base", "diff"), ("Normal", "nor_gl"), ("ARM", "rough")):
        if key in maps:
            mel.set_material_instance_texture_parameter_value(mi, param, maps[key])
    mel.set_material_instance_vector_parameter_value(mi, "Tint", unreal.LinearColor(tint[0], tint[1], tint[2], 1.0))
    mel.set_material_instance_scalar_parameter_value(mi, "Moss", moss)
    mel.set_material_instance_scalar_parameter_value(mi, "AOStrength", ao_strength)
    eal.save_loaded_asset(mi)
    return mi


# Per model: tint, moss amount, whether its packed map carries AO in R (Poly Haven "arm") or only roughness.
PROP_LOOKS = {
    "rock_moss_set_01": ((0.95, 0.97, 1.0), 0.45, 0.0),
    "rock_moss_set_02": ((0.95, 0.97, 1.0), 0.45, 0.0),
    "boulder_01": ((0.62, 0.64, 0.66), 0.65, 1.0),
    "rock_07": ((0.62, 0.66, 0.7), 0.45, 1.0),
    "rock_face_02": ((0.8, 0.82, 0.84), 0.7, 1.0),
    "mountainside": ((0.78, 0.8, 0.82), 0.75, 1.0),
    "modular_fort_01": ((0.6, 0.62, 0.62), 0.75, 1.0),
    "dead_tree_trunk": ((0.85, 0.85, 0.85), 0.0, 1.0),
}


def apply_prop_materials(models, master):
    for mid, (tint, moss, ao_strength) in PROP_LOOKS.items():
        meshes = models.get(mid) or []
        if not meshes:
            continue
        folder = "%s/Models/%s" % (ROOT, mid)
        sets = {}
        for t in assets_of(folder, unreal.Texture2D):
            n = t.get_name()
            for key in ("diff", "nor_gl", "rough"):
                if n.endswith("_" + key):
                    sets.setdefault(n[:-len(key) - 1], {})[key] = t
        if not sets:
            warn("no textures for %s" % mid)
            continue
        instances = {prefix: prop_instance(master, "MI_" + prefix,
                                           maps, tint, moss, ao_strength) for prefix, maps in sets.items()}
        for mesh in meshes:
            for i, slot in enumerate(mesh.get_editor_property("static_materials")):
                slot_name = str(slot.get_editor_property("material_slot_name"))
                mi = next((m for p, m in instances.items() if slot_name.startswith(p)), None) or next(iter(instances.values()))
                mesh.set_material(i, mi)
            eal.save_loaded_asset(mesh)
        log("prop materials %s: %s" % (mid, sorted(instances)))


def build_grass_type(models, grass_mat):
    path = ROOT + "/Foliage/LGT_IslandTurf"
    ensure_dir(ROOT + "/Foliage")
    lgt = eal.load_asset(path) if eal.does_asset_exist(path) else \
        tools.create_asset("LGT_IslandTurf", ROOT + "/Foliage", unreal.LandscapeGrassType, None)
    by_name = {m.get_name(): m for m in models["grass"]}
    varieties = []
    for name, density, cull_start, cull_end, scale, shadows in (
            ("SM_GrassClump_C", 520.0, 3200, 5200, (0.8, 1.3), False),
            ("SM_GrassClump_A", 380.0, 3600, 6000, (0.8, 1.35), False),
            ("SM_GrassClump_B", 110.0, 4500, 7500, (0.8, 1.4), True)):
        mesh = by_name.get(name)
        if mesh is None:
            warn("grass mesh %s missing" % name)
            continue
        for i in range(len(mesh.get_editor_property("static_materials"))):
            mesh.set_material(i, grass_mat)
        eal.save_loaded_asset(mesh)
        v = unreal.GrassVariety()
        v.set_editor_property("grass_mesh", mesh)
        d = unreal.PerPlatformFloat()
        d.set_editor_property("default", density)
        v.set_editor_property("grass_density", d)
        cs, ce = unreal.PerPlatformInt(), unreal.PerPlatformInt()
        cs.set_editor_property("default", cull_start)
        ce.set_editor_property("default", cull_end)
        v.set_editor_property("start_cull_distance", cs)
        v.set_editor_property("end_cull_distance", ce)
        v.set_editor_property("use_grid", True)
        v.set_editor_property("placement_jitter", 1.0)
        v.set_editor_property("random_rotation", True)
        v.set_editor_property("align_to_surface", True)
        v.set_editor_property("scaling", unreal.GrassScaling.UNIFORM)
        v.set_editor_property("scale_x", unreal.FloatInterval(scale[0], scale[1]))
        v.set_editor_property("cast_dynamic_shadow", shadows)
        try:
            v.set_editor_property("cast_contact_shadow", True)
        except Exception:
            pass
        varieties.append(v)
    lgt.set_editor_property("grass_varieties", varieties)
    eal.save_loaded_asset(lgt)
    log("grass type with %d varieties" % len(varieties))
    return lgt


# ----------------------------------------------------------------------------------------------
# Level
# ----------------------------------------------------------------------------------------------

actors_sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def spawn(cls, loc, rot=None, label=None, folder=None):
    a = actors_sub.spawn_actor_from_class(cls, unreal.Vector(*loc), rot or unreal.Rotator())
    if label:
        a.set_actor_label(label)
    if folder:
        a.set_folder_path(folder)
    return a


def spawn_mesh(mesh, loc, rot):
    a = actors_sub.spawn_actor_from_class(unreal.StaticMeshActor, loc, rot)
    smc = a.get_editor_property("static_mesh_component")
    smc.set_static_mesh(mesh)
    smc.set_mobility(unreal.ComponentMobility.STATIC)
    a.set_actor_label(mesh.get_name())
    return a


def place_mesh(mesh, x, y, yaw=0.0, scale=(1.0, 1.0, 1.0), sink=0.15, pitch=0.0, roll=0.0, z=None, folder="Dressing",
               label=None, collide=True):
    """Put the mesh's bottom-centre (raised by sink * height), measured after scale and rotation, at the
    ground point (x, y) (or at height z)."""
    if isinstance(scale, (int, float)):
        scale = (scale, scale, scale)
    b = mesh.get_bounding_box()
    rot = unreal.Rotator(roll=roll, pitch=pitch, yaw=yaw)
    q = rot.quaternion()
    corners = [q.rotate_vector(unreal.Vector(px * scale[0], py * scale[1], pz * scale[2]))
               for px in (b.min.x, b.max.x) for py in (b.min.y, b.max.y) for pz in (b.min.z, b.max.z)]
    lo_z = min(c.z for c in corners)
    hi_z = max(c.z for c in corners)
    centre = q.rotate_vector(unreal.Vector((b.min.x + b.max.x) * 0.5 * scale[0], (b.min.y + b.max.y) * 0.5 * scale[1],
                                           (b.min.z + b.max.z) * 0.5 * scale[2]))
    gz = height(x, y) if z is None else z
    a = spawn_mesh(mesh, unreal.Vector(x - centre.x, y - centre.y, gz - (lo_z + sink * (hi_z - lo_z))), rot)
    a.set_actor_scale3d(unreal.Vector(*scale))
    a.set_folder_path(folder)
    if label:
        a.set_actor_label(label)
    if not collide:
        a.get_editor_property("static_mesh_component").set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    return a


def setp(obj, name, value):
    try:
        obj.set_editor_property(name, value)
    except Exception as exc:
        warn("%s.%s: %s" % (obj.get_class().get_name(), name, exc))


def build_sky():
    folder = "Sky"
    sun = spawn(unreal.DirectionalLight, (0, 0, 5000), unreal.Rotator(roll=0, pitch=-19.0, yaw=150.0), "Sun", folder)
    lc = sun.get_editor_property("light_component")
    lc.set_mobility(unreal.ComponentMobility.MOVABLE)
    setp(lc, "intensity", 10.0)
    setp(lc, "use_temperature", True)
    setp(lc, "temperature", 5700.0)
    setp(lc, "atmosphere_sun_light", True)
    setp(lc, "cast_cloud_shadows", True)
    setp(lc, "enable_light_shaft_bloom", True)
    setp(lc, "bloom_scale", 0.12)
    setp(lc, "light_source_angle", 1.2)
    for prop, value in (("cast_shadows_on_clouds", True), ("cloud_scattered_luminance_scale", unreal.LinearColor(1, 1, 1, 1))):
        try:
            setp(lc, prop, value)
        except Exception:
            pass

    atm = spawn(unreal.SkyAtmosphere, (0, 0, 0), None, "SkyAtmosphere", folder)
    ac = atm.get_component_by_class(unreal.SkyAtmosphereComponent)
    setp(ac, "mie_scattering_scale", 0.008)
    setp(ac, "mie_anisotropy", 0.8)
    setp(ac, "rayleigh_exponential_distribution", 9.0)

    sky = spawn(unreal.SkyLight, (0, 0, 3000), None, "SkyLight", folder)
    sc = sky.get_editor_property("light_component")
    sc.set_mobility(unreal.ComponentMobility.MOVABLE)
    setp(sc, "real_time_capture", True)
    setp(sc, "intensity", 1.15)

    cloud = spawn(unreal.VolumetricCloud, (0, 0, 0), None, "VolumetricCloud", folder)
    cc = cloud.get_component_by_class(unreal.VolumetricCloudComponent)
    setp(cc, "layer_bottom_altitude", 1.4)
    setp(cc, "layer_height", 5.0)
    base = unreal.load_object(None, "/Engine/EngineSky/VolumetricClouds/m_SimpleVolumetricCloud.m_SimpleVolumetricCloud")
    if base and CLOUDS:
        # Heavier Atlantic cover than the engine default (coverage -0.2, density 0.008).
        path = ROOT + "/Materials/MI_IslandClouds"
        mi = eal.load_asset(path) if eal.does_asset_exist(path) else tools.create_asset(
            "MI_IslandClouds", ROOT + "/Materials", unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
        mel.set_material_instance_parent(mi, base)
        for name, value in CLOUDS.items():
            mel.set_material_instance_scalar_parameter_value(mi, name, value)
        eal.save_loaded_asset(mi)
        setp(cc, "material", mi)
    elif not base:
        warn("cloud base material not found")

    fog = spawn(unreal.ExponentialHeightFog, (0, 0, -200), None, "HeightFog", folder)
    fc = fog.get_editor_property("component")
    setp(fc, "fog_density", 0.022)
    setp(fc, "fog_height_falloff", 0.09)
    setp(fc, "fog_inscattering_luminance", unreal.LinearColor(0.24, 0.30, 0.37, 1.0))
    setp(fc, "directional_inscattering_luminance", unreal.LinearColor(0.42, 0.36, 0.3, 1.0))
    setp(fc, "directional_inscattering_exponent", 6.0)
    setp(fc, "start_distance", 2000.0)
    setp(fc, "enable_volumetric_fog", True)
    setp(fc, "volumetric_fog_scattering_distribution", 0.65)
    setp(fc, "volumetric_fog_extinction_scale", 1.6)
    setp(fc, "volumetric_fog_albedo", unreal.Color(r=215, g=225, b=235, a=255))
    try:
        sec = fc.get_editor_property("second_fog_data")
        sec.set_editor_property("fog_density", 0.06)
        sec.set_editor_property("fog_height_falloff", 0.6)
        sec.set_editor_property("fog_height_offset", -300.0)
        setp(fc, "second_fog_data", sec)
    except Exception as exc:
        log("second fog: %s" % exc)

    ppv = spawn(unreal.PostProcessVolume, (0, 0, 0), None, "PostProcess", folder)
    ppv.set_editor_property("unbound", True)
    s = ppv.get_editor_property("settings")

    def ov(name, value):

        try:
            s.set_editor_property("override_" + name, True)
            s.set_editor_property(name, value)
        except Exception as exc:
            warn("post %s: %s" % (name, exc))

    # Fixed exposure: the whole map is one outdoor lighting condition, so no adaptation surprises.
    ov("auto_exposure_method", unreal.AutoExposureMethod.AEM_MANUAL)
    ov("auto_exposure_apply_physical_camera_exposure", False)  # EV100 0 + bias, like the template levels
    ov("auto_exposure_bias", EXPOSURE_BIAS)
    ov("bloom_intensity", 0.45)
    ov("bloom_threshold", -1.0)
    ov("vignette_intensity", 0.38)
    ov("film_grain_intensity", 0.08)
    ov("color_saturation", unreal.Vector4(0.88, 0.9, 0.92, 1.0))
    ov("color_contrast", unreal.Vector4(1.08, 1.08, 1.08, 1.0))
    ov("color_gain_shadows", unreal.Vector4(0.92, 0.98, 1.06, 1.0))
    ov("color_gain_highlights", unreal.Vector4(1.04, 1.0, 0.95, 1.0))
    ov("scene_fringe_intensity", 0.25)
    ov("lumen_scene_lighting_quality", 1.0)
    ppv.set_editor_property("settings", s)
    return sun


EXPOSURE_BIAS = 0.0  # manual exposure compensation (EV)
CLOUDS = {}  # MI_IslandClouds overrides for m_SimpleVolumetricCloud; empty = engine default clouds (a denser test lost them)


def build_ocean(mat):
    plane = eal.load_asset("/Engine/BasicShapes/Plane")
    a = spawn_mesh(plane, unreal.Vector(0, 0, 0), unreal.Rotator())
    a.set_actor_label("Ocean")
    a.set_folder_path("Ocean")
    a.set_actor_scale3d(unreal.Vector(40000.0, 40000.0, 1.0))
    smc = a.get_editor_property("static_mesh_component")
    smc.set_material(0, mat)
    smc.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    smc.set_editor_property("cast_shadow", False)
    return a


def build_sea_walls(start):
    """Invisible walls on the ~1.3 m depth line so nobody wades off into the open sea."""
    cube = eal.load_asset("/Engine/BasicShapes/Cube")
    n = 140
    count = 0
    for i in range(n):
        ang = 2 * math.pi * i / n
        dx, dy = math.cos(ang), math.sin(ang)
        r = 32000.0
        while r > 3000.0 and height(dx * r, dy * r) < -130.0:
            r -= 100.0
        x, y = dx * (r + 150.0), dy * (r + 150.0)
        seg = 2 * math.pi * (r + 150.0) / n * 1.6
        a = spawn_mesh(cube, unreal.Vector(x, y, 500.0), unreal.Rotator(yaw=math.degrees(ang)))
        a.set_actor_scale3d(unreal.Vector(1.0, seg / 100.0, 30.0))
        a.set_folder_path("SeaWalls")
        a.set_actor_hidden_in_game(True)
        smc = a.get_editor_property("static_mesh_component")
        smc.set_collision_profile_name("InvisibleWall")
        smc.set_editor_property("cast_shadow", False)
        smc.set_visibility(False)
        count += 1
    log("sea walls: %d" % count)


def pick(models, key, rng):
    meshes = models[key]
    return meshes[rng.randrange(len(meshes))]


def clear_of(x, y, keep_out):
    for kx, ky, kr in keep_out:
        if (x - kx) ** 2 + (y - ky) ** 2 < kr * kr:
            return False
    return True


def dress(models, route, pads, start):
    rng = random.Random(1717)
    keep_out = [(HILL[0], HILL[1], ARENA_RADIUS + 300.0), (start[0], start[1], 1800.0)] + [(x, y, r + 300.0) for x, y, r in pads]
    path_clear = 700.0
    rocks = models["rock_moss_set_01"] + models["rock_moss_set_02"] + models["boulder_01"]
    big = models["rock_face_02"] + models["mountainside"]
    outcrop = models["rock_face_02"] + models["boulder_01"]
    count = {"boulders": 0, "outcrops": 0, "coast": 0, "cliff": 0, "drift": 0}

    # Boulder clusters across the interior.
    for _ in range(75):
        cx, cy = rng.uniform(-19000, 19000), rng.uniform(-16000, 16000)
        if height(cx, cy) < 350.0:
            continue
        for _ in range(rng.randint(2, 6)):
            x, y = cx + rng.gauss(0, 500), cy + rng.gauss(0, 500)
            if height(x, y) < 300.0 or LIB.get_path_distance(x, y) < path_clear or not clear_of(x, y, keep_out):
                continue
            s = rng.uniform(0.5, 1.5)
            place_mesh(rocks[rng.randrange(len(rocks))], x, y, yaw=rng.uniform(0, 360), scale=s, sink=rng.uniform(0.2, 0.4),
                       pitch=rng.uniform(-8, 8), roll=rng.uniform(-8, 8), folder="Dressing/Boulders")
            count["boulders"] += 1

    # Outcrops on steep ground (cover stretched landscape) and lone big rocks on the moor.
    for gy in range(-17000, 17001, 900):
        for gx in range(-21000, 21001, 900):
            x, y = gx + rng.uniform(-300, 300), gy + rng.uniform(-300, 300)
            h = height(x, y)
            if h < 250.0 or not clear_of(x, y, keep_out) or LIB.get_path_distance(x, y) < path_clear + 400:
                continue
            nz = normal_z(x, y)
            if nz < 0.74 and rng.random() < 0.4:
                place_mesh(outcrop[rng.randrange(len(outcrop))], x, y, yaw=rng.uniform(0, 360), scale=rng.uniform(1.0, 1.9),
                           sink=0.4, pitch=rng.uniform(-10, 10), folder="Dressing/Outcrops")
                count["outcrops"] += 1

    # Shoreline rocks (not on the start beach) and cliffs on the cliff coast.
    cdx, cdy = CLIFF_DIR
    for i in range(440):
        ang = 2 * math.pi * i / 440 + rng.uniform(-0.005, 0.005)
        dx, dy = math.cos(ang), math.sin(ang)
        r = 30000.0
        while r > 4000.0 and height(dx * r, dy * r) < 0.0:
            r -= 100.0
        x, y = dx * r, dy * r
        facing = dx * cdx + dy * cdy
        if (x - start[0]) ** 2 + (y - start[1]) ** 2 < 6000.0 ** 2:
            continue
        if facing > 0.38:
            # Clad the cliff face: big rock masses at the foot, smaller slabs higher up the face.
            rr = r + 600.0
            while rr > 4000.0 and height(dx * rr, dy * rr) < 700.0:
                rr -= 100.0
            fx, fy = dx * (rr + 250.0), dy * (rr + 250.0)
            top = height(dx * (rr - 800.0), dy * (rr - 800.0))
            if i % 2 == 0:
                mesh = models["mountainside"][0] if rng.random() < 0.65 else models["rock_face_02"][0]
                s = rng.uniform(1.9, 2.7) if mesh is models["mountainside"][0] else rng.uniform(6.0, 9.0)
                place_mesh(mesh, fx - dx * 150.0, fy - dy * 150.0, yaw=rng.uniform(0, 360), scale=s, sink=0.0, z=-1100.0,
                           pitch=rng.uniform(-8, 8), folder="Dressing/Cliffs")
                count["cliff"] += 1
            if i % 3 == 0 and top > 900.0:
                place_mesh(models["rock_face_02"][0], dx * (rr + 50.0), dy * (rr + 50.0), yaw=rng.uniform(0, 360),
                           scale=rng.uniform(2.0, 3.2), sink=0.0, z=top - rng.uniform(500.0, 900.0), pitch=rng.uniform(-15, 15),
                           roll=rng.uniform(-15, 15), folder="Dressing/Cliffs")
                count["cliff"] += 1
        elif rng.random() < 0.3:
            for _ in range(rng.randint(1, 3)):
                ox, oy = rng.gauss(0, 350), rng.gauss(0, 350)
                place_mesh(rocks[rng.randrange(len(rocks))], x + ox, y + oy, yaw=rng.uniform(0, 360),
                           scale=rng.uniform(0.7, 2.2), sink=0.3, folder="Dressing/Shore")
                count["coast"] += 1

    # Sea stacks off the cliff coast and skerries elsewhere: the Hebridean silhouette.
    for k in range(9):
        ang = math.atan2(cdy, cdx) + rng.uniform(-0.75, 0.75)
        dx, dy = math.cos(ang), math.sin(ang)
        r = 30000.0
        while r > 4000.0 and height(dx * r, dy * r) < 0.0:
            r -= 100.0
        r += rng.uniform(1800.0, 5000.0)
        mesh = models["mountainside"][0] if k % 3 else models["rock_face_02"][0]
        s = rng.uniform(1.1, 2.0) if mesh is models["mountainside"][0] else rng.uniform(4.0, 6.5)
        place_mesh(mesh, dx * r, dy * r, yaw=rng.uniform(0, 360), scale=s, sink=0.0, z=-1400.0, folder="Dressing/SeaStacks")
        count["cliff"] += 1
    for k in range(14):
        ang = rng.uniform(0, 2 * math.pi)
        dx, dy = math.cos(ang), math.sin(ang)
        if dx * BEACH_DIR[0] + dy * BEACH_DIR[1] > 0.75:
            continue
        r = 30000.0
        while r > 4000.0 and height(dx * r, dy * r) < -150.0:
            r -= 100.0
        for _ in range(rng.randint(2, 4)):
            rr = r + rng.uniform(300.0, 2200.0)
            place_mesh(rocks[rng.randrange(len(rocks))], dx * rr + rng.gauss(0, 300), dy * rr + rng.gauss(0, 300),
                       yaw=rng.uniform(0, 360), scale=rng.uniform(1.5, 3.2), sink=0.0, z=-150.0, folder="Dressing/Skerries")
            count["coast"] += 1

    # Driftwood on the start beach.
    for i in range(4):
        ang = math.atan2(start[1], start[0]) + rng.uniform(-0.25, 0.25)
        r = math.hypot(*start) + rng.uniform(-400, 900)
        x, y = math.cos(ang) * r, math.sin(ang) * r
        if (x - start[0]) ** 2 + (y - start[1]) ** 2 < 500.0 ** 2:
            continue
        place_mesh(models["dead_tree_trunk"][0], x, y, yaw=rng.uniform(0, 360), scale=rng.uniform(0.9, 1.4), sink=0.25,
                   folder="Dressing/Driftwood")
        count["drift"] += 1

    log("dressing: %s" % count)


def fort(models, name):
    for m in models["modular_fort_01"]:
        if m.get_name().endswith(name):
            return m
    warn("fort piece %s missing" % name)
    return models["modular_fort_01"][0]


def build_ruins(models, route, pads):
    rng = random.Random(99)
    s = 0.62

    def ruin(name, x, y, yaw, sink=0.12, tilt=3.0, scale=s):
        return place_mesh(fort(models, name), x, y, yaw=yaw, scale=scale, sink=sink, pitch=rng.uniform(-tilt, tilt),
                          roll=rng.uniform(-tilt, tilt), folder="Ruins")

    def around(pad, t, pieces):
        x, y, r = pad
        (px, py), (dx, dy) = route.at(t)
        base = math.degrees(math.atan2(dy, dx))
        for name, ang, dist, yaw_off, sink in pieces:
            a = math.radians(base + ang)
            ruin(name, x + math.cos(a) * r * dist, y + math.sin(a) * r * dist, base + ang + yaw_off, sink=sink)

    # A: broken gatehouse wall the path passes beside.
    around(pads[0], 0.25, [("wall_thin_gate_01", 90, 0.15, 0, 0.1), ("wall_thin_straight_03", 90, 0.75, 0, 0.25),
                           ("wall_thin_corner_01", 180, 0.8, 30, 0.2), ("wall_thin_straight_04", 270, 0.7, 90, 0.35)])
    # B: the ruined keep - round tower and a courtyard of shattered thick walls.
    around(pads[1], 0.47, [("tower_round", 200, 0.55, 0, 0.18), ("wall_thick_corner_01", 120, 0.95, 0, 0.3),
                           ("wall_thick_end_01", 300, 0.9, 90, 0.25), ("wall_thick_straight_01", 40, 1.0, 90, 0.45),
                           ("wall_thick_end_02", 250, 1.05, 10, 0.3)])
    # C: a collapsed rampart with stairs.
    around(pads[2], 0.70, [("wall_stairs_straight_01", 90, 0.6, 0, 0.15), ("wall_walkway_straight_01", 140, 0.8, 90, 0.3),
                           ("wall_walkway_corner_01", 220, 0.85, 0, 0.25)])
    # Lone fragments along the way.
    for t, side in ((0.12, 1600.0), (0.36, -1700.0), (0.58, 1800.0), (0.86, -1500.0)):
        x, y = route.offset(t, side)
        ruin(rng.choice(["wall_thin_straight_03", "wall_thin_corner_02", "wall_thick_thin_transition_01"]), x, y,
             rng.uniform(0, 360), sink=0.3, tilt=5.0)
    log("ruins placed")


def build_stone_circle(models, route):
    rng = random.Random(4242)
    stone = models["rock_07"][0]
    hx, hy = HILL
    stones = 0
    for i in range(13):
        a = 2 * math.pi * i / 13 + rng.uniform(-0.05, 0.05)
        x, y = hx + math.cos(a) * RING_RADIUS, hy + math.sin(a) * RING_RADIUS
        tall = rng.uniform(17.0, 23.0)
        place_mesh(stone, x, y, yaw=math.degrees(a) + 90 + rng.uniform(-15, 15), roll=90.0 + rng.uniform(-4, 4),
                   pitch=rng.uniform(-3, 3), scale=(rng.uniform(7.0, 9.0), tall, rng.uniform(3.6, 4.6)), sink=0.06,
                   z=ARENA_HEIGHT - 60.0, folder="StoneCircle")
        stones += 1
    # Avenue of paired stones leading to the circle, and a couple of fallen ones.
    for k in range(5):
        t = 1.0 - (ARENA_RADIUS + 900.0 + k * 900.0) / route.length
        for side in (-650.0, 650.0):
            x, y = route.offset(t, side)
            place_mesh(stone, x, y, yaw=rng.uniform(0, 360), roll=90.0 + rng.uniform(-6, 6), sink=0.0,
                       scale=(rng.uniform(6.0, 8.0), rng.uniform(10.0, 14.0), rng.uniform(3.0, 3.8)), z=height(x, y) - 50.0,
                       folder="StoneCircle")
            stones += 1
    for ang in (0.9, 3.7):
        x, y = hx + math.cos(ang) * (ARENA_RADIUS + 700.0), hy + math.sin(ang) * (ARENA_RADIUS + 700.0)
        place_mesh(stone, x, y, yaw=rng.uniform(0, 360), scale=(8.0, 15.0, 4.0), sink=0.2, folder="StoneCircle")
        stones += 1
    log("standing stones: %d" % stones)


def target_point(x, y, tag, label, z_offset=10.0):
    tp = spawn(unreal.TargetPoint, (x, y, height(x, y) + z_offset), None, label, "Gameplay")
    tp.set_editor_property("tags", [unreal.Name(tag)])
    return tp


def clear_markers():
    """No prop may stand on a gameplay marker: remove any dressing whose bounds (grown by 1.5 m) cover one."""
    markers = [a for a in actors_sub.get_all_level_actors() if isinstance(a, (unreal.TargetPoint, unreal.PlayerStart))]
    removed = []
    for a in actors_sub.get_all_level_actors():
        if not isinstance(a, unreal.StaticMeshActor):
            continue
        folder = str(a.get_folder_path())
        if not folder.startswith(("Dressing", "Ruins", "StoneCircle")):
            continue
        origin, extent = a.get_actor_bounds(False)
        for m in markers:
            p = m.get_actor_location()
            if (abs(p.x - origin.x) < extent.x + 150.0 and abs(p.y - origin.y) < extent.y + 150.0
                    and p.z < origin.z + extent.z + 50.0 and p.z > origin.z - extent.z - 300.0):
                removed.append("%s near %s" % (a.get_actor_label(), m.get_actor_label()))
                actors_sub.destroy_actor(a)
                break
    log("marker clearance: removed %d props %s" % (len(removed), removed))


def build_markers(route, start):
    (sx, sy), (dx, dy) = route.at(0.0)
    yaw = math.degrees(math.atan2(dy, dx))
    ps = spawn(unreal.PlayerStart, (sx, sy, height(sx, sy) + 100.0), unreal.Rotator(yaw=yaw), "PlayerStart", "Gameplay")
    gx, gy = route.offset(0.035, 450.0)
    target_point(gx, gy, "Grace", "Grace_Beach")
    t_circle = 1.0 - (ARENA_RADIUS + 1100.0) / route.length
    gx, gy = route.offset(t_circle, 380.0)
    target_point(gx, gy, "Grace", "Grace_StoneCircle")
    enemies = [(0.21, -250.0), (0.23, 300.0), (0.44, 0.0), (0.47, 350.0), (0.50, -300.0), (0.64, 200.0), (0.67, -250.0),
               (0.80, 250.0), (0.83, -200.0)]
    for i, (t, side) in enumerate(enemies):
        x, y = route.offset(t, side)
        target_point(x, y, "Enemy", "Enemy_%02d" % (i + 1))
    target_point(HILL[0], HILL[1], "Boss", "Boss")
    clear_markers()
    log("markers: PlayerStart yaw %.0f, 2 graces, %d enemies, boss at (%.0f, %.0f, %.0f)" % (
        yaw, len(enemies), HILL[0], HILL[1], height(*HILL)))
    return ps


def write_shot_list(route, start):
    (sx, sy), (dx, dy) = route.at(0.0)
    lines = ["player player"]
    (mx, my), _ = route.at(0.40)
    (tx, ty), _ = route.at(0.56)
    lines.append(shot("route_mid", mx, my, height(mx, my) + 260.0, tx, ty, height(tx, ty) + 200.0))
    hx, hy = HILL
    (cx, cy) = route.offset(1.0 - (ARENA_RADIUS + 1600.0) / route.length, 600.0)
    lines.append(shot("stone_circle", cx, cy, height(cx, cy) + 450.0, hx, hy, ARENA_HEIGHT + 150.0, fov=80))
    lines.append(shot("overview", -26000.0, -24000.0, 13000.0, 1500.0, 1500.0, 0.0, fov=60))
    lines.append(shot("overview_cliffs", 30000.0, 21000.0, 9000.0, 2000.0, 2000.0, 600.0, fov=60))
    lines.append(shot("beach", sx - dy * 1500 - dx * 300, sy + dx * 1500 - dy * 300, height(sx, sy) + 220.0, sx + dx * 6000, sy + dy * 6000, 600.0))
    cdx, cdy = CLIFF_DIR
    lines.append(shot("cliffs_from_sea", cdx * 30000 - cdy * 9000, cdy * 30000 + cdx * 9000, 900.0, cdx * 16000, cdy * 16000, 1200.0, fov=65))
    lines.append(shot("arena_inside", hx - 1500.0, hy - 1200.0, ARENA_HEIGHT + 190.0, hx + 1500, hy + 1000, ARENA_HEIGHT + 200, fov=85))
    path = os.path.join(PROJECT, "Saved", "island_shots.txt")
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    log("shot list -> %s" % path)


def shot(name, x, y, z, tx, ty, tz, fov=75):
    dx, dy, dz = tx - x, ty - y, tz - z
    yaw = math.degrees(math.atan2(dy, dx))
    pitch = math.degrees(math.atan2(dz, math.hypot(dx, dy)))
    return "%s %.0f %.0f %.0f %.2f %.2f %.0f" % (name, x, y, z, pitch, yaw, fov)


def new_map():
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    ensure_dir("/Game/Souls/Maps")
    if eal.does_asset_exist(MAP):
        # Rebuild in place: deleting a map package from a commandlet is unreliable.
        if not les.load_level(MAP):
            raise RuntimeError("could not load " + MAP)
        def removable(a):
            # Keep only the world settings and the level's builder brush; volumes (post process...) are brushes too.
            return not isinstance(a, unreal.WorldSettings) and (isinstance(a, unreal.Volume) or not isinstance(a, unreal.Brush))

        doomed = [a for a in actors_sub.get_all_level_actors() if removable(a)]
        actors_sub.destroy_actors(doomed)
        left = [a for a in actors_sub.get_all_level_actors() if removable(a)]
        log("cleared existing %s: %d actors removed, %d left" % (MAP, len(doomed), len(left)))
        if left:
            raise RuntimeError("could not clear %s (%d actors left)" % (MAP, len(left)))
    else:
        try:
            ok = les.new_level(MAP, False)
        except TypeError:
            ok = les.new_level(MAP)
        if not ok:
            raise RuntimeError("new_level failed for " + MAP)
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    log("new level %s (world partition: %s)" % (MAP, world.get_world_settings().get_editor_property("world_partition") is not None
                                                 if hasattr(world.get_world_settings(), "world_partition") else "?"))
    return les, world


def build_everything(textures, models):
    route, pads = plan_layout()
    start = route.points[0]
    mask_tex = LIB.create_island_mask_texture(ROOT + "/Textures/Generated/T_IslandMask", 1024)
    eal.save_loaded_asset(mask_tex)
    GRASS_NOISE.append(textures["generated"]["T_IslandNoise"])
    grass_mat = build_grass_material()
    grass_type = build_grass_type(models, grass_mat)
    terrain_mat = build_terrain_material(textures, mask_tex, grass_type)
    apply_prop_materials(models, build_prop_master(textures))
    ocean_mat = build_ocean_material(textures, mask_tex)

    les, world = new_map()
    landscape = LIB.create_island_landscape(world, terrain_mat)
    if landscape is None:
        raise RuntimeError("landscape creation failed")
    landscape.set_folder_path("Terrain")
    log("landscape: extent %.0f cm, height at start %.0f, at hill %.0f" % (LIB.get_landscape_extent(), height(*start), height(*HILL)))

    build_sky()
    build_ocean(ocean_mat)
    build_sea_walls(start)
    dress(models, route, pads, start)
    build_ruins(models, route, pads)
    build_stone_circle(models, route)
    build_markers(route, start)
    write_shot_list(route, start)

    gm = unreal.load_class(None, GAME_MODE)
    if gm:
        world.get_world_settings().set_editor_property("default_game_mode", gm)
    else:
        warn("game mode %s not found (run build_souls_character.py first)" % GAME_MODE)
    les.save_current_level()
    eal.save_directory(ROOT, only_if_is_dirty=True, recursive=True)
    log("saved %s" % MAP)


def main():
    ensure_dir(ROOT)
    unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX true")
    textures = import_textures()
    models = import_models()
    if STAGE != "import":
        build_everything(textures, models)
    log("problems: %d" % len(problems))
    for p in problems:
        log("  - " + p)
    log("ISLAND_OK")


try:
    main()
except Exception as exc:
    import traceback
    log("FAILED: %s\n%s" % (exc, traceback.format_exc()))
