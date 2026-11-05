"""Editor-Python (UE 5.8), UNR-T51 / UNR-T75: every fire in the game.

UNR-T75 replaced the cartoon sprites with real fire: flipbooks rendered from a Mantaflow fire simulation
(tools/bake_fire_flipbooks.py, Blender; the sheets are committed under SourceArt/FX/Fire, see
SourceArt/FX/CREDITS.md), shaded as HDR fire, plus Epic's free Niagara Fluids 3D gas fire for the hero
fires close to the camera. No meshes: flames, smoke, embers and heat haze are all particles.

Imports the sheets, builds the materials and the Niagara systems under /Game/Souls/FX/Fire, which
Source/UnrealSoulslike/Souls/SoulsTorch.cpp and SoulsFire.cpp load by path on first use in play (a packaged
build cooks them through DirectoriesToAlwaysCook=/Game/Souls in Config/DefaultGame.ini):

  T_FireFlipbook       8x8 flipbook of the simulated fire (R flame, G hottest gas, B soot, A coverage)
  T_SmokeFlipbook      8x8 flipbook of the simulated smoke plume (RGB lit grey, A opacity)
  M_FireFlipbook       additive HDR fire: deep red -> orange -> yellow-white by the sim's temperature,
                       frame-blended sub-UV, soft where it meets the ground
  M_FireSmokeLit       lit translucent smoke (the fire's own light and the day/night reach it)
  M_TorchEmber         additive streak for the embers (velocity aligned)
  M_FireHaze           heat shimmer: screen-space refraction (2D offset), no colour

  NS_TorchFlame        the flame of a torch in a hand, in a bracket or in flight (rises world-up)
  NS_TorchFlameGround  the wider fire of a torch lying on the ground (and of a knight set alight)
  NS_TorchEmbers       sparks lifting off either, drifting on curl noise
  NS_TorchSmoke        smoke over a torch at rest
  NS_FireHaze          heat shimmer over a fire
  NS_FieldFire         one patch (2.56 m) of a grass / heather fire's flames (SoulsFire.cpp pools these)
  NS_FieldSmoke        a thick rising smoke column over a burning area, leaning downwind
  NS_FieldEmbers       embers blown up and downwind off a burning area
  NS_FluidFire         copy of NiagaraFluids' Grid3D_Gas_Fire (a real-time 3D gas simulation, raymarched):
                       the hero fire for the torch nearest the camera (the plugin must be enabled in the
                       .uproject; without it this one is skipped with a warning and torches use sprites)

The sprite systems are copies of the engine's FountainLightweight template: one lightweight (stateless)
emitter, simulated in the component's space. Stateless modules aren't scriptable from Python, so they're
tuned through unreal.SoulsFxLibrary (Source/UnrealSoulslike/Souls/SoulsFxLibrary.*): build the C++ first.

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
SRC = os.path.join(PROJECT, "SourceArt", "FX", "Fire")
STAGE = os.path.join(PROJECT, "Saved", "torch_fire")
TEMPLATE = "/Niagara/DefaultAssets/Templates/Systems/FountainLightweight"
FLUID_TEMPLATE = "/NiagaraFluids/Templates/Gas/3D/Systems/Grid3D_Gas_Fire"
# Modules the emitter can't do without; every other one is switched off, then on again where used.
KEEP_ON = {"InitializeParticle", "SolveVelocitiesAndForces"}
FRAMES = 64          # the flipbooks: 8 x 8
SHEET = 8
FIRE_FPS = 30.0      # the sim was rendered at 30 fps


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

def import_texture(src_name, asset_name, compression):
    """Imports SourceArt/FX/Fire/<src_name> as FIRE/<asset_name>: sRGB colour (stored with a 1/2.2 curve), linear alpha."""
    path = os.path.join(SRC, src_name)
    if not os.path.exists(path):
        raise RuntimeError("missing %s (bake it with tools/bake_fire_flipbooks.py)" % path)
    os.makedirs(STAGE, exist_ok=True)
    staged = os.path.join(STAGE, asset_name + ".png")
    shutil.copyfile(path, staged)
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
    tex.set_editor_property("srgb", True)
    tex.set_editor_property("compression_settings", compression)
    for key, value in (("lod_group", unreal.TextureGroup.TEXTUREGROUP_EFFECTS),
                       ("address_x", unreal.TextureAddress.TA_CLAMP),
                       ("address_y", unreal.TextureAddress.TA_CLAMP)):
        try:
            tex.set_editor_property(key, value)
        except Exception as exc:
            warn("%s on %s: %s" % (key, asset_name, exc))
    eal.save_loaded_asset(tex)
    log("texture %s <- %s" % (asset_name, src_name))
    return tex


# ----------------------------------------------------------------------------------------------
# Materials (sprite materials for Niagara)
# ----------------------------------------------------------------------------------------------

def new_material(name, blend, shading=unreal.MaterialShadingModel.MSM_UNLIT):
    path = "%s/%s" % (FIRE, name)
    mat = eal.load_asset(path) if eal.does_asset_exist(path) else \
        tools.create_asset(name, FIRE, unreal.Material, unreal.MaterialFactoryNew())
    MEL.delete_all_material_expressions(mat)
    mat.set_editor_property("shading_model", shading)
    mat.set_editor_property("blend_mode", blend)
    mat.set_editor_property("used_with_niagara_sprites", True)
    return mat


class Graph:
    def __init__(self, mat):
        self.mat = mat
        self.y = 0

    def new(self, cls, **props):
        e = MEL.create_material_expression(self.mat, cls, -1200 + 160 * (self.y % 7), 140 * (self.y // 7))
        self.y += 1
        for key, value in props.items():
            e.set_editor_property(key, value)
        return e

    def link(self, src, out, dst, inp):
        if not MEL.connect_material_expressions(src, out, dst, inp):
            warn("%s: could not link %s.%s -> %s.%s" % (self.mat.get_name(), src.get_name(), out, dst.get_name(), inp))

    def const(self, value):
        if isinstance(value, (tuple, list)):
            if len(value) == 2:
                return self.new(unreal.MaterialExpressionConstant2Vector, r=value[0], g=value[1])
            return self.new(unreal.MaterialExpressionConstant3Vector, constant=unreal.LinearColor(value[0], value[1], value[2], 1.0))
        return self.new(unreal.MaterialExpressionConstant, r=float(value))

    def _in(self, operand, out=""):
        """An operand: an expression, an (expression, output) pair, a number or a 2/3-tuple of numbers."""
        if isinstance(operand, (int, float)) or (isinstance(operand, (tuple, list)) and all(isinstance(v, (int, float)) for v in operand)):
            return self.const(operand), ""
        if isinstance(operand, tuple):
            return operand
        return operand, out

    def op(self, cls, pins, **props):
        e = self.new(cls, **props)
        for pin, operand in pins.items():
            src, out = self._in(operand)
            self.link(src, out, e, pin)
        return e

    def binop(self, cls, a, b):
        return self.op(cls, {"A": a, "B": b})

    def mul(self, a, b):
        return self.binop(unreal.MaterialExpressionMultiply, a, b)

    def add(self, a, b):
        return self.binop(unreal.MaterialExpressionAdd, a, b)

    def sub(self, a, b):
        return self.binop(unreal.MaterialExpressionSubtract, a, b)

    def div(self, a, b):
        return self.binop(unreal.MaterialExpressionDivide, a, b)

    def lerp(self, a, b, alpha):
        return self.op(unreal.MaterialExpressionLinearInterpolate, {"A": a, "B": b, "Alpha": alpha})

    def sat(self, a):
        return self.op(unreal.MaterialExpressionSaturate, {"": a})

    def power(self, a, exponent):
        return self.op(unreal.MaterialExpressionPower, {"Base": a, "Exp": exponent})

    def append(self, a, b):
        return self.binop(unreal.MaterialExpressionAppendVector, a, b)

    def scalar_param(self, name, value):
        return self.new(unreal.MaterialExpressionScalarParameter, parameter_name=name, default_value=float(value))

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
        src, out_name = self._in(src, out_name)
        if not MEL.connect_material_property(src, out_name, prop):
            warn("%s: could not connect %s" % (self.mat.get_name(), prop))


def finish_material(mat):
    MEL.recompile_material(mat)
    eal.save_loaded_asset(mat)
    log("material %s" % mat.get_name())
    return mat


def sub_uv(g, tex):
    """The particle's flipbook frame, blended with the next one (the renderer's SubImageSize and SubImageIndex)."""
    sub = g.new(unreal.MaterialExpressionParticleSubUV, texture=tex,
                sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    try:
        sub.set_editor_property("blend", True)
    except Exception as exc:
        warn("sub-UV blend: %s" % exc)
    return sub


def build_fire_material(sheet):
    """Additive HDR fire from the simulated flipbook.

    The sheet's R is how much flame the view ray crossed, G how much of it was the hottest gas. Thin flame
    is a deep, dim red; denser flame orange; the hottest gas yellow, then a white-hot core; the light
    output climbs steeply with both (fire is far brighter at its core than at its licks). Particle colour
    tints and scales it (rgb) and fades it (alpha). Soft where it meets the ground.
    """
    mat = new_material("M_FireFlipbook", unreal.BlendMode.BLEND_ADDITIVE)
    g = Graph(mat)
    sub = sub_uv(g, sheet)
    r = (sub, "R")
    hot = (sub, "G")
    soot = (sub, "B")
    # A blackbody-style ramp on the gas temperature (t = hot^1.8): only the base core is yellow-white,
    # the body orange, the licks deep red; soot darkens the tips. (The old ramp keyed yellow-white off
    # hot > 0.55, which the sim's hot channel exceeds almost everywhere: the whole sprite went white.)
    t = g.power(g.sat(hot), 1.8)
    col = g.lerp((0.45, 0.04, 0.005), (0.95, 0.22, 0.02), g.sat(g.div(t, 0.25)))
    col = g.lerp(col, (1.0, 0.48, 0.07), g.sat(g.div(g.sub(t, 0.25), 0.3)))
    col = g.lerp(col, (1.0, 0.80, 0.40), g.sat(g.div(g.sub(t, 0.55), 0.3)))
    col = g.lerp(col, (1.0, 0.95, 0.85), g.sat(g.div(g.sub(t, 0.85), 0.15)))
    glow = g.add(g.mul(g.power(r, 1.2), 1.2), g.mul(t, 3.0))
    glow = g.mul(glow, g.sub(1.0, g.mul(g.sat(g.sub(g.mul(soot, 1.5), r)), 0.6)))
    pc = g.new(unreal.MaterialExpressionParticleColor)
    brightness = g.scalar_param("Brightness", 2.0)
    lit = g.mul(g.mul(col, glow), g.mul(brightness, (pc, "A")))
    lit = g.mul(lit, (pc, "RGB"))
    g.out(g.mul(lit, g.depth_fade(12.0)), unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    return finish_material(mat)


def build_smoke_material(sheet):
    """Lit translucent smoke from the simulated flipbook: the fire's point light and the sky light it, so
    it glows over the flames at night and reads grey-brown by day. Particle colour tints (rgb) and fades (a)."""
    mat = new_material("M_FireSmokeLit", unreal.BlendMode.BLEND_TRANSLUCENT, unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    try:
        mat.set_editor_property("translucency_lighting_mode",
                                unreal.TranslucencyLightingMode.TLM_VOLUMETRIC_PER_VERTEX_NON_DIRECTIONAL)
    except Exception as exc:
        warn("smoke lighting mode: %s" % exc)
    g = Graph(mat)
    sub = sub_uv(g, sheet)
    pc = g.new(unreal.MaterialExpressionParticleColor)
    g.out(g.mul((sub, "RGB"), (pc, "RGB")), unreal.MaterialProperty.MP_BASE_COLOR)
    g.out(0.0, unreal.MaterialProperty.MP_SPECULAR)
    g.out(1.0, unreal.MaterialProperty.MP_ROUGHNESS)
    g.out(g.mul(g.mul((sub, "A"), (pc, "A")), g.depth_fade(40.0)), unreal.MaterialProperty.MP_OPACITY)
    return finish_material(mat)


def build_ember_material():
    """Additive: a soft hot streak (no texture) in the particle colour; the sprite is long and velocity aligned."""
    mat = new_material("M_TorchEmber", unreal.BlendMode.BLEND_ADDITIVE)
    g = Graph(mat)
    uv = g.new(unreal.MaterialExpressionTextureCoordinate)
    d = g.sub(uv, (0.5, 0.5))
    d2 = g.binop(unreal.MaterialExpressionDotProduct, d, d)
    dot = g.sat(g.sub(1.0, g.mul(d2, 4.0)))
    dot = g.mul(dot, dot)
    pc = g.new(unreal.MaterialExpressionParticleColor)
    g.out(g.mul(g.mul((pc, "RGB"), (pc, "A")), dot), unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    return finish_material(mat)


def set_refraction_2d(mat):
    enum = getattr(unreal, "RefractionMode", None)
    for name in ("RM_2D_OFFSET", "RM_2DOFFSET", "RM_2D_OFF_SET"):
        value = getattr(enum, name, None) if enum else None
        if value is not None:
            try:
                mat.set_editor_property("refraction_method", value)
                return True
            except Exception as exc:
                warn("refraction method: %s" % exc)
                return False
    warn("refraction method 2D Offset not found: heat haze left without refraction")
    return False


def build_haze_material(noise_sheet):
    """Heat shimmer: no colour, a screen-space 2D refraction offset from two drifting samples of the smoke
    sheet (as noise), strongest in the sprite's middle and scaled by particle alpha."""
    mat = new_material("M_FireHaze", unreal.BlendMode.BLEND_TRANSLUCENT)
    has_2d = set_refraction_2d(mat)
    g = Graph(mat)
    uv = g.new(unreal.MaterialExpressionTextureCoordinate)
    t = g.new(unreal.MaterialExpressionTime)
    uv1 = g.add(g.mul(uv, 1.7), g.mul(t, (0.03, 0.42)))
    uv2 = g.add(g.mul(uv, 2.3), g.mul(t, (-0.05, 0.61)))
    n1 = g.new(unreal.MaterialExpressionTextureSample, texture=noise_sheet,
               sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    g.link(uv1, "", n1, "UVs")
    n2 = g.new(unreal.MaterialExpressionTextureSample, texture=noise_sheet,
               sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    g.link(uv2, "", n2, "UVs")
    dx = g.sub((n1, "A"), (n2, "A"))
    dy = g.sub((n2, "A"), g.mul((n1, "A"), 0.6))
    d = g.sub(uv, (0.5, 0.5))
    mask = g.sat(g.sub(1.0, g.mul(g.binop(unreal.MaterialExpressionDotProduct, d, d), 4.0)))
    pc = g.new(unreal.MaterialExpressionParticleColor)
    strength = g.mul(g.mul(mask, (pc, "A")), g.scalar_param("Strength", 0.012))
    if has_2d:
        g.out(g.mul(g.append(dx, dy), strength), unreal.MaterialProperty.MP_REFRACTION)
    g.out(0.0, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    g.out(0.0, unreal.MaterialProperty.MP_OPACITY)
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
                             ("SpriteRenderer", "Material"), ("SpriteRenderer", "FacingMode")):
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
    size = spec["size"]
    if len(size) == 4:   # (minX, minY, maxX, maxY): a long sprite (embers)
        fx.dist("InitializeParticle", "SpriteSizeDistribution", "NonUniformRange", size)
    else:
        fx.dist("InitializeParticle", "SpriteSizeDistribution", "UniformRange", size)
    fx.dist("InitializeParticle", "SpriteRotationDistribution", "UniformRange", spec["rotation"])

    fx.module("ShapeLocation")
    if "box" in spec:
        fx.value("ShapeLocation", "ShapePrimitive", "Box")
        bx, by, bz = spec["box"]
        fx.dist("ShapeLocation", "BoxSize", "NonUniformConstant", (bx, by, bz))
    else:
        fx.value("ShapeLocation", "ShapePrimitive", "Sphere")
        fx.dist("ShapeLocation", "SphereRadius", "UniformConstant", [spec["radius"]])
        fx.dist("ShapeLocation", "ShapeScale", "NonUniformConstant", spec.get("squash", (1.0, 1.0, 1.0)))
    fx.dist("ShapeLocation", "TransformOffset", "NonUniformConstant", spec.get("offset", (0.0, 0.0, 0.0)))

    fx.module("AddVelocity")
    fx.value("AddVelocity", "VelocityType", "Linear")
    fx.value("AddVelocity", "CoordinateSpace", "Local")
    fx.dist("AddVelocity", "LinearVelocityDistribution", "NonUniformRange", spec["velocity"])

    # Hot gas rises: an upward pull (and, for the field, a push downwind along local +X) instead of
    # gravity, slowed by drag and stirred by curl noise.
    fx.module("AccelerationForce")
    fx.value("AccelerationForce", "CoordinateSpace", "Local")
    fx.dist("AccelerationForce", "AccelerationDistribution", "NonUniformRange", spec["lift"])
    fx.module("Drag")
    fx.dist("Drag", "DragDistribution", "UniformConstant", [spec["drag"]])
    if spec.get("curl"):
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

    # The component's scale grows / shrinks the fire (the field patches, a burning knight). The emitter
    # simulates in the component's space, so positions and motion already scale with it: only the sprite
    # size needs the owner's scale applied.
    fx.module("ApplyOwnerScaleToAttributes")
    for flag, on in (("bScaleInitialSpriteSize", True), ("bScaleInitialSpriteSizeBySingleAxis", True),
                     ("bScaleInitialVelocity", False), ("bScaleForces", False)):
        fx.value("ApplyOwnerScaleToAttributes", "SystemScaleData." + flag, str(on))

    fx.value("SpriteRenderer", "Material", spec["material"].get_path_name())
    if spec.get("flipbook"):
        fx.module("SubUVAnimation")
        fx.value("SubUVAnimation", "NumFrames", FRAMES)
        fx.value("SubUVAnimation", "AnimationMode", "InfiniteLoop")
        # Real time: the sheet's 64 frames at 30 fps (a touch faster for small, quick torch flames).
        fx.value("SubUVAnimation", "LoopsPerSecond", "%g" % (spec.get("fps", FIRE_FPS) / FRAMES))
        fx.value("SpriteRenderer", "SubImageSize", "(X=%d.000000,Y=%d.000000)" % (SHEET, SHEET))
        fx.value("SpriteRenderer", "bSubImageBlend", "True")
    else:
        fx.value("SpriteRenderer", "SubImageSize", "(X=1.000000,Y=1.000000)")
        fx.value("SpriteRenderer", "bSubImageBlend", "False")
    # Flames and smoke stay upright (world up, whatever the camera's roll); embers streak along their path.
    fx.value("SpriteRenderer", "FacingMode", spec.get("facing", "FaceCameraPosition"))
    fx.value("SpriteRenderer", "Alignment", spec.get("align", "Unaligned"))
    pivot = spec.get("pivot", (0.5, 0.5))
    fx.value("SpriteRenderer", "PivotInUVSpace", "(X=%g,Y=%g)" % pivot)
    fx.value("SpriteRenderer", "SortMode", spec.get("sort", "None"))
    fx.value("SpriteRenderer", "bCastShadows", "False")
    fx.finish()
    return fx.system


def fade(points):
    """[(age, alpha)] -> a ColorGradient that keeps rgb at 1 and shapes alpha."""
    out = []
    for age, alpha in points:
        out += [age, 1.0, 1.0, 1.0, alpha]
    return out


# The flipbook frame is the whole fire: each sprite fades in, burns for about a second and fades out,
# so the sprites overlapping at once show different moments of the same living fire.
FLAME_FADE = fade([(0.0, 0.0), (0.18, 1.0), (0.7, 0.85), (1.0, 0.0)])
# The fire's base sits low in each cell (the burner was at the bottom of the sim domain).
FLAME_PIVOT = (0.5, 0.86)


def build_systems(fire_mat, smoke_mat, ember_mat, haze_mat):
    build_system("NS_TorchFlame", {
        "rate": (5, 6), "lifetime": (0.9, 1.3), "color": (1.0, 1.0, 1.0, 0.62),
        "size": (30, 40), "rotation": (-6, 6),
        "radius": 1.5, "squash": (1.0, 1.0, 0.5),
        "velocity": (-2, -2, 3, 2, 2, 9), "lift": (0, 0, 6, 0, 0, 16), "drag": 1.0, "curl": (6, 30),
        "size_curve": [0.0, 0.8, 0.25, 1.0, 1.0, 0.9], "color_curve": FLAME_FADE,
        "material": fire_mat, "flipbook": True, "fps": 36.0, "pivot": FLAME_PIVOT,
        "bounds": ((-40, -40, -15), (40, 40, 90)),
    })
    build_system("NS_TorchFlameGround", {
        "rate": (7, 9), "lifetime": (1.0, 1.5), "color": (1.0, 1.0, 1.0, 0.6),
        "size": (38, 54), "rotation": (-8, 8),
        "radius": 7.0, "squash": (1.0, 1.0, 0.25),
        "velocity": (-3, -3, 3, 3, 3, 10), "lift": (0, 0, 6, 0, 0, 16), "drag": 1.0, "curl": (8, 25),
        "size_curve": [0.0, 0.75, 0.25, 1.0, 1.0, 0.85], "color_curve": FLAME_FADE,
        "material": fire_mat, "flipbook": True, "pivot": FLAME_PIVOT,
        "bounds": ((-70, -70, -15), (70, 70, 120)),
    })
    build_system("NS_TorchEmbers", {
        "rate": (9, 13), "lifetime": (1.0, 2.4), "color": (9.0, 3.2, 0.7, 1.0),
        "size": (0.35, 1.0, 0.7, 2.4), "rotation": (0, 0),
        "radius": 5.0, "squash": (1.0, 1.0, 0.5), "offset": (0.0, 0.0, 8.0),
        "velocity": (-22, -22, 55, 22, 22, 140), "lift": (0, 0, -10, 0, 0, 20), "drag": 1.1, "curl": (160, 35),
        "size_curve": [0.0, 1.0, 0.7, 0.8, 1.0, 0.0],
        "color_curve": [0.0, 1.0, 1.0, 1.0, 1.0, 0.5, 1.0, 0.55, 0.35, 1.0, 1.0, 0.6, 0.15, 0.06, 0.0],
        "material": ember_mat, "facing": "FaceCamera", "align": "VelocityAligned",
        "bounds": ((-150, -150, -10), (150, 150, 300)),
    })
    build_system("NS_TorchSmoke", {
        "rate": (3, 4), "lifetime": (2.4, 3.4), "color": (0.16, 0.145, 0.13, 0.42),
        "size": (16, 24), "rotation": (-15, 15), "spin": (-8, 8),
        "radius": 3.0, "offset": (0.0, 0.0, 24.0),
        "velocity": (-5, -5, 32, 5, 5, 52), "lift": (0, 0, 8, 0, 0, 18), "drag": 0.5, "curl": (22, 25),
        "size_curve": [0.0, 1.0, 1.0, 4.5],
        "color_curve": fade([(0.0, 0.0), (0.15, 1.0), (0.6, 0.8), (1.0, 0.0)]),
        "material": smoke_mat, "flipbook": True, "fps": 20.0, "sort": "ViewDepth",
        "bounds": ((-150, -150, -10), (150, 150, 330)),
    })
    build_system("NS_FireHaze", {
        "rate": (3, 4), "lifetime": (1.0, 1.4), "color": (1.0, 1.0, 1.0, 1.0),
        "size": (38, 56), "rotation": (0, 360),
        "radius": 4.0, "offset": (0.0, 0.0, 26.0),
        "velocity": (-4, -4, 30, 4, 4, 50), "lift": (0, 0, 5, 0, 0, 12), "drag": 0.6,
        "size_curve": [0.0, 0.7, 1.0, 1.6],
        "color_curve": fade([(0.0, 0.0), (0.3, 1.0), (0.7, 0.8), (1.0, 0.0)]),
        "material": haze_mat,
        "bounds": ((-120, -120, -10), (120, 120, 200)),
    })
    # The field fire (SoulsFire.cpp): one patch per 2 x 2 burning cells, scaled by its strength and
    # turned so local +X is downwind.
    build_system("NS_FieldFire", {
        "rate": (9, 12), "lifetime": (0.9, 1.5), "color": (1.0, 1.0, 1.0, 0.7),
        "size": (120, 190), "rotation": (-7, 7),
        "box": (230, 230, 10),
        "velocity": (-8, -8, 10, 8, 8, 35), "lift": (20, 0, 10, 60, 0, 30), "drag": 1.0, "curl": (30, 6),
        "size_curve": [0.0, 0.75, 0.25, 1.0, 1.0, 0.85], "color_curve": FLAME_FADE,
        "material": fire_mat, "flipbook": True, "fps": 26.0, "pivot": FLAME_PIVOT,
        "bounds": ((-400, -400, -50), (600, 400, 500)),
    })
    build_system("NS_FieldSmoke", {
        "rate": (4, 6), "lifetime": (6.0, 9.0), "color": (0.085, 0.078, 0.07, 0.62),
        "size": (220, 340), "rotation": (-20, 20), "spin": (-5, 5),
        "box": (500, 500, 60), "offset": (0.0, 0.0, 160.0),
        "velocity": (-20, -20, 150, 20, 20, 260), "lift": (40, 0, 30, 95, 0, 80), "drag": 0.25, "curl": (60, 6),
        "size_curve": [0.0, 1.0, 1.0, 4.5],
        "color_curve": fade([(0.0, 0.0), (0.1, 1.0), (0.55, 0.8), (1.0, 0.0)]),
        "material": smoke_mat, "flipbook": True, "fps": 14.0, "sort": "ViewDepth",
        "bounds": ((-1500, -1500, -100), (4500, 1500, 3500)),
    })
    build_system("NS_FieldEmbers", {
        "rate": (25, 35), "lifetime": (2.0, 4.0), "color": (12.0, 4.0, 0.9, 1.0),
        "size": (1.0, 2.5, 2.0, 5.0), "rotation": (0, 0),
        "box": (500, 500, 20), "offset": (0.0, 0.0, 40.0),
        "velocity": (-40, -40, 150, 40, 40, 400), "lift": (60, 0, -20, 140, 0, 20), "drag": 0.6, "curl": (250, 6),
        "size_curve": [0.0, 1.0, 0.7, 0.8, 1.0, 0.0],
        "color_curve": [0.0, 1.0, 1.0, 1.0, 1.0, 0.5, 1.0, 0.55, 0.35, 1.0, 1.0, 0.6, 0.15, 0.06, 0.0],
        "material": ember_mat, "facing": "FaceCamera", "align": "VelocityAligned",
        "bounds": ((-800, -800, -50), (2500, 800, 2000)),
    })


def build_fluid_fire():
    """NS_FluidFire: a copy of the NiagaraFluids plugin's Grid3D_Gas_Fire (a GPU 3D gas simulation with
    temperature, fuel and smoke, raymarched). SoulsTorch.cpp sizes its grid (User.WorldSpaceSize,
    User.ResolutionMaxAxis) and feeds it the torch head as its source (User.StaticMesh) at run time."""
    path = "%s/NS_FluidFire" % FIRE
    if eal.does_asset_exist(path):
        eal.delete_asset(path)   # always a fresh copy of the template (it's tuned at run time)
    try:
        unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous([FLUID_TEMPLATE.rsplit("/", 1)[0]], True)
    except Exception as exc:
        warn("scan %s: %s" % (FLUID_TEMPLATE, exc))
    template = unreal.load_asset(FLUID_TEMPLATE)
    if template is None:
        warn("NiagaraFluids template %s not found (enable the NiagaraFluids plugin): no hero fluid fire" % FLUID_TEMPLATE)
        return None
    system = eal.duplicate_loaded_asset(template, path)
    if system is None:
        warn("could not duplicate %s" % FLUID_TEMPLATE)
        return None
    eal.save_loaded_asset(system)
    log("system NS_FluidFire <- %s" % FLUID_TEMPLATE)
    return system


def main():
    ensure_dir(FIRE)
    fire_sheet = import_texture("T_FireFlipbook.png", "T_FireFlipbook", unreal.TextureCompressionSettings.TC_BC7)
    smoke_sheet = import_texture("T_SmokeFlipbook.png", "T_SmokeFlipbook", unreal.TextureCompressionSettings.TC_DEFAULT)
    fire_mat = build_fire_material(fire_sheet)
    smoke_mat = build_smoke_material(smoke_sheet)
    ember_mat = build_ember_material()
    haze_mat = build_haze_material(smoke_sheet)
    build_systems(fire_mat, smoke_mat, ember_mat, haze_mat)
    build_fluid_fire()
    # The UNR-T51 cartoon sprites and their materials are no longer used.
    for old in ("T_FlameAtlas", "T_Smoke", "M_TorchFlame", "M_TorchSmoke"):
        if eal.does_asset_exist("%s/%s" % (FIRE, old)):
            if not eal.delete_asset("%s/%s" % (FIRE, old)):
                warn("could not delete old %s" % old)
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
