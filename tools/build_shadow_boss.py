"""Editor-Python (UE 5.8): the Shade of the Fen (ASoulsShadowBoss), built on top of what
tools/build_souls_character.py made (montages, weapons, audio, BP_SoulsGameMode), so run it after.

  - M_ShadowPool   deferred decal: a creeping black pool with a boiling edge and violet veins
                   (scalar params Opacity, Pulse); also used for the phase-two puddles
  - M_ShadeOverlay translucent overlay over the armour: violet rim, black silhouette as it melts (Shade)
  - M_ShadowWisp   soft translucent smoke for the wisps curling off the pool
  - MI_ShadeNight_*/MI_ShadeAbyss_*  near-black armour with a violet sigil (phase two: blue, blazing)
  - BP_SoulsShadowBoss, and BP_SoulsGameMode.ShadowBossClass

Run: UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<this file> -unattended -nop4 -nosplash
Writes Saved/shadow_boss.log (the commandlet swallows print()).
"""
import os
import unreal

PROJECT = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
LOG = open(os.path.join(PROJECT, "Saved", "shadow_boss.log"), "w", encoding="utf-8")
eal = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
problems = []

SOULS = "/Game/Souls"
SHADE = SOULS + "/Shade"
MANNY_MESH = "/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple"
MANNY_SLOTS = ("/Game/Characters/Mannequins/Materials/Manny/MI_Manny_01_New",
               "/Game/Characters/Mannequins/Materials/Manny/MI_Manny_02_New")
# Same fit as the knights (tools/build_souls_character.py).
SWORD_TRANSFORM = {"location": (0.0, 0.0, 41.0), "rotation": (0.0, 0.0, 0.0), "scale": 55.0}
SHIELD_TRANSFORM = {"location": (0.0, 0.0, 0.0), "rotation": (0.0, 0.0, 0.0), "scale": 1.25}


def log(msg):
    LOG.write(msg + "\n")
    LOG.flush()


def warn(msg):
    problems.append(msg)
    log("WARNING " + msg)


def ensure_dir(path):
    if not eal.does_directory_exist(path):
        eal.make_directory(path)


def new_material(name):
    ensure_dir(SHADE)
    path = "%s/%s" % (SHADE, name)
    mat = eal.load_asset(path) if eal.does_asset_exist(path) else \
        tools.create_asset(name, SHADE, unreal.Material, unreal.MaterialFactoryNew())
    MEL.delete_all_material_expressions(mat)
    return mat


def const1(mat, value, x, y):
    c = MEL.create_material_expression(mat, unreal.MaterialExpressionConstant, x, y)
    c.set_editor_property("r", value)
    return c


def const3(mat, rgb, x, y):
    c = MEL.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, x, y)
    c.set_editor_property("constant", unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0))
    return c


def scalar(mat, name, default, x, y):
    p = MEL.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, x, y)
    p.set_editor_property("parameter_name", name)
    p.set_editor_property("default_value", default)
    return p


def custom(mat, code, output_type, inputs, x, y, desc):
    """A Custom HLSL node; inputs is [(name, expression)]."""
    node = MEL.create_material_expression(mat, unreal.MaterialExpressionCustom, x, y)
    node.set_editor_property("code", code)
    node.set_editor_property("output_type", output_type)
    node.set_editor_property("description", desc)
    pins = []
    for name, _ in inputs:
        pin = unreal.CustomInput()
        pin.set_editor_property("input_name", name)
        pins.append(pin)
    node.set_editor_property("inputs", pins)
    for name, expr in inputs:
        if not MEL.connect_material_expressions(expr, "", node, name):
            warn("%s: could not wire %s" % (mat.get_name(), name))
    return node


def finish(mat):
    MEL.recompile_material(mat)
    eal.save_loaded_asset(mat)
    log("material %s" % mat.get_name())
    return mat


# Shared by the pool's opacity and emissive: four octaves of drifting value noise over the disc.
POOL_NOISE = """
float2 p = UVIn - 0.5;
float r = length(p) * 2.0;
float n = 0.0;
float amp = 0.5;
float2 q = p * 4.5 + float2(TimeIn * 0.13, -TimeIn * 0.09);
for (int i = 0; i < 4; i++)
{
    float2 ip = floor(q);
    float2 fp = frac(q);
    float2 u = fp * fp * (3.0 - 2.0 * fp);
    float a = frac(sin(dot(ip, float2(127.1, 311.7))) * 43758.5453);
    float b = frac(sin(dot(ip + float2(1.0, 0.0), float2(127.1, 311.7))) * 43758.5453);
    float c = frac(sin(dot(ip + float2(0.0, 1.0), float2(127.1, 311.7))) * 43758.5453);
    float d = frac(sin(dot(ip + float2(1.0, 1.0), float2(127.1, 311.7))) * 43758.5453);
    n += amp * lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
    q = q * 2.02 + float2(5.2, 1.3) + TimeIn * 0.05;
    amp *= 0.5;
}
float dr = r + (n - 0.47) * (0.65 + PulseIn * 0.45);
float m = saturate((0.9 - dr) / 0.3);
m = m * m * (3.0 - 2.0 * m);
"""

POOL_OPACITY = POOL_NOISE + """
return m * OpacityIn;
"""

POOL_EMISSIVE = POOL_NOISE + """
float rim = saturate(1.0 - abs(dr - 0.74) / 0.1);
float veins = saturate(1.0 - abs(n - 0.5) * 16.0) * m;
float rings = saturate(1.0 - abs(frac(r * 2.5 - TimeIn * 1.8) - 0.5) * 5.0) * PulseIn * m;
float3 violet = float3(0.28, 0.07, 0.9);
return violet * (rim * (0.35 + PulseIn * 1.4) + veins * (0.05 + PulseIn * 0.3) + rings * 0.45) * OpacityIn;
"""


def build_pool_material():
    mat = new_material("M_ShadowPool")
    mat.set_editor_property("material_domain", unreal.MaterialDomain.MD_DEFERRED_DECAL)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    uv = MEL.create_material_expression(mat, unreal.MaterialExpressionTextureCoordinate, -1100, 0)
    time = MEL.create_material_expression(mat, unreal.MaterialExpressionTime, -1100, 120)
    opacity = scalar(mat, "Opacity", 1.0, -1100, 240)
    pulse = scalar(mat, "Pulse", 0.0, -1100, 360)
    pins = [("UVIn", uv), ("TimeIn", time), ("PulseIn", pulse), ("OpacityIn", opacity)]
    op = custom(mat, POOL_OPACITY, unreal.CustomMaterialOutputType.CMOT_FLOAT1, pins, -600, 200, "PoolOpacity")
    em = custom(mat, POOL_EMISSIVE, unreal.CustomMaterialOutputType.CMOT_FLOAT3, pins, -600, 0, "PoolEmissive")
    MEL.connect_material_property(op, "", unreal.MaterialProperty.MP_OPACITY)
    MEL.connect_material_property(em, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MEL.connect_material_property(const3(mat, (0.003, 0.002, 0.006), -400, -200), "", unreal.MaterialProperty.MP_BASE_COLOR)
    MEL.connect_material_property(const1(mat, 0.1, -400, -120), "", unreal.MaterialProperty.MP_ROUGHNESS)
    return finish(mat)


def build_overlay_material():
    mat = new_material("M_ShadeOverlay")
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    mat.set_editor_property("used_with_skeletal_mesh", True)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    fres = MEL.create_material_expression(mat, unreal.MaterialExpressionFresnel, -900, 0)
    fres.set_editor_property("exponent", 2.2)
    fres.set_editor_property("base_reflect_fraction", 0.0)
    shade = scalar(mat, "Shade", 0.12, -900, 200)
    pins = [("F", fres), ("ShadeIn", shade)]
    em = custom(mat, """
float s = saturate(ShadeIn);
return float3(0.30, 0.08, 1.0) * F * F * (0.45 + 0.35 * s) * (1.0 - 0.5 * s * (1.0 - F)) + float3(0.002, 0.001, 0.005) * s;
""", unreal.CustomMaterialOutputType.CMOT_FLOAT3, pins, -500, 0, "ShadeRim")
    op = custom(mat, """
float s = saturate(ShadeIn);
return saturate(lerp(F * 0.35, 1.0, s));
""", unreal.CustomMaterialOutputType.CMOT_FLOAT1, pins, -500, 200, "ShadeVeil")
    MEL.connect_material_property(em, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MEL.connect_material_property(op, "", unreal.MaterialProperty.MP_OPACITY)
    return finish(mat)


def build_wisp_material():
    mat = new_material("M_ShadowWisp")
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    fres = MEL.create_material_expression(mat, unreal.MaterialExpressionFresnel, -900, 0)
    fres.set_editor_property("exponent", 1.4)
    fres.set_editor_property("base_reflect_fraction", 0.0)
    pins = [("F", fres)]
    em = custom(mat, "return float3(0.005, 0.002, 0.012) + float3(0.25, 0.07, 0.8) * pow(F, 3.0) * 0.6;",
                unreal.CustomMaterialOutputType.CMOT_FLOAT3, pins, -500, 0, "WispColour")
    op = custom(mat, "return saturate(1.0 - F) * 0.55;", unreal.CustomMaterialOutputType.CMOT_FLOAT1, pins, -500, 200, "WispSoftness")
    MEL.connect_material_property(em, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MEL.connect_material_property(op, "", unreal.MaterialProperty.MP_OPACITY)
    return finish(mat)


def armour_instances(name, tint, metallic, roughness, logo, glow):
    """Epic's mannequin material per slot, as in build_souls_character.armour_instances."""
    folder = SOULS + "/Materials"
    ensure_dir(folder)
    out = []
    for i, parent_path in enumerate(MANNY_SLOTS):
        mi_name = "%s_%d" % (name, i)
        path = "%s/%s" % (folder, mi_name)
        mi = eal.load_asset(path) if eal.does_asset_exist(path) else \
            tools.create_asset(mi_name, folder, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
        mi.set_editor_property("parent", eal.load_asset(parent_path))
        MEL.set_material_instance_vector_parameter_value(mi, "Paint Tint", unreal.LinearColor(tint[0], tint[1], tint[2], 1.0))
        MEL.set_material_instance_scalar_parameter_value(mi, "MetalPaintMetallic", metallic)
        MEL.set_material_instance_scalar_parameter_value(mi, "MetalPaintRoughness", roughness)
        MEL.set_material_instance_vector_parameter_value(mi, "LogoTint", unreal.LinearColor(logo[0], logo[1], logo[2], 1.0))
        MEL.set_material_instance_scalar_parameter_value(mi, "EmissivePower", glow if i == 1 else 0.0)
        MEL.update_material_instance(mi)
        eal.save_loaded_asset(mi)
        out.append(mi)
    log("armour %s" % name)
    return out


def sound(stem):
    path = "%s/Audio/%s" % (SOULS, stem)
    return eal.load_asset(path) if eal.does_asset_exist(path) else None


def sounds_of(role):
    return [s for s in (sound("%s_%02d" % (role, i)) for i in range(1, 10)) if s is not None]


def place(component, mesh, spec):
    component.set_editor_property("static_mesh", mesh)
    component.set_editor_property("relative_location", unreal.Vector(*spec["location"]))
    component.set_editor_property("relative_rotation", unreal.Rotator(*spec["rotation"]))
    component.set_editor_property("relative_scale3d", unreal.Vector(spec["scale"], spec["scale"], spec["scale"]))


def build_blueprint(pool, overlay, wisp, night, abyss):
    script = lambda name: unreal.load_class(None, "/Script/UnrealSoulslike." + name)
    dest = SOULS + "/BP_SoulsShadowBoss"
    # Reused on rebuilds: BP_SoulsGameMode references it, so deleting it can fail.
    bp = eal.load_asset(dest) if eal.does_asset_exist(dest) else None
    if bp is None:
        bp = eal.duplicate_asset("/Game/Variant_Combat/Blueprints/BP_CombatCharacter", dest)
    if bp is None:
        raise RuntimeError("could not create " + dest)
    unreal.BlueprintEditorLibrary.reparent_blueprint(bp, script("SoulsShadowBoss"))
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    cdo = unreal.get_default_object(bp.generated_class())

    montage = lambda n: eal.load_asset("%s/Montages/%s" % (SOULS, n))
    cdo.set_editor_property("SwordComboMontages", [m for m in (montage(n) for n in ("AM_SwordA", "AM_SwordB", "AM_SwordC")) if m])
    for prop, name in (("HeavyAttackMontage", "AM_SwordHeavy"), ("RollMontage", "AM_Roll"), ("BlockMontage", "AM_Block"),
                       ("HealMontage", "AM_Heal"), ("HitMontage", "AM_Hit"), ("ParryMontage", "AM_Parry"),
                       ("StaggerMontage", "AM_Stagger"), ("GuardBreakMontage", "AM_GuardBreak"), ("CriticalMontage", "AM_Critical"),
                       ("SwimMontage", "AM_Swim"), ("SwimIdleMontage", "AM_SwimIdle"), ("ThrowMontage", "AM_Throw")):
        m = montage(name) if eal.does_asset_exist("%s/Montages/%s" % (SOULS, name)) else None
        if m is None:
            log("no montage %s (skipped)" % name)
            continue
        cdo.set_editor_property(prop, m)
    cdo.get_editor_property("mesh").set_editor_property("skeletal_mesh_asset", eal.load_asset(MANNY_MESH))
    sword = eal.load_asset(SOULS + "/Weapons/sword/KnightSword")
    shield = eal.load_asset(SOULS + "/Weapons/shield/KiteShield")
    if sword:
        place(cdo.get_editor_property("Sword"), sword, SWORD_TRANSFORM)
    if shield:
        place(cdo.get_editor_property("Shield"), shield, SHIELD_TRANSFORM)
    cdo.set_editor_property("BodyMaterials", night)
    cdo.set_editor_property("PhaseTwoMaterials", abyss)
    for prop, role in (("SwingSounds", "sword_swing"), ("HitSounds", "sword_hit_flesh"), ("BlockSounds", "shield_block"),
                       ("HurtSounds", "armor_hurt"), ("FootstepSounds", "footstep_grass")):
        cdo.set_editor_property(prop, sounds_of(role))
    for prop, stem in (("ParrySound", "parry_01"), ("CriticalSound", "riposte_01"), ("RollSound", "roll_01"),
                       ("DrinkSound", "flask_drink_01"), ("DeathSound", "death_01"),
                       ("MeltSound", "roll_01"), ("EruptSound", "riposte_01")):
        s = sound(stem)
        if s is not None:
            cdo.set_editor_property(prop, s)
    cdo.set_editor_property("ShadeOverlayMaterial", overlay)
    cdo.set_editor_property("PoolMaterial", pool)
    cdo.set_editor_property("WispMaterial", wisp)
    cdo.set_editor_property("ai_controller_class", script("SoulsEnemyController"))
    cdo.set_editor_property("auto_possess_ai", unreal.AutoPossessAI.PLACED_IN_WORLD_OR_SPAWNED)
    cdo.set_editor_property("tags", ["Enemy", "Boss"])
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    eal.save_loaded_asset(bp)
    log("BP_SoulsShadowBoss ready")
    return bp


def hook_game_mode(bp):
    gm = eal.load_asset(SOULS + "/BP_SoulsGameMode")
    if gm is None:
        warn("no BP_SoulsGameMode (run build_souls_character.py first)")
        return
    cdo = unreal.get_default_object(gm.generated_class())
    cdo.set_editor_property("ShadowBossClass", bp.generated_class())
    unreal.BlueprintEditorLibrary.compile_blueprint(gm)
    eal.save_loaded_asset(gm)
    log("BP_SoulsGameMode.ShadowBossClass = BP_SoulsShadowBoss")


def main():
    pool = build_pool_material()
    overlay = build_overlay_material()
    wisp = build_wisp_material()
    night = armour_instances("MI_ShadeNight", (0.012, 0.011, 0.02), 0.9, 0.3, (0.5, 0.18, 1.0), 16.0)
    abyss = armour_instances("MI_ShadeAbyss", (0.006, 0.006, 0.014), 0.95, 0.24, (0.22, 0.4, 1.0), 48.0)
    bp = build_blueprint(pool, overlay, wisp, night, abyss)
    hook_game_mode(bp)
    eal.save_directory(SHADE, only_if_is_dirty=True, recursive=True)
    log("problems: %d" % len(problems))
    for p in problems:
        log("  - " + p)
    log("SHADOW_BOSS_OK")


try:
    main()
except Exception as exc:
    import traceback
    log("FAILED: %s\n%s" % (exc, traceback.format_exc()))
