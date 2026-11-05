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
GRASS_OBJS = ("SM_GrassClump_A.obj", "SM_GrassClump_B.obj", "SM_GrassClump_C.obj", "SM_Marram.obj", "SM_Reeds.obj", "SM_Heather.obj")
MODELS = ["rock_moss_set_01", "rock_moss_set_02", "boulder_01", "rock_07", "rock_face_02", "mountainside",
          "modular_fort_01", "dead_tree_trunk",
          "island_tree_02", "shrub_02", "fern_02", "dandelion_01"]


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
                foliage = mid in ("fern_02", "shrub_02", "dandelion_01")
                nanite_and_collision(mesh, nanite=True, complex_collision=not foliage)
        alpha_files = [os.path.join(PH_MOD, mid, "textures", f) for f in os.listdir(os.path.join(PH_MOD, mid, "textures"))
                       if "alpha" in f.lower()]
        if alpha_files and (REIMPORT or not eal.does_directory_exist(dest + "/Alpha")):
            import_files(alpha_files, dest + "/Alpha")
            for tex_ in assets_of(dest + "/Alpha", unreal.Texture2D):
                tex_.set_editor_property("srgb", False)
                tex_.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
                eal.save_loaded_asset(tex_)
        meshes = assets_of(dest, unreal.StaticMesh)
        models[mid] = meshes
        for m in meshes:
            b = m.get_bounding_box()
            log("model %s: %s size=(%.0f, %.0f, %.0f) min=(%.0f, %.0f, %.0f) tris=%s" % (
                mid, m.get_name(), b.max.x - b.min.x, b.max.y - b.min.y, b.max.z - b.min.z, b.min.x, b.min.y, b.min.z,
                m.get_num_triangles(0) if hasattr(m, "get_num_triangles") else "?"))
    dest = "%s/Models/Grass" % ROOT
    if REIMPORT or len(assets_of(dest, unreal.StaticMesh)) < len(GRASS_OBJS) if eal.does_directory_exist(dest) else True:
        import_files([os.path.join(GEN, f) for f in GRASS_OBJS], dest)
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
MARSH_RADIUS = 6200.0
MARSH_LEVEL = 380.0   # marsh ground
POOL_LEVEL = 330.0    # marsh pool water surface
CLEARING_RADIUS = 2300.0  # the shadow boss arena (19-25 m), flat and open
ISLET_DIR = (-0.05, -1.0)


def v2(x, y):
    return unreal.Vector2D(x, y)


BIOMES = {}  # filled by plan_layout: marsh / clearing / wood centres, islet


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
    if BIOMES:
        lay.set_editor_property("marsh_center", v2(*BIOMES["marsh"]))
        lay.set_editor_property("marsh_radius", MARSH_RADIUS)
        lay.set_editor_property("marsh_level", MARSH_LEVEL)
        lay.set_editor_property("pool_level", POOL_LEVEL)
        lay.set_editor_property("clearing_center", v2(*BIOMES["clearing"]))
        lay.set_editor_property("clearing_radius", CLEARING_RADIUS)
        lay.set_editor_property("wood_center", v2(*BIOMES["wood"]))
        lay.set_editor_property("wood_radius", 5800.0)
        lay.set_editor_property("islets", [unreal.Vector(*BIOMES["islet"])])
        lay.set_editor_property("islet_height", 650.0)
    else:
        lay.set_editor_property("marsh_radius", 1.0)
        lay.set_editor_property("wood_radius", 1.0)
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
    # Biomes along the route: machair dunes (start) -> green woodland (west) -> ruined keep ->
    # peat marsh (south-east, the causeway skirts it) -> heather highland -> the stone circle.
    BIOMES["wood"] = route.offset(0.27, 5600.0)
    BIOMES["marsh"] = route.offset(0.60, -6800.0)
    mx, my = BIOMES["marsh"]
    (_, _), (rdx, rdy) = route.at(0.60)
    BIOMES["clearing"] = (mx + rdy * 2000.0, my - rdx * 2000.0)
    # Offshore islet: swim ~35 m from the south coast.
    ix, iy = ISLET_DIR
    n = math.hypot(ix, iy)
    ix, iy = ix / n, iy / n
    r = 6000.0
    while r < 30000.0 and height(ix * r, iy * r) > -100.0:
        r += 100.0
    rc = r + 5200.0
    BIOMES["islet"] = (ix * rc, iy * rc, 2100.0)
    cx, cy = BIOMES["clearing"]
    pads = [route.offset(0.25, 1500.0) + (1200.0,), route.offset(0.47, -1800.0) + (1700.0,),
            route.offset(0.70, 1500.0) + (1100.0,), (cx, cy, CLEARING_RADIUS),
            (ix * rc, iy * rc, 900.0)]
    LIB.set_island_layout(make_layout(pts, pads))
    log("biomes: %s" % {k: tuple(round(c) for c in v) for k, v in BIOMES.items()})
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


def build_terrain_material(tex, mask_tex, biome_tex, grass_types):
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
    # Biomes: lush woodland turf, purple-brown heather on the highland, dark peat and wet moss in the marsh.
    biome = g.tex(biome_tex, mask_uv(g, wp_xy, extent), "linear", clamp=True)
    marsh, wood, heather, pool = (biome, "R"), (biome, "G"), (biome, "B"), (biome, "A")
    grass_c = g.lerp(grass_c, g.mul(grass_c, (0.86, 1.12, 0.78)), g.mul(wood, 0.8))
    grass_c = g.lerp(grass_c, g.mul(heath, (0.72, 0.46, 0.56)), g.mul(heather, g.smooth(0.25, 0.6, g.add(mid, 0.2))))
    peat = g.lerp(g.mul(olive, (0.5, 0.48, 0.32)), (0.05, 0.04, 0.03), g.smooth(0.0, 0.25, g.add(pool, g.mul(g.sub(mid, 0.5), 0.3))))
    grass_c = g.lerp(grass_c, peat, marsh)
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
    sand_w = g.mul(g.one_minus(g.smooth(g.sub(sand_line, 60.0), sand_line, wz)), g.one_minus(marsh))
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
    base, fire_glow = fire_char(g, base, wp, strength=0.9, glow=0.8)  # scorched ground after a field fire
    if fire_glow is not None:
        g.out(fire_glow, E.MaterialProperty.MP_EMISSIVE_COLOR)
    normal = blend("normal", grass["normal"], trail["normal"], sand["normal"], rock["normal"])
    rough = blend("rough", g.maxx(grass["rough"], 0.82), g.maxx((trail["arm"], "G"), 0.65), (sand["arm"], "G"), (rock["arm"], "G"),
                  g.mul((sand["arm"], "G"), 0.45))
    ao = blend("ao", grass["ao"], (trail["arm"], "R"), (sand["arm"], "R"), (rock["arm"], "R"))

    base, rough = weather_wet(g, base, rough)  # rain-soaked: darker, glossier (UNR-T79)
    g.out(base, E.MaterialProperty.MP_BASE_COLOR)
    g.out(normal, E.MaterialProperty.MP_NORMAL)
    rough = g.lerp(rough, 0.28, g.mul(marsh, g.smooth(0.0, 0.15, g.add(pool, g.mul(g.sub(mid, 0.6), 0.4)))))
    g.out(rough, E.MaterialProperty.MP_ROUGHNESS)
    g.out(ao, E.MaterialProperty.MP_AMBIENT_OCCLUSION)
    g.out(g.lerp(0.22, 0.4, g.maxx(sand_a, rock_a)), E.MaterialProperty.MP_SPECULAR)

    # Procedural grass per biome, each driven by its own weight.
    if grass_types:
        not_path = g.one_minus(g.smooth(0.12, 0.42, path_w))
        land = g.mul(g.mul(not_path, g.one_minus(sand_a)), g.one_minus(rock_a))
        patchy = g.smooth(0.15, 0.45, g.add(mid, g.mul(macro, 0.3)))
        weights = {
            "LGT_IslandTurf": g.mul(g.mul(g.mul(land, patchy), g.one_minus(marsh)), g.one_minus(g.mul(heather, 0.85))),
            "LGT_Marram": g.mul(g.mul(g.mul(g.smooth(130.0, 210.0, wz), g.one_minus(g.smooth(380.0, 620.0, wz))),
                                      g.mul(g.one_minus(marsh), g.one_minus(rock_a))),
                                g.mul(not_path, g.smooth(0.35, 0.6, mid))),
            "LGT_Reeds": g.mul(g.mul(marsh, g.one_minus(g.smooth(0.4, 0.75, pool))),
                               g.mul(g.one_minus(path_a), g.smooth(0.3, 0.55, g.add(mid, g.mul(macro, 0.2))))),
            "LGT_Heather": g.mul(g.mul(heather, land), g.smooth(0.2, 0.45, g.add(mid, 0.1))),
        }
        go = g.new(E.MaterialExpressionLandscapeGrassOutput)
        inputs = []
        for name, lgt in grass_types.items():
            gi = unreal.GrassInput()
            gi.set_editor_property("name", name)
            gi.set_editor_property("grass_type", lgt)
            inputs.append(gi)
        go.set_editor_property("grass_types", inputs)
        for name in grass_types:
            g.link(weights[name], go, name)
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


GRASS_LOOKS = {
    # name: (root, tip A, tip B, dry-patch tint, sway cm)
    "M_GrassBlades": ((0.018, 0.030, 0.008), (0.11, 0.16, 0.035), (0.20, 0.21, 0.06), (0.17, 0.12, 0.07), 7.0),
    "M_Marram": ((0.05, 0.06, 0.03), (0.30, 0.33, 0.22), (0.42, 0.40, 0.26), (0.45, 0.38, 0.22), 11.0),
    "M_Reeds": ((0.03, 0.035, 0.015), (0.16, 0.17, 0.06), (0.26, 0.21, 0.09), (0.30, 0.22, 0.10), 9.0),
    "M_Heather": ((0.03, 0.02, 0.015), (0.16, 0.07, 0.12), (0.12, 0.09, 0.05), (0.20, 0.08, 0.17), 2.5),
}


def build_grass_material(name="M_GrassBlades"):
    root_c, tip_a, tip_b, dry_c, sway_cm = GRASS_LOOKS[name]
    mat, g = new_material(name, ROOT + "/Materials")
    mat.set_editor_property("shading_model", E.MaterialShadingModel.MSM_TWO_SIDED_FOLIAGE)
    mat.set_editor_property("two_sided", True)
    mat.set_editor_property("used_with_instanced_static_meshes", True)
    uv = g.new(E.MaterialExpressionTextureCoordinate)
    tip = g.sat(g.one_minus(g.mask(uv, g=True)))  # OBJ import flips V: root = 1
    rnd = g.new(E.MaterialExpressionPerInstanceRandom)
    wp = g.world()
    wp_xy = g.mask(wp, r=True, g=True)
    tip_c = g.lerp(tip_a, tip_b, rnd)
    patch = (g.tex(GRASS_NOISE[0], g.div(wp_xy, (9000.0, 9000.0)), "masks"), "B")
    tip_c = g.lerp(tip_c, dry_c, g.mul(g.smooth(0.56, 0.74, patch), 0.6))
    color = g.lerp(root_c, tip_c, g.op(E.MaterialExpressionPower, {"Base": tip, "Exp": 0.7}))
    # Wind: gusts travel across the island, blades sway by tip^2.
    t = weather_time(g)  # quickens in the gusts (UNR-T79)
    phase = g.add(g.mul(t, 1.7), g.add(g.div(g.mask(wp, r=True), 420.0), g.div(g.mask(wp, g=True), 650.0)))
    gust = g.add(g.mul(g.sine(phase), 0.5), g.mul(g.sine(g.add(g.mul(phase, 2.3), g.mul(rnd, 6.0))), 0.25))
    sway = g.mul(g.mul(g.mul(tip, tip), g.add(gust, 0.6)), sway_cm)
    wpo = weather_sway(g, sway)  # along the wind, scaled by its strength (UNR-T79)
    color, fire_glow, wpo = fire_grass(g, color, wp, tip, wpo)
    color, grass_rough = weather_wet(g, color, g.lerp(0.75, 0.5, tip), darken=0.8, gloss=0.3)  # UNR-T79
    g.out(color, E.MaterialProperty.MP_BASE_COLOR)
    if fire_glow is not None:
        g.out(fire_glow, E.MaterialProperty.MP_EMISSIVE_COLOR)
    g.out(grass_rough, E.MaterialProperty.MP_ROUGHNESS)
    g.out(0.25, E.MaterialProperty.MP_SPECULAR)
    g.out(g.mul(color, 0.6), E.MaterialProperty.MP_SUBSURFACE_COLOR)
    g.out(g.lerp(0.35, 1.0, tip), E.MaterialProperty.MP_AMBIENT_OCCLUSION)
    g.out(wpo, E.MaterialProperty.MP_WORLD_POSITION_OFFSET)
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
    color, fire_glow = fire_char(g, color, wp, strength=0.85, glow=0.6)  # soot on trunks and stones
    color, rough = weather_wet(g, color, rough, gloss=0.2)  # rain-soaked (UNR-T79)
    g.out(color, E.MaterialProperty.MP_BASE_COLOR)
    if fire_glow is not None:
        g.out(fire_glow, E.MaterialProperty.MP_EMISSIVE_COLOR)
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
    "island_tree_02": ((0.9, 0.9, 0.9), 0.25, 1.0),
}

# Foliage: model -> texture prefix of its leaf material (gets M_IslandFoliage, masked by the fetched alpha map).
FOLIAGE = {"island_tree_02": "island_tree_02_leaves", "shrub_02": "shrub_02", "fern_02": "fern_02",
           "dandelion_01": "dandelion_01"}


def build_foliage_master(tex):
    mat, g = new_material("M_IslandFoliage", ROOT + "/Materials")
    mat.set_editor_property("blend_mode", E.BlendMode.BLEND_MASKED)
    mat.set_editor_property("shading_model", E.MaterialShadingModel.MSM_TWO_SIDED_FOLIAGE)
    mat.set_editor_property("two_sided", True)
    mat.set_editor_property("used_with_nanite", True)
    mat.set_editor_property("used_with_instanced_static_meshes", True)
    uv = g.new(E.MaterialExpressionTextureCoordinate)

    def tparam(name, default, kind):
        st = {"color": E.MaterialSamplerType.SAMPLERTYPE_COLOR, "normal": E.MaterialSamplerType.SAMPLERTYPE_NORMAL,
              "masks": E.MaterialSamplerType.SAMPLERTYPE_MASKS}[kind]
        e = g.new(E.MaterialExpressionTextureSampleParameter2D, parameter_name=name, texture=default, sampler_type=st,
                  sampler_source=E.SamplerSourceMode.SSM_WRAP_WORLD_GROUP_SETTINGS)
        g.link(uv, e, "UVs")
        return e

    base = tparam("Base", tex["seaside_rock"]["color"], "color")
    nrm = tparam("Normal", tex["seaside_rock"]["normal"], "normal")
    arm = tparam("ARM", tex["seaside_rock"]["arm"], "masks")
    alpha = tparam("Alpha", tex["seaside_rock"]["arm"], "masks")
    tint = g.new(E.MaterialExpressionVectorParameter, parameter_name="Tint", default_value=unreal.LinearColor(1, 1, 1, 1))
    color = g.mul((base, "RGB"), (tint, "RGB"))
    wp = g.world()
    color, leaf_rough = weather_wet(g, color, g.maxx((arm, "G"), 0.55), darken=0.75, gloss=0.3)  # UNR-T79
    color, fire_glow = fire_char(g, color, wp)
    g.out(color, E.MaterialProperty.MP_BASE_COLOR)
    if fire_glow is not None:
        g.out(fire_glow, E.MaterialProperty.MP_EMISSIVE_COLOR)
    g.out((nrm, "RGB"), E.MaterialProperty.MP_NORMAL)
    g.out(leaf_rough, E.MaterialProperty.MP_ROUGHNESS)
    g.out(g.mul(color, 0.5), E.MaterialProperty.MP_SUBSURFACE_COLOR)
    g.out(fire_dissolve(g, (alpha, "R"), uv, wp), E.MaterialProperty.MP_OPACITY_MASK)
    # Light wind sway growing with height above the plant's pivot.
    above = g.sat(g.div(g.sub(g.mask(wp, b=True), g.mask(g.new(E.MaterialExpressionObjectPositionWS), b=True)), 600.0))
    phase = g.add(g.mul(weather_time(g), 1.3), g.div(g.add(g.mask(wp, r=True), g.mask(wp, g=True)), 380.0))
    sway = g.mul(g.mul(g.sine(phase), above), 6.0)
    g.out(weather_sway(g, sway, 0.6), E.MaterialProperty.MP_WORLD_POSITION_OFFSET)  # UNR-T79
    try:
        mat.set_editor_property("max_world_position_offset_displacement", 20.0)
    except Exception:
        pass
    finish_material(mat)
    return mat


def apply_foliage_materials(models, master):
    for mid, prefix in FOLIAGE.items():
        meshes = models.get(mid) or []
        folder = "%s/Models/%s" % (ROOT, mid)
        maps = {}
        for t_ in assets_of(folder, unreal.Texture2D):
            n = t_.get_name()
            for key in ("diff", "nor_gl", "rough"):
                # Interchange may merge the alpha into the diffuse ("<prefix>_diff-<prefix>_alpha").
                if n == "%s_%s" % (prefix, key) or n.startswith("%s_%s-" % (prefix, key)):
                    maps[key] = t_
            if n.endswith("alpha_2k"):
                maps["alpha"] = t_
        if "alpha" not in maps or "diff" not in maps:
            warn("foliage %s: missing maps %s" % (mid, sorted(maps)))
            continue
        name = "MI_%s_foliage" % mid
        folder_mi = ROOT + "/Materials/Props"
        path = "%s/%s" % (folder_mi, name)
        mi = eal.load_asset(path) if eal.does_asset_exist(path) else \
            tools.create_asset(name, folder_mi, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
        mel.set_material_instance_parent(mi, master)
        for param, key in (("Base", "diff"), ("Normal", "nor_gl"), ("ARM", "rough"), ("Alpha", "alpha")):
            if key in maps:
                mel.set_material_instance_texture_parameter_value(mi, param, maps[key])
        mel.set_material_instance_vector_parameter_value(mi, "Tint", unreal.LinearColor(0.85, 0.92, 0.8, 1.0))
        eal.save_loaded_asset(mi)
        for mesh in meshes:
            for i, slot in enumerate(mesh.get_editor_property("static_materials")):
                slot_name = str(slot.get_editor_property("material_slot_name"))
                if slot_name.startswith(prefix) and (mid != "island_tree_02" or "leaves" in slot_name):
                    mesh.set_material(i, mi)
            eal.save_loaded_asset(mesh)
        log("foliage material %s on %d meshes" % (name, len(meshes)))


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


GRASS_TYPES = {
    # grass type: (material, [(mesh, density, cull start, cull end, scale range, dynamic shadows)])
    "LGT_IslandTurf": ("M_GrassBlades", [("SM_GrassClump_C", 520.0, 3200, 5200, (0.8, 1.3), False),
                                         ("SM_GrassClump_A", 380.0, 3600, 6000, (0.8, 1.35), False),
                                         ("SM_GrassClump_B", 110.0, 4500, 7500, (0.8, 1.4), True)]),
    "LGT_Marram": ("M_Marram", [("SM_Marram", 160.0, 4000, 7000, (0.7, 1.3), True)]),
    "LGT_Reeds": ("M_Reeds", [("SM_Reeds", 140.0, 4500, 8000, (0.7, 1.3), True)]),
    "LGT_Heather": ("M_Heather", [("SM_Heather", 300.0, 3500, 6000, (0.8, 1.5), False)]),
}


def build_grass_types(models):
    return {name: build_grass_type(models, build_grass_material(mat_name), name, varieties)
            for name, (mat_name, varieties) in GRASS_TYPES.items()}


def build_grass_type(models, grass_mat, type_name, variety_specs):
    path = ROOT + "/Foliage/" + type_name
    ensure_dir(ROOT + "/Foliage")
    lgt = eal.load_asset(path) if eal.does_asset_exist(path) else \
        tools.create_asset(type_name, ROOT + "/Foliage", unreal.LandscapeGrassType, None)
    by_name = {m.get_name(): m for m in models["grass"]}
    varieties = []
    for name, density, cull_start, cull_end, scale, shadows in variety_specs:
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
    log("grass type %s with %d varieties" % (type_name, len(varieties)))
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
    setp(lc, "intensity", 4.0)  # the low sun as built; at runtime IslandDayNight drives it (UNR-T39)
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
    place_day_night()
    return sun


EXPOSURE_BIAS = 0.0  # manual exposure compensation (EV)
CLOUDS = {}  # MI_IslandClouds overrides for m_SimpleVolumetricCloud; empty = engine default clouds (a denser test lost them)


# ----------------------------------------------------------------------------------------------
# Day/night cycle (UNR-T39): the IslandDayNight actor turns the sun, moon and stars at runtime
# ----------------------------------------------------------------------------------------------

STAR_DISTANCE = 8000000.0  # cm; must match AIslandDayNight::StarDistance


def build_star_material():
    """Unlit pin-prick stars: IslandDayNight drives Brightness; each star gets its own brightness and tint, and
    they fade out over the lowest few degrees above the horizon."""
    mat, g = new_material("M_IslandStars", ROOT + "/Materials")
    setp(mat, "shading_model", E.MaterialShadingModel.MSM_UNLIT)
    setp(mat, "used_with_instanced_static_meshes", True)
    bright = g.new(E.MaterialExpressionScalarParameter, parameter_name="Brightness", default_value=0.0)
    rnd = g.new(E.MaterialExpressionPerInstanceRandom)
    tint = g.lerp((0.72, 0.8, 1.0), (1.0, 0.9, 0.78), rnd)
    lum = g.mul(g.lerp(0.12, 1.0, g.mul(rnd, rnd)), bright)
    horizon = g.sat(g.div(g.mask(g.world(), b=True), STAR_DISTANCE * 0.12))
    g.out(g.mul(g.mul(tint, lum), horizon), E.MaterialProperty.MP_EMISSIVE_COLOR)
    finish_material(mat)
    return mat


def place_day_night():
    cls = getattr(unreal, "IslandDayNight", None)
    if cls is None:
        warn("IslandDayNight class missing (compile the C++ module first); no day/night cycle placed")
        return None
    dn = spawn(cls, (0, 0, 0), None, "DayNight", "Sky")
    star_mesh = unreal.load_asset("/Engine/BasicShapes/Sphere")
    if star_mesh is None:
        warn("day/night: /Engine/BasicShapes/Sphere not found, no stars")
    else:
        setp(dn, "star_mesh", star_mesh)
        setp(dn, "star_material", build_star_material())
    setp(dn, "star_distance", STAR_DISTANCE)
    log("day/night cycle placed (stars: %s)" % (star_mesh is not None))
    return dn


# ----------------------------------------------------------------------------------------------
# Weather (UNR-T79): AIslandWeather writes MPC_Weather at runtime. Wetness darkens and glosses the terrain, props,
# foliage and grass (weather_wet); the wind's direction, strength and gusts drive their sway (weather_time,
# weather_sway). It draws rain streaks, splash rings and blown leaves with the materials built here.
# ----------------------------------------------------------------------------------------------

WEATHER_MPC = ROOT + "/Materials/MPC_Weather"          # AIslandWeather loads it from here too
WEATHER_FX = ROOT + "/Materials/Weather"               # M_WeatherRain / M_WeatherSplash / M_WeatherLeaf (paths in IslandWeather.cpp)
WEATHER_SCALARS = (("Wetness", 0.0), ("Rain", 0.0), ("Overcast", 0.0), ("WindStrength", 1.0), ("Gust", 0.0),
                   ("WindTime", 0.0), ("RainBrightness", 0.5))
WEATHER_VECTORS = (("WindDir", (0.6, 0.8, 1.0, 0.0)), ("RainOffset", (0.0, 0.0, 0.0, 0.0)),
                   ("RainBox", (3000.0, 3000.0, 1400.0, 0.0)), ("RainSlant", (0.0, 0.0, 0.0, 0.0)),
                   ("RainStreak", (0.0, 27.5, 0.0, 0.0)))
_weather_mpc = []


def weather_mpc():
    """MPC_Weather, created on first use (kept as it is when its parameters already match)."""
    if not _weather_mpc:
        mpc = None
        try:
            folder = ROOT + "/Materials"
            ensure_dir(folder)
            mpc = eal.load_asset(WEATHER_MPC) if eal.does_asset_exist(WEATHER_MPC) else tools.create_asset(
                "MPC_Weather", folder, unreal.MaterialParameterCollection, unreal.MaterialParameterCollectionFactoryNew())
            want = ([n for n, _ in WEATHER_SCALARS], [n for n, _ in WEATHER_VECTORS])
            have = ([str(p.get_editor_property("parameter_name")) for p in mpc.get_editor_property("scalar_parameters")],
                    [str(p.get_editor_property("parameter_name")) for p in mpc.get_editor_property("vector_parameters")])
            if have != want:
                scalars, vectors = [], []
                for name, value in WEATHER_SCALARS:
                    p = unreal.CollectionScalarParameter()  # the constructor gives it a fresh id
                    p.set_editor_property("parameter_name", name)
                    p.set_editor_property("default_value", value)
                    scalars.append(p)
                for name, v in WEATHER_VECTORS:
                    p = unreal.CollectionVectorParameter()
                    p.set_editor_property("parameter_name", name)
                    p.set_editor_property("default_value", unreal.LinearColor(v[0], v[1], v[2], v[3]))
                    vectors.append(p)
                mpc.set_editor_property("scalar_parameters", scalars)
                mpc.set_editor_property("vector_parameters", vectors)
            eal.save_loaded_asset(mpc)
            log("weather MPC %s: %s" % (WEATHER_MPC, ", ".join(want[0] + want[1])))
        except Exception as exc:
            warn("weather MPC: %s (no wetness, rain or wind-driven sway)" % exc)
            mpc = None
        _weather_mpc.append(mpc)
    return _weather_mpc[0]


def weather_param(g, name):
    """MPC_Weather.<name> in this graph (float4 for the vectors), or None without the MPC."""
    mpc = weather_mpc()
    if mpc is None:
        return None
    return g.new(E.MaterialExpressionCollectionParameter, collection=mpc, parameter_name=name)


def weather_wet(g, color, rough, darken=0.6, gloss=0.15):
    """Rain-soaked surfaces: darker, and glossier (most on upward faces, where the water sits)."""
    wet = weather_param(g, "Wetness")
    if wet is None:
        return color, rough
    up = g.sat(g.mask(g.new(E.MaterialExpressionVertexNormalWS), b=True))
    color = g.mul(color, g.lerp(1.0, darken, wet))
    rough = g.lerp(rough, gloss, g.mul(wet, g.lerp(0.5, 1.0, up)))
    return color, rough


def weather_time(g):
    """The sway clock: Time, run faster in the gusts (AIslandWeather integrates WindTime)."""
    t = g.time()
    wt = weather_param(g, "WindTime")
    return t if wt is None else g.add(t, wt)


def weather_sway(g, sway, side=0.35):
    """Sway (cm, signed) laid along the wind and scaled by its strength, as a WPO vector."""
    d = weather_param(g, "WindDir")
    if d is None:
        return g.append(g.append(sway, g.mul(sway, side)), 0.0)
    s = g.mul(sway, weather_param(g, "WindStrength"))
    return g.append(g.append(g.mul(s, g.mask(d, r=True)), g.mul(s, g.mask(d, g=True))), 0.0)


def build_rain_material():
    """Rain streaks: thin cylinders AIslandWeather scatters through a box. The material wraps each one round the
    camera by world position (so the rain stays put in the world as you move), drops it with RainOffset, slants
    it along the wind, and shows the fraction of drops the Rain amount asks for."""
    mat, g = new_material("M_WeatherRain", WEATHER_FX)
    setp(mat, "shading_model", E.MaterialShadingModel.MSM_UNLIT)
    setp(mat, "blend_mode", E.BlendMode.BLEND_TRANSLUCENT)
    setp(mat, "two_sided", True)
    setp(mat, "used_with_instanced_static_meshes", True)
    # The drops are moved far from their instances (wrapped round the camera): the GPU culls instances by
    # their bounds, so grow them by the whole rain box or almost every drop is culled before it moves.
    setp(mat, "max_world_position_offset_displacement", 60000.0)

    def rgb(name):
        return g.mask(weather_param(g, name), r=True, g=True, b=True)

    box = rgb("RainBox")
    # Each drop's own origin: its local (0,0,0) in world space. (Object Position is the whole component's
    # position on an instanced mesh, so every drop got the same shift and the box slid off screen.)
    pivot = g.op(E.MaterialExpressionTransformPosition, {"": (0.0, 0.0, 0.0)},
                 transform_source_type=E.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_LOCAL,
                 transform_type=E.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_WORLD)
    cam = g.new(E.MaterialExpressionCameraPositionWS)
    d = g.sub(pivot, cam)
    rel = g.add(d, rgb("RainOffset"))
    wrapped = g.mul(g.sub(g.op(E.MaterialExpressionFrac, {"": g.add(g.div(rel, box), (0.5, 0.5, 0.5))}), (0.5, 0.5, 0.5)), box)
    local = g.sub(g.world(), pivot)
    vz = g.mask(local, b=True)
    slant = g.mask(weather_param(g, "RainSlant"), r=True, g=True)
    wpo = g.add(g.sub(wrapped, d), g.append(g.mul(slant, vz), 0.0))
    vis = g.sat(g.mul(g.sub(weather_param(g, "Rain"), g.new(E.MaterialExpressionPerInstanceRandom)), 40.0))
    wpo = g.sub(wpo, g.mul(local, g.one_minus(vis)))  # the drops not falling collapse to a point
    g.out(wpo, E.MaterialProperty.MP_WORLD_POSITION_OFFSET)

    along = 1.0
    try:
        raw = g.new(E.MaterialExpressionWorldPosition,
                    world_position_shader_offset=E.WorldPositionIncludedOffsets.WPT_EXCLUDE_ALL_SHADER_OFFSETS)
        streak = weather_param(g, "RainStreak")
        pz = g.mask(g.sub(raw, pivot), b=True)
        along = g.sat(g.mul(g.one_minus(g.sat(g.div(g.abs(g.sub(pz, g.mask(streak, r=True))), g.mask(streak, g=True)))), 2.5))
    except Exception as exc:
        warn("rain streak taper: %s" % exc)
    near = g.smooth(30.0, 160.0, g.depth())
    flat = g.mask(g.sub(g.world(), cam), r=True, g=True)
    dist = g.op(E.MaterialExpressionSquareRoot, {"": g.dot(flat, flat)})
    half = g.mul(g.mask(weather_param(g, "RainBox"), r=True), 0.5)
    far = g.one_minus(g.smooth(g.mul(half, 0.6), half, dist))
    g.out(g.mul(g.mul(g.mul(near, far), along), 0.55), E.MaterialProperty.MP_OPACITY)
    g.out(g.mul((0.78, 0.82, 0.88), weather_param(g, "RainBrightness")), E.MaterialProperty.MP_EMISSIVE_COLOR)
    finish_material(mat)
    return mat


def build_weather_splash_material():
    """Where a drop lands: a bright fleck and a ring spreading out and fading (reads as a ripple on water).
    Per-instance custom data 0 is the splash's age, 0..1."""
    mat, g = new_material("M_WeatherSplash", WEATHER_FX)
    setp(mat, "shading_model", E.MaterialShadingModel.MSM_UNLIT)
    setp(mat, "blend_mode", E.BlendMode.BLEND_TRANSLUCENT)
    setp(mat, "two_sided", True)
    setp(mat, "used_with_instanced_static_meshes", True)
    c = g.sub(g.new(E.MaterialExpressionTextureCoordinate), (0.5, 0.5))
    r = g.mul(g.op(E.MaterialExpressionSquareRoot, {"": g.dot(c, c)}), 2.0)
    age = g.new(E.MaterialExpressionPerInstanceCustomData, data_index=0, const_default_value=1.0)
    ring = g.one_minus(g.smooth(0.0, 0.1, g.abs(g.sub(r, g.add(0.12, g.mul(age, 0.85))))))
    ring = g.mul(ring, g.one_minus(age))
    fleck = g.mul(g.one_minus(g.smooth(0.0, 0.2, r)), g.one_minus(g.smooth(0.0, 0.35, age)))
    alpha = g.mul(g.sat(g.add(g.mul(ring, 0.6), g.mul(fleck, 0.8))), g.one_minus(g.smooth(0.9, 1.0, r)))
    g.out(alpha, E.MaterialProperty.MP_OPACITY)
    g.out(g.mul((1.04, 1.09, 1.17), weather_param(g, "RainBrightness")), E.MaterialProperty.MP_EMISSIVE_COLOR)
    finish_material(mat)
    return mat


def build_leaf_material():
    """A blown leaf on a quad: pointed oval cut out of the UVs, a midrib, green to autumn brown per instance."""
    mat, g = new_material("M_WeatherLeaf", WEATHER_FX)
    setp(mat, "blend_mode", E.BlendMode.BLEND_MASKED)
    setp(mat, "shading_model", E.MaterialShadingModel.MSM_TWO_SIDED_FOLIAGE)
    setp(mat, "two_sided", True)
    setp(mat, "used_with_instanced_static_meshes", True)
    uv = g.new(E.MaterialExpressionTextureCoordinate)
    x = g.abs(g.mul(g.sub(g.mask(uv, r=True), 0.5), 2.0))
    y = g.mask(uv, g=True)
    width = g.mul(g.op(E.MaterialExpressionPower, {"Base": g.sat(g.sine(g.mul(y, 3.14159265))), "Exp": 0.75}), 0.85)
    g.out(g.sat(g.mul(g.sub(width, x), 25.0)), E.MaterialProperty.MP_OPACITY_MASK)
    rnd = g.new(E.MaterialExpressionPerInstanceRandom)
    color = g.lerp(g.lerp((0.09, 0.15, 0.03), (0.32, 0.28, 0.05), g.sat(g.mul(rnd, 2.0))), (0.24, 0.11, 0.035),
                   g.sat(g.sub(g.mul(rnd, 2.0), 1.0)))
    color = g.mul(color, g.lerp(0.65, 1.0, g.smooth(0.0, 0.07, x)))
    color, rough = weather_wet(g, color, 0.6, darken=0.8, gloss=0.35)
    g.out(color, E.MaterialProperty.MP_BASE_COLOR)
    g.out(rough, E.MaterialProperty.MP_ROUGHNESS)
    g.out(g.mul(color, 0.7), E.MaterialProperty.MP_SUBSURFACE_COLOR)
    finish_material(mat)
    return mat


def build_weather():
    """UNR-T79: the weather's materials and the IslandWeather actor (called once, after build_sky)."""
    mats = (None, None, None)
    if weather_mpc() is not None:
        try:
            ensure_dir(WEATHER_FX)
            mats = (build_rain_material(), build_weather_splash_material(), build_leaf_material())
        except Exception as exc:
            warn("weather materials: %s" % exc)
    cls = getattr(unreal, "IslandWeather", None)
    if cls is None:
        warn("IslandWeather class missing (compile the C++ module first); no weather placed")
        return None
    w = spawn(cls, (0, 0, 0), None, "Weather", "Sky")
    if weather_mpc() is not None:
        setp(w, "parameter_collection", weather_mpc())
    for prop, mat in zip(("rain_material", "splash_material", "leaf_material"), mats):
        if mat is not None:
            setp(w, prop, mat)
    log("weather placed (MPC %s, materials %s)" % (weather_mpc() is not None, [m is not None for m in mats]))
    return w


def build_ocean(mat):
    plane = eal.load_asset("/Engine/BasicShapes/Plane")
    a = spawn_mesh(plane, unreal.Vector(0, 0, 0), unreal.Rotator())
    a.set_actor_label("Ocean")
    a.set_folder_path("Ocean")
    a.set_actor_scale3d(unreal.Vector(40000.0, 40000.0, 1.0))
    smc = a.get_editor_property("static_mesh_component")
    smc.set_material(0, mat)
    smc.set_collision_profile_name("NoCollision")  # swim through it (the enabled flag alone didn't persist)
    smc.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    smc.set_editor_property("cast_shadow", False)
    return a


def build_sea_walls(start):
    """Invisible walls in a ring well out at sea (beyond the islet), so the open sea is swimmable but bounded."""
    cube = eal.load_asset("/Engine/BasicShapes/Cube")
    n = 96
    radius = SEA_WALL_RADIUS
    seg = 2 * math.pi * radius / n * 1.15
    for i in range(n):
        ang = 2 * math.pi * i / n
        a = spawn_mesh(cube, unreal.Vector(math.cos(ang) * radius, math.sin(ang) * radius, -1000.0), unreal.Rotator(yaw=math.degrees(ang)))
        a.set_actor_scale3d(unreal.Vector(2.0, seg / 100.0, 40.0))
        a.set_folder_path("SeaWalls")
        a.set_actor_hidden_in_game(True)
        smc = a.get_editor_property("static_mesh_component")
        smc.set_collision_profile_name("InvisibleWall")
        smc.set_editor_property("cast_shadow", False)
        smc.set_visibility(False)
    log("sea walls: %d at r=%.0f m" % (n, radius / 100.0))


SEA_WALL_RADIUS = 29000.0


def build_water(world, tex, mask_tex):
    """Swimmable water: one PhysicsVolume over the whole sea, one over the marsh pools, plus the pool surface."""
    sea = LIB.create_water_volume(world, unreal.Vector(0.0, 0.0, -3000.0), unreal.Vector(SEA_WALL_RADIUS + 2000.0, SEA_WALL_RADIUS + 2000.0, 3000.0), "SeaWater")
    mx, my = BIOMES["marsh"]
    marsh = LIB.create_water_volume(world, unreal.Vector(mx, my, POOL_LEVEL - 250.0), unreal.Vector(MARSH_RADIUS * 0.8, MARSH_RADIUS * 0.8, 250.0), "MarshWater")
    for v in (sea, marsh):
        if v is None:
            warn("water volume not created")
        else:
            v.set_folder_path("Water")
    leaks = sum(1 for gx in range(-10, 11) for gy in range(-10, 11)
                if height(mx + gx * MARSH_RADIUS * 0.08, my + gy * MARSH_RADIUS * 0.08) < POOL_LEVEL
                and biome(mx + gx * MARSH_RADIUS * 0.08, my + gy * MARSH_RADIUS * 0.08)[0] < 0.5)
    if leaks:
        warn("marsh water box covers %d low non-marsh samples" % leaks)
    pool_mat = build_pool_material(tex)
    plane = eal.load_asset("/Engine/BasicShapes/Plane")
    a = spawn_mesh(plane, unreal.Vector(mx, my, POOL_LEVEL), unreal.Rotator())
    a.set_actor_label("MarshPools")
    a.set_folder_path("Water")
    a.set_actor_scale3d(unreal.Vector(MARSH_RADIUS * 1.6 / 100.0, MARSH_RADIUS * 1.6 / 100.0, 1.0))
    smc = a.get_editor_property("static_mesh_component")
    smc.set_material(0, pool_mat)
    smc.set_collision_profile_name("NoCollision")
    smc.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    smc.set_editor_property("cast_shadow", False)
    # Low mist hanging over the fen.
    try:
        fog = spawn(unreal.LocalFogVolume, (mx, my, MARSH_LEVEL + 50.0), None, "MarshMist", "Water")
        # LocalFogVolume is a 10 m-radius unit sphere scaled by the actor.
        fog.set_actor_scale3d(unreal.Vector(MARSH_RADIUS * 0.8 / 1000.0, MARSH_RADIUS * 0.8 / 1000.0, 0.25))
        fc = fog.get_component_by_class(unreal.LocalFogVolumeComponent)
        setp(fc, "radial_fog_extinction", 0.08)
        setp(fc, "height_fog_extinction", 0.18)
        setp(fc, "height_fog_falloff", 350.0)
        setp(fc, "fog_albedo", unreal.LinearColor(0.75, 0.8, 0.82, 1.0))
    except Exception as exc:
        warn("marsh mist: %s" % exc)
    log("water: sea + marsh volumes, pool surface at %.0f" % POOL_LEVEL)


def build_pool_material(tex):
    mat, g = new_material("M_PeatPool", ROOT + "/Materials")
    mat.set_editor_property("shading_model", E.MaterialShadingModel.MSM_SINGLE_LAYER_WATER)
    wp_xy = g.mask(g.world(), r=True, g=True)
    t = g.time()
    nrm = tex["generated"]["T_WaterNormal"]
    n1 = g.tex(nrm, g.add(g.div(wp_xy, (900.0, 900.0)), g.mul(t, (0.004, 0.002))), "normal")
    n2 = g.tex(nrm, g.add(g.div(wp_xy, (310.0, 310.0)), g.mul(t, (-0.006, 0.004))), "normal")
    xy = g.mul(g.add(g.mask((n1, "RGB"), r=True, g=True), g.mask((n2, "RGB"), r=True, g=True)), 0.12)
    g.out(g.normalize(g.append(xy, 1.0)), E.MaterialProperty.MP_NORMAL)
    g.out(0.0, E.MaterialProperty.MP_BASE_COLOR)
    g.out(0.04, E.MaterialProperty.MP_ROUGHNESS)
    wo = g.new(E.MaterialExpressionSingleLayerWaterMaterialOutput)
    for names, value in ((("ScatteringCoefficients", "Scattering Coefficients"), (0.0006, 0.0004, 0.0002)),
                         (("AbsorptionCoefficients", "Absorption Coefficients"), (0.03, 0.045, 0.07)),
                         (("PhaseG", "Phase G"), 0.1),
                         (("ColorScaleBehindWater", "Color Scale Behind Water"), 1.0)):
        src = g.val(value)
        if not any(mel.connect_material_expressions(src, "", wo, n) for n in names):
            warn("pool: no SLW input %s" % (names,))
    finish_material(mat)
    return mat

def pick(models, key, rng):
    meshes = models[key]
    return meshes[rng.randrange(len(meshes))]


def clear_of(x, y, keep_out):
    for kx, ky, kr in keep_out:
        if (x - kx) ** 2 + (y - ky) ** 2 < kr * kr:
            return False
    return True


def place_mesh_at(mesh, cx, cy, cz, yaw=0.0, pitch=0.0, roll=0.0, scale=(1.0, 1.0, 1.0), folder="Dressing"):
    """Put the mesh's bounds centre, after scale and rotation, at (cx, cy, cz)."""
    b = mesh.get_bounding_box()
    rot = unreal.Rotator(roll=roll, pitch=pitch, yaw=yaw)
    centre = rot.quaternion().rotate_vector(unreal.Vector((b.min.x + b.max.x) * 0.5 * scale[0], (b.min.y + b.max.y) * 0.5 * scale[1],
                                                          (b.min.z + b.max.z) * 0.5 * scale[2]))
    a = spawn_mesh(mesh, unreal.Vector(cx - centre.x, cy - centre.y, cz - centre.z), rot)
    a.set_actor_scale3d(unreal.Vector(*scale))
    a.set_folder_path(folder)
    return a


def cliff_surface(dx, dy, z, r_start=30000.0):
    """Distance along the bearing (dx, dy) at which the ground first rises to height z, coming in from the sea."""
    r = r_start
    while r > 4000.0 and height(dx * r, dy * r) < z:
        r -= 200.0
    lo, hi = r, r + 200.0
    for _ in range(6):
        mid = (lo + hi) * 0.5
        if height(dx * mid, dy * mid) >= z:
            lo = mid
        else:
            hi = mid
    return (lo + hi) * 0.5


def clad_cliff_column(rng, meshes, dx, dy):
    """Stack boulders up the cliff face on one bearing, from below the waterline to the clifftop. Each rock turns
    its broad side out to sea along the face's own outward normal and leans back with the slope, so the face
    reads as a wall of stacked stone rather than a scatter. Returns how many were placed."""
    r_foot = cliff_surface(dx, dy, 0.0)
    top = max(height(dx * (r_foot - 1300.0), dy * (r_foot - 1300.0)), height(dx * (r_foot - 1800.0), dy * (r_foot - 1800.0)))
    if top < 600.0:
        return 0  # the cliff coast fades out here: leave it to the shore rocks
    placed = 0
    zc = -380.0
    r_prev = 30000.0
    first = True
    while placed < 9:
        mesh = meshes[rng.randrange(len(meshes))]
        b = mesh.get_bounding_box()
        bx, by, bz = b.max.x - b.min.x, b.max.y - b.min.y, b.max.z - b.min.z
        broad_y = by >= bx  # present the broad horizontal side to the sea
        w, t = (by, bx) if broad_y else (bx, by)
        s = rng.uniform(780.0, 1050.0) / max(w, 1.0)
        vz = rng.uniform(1.0, 1.35)
        h = bz * s * vz
        if first:
            zc += h * 0.5
            first = False
        if zc - h * 0.5 > top - 150.0:
            break
        if zc + h * 0.5 > top + 90.0:
            zc = top + 90.0 - h * 0.5  # crown the edge without walling off the clifftop
        r = cliff_surface(dx, dy, zc, min(r_prev + 400.0, 30000.0))
        r_prev = r
        sx, sy = dx * r, dy * r
        n = LIB.get_island_normal(sx, sy)
        hn = math.hypot(n.x, n.y)
        ox, oy = (n.x / hn * 0.7 + dx * 0.3, n.y / hn * 0.7 + dy * 0.3) if hn > 0.2 else (dx, dy)
        on = math.hypot(ox, oy) or 1.0
        ox, oy = ox / on, oy / on
        out_yaw = math.degrees(math.atan2(oy, ox))
        lean = math.degrees(math.asin(max(0.0, min(0.6, n.z)))) if hn > 0.2 else 15.0
        depth = t * s
        # Embedded in the face, its front standing about a third of its depth proud of the rock.
        cx, cy = sx - ox * depth * 0.15, sy - oy * depth * 0.15
        yaw = out_yaw if broad_y else out_yaw + 90.0  # mesh +X, or mesh -Y, faces out
        scale = (s, s, s * vz)
        place_mesh_at(mesh, cx, cy, zc, yaw=yaw + rng.uniform(-6, 6),
                      pitch=(lean if broad_y else 0.0) + rng.uniform(-4, 4),
                      # (+pitch tips the X face up; +roll tips the -Y face up, which faces out when broad along X)
                      roll=(0.0 if broad_y else lean) + rng.uniform(-4, 4), scale=scale, folder="Dressing/Cliffs")
        placed += 1
        zc += h * 0.5 + h * 0.3  # next rock overlaps this one's top fifth
    return placed


def dress(models, route, pads, start):
    rng = random.Random(1717)
    keep_out = [(HILL[0], HILL[1], ARENA_RADIUS + 300.0), (start[0], start[1], 1800.0)] + [(x, y, r + 300.0) for x, y, r in pads]
    path_clear = 700.0
    rocks = models["rock_moss_set_01"] + models["rock_moss_set_02"] + models["boulder_01"]
    # rock_face_02 and mountainside are open scanned shells (one-sided: invisible from behind), so
    # every rock that can be walked or swum around is one of the closed boulders.
    massive = models["boulder_01"] + models["rock_moss_set_01"]
    outcrop = models["boulder_01"] + models["rock_moss_set_01"]
    count = {"boulders": 0, "outcrops": 0, "coast": 0, "cliff": 0, "drift": 0}

    # Boulder clusters across the interior (UNR-T38: strewn thicker).
    for _ in range(130):
        cx, cy = rng.uniform(-19000, 19000), rng.uniform(-16000, 16000)
        if height(cx, cy) < 350.0:
            continue
        for _ in range(rng.randint(2, 6)):
            x, y = cx + rng.gauss(0, 500), cy + rng.gauss(0, 500)
            if height(x, y) < 300.0 or LIB.get_path_distance(x, y) < path_clear or not clear_of(x, y, keep_out) or biome(x, y)[0] > 0.3:
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
                place_mesh(outcrop[rng.randrange(len(outcrop))], x, y, yaw=rng.uniform(0, 360), scale=rng.uniform(1.8, 3.4),
                           sink=0.4, pitch=rng.uniform(-10, 10), folder="Dressing/Outcrops")
                count["outcrops"] += 1

    # Lone boulders strewn over the open land, half sunk into the turf (UNR-T38).
    count["strewn"] = 0
    for _ in range(2400):
        if count["strewn"] >= 240:
            break
        x, y = rng.uniform(-21000, 21000), rng.uniform(-17000, 17000)
        if (height(x, y) < 150.0 or normal_z(x, y) < 0.8 or LIB.get_path_distance(x, y) < path_clear
                or not clear_of(x, y, keep_out) or biome(x, y)[0] > 0.3):
            continue
        place_mesh(rocks[rng.randrange(len(rocks))], x, y, yaw=rng.uniform(0, 360), scale=rng.uniform(0.3, 1.3),
                   sink=rng.uniform(0.25, 0.45), pitch=rng.uniform(-12, 12), roll=rng.uniform(-12, 12),
                   folder="Dressing/Boulders")
        count["strewn"] += 1

    # Shoreline rocks (not on the start beach) and the cliff coast clad in boulders (UNR-T38).
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
            # A column of boulders stacked up the face every third bearing (~7-8 m apart).
            if i % 3 == 0:
                count["cliff"] += clad_cliff_column(rng, massive, dx, dy)
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
        # A sea stack is a closed boulder stretched tall, so it reads from every side as you swim round it.
        mesh = massive[k % len(massive)]
        s = rng.uniform(7.0, 11.0)
        place_mesh(mesh, dx * r, dy * r, yaw=rng.uniform(0, 360), scale=(s, s, s * rng.uniform(1.8, 2.6)), sink=0.0,
                   z=-1400.0, folder="Dressing/SeaStacks")
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


# ----------------------------------------------------------------------------------------------
# Biomes (UNR-T17): woodland, marsh, islet; grapple ledges and loot
# ----------------------------------------------------------------------------------------------

def biome(x, y):
    b = LIB.get_island_biome(x, y)
    return b.x, b.y, b.z, b.w  # marsh, wood, heather, pool


def inst_transform(mesh, x, y, yaw, s, sink=0.05, z=None, pitch=0.0, roll=0.0):
    """Instance transform putting the mesh's (rotated, scaled) bottom-centre on the ground at (x, y)."""
    b = mesh.get_bounding_box()
    rot = unreal.Rotator(roll=roll, pitch=pitch, yaw=yaw)
    q = rot.quaternion()
    corners = [q.rotate_vector(unreal.Vector(px * s, py * s, pz * s))
               for px in (b.min.x, b.max.x) for py in (b.min.y, b.max.y) for pz in (b.min.z, b.max.z)]
    lo_z, hi_z = min(c.z for c in corners), max(c.z for c in corners)
    centre = q.rotate_vector(unreal.Vector((b.min.x + b.max.x) * 0.5 * s, (b.min.y + b.max.y) * 0.5 * s, 0.0))
    gz = height(x, y) if z is None else z
    return unreal.Transform(unreal.Vector(x - centre.x, y - centre.y, gz - (lo_z + sink * (hi_z - lo_z))), rot,
                            unreal.Vector(s, s, s))


def scatter(world, rng, meshes, count, accept, scale, label, collision=False, cull=0.0, sink=0.05, tries=40):
    """Rejection-sample count instances over the island where accept(x, y) holds; one HISM actor per mesh."""
    per_mesh = {m.get_name(): [] for m in meshes}
    placed = 0
    for _ in range(count * tries):
        if placed >= count:
            break
        x, y = rng.uniform(-24000, 24000), rng.uniform(-24000, 24000)
        if not accept(x, y):
            continue
        m = meshes[rng.randrange(len(meshes))]
        per_mesh[m.get_name()].append(inst_transform(m, x, y, rng.uniform(0, 360), rng.uniform(*scale), sink=sink,
                                                     pitch=rng.uniform(-4, 4), roll=rng.uniform(-4, 4)))
        placed += 1
    for m in meshes:
        if per_mesh[m.get_name()]:
            LIB.create_instanced_meshes(world, m, per_mesh[m.get_name()], "%s_%s" % (label, m.get_name()), "Biomes/" + label,
                                        collision, cull)
    log("biome scatter %s: %d instances" % (label, placed))
    return placed


def dress_biomes(world, models, route, pads):
    rng = random.Random(2468)
    keep = [(HILL[0], HILL[1], ARENA_RADIUS + 400.0)] + [(x, y, r + 200.0) for x, y, r in pads]

    def land(x, y, min_h=300.0, path=600.0):
        return (height(x, y) > min_h and normal_z(x, y) > 0.8 and LIB.get_path_distance(x, y) > path and clear_of(x, y, keep))

    # Woodland: wind-bent island trees with bushes, ferns and wildflowers beneath.
    scatter(world, rng, models["island_tree_02"], 48, lambda x, y: biome(x, y)[1] > 0.45 and land(x, y, path=900.0),
            (1.6, 2.5), "Trees", collision=True, sink=0.02)
    scatter(world, rng, models["shrub_02"], 140, lambda x, y: biome(x, y)[1] > 0.3 and land(x, y), (0.8, 1.4), "Shrubs",
            cull=9000.0)
    scatter(world, rng, models["fern_02"], 700, lambda x, y: biome(x, y)[1] > 0.4 and land(x, y, path=350.0), (1.0, 1.8),
            "Ferns", cull=6000.0, sink=0.02)
    scatter(world, rng, models["dandelion_01"], 500,
            lambda x, y: (biome(x, y)[1] > 0.2 or rng.random() < 0.25) and biome(x, y)[0] < 0.2 and land(x, y, path=250.0),
            (0.9, 1.3), "Flowers", cull=4500.0, sink=0.0)
    # Marsh: drowned snags and logs among the reeds.
    logs = models["dead_tree_trunk"]
    for i in range(14):
        for _ in range(60):
            x, y = BIOMES["marsh"][0] + rng.gauss(0, MARSH_RADIUS * 0.45), BIOMES["marsh"][1] + rng.gauss(0, MARSH_RADIUS * 0.45)
            if biome(x, y)[0] > 0.7 and LIB.get_path_distance(x, y) > 500 and clear_of(x, y, keep):
                break
        else:
            continue
        upright = i % 2 == 0
        place_mesh(logs[0], x, y, yaw=rng.uniform(0, 360), pitch=rng.uniform(62, 80) if upright else rng.uniform(-5, 5),
                   scale=rng.uniform(0.8, 1.3), sink=0.15 if upright else 0.35, folder="Biomes/Marsh")


def dress_more(world, models, route, pads, start):
    """Beach pebbles, and more boulders, bushes and ruined fragments over the land (UNR-T61, UNR-T62)."""
    rng = random.Random(8080)
    keep = [(HILL[0], HILL[1], ARENA_RADIUS + 400.0), (start[0], start[1], 900.0)] + [(x, y, r + 250.0) for x, y, r in pads]

    def land(x, y, min_h=300.0, path=650.0, flat=0.8):
        return (height(x, y) > min_h and normal_z(x, y) > flat and LIB.get_path_distance(x, y) > path and clear_of(x, y, keep))

    def beach(x, y):
        h = height(x, y)
        return 15.0 < h < 260.0 and normal_z(x, y) > 0.92 and biome(x, y)[3] < 0.2

    # Pebbles over the sand: the small stone at pebble size, thick near the tideline.
    pebble = models["rock_07"]
    scatter(world, rng, pebble, 2600, lambda x, y: beach(x, y) and (height(x, y) < 120.0 or rng.random() < 0.35),
            (0.25, 0.9), "Pebbles", cull=4500.0, sink=0.25)
    # Boulders strewn over open ground, half-sunk, with collision.
    scatter(world, rng, models["boulder_01"] + models["rock_moss_set_01"] + models["rock_moss_set_02"], 300,
            lambda x, y: land(x, y, path=800.0, flat=0.75) and biome(x, y)[0] < 0.6, (0.6, 2.4), "Boulders",
            collision=True, sink=0.3)
    # Bushes: thicker in the woods, and out on the moor and the dunes' lee as well.
    scatter(world, rng, models["shrub_02"], 380,
            lambda x, y: land(x, y, min_h=200.0, path=450.0) and biome(x, y)[0] < 0.5 and (biome(x, y)[1] > 0.15 or rng.random() < 0.5),
            (0.7, 1.7), "MoreShrubs", cull=9000.0)
    # Ruined fragments: lone walls and corners scattered off the path, sunk and leaning.
    pieces = ["wall_thin_straight_03", "wall_thin_corner_02", "wall_thick_thin_transition_01", "wall_thick_end_01",
              "wall_thin_straight_04", "wall_thick_end_02"]
    ruins = 0
    for _ in range(400):
        if ruins >= 10:
            break
        x, y = rng.uniform(-18000, 18000), rng.uniform(-16000, 16000)
        if not land(x, y, min_h=350.0, path=1100.0, flat=0.85) or biome(x, y)[0] > 0.3:
            continue
        place_mesh(fort(models, rng.choice(pieces)), x, y, yaw=rng.uniform(0, 360), scale=0.62, sink=rng.uniform(0.25, 0.45),
                   pitch=rng.uniform(-6, 6), roll=rng.uniform(-6, 6), folder="Ruins/Fragments")
        keep.append((x, y, 1500.0))
        ruins += 1
    log("more dressing: 10 ruin fragments target, %d placed" % ruins)


def build_islet(models):
    """The offshore islet: rocky shore, a ruined broch (grapple to its wall top) and a Grace."""
    rng = random.Random(777)
    ix, iy, ir = BIOMES["islet"]
    rocks = models["rock_moss_set_01"] + models["rock_moss_set_02"] + models["boulder_01"]
    for k in range(16):
        a = 2 * math.pi * k / 16 + rng.uniform(-0.1, 0.1)
        r = ir * rng.uniform(0.85, 1.15)
        x, y = ix + math.cos(a) * r, iy + math.sin(a) * r
        place_mesh(rocks[rng.randrange(len(rocks))], x, y, yaw=rng.uniform(0, 360), scale=rng.uniform(1.0, 2.4), sink=0.35,
                   folder="Islet")
    broch = place_mesh(fort(models, "tower_round"), ix + ir * 0.2, iy - ir * 0.15, yaw=rng.uniform(0, 360), scale=0.5,
                       sink=0.08, folder="Islet")
    log("islet at (%.0f, %.0f) r=%.0f top %.0f" % (ix, iy, ir, height(ix, iy)))
    return broch


def trace_down(world, x, y, z_top, z_bottom):
    res = unreal.SystemLibrary.line_trace_single(world, unreal.Vector(x, y, z_top), unreal.Vector(x, y, z_bottom),
                                                 unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, True, [],
                                                 unreal.DrawDebugTrace.NONE, True)
    hit = res[1] if isinstance(res, tuple) else res
    if hit is None:
        return None
    t = hit.to_tuple()
    if not t[0]:
        return None
    return t[5], t[7]  # impact point, impact normal


def find_ledge(world, actor, approach):
    """Walk from outside the actor toward its centre along approach; the first walkable surface well above the
    ground is the lip. Returns (lip point, yaw) when a 1.7 m landing behind the lip is walkable too."""
    origin, extent = actor.get_actor_bounds(False)
    ax, ay = approach
    n = math.hypot(ax, ay) or 1.0
    ax, ay = ax / n, ay / n
    reach = max(extent.x, extent.y) + 150.0
    top = origin.z + extent.z + 300.0
    prev = None
    for step in range(int(reach * 2 / 20.0)):
        d = -reach + step * 20.0
        x, y = origin.x + ax * d, origin.y + ay * d
        hit = trace_down(world, x, y, top, origin.z - extent.z - 500.0)
        if hit is None:
            continue
        p, nrm = hit
        ground = height(x, y)
        if p.z > ground + 250.0 and nrm.z > 0.8:
            land = trace_down(world, x + ax * 170.0, y + ay * 170.0, top, p.z - 200.0)
            if land and land[1].z > 0.8 and abs(land[0].z - p.z) < 60.0:
                return unreal.Vector(x - ax * 10.0, y - ay * 10.0, p.z + 20.0), math.degrees(math.atan2(ay, ax))
        prev = hit
    return None


def build_grapples_and_loot(world, route, pads, broch):
    """Grapple hook points on ruin and broch tops (forward = onto the walkable top) and Loot off the main route."""
    ruins = [a for a in actors_sub.get_all_level_actors() if isinstance(a, unreal.StaticMeshActor)
             and str(a.get_folder_path()) in ("Ruins", "Islet")]
    # Every ruin is a candidate; keep up to 5 well-separated ledges (towers first), approached from the route side.
    ruins.sort(key=lambda a: (0 if "tower" in a.get_actor_label() else 1, a.get_actor_label()))
    grapples = []
    for a in ruins:
        if str(a.get_folder_path()) != "Ruins" or len(grapples) >= 5:
            continue
        o = a.get_actor_location()
        if any((o.x - g_[1][0].x) ** 2 + (o.y - g_[1][0].y) ** 2 < 2000.0 ** 2 for g_ in grapples):
            continue
        px, py = route_nearest(route, o.x, o.y)
        got = find_ledge(world, a, (o.x - px, o.y - py)) or find_ledge(world, a, (px - o.x, py - o.y))
        if got:
            grapples.append((a.get_actor_label().replace("modular_fort_01_", ""), got, a))
    got = find_ledge(world, broch, (-BIOMES["islet"][0], -BIOMES["islet"][1]))
    if got:
        grapples.append(("broch", got, broch))
    else:
        warn("no grapple ledge on the islet broch")
    for i, (name, (p, yaw), a) in enumerate(grapples):
        target_point(p.x, p.y, "Grapple", "Grapple_%02d_%s" % (i + 1, name), z=p.z, yaw=yaw)
    log("grapple points: %s" % [(n, round(p.z)) for n, (p, _), _ in grapples])

    # Loot (5-7, max 2 Seeds, 1 Tear), all off the main route.
    loot = []
    rad = math.radians
    for name, (p, yaw), a in grapples:
        if name == "broch" or name.startswith("tower_round"):
            # Up on the top, 2.5 m past the lip.
            x, y = p.x + math.cos(rad(yaw)) * 250.0, p.y + math.sin(rad(yaw)) * 250.0
            hit = trace_down(world, x, y, p.z + 300.0, p.z - 300.0)
            if hit:
                loot.append(("Seed", "Loot_%s_Top" % name, hit[0].x, hit[0].y, hit[0].z + 5.0))
    # Deep in the marsh, on a dry hummock between pools.
    rng = random.Random(31)
    mx, my = BIOMES["marsh"]
    for _ in range(400):
        x, y = mx + rng.gauss(0, MARSH_RADIUS * 0.35), my + rng.gauss(0, MARSH_RADIUS * 0.35)
        m, w, h, pool = biome(x, y)
        if m > 0.85 and pool == 0.0 and LIB.get_path_distance(x, y) > 1800.0:
            loot.append(("Tear", "Loot_Marsh", x, y, height(x, y) + 5.0))
            break
    # Cliff-edge viewpoint: the clifftop just past the stone circle, looking out to sea.
    cdx, cdy = CLIFF_DIR
    for side in (-0.35, 0.35):
        ax, ay = cdx * math.cos(side) - cdy * math.sin(side), cdx * math.sin(side) + cdy * math.cos(side)
        r = 10000.0
        while r < 28000.0 and height(ax * r, ay * r) > 1200.0:
            r += 100.0
        x, y = ax * (r - 700.0), ay * (r - 700.0)
        if height(x, y) > 1200.0 and normal_z(x, y) > 0.85:
            loot.append(("", "Loot_Clifftop", x, y, height(x, y) + 5.0))
            break
    # Islet shore and behind a woodland rock.
    ix, iy, ir = BIOMES["islet"]
    loot.append(("", "Loot_IsletShore", ix - ir * 0.55, iy - ir * 0.3, height(ix - ir * 0.55, iy - ir * 0.3) + 5.0))
    wx, wy = BIOMES["wood"]
    for _ in range(300):
        x, y = wx + rng.gauss(0, 2500), wy + rng.gauss(0, 2500)
        if biome(x, y)[1] > 0.6 and LIB.get_path_distance(x, y) > 2000.0 and normal_z(x, y) > 0.9:
            loot.append(("", "Loot_Woodland", x, y, height(x, y) + 5.0))
            break
    seeds = 0
    for kind, label, x, y, z in loot[:7]:
        if kind == "Seed":
            seeds += 1
            if seeds > 2:
                kind = ""
        target_point(x, y, "Loot", label, z=z, extra_tags=(kind,) if kind else ())
    log("loot: %s" % [(k or "purse", l) for k, l, _, _, _ in loot[:7]])
    return grapples


def route_nearest(route, x, y):
    best, bp = 1e18, route.points[0]
    for i in range(101):
        (px, py), _ = route.at(i / 100.0)
        d = (px - x) ** 2 + (py - y) ** 2
        if d < best:
            best, bp = d, (px, py)
    return bp

def target_point(x, y, tag, label, z_offset=10.0, yaw=0.0, z=None, extra_tags=()):
    tz = height(x, y) + z_offset if z is None else z
    tp = spawn(unreal.TargetPoint, (x, y, tz), unreal.Rotator(yaw=yaw), label, "Gameplay")
    tp.set_editor_property("tags", [unreal.Name(tag)] + [unreal.Name(t) for t in extra_tags])
    return tp


def clear_markers():
    """No prop may stand on a gameplay marker: remove any dressing whose bounds (grown by 1.5 m) cover one."""
    markers = [a for a in actors_sub.get_all_level_actors() if isinstance(a, unreal.PlayerStart) or (
        isinstance(a, unreal.TargetPoint) and any(str(n) in ("Enemy", "Grace", "Boss", "ShadowBoss") for n in a.tags))]
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


# ----------------------------------------------------------------------------------------------
# The bestiary (UNR-T64/T67): monster posts by biome. An "Enemy" TargetPoint also tagged
# "Bestiary_<Type>" spawns /Game/Souls/Bestiary/BP_SoulsEnemy_<Type> (ASoulsMonster::ClassForMarker,
# tools/build_bestiary.py) instead of a Hollow knight.
# ----------------------------------------------------------------------------------------------

# Route posts (1-based, the enemies list in build_markers) that hold a monster.
BESTIARY_ROUTE_POSTS = {1: "Hollow", 3: "Skeleton", 5: "Skeleton", 6: "Zombie", 7: "Imp", 8: "Brute"}
# (kind, route t, side offset cm): hollow pack and a huntress in the woods (positive side = toward the
# woodland), skeletons in the ruined keep and on the standing-stone avenue, the drowned and an imp in
# the marsh (negative side), brutes on the heather moor.
BESTIARY_POSTS = [
    ("Hollow", 0.26, 1500.0), ("Hollow", 0.27, 1850.0), ("Hollow", 0.28, 1450.0), ("Huntress", 0.31, 2000.0),
    ("Skeleton", 0.46, -1500.0), ("Skeleton", 0.48, -2100.0),
    ("Zombie", 0.58, -1900.0), ("Zombie", 0.60, -1500.0), ("Zombie", 0.62, -2050.0), ("Imp", 0.61, -2600.0),
    ("Brute", 0.76, 1700.0), ("Brute", 0.88, -1400.0),
]


def build_bestiary_markers(route):
    # clear_markers() deletes any ruin or standing stone over a marker, so keep 2.5 m clear of them.
    solid = []
    for a in actors_sub.get_all_level_actors():
        if isinstance(a, unreal.StaticMeshActor) and str(a.get_folder_path()).startswith(("Ruins", "StoneCircle")):
            o, e = a.get_actor_bounds(False)
            solid.append((o.x, o.y, e.x + 250.0, e.y + 250.0))

    def ok(x, y):
        return (height(x, y) > 80.0 and normal_z(x, y) > 0.8 and biome(x, y)[3] < 0.05
                and math.hypot(x - HILL[0], y - HILL[1]) > ARENA_RADIUS + 600.0
                and not any(abs(x - ox) < ex and abs(y - oy) < ey for ox, oy, ex, ey in solid))

    # The standing-stone avenue (tools/build_island.py build_stone_circle): between the 3rd and 5th pairs.
    posts = list(BESTIARY_POSTS) + [("Skeleton", 1.0 - (ARENA_RADIUS + 900.0 + k * 900.0) / route.length, 0.0) for k in (2, 4)]
    placed, skipped = [], []
    for n, (kind, t, side) in enumerate(posts):
        spot = None
        for shrink in (1.0, 0.75, 0.5, 0.3):   # step toward the path until the ground is dry and level
            x, y = route.offset(t, side * shrink)
            if ok(x, y):
                spot = (x, y)
                break
        if spot is None:
            skipped.append("%s@%.2f" % (kind, t))
            continue
        (px, py), (rdx, rdy) = route.at(t)
        if side:
            yaw = math.degrees(math.atan2(py - spot[1], px - spot[0]))   # watching the path
        else:
            yaw = math.degrees(math.atan2(rdy, rdx)) + 180.0             # on the path, facing the way you come
        target_point(spot[0], spot[1], "Enemy", "Bestiary_%02d_%s" % (n + 1, kind), yaw=yaw, extra_tags=("Bestiary_" + kind,))
        placed.append(kind)
    log("bestiary posts: %d placed %s; %d skipped %s; route posts %s" % (len(placed), placed, len(skipped), skipped, BESTIARY_ROUTE_POSTS))
    if skipped:
        warn("bestiary posts skipped (no dry level ground): %s" % skipped)


def build_markers(route, start):
    (sx, sy), (dx, dy) = route.at(0.0)
    yaw = math.degrees(math.atan2(dy, dx))
    ps = spawn(unreal.PlayerStart, (sx, sy, height(sx, sy) + 100.0), unreal.Rotator(yaw=yaw), "PlayerStart", "Gameplay")
    gx, gy = route.offset(0.035, 450.0)
    target_point(gx, gy, "Grace", "Grace_Beach")
    t_circle = 1.0 - (ARENA_RADIUS + 1100.0) / route.length
    gx, gy = route.offset(t_circle, 380.0)
    target_point(gx, gy, "Grace", "Grace_StoneCircle")
    # (t, side, facing, extra tags). facing "away" = back turned to a player coming up the route (ambush);
    # "back" = watching the way the player comes.
    enemies = [(0.21, -250.0, "away", ()), (0.23, 300.0, "back", ()), (0.44, 0.0, "back", ()), (0.47, 350.0, "away", ("Leaper",)),
               (0.50, -300.0, "away", ()), (0.64, 200.0, "away", ()), (0.67, -250.0, "back", ("Leaper",)),
               (0.80, 250.0, "away", ()), (0.83, -200.0, "back", ())]
    for i, (t, side, facing, extra) in enumerate(enemies):
        x, y = route.offset(t, side)
        (_, _), (rdx, rdy) = route.at(t)
        fyaw = math.degrees(math.atan2(rdy, rdx)) + (0.0 if facing == "away" else 180.0)
        kind = BESTIARY_ROUTE_POSTS.get(i + 1)  # UNR-T64: some route posts hold a monster instead of a knight
        target_point(x, y, "Enemy", "Enemy_%02d" % (i + 1), yaw=fyaw, extra_tags=extra + (("Bestiary_" + kind,) if kind else ()))
    build_bestiary_markers(route)
    # Grace on the offshore islet and the shadow boss in the marsh clearing.
    ix, iy, ir = BIOMES["islet"]
    target_point(ix - ir * 0.25, iy + ir * 0.2, "Grace", "Grace_Islet")
    cx, cy = BIOMES["clearing"]
    target_point(cx, cy, "ShadowBoss", "ShadowBoss")
    target_point(HILL[0], HILL[1], "Boss", "Boss")
    clear_markers()
    log("markers: PlayerStart yaw %.0f, 3 graces, %d enemies, shadow boss, boss at (%.0f, %.0f, %.0f)" % (
        yaw, len(enemies), HILL[0], HILL[1], height(*HILL)))
    return ps


def write_shot_list(route, start, grapples=()):
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
    # Biomes, swimming, a grapple ledge.
    wx, wy = BIOMES["wood"]
    (rx, ry) = route.offset(0.27, 2600.0)
    lines.append(shot("biome_woodland", rx, ry, height(rx, ry) + 200.0, wx, wy, height(wx, wy) + 250.0, fov=85))
    mx, my = BIOMES["marsh"]
    (rx, ry) = route.offset(0.56, -1500.0)
    lines.append(shot("biome_marsh", rx, ry, height(rx, ry) + 260.0, mx, my, MARSH_LEVEL + 150.0, fov=80))
    cx, cy = BIOMES["clearing"]
    ex, ey = cx + 3200.0, cy + 800.0
    lines.append(shot("marsh_clearing", ex, ey, max(height(ex, ey), POOL_LEVEL) + 450.0, cx, cy, height(cx, cy) + 100.0, fov=80))
    (rx, ry) = route.at(0.80)[0]
    lines.append(shot("biome_highland", rx, ry, height(rx, ry) + 250.0, HILL[0], HILL[1], ARENA_HEIGHT, fov=80))
    lines.append(shot("biome_dunes", sx - dy * 2500, sy + dx * 2500, height(sx - dy * 2500, sy + dx * 2500) + 200.0,
                      sx + dx * 3000, sy + dy * 3000, 400.0, fov=80))
    ix, iy, ir = BIOMES["islet"]
    idist = math.hypot(ix, iy)
    sx2, sy2 = ix - ix / idist * (ir + 1300.0), iy - iy / idist * (ir + 1500.0)  # open water off the islet
    lines.append("swim pawn %.0f %.0f %.0f %.1f" % (sx2, sy2, -60.0, math.degrees(math.atan2(iy - sy2, ix - sx2))))
    lines.append(shot("islet", ix * 0.82 - 1500.0, iy * 0.82, 350.0, ix, iy, 500.0, fov=70))
    for name, (p, yaw), a in list(grapples)[:2]:
        back = math.radians(yaw + 180.0)
        cxg, cyg = p.x + math.cos(back) * 1500.0, p.y + math.sin(back) * 1500.0
        lines.append(shot("grapple_%s" % name, cxg, cyg, height(cxg, cyg) + 300.0, p.x, p.y, p.z - 250.0, fov=70))
    path = os.path.join(PROJECT, "Saved", "island_shots.txt")
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    log("shot list -> %s" % path)


def shot(name, x, y, z, tx, ty, tz, fov=75):
    dx, dy, dz = tx - x, ty - y, tz - z
    yaw = math.degrees(math.atan2(dy, dx))
    pitch = math.degrees(math.atan2(dz, math.hypot(dx, dy)))
    return "%s %.0f %.0f %.0f %.2f %.2f %.0f" % (name, x, y, z, pitch, yaw, fov)


# ----------------------------------------------------------------------------------------------
# Tall grass to hide in (UNR-T43): TallGrassPatch actors, each its own instanced grass + hiding radius
# ----------------------------------------------------------------------------------------------

TALL_GRASS_SCATTER = 22            # patches over open ground, besides one by each enemy post
TALL_GRASS_DENSITY = 1.3           # stems per square metre
TALL_GRASS_HEIGHT = (125.0, 165.0)  # cm; tall enough to swallow a crouching knight
TALL_GRASS_RADIUS = (380.0, 620.0)  # cm


def tall_grass_mesh(by_name, x, y):
    """Marram scaled up reads as long pale grass; reeds where it is wet."""
    name = "SM_Reeds" if biome(x, y)[0] > 0.5 else "SM_Marram"
    return by_name.get(name) or by_name.get("SM_Marram") or by_name.get("SM_GrassClump_B")


def tall_grass_instances(rng, mesh, cx, cy, radius):
    b = mesh.get_bounding_box()
    mh = max(b.max.z - b.min.z, 1.0)
    out = []
    for _ in range(int(math.pi * (radius / 100.0) ** 2 * TALL_GRASS_DENSITY)):
        r = radius * math.sqrt(rng.random())
        a = rng.uniform(0.0, 2.0 * math.pi)
        x, y = cx + math.cos(a) * r, cy + math.sin(a) * r
        if height(x, y) < 60.0 or normal_z(x, y) < 0.7:
            continue
        sz = rng.uniform(*TALL_GRASS_HEIGHT) * (1.0 - 0.4 * (r / radius) ** 3) / mh  # shorter toward the rim
        sxy = sz * rng.uniform(0.6, 0.85)
        rot = unreal.Rotator(roll=rng.uniform(-6.0, 6.0), pitch=rng.uniform(-6.0, 6.0), yaw=rng.uniform(0.0, 360.0))
        z = height(x, y) - b.min.z * sz - 0.04 * mh * sz
        out.append(unreal.Transform(unreal.Vector(x, y, z), rot, unreal.Vector(sxy, sxy, sz)))
    return out


def build_tall_grass(world, models, route, pads):
    cls = getattr(unreal, "TallGrassPatch", None)
    if cls is None:
        warn("TallGrassPatch class missing (compile the C++ module first); no tall grass")
        return []
    by_name = {m.get_name(): m for m in models["grass"]}
    rng = random.Random(4321)
    markers, enemies = [], []
    for a in actors_sub.get_all_level_actors():
        if isinstance(a, (unreal.PlayerStart, unreal.TargetPoint)):
            p = a.get_actor_location()
            markers.append((p.x, p.y))
            if isinstance(a, unreal.TargetPoint) and any(str(n) in ("Enemy", "Leaper") for n in a.tags):
                enemies.append((p.x, p.y))
    keep = [(HILL[0], HILL[1], ARENA_RADIUS + 600.0)] + [(x, y, r + 300.0) for x, y, r in pads]
    placed = []

    def ok(x, y, r):
        return (height(x, y) > 150.0 and normal_z(x, y) > 0.85 and LIB.get_path_distance(x, y) > r + 60.0
                and clear_of(x, y, keep) and biome(x, y)[3] < 0.1
                and all(math.hypot(x - mx, y - my) > r + 350.0 for mx, my in markers)
                and all(math.hypot(x - px, y - py) > r + pr + 400.0 for px, py, pr in placed))

    stems = [0]

    def make(x, y, r, label):
        mesh = tall_grass_mesh(by_name, x, y)
        if mesh is None:
            return False
        inst = tall_grass_instances(rng, mesh, x, y, r)
        if len(inst) < 8:
            return False
        a = spawn(cls, (x, y, height(x, y)), None, label, "Biomes/TallGrass")
        setp(a, "radius", r)
        setp(a, "grass_height", TALL_GRASS_HEIGHT[0])
        a.set_grass(mesh, inst)
        placed.append((x, y, r))
        stems[0] += len(inst)
        return True

    # One patch a few metres from each enemy post, to creep up through.
    near = []
    for i, (ex, ey) in enumerate(enemies):
        for _ in range(60):
            r = rng.uniform(*TALL_GRASS_RADIUS)
            ang = rng.uniform(0.0, 2.0 * math.pi)
            d = r + rng.uniform(350.0, 900.0)
            x, y = ex + math.cos(ang) * d, ey + math.sin(ang) * d
            if ok(x, y, r) and make(x, y, r, "TallGrass_Post%02d" % (i + 1)):
                near.append((x, y, r, ex, ey))
                break
    count = 0
    for _ in range(TALL_GRASS_SCATTER * 60):
        if count >= TALL_GRASS_SCATTER:
            break
        r = rng.uniform(*TALL_GRASS_RADIUS)
        x, y = rng.uniform(-20000.0, 20000.0), rng.uniform(-18000.0, 18000.0)
        if ok(x, y, r) and make(x, y, r, "TallGrass_%02d" % (count + 1)):
            count += 1
    log("tall grass: %d patches (%d by enemy posts of %d), %d stems" % (len(placed), len(near), len(enemies), stems[0]))

    # A look at the first one from the far side of its enemy, added to the -IslandShots list.
    path = os.path.join(PROJECT, "Saved", "island_shots.txt")
    if near and os.path.exists(path):
        x, y, r, ex, ey = near[0]
        dx, dy = x - ex, y - ey
        n = math.hypot(dx, dy) or 1.0
        cx, cy = x + dx / n * (r + 700.0), y + dy / n * (r + 700.0)
        with open(path, "a", encoding="utf-8") as f:
            f.write(shot("tall_grass", cx, cy, height(cx, cy) + 230.0, x, y, height(x, y) + 70.0, fov=75) + "\n")
    return placed


# ----------------------------------------------------------------------------------------------
# Wildlife (UNR-T36/T37): crabs and washed-up fish on the beach; frogs on the pool banks and fish
# in the marsh pools. The live ones are ASoulsWildlife actors (Source/UnrealSoulslike/Souls).
# ----------------------------------------------------------------------------------------------

CRITTER_ART = os.path.join(ART, "Critters")
CRITTER_NAMES = ("Crab", "Frog", "Fish")
# Slot colour overrides (linear); other slots take their .mtl Kd. The source frog is a bright tree frog:
# make it a Hebridean common frog (olive-brown, dark eyes).
CRITTER_TINTS = {("Crab", "crab"): (0.82, 0.66, 0.56), ("Frog", "Green"): (0.16, 0.2, 0.08),
                 ("Frog", "Yellow"): (0.34, 0.27, 0.13), ("Frog", "Red"): (0.09, 0.06, 0.03)}
CRITTER_ROUGHNESS = {"Crab": 0.55, "Frog": 0.35, "Fish": 0.3}


def import_critters():
    """SourceArt/Environment/Critters/SM_<Name>.obj (baked by tools/convert_critters.py) -> /Game/Island/Models/Critters/<Name>."""
    out = {}
    for name in CRITTER_NAMES:
        dest = "%s/Models/Critters/%s" % (ROOT, name)
        if REIMPORT or not (eal.does_directory_exist(dest) and assets_of(dest, unreal.StaticMesh)):
            if eal.does_directory_exist(dest):
                eal.delete_directory(dest)
            import_files([os.path.join(CRITTER_ART, "SM_%s.obj" % name)], dest)
            for mesh in assets_of(dest, unreal.StaticMesh):
                nanite_and_collision(mesh, nanite=False, complex_collision=False)
                mesh.set_editor_property("body_setup", None)  # never blocks or catches traces
                eal.save_loaded_asset(mesh)
        meshes = assets_of(dest, unreal.StaticMesh)
        if not meshes:
            warn("critter %s: no static mesh imported" % name)
            continue
        out[name] = meshes[0]
        b = meshes[0].get_bounding_box()
        log("critter %s: %s size=(%.1f, %.1f, %.1f) slots=%s" % (name, meshes[0].get_name(), b.max.x - b.min.x, b.max.y - b.min.y,
                                                                  b.max.z - b.min.z, [str(s.get_editor_property("material_slot_name"))
                                                                                      for s in meshes[0].get_editor_property("static_materials")]))
    return out


def read_mtl(path):
    mats, cur = {}, None
    with open(path, encoding="utf-8") as f:
        for line in f:
            p = line.split()
            if not p:
                continue
            if p[0] == "newmtl":
                cur = p[1]
                mats[cur] = {}
            elif cur and p[0] == "Kd":
                mats[cur]["kd"] = tuple(float(v) for v in p[1:4])
            elif cur and p[0] == "map_Kd":
                mats[cur]["map"] = p[-1]
    return mats


def critter_texture(name):
    """The critter's colour texture: the one Interchange brought in with the OBJ, else import it ourselves."""
    dest = "%s/Models/Critters/%s" % (ROOT, name)
    tex = assets_of(dest, unreal.Texture2D)
    if tex:
        return tex[0]
    png = os.path.join(CRITTER_ART, "%s.png" % name.lower())
    if not os.path.exists(png):
        return None
    import_files([png], dest + "/Tex")
    tex = assets_of(dest + "/Tex", unreal.Texture2D)
    return tex[0] if tex else None


def build_critter_materials(critters):
    """M_Critter: tint x (optionally) a colour texture; one instance per mesh slot from the baked .mtl."""
    textures = {n: critter_texture(n) for n in critters}
    any_tex = next((t for t in textures.values() if t), None)
    mat, g = new_material("M_Critter", ROOT + "/Materials")
    tint = g.new(E.MaterialExpressionVectorParameter, parameter_name="Tint", default_value=unreal.LinearColor(1, 1, 1, 1))
    color = (tint, "RGB")
    if any_tex:
        uv = g.new(E.MaterialExpressionTextureCoordinate)
        base = g.new(E.MaterialExpressionTextureSampleParameter2D, parameter_name="Base", texture=any_tex,
                     sampler_type=E.MaterialSamplerType.SAMPLERTYPE_COLOR, sampler_source=E.SamplerSourceMode.SSM_WRAP_WORLD_GROUP_SETTINGS)
        g.link(uv, base, "UVs")
        use_tex = g.new(E.MaterialExpressionScalarParameter, parameter_name="UseTexture", default_value=0.0)
        color = g.lerp(color, g.mul((base, "RGB"), (tint, "RGB")), use_tex)
    g.out(color, E.MaterialProperty.MP_BASE_COLOR)
    g.out(g.new(E.MaterialExpressionScalarParameter, parameter_name="Roughness", default_value=0.6), E.MaterialProperty.MP_ROUGHNESS)
    finish_material(mat)

    folder = ROOT + "/Materials/Critters"
    ensure_dir(folder)
    for name, mesh in critters.items():
        mtl = read_mtl(os.path.join(CRITTER_ART, "SM_%s.mtl" % name))
        for i, slot in enumerate(mesh.get_editor_property("static_materials")):
            slot_name = str(slot.get_editor_property("material_slot_name"))
            key = next((k for k in mtl if k == slot_name), None) or \
                next((k for k in mtl if slot_name.lower().startswith(k.lower()) or k.lower().startswith(slot_name.lower())), None) or \
                (next(iter(mtl)) if len(mtl) == 1 else None)
            entry = mtl.get(key, {}) if key else {}
            if key is None:
                warn("critter %s: slot %s not in the .mtl %s" % (name, slot_name, sorted(mtl)))
            mi_name = "MI_Critter_%s_%s" % (name, key or i)
            path = "%s/%s" % (folder, mi_name)
            mi = eal.load_asset(path) if eal.does_asset_exist(path) else \
                tools.create_asset(mi_name, folder, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
            mel.set_material_instance_parent(mi, mat)
            tint_rgb = CRITTER_TINTS.get((name, key)) or entry.get("kd") or (1.0, 1.0, 1.0)
            mel.set_material_instance_vector_parameter_value(mi, "Tint", unreal.LinearColor(tint_rgb[0], tint_rgb[1], tint_rgb[2], 1.0))
            mel.set_material_instance_scalar_parameter_value(mi, "Roughness", CRITTER_ROUGHNESS.get(name, 0.6))
            if any_tex and "map" in entry and textures.get(name):
                mel.set_material_instance_texture_parameter_value(mi, "Base", textures[name])
                mel.set_material_instance_scalar_parameter_value(mi, "UseTexture", 1.0)
            eal.save_loaded_asset(mi)
            mesh.set_material(i, mi)
        eal.save_loaded_asset(mesh)
        log("critter materials %s: %s" % (name, sorted(mtl)))


def build_splash_material():
    """Translucent white water for the frog's splash; ASoulsWildlife fades its Opacity. A ring brightest near the rim."""
    mat, g = new_material("M_CritterSplash", ROOT + "/Materials")
    setp(mat, "blend_mode", E.BlendMode.BLEND_TRANSLUCENT)
    setp(mat, "shading_model", E.MaterialShadingModel.MSM_UNLIT)
    opacity = g.new(E.MaterialExpressionScalarParameter, parameter_name="Opacity", default_value=0.0)
    shape = 1.0
    if hasattr(E, "MaterialExpressionObjectRadius") and hasattr(E, "MaterialExpressionObjectPositionWS"):
        d = g.op(E.MaterialExpressionDistance, {"A": g.mask(g.world(), r=True, g=True),
                                                "B": g.mask(g.new(E.MaterialExpressionObjectPositionWS), r=True, g=True)})
        rel = g.div(d, g.maxx(g.new(E.MaterialExpressionObjectRadius), 1.0))
        shape = g.add(0.25, g.sat(g.sub(1.0, g.mul(g.abs(g.sub(rel, 0.62)), 6.0))))
    g.out((0.82, 0.88, 0.92), E.MaterialProperty.MP_EMISSIVE_COLOR)
    g.out(g.sat(g.mul(opacity, shape)), E.MaterialProperty.MP_OPACITY)
    finish_material(mat)
    return mat


def shore_point(ang, h):
    """Coming in from the sea along bearing ang, the first point where the ground reaches height h."""
    dx, dy = math.cos(ang), math.sin(ang)
    r = 30000.0
    while r > 4000.0 and height(dx * r, dy * r) < h:
        r -= 50.0
    return dx * r, dy * r


def build_wildlife(models, route, start):
    cls = getattr(unreal, "SoulsWildlife", None)
    kinds = getattr(unreal, "SoulsWildlifeKind", None)
    critters = import_critters()
    if not critters:
        return
    build_critter_materials(critters)
    rng = random.Random(3636)
    count = {"crabs": 0, "dead_fish": 0, "frogs": 0, "fish": 0, "pools": 0}
    shots = []

    # Dead fish washed up along the tideline (static props, lying on their side): mostly on the start beach.
    fish_mesh = critters.get("Fish")
    base_ang = math.atan2(start[1], start[0])
    if fish_mesh:
        for k in range(40):
            ang = base_ang + rng.uniform(-0.32, 0.32) if k < 24 else rng.uniform(0.0, 2.0 * math.pi)
            if k >= 24 and math.cos(ang) * CLIFF_DIR[0] + math.sin(ang) * CLIFF_DIR[1] > 0.2:
                continue  # no beach under the cliffs
            x, y = shore_point(ang, rng.uniform(4.0, 20.0))
            if normal_z(x, y) < 0.9 or math.hypot(x - start[0], y - start[1]) < 300.0:
                continue
            a = place_mesh(fish_mesh, x, y, yaw=rng.uniform(0, 360), roll=90.0 + rng.uniform(-8, 8), pitch=rng.uniform(-5, 5),
                           scale=rng.uniform(0.7, 1.0), sink=0.3, folder="Wildlife/DeadFish", label="DeadFish_%02d" % (k + 1),
                           collide=False)
            a.get_editor_property("static_mesh_component").set_editor_property("cast_shadow", True)
            count["dead_fish"] += 1

    if cls is None or kinds is None:
        warn("SoulsWildlife class missing (compile the C++ module first); no live crabs, frogs or fish")
        log("wildlife: %s" % count)
        return
    splash = build_splash_material()

    def critter(kind, mesh, x, y, z, yaw, label, folder, **props):
        a = spawn(cls, (x, y, z), unreal.Rotator(yaw=yaw), label, "Wildlife/" + folder)
        setp(a, "kind", kind)
        setp(a, "mesh", mesh)
        setp(a, "splash_material", splash)
        for k, v in props.items():
            setp(a, k, v)
        return a

    # Crabs: little groups on the start beach between the tideline and the dunes, a few on other sandy shores.
    if critters.get("Crab"):
        groups = []
        for k in range(16):
            if len(groups) >= 8:
                break
            ang = base_ang + rng.uniform(-0.35, 0.35) if k < 10 else rng.uniform(0.0, 2.0 * math.pi)
            if math.cos(ang) * CLIFF_DIR[0] + math.sin(ang) * CLIFF_DIR[1] > 0.2:
                continue
            gx, gy = shore_point(ang, rng.uniform(15.0, 70.0))
            if normal_z(gx, gy) < 0.9 or math.hypot(gx - start[0], gy - start[1]) < 500.0:
                continue
            groups.append((gx, gy))
            for _ in range(rng.randint(2, 3)):
                x, y = gx + rng.gauss(0, 160), gy + rng.gauss(0, 160)
                h = height(x, y)
                if h < 5.0 or normal_z(x, y) < 0.85:
                    continue
                count["crabs"] += 1
                critter(kinds.CRAB, critters["Crab"], x, y, h, rng.uniform(0, 360), "Crab_%02d" % count["crabs"], "Crabs",
                        body_length=rng.uniform(10.0, 14.0), roam_radius=350.0, scare_radius=550.0, speed=rng.uniform(220.0, 280.0))
        if groups:
            gx, gy = groups[0]
            n = math.hypot(gx, gy) or 1.0
            ix, iy = -gx / n, -gy / n  # inland
            cx, cy = gx + ix * 450.0 - iy * 250.0, gy + iy * 450.0 + ix * 250.0  # a few metres up the beach, a little aside
            shots.append(shot("wildlife_beach", cx, cy, height(cx, cy) + 160.0, gx, gy, height(gx, gy) + 5.0, fov=60))

    mx, my = BIOMES["marsh"]
    clx, cly = BIOMES["clearing"]

    # Frogs on the pool banks, each with the patch of open water it leaps into.
    if critters.get("Frog"):
        frogs = []
        for _ in range(6000):
            if len(frogs) >= 16:
                break
            x, y = mx + rng.gauss(0, MARSH_RADIUS * 0.4), my + rng.gauss(0, MARSH_RADIUS * 0.4)
            h = height(x, y)
            if not (POOL_LEVEL + 3.0 < h < POOL_LEVEL + 35.0) or biome(x, y)[0] < 0.6 or normal_z(x, y) < 0.8:
                continue
            if LIB.get_path_distance(x, y) < 350.0 or math.hypot(x - clx, y - cly) < CLEARING_RADIUS + 400.0:
                continue
            if any(math.hypot(x - fx, y - fy) < 700.0 for fx, fy, _ in frogs):
                continue
            water = None
            for d in (110.0, 170.0, 240.0):
                for k in range(12):
                    a = 2.0 * math.pi * k / 12
                    wx, wy = x + math.cos(a) * d, y + math.sin(a) * d
                    if height(wx, wy) < POOL_LEVEL - 25.0:
                        water = (wx, wy)
                        break
                if water:
                    break
            if not water:
                continue
            frogs.append((x, y, water))
            yaw = math.degrees(math.atan2(water[1] - y, water[0] - x)) + rng.uniform(-35, 35)
            critter(kinds.FROG, critters["Frog"], x, y, h, yaw, "Frog_%02d" % len(frogs), "Frogs",
                    water_target=unreal.Vector(water[0], water[1], POOL_LEVEL), water_level=POOL_LEVEL,
                    scare_radius=450.0, body_length=rng.uniform(12.0, 15.0))
        count["frogs"] = len(frogs)
        if frogs:
            x, y, (wx, wy) = frogs[0]
            dx, dy = x - wx, y - wy
            n = math.hypot(dx, dy) or 1.0
            cx, cy = x + dx / n * 520.0, y + dy / n * 520.0  # behind the frog, looking over it at its pool
            shots.append(shot("wildlife_pond", cx, cy, max(height(cx, cy), POOL_LEVEL) + 150.0, wx, wy, POOL_LEVEL, fov=55))

    # Fish: find open, deep-enough water (every point of a disc well below the surface), largest discs first.
    if critters.get("Fish"):
        spots = []
        step = 250.0
        gy = my - MARSH_RADIUS
        while gy <= my + MARSH_RADIUS:
            gx = mx - MARSH_RADIUS
            while gx <= mx + MARSH_RADIUS:
                h0 = height(gx, gy)
                if h0 < POOL_LEVEL - 50.0:
                    best, floor = 0.0, h0
                    for rr in (150.0, 220.0, 300.0, 400.0, 520.0, 650.0):
                        ring = [height(gx + math.cos(a) * rr, gy + math.sin(a) * rr) for a in [2.0 * math.pi * k / 12 for k in range(12)]]
                        if max(ring) > POOL_LEVEL - 20.0:
                            break
                        best, floor = rr, max(floor, max(ring))
                    if best >= 150.0 and floor + 10.0 < POOL_LEVEL - 25.0:
                        spots.append((best, gx, gy, floor))
                gx += step
            gy += step
        spots.sort(key=lambda s_: -s_[0])
        pools = []
        for r_, x, y, floor in spots:
            if len(pools) >= 9:
                break
            if any(math.hypot(x - px, y - py) < r_ + pr + 200.0 for pr, px, py, _ in pools):
                continue
            pools.append((r_, x, y, floor))
        for r_, x, y, floor in pools:
            floor_z = floor + 10.0
            for _ in range(1 + (r_ >= 300.0) + (r_ >= 520.0)):
                count["fish"] += 1
                critter(kinds.FISH, critters["Fish"], x + rng.uniform(-50, 50), y + rng.uniform(-50, 50),
                        (floor_z + POOL_LEVEL - 12.0) * 0.5, rng.uniform(0, 360), "PondFish_%02d" % count["fish"], "Fish",
                        roam_radius=r_ * 0.8, water_level=POOL_LEVEL, floor_z=floor_z, speed=rng.uniform(55.0, 80.0),
                        scare_radius=350.0, body_length=rng.uniform(22.0, 30.0))
        count["pools"] = len(pools)
        if pools:
            r_, x, y, _ = pools[0]
            shots.append(shot("wildlife_pool_fish", x - r_ - 250.0, y, POOL_LEVEL + 260.0, x, y, POOL_LEVEL - 40.0, fov=60))

    log("wildlife: %s" % count)
    # Add looks at them to the -IslandShots list.
    path = os.path.join(PROJECT, "Saved", "island_shots.txt")
    if shots and os.path.exists(path):
        with open(path, "a", encoding="utf-8") as f:
            f.write("\n".join(shots) + "\n")


# ----------------------------------------------------------------------------------------------
# Wall torches (UNR-T40): TargetPoints tagged "Torch" on the ruined keep's walls and the islet broch,
# where ASoulsTorch (Source/UnrealSoulslike/Souls/SoulsTorch.cpp) mounts a burning torch in a bracket.
# ----------------------------------------------------------------------------------------------

TORCH_HEIGHT = 185.0      # bracket height above the ground in front of the wall
TORCH_SPACING = 450.0     # minimum distance between two torches
TORCH_MAX_KEEP = 6
TORCH_MAX_BROCH = 2


def torch_trace(world, start, end):
    """Horizontal trace -> (impact point, impact normal, hit actor) or None."""
    res = unreal.SystemLibrary.line_trace_single(world, start, end, unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, True, [],
                                                 unreal.DrawDebugTrace.NONE, True)
    hit = res[1] if isinstance(res, tuple) else res
    if hit is None:
        return None
    t = hit.to_tuple()
    if not t[0]:
        return None
    return t[5], t[7], t[9]


def torch_spots_on(world, actor, directions=16):
    """Points on actor's outer wall faces at torch height, reachable from open ground in front."""
    origin, extent = actor.get_actor_bounds(False)
    reach = max(extent.x, extent.y) + 250.0
    spots = []
    for k in range(directions):
        a = 2.0 * math.pi * k / directions
        dx, dy = math.cos(a), math.sin(a)
        sx, sy = origin.x + dx * reach, origin.y + dy * reach
        z = height(sx, sy) + TORCH_HEIGHT
        got = torch_trace(world, unreal.Vector(sx, sy, z), unreal.Vector(origin.x, origin.y, z))
        if not got or got[2] != actor or abs(got[1].z) > 0.35:
            continue
        p, n = got[0], got[1]
        nn = math.hypot(n.x, n.y) or 1.0
        nx, ny = n.x / nn, n.y / nn
        # Re-level at the ground right in front of this face, and check it is open, standable ground.
        fx, fy = p.x + nx * 90.0, p.y + ny * 90.0
        ground = trace_down(world, fx, fy, p.z + 400.0, height(fx, fy) - 200.0)
        if not ground or ground[1].z < 0.75 or abs(ground[0].z - height(fx, fy)) > 60.0:
            continue
        z = ground[0].z + TORCH_HEIGHT
        got = torch_trace(world, unreal.Vector(p.x + nx * 120.0, p.y + ny * 120.0, z), unreal.Vector(p.x - nx * 60.0, p.y - ny * 60.0, z))
        if not got or got[2] != actor or abs(got[1].z) > 0.35:
            continue
        p = got[0]
        spots.append((p.x + nx * 4.0, p.y + ny * 4.0, z, math.degrees(math.atan2(ny, nx))))
    return spots


def build_wall_torches(world, pads):
    keep_x, keep_y, keep_r = pads[1]
    keep, broch = [], []
    for a in actors_sub.get_all_level_actors():
        if not isinstance(a, unreal.StaticMeshActor):
            continue
        folder = str(a.get_folder_path())
        o = a.get_actor_location()
        if folder == "Ruins" and math.hypot(o.x - keep_x, o.y - keep_y) < keep_r * 1.6:
            keep.append(a)
        elif folder == "Islet" and "tower_round" in a.get_actor_label():
            broch.append(a)
    placed = []

    def place(actors, limit, name):
        count = 0
        for a in sorted(actors, key=lambda x: x.get_actor_label()):
            per_wall = 0
            for x, y, z, yaw in torch_spots_on(world, a):
                if count >= limit or per_wall >= 2:
                    break
                if any(math.hypot(x - px, y - py) < TORCH_SPACING for px, py in placed):
                    continue
                target_point(x, y, "Torch", "Torch_%02d_%s" % (len(placed) + 1, name), z=z, yaw=yaw)
                placed.append((x, y))
                count += 1
                per_wall += 1
        return count

    n_keep = place(keep, TORCH_MAX_KEEP, "Keep")
    n_broch = place(broch, TORCH_MAX_BROCH, "Broch")
    if n_keep == 0:
        warn("wall torches: no torch spot found on the keep's %d ruins" % len(keep))
    if n_broch == 0:
        warn("wall torches: no torch spot found on the islet broch")
    log("wall torches: %d on the keep, %d on the broch" % (n_keep, n_broch))


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


# ----------------------------------------------------------------------------------------------
# Field fire (UNR-T41): the burn-field texture the runtime fire (Source/.../Souls/SoulsFire.cpp)
# writes, sampled by the grass, foliage, prop and terrain materials, plus the flame and smoke materials.
# R = char (blackened), G = heat (ember glow), B = consumed (grass flattens away). Each helper is a
# no-op when the texture can't be made (C++ not rebuilt yet), so the island still builds.
# ----------------------------------------------------------------------------------------------

FIRE_FX = "/Game/Souls/FX"
FIRE_FIELD = FIRE_FX + "/T_FireField"
FIRE_FIELD_HALF = 32768.0  # cm; must match SoulsFireTuning::Half
FIRE_FIELD_RES = 512       # must match SoulsFireTuning::N
CHARCOAL = (0.022, 0.019, 0.017)
_fire_field = []


def fire_field_texture():
    if not _fire_field:
        tex = None
        try:
            ensure_dir(FIRE_FX)
            tex = unreal.SoulsFireLibrary.create_fire_field_texture(FIRE_FIELD, FIRE_FIELD_RES)
            if tex is not None:
                eal.save_loaded_asset(tex)
                log("fire field %s (%d px over +-%.0f cm)" % (FIRE_FIELD, FIRE_FIELD_RES, FIRE_FIELD_HALF))
        except Exception as exc:
            warn("fire field texture: %s" % exc)
        if tex is None:
            warn("no fire field texture: vegetation won't char (rebuild the C++ module)")
        _fire_field.append(tex)
    return _fire_field[0]


def fire_burn(g, wp):
    """Burn-field sample at this world XY: {"char", "heat", "gone"} scalars, or None."""
    tex = fire_field_texture()
    if tex is None:
        return None
    uv = mask_uv(g, g.mask(wp, r=True, g=True), FIRE_FIELD_HALF)
    s = g.tex(tex, uv, "linear", clamp=True)
    return {"char": (s, "R"), "heat": (s, "G"), "gone": (s, "B")}


def fire_char(g, color, wp, strength=1.0, glow=1.0):
    """Blacken color by the field's char; return (color, ember emissive or None)."""
    burn = fire_burn(g, wp)
    if burn is None:
        return color, None
    amount = burn["char"] if strength == 1.0 else g.mul(burn["char"], strength)
    color = g.lerp(color, CHARCOAL, amount)
    heat = burn["heat"]
    ember = g.mul(g.mul(g.mul(heat, heat), burn["char"]), (3.2 * glow, 0.75 * glow, 0.08 * glow))
    return color, ember


def fire_grass(g, color, wp, tip, wpo):
    """Grass blades: char, glowing tips while hot, and (consumed) flattened into the ground; wind dies as it chars."""
    burn = fire_burn(g, wp)
    if burn is None:
        return color, None, wpo
    color = g.lerp(color, CHARCOAL, burn["char"])
    ember = g.mul(g.mul(g.mul(burn["heat"], burn["heat"]), g.add(tip, 0.25)), (4.0, 0.9, 0.08))
    sink = g.append(g.append(0.0, 0.0), g.mul(g.mul(tip, burn["gone"]), -140.0))
    wpo = g.add(g.mul(wpo, g.one_minus(burn["char"])), sink)
    return color, ember, wpo


def fire_dissolve(g, opacity, uv, wp):
    """Masked foliage: leaves break up and vanish (noise threshold) as the ground under them is consumed."""
    burn = fire_burn(g, wp)
    if burn is None or not GRASS_NOISE:
        return opacity
    n = (g.tex(GRASS_NOISE[0], g.mul(uv, (3.0, 3.0)), "masks"), "G")
    n = g.add(g.mul(n, 0.9), 0.05)
    keep = g.smooth(g.sub(burn["gone"], 0.05), g.add(burn["gone"], 0.05), n)
    return g.mul(opacity, keep)


def build_fire_materials():
    """M_FireFlame (additive flickering cone the fire instances) and M_FireSmoke (lit translucent puff)."""
    ensure_dir(FIRE_FX)
    cone = eal.load_asset("/Engine/BasicShapes/Cone")
    b = cone.get_bounding_box() if cone else None
    zmin, zh = (b.min.z, max(b.max.z - b.min.z, 1.0)) if b else (-50.0, 100.0)

    mat, g = new_material("M_FireFlame", FIRE_FX)
    mat.set_editor_property("shading_model", E.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property("blend_mode", E.BlendMode.BLEND_ADDITIVE)
    mat.set_editor_property("used_with_instanced_static_meshes", True)
    if hasattr(E, "MaterialExpressionLocalPosition"):
        lp = g.new(E.MaterialExpressionLocalPosition)
        try:
            lp.set_editor_property("included_offsets", E.PositionIncludedOffsets.EXCLUDE_OFFSETS)
        except Exception:
            pass
        lz = g.mask(lp, b=True)
        h = g.sat(g.div(g.sub(lz, zmin), zh))
    else:
        h = g.sat(g.one_minus(g.mask(g.new(E.MaterialExpressionTextureCoordinate), g=True)))
    rnd = g.new(E.MaterialExpressionPerInstanceRandom)
    t = g.time()
    flick = g.add(0.72, g.mul(g.mul(g.sine(g.add(g.mul(t, 13.0), g.mul(rnd, 40.0))),
                                    g.sine(g.add(g.mul(t, 7.3), g.mul(rnd, 11.0)))), 0.28))
    col = g.lerp((9.0, 4.2, 1.0), (3.2, 0.55, 0.06), g.sat(g.mul(h, 1.3)))
    fade = g.op(E.MaterialExpressionPower, {"Base": g.one_minus(h), "Exp": 0.9})
    soft = g.one_minus(g.new(E.MaterialExpressionFresnel, exponent=2.0))
    g.out(g.mul(g.mul(g.mul(col, fade), soft), flick), E.MaterialProperty.MP_EMISSIVE_COLOR)
    # Tongues lick sideways, more toward the tip.
    h2 = g.mul(h, h)
    sx = g.mul(g.sine(g.add(g.add(g.mul(t, 6.0), g.mul(rnd, 30.0)), g.div(g.mask(g.world(), b=True), 45.0))), g.mul(h2, 14.0))
    sy = g.mul(g.sine(g.add(g.mul(t, 5.1), g.mul(rnd, 17.0))), g.mul(h2, 10.0))
    g.out(g.append(g.append(sx, sy), 0.0), E.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    finish_material(mat)

    mat, g = new_material("M_FireSmoke", FIRE_FX)
    mat.set_editor_property("blend_mode", E.BlendMode.BLEND_TRANSLUCENT)
    mat.set_editor_property("used_with_instanced_static_meshes", True)
    fade = g.new(E.MaterialExpressionPerInstanceCustomData, data_index=0, const_default_value=0.0)
    soft = g.one_minus(g.new(E.MaterialExpressionFresnel, exponent=1.5))
    opacity = g.mul(fade, soft)
    if GRASS_NOISE:
        pan = g.add(g.div(g.mask(g.world(), r=True, g=True), (380.0, 380.0)), g.mul(g.time(), (0.02, 0.05)))
        opacity = g.mul(opacity, g.lerp(0.45, 1.0, (g.tex(GRASS_NOISE[0], pan, "masks"), "R")))
    g.out((0.09, 0.085, 0.08), E.MaterialProperty.MP_BASE_COLOR)
    g.out(1.0, E.MaterialProperty.MP_ROUGHNESS)
    g.out(opacity, E.MaterialProperty.MP_OPACITY)
    finish_material(mat)
    log("fire materials: M_FireFlame (cone z %.0f..%.0f), M_FireSmoke" % (zmin, zmin + zh))


def build_fire_assets():
    try:
        fire_field_texture()
        build_fire_materials()
    except Exception as exc:
        warn("fire assets: %s" % exc)


# ----------------------------------------------------------------------------------------------
# Ship graveyard (UNR-T63): the bay off the start beach and its approaches are choked with wrecked great
# ships - on their sides in the shallows, listing half-sunk, broken in two, masts standing out of the water,
# and far out, broken on the reefs - so no one makes it to this island. One rowboat lies dragged up the
# sand a few metres from the player start: where the player came ashore. Kenney's CC0 Pirate Kit, wrecked
# and baked to life size by tools/convert_ships.py into SourceArt/Environment/Ships.
# ----------------------------------------------------------------------------------------------

SHIP_ART = os.path.join(ART, "Ships")
SHIP_HULLS = ("Wreck_Large", "Wreck_Medium", "Wreck_Small", "Wreck_Ghost", "Wreck_Hulk", "Wreck_Large_Bow",
              "Wreck_Large_Stern", "Wreck_Medium_Bow", "Wreck_Medium_Stern", "Wreck_Mast", "Rowboat")
SHIP_DEBRIS = ("Oar", "Barrel", "Crate", "CrateBottles", "Planks")
# Slot looks: (tint, desaturation of the kit's bright palette, roughness, moss on upward faces)
SHIP_LOOKS = {"Wood": ((0.46, 0.4, 0.33), 0.6, 0.85, 0.55), "Cloth": ((0.4, 0.38, 0.34), 0.85, 0.92, 0.15),
              "Weed": ((0.22, 0.28, 0.13), 0.3, 0.7, 0.0)}
ROWBOAT_LOOK = ((0.66, 0.54, 0.43), 0.4, 0.72, 0.1)
# (mesh, bearing offset from the start beach (radians), offshore of the waterline: cm, or <= 1 a fraction of the
#  open water out to the sea walls (only ~90 m off this beach), how it lies, yaw from facing inland (deg), roll,
#  pitch). The gentle shore deepens ~0.07 d + 0.0016 d^2 metres at d metres out: shallows within ~18 m, half-sunk
#  to ~45 m, beyond that deep enough to swallow a hull up to its masts.
SHIP_WRECKS = [
    ("Wreck_Large", -0.34, 900.0, "side", 70.0, 78.0, 4.0),
    ("Wreck_Hulk", 0.30, 700.0, "beached", -25.0, 14.0, 5.0),  # driven bow-first onto the sand
    ("Wreck_Large_Bow", 0.12, 3600.0, "end", 60.0, 18.0, 28.0),  # broken in two: the bow rears up...
    ("Wreck_Large_Stern", 0.21, 3000.0, "list", 150.0, -32.0, -10.0),  # ...the stern lies a few lengths off
    ("Wreck_Medium_Bow", -0.48, 1500.0, "side", -40.0, -72.0, 10.0),
    ("Wreck_Medium_Stern", -0.41, 4300.0, "end", 30.0, 12.0, -30.0),
    ("Wreck_Small", 0.50, 1100.0, "side", 160.0, 82.0, -6.0),
    ("Wreck_Ghost", -0.16, 4600.0, "list", -20.0, 38.0, 8.0),
    ("Wreck_Medium", 0.02, 0.66, "sunk", 75.0, -9.0, 5.0),
    ("Wreck_Mast", -0.07, 0.72, "mast", 0.0, 14.0, -10.0),
    ("Wreck_Mast", 0.36, 0.6, "mast", 40.0, -18.0, 6.0),
    ("Wreck_Large", 0.42, 0.7, "sunk", -50.0, 12.0, -4.0),
    ("Wreck_Hulk", -0.27, 0.64, "sunk", 100.0, 6.0, 9.0),
    # The approaches: broken on the reefs out by the edge of the world.
    ("Wreck_Ghost", -0.12, 0.9, "reef", 35.0, 34.0, 10.0),
    ("Wreck_Large", 0.19, 0.97, "reef", -60.0, -28.0, -12.0),
    ("Wreck_Medium", -0.36, 0.95, "reef", 80.0, 40.0, 6.0),
    ("Wreck_Small", 0.31, 0.86, "reef", 10.0, -45.0, 15.0),
    ("Wreck_Mast", 0.06, 1.0, "reef_mast", 0.0, 20.0, 0.0),
]
SHIP_START_CLEAR = 5000.0  # cm round the player start kept free of wrecks (the start beach, the knight demo's swim)


def import_ships():
    """SourceArt/Environment/Ships/SM_<Name>.obj (baked by tools/convert_ships.py) -> /Game/Island/Models/Ships/<Name>."""
    out = {}
    for name in SHIP_HULLS + SHIP_DEBRIS:
        src = os.path.join(SHIP_ART, "SM_%s.obj" % name)
        if not os.path.exists(src):
            warn("ship mesh %s missing (run tools/convert_ships.py in Blender)" % src)
            continue
        dest = "%s/Models/Ships/%s" % (ROOT, name)
        if REIMPORT or not (eal.does_directory_exist(dest) and assets_of(dest, unreal.StaticMesh)):
            if eal.does_directory_exist(dest):
                eal.delete_directory(dest)
            import_files([src], dest)
            for mesh in assets_of(dest, unreal.StaticMesh):
                # Hulls are climbable: collide on their real shape. Debris never blocks.
                nanite_and_collision(mesh, nanite=False, complex_collision=name in SHIP_HULLS)
        meshes = assets_of(dest, unreal.StaticMesh)
        if not meshes:
            warn("ship %s: no static mesh imported" % name)
            continue
        mesh = meshes[0]
        if name in SHIP_HULLS and mesh.get_editor_property("body_setup") is None:
            warn("ship %s: no collision body" % name)
        out[name] = mesh
        b = mesh.get_bounding_box()
        log("ship %s: size=(%.0f, %.0f, %.0f) slots=%s" % (name, b.max.x - b.min.x, b.max.y - b.min.y, b.max.z - b.min.z,
                                                          [str(s.get_editor_property("material_slot_name"))
                                                           for s in mesh.get_editor_property("static_materials")]))
    return out


def ship_colormap():
    """The kit's palette texture (every face samples a flat colour cell): nearest filtering keeps cells crisp."""
    dest = "%s/Models/Ships/Tex" % ROOT
    if REIMPORT or not assets_of(dest, unreal.Texture2D) if eal.does_directory_exist(dest) else True:
        import_files([os.path.join(SHIP_ART, "colormap.png")], dest)
        for t in assets_of(dest, unreal.Texture2D):
            setp(t, "filter", unreal.TextureFilter.TF_NEAREST)
            eal.save_loaded_asset(t)
    tex = assets_of(dest, unreal.Texture2D)
    return tex[0] if tex else None


def island_texture(set_name, tag):
    for t in assets_of("%s/Textures/%s" % (ROOT, set_name), unreal.Texture2D):
        if tag in t.get_name():
            return t
    return None


def build_ship_material(colormap):
    """M_Shipwreck: the kit palette, desaturated and tinted to old dark wood, weathered in vertical streaks,
    soaked dark below ~3 m above the sea, barnacles and weed in a ragged band at the waterline, green-black
    slime under it, moss on the decks. Two-sided: the broken hulls are open shells."""
    mat, g = new_material("M_Shipwreck", ROOT + "/Materials")
    setp(mat, "two_sided", True)
    noise_tex = eal.load_asset(ROOT + "/Textures/Generated/T_IslandNoise")
    rock = island_texture("seaside_rock", "_diff_")
    moss = island_texture("Grass003", "_Color")

    def sparam(name, value):
        return g.new(E.MaterialExpressionScalarParameter, parameter_name=name, default_value=value)

    uv = g.new(E.MaterialExpressionTextureCoordinate)
    if colormap:
        base = g.new(E.MaterialExpressionTextureSampleParameter2D, parameter_name="Base", texture=colormap,
                     sampler_type=E.MaterialSamplerType.SAMPLERTYPE_COLOR, sampler_source=E.SamplerSourceMode.SSM_CLAMP_WORLD_GROUP_SETTINGS)
        g.link(uv, base, "UVs")
        base_rgb = (base, "RGB")
    else:
        base_rgb = (0.5, 0.36, 0.24)
    tint = g.new(E.MaterialExpressionVectorParameter, parameter_name="Tint", default_value=unreal.LinearColor(1, 1, 1, 1))
    lum = g.dot(base_rgb, (0.3, 0.59, 0.11))
    color = g.mul(g.lerp(base_rgb, lum, sparam("Desaturate", 0.6)), (tint, "RGB"))

    wp = g.world()
    wz = g.mask(wp, b=True)
    along = g.add(g.mask(wp, r=True), g.mask(wp, g=True))
    if noise_tex:
        streak = (g.tex(noise_tex, g.append(g.div(along, 420.0), g.div(wz, 2600.0)), "masks"), "G")
        blotch = (g.tex(noise_tex, g.div(g.mask(wp, r=True, g=True), (380.0, 380.0)), "masks"), "B")
        color = g.mul(color, g.lerp(0.6, 1.15, streak))
        ragged = g.mul(g.sub(blotch, 0.5), 1.0)
    else:
        streak, ragged = 0.5, 0.0
    # Soaked: dark below ~3.5 m above the sea, fading up the hull with a ragged edge.
    wet = g.mul(g.one_minus(g.smooth(-50.0, 380.0, g.add(wz, g.mul(ragged, 160.0)))), sparam("Wet", 1.0))
    color = g.lerp(color, g.mul(color, 0.42), wet)
    # Barnacles and weed in a band round the waterline.
    band = g.mul(g.smooth(-320.0, -60.0, wz), g.one_minus(g.smooth(30.0, 170.0, g.add(wz, g.mul(ragged, 220.0)))))
    side_uv = g.append(g.div(along, 110.0), g.div(wz, 110.0))
    barnacle = g.mul((g.tex(rock, side_uv, "color"), "RGB"), (0.46, 0.47, 0.43)) if rock else g.val((0.2, 0.2, 0.18))
    weed = g.mul((g.tex(moss, side_uv, "color"), "RGB"), (0.22, 0.3, 0.14)) if moss else g.val((0.05, 0.07, 0.03))
    color = g.lerp(color, g.lerp(barnacle, weed, streak), g.mul(band, 0.9))
    # Green-black slime on everything under the water.
    deep = g.one_minus(g.smooth(-450.0, -80.0, wz))
    color = g.lerp(color, (0.035, 0.05, 0.03), g.mul(deep, 0.8))
    # Moss on the decks and other upward faces above the water.
    vnz = g.mask(g.new(E.MaterialExpressionVertexNormalWS), b=True)
    moss_w = g.mul(g.mul(g.smooth(0.5, 0.85, g.add(vnz, g.mul(ragged, 0.6))), sparam("Moss", 0.5)), g.smooth(-20.0, 120.0, wz))
    moss_col = g.mul((g.tex(moss, g.div(g.mask(wp, r=True, g=True), (260.0, 260.0)), "color"), "RGB"), (0.5, 0.6, 0.38)) if moss \
        else g.val((0.08, 0.1, 0.05))
    color = g.lerp(color, moss_col, moss_w)
    g.out(color, E.MaterialProperty.MP_BASE_COLOR)
    rough = g.lerp(sparam("Roughness", 0.85), 0.3, g.mul(wet, 0.8))
    g.out(g.lerp(rough, 0.92, band), E.MaterialProperty.MP_ROUGHNESS)
    finish_material(mat)
    return mat


def ship_instance(master, name, look, wet=1.0):
    tint, desat, rough, moss = look
    folder = ROOT + "/Materials/Ships"
    ensure_dir(folder)
    path = "%s/%s" % (folder, name)
    mi = eal.load_asset(path) if eal.does_asset_exist(path) else \
        tools.create_asset(name, folder, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mel.set_material_instance_parent(mi, master)
    mel.set_material_instance_vector_parameter_value(mi, "Tint", unreal.LinearColor(tint[0], tint[1], tint[2], 1.0))
    for param, value in (("Desaturate", desat), ("Roughness", rough), ("Moss", moss), ("Wet", wet)):
        mel.set_material_instance_scalar_parameter_value(mi, param, value)
    eal.save_loaded_asset(mi)
    return mi


def build_furrow_material():
    """M_DragFurrow: a deferred decal of churned, wet, darker sand with soft ragged edges."""
    mat, g = new_material("M_DragFurrow", ROOT + "/Materials")
    setp(mat, "material_domain", E.MaterialDomain.MD_DEFERRED_DECAL)
    setp(mat, "blend_mode", E.BlendMode.BLEND_TRANSLUCENT)
    uv = g.new(E.MaterialExpressionTextureCoordinate)
    edge_u = g.one_minus(g.smooth(0.3, 0.5, g.abs(g.sub(g.mask(uv, r=True), 0.5))))
    edge_v = g.one_minus(g.smooth(0.3, 0.5, g.abs(g.sub(g.mask(uv, g=True), 0.5))))
    opacity = g.mul(edge_u, edge_v)
    noise_tex = eal.load_asset(ROOT + "/Textures/Generated/T_IslandNoise")
    if noise_tex:
        scuff = (g.tex(noise_tex, g.div(g.mask(g.world(), r=True, g=True), (90.0, 90.0)), "masks"), "B")
        opacity = g.mul(opacity, g.lerp(0.35, 0.9, scuff))
    g.out((0.11, 0.09, 0.07), E.MaterialProperty.MP_BASE_COLOR)
    g.out(0.45, E.MaterialProperty.MP_ROUGHNESS)
    g.out(g.sat(opacity), E.MaterialProperty.MP_OPACITY)
    finish_material(mat)
    return mat


def build_ship_graveyard(models, route, start):
    ships = import_ships()
    if "Wreck_Large" not in ships:
        warn("ship graveyard: no wreck meshes; skipped")
        return
    master = build_ship_material(ship_colormap())
    looks = {k: ship_instance(master, "MI_Ship_%s" % k, v) for k, v in SHIP_LOOKS.items()}
    looks["Rowboat"] = ship_instance(master, "MI_Ship_Rowboat", ROWBOAT_LOOK, wet=0.6)
    for name, mesh in ships.items():
        for i, slot in enumerate(mesh.get_editor_property("static_materials")):
            s = str(slot.get_editor_property("material_slot_name")).lower()
            if name in ("Rowboat", "Oar"):
                mi = looks["Rowboat"]
            elif s.startswith("cloth"):
                mi = looks["Cloth"]
            elif s.startswith("weed"):
                mi = looks["Weed"]
            else:
                mi = looks["Wood"]
            mesh.set_material(i, mi)
        eal.save_loaded_asset(mesh)

    rng = random.Random(6363)
    sx, sy = start
    base_ang = math.atan2(sy, sx)
    grace = route.offset(0.035, 450.0)
    ix, iy, ir = BIOMES["islet"]
    reef_rocks = (models.get("boulder_01") or []) + (models.get("rock_moss_set_01") or [])
    count = {"wrecks": 0, "masts": 0, "reef_rocks": 0, "skipped": 0, "debris": 0, "flotsam": 0}
    placed = []  # (x, y, footprint radius)
    near = []    # wrecks in the shallows, for flotsam

    def footprint(mesh):
        b = mesh.get_bounding_box()
        return 0.5 * max(b.max.x - b.min.x, b.max.y - b.min.y)

    def free(x, y, r):
        return (math.hypot(x - sx, y - sy) > SHIP_START_CLEAR + r * 0.5
                and math.hypot(x - grace[0], y - grace[1]) > 1500.0 + r
                and LIB.get_path_distance(x, y) > r + 800.0
                and math.hypot(x - ix, y - iy) > ir + r + 2500.0
                and math.hypot(x, y) < SEA_WALL_RADIUS - r - 1500.0
                and height(x, y) < 60.0  # in the water or on the wet sand, never up the beach
                and all(math.hypot(x - px, y - py) > (r + pr) * 0.75 for px, py, pr in placed))

    for k, (name, dang, off, how, yaw_rel, roll, pitch) in enumerate(SHIP_WRECKS):
        mesh = ships.get(name)
        if mesh is None:
            continue
        r = footprint(mesh)
        spot = None
        for attempt in range(14):
            ang = base_ang + dang + (0.035 * ((attempt + 1) // 2) * (1 if attempt % 2 else -1))
            wx, wy = shore_point(ang, 0.0)
            room = SEA_WALL_RADIUS - 1700.0 - r - math.hypot(wx, wy)  # open water out to the sea walls
            d = off * room if off <= 1.0 else off + 250.0 * (attempt // 4)
            d = min(d, room)
            if d < 300.0:
                continue
            x, y = wx + math.cos(ang) * d, wy + math.sin(ang) * d
            if free(x, y, r):
                spot = (x, y, ang)
                break
        if spot is None:
            count["skipped"] += 1
            log("ship graveyard: no room for %s (%s)" % (name, how))
            continue
        x, y, ang = spot
        yaw = math.degrees(ang) + 180.0 + yaw_rel + rng.uniform(-8.0, 8.0)
        roll += rng.uniform(-4.0, 4.0)
        pitch += rng.uniform(-3.0, 3.0)
        ground = height(x, y)
        label = "Wreck_%02d_%s" % (k + 1, name)
        folder = "ShipGraveyard/Wrecks"
        if how in ("side", "beached", "list", "end"):
            # Bedded into the sand by a few metres (the bounds' low corner is a mast tip or a keel edge).
            embed = {"side": 300.0, "beached": 150.0, "list": 250.0, "end": 400.0}[how]
            place_mesh(mesh, x, y, yaw=yaw, sink=0.0, pitch=pitch, roll=roll, z=ground - embed, folder=folder, label=label)
            near.append((x, y, r))
        elif how in ("sunk", "mast"):
            # Settled on the bottom (decks awash at worst); the masts stand out of the water.
            place_mesh(mesh, x, y, yaw=yaw, sink=0.0, pitch=pitch, roll=roll, z=max(ground, -2400.0) - 150.0, folder=folder, label=label)
        else:
            # Far out on the approaches: impaled on a reef that breaks the surface.
            for j in (range(rng.randint(2, 4)) if reef_rocks else ()):
                rock = reef_rocks[rng.randrange(len(reef_rocks))]
                b = rock.get_bounding_box()
                s = rng.uniform(5.0, 8.0)
                top = rng.uniform(-40.0, 260.0)
                sz = (1500.0 + top) / max(1.0, (b.max.z - b.min.z) * s)
                a_ = rng.uniform(0, 2 * math.pi)
                rr = rng.uniform(0.2, 0.8) * r
                place_mesh(rock, x + math.cos(a_) * rr, y + math.sin(a_) * rr, yaw=rng.uniform(0, 360), scale=(s, s, sz),
                           sink=0.0, z=-1500.0, folder="ShipGraveyard/Reefs", label="%s_Reef%d" % (label, j + 1))
                count["reef_rocks"] += 1
            place_mesh(mesh, x, y, yaw=yaw, sink=0.0, pitch=pitch, roll=roll, z=-1500.0 if how == "reef_mast" else -650.0,
                       folder=folder, label=label)
        placed.append((x, y, r))
        count["masts" if "Mast" in name else "wrecks"] += 1

    # The rowboat the player came ashore in: dragged bow-first up the sand a few metres beside the start.
    n = LIB.get_island_normal(sx, sy)
    hn = math.hypot(n.x, n.y)
    inland = (-n.x / hn, -n.y / hn) if hn > 0.02 else (-math.cos(base_ang), -math.sin(base_ang))
    ri = (-math.cos(base_ang), -math.sin(base_ang))
    inland = (inland[0] + ri[0], inland[1] + ri[1])  # the beach's fall line, steadied by the bearing
    nn = math.hypot(*inland) or 1.0
    inland = (inland[0] / nn, inland[1] / nn)
    side = (-inland[1], inland[0])
    away = 1.0 if math.hypot(sx + side[0] * 480.0 - grace[0], sy + side[1] * 480.0 - grace[1]) > \
        math.hypot(sx - side[0] * 480.0 - grace[0], sy - side[1] * 480.0 - grace[1]) else -1.0
    side = (side[0] * away, side[1] * away)
    in_yaw = math.degrees(math.atan2(inland[1], inland[0]))
    boat_at = None
    boat = ships.get("Rowboat")
    if boat:
        bx, by = sx + side[0] * 480.0 - inland[0] * 150.0, sy + side[1] * 480.0 - inland[1] * 150.0
        rise = height(bx + inland[0] * 150.0, by + inland[1] * 150.0) - height(bx - inland[0] * 150.0, by - inland[1] * 150.0)
        b = boat.get_bounding_box()
        hz = b.max.z - b.min.z
        a = place_mesh_at(boat, bx, by, height(bx, by) + hz * 0.5 - 16.0, yaw=in_yaw + 7.0,
                          pitch=math.degrees(math.atan2(rise, 300.0)) + 2.0, roll=9.0, folder="ShipGraveyard/Landing")
        a.set_actor_label("Rowboat_Landing")
        a.get_editor_property("static_mesh_component").set_editor_property("receives_decals", False)
        boat_at = (bx, by)
        oar = ships.get("Oar")
        if oar:
            ox, oy = bx + side[0] * 175.0 + inland[0] * 30.0, by + side[1] * 175.0 + inland[1] * 30.0
            place_mesh(oar, ox, oy, yaw=in_yaw + 14.0, sink=0.35, roll=rng.uniform(-6, 6), pitch=rng.uniform(-3, 3),
                       folder="ShipGraveyard/Landing", label="Rowboat_Oar", collide=False)
        # The drag furrow from the stern down to the waterline.
        half = (b.max.x - b.min.x) * 0.5
        tx, ty = bx - inland[0] * half, by - inland[1] * half
        length = 100.0
        while length < 3000.0 and height(tx - inland[0] * length, ty - inland[1] * length) > 3.0:
            length += 100.0
        cx, cy = tx - inland[0] * length * 0.5, ty - inland[1] * length * 0.5
        try:
            furrow = spawn(unreal.DecalActor, (cx, cy, height(cx, cy)), unreal.Rotator(pitch=-90.0, yaw=in_yaw),
                           "Rowboat_DragFurrow", "ShipGraveyard/Landing")
            furrow.set_decal_material(build_furrow_material())
            # Decal X projects down; after pitch -90 its Z runs along in_yaw (the furrow) and Y across it.
            setp(furrow.get_editor_property("decal"), "decal_size", unreal.Vector(160.0, 80.0, length * 0.5 + 60.0))
        except Exception as exc:
            warn("rowboat drag furrow: %s" % exc)
        log("rowboat at (%.0f, %.0f) %.0f cm from the start, yaw %.0f, furrow %.0f cm" % (
            bx, by, math.hypot(bx - sx, by - sy), in_yaw, length))

    # Wreckage washed up along the tideline of the bay, and flotsam bobbing round the wrecks in the shallows.
    junk = [ships[n_] for n_ in ("Planks", "Planks", "Barrel", "Crate", "CrateBottles", "Barrel", "Planks") if n_ in ships]
    if junk:
        for _ in range(140):
            if count["debris"] >= 34:
                break
            ang = base_ang + rng.uniform(-0.55, 0.55)
            x, y = shore_point(ang, rng.uniform(2.0, 45.0))
            if (math.hypot(x - sx, y - sy) < 700.0 or LIB.get_path_distance(x, y) < 300.0
                    or (boat_at and math.hypot(x - boat_at[0], y - boat_at[1]) < 600.0)):
                continue
            mesh = junk[rng.randrange(len(junk))]
            lying = "Barrel" in mesh.get_name() and rng.random() < 0.6
            place_mesh(mesh, x, y, yaw=rng.uniform(0, 360), pitch=rng.uniform(-10, 10),
                       roll=90.0 + rng.uniform(-5, 5) if lying else rng.uniform(-14, 14), sink=0.25,
                       folder="ShipGraveyard/Debris", label="Wreckage_%02d" % (count["debris"] + 1), collide=False)
            count["debris"] += 1
        for wx, wy, r in near:
            for _ in range(rng.randint(2, 4)):
                x, y = wx + rng.gauss(0, r * 0.6), wy + rng.gauss(0, r * 0.6)
                if not -250.0 < height(x, y) < -10.0 or math.hypot(x - sx, y - sy) < SHIP_START_CLEAR:
                    continue
                place_mesh(junk[rng.randrange(len(junk))], x, y, yaw=rng.uniform(0, 360), pitch=rng.uniform(-15, 15),
                           roll=rng.uniform(-20, 20), sink=0.55, z=0.0, folder="ShipGraveyard/Debris",
                           label="Flotsam_%02d" % (count["flotsam"] + 1), collide=False)
                count["flotsam"] += 1
    log("ship graveyard: %s" % count)

    # Shots for -IslandShots: out over the bay from the start, the whole graveyard from above, the landing.
    shots = []
    cx, cy = sx + inland[0] * 350.0 - side[0] * 250.0, sy + inland[1] * 350.0 - side[1] * 250.0
    shots.append(shot("ship_graveyard_bay", cx, cy, height(cx, cy) + 320.0, sx - inland[0] * 7000.0, sy - inland[1] * 7000.0, 250.0, fov=80))
    cx, cy = sx + inland[0] * 3500.0 - side[0] * 1500.0, sy + inland[1] * 3500.0 - side[1] * 1500.0
    shots.append(shot("ship_graveyard_wide", cx, cy, height(cx, cy) + 3800.0, sx - inland[0] * 8000.0, sy - inland[1] * 8000.0, 0.0, fov=75))
    if boat_at:
        bx, by = boat_at
        cx, cy = bx + side[0] * 650.0 - inland[0] * 380.0, by + side[1] * 650.0 - inland[1] * 380.0
        shots.append(shot("rowboat_landing", cx, cy, max(height(cx, cy), 0.0) + 190.0, bx, by, height(bx, by) + 40.0, fov=55))
    path = os.path.join(PROJECT, "Saved", "island_shots.txt")
    if os.path.exists(path):
        with open(path, "a", encoding="utf-8") as f:
            f.write("\n".join(shots) + "\n")


SHELL_ART = os.path.join(ART, "Shells")
SHELL_KINDS = ("Cockle", "CockleUp", "Clam", "Mussel", "Scallop", "Whelk")
# (kind, slot): (linear tint, banding from the shell's own texture 0..1, roughness). Slots come from the baked .mtl.
SHELL_LOOKS = {
    ("Cockle", "Outer"): ((0.56, 0.47, 0.35), 0.8, 0.72),     # sandy, ridged bands
    ("Cockle", "Inner"): ((0.66, 0.61, 0.53), 0.0, 0.55),
    ("CockleUp", "Outer"): ((0.52, 0.44, 0.33), 0.8, 0.72),
    ("CockleUp", "Inner"): ((0.70, 0.65, 0.57), 0.0, 0.5),    # the pale hollow showing
    ("Clam", "Outer"): ((0.70, 0.68, 0.62), 0.0, 0.68),       # chalky, sun-bleached
    ("Mussel", "Outer"): ((0.014, 0.017, 0.034), 0.0, 0.32),  # blue-black, glossy
    ("Mussel", "Inner"): ((0.36, 0.42, 0.54), 0.0, 0.25),     # pearly blue nacre
    ("Scallop", "Outer"): ((0.70, 0.44, 0.40), 0.9, 0.62),    # pinkish
    ("Whelk", "Outer"): ((0.60, 0.52, 0.40), 0.9, 0.6),
}
SHELL_CULL = 3000.0  # cm; a few centimetres of shell is invisible past this


def import_shells():
    """SourceArt/Environment/Shells/SM_Shell_<Kind>.obj (baked by tools/convert_shells.py) -> /Game/Island/Models/Shells/<Kind>."""
    out = {}
    for kind in SHELL_KINDS:
        src = os.path.join(SHELL_ART, "SM_Shell_%s.obj" % kind)
        if not os.path.exists(src):
            warn("shell mesh %s missing (run tools/convert_shells.py in Blender)" % src)
            continue
        dest = "%s/Models/Shells/%s" % (ROOT, kind)
        if REIMPORT or not (eal.does_directory_exist(dest) and assets_of(dest, unreal.StaticMesh)):
            if eal.does_directory_exist(dest):
                eal.delete_directory(dest)
            import_files([src], dest)
            for mesh in assets_of(dest, unreal.StaticMesh):
                nanite_and_collision(mesh, nanite=False, complex_collision=False)
                mesh.set_editor_property("body_setup", None)  # never blocks or catches traces
                eal.save_loaded_asset(mesh)
        meshes = assets_of(dest, unreal.StaticMesh)
        if not meshes:
            warn("shell %s: no static mesh imported" % kind)
            continue
        out[kind] = meshes[0]
        b = meshes[0].get_bounding_box()
        log("shell %s: size=(%.1f, %.1f, %.1f) cm" % (kind, b.max.x - b.min.x, b.max.y - b.min.y, b.max.z - b.min.z))
    return out


def shell_texture(png):
    """One shared import per shell texture (the .mtl's map_Kd), under /Game/Island/Models/Shells/Tex."""
    dest = "%s/Models/Shells/Tex" % ROOT
    name = os.path.splitext(png)[0].lower()

    def find():
        if not eal.does_directory_exist(dest):
            return None
        return next((t for t in assets_of(dest, unreal.Texture2D) if name in t.get_name().lower()), None)

    tex = None if REIMPORT else find()
    if tex is None:
        src = os.path.join(SHELL_ART, png)
        if not os.path.exists(src):
            return None
        import_files([src], dest)
        tex = find()
    return tex


def build_shell_materials(shells):
    """M_Shell: tint x (optionally) the shell's own texture as light/dark banding, a little per-instance colour
    variety, darker and glossier where the sea last wet it (the lowest ~half metre above sea level). Two-sided:
    the valves are thin open shells. One MI_Shell_<Kind>_<Slot> per mesh slot."""
    mat, g = new_material("M_Shell", ROOT + "/Materials")
    setp(mat, "two_sided", True)
    setp(mat, "used_with_instanced_static_meshes", True)

    def sparam(name, value):
        return g.new(E.MaterialExpressionScalarParameter, parameter_name=name, default_value=value)

    tint = (g.new(E.MaterialExpressionVectorParameter, parameter_name="Tint", default_value=unreal.LinearColor(1, 1, 1, 1)), "RGB")
    first_tex = next((t for t in (shell_texture(p) for p in sorted(os.listdir(SHELL_ART)) if p.endswith(".png")) if t), None) \
        if os.path.isdir(SHELL_ART) else None
    color = tint
    if first_tex:
        uv = g.new(E.MaterialExpressionTextureCoordinate)
        base = g.new(E.MaterialExpressionTextureSampleParameter2D, parameter_name="Base", texture=first_tex,
                     sampler_type=E.MaterialSamplerType.SAMPLERTYPE_COLOR, sampler_source=E.SamplerSourceMode.SSM_WRAP_WORLD_GROUP_SETTINGS)
        g.link(uv, base, "UVs")
        lum = g.dot((base, "RGB"), (0.3, 0.59, 0.11))
        color = g.mul(tint, g.lerp(1.0, g.minx(g.mul(lum, 2.2), 1.6), sparam("Banding", 0.0)))
    if hasattr(E, "MaterialExpressionPerInstanceRandom"):
        color = g.mul(color, g.lerp(0.8, 1.15, g.new(E.MaterialExpressionPerInstanceRandom)))
    wet = g.one_minus(g.smooth(8.0, 55.0, g.mask(g.world(), b=True)))
    color = g.lerp(color, g.mul(color, 0.6), wet)
    g.out(color, E.MaterialProperty.MP_BASE_COLOR)
    g.out(g.lerp(sparam("Roughness", 0.6), 0.2, wet), E.MaterialProperty.MP_ROUGHNESS)
    finish_material(mat)

    folder = ROOT + "/Materials/Shells"
    ensure_dir(folder)
    for kind, mesh in shells.items():
        mtl = read_mtl(os.path.join(SHELL_ART, "SM_Shell_%s.mtl" % kind))
        for i, slot in enumerate(mesh.get_editor_property("static_materials")):
            slot_name = str(slot.get_editor_property("material_slot_name"))
            key = next((k for k in mtl if k == slot_name), None) or \
                next((k for k in mtl if slot_name.lower().startswith(k.lower()) or k.lower().startswith(slot_name.lower())), None) or \
                (next(iter(mtl)) if len(mtl) == 1 else None)
            part = key.split("_", 1)[1] if key and "_" in key else "Outer"
            tint_rgb, banding, rough = SHELL_LOOKS.get((kind, part), ((0.6, 0.55, 0.45), 0.0, 0.6))
            mi_name = "MI_Shell_%s_%s" % (kind, part)
            path = "%s/%s" % (folder, mi_name)
            mi = eal.load_asset(path) if eal.does_asset_exist(path) else \
                tools.create_asset(mi_name, folder, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
            mel.set_material_instance_parent(mi, mat)
            mel.set_material_instance_vector_parameter_value(mi, "Tint", unreal.LinearColor(tint_rgb[0], tint_rgb[1], tint_rgb[2], 1.0))
            mel.set_material_instance_scalar_parameter_value(mi, "Roughness", rough)
            tex = shell_texture(mtl[key]["map"]) if key and "map" in mtl.get(key, {}) else None
            if first_tex and tex:
                mel.set_material_instance_texture_parameter_value(mi, "Base", tex)
                mel.set_material_instance_scalar_parameter_value(mi, "Banding", banding)
            eal.save_loaded_asset(mi)
            mesh.set_material(i, mi)
        eal.save_loaded_asset(mesh)


def build_shells(world, models, start):
    """Sea shells on the sand (UNR-T61): cockles, a pale clam, mussels, scallops and whelks at real size (2-12 cm),
    thickest in a wrack line along the tideline, thinner up the beach, and a few little drifts on the start beach.
    Instanced, no collision, culled at SHELL_CULL."""
    shells = import_shells()
    if not shells:
        return
    build_shell_materials(shells)
    rng = random.Random(6161)
    bivalves = [shells[k] for k in ("Cockle", "CockleUp", "Clam", "Mussel") if k in shells]
    spirals = [shells[k] for k in ("Scallop", "Whelk") if k in shells]
    every = bivalves + spirals

    def sand(x, y):
        return normal_z(x, y) > 0.9 and biome(x, y)[3] < 0.2

    def wrack(x, y):
        # The strand line: where the last tide dropped its load, ~5-55 cm above the sea, thickest at 15-40 cm.
        h = height(x, y)
        return 5.0 < h < 55.0 and (15.0 < h < 40.0 or rng.random() < 0.4) and sand(x, y)

    def upper(x, y):
        h = height(x, y)
        return 55.0 <= h < 240.0 and sand(x, y)

    placed = 0
    if bivalves:
        placed += scatter(world, rng, bivalves, 340, wrack, (0.7, 1.3), "Shells_Wrack", cull=SHELL_CULL, sink=0.2, tries=200)
    if spirals:
        placed += scatter(world, rng, spirals, 90, wrack, (0.75, 1.35), "Shells_WrackSpiral", cull=SHELL_CULL, sink=0.2, tries=400)
    placed += scatter(world, rng, every, 170, upper, (0.6, 1.1), "Shells_Beach", cull=SHELL_CULL, sink=0.25, tries=120)

    # A few little drifts on the start beach, where the player wakes and looks down first.
    per_mesh = {m.get_name(): [] for m in every}
    clusters = 0
    for _ in range(200):
        if clusters >= 6:
            break
        ang, r = rng.uniform(0.0, 2.0 * math.pi), rng.uniform(250.0, 1400.0)
        cx, cy = start[0] + math.cos(ang) * r, start[1] + math.sin(ang) * r
        if not (5.0 < height(cx, cy) < 200.0 and sand(cx, cy)):
            continue
        clusters += 1
        for _ in range(rng.randint(6, 11)):
            x, y = cx + rng.gauss(0.0, 45.0), cy + rng.gauss(0.0, 45.0)
            if not 3.0 < height(x, y) < 240.0:
                continue
            m = every[rng.randrange(len(every))]
            per_mesh[m.get_name()].append(inst_transform(m, x, y, rng.uniform(0, 360), rng.uniform(0.7, 1.2), sink=0.2,
                                                         pitch=rng.uniform(-6, 6), roll=rng.uniform(-6, 6)))
    for m in every:
        if per_mesh[m.get_name()]:
            LIB.create_instanced_meshes(world, m, per_mesh[m.get_name()], "Shells_StartBeach_%s" % m.get_name(),
                                        "Biomes/Shells_StartBeach", False, SHELL_CULL)
            placed += len(per_mesh[m.get_name()])
    log("shells: %d placed (%d start-beach drifts), kinds %s" % (placed, clusters, sorted(shells)))


# ----------------------------------------------------------------------------------------------
# The walking mech (UNR-T68): Quaternius's CC0 "George" baked into rigid parts by tools/convert_mech.py,
# assembled and animated at runtime by ASoulsMech (Source/UnrealSoulslike/Souls/SoulsMech.*).
# ----------------------------------------------------------------------------------------------

MECH_ART = os.path.join(ART, "Mech")
MECH_DIR = ROOT + "/Models/Mech"           # ASoulsMech loads SM_Mech_<Part> from here (MechRig::MeshFolder)
MECH_MATS = ROOT + "/Materials/Mech"       # M_MechTracer / M_MechSmoke paths are in SoulsMech.cpp too
MECH_PARTS = ("Pelvis", "Torso", "Canopy", "ArmL", "ArmR", "ThighL", "ShinL", "MetaL", "FootL",
              "ThighR", "ShinR", "MetaR", "FootR")
MECH_HALF_HEIGHT = 370.0                   # ASoulsMech's capsule (MechRig::CapsuleHalfHeight)


def import_mech():
    """SourceArt/Environment/Mech/SM_Mech_<Part>.obj -> /Game/Island/Models/Mech/SM_Mech_<Part>, plus T_Mech."""
    out = {}
    for part in MECH_PARTS:
        target = "%s/SM_Mech_%s" % (MECH_DIR, part)
        src = os.path.join(MECH_ART, "SM_Mech_%s.obj" % part)
        if not os.path.exists(src):
            warn("mech part %s missing (run tools/convert_mech.py in Blender)" % src)
            continue
        if REIMPORT or not eal.does_asset_exist(target):
            tmp = "%s/Import/%s" % (MECH_DIR, part)
            if eal.does_directory_exist(tmp):
                eal.delete_directory(tmp)
            if eal.does_asset_exist(target):
                eal.delete_asset(target)
            import_files([src], tmp)
            meshes = assets_of(tmp, unreal.StaticMesh)
            if not meshes:
                warn("mech part %s: no static mesh imported" % part)
                continue
            if not eal.rename_asset(meshes[0].get_path_name().split(".")[0], target):
                warn("mech part %s: could not move the mesh to %s" % (part, target))
                continue
        mesh = eal.load_asset(target)
        if mesh is None:
            continue
        # Animated parts: no Nanite, no collision of their own (the mech's capsule collides).
        nanite_and_collision(mesh, nanite=False, complex_collision=False)
        out[part] = mesh
    tex_path = MECH_DIR + "/T_Mech"
    if REIMPORT or not eal.does_asset_exist(tex_path):
        tmp = MECH_DIR + "/Import/Tex"
        if eal.does_directory_exist(tmp):
            eal.delete_directory(tmp)
        if eal.does_asset_exist(tex_path):
            eal.delete_asset(tex_path)
        import_files([os.path.join(MECH_ART, "Mech_Texture.png")], tmp)
        texs = assets_of(tmp, unreal.Texture2D)
        if texs:
            eal.rename_asset(texs[0].get_path_name().split(".")[0], tex_path)
    tex = eal.load_asset(tex_path) if eal.does_asset_exist(tex_path) else None
    log("mech: %d/%d parts imported, texture %s" % (len(out), len(MECH_PARTS), "ok" if tex else "MISSING"))
    return out, tex


def build_mech_materials(tex):
    """M_Mech (the pack's painted texture, tinted, with a 'Scorch' parameter that chars it as the armour goes),
    M_MechTracer (unlit additive glow: shells, missile exhausts, fireballs) and M_MechSmoke (soft lit smoke puff,
    'Opacity' driven per puff from code)."""
    def sparam(g, name, value):
        return g.new(E.MaterialExpressionScalarParameter, parameter_name=name, default_value=value)

    def vparam(g, name, rgb):
        return g.new(E.MaterialExpressionVectorParameter, parameter_name=name, default_value=unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0))

    hull, g = new_material("M_Mech", MECH_MATS)
    uv = g.new(E.MaterialExpressionTextureCoordinate)
    base = (g.tex(tex, uv, "color"), "RGB") if tex else g.val((0.3, 0.4, 0.32))
    color = g.mul(base, (vparam(g, "Tint", (1.0, 1.0, 1.0)), "RGB"))
    noise_tex = eal.load_asset(ROOT + "/Textures/Generated/T_IslandNoise")
    patch = (g.tex(noise_tex, g.mul(uv, 3.0), "masks"), "B") if noise_tex else 0.5
    burnt = g.sat(g.mul(g.sub(g.add(patch, sparam(g, "Scorch", 0.0)), 1.0), 3.0))
    g.out(g.lerp(color, (0.025, 0.022, 0.02), burnt), E.MaterialProperty.MP_BASE_COLOR)
    g.out(g.lerp(sparam(g, "Roughness", 0.6), 0.92, burnt), E.MaterialProperty.MP_ROUGHNESS)
    g.out(sparam(g, "Metallic", 0.35), E.MaterialProperty.MP_METALLIC)
    finish_material(hull)

    tracer, g = new_material("M_MechTracer", MECH_MATS)
    setp(tracer, "shading_model", E.MaterialShadingModel.MSM_UNLIT)
    setp(tracer, "blend_mode", E.BlendMode.BLEND_ADDITIVE)
    soft = g.one_minus(g.new(E.MaterialExpressionFresnel, exponent=1.2))
    g.out(g.mul((vparam(g, "Color", (16.0, 7.5, 2.0)), "RGB"), soft), E.MaterialProperty.MP_EMISSIVE_COLOR)
    finish_material(tracer)

    smoke, g = new_material("M_MechSmoke", MECH_MATS)
    setp(smoke, "blend_mode", E.BlendMode.BLEND_TRANSLUCENT)
    soft = g.one_minus(g.new(E.MaterialExpressionFresnel, exponent=1.6))
    opacity = g.mul(sparam(g, "Opacity", 0.5), soft)
    if noise_tex:
        wisp = (g.tex(noise_tex, g.mul(g.new(E.MaterialExpressionTextureCoordinate), 2.0), "masks"), "R")
        opacity = g.mul(opacity, g.lerp(0.5, 1.0, wisp))
    g.out((vparam(g, "Tint", (0.16, 0.15, 0.14)), "RGB"), E.MaterialProperty.MP_BASE_COLOR)
    g.out(1.0, E.MaterialProperty.MP_ROUGHNESS)
    g.out(g.sat(opacity), E.MaterialProperty.MP_OPACITY)
    finish_material(smoke)
    return hull


def build_mech(world, route, pads, start):
    """One parked mech on open, flat ground a little off the route through the dunes, facing the path."""
    parts, tex = import_mech()
    if parts:
        hull = build_mech_materials(tex)
        for mesh in parts.values():
            mesh.set_material(0, hull)
            eal.save_loaded_asset(mesh)
    cls = getattr(unreal, "SoulsMech", None)
    if cls is None:
        warn("mech: unreal.SoulsMech not found (compile the C++ first); no mech placed")
        return
    grace = route.offset(0.035, 450.0)
    sx, sy = start

    def flat_and_clear(x, y):
        h = height(x, y)
        if h < 120.0 or LIB.get_path_distance(x, y) < 650.0 or normal_z(x, y) < 0.965:
            return False
        if math.hypot(x - sx, y - sy) < 2500.0 or math.hypot(x - grace[0], y - grace[1]) < 1500.0:
            return False
        if any(math.hypot(x - px, y - py) < pr + 900.0 for px, py, pr in pads):
            return False
        for i in range(8):  # the mech's footprint (~5 m across) is level and free of props
            a = i * math.pi / 4.0
            rx, ry = x + math.cos(a) * 520.0, y + math.sin(a) * 520.0
            if abs(height(rx, ry) - h) > 70.0 or normal_z(rx, ry) < 0.94:
                return False
            hit = trace_down(world, rx, ry, h + 3000.0, h - 500.0)
            if hit is not None and hit[0].z > h + 60.0:
                return False
        hit = trace_down(world, x, y, h + 3000.0, h - 500.0)
        return hit is None or hit[0].z < h + 60.0

    spot = None
    for t in (0.12, 0.10, 0.14, 0.08, 0.16, 0.18, 0.06):
        for side in (1100.0, -1100.0, 1500.0, -1500.0, 900.0, -900.0, 1900.0, -1900.0):
            x, y = route.offset(t, side)
            if flat_and_clear(x, y):
                spot = (x, y, t)
                break
        if spot:
            break
    if spot is None:
        x, y = route.offset(0.12, 1100.0)
        spot = (x, y, 0.12)
        warn("mech: no clear flat spot found; parked at route 0.12 anyway")
    x, y, t = spot
    (px, py), _ = route.at(t)
    yaw = math.degrees(math.atan2(py - y, px - x))  # facing the path, so it greets the player coming up it
    mech = spawn(cls, (x, y, height(x, y) + MECH_HALF_HEIGHT + 15.0), unreal.Rotator(yaw=yaw), "Mech_Parked", "Gameplay/Vehicles")
    log("mech parked at (%.0f, %.0f, %.0f) yaw %.0f, route t=%.2f, %.0f cm off the path" % (
        x, y, height(x, y), yaw, t, LIB.get_path_distance(x, y)))
    # A shot for -IslandShots: from the path, looking at it.
    path = os.path.join(PROJECT, "Saved", "island_shots.txt")
    if os.path.exists(path):
        cx, cy = px + (px - x) * 0.25, py + (py - y) * 0.25
        with open(path, "a", encoding="utf-8") as f:
            f.write(shot("mech_parked", cx, cy, height(cx, cy) + 260.0, x, y, height(x, y) + 380.0, fov=70) + "\n")
    return mech


# ----------------------------------------------------------------------------------------------
# The star fighter (UNR-T69): Quaternius's CC0 "Striker" split into hull / canopy / landing legs, and "Bob" for the
# AI drones, by tools/convert_fighter.py; flown at runtime by ASoulsFighter (Source/UnrealSoulslike/Souls/SoulsFighter.*),
# which also spawns the drones.
# ----------------------------------------------------------------------------------------------

FIGHTER_ART = os.path.join(ART, "Fighter")
FIGHTER_DIR = ROOT + "/Models/Fighter"       # ASoulsFighter loads SM_Fighter_<Part> from here (FighterRig::MeshFolder)
FIGHTER_MATS = ROOT + "/Materials/Fighter"   # M_FighterGlow / M_FighterSmoke paths are in SoulsFighter.cpp too
FIGHTER_PARTS = ("Hull", "Canopy", "GearN", "GearL", "GearR", "Drone")
FIGHTER_REST_Z = 245.4                       # origin above the ground on its legs (-FighterRig::GearFloor)


def import_fighter():
    """SourceArt/Environment/Fighter/SM_Fighter_<Part>.obj -> /Game/Island/Models/Fighter/SM_Fighter_<Part>, plus textures."""
    out = {}
    for part in FIGHTER_PARTS:
        target = "%s/SM_Fighter_%s" % (FIGHTER_DIR, part)
        src = os.path.join(FIGHTER_ART, "SM_Fighter_%s.obj" % part)
        if not os.path.exists(src):
            warn("fighter part %s missing (run tools/convert_fighter.py in Blender)" % src)
            continue
        if REIMPORT or not eal.does_asset_exist(target):
            tmp = "%s/Import/%s" % (FIGHTER_DIR, part)
            if eal.does_directory_exist(tmp):
                eal.delete_directory(tmp)
            if eal.does_asset_exist(target):
                eal.delete_asset(target)
            import_files([src], tmp)
            meshes = assets_of(tmp, unreal.StaticMesh)
            if not meshes:
                warn("fighter part %s: no static mesh imported" % part)
                continue
            if not eal.rename_asset(meshes[0].get_path_name().split(".")[0], target):
                warn("fighter part %s: could not move the mesh to %s" % (part, target))
                continue
        mesh = eal.load_asset(target)
        if mesh is None:
            continue
        # Moving parts: no Nanite, no collision of their own (the ship's sphere collides).
        nanite_and_collision(mesh, nanite=False, complex_collision=False)
        out[part] = mesh
    texs = {}
    for name, png in (("T_Fighter", "Fighter_Texture.png"), ("T_FighterDrone", "Drone_Texture.png")):
        tex_path = FIGHTER_DIR + "/" + name
        if REIMPORT or not eal.does_asset_exist(tex_path):
            tmp = FIGHTER_DIR + "/Import/" + name
            if eal.does_directory_exist(tmp):
                eal.delete_directory(tmp)
            if eal.does_asset_exist(tex_path):
                eal.delete_asset(tex_path)
            import_files([os.path.join(FIGHTER_ART, png)], tmp)
            found = assets_of(tmp, unreal.Texture2D)
            if found:
                eal.rename_asset(found[0].get_path_name().split(".")[0], tex_path)
        texs[name] = eal.load_asset(tex_path) if eal.does_asset_exist(tex_path) else None
    log("fighter: %d/%d parts imported, textures %s" % (len(out), len(FIGHTER_PARTS), {k: v is not None for k, v in texs.items()}))
    return out, texs


def build_fighter_materials(texs):
    """M_Fighter / M_FighterDrone (the pack's painted textures, tinted, with a 'Scorch' parameter that chars them as the
    hull goes), M_FighterGlow (unlit additive 'Color': laser bolts, engines, muzzle flashes, fireballs) and M_FighterSmoke
    (soft lit puff, 'Opacity' driven per puff from code)."""
    def sparam(g, name, value):
        return g.new(E.MaterialExpressionScalarParameter, parameter_name=name, default_value=value)

    def vparam(g, name, rgb):
        return g.new(E.MaterialExpressionVectorParameter, parameter_name=name, default_value=unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0))

    noise_tex = eal.load_asset(ROOT + "/Textures/Generated/T_IslandNoise")
    hulls = {}
    for name, tex, tint, metal in (("M_Fighter", texs.get("T_Fighter"), (1.0, 1.0, 1.0), 0.45),
                                   ("M_FighterDrone", texs.get("T_FighterDrone"), (1.0, 0.55, 0.5), 0.4)):
        mat, g = new_material(name, FIGHTER_MATS)
        uv = g.new(E.MaterialExpressionTextureCoordinate)
        base = (g.tex(tex, uv, "color"), "RGB") if tex else g.val((0.3, 0.32, 0.36))
        color = g.mul(base, (vparam(g, "Tint", tint), "RGB"))
        patch = (g.tex(noise_tex, g.mul(uv, 3.0), "masks"), "B") if noise_tex else 0.5
        burnt = g.sat(g.mul(g.sub(g.add(patch, sparam(g, "Scorch", 0.0)), 1.0), 3.0))
        g.out(g.lerp(color, (0.025, 0.022, 0.02), burnt), E.MaterialProperty.MP_BASE_COLOR)
        g.out(g.lerp(sparam(g, "Roughness", 0.45), 0.92, burnt), E.MaterialProperty.MP_ROUGHNESS)
        g.out(sparam(g, "Metallic", metal), E.MaterialProperty.MP_METALLIC)
        finish_material(mat)
        hulls[name] = mat

    glow, g = new_material("M_FighterGlow", FIGHTER_MATS)
    setp(glow, "shading_model", E.MaterialShadingModel.MSM_UNLIT)
    setp(glow, "blend_mode", E.BlendMode.BLEND_ADDITIVE)
    soft = g.one_minus(g.new(E.MaterialExpressionFresnel, exponent=1.2))
    g.out(g.mul((vparam(g, "Color", (16.0, 7.5, 2.0)), "RGB"), soft), E.MaterialProperty.MP_EMISSIVE_COLOR)
    finish_material(glow)

    smoke, g = new_material("M_FighterSmoke", FIGHTER_MATS)
    setp(smoke, "blend_mode", E.BlendMode.BLEND_TRANSLUCENT)
    soft = g.one_minus(g.new(E.MaterialExpressionFresnel, exponent=1.6))
    opacity = g.mul(sparam(g, "Opacity", 0.5), soft)
    if noise_tex:
        wisp = (g.tex(noise_tex, g.mul(g.new(E.MaterialExpressionTextureCoordinate), 2.0), "masks"), "R")
        opacity = g.mul(opacity, g.lerp(0.5, 1.0, wisp))
    g.out((vparam(g, "Tint", (0.17, 0.16, 0.15)), "RGB"), E.MaterialProperty.MP_BASE_COLOR)
    g.out(1.0, E.MaterialProperty.MP_ROUGHNESS)
    g.out(g.sat(opacity), E.MaterialProperty.MP_OPACITY)
    finish_material(smoke)
    return hulls


def build_fighter(world, route, pads, start):
    """One star fighter parked on open, flat ground near the start beach, off the path, its left side (where the pilot
    climbs in) turned to the start. Its AI drones are spawned by the fighter itself at runtime."""
    parts, texs = import_fighter()
    if parts:
        hulls = build_fighter_materials(texs)
        for part, mesh in parts.items():
            mesh.set_material(0, hulls["M_FighterDrone" if part == "Drone" else "M_Fighter"])
            eal.save_loaded_asset(mesh)
    cls = getattr(unreal, "SoulsFighter", None)
    if cls is None:
        warn("fighter: unreal.SoulsFighter not found (compile the C++ first); no fighter placed")
        return None
    sx, sy = start
    grace = route.offset(0.035, 450.0)
    others = [a.get_actor_location() for a in actors_sub.get_all_level_actors()
              if getattr(unreal, "SoulsMech", None) is not None and isinstance(a, unreal.SoulsMech)]

    def flat_and_clear(x, y):
        h = height(x, y)
        if h < 130.0 or LIB.get_path_distance(x, y) < 600.0 or normal_z(x, y) < 0.97:
            return False
        if math.hypot(x - grace[0], y - grace[1]) < 1300.0:
            return False
        if any(math.hypot(x - o.x, y - o.y) < 2600.0 for o in others):
            return False
        if any(math.hypot(x - px, y - py) < pr + 900.0 for px, py, pr in pads):
            return False
        for i in range(12):  # wings ~7 m across, ~12 m nose to tail: level and free of props out to 8 m
            a = i * math.pi / 6.0
            for r in (450.0, 800.0):
                rx, ry = x + math.cos(a) * r, y + math.sin(a) * r
                if abs(height(rx, ry) - h) > 80.0 or normal_z(rx, ry) < 0.94:
                    return False
                hit = trace_down(world, rx, ry, h + 3000.0, h - 500.0)
                if hit is not None and hit[0].z > h + 60.0:
                    return False
        hit = trace_down(world, x, y, h + 3000.0, h - 500.0)
        return hit is None or hit[0].z < h + 60.0

    spot = None
    for r in (1600.0, 2100.0, 2600.0, 3100.0, 3700.0, 4400.0, 5200.0):
        for k in range(24):
            a = k * math.pi / 12.0
            x, y = sx + math.cos(a) * r, sy + math.sin(a) * r
            if flat_and_clear(x, y):
                spot = (x, y)
                break
        if spot:
            break
    if spot is None:
        for t in (0.04, 0.06, 0.08, 0.10, 0.03):
            for side in (1300.0, -1300.0, 1800.0, -1800.0):
                x, y = route.offset(t, side)
                if flat_and_clear(x, y):
                    spot = (x, y)
                    break
            if spot:
                break
    if spot is None:
        spot = route.offset(0.05, -1300.0)
        warn("fighter: no clear flat spot found; parked at route 0.05 anyway")
    x, y = spot
    to_start = math.degrees(math.atan2(sy - y, sx - x))
    yaw = to_start + 90.0  # left side to the start
    fighter = spawn(cls, (x, y, height(x, y) + FIGHTER_REST_Z + 20.0), unreal.Rotator(yaw=yaw), "Fighter_Parked", "Gameplay/Vehicles")
    log("fighter parked at (%.0f, %.0f, %.0f) yaw %.0f, %.0f cm from the start, %.0f cm off the path" % (
        x, y, height(x, y), yaw, math.hypot(x - sx, y - sy), LIB.get_path_distance(x, y)))
    # A shot for -IslandShots: from the start side, looking at it.
    path = os.path.join(PROJECT, "Saved", "island_shots.txt")
    if os.path.exists(path):
        d = math.hypot(sx - x, sy - y) or 1.0
        cx, cy = x + (sx - x) / d * 1500.0, y + (sy - y) / d * 1500.0
        with open(path, "a", encoding="utf-8") as f:
            f.write(shot("fighter_parked", cx, cy, height(cx, cy) + 260.0, x, y, height(x, y) + 250.0, fov=70) + "\n")
    return fighter


# ----------------------------------------------------------------------------------------------
# The wall-run course (UNR-T77): long, tall stone walls on open flat ground near the start, to practise
# USoulsWallRunComponent (Source/UnrealSoulslike/Souls/SoulsWallRun.*) on. Two parallel pairs to chain
# wall jumps between, a lone long wall, and a zig-zag line, all reached from one start slab. A TargetPoint
# tagged "WallRunStart" (on the axis of pair A, facing down it, WALLRUN_LEAD_IN short of it) is where
# -WallRunDemo starts.
# ----------------------------------------------------------------------------------------------

WALLRUN_THICK = 80.0       # cm, every wall
WALLRUN_LEAD_IN = 900.0    # cm of run-up from the start slab to the walls
WALLRUN_TILE = 300.0       # cm per repeat of the stone texture (world-aligned, so the scaled boxes don't stretch it)
# (label, (u0, v0), (u1, v1), height above the highest ground under it): u runs down the course from the start
# line, v to its right. The inner gap of pair A is 4.5 m, of pair B 5 m.
WALLRUN_ZIG = (math.cos(math.radians(35.0)) * 2000.0, math.sin(math.radians(35.0)) * 2000.0)
WALLRUN_WALLS = [
    ("WallRun_PairA_L", (0.0, -265.0), (3000.0, -265.0), 550.0),
    ("WallRun_PairA_R", (0.0, 265.0), (3000.0, 265.0), 550.0),
    ("WallRun_PairB_L", (0.0, 1210.0), (4000.0, 1210.0), 500.0),
    ("WallRun_PairB_R", (0.0, 1790.0), (4000.0, 1790.0), 500.0),
    ("WallRun_Long", (0.0, -1500.0), (3500.0, -1500.0), 600.0),
    ("WallRun_Zig_1", (0.0, -2900.0), (WALLRUN_ZIG[0], -2900.0 - WALLRUN_ZIG[1]), 450.0),
    ("WallRun_Zig_2", (WALLRUN_ZIG[0], -2900.0 - WALLRUN_ZIG[1]), (2 * WALLRUN_ZIG[0], -2900.0), 450.0),
    ("WallRun_Zig_3", (2 * WALLRUN_ZIG[0], -2900.0), (3 * WALLRUN_ZIG[0], -2900.0 - WALLRUN_ZIG[1]), 450.0),
]
WALLRUN_AREA = (-WALLRUN_LEAD_IN - 400.0, max(4300.0, 3 * WALLRUN_ZIG[0] + 300.0),
                -2900.0 - WALLRUN_ZIG[1] - 400.0, 2200.0)  # u min, u max, v min, v max: the whole course, run-ups included


def build_wallrun_material(textures):
    """Stone for the scaled boxes: the fort's masonry (else the seaside rock), projected in world space from the
    side each face looks along, so a 40 m box shows the same stone size as a 1 m one."""
    diffs = [t for t in assets_of(ROOT + "/Models/modular_fort_01", unreal.Texture2D) if t.get_name().endswith("_diff")]
    base_tex = (sorted(diffs, key=lambda t: ("wall" not in t.get_name().lower(), t.get_name()))[0] if diffs
                else textures["seaside_rock"]["color"])
    mat, g = new_material("M_WallRunStone", ROOT + "/Materials")
    wp = g.world()
    nrm = g.new(E.MaterialExpressionVertexNormalWS)
    wx, wy, wz = g.mask(wp, r=True), g.mask(wp, g=True), g.mask(wp, b=True)
    down = g.mul(wz, -1.0)
    tile = (WALLRUN_TILE, WALLRUN_TILE)
    c_x = (g.tex(base_tex, g.div(g.append(wy, down), tile), "color"), "RGB")   # faces looking along +-X
    c_y = (g.tex(base_tex, g.div(g.append(wx, down), tile), "color"), "RGB")   # faces looking along +-Y
    c_z = (g.tex(base_tex, g.div(g.append(wx, wy), tile), "color"), "RGB")     # the tops
    ax, ay, az = g.abs(g.mask(nrm, r=True)), g.abs(g.mask(nrm, g=True)), g.abs(g.mask(nrm, b=True))
    side = g.lerp(c_y, c_x, g.smooth(-0.15, 0.15, g.sub(ax, ay)))
    color = g.lerp(side, c_z, g.smooth(0.55, 0.8, az))
    tint = g.new(E.MaterialExpressionVectorParameter, parameter_name="Tint", default_value=unreal.LinearColor(0.72, 0.72, 0.7, 1.0))
    g.out(g.mul(color, (tint, "RGB")), E.MaterialProperty.MP_BASE_COLOR)
    g.out(0.85, E.MaterialProperty.MP_ROUGHNESS)
    finish_material(mat)
    log("wall-run stone: %s" % base_tex.get_name())
    return mat


def build_wallrun_course(world, textures, route, pads, start):
    """Lay the course out on the flattest open ground near the start (off the path, clear of markers, vehicles,
    pads and biomes), then build it from engine cubes in M_WallRunStone with collision on."""
    cube = unreal.load_asset("/Engine/BasicShapes/Cube")
    if cube is None:
        warn("wall-run course: /Engine/BasicShapes/Cube missing; no course built")
        return None
    sx, sy = start
    u_lo, u_hi, v_lo, v_hi = WALLRUN_AREA
    keep_names = ("PlayerStart", "TargetPoint", "SoulsMech", "SoulsFighter", "GrapplePoint", "SiteOfGrace", "SoulsPickup",
                  "SoulsTorch")
    keep = []
    for a in actors_sub.get_all_level_actors():
        cname = a.get_class().get_name()
        if any(cname.startswith(k) or cname.startswith("BP_" + k) for k in keep_names):
            p = a.get_actor_location()
            keep.append((p.x, p.y))
    avoid = []  # (x, y, r): biomes the course must not sit in
    for key, r in (("marsh", MARSH_RADIUS), ("clearing", CLEARING_RADIUS), ("wood", 5800.0)):
        if key in BIOMES:
            avoid.append((BIOMES[key][0], BIOMES[key][1], r + 800.0))
    avoid += [(px, py, pr + 600.0) for px, py, pr in pads]

    def frame(cx, cy, yaw):
        f = (math.cos(math.radians(yaw)), math.sin(math.radians(yaw)))
        r = (-f[1], f[0])
        return lambda u, v: (cx + f[0] * u + r[0] * v, cy + f[1] * u + r[1] * v)

    def wall_samples(at):
        for _, (u0, v0), (u1, v1), _ in WALLRUN_WALLS:
            n = max(2, int(math.hypot(u1 - u0, v1 - v0) / 250.0))
            for i in range(n + 1):
                t = i / float(n)
                yield at(u0 + (u1 - u0) * t, v0 + (v1 - v0) * t)

    def grid(at, step=400.0):
        u = u_lo
        while u <= u_hi:
            v = v_lo
            while v <= v_hi:
                yield at(u, v)
                v += step
            u += step

    def terrain(cx, cy, yaw):
        """Height range over the course, or None when the ground won't do."""
        at = frame(cx, cy, yaw)
        lo, hi = 1e9, -1e9
        for x, y in grid(at):
            h = height(x, y)
            if h < 150.0 or normal_z(x, y) < 0.9 or not clear_of(x, y, avoid):
                return None
            if any((x - kx) ** 2 + (y - ky) ** 2 < 800.0 ** 2 for kx, ky in keep):
                return None
            lo, hi = min(lo, h), max(hi, h)
            if hi - lo > 450.0:
                return None
        for x, y in wall_samples(at):
            if LIB.get_path_distance(x, y) < 400.0:
                return None
        return hi - lo

    def obstacles(cx, cy, yaw):
        """Props standing where a wall or a run-up goes (things a trace down meets well above the ground)."""
        at = frame(cx, cy, yaw)
        count = 0
        for x, y in list(wall_samples(at)) + list(grid(at, 500.0)):
            h = height(x, y)
            hit = trace_down(world, x, y, h + 3000.0, h - 300.0)
            if hit is not None and hit[0].z > h + 60.0:
                count += 1
        return count

    best = None  # (obstacles, range, cx, cy, yaw)
    tried = 0
    max_tried = 60  # layouts whose props are counted (traces are slow); then the best so far is taken
    for radius in (2500.0, 3200.0, 4000.0, 5000.0, 6000.0, 7200.0, 8500.0, 10000.0):
        for k in range(20):
            ang = k * math.pi / 10.0
            cx, cy = sx + math.cos(ang) * radius, sy + math.sin(ang) * radius
            for yaw in range(0, 180, 30):
                yaw = float(yaw)
                rng_h = terrain(cx, cy, yaw)
                if rng_h is None:
                    continue
                if tried >= max_tried:
                    break
                tried += 1
                obs = obstacles(cx, cy, yaw)
                cand = (obs, rng_h, cx, cy, yaw)
                if best is None or cand[:2] < best[:2]:
                    best = cand
                if obs == 0 and rng_h < 250.0:
                    break
            if tried >= max_tried or (best is not None and best[0] == 0 and best[1] < 250.0):
                break
        if tried >= max_tried or best is not None and best[0] == 0 and best[1] < 250.0:
            break
    if best is None:
        (bx, by), (dx, dy) = route.at(0.08)
        best = (-1, -1.0, bx - dy * 3000.0, by + dx * 3000.0, math.degrees(math.atan2(dy, dx)))
        warn("wall-run course: no open flat ground found; built 30 m off the path at route 0.08 anyway")
    obs, rng_h, cx, cy, yaw = best
    at = frame(cx, cy, yaw)
    stone = build_wallrun_material(textures)

    def box(label, p0, p1, thick, top_above, z_floor=None, folder="WallRun"):
        """A cube stretched from p0 to p1 (ground points), thick wide, from below the lowest ground under it
        to top_above over the highest; extended by half its thickness at each end so corners close."""
        (x0, y0), (x1, y1) = p0, p1
        length = math.hypot(x1 - x0, y1 - y0)
        n = max(2, int(length / 100.0))
        hs = [height(x0 + (x1 - x0) * i / n, y0 + (y1 - y0) * i / n) for i in range(n + 1)]
        bottom = (min(hs) if z_floor is None else z_floor) - 120.0
        top = max(hs) + top_above
        a = spawn_mesh(cube, unreal.Vector((x0 + x1) * 0.5, (y0 + y1) * 0.5, (bottom + top) * 0.5),
                       unreal.Rotator(yaw=math.degrees(math.atan2(y1 - y0, x1 - x0))))
        a.set_actor_scale3d(unreal.Vector((length + thick) / 100.0, thick / 100.0, (top - bottom) / 100.0))
        a.set_actor_label(label)
        a.set_folder_path(folder)
        a.set_editor_property("tags", [unreal.Name("WallRun")])
        smc = a.get_editor_property("static_mesh_component")
        smc.set_material(0, stone)
        smc.set_collision_profile_name("BlockAll")
        smc.set_collision_enabled(unreal.CollisionEnabled.QUERY_AND_PHYSICS)
        return a

    walls = []
    for label, (u0, v0), (u1, v1), h_above in WALLRUN_WALLS:
        walls.append(box(label, at(u0, v0), at(u1, v1), WALLRUN_THICK, h_above))
    # The start: a low slab across the run-up line and a waymark post beside it, and the demo's marker.
    su = -WALLRUN_LEAD_IN
    slab_h = min(height(*at(su, v)) for v in (-500.0, 0.0, 500.0))
    box("WallRun_StartSlab", at(su, -500.0), at(su, 500.0), 160.0, 6.0, z_floor=slab_h - 30.0)
    box("WallRun_StartPost", at(su, -1000.0), at(su, -980.0), 30.0, 170.0)
    mx, my = at(su + 120.0, 0.0)
    target_point(mx, my, "WallRunStart", "WallRunStart", yaw=yaw)

    # Nothing from the dressing pass may stand inside a wall or on the run-ups.
    removed = []
    for a in actors_sub.get_all_level_actors():
        if not isinstance(a, unreal.StaticMeshActor) or not str(a.get_folder_path()).startswith("Dressing"):
            continue
        p = a.get_actor_location()
        f = (math.cos(math.radians(yaw)), math.sin(math.radians(yaw)))
        du, dv = (p.x - cx) * f[0] + (p.y - cy) * f[1], -(p.x - cx) * f[1] + (p.y - cy) * f[0]
        _, extent = a.get_actor_bounds(False)
        grow = max(extent.x, extent.y)
        if u_lo - grow < du < u_hi + grow and v_lo - grow < dv < v_hi + grow:
            removed.append(a.get_actor_label())
            actors_sub.destroy_actor(a)
    log("wall-run course at (%.0f, %.0f, %.0f) yaw %.0f, %.0f cm from the start: %d walls, ground range %.0f cm, %d prop hits "
        "in the footprint (%d layouts tried), removed %d dressing props %s" % (
            cx, cy, height(cx, cy), yaw, math.hypot(cx - sx, cy - sy), len(walls), rng_h, obs, tried, len(removed), removed[:12]))
    # A shot for -IslandShots: from behind and above the start, down the course.
    path = os.path.join(PROJECT, "Saved", "island_shots.txt")
    if os.path.exists(path):
        ex, ey = at(su - 1400.0, -1300.0)
        tx, ty = at(1500.0, -400.0)
        with open(path, "a", encoding="utf-8") as fh:
            fh.write(shot("wallrun_course", ex, ey, height(ex, ey) + 650.0, tx, ty, height(tx, ty) + 200.0, fov=75) + "\n")
    return walls


def build_everything(textures, models):
    route, pads = plan_layout()
    start = route.points[0]
    mask_tex = LIB.create_island_mask_texture(ROOT + "/Textures/Generated/T_IslandMask", 1024)
    eal.save_loaded_asset(mask_tex)
    GRASS_NOISE.append(textures["generated"]["T_IslandNoise"])
    biome_tex = LIB.create_island_biome_texture(ROOT + "/Textures/Generated/T_IslandBiome", 1024)
    eal.save_loaded_asset(biome_tex)
    build_fire_assets()
    grass_types = build_grass_types(models)
    terrain_mat = build_terrain_material(textures, mask_tex, biome_tex, grass_types)
    apply_prop_materials(models, build_prop_master(textures))
    apply_foliage_materials(models, build_foliage_master(textures))
    ocean_mat = build_ocean_material(textures, mask_tex)

    les, world = new_map()
    landscape = LIB.create_island_landscape(world, terrain_mat)
    if landscape is None:
        raise RuntimeError("landscape creation failed")
    landscape.set_folder_path("Terrain")
    log("landscape: extent %.0f cm, height at start %.0f, at hill %.0f" % (LIB.get_landscape_extent(), height(*start), height(*HILL)))

    build_sky()
    build_weather()  # UNR-T79: rain fronts, wind, wet surfaces, blown leaves
    build_ocean(ocean_mat)
    build_sea_walls(start)
    build_water(world, textures, mask_tex)
    dress(models, route, pads, start)
    build_ruins(models, route, pads)
    build_stone_circle(models, route)
    dress_biomes(world, models, route, pads)
    dress_more(world, models, route, pads, start)
    build_shells(world, models, start)
    broch = build_islet(models)
    build_markers(route, start)
    grapples = build_grapples_and_loot(world, route, pads, broch)
    build_wall_torches(world, pads)
    write_shot_list(route, start, grapples)
    build_tall_grass(world, models, route, pads)
    build_wildlife(models, route, start)
    build_ship_graveyard(models, route, start)
    build_mech(world, route, pads, start)
    build_fighter(world, route, pads, start)
    build_wallrun_course(world, textures, route, pads, start)

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
