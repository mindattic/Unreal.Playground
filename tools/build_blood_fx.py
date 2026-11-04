"""Editor-Python (UE 5.8), UNR-T56: blood and guts.

Imports the blood sheets baked from CC0 art (SourceArt/FX/Blood, credits in SourceArt/FX/Blood/CREDITS.md;
re-bake with tools/bake_blood_atlases.py) and builds the four materials under /Game/Souls/FX/Blood that
Source/UnrealSoulslike/Souls/SoulsBlood.cpp hard-references (so a packaged build cooks them):

  M_BloodSpurt  masked, lit, wet: the 4x2 arterial-spurt flipbook on a camera-facing quad
  M_BloodMist   translucent, lit: the 4x4 red-mist flipbook, soft where it meets the ground
  M_BloodDrop   masked, lit, wet: a round drop with a domed normal (glints), stretched along its flight
  M_BloodDecal  deferred decal: one of four splatter patches, dark wet red, glossy, fades in and out

The particle materials are drawn by instanced quads that ASoulsBloodPool moves itself (no Niagara):
per-instance custom data 0 = flipbook frame, 1 = fade (1 fresh .. 0 gone), 2 = random seed. The quad's
texture "up" is its local +X, its face +Z (towards the camera); the materials build their normals from
the quad's own vertex normal and tangent, so the blood is lit as a wet surface, not a flat card.

Run (any time again, it's idempotent; the C++ doesn't need to be built first):
  UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<this file> -unattended -nop4 -nosplash
Writes Saved/blood_fx.log (the commandlet swallows print()), ending in BLOOD_FX_OK.
"""
import os
import shutil
import unreal

PROJECT = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
LOG = open(os.path.join(PROJECT, "Saved", "blood_fx.log"), "w", encoding="utf-8")
eal = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
MEL = unreal.MaterialEditingLibrary
MP = unreal.MaterialProperty
problems = []

BLOOD = "/Game/Souls/FX/Blood"
SRC = os.path.join(PROJECT, "SourceArt", "FX", "Blood")
STAGE = os.path.join(PROJECT, "Saved", "blood_fx")

# Linear colours of blood: nearly black where it pools thick, a deep crimson where light gets in.
DARK = (0.045, 0.0016, 0.0012)
MID = (0.20, 0.007, 0.0045)


def log(msg):
    LOG.write(msg + "\n")
    LOG.flush()


def warn(msg):
    problems.append(msg)
    log("WARNING " + msg)


def ensure_dir(path):
    if not eal.does_directory_exist(path):
        eal.make_directory(path)


def hlsl3(rgb):
    return "float3(%g, %g, %g)" % rgb


# ----------------------------------------------------------------------------------------------
# Textures
# ----------------------------------------------------------------------------------------------

def import_texture(name):
    """Imports SourceArt/FX/Blood/<name>.png as BLOOD/<name> (sRGB colour + alpha, clamped, effects LODs)."""
    os.makedirs(STAGE, exist_ok=True)
    staged = os.path.join(STAGE, name + ".png")
    shutil.copyfile(os.path.join(SRC, name + ".png"), staged)
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", staged)
    task.set_editor_property("destination_path", BLOOD)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("automated", True)
    task.set_editor_property("save", True)
    tools.import_asset_tasks([task])
    tex = eal.load_asset("%s/%s" % (BLOOD, name))
    if tex is None:
        raise RuntimeError("texture import failed: " + name)
    tex.set_editor_property("srgb", True)
    tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_DEFAULT)
    for key, value in (("lod_group", unreal.TextureGroup.TEXTUREGROUP_EFFECTS),
                       ("address_x", unreal.TextureAddress.TA_CLAMP),
                       ("address_y", unreal.TextureAddress.TA_CLAMP)):
        try:
            tex.set_editor_property(key, value)
        except Exception as exc:
            warn("%s on %s: %s" % (key, name, exc))
    eal.save_loaded_asset(tex)
    log("texture %s" % name)
    return tex


# ----------------------------------------------------------------------------------------------
# Material graph helpers
# ----------------------------------------------------------------------------------------------

class Graph:
    def __init__(self, mat):
        self.mat = mat
        self.n = 0

    def new(self, cls, **props):
        e = MEL.create_material_expression(self.mat, cls, -1200 + 220 * (self.n % 5), 180 * (self.n // 5))
        self.n += 1
        for key, value in props.items():
            e.set_editor_property(key, value)
        return e

    def link(self, src, out, dst, inp):
        if not MEL.connect_material_expressions(src, out, dst, inp):
            warn("%s: could not link %s.%s -> %s.%s" % (self.mat.get_name(), src.get_name(), out, dst.get_name(), inp))

    def const(self, value):
        return self.new(unreal.MaterialExpressionConstant, r=value)

    def custom(self, name, code, out_type, inputs):
        """An HLSL node. inputs: [(pin name, source expression, source output name)]."""
        e = self.new(unreal.MaterialExpressionCustom)
        e.set_editor_property("code", code)
        e.set_editor_property("description", name)
        e.set_editor_property("output_type", out_type)
        pins = []
        for pin, _src, _out in inputs:
            ci = unreal.CustomInput()
            ci.set_editor_property("input_name", pin)
            pins.append(ci)
        e.set_editor_property("inputs", pins)
        for pin, src, out in inputs:
            self.link(src, out, e, pin)
        return e

    def instance_data(self, index):
        e = self.new(unreal.MaterialExpressionPerInstanceCustomData)
        e.set_editor_property("data_index", index)
        return e

    def sample(self, texture, uv):
        e = self.new(unreal.MaterialExpressionTextureSample, texture=texture)
        self.link(uv, "", e, "")  # first input: the UVs
        return e

    def out(self, src, prop, out_name=""):
        if not MEL.connect_material_property(src, out_name, prop):
            warn("%s: could not connect %s" % (self.mat.get_name(), prop))


F1 = unreal.CustomMaterialOutputType.CMOT_FLOAT1
F2 = unreal.CustomMaterialOutputType.CMOT_FLOAT2
F3 = unreal.CustomMaterialOutputType.CMOT_FLOAT3


def new_material(name):
    path = "%s/%s" % (BLOOD, name)
    mat = eal.load_asset(path) if eal.does_asset_exist(path) else \
        tools.create_asset(name, BLOOD, unreal.Material, unreal.MaterialFactoryNew())
    MEL.delete_all_material_expressions(mat)
    return mat


def particle_material(name, blend):
    """Lit, two-sided, for the pool's instanced quads."""
    mat = new_material(name)
    mat.set_editor_property("material_domain", unreal.MaterialDomain.MD_SURFACE)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    mat.set_editor_property("blend_mode", blend)
    mat.set_editor_property("two_sided", True)
    mat.set_editor_property("used_with_instanced_static_meshes", True)
    return mat


def finish_material(mat):
    MEL.recompile_material(mat)
    eal.save_loaded_asset(mat)
    log("material %s" % mat.get_name())
    return mat


def flipbook_uv(g, cols, rows):
    """UV of cell <custom data 0> in a cols x rows sheet, read left to right, top to bottom."""
    uv = g.new(unreal.MaterialExpressionTextureCoordinate)
    frame = g.instance_data(0)
    code = ("float f = floor(Frame + 0.5);\n"
            "float2 cell = float2(fmod(f, %d.0), floor(f / %d.0));\n"
            "return (saturate(UV) + cell) / float2(%d.0, %d.0);" % (cols, cols, cols, rows))
    return g.custom("FlipbookUV", code, F2, [("UV", uv, ""), ("Frame", frame, "")])


def wet_normal(g, flat, bulge):
    """World normal of a dome over the quad (flat=0, bulge=1: a round drop; more flat, a gentle swell),
    built from the quad's vertex normal (towards the camera) and tangent (its texture up)."""
    uv = g.new(unreal.MaterialExpressionTextureCoordinate)
    n = g.new(unreal.MaterialExpressionVertexNormalWS)
    t = g.new(unreal.MaterialExpressionVertexTangentWS)
    code = ("float2 d = saturate(UV) * 2.0 - 1.0;\n"
            "float z = sqrt(saturate(1.0 - dot(d, d)));\n"
            "float3 Nn = normalize(N);\n"
            "float3 Tn = normalize(T - dot(T, Nn) * Nn);\n"
            "float3 Bn = cross(Nn, Tn);\n"
            "return normalize(Nn * (z + %g) + (Bn * d.x - Tn * d.y) * %g);" % (flat, bulge))
    return g.custom("WetNormal", code, F3, [("UV", uv, ""), ("N", n, ""), ("T", t, "")])


def wet_surface(g, roughness, specular):
    g.out(g.const(roughness), MP.MP_ROUGHNESS)
    g.out(g.const(specular), MP.MP_SPECULAR)
    g.out(g.const(0.0), MP.MP_METALLIC)


# ----------------------------------------------------------------------------------------------
# Materials
# ----------------------------------------------------------------------------------------------

def build_spurt(atlas):
    mat = particle_material("M_BloodSpurt", unreal.BlendMode.BLEND_MASKED)
    mat.set_editor_property("opacity_mask_clip_value", 0.33)
    mat.set_editor_property("tangent_space_normal", False)
    g = Graph(mat)
    tex = g.sample(atlas, flipbook_uv(g, 4, 2))
    fade = g.instance_data(1)
    # Thins (erodes) as it fades rather than turning see-through.
    g.out(g.custom("SpurtMask", "return A * (0.3 + 0.7 * Fade) * 1.3;", F1, [("A", tex, "A"), ("Fade", fade, "")]),
          MP.MP_OPACITY_MASK)
    # The sheet's red says thick or thin; its painted white streaks become wet highlights.
    colour = ("float k = saturate(R);\n"
              "return lerp(%s, %s, k * k) + saturate(G - 0.35) * float3(0.12, 0.05, 0.05);" % (hlsl3(DARK), hlsl3(MID)))
    g.out(g.custom("SpurtColour", colour, F3, [("R", tex, "R"), ("G", tex, "G")]), MP.MP_BASE_COLOR)
    g.out(wet_normal(g, 0.7, 0.45), MP.MP_NORMAL)
    wet_surface(g, 0.16, 0.6)
    return finish_material(mat)


def build_drop():
    mat = particle_material("M_BloodDrop", unreal.BlendMode.BLEND_MASKED)
    mat.set_editor_property("opacity_mask_clip_value", 0.33)
    mat.set_editor_property("tangent_space_normal", False)
    g = Graph(mat)
    uv = g.new(unreal.MaterialExpressionTextureCoordinate)
    fade = g.instance_data(1)
    mask = ("float2 d = saturate(UV) * 2.0 - 1.0;\n"
            "return (1.0 - dot(d, d)) * (0.45 + 0.55 * Fade) * 1.6;")
    g.out(g.custom("DropMask", mask, F1, [("UV", uv, ""), ("Fade", fade, "")]), MP.MP_OPACITY_MASK)
    colour = ("float2 d = saturate(UV) * 2.0 - 1.0;\n"
              "return lerp(%s, %s, saturate(dot(d, d)));" % (hlsl3((0.17, 0.006, 0.004)), hlsl3(DARK)))
    g.out(g.custom("DropColour", colour, F3, [("UV", uv, "")]), MP.MP_BASE_COLOR)
    g.out(wet_normal(g, 0.0, 1.0), MP.MP_NORMAL)
    wet_surface(g, 0.1, 0.7)
    return finish_material(mat)


def build_mist(atlas):
    mat = particle_material("M_BloodMist", unreal.BlendMode.BLEND_TRANSLUCENT)
    g = Graph(mat)
    tex = g.sample(atlas, flipbook_uv(g, 4, 4))
    fade = g.instance_data(1)
    soft = g.new(unreal.MaterialExpressionDepthFade)
    for key, value in (("opacity_default", 1.0), ("fade_distance_default", 15.0)):
        try:
            soft.set_editor_property(key, value)
        except Exception as exc:
            warn("depth fade %s: %s" % (key, exc))
    g.out(g.custom("MistOpacity", "return saturate(A * Fade * 0.65 * Soft);", F1,
                   [("A", tex, "A"), ("Fade", fade, ""), ("Soft", soft, "")]), MP.MP_OPACITY)
    colour = "return lerp(%s, %s, saturate(R));" % (hlsl3(DARK), hlsl3(MID))
    g.out(g.custom("MistColour", colour, F3, [("R", tex, "R")]), MP.MP_BASE_COLOR)
    wet_surface(g, 0.5, 0.3)
    return finish_material(mat)


def build_decal(atlas):
    mat = new_material("M_BloodDecal")
    mat.set_editor_property("material_domain", unreal.MaterialDomain.MD_DEFERRED_DECAL)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    g = Graph(mat)
    uv = g.new(unreal.MaterialExpressionTextureCoordinate)
    dc = g.new(unreal.MaterialExpressionDecalColor)
    # Decal colour A picks one of the sheet's four patches, R how dark it is.
    atlas_uv = g.custom("DecalCell", "float c = floor(Cell + 0.5);\n"
                                     "return (saturate(UV) + float2(fmod(c, 2.0), floor(c / 2.0))) * 0.5;",
                        F2, [("UV", uv, ""), ("Cell", dc, "A")])
    tex = g.sample(atlas, atlas_uv)
    life = g.new(unreal.MaterialExpressionDecalLifetimeOpacity)
    g.out(g.custom("DecalOpacity", "return saturate((A - 0.12) * 1.8) * Life;", F1,
                   [("A", tex, "A"), ("Life", life, "")]), MP.MP_OPACITY)
    colour = "return lerp(%s, %s, saturate(R)) * Shade;" % (hlsl3((0.03, 0.0012, 0.001)), hlsl3((0.15, 0.005, 0.004)))
    g.out(g.custom("DecalColour", colour, F3, [("R", tex, "R"), ("Shade", dc, "R")]), MP.MP_BASE_COLOR)
    wet_surface(g, 0.07, 0.5)
    return finish_material(mat)


def main():
    ensure_dir(BLOOD)
    spurt = import_texture("T_BloodSpurtAtlas")
    mist = import_texture("T_BloodMistAtlas")
    decals = import_texture("T_BloodDecals")
    build_spurt(spurt)
    build_mist(mist)
    build_drop()
    build_decal(decals)
    for name in ("M_BloodSpurt", "M_BloodMist", "M_BloodDrop", "M_BloodDecal"):
        if not eal.does_asset_exist("%s/%s" % (BLOOD, name)):
            warn("missing %s/%s" % (BLOOD, name))
    if problems:
        log("%d problem(s): %s" % (len(problems), "; ".join(problems)))
    log("BLOOD_FX_OK")


try:
    main()
except Exception as exc:
    import traceback
    log("ERROR %s\n%s" % (exc, traceback.format_exc()))
finally:
    LOG.close()
