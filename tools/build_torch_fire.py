"""Editor-Python (UE 5.8), UNR-T51: the torches' fire.

Imports the CC0 Kenney flame and smoke sprites (SourceArt/FX/KenneyParticlePack, see SourceArt/FX/CREDITS.md),
builds their particle materials and four Niagara systems under /Game/Souls/FX/Fire, which
Source/UnrealSoulslike/Souls/SoulsTorch.cpp hard-references (so a packaged build cooks them):

  NS_TorchFlame        the tall tongue of a torch in a hand, in a bracket or in flight
  NS_TorchFlameGround  the wider little fire of a torch lying on the ground (and of a knight set alight)
  NS_TorchEmbers       sparks lifting off either
  NS_TorchSmoke        a thin dark plume over a torch at rest

Each system is a copy of the engine's FountainLightweight template: one lightweight (stateless) emitter,
simulated in the component's space (the torch keeps that space upright, so the flames always rise).
Stateless modules aren't scriptable from Python, so they're tuned through unreal.SoulsFxLibrary
(Source/UnrealSoulslike/Souls/SoulsFxLibrary.*): build the C++ module first.

Run (after the C++ is built; any time again, it's idempotent):
  UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<this file> -unattended -nop4 -nosplash
Writes Saved/torch_fire.log (the commandlet swallows print()), ending in TORCH_FIRE_OK.
"""
import os
import shutil
import unreal

PROJECT = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
LOG = open(os.path.join(PROJECT, "Saved", "torch_fire.log"), "w", encoding="utf-8")
eal = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
MEL = unreal.MaterialEditingLibrary
problems = []

FIRE = "/Game/Souls/FX/Fire"
SRC = os.path.join(PROJECT, "SourceArt", "FX", "KenneyParticlePack")
STAGE = os.path.join(PROJECT, "Saved", "torch_fire")
TEMPLATE = "/Niagara/DefaultAssets/Templates/Systems/FountainLightweight"
# Modules the emitter can't do without; every other one is switched off, then on again where used.
KEEP_ON = {"InitializeParticle", "SolveVelocitiesAndForces"}


def log(msg):
    LOG.write(msg + "\n")
    LOG.flush()


def warn(msg):
    problems.append(msg)
    log("WARNING " + msg)


def ensure_dir(path):
    if not eal.does_directory_exist(path):
        eal.make_directory(path)


# ----------------------------------------------------------------------------------------------
# Textures
# ----------------------------------------------------------------------------------------------

def import_texture(src_name, asset_name):
    """Imports SourceArt/FX/KenneyParticlePack/<src_name> as FIRE/<asset_name>: a linear grey mask."""
    os.makedirs(STAGE, exist_ok=True)
    staged = os.path.join(STAGE, asset_name + ".png")
    shutil.copyfile(os.path.join(SRC, src_name), staged)
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", staged)
    task.set_editor_property("destination_path", FIRE)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("automated", True)
    task.set_editor_property("save", True)
    tools.import_asset_tasks([task])
    tex = eal.load_asset("%s/%s" % (FIRE, asset_name))
    if tex is None:
        raise RuntimeError("texture import failed: " + src_name)
    tex.set_editor_property("srgb", False)
    tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
    try:
        tex.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_EFFECTS)
    except Exception as exc:
        warn("lod group on %s: %s" % (asset_name, exc))
    eal.save_loaded_asset(tex)
    log("texture %s <- %s" % (asset_name, src_name))
    return tex


# ----------------------------------------------------------------------------------------------
# Materials (unlit sprite materials for Niagara)
# ----------------------------------------------------------------------------------------------

def new_material(name, blend):
    path = "%s/%s" % (FIRE, name)
    mat = eal.load_asset(path) if eal.does_asset_exist(path) else \
        tools.create_asset(name, FIRE, unreal.Material, unreal.MaterialFactoryNew())
    MEL.delete_all_material_expressions(mat)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property("blend_mode", blend)
    mat.set_editor_property("used_with_niagara_sprites", True)
    return mat


class Graph:
    def __init__(self, mat):
        self.mat = mat
        self.y = 0

    def new(self, cls, **props):
        e = MEL.create_material_expression(self.mat, cls, -900 + 160 * (self.y % 5), 160 * (self.y // 5))
        self.y += 1
        for key, value in props.items():
            e.set_editor_property(key, value)
        return e

    def link(self, src, out, dst, inp):
        if not MEL.connect_material_expressions(src, out, dst, inp):
            warn("%s: could not link %s.%s -> %s.%s" % (self.mat.get_name(), src.get_name(), out, dst.get_name(), inp))

    def const(self, value):
        return self.new(unreal.MaterialExpressionConstant, r=value)

    def binop(self, cls, a, b, a_out="", b_out=""):
        """a and b are expressions, (expression, output) pairs or plain numbers."""
        e = self.new(cls)
        for pin, operand, out in (("A", a, a_out), ("B", b, b_out)):
            if isinstance(operand, (int, float)):
                operand = self.const(float(operand))
            if isinstance(operand, tuple):
                operand, out = operand
            self.link(operand, out, e, pin)
        return e

    def mul(self, a, b, a_out="", b_out=""):
        return self.binop(unreal.MaterialExpressionMultiply, a, b, a_out, b_out)

    def add(self, a, b, a_out="", b_out=""):
        return self.binop(unreal.MaterialExpressionAdd, a, b, a_out, b_out)

    def depth_fade(self, distance):
        """Soft-particle fade where the sprite meets the ground or a wall (a scalar 0..1)."""
        e = self.new(unreal.MaterialExpressionDepthFade)
        for key, value in (("opacity_default", 1.0), ("fade_distance_default", distance)):
            try:
                e.set_editor_property(key, value)
            except Exception as exc:
                warn("depth fade %s: %s" % (key, exc))
        return e

    def out(self, src, prop, out_name=""):
        if not MEL.connect_material_property(src, out_name, prop):
            warn("%s: could not connect %s" % (self.mat.get_name(), prop))


def finish_material(mat):
    MEL.recompile_material(mat)
    eal.save_loaded_asset(mat)
    log("material %s" % mat.get_name())
    return mat


def build_flame_material(atlas):
    """Additive: the 2x2 flame sheet (blended frame to frame) in the particle colour, white-hot where dense."""
    mat = new_material("M_TorchFlame", unreal.BlendMode.BLEND_ADDITIVE)
    g = Graph(mat)
    sub = g.new(unreal.MaterialExpressionParticleSubUV, texture=atlas,
                sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_MASKS)
    try:
        sub.set_editor_property("blend", True)
    except Exception as exc:
        warn("sub-UV blend: %s" % exc)
    pc = g.new(unreal.MaterialExpressionParticleColor)
    dense = g.mul((sub, "R"), (sub, "R"))
    hot = g.add(1.0, g.mul(dense, 1.8))
    lit = g.mul(g.mul((pc, ""), hot), g.mul((sub, "R"), (pc, "A")))
    g.out(g.mul(lit, g.depth_fade(6.0)), unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    return finish_material(mat)


def build_ember_material():
    """Additive: a soft round dot (no texture) in the particle colour."""
    mat = new_material("M_TorchEmber", unreal.BlendMode.BLEND_ADDITIVE)
    g = Graph(mat)
    uv = g.new(unreal.MaterialExpressionTextureCoordinate)
    centre = g.new(unreal.MaterialExpressionConstant2Vector, r=0.5, g=0.5)
    d = g.binop(unreal.MaterialExpressionSubtract, uv, centre)
    d2 = g.binop(unreal.MaterialExpressionDotProduct, d, d)
    edge = g.new(unreal.MaterialExpressionOneMinus)
    g.link(g.mul(d2, 4.0), "", edge, "")
    sat = g.new(unreal.MaterialExpressionSaturate)
    g.link(edge, "", sat, "")
    dot = g.mul(sat, sat)
    pc = g.new(unreal.MaterialExpressionParticleColor)
    g.out(g.mul(g.mul((pc, ""), (pc, "A")), dot), unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    return finish_material(mat)


def build_smoke_material(smoke):
    """Translucent: the smoke puff's grey as opacity, its (dark) particle colour as the colour."""
    mat = new_material("M_TorchSmoke", unreal.BlendMode.BLEND_TRANSLUCENT)
    g = Graph(mat)
    tex = g.new(unreal.MaterialExpressionTextureSample, texture=smoke,
                sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_MASKS)
    pc = g.new(unreal.MaterialExpressionParticleColor)
    g.out(pc, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    opacity = g.mul(g.mul((tex, "R"), (pc, "A")), g.depth_fade(20.0))
    g.out(opacity, unreal.MaterialProperty.MP_OPACITY)
    return finish_material(mat)


# ----------------------------------------------------------------------------------------------
# Niagara systems
# ----------------------------------------------------------------------------------------------

def fx_lib():
    lib = getattr(unreal, "SoulsFxLibrary", None)
    if lib is None:
        raise RuntimeError("unreal.SoulsFxLibrary missing: build the UnrealSoulslike C++ module first")
    return lib


class Fx:
    """One system: a FountainLightweight copy with every module set explicitly (so re-runs converge)."""

    def __init__(self, name):
        self.lib = fx_lib()
        self.name = name
        path = "%s/%s" % (FIRE, name)
        if eal.does_asset_exist(path):
            self.system = eal.load_asset(path)
        else:
            # The commandlet's asset registry hasn't scanned plugin content, so duplicate by object, not path.
            unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous([TEMPLATE.rsplit("/", 1)[0]], True)
            template = unreal.load_asset(TEMPLATE)
            self.system = eal.duplicate_loaded_asset(template, path) if template else None
        if self.system is None:
            raise RuntimeError("could not make %s from %s" % (path, TEMPLATE))
        modules = list(self.lib.list_fx_modules(self.system))
        log("%s modules: %s" % (name, ", ".join(modules)))
        for entry in modules:
            module = entry.split(" ")[0]
            if entry.startswith("renderer ") or entry.startswith("no ") or module in KEEP_ON:
                continue
            self.value(module, "bModuleEnabled", "False")
        for module in KEEP_ON:
            self.value(module, "bModuleEnabled", "True")

    def value(self, target, path, text):
        if not self.lib.set_fx_value(self.system, target, path, str(text)):
            warn("%s: %s.%s = %s failed" % (self.name, target, path, text))

    def dist(self, target, path, mode, values):
        if not self.lib.set_fx_distribution(self.system, target, path, mode, [float(v) for v in values]):
            warn("%s: %s.%s (%s %s) failed" % (self.name, target, path, mode, values))

    def module(self, target):
        self.value(target, "bModuleEnabled", "True")

    def finish(self):
        if not self.lib.finish_fx(self.system):
            warn("%s: finish failed" % self.name)
        eal.save_loaded_asset(self.system)
        for target, path in (("Emitter", "SpawnInfos"), ("Emitter", "EmitterState.LoopBehavior"),
                             ("SpriteRenderer", "Material")):
            log("  %s.%s = %s" % (target, path, self.lib.get_fx_value(self.system, target, path)))
        log("system %s" % self.name)


def build_system(name, spec):
    fx = Fx(name)
    # Spawn: a steady rate, looping for ever (Deactivate stops it; what's alive burns out).
    fx.value("Emitter", "SpawnInfos[0].Type", "Rate")
    fx.value("Emitter", "SpawnInfos[0].bEnabled", "True")
    fx.dist("Emitter", "SpawnInfos[0].Rate", "UniformRange", spec["rate"])
    fx.value("Emitter", "EmitterState.LoopBehavior", "Infinite")
    lo, hi = spec["bounds"]
    fx.value("Emitter", "FixedBounds", "(Min=(X=%g,Y=%g,Z=%g),Max=(X=%g,Y=%g,Z=%g),IsValid=1)" % (lo + hi))

    fx.dist("InitializeParticle", "LifetimeDistribution", "UniformRange", spec["lifetime"])
    fx.dist("InitializeParticle", "ColorDistribution", "NonUniformConstant", spec["color"])
    fx.dist("InitializeParticle", "SpriteSizeDistribution", "UniformRange", spec["size"])
    fx.dist("InitializeParticle", "SpriteRotationDistribution", "UniformRange", spec["rotation"])

    fx.module("ShapeLocation")
    fx.value("ShapeLocation", "ShapePrimitive", "Sphere")
    fx.dist("ShapeLocation", "SphereRadius", "UniformConstant", [spec["radius"]])
    fx.dist("ShapeLocation", "ShapeScale", "NonUniformConstant", spec.get("squash", (1.0, 1.0, 1.0)))
    fx.dist("ShapeLocation", "TransformOffset", "NonUniformConstant", spec.get("offset", (0.0, 0.0, 0.0)))

    fx.module("AddVelocity")
    fx.value("AddVelocity", "VelocityType", "Linear")
    fx.value("AddVelocity", "CoordinateSpace", "Local")
    fx.dist("AddVelocity", "LinearVelocityDistribution", "NonUniformRange", spec["velocity"])

    # Hot gas rises: an upward pull instead of gravity, slowed by drag and stirred by curl noise.
    fx.module("AccelerationForce")
    fx.value("AccelerationForce", "CoordinateSpace", "Local")
    fx.dist("AccelerationForce", "AccelerationDistribution", "NonUniformRange", spec["lift"])
    fx.module("Drag")
    fx.dist("Drag", "DragDistribution", "UniformConstant", [spec["drag"]])
    fx.module("CurlNoiseForce")
    fx.value("CurlNoiseForce", "NoiseStrength", spec["curl"][0])
    fx.value("CurlNoiseForce", "NoiseFrequency", spec["curl"][1])

    if spec.get("spin"):
        fx.module("SpriteRotationRate")
        fx.dist("SpriteRotationRate", "RotationRateDistribution", "UniformRange", spec["spin"])
    fx.module("ScaleSpriteSize")
    fx.dist("ScaleSpriteSize", "ScaleDistribution", "UniformCurve", spec["size_curve"])
    fx.module("ScaleColor")
    fx.dist("ScaleColor", "ScaleDistribution", "ColorGradient", spec["color_curve"])

    fx.value("SpriteRenderer", "Material", spec["material"].get_path_name())
    if spec.get("frames"):
        fx.module("SubUVAnimation")
        fx.value("SubUVAnimation", "NumFrames", spec["frames"])
        fx.value("SubUVAnimation", "AnimationMode", "Linear")
        fx.value("SpriteRenderer", "SubImageSize", "(X=2.000000,Y=2.000000)")
        fx.value("SpriteRenderer", "bSubImageBlend", "True")
    else:
        fx.value("SpriteRenderer", "SubImageSize", "(X=1.000000,Y=1.000000)")
        fx.value("SpriteRenderer", "bSubImageBlend", "False")
    fx.value("SpriteRenderer", "SortMode", spec.get("sort", "None"))
    fx.value("SpriteRenderer", "bCastShadows", "False")
    fx.finish()
    return fx.system


# Flames fade in fast, swell, then shrink and redden as they cool; times are the particle's age 0..1.
FLAME_SIZE = [0.0, 0.55, 0.25, 1.0, 0.7, 0.75, 1.0, 0.2]
FLAME_COLOR = [0.0, 1.0, 1.0, 1.0, 0.0,
               0.12, 1.0, 1.0, 1.0, 1.0,
               0.55, 1.0, 0.72, 0.5, 0.8,
               1.0, 0.65, 0.28, 0.14, 0.0]
FLAME_RGBA = (1.0, 0.42, 0.12, 1.0)


def build_systems(flame, ember, smoke):
    build_system("NS_TorchFlame", {
        "rate": (55, 65), "lifetime": (0.35, 0.65), "color": FLAME_RGBA,
        "size": (7, 12), "rotation": (-25, 25), "spin": (-50, 50),
        "radius": 2.5, "squash": (1.0, 1.0, 0.6),
        "velocity": (-5, -5, 18, 5, 5, 40), "lift": (0, 0, 80, 0, 0, 140), "drag": 1.5, "curl": (40, 50),
        "size_curve": FLAME_SIZE, "color_curve": FLAME_COLOR,
        "material": flame, "frames": 4, "bounds": ((-35, -35, -10), (35, 35, 80)),
    })
    build_system("NS_TorchFlameGround", {
        "rate": (85, 100), "lifetime": (0.45, 0.85), "color": FLAME_RGBA,
        "size": (11, 19), "rotation": (-25, 25), "spin": (-40, 40),
        "radius": 8.0, "squash": (1.0, 1.0, 0.25),
        "velocity": (-6, -6, 22, 6, 6, 48), "lift": (0, 0, 100, 0, 0, 160), "drag": 1.2, "curl": (45, 50),
        "size_curve": FLAME_SIZE, "color_curve": FLAME_COLOR,
        "material": flame, "frames": 4, "bounds": ((-60, -60, -15), (60, 60, 120)),
    })
    build_system("NS_TorchEmbers", {
        "rate": (6, 9), "lifetime": (0.9, 1.8), "color": (6.0, 2.2, 0.5, 1.0),
        "size": (0.7, 1.5), "rotation": (0, 0),
        "radius": 4.0, "squash": (1.0, 1.0, 0.5),
        "velocity": (-18, -18, 35, 18, 18, 90), "lift": (0, 0, 15, 0, 0, 40), "drag": 0.8, "curl": (120, 40),
        "size_curve": [0.0, 1.0, 0.8, 0.8, 1.0, 0.0],
        "color_curve": [0.0, 1.0, 1.0, 1.0, 1.0, 0.6, 1.0, 0.6, 0.4, 1.0, 1.0, 0.6, 0.2, 0.1, 0.0],
        "material": ember, "bounds": ((-120, -120, -10), (120, 120, 220)),
    })
    build_system("NS_TorchSmoke", {
        "rate": (4, 6), "lifetime": (1.8, 2.8), "color": (0.05, 0.045, 0.04, 0.35),
        "size": (10, 16), "rotation": (0, 360), "spin": (-25, 25),
        "radius": 3.0, "offset": (0.0, 0.0, 28.0),
        "velocity": (-6, -6, 30, 6, 6, 55), "lift": (0, 0, 10, 0, 0, 25), "drag": 0.6, "curl": (30, 30),
        "size_curve": [0.0, 0.6, 1.0, 3.2],
        "color_curve": [0.0, 1.0, 1.0, 1.0, 0.0, 0.2, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 0.0],
        "material": smoke, "sort": "ViewDepth", "bounds": ((-120, -120, -10), (120, 120, 300)),
    })


def main():
    ensure_dir(FIRE)
    atlas = import_texture("T_FlameAtlas.png", "T_FlameAtlas")
    smoke_tex = import_texture("smoke_04.png", "T_Smoke")
    flame = build_flame_material(atlas)
    ember = build_ember_material()
    smoke = build_smoke_material(smoke_tex)
    build_systems(flame, ember, smoke)
    eal.save_directory(FIRE, only_if_is_dirty=True, recursive=True)
    log("problems: %d" % len(problems))
    for p in problems:
        log("  - " + p)
    log("TORCH_FIRE_OK")


try:
    main()
except Exception as exc:
    import traceback
    log("FAILED: %s\n%s" % (exc, traceback.format_exc()))
