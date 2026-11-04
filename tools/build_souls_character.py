"""Editor-Python (UE 5.8): import the CC0 Quaternius Universal Animation Library clips, retarget
them onto Epic's Manny, and build the sword knight's montages under /Game/Souls.

Run (after tools/install_thirdperson_template.py):
  UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<this file> -unattended -nop4 -nosplash
Writes Saved/souls_character.log (the commandlet swallows print()).
"""
import os
import unreal

PROJECT = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
LOG = open(os.path.join(PROJECT, "Saved", "souls_character.log"), "w", encoding="utf-8")
eal = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
problems = []

SOULS = "/Game/Souls"
MANNY_MESH = "/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple"
UAL1_FBX = os.path.join(PROJECT, "SourceArt", "Animations", "UAL", "UAL1", "AL_Standard.fbx")
UAL2_GLB = os.path.join(PROJECT, "SourceArt", "Animations", "UAL", "UAL2", "UAL2_Standard.glb")

# Clip (as named in the source file) -> name of the retargeted Manny animation.
UAL1_CLIPS = {"Roll": "A_Roll", "Sword_Idle": "A_SwordIdle", "Hit_Chest": "A_HitChest", "Death01": "A_Death",
              "Sword_Attack": "A_SwordAttack", "Swim_Fwd_Loop": "A_SwimFwd", "Swim_Idle_Loop": "A_SwimIdle",
              "Spell_Simple_Shoot": "A_Throw", "Crouch_Idle_Loop": "A_CrouchIdle", "Crouch_Fwd_Loop": "A_CrouchFwd",
              "Idle_Talking_Loop": "A_Talk"}
UAL2_CLIPS = {"Sword_Regular_A": "A_SwordA", "Sword_Regular_B": "A_SwordB", "Sword_Regular_C": "A_SwordC",
              "Sword_Block": "A_Block", "Consume": "A_Consume", "Idle_Shield_Break": "A_GuardBreak",
              "Shield_OneShot": "A_Parry", "Hit_Knockback": "A_Knockback", "LayToIdle": "A_LayToIdle",
              "Yes": "A_Yes", "Idle_No_Loop": "A_No"}


def log(msg):
    LOG.write(msg + "\n")
    LOG.flush()


def warn(msg):
    problems.append(msg)
    log("WARNING " + msg)


def ensure_dir(path):
    if not eal.does_directory_exist(path):
        eal.make_directory(path)


def assets_of(folder, cls):
    out = []
    for p in eal.list_assets(folder, recursive=True, include_folder=False):
        a = eal.load_asset(p)
        if isinstance(a, cls):
            out.append(a)
    return out


def import_file(filename, dest, options=None):
    if eal.does_directory_exist(dest):
        eal.delete_directory(dest)
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", filename)
    task.set_editor_property("destination_path", dest)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("automated", True)
    task.set_editor_property("save", True)
    if options is not None:
        task.set_editor_property("options", options)
    tools.import_asset_tasks([task])
    log("imported %s -> %d objects" % (os.path.basename(filename), len(task.get_editor_property("imported_object_paths") or [])))


def ual1_options():
    o = unreal.FbxImportUI()
    o.set_editor_property("automated_import_should_detect_type", False)
    o.set_editor_property("import_mesh", True)
    o.set_editor_property("import_as_skeletal", True)
    o.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_SKELETAL_MESH)
    o.set_editor_property("import_animations", True)
    o.set_editor_property("import_materials", False)
    o.set_editor_property("import_textures", False)
    o.set_editor_property("create_physics_asset", False)
    sk = o.get_editor_property("skeletal_mesh_import_data")
    sk.set_editor_property("use_t0_as_ref_pose", False)
    an = o.get_editor_property("anim_sequence_import_data")
    an.set_editor_property("animation_length", unreal.FBXAnimationLengthImportType.FBXALIT_EXPORTED_TIME)
    an.set_editor_property("snap_to_closest_frame_boundary", True)
    return o


def find_clip(anims, clip):
    by_name = {a.get_name(): a for a in anims}
    for candidate in (clip, "Anim_" + clip):
        if candidate in by_name:
            return by_name[candidate]
    ends = sorted((n for n in by_name if n.endswith("_" + clip) or n.endswith(clip)), key=len)
    return by_name[ends[0]] if ends else None


def make_ik_rig(name, mesh):
    path = "%s/Rigs/%s" % (SOULS, name)
    rig = eal.load_asset(path) if eal.does_asset_exist(path) else         tools.create_asset(name, SOULS + "/Rigs", unreal.IKRigDefinition, unreal.IKRigDefinitionFactory())
    c = unreal.IKRigController.get_controller(rig)
    c.set_skeletal_mesh(mesh)
    ok = c.apply_auto_generated_retarget_definition()
    log("ik rig %s: auto retarget definition %s, %d chains" % (name, ok, len(c.get_retarget_chains())))
    eal.save_loaded_asset(rig)
    return rig


def make_retargeter(name, source_rig, target_rig):
    path = "%s/Rigs/%s" % (SOULS, name)
    rtg = eal.load_asset(path) if eal.does_asset_exist(path) else         tools.create_asset(name, SOULS + "/Rigs", unreal.IKRetargeter, unreal.IKRetargetFactory())
    c = unreal.IKRetargeterController.get_controller(rtg)
    try:
        c.remove_all_ops()
    except Exception:
        pass
    c.set_ik_rig(unreal.RetargetSourceOrTarget.SOURCE, source_rig)
    c.set_ik_rig(unreal.RetargetSourceOrTarget.TARGET, target_rig)
    try:
        c.add_default_ops()
    except Exception as exc:
        log("add_default_ops: %s" % exc)
    try:
        c.assign_ik_rig_to_all_ops(unreal.RetargetSourceOrTarget.TARGET, target_rig)
    except Exception as exc:
        log("assign_ik_rig_to_all_ops: %s" % exc)
    c.auto_map_chains(unreal.AutoMapChainType.FUZZY, True)
    try:
        c.auto_align_all_bones(unreal.RetargetSourceOrTarget.TARGET)
    except Exception as exc:
        log("auto_align_all_bones: %s" % exc)
    eal.save_loaded_asset(rtg)
    log("retargeter %s ready" % name)
    return rtg


def retarget(clips, anims, source_mesh, target_mesh, rtg):
    picked = {}
    for clip, out_name in clips.items():
        anim = find_clip(anims, clip)
        if anim is None:
            warn("clip %s not found (have e.g. %s)" % (clip, sorted(a.get_name() for a in anims)[:8]))
            continue
        picked[anim.get_name()] = out_name
    data = [unreal.AssetRegistryHelpers.get_asset_registry().get_asset_by_object_path(eal.get_path_name_for_loaded_asset(eal.load_asset(a.get_path_name())))
            for a in anims if a.get_name() in picked]
    log("duplicate_and_retarget doc: %s" % unreal.IKRetargetBatchOperation.duplicate_and_retarget.__doc__)
    new_paths = unreal.IKRetargetBatchOperation.duplicate_and_retarget(
        assets_to_retarget=data, source_mesh=source_mesh, target_mesh=target_mesh, ik_retarget_asset=rtg,
        search="", replace="", prefix="RTG_", suffix="", target_path=SOULS + "/Anims")
    results = {}
    for p in new_paths or []:
        asset = eal.load_asset(str(p.package_name) if hasattr(p, "package_name") else str(p))
        if not isinstance(asset, unreal.AnimSequence):
            continue
        source_name = asset.get_name()[len("RTG_"):]
        out_name = picked.get(source_name)
        if not out_name:
            continue
        dest = "%s/Anims/%s" % (SOULS, out_name)
        if eal.does_asset_exist(dest):
            eal.delete_asset(dest)
        eal.rename_asset(asset.get_path_name(), dest)
        moved = eal.load_asset(dest)
        moved.set_editor_property("bone_compression_settings", eal.load_asset("/Engine/Animation/DefaultAnimBoneCompressionSettings"))
        eal.save_loaded_asset(moved)
        results[out_name] = moved
    log("retargeted %d/%d clips: %s" % (len(results), len(clips), sorted(results)))
    return results


def dump_blueprint_defaults():
    bp = eal.load_asset("/Game/Variant_Combat/Blueprints/BP_CombatCharacter")
    cdo = unreal.get_default_object(bp.generated_class())
    for prop in ["ComboAttackMontage", "ComboSectionNames", "ChargedAttackMontage", "ChargeLoopSection",
                 "ChargeAttackSection", "JumpAction", "MoveAction", "LookAction", "MouseLookAction",
                 "ComboAttackAction", "ChargedAttackAction", "ToggleCameraAction", "PelvisBoneName",
                 "LifeBarColor", "MaxHP", "MeleeDamage"]:
        try:
            value = cdo.get_editor_property(prop)
            log("BP %s = %s" % (prop, value.get_path_name() if hasattr(value, "get_path_name") else value))
        except Exception as exc:
            log("BP %s: %s" % (prop, exc))
    mesh = cdo.get_editor_property("mesh")
    log("BP mesh = %s anim = %s" % (mesh.get_editor_property("skeletal_mesh_asset").get_path_name() if mesh.get_editor_property("skeletal_mesh_asset") else None,
                                    mesh.get_editor_property("anim_class").get_path_name() if mesh.get_editor_property("anim_class") else None))
    montage = eal.load_asset("/Game/Variant_Combat/Anims/AM_ComboAttack")
    tracks = montage.get_editor_property("slot_anim_tracks")
    log("AM_ComboAttack slot = %s" % (tracks[0].get_editor_property("slot_name") if tracks else None))
    for anim in ["/Game/Characters/Mannequins/Anims/Unarmed/Attack/MM_Attack_01"]:
        a = eal.load_asset(anim)
        log("%s notifies: %s" % (anim, [(e.get_editor_property("notify").get_class().get_name() if e.get_editor_property("notify") else None)
                                       for e in unreal.AnimationLibrary.get_animation_notify_events(a)]))
    for e in unreal.AnimationLibrary.get_animation_notify_events(montage):
        n = e.get_editor_property("notify")
        log("AM_ComboAttack notify %s at %.3f" % (n.get_class().get_path_name() if n else None, e.get_editor_property("trigger_time_offset") if False else unreal.AnimationLibrary.get_anim_notify_event_trigger_time(e)))


WEAPONS_SRC = os.path.join(PROJECT, "SourceArt", "Weapons")
# (animation, montage) pairs; one animation can back several montages.
MONTAGES = [("A_SwordA", "AM_SwordA"), ("A_SwordB", "AM_SwordB"), ("A_SwordC", "AM_SwordC"),
            ("A_SwordAttack", "AM_SwordHeavy"), ("A_SwordAttack", "AM_Critical"), ("A_Roll", "AM_Roll"),
            ("A_Block", "AM_Block"), ("A_Consume", "AM_Heal"), ("A_HitChest", "AM_Hit"),
            ("A_Parry", "AM_Parry"), ("A_Knockback", "AM_Stagger"), ("A_GuardBreak", "AM_GuardBreak"),
            ("A_SwimFwd", "AM_Swim"), ("A_SwimIdle", "AM_SwimIdle"), ("A_Throw", "AM_Throw"),
            # Stealth: crouch loops, the swim stroke doubling as an army crawl, lying down / getting up.
            ("A_CrouchIdle", "AM_CrouchIdle"), ("A_CrouchFwd", "AM_CrouchFwd"), ("A_SwimFwd", "AM_Crawl"),
            ("A_LayToIdle", "AM_GetUp"),
            # Patrollers' idle talk.
            ("A_Talk", "AM_Talk"), ("A_Yes", "AM_Yes"), ("A_No", "AM_No")]


def import_texture(path, dest, normal=False):
    import_file_keep(path, dest)
    tex = eal.load_asset("%s/%s" % (dest, os.path.splitext(os.path.basename(path))[0]))
    if tex and normal:
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
        tex.set_editor_property("srgb", False)
        eal.save_loaded_asset(tex)
    return tex


def import_file_keep(filename, dest):
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", filename)
    task.set_editor_property("destination_path", dest)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("automated", True)
    task.set_editor_property("save", True)
    tools.import_asset_tasks([task])
    return list(task.get_editor_property("imported_object_paths") or [])


def weapon_material(name, base, normal, metallic, roughness):
    mel = unreal.MaterialEditingLibrary
    path = "%s/Weapons/%s" % (SOULS, name)
    mat = eal.load_asset(path) if eal.does_asset_exist(path) else \
        tools.create_asset(name, SOULS + "/Weapons", unreal.Material, unreal.MaterialFactoryNew())
    mel.delete_all_material_expressions(mat)
    tex = mel.create_material_expression(mat, unreal.MaterialExpressionTextureSample, -400, 0)
    tex.set_editor_property("texture", base)
    mel.connect_material_property(tex, "RGB", unreal.MaterialProperty.MP_BASE_COLOR)
    if normal:
        nrm = mel.create_material_expression(mat, unreal.MaterialExpressionTextureSample, -400, 300)
        nrm.set_editor_property("texture", normal)
        nrm.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
        mel.connect_material_property(nrm, "RGB", unreal.MaterialProperty.MP_NORMAL)
    for value, prop, y in ((metallic, unreal.MaterialProperty.MP_METALLIC, 150), (roughness, unreal.MaterialProperty.MP_ROUGHNESS, 220)):
        c = mel.create_material_expression(mat, unreal.MaterialExpressionConstant, -200, y)
        c.set_editor_property("r", value)
        mel.connect_material_property(c, "", prop)
    mel.recompile_material(mat)
    eal.save_loaded_asset(mat)
    return mat


def build_weapons():
    dest = SOULS + "/Weapons"
    ensure_dir(dest)
    weapons = {}
    for key, mesh_file, base_file, normal_file, metallic, roughness in (
            ("sword", os.path.join(WEAPONS_SRC, "Sword", "KnightSword.obj"), os.path.join(WEAPONS_SRC, "Sword", "KnightSword_BaseColor.png"), None, 0.8, 0.3),
            ("shield", os.path.join(WEAPONS_SRC, "Shield", "KiteShield.fbx"), os.path.join(WEAPONS_SRC, "Shield", "Textures_Steel", "KiteShield_BaseColor.png"),
             os.path.join(WEAPONS_SRC, "Shield", "Textures_Steel", "KiteShield_NRM.png"), 0.7, 0.45)):
        paths = import_file_keep(mesh_file, dest + "/" + key)
        mesh = next((eal.load_asset(p) for p in paths if isinstance(eal.load_asset(p), unreal.StaticMesh)), None)
        if mesh is None:
            mesh = (assets_of(dest + "/" + key, unreal.StaticMesh) or [None])[0]
        if mesh is None:
            warn("no static mesh from " + mesh_file)
            continue
        base = import_texture(base_file, dest + "/" + key)
        normal = import_texture(normal_file, dest + "/" + key, normal=True) if normal_file else None
        mat = weapon_material("M_" + key.capitalize(), base, normal, metallic, roughness)
        for i in range(len(mesh.get_editor_property("static_materials"))):
            mesh.set_material(i, mat)
        eal.save_loaded_asset(mesh)
        box = mesh.get_bounding_box()
        log("weapon %s: %s bounds min=%s max=%s" % (key, mesh.get_path_name(), box.min, box.max))
        weapons[key] = mesh
    return weapons


def build_montages():
    dest = SOULS + "/Montages"
    ensure_dir(dest)
    out = {}
    for anim_name, montage_name in MONTAGES:
        anim = eal.load_asset("%s/Anims/%s" % (SOULS, anim_name))
        if anim is None:
            warn("no anim " + anim_name)
            continue
        path = "%s/%s" % (dest, montage_name)
        if eal.does_asset_exist(path):
            eal.delete_asset(path)
        factory = unreal.AnimMontageFactory()
        factory.set_editor_property("source_animation", anim)
        montage = tools.create_asset(montage_name, dest, unreal.AnimMontage, factory)
        eal.save_loaded_asset(montage)
        out[montage_name] = montage
        log("montage %s (%.2fs)" % (montage_name, montage.get_play_length()))
    return out


def duplicate_blueprint(source, dest_name, parent_class=None):
    dest = "%s/%s" % (SOULS, dest_name)
    if eal.does_asset_exist(dest):
        eal.delete_asset(dest)
    bp = eal.duplicate_asset(source, dest)
    if parent_class is not None:
        unreal.BlueprintEditorLibrary.reparent_blueprint(bp, parent_class)
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    return bp


# Fitted to SKM_Manny hand_r by eye: blade along the hand's +Z (forward of the fist), grip (mesh z=-0.75) in the fist.
SWORD_TRANSFORM = {"location": (0.0, 0.0, 41.0), "rotation": (0.0, 0.0, 0.0), "scale": 55.0}
SHIELD_TRANSFORM = {"location": (0.0, 0.0, 0.0), "rotation": (0.0, 0.0, 0.0), "scale": 1.25}


def place(component, mesh, spec):
    component.set_editor_property("static_mesh", mesh)
    component.set_editor_property("relative_location", unreal.Vector(*spec["location"]))
    component.set_editor_property("relative_rotation", unreal.Rotator(*spec["rotation"]))
    component.set_editor_property("relative_scale3d", unreal.Vector(spec["scale"], spec["scale"], spec["scale"]))


MEL = unreal.MaterialEditingLibrary
AUDIO_SRC = os.path.join(PROJECT, "SourceArt", "Audio")
FONT_SRC = os.path.join(PROJECT, "SourceArt", "Fonts", "Cinzel.ttf")


def new_material(folder, name):
    ensure_dir(folder)
    path = "%s/%s" % (folder, name)
    mat = eal.load_asset(path) if eal.does_asset_exist(path) else \
        tools.create_asset(name, folder, unreal.Material, unreal.MaterialFactoryNew())
    MEL.delete_all_material_expressions(mat)
    return mat


def const3(mat, rgb, x, y):
    c = MEL.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, x, y)
    c.set_editor_property("constant", unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0))
    return c


def const1(mat, value, x, y):
    c = MEL.create_material_expression(mat, unreal.MaterialExpressionConstant, x, y)
    c.set_editor_property("r", value)
    return c


def finish(mat):
    MEL.recompile_material(mat)
    eal.save_loaded_asset(mat)
    return mat


MANNY_SLOTS = ("/Game/Characters/Mannequins/Materials/Manny/MI_Manny_01_New",
               "/Game/Characters/Mannequins/Materials/Manny/MI_Manny_02_New")


def armour_instances(name, tint, metallic, roughness, logo, glow):
    """Per-slot instances of Epic's mannequin material (keeps its panel normals and wear), tinted
    as faction armour with a glowing chest sigil."""
    folder = SOULS + "/Materials"
    ensure_dir(folder)
    out = []
    for i, parent_path in enumerate(MANNY_SLOTS):
        mi_name = "%s_%d" % (name, i)
        path = "%s/%s" % (folder, mi_name)
        mi = eal.load_asset(path) if eal.does_asset_exist(path) else             tools.create_asset(mi_name, folder, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
        mi.set_editor_property("parent", eal.load_asset(parent_path))
        MEL.set_material_instance_vector_parameter_value(mi, "Paint Tint", unreal.LinearColor(tint[0], tint[1], tint[2], 1.0))
        MEL.set_material_instance_scalar_parameter_value(mi, "MetalPaintMetallic", metallic)
        MEL.set_material_instance_scalar_parameter_value(mi, "MetalPaintRoughness", roughness)
        MEL.set_material_instance_vector_parameter_value(mi, "LogoTint", unreal.LinearColor(logo[0], logo[1], logo[2], 1.0))
        MEL.set_material_instance_scalar_parameter_value(mi, "EmissivePower", glow if i == 1 else 0.0)
        MEL.update_material_instance(mi)
        eal.save_loaded_asset(mi)
        out.append(mi)
    return out


def build_armour_materials():
    out = {
        "player": armour_instances("MI_KnightSteel", (0.62, 0.6, 0.56), 1.0, 0.28, (1.0, 0.7, 0.25), 3.0),
        "enemy": armour_instances("MI_HollowIron", (0.035, 0.032, 0.03), 0.7, 0.5, (1.0, 0.07, 0.02), 9.0),
        "boss": armour_instances("MI_WardenGold", (0.78, 0.55, 0.2), 1.0, 0.22, (1.0, 0.62, 0.15), 10.0),
        "boss2": armour_instances("MI_WardenEmber", (0.45, 0.2, 0.06), 1.0, 0.3, (1.0, 0.22, 0.02), 40.0),
    }
    log("armour materials: %s" % sorted(out))
    return out


def build_fx_materials():
    folder = SOULS + "/FX"
    glow = new_material(folder, "M_GraceGlow")
    glow.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    MEL.connect_material_property(const3(glow, (12.0, 7.5, 2.4), -400, 0), "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    finish(glow)

    beam = new_material(folder, "M_GraceBeam")
    beam.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    beam.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
    beam.set_editor_property("two_sided", True)
    # Brightest along the axis, fading out at the silhouette and toward the top.
    fres = MEL.create_material_expression(beam, unreal.MaterialExpressionFresnel, -1000, 0)
    fres.set_editor_property("exponent", 1.5)
    inv = MEL.create_material_expression(beam, unreal.MaterialExpressionOneMinus, -800, 0)
    MEL.connect_material_expressions(fres, "", inv, "")
    uv = MEL.create_material_expression(beam, unreal.MaterialExpressionTextureCoordinate, -1000, 200)
    mask = MEL.create_material_expression(beam, unreal.MaterialExpressionComponentMask, -800, 200)
    mask.set_editor_property("r", False)
    mask.set_editor_property("g", True)
    MEL.connect_material_expressions(uv, "", mask, "")
    power = MEL.create_material_expression(beam, unreal.MaterialExpressionPower, -600, 200)
    MEL.connect_material_expressions(mask, "", power, "Base")
    MEL.connect_material_expressions(const1(beam, 2.0, -800, 320), "", power, "Exp")
    mul = MEL.create_material_expression(beam, unreal.MaterialExpressionMultiply, -400, 0)
    MEL.connect_material_expressions(inv, "", mul, "A")
    MEL.connect_material_expressions(power, "", mul, "B")
    mul2 = MEL.create_material_expression(beam, unreal.MaterialExpressionMultiply, -200, 0)
    MEL.connect_material_expressions(mul, "", mul2, "A")
    MEL.connect_material_expressions(const3(beam, (1.6, 1.0, 0.35), -400, 200), "", mul2, "B")
    MEL.connect_material_property(mul2, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    finish(beam)
    hook = new_material(folder, "M_GrappleGlow")
    hook.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    MEL.connect_material_property(const3(hook, (1.2, 4.5, 3.6), -400, 0), "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    finish(hook)

    line = new_material(folder, "M_GrappleLine")
    MEL.connect_material_property(const3(line, (0.06, 0.05, 0.04), -400, 0), "", unreal.MaterialProperty.MP_BASE_COLOR)
    MEL.connect_material_property(const1(line, 0.9, -400, 150), "", unreal.MaterialProperty.MP_METALLIC)
    MEL.connect_material_property(const1(line, 0.45, -400, 250), "", unreal.MaterialProperty.MP_ROUGHNESS)
    finish(line)
    mote = new_material(folder, "M_PickupGlow")
    mote.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    MEL.connect_material_property(const3(mote, (9.0, 8.0, 6.0), -400, 0), "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    finish(mote)
    log("fx materials: M_GraceGlow, M_GraceBeam, M_GrappleGlow, M_GrappleLine, M_PickupGlow")


def import_audio():
    """SourceArt/Audio/*.wav -> /Game/Souls/Audio, grouped by role (name minus the _NN suffix)."""
    dest = SOULS + "/Audio"
    ensure_dir(dest)
    sounds = {}
    if not os.path.isdir(AUDIO_SRC):
        warn("no SourceArt/Audio yet; the game runs silent")
        return sounds
    att_path = dest + "/ATT_World"
    att = eal.load_asset(att_path) if eal.does_asset_exist(att_path) else \
        tools.create_asset("ATT_World", dest, unreal.SoundAttenuation, unreal.SoundAttenuationFactory())
    settings = att.get_editor_property("attenuation")
    settings.set_editor_property("falloff_distance", 3200.0)
    settings.set_editor_property("attenuation_shape_extents", unreal.Vector(350.0, 0.0, 0.0))
    att.set_editor_property("attenuation", settings)
    eal.save_loaded_asset(att)
    for name in sorted(os.listdir(AUDIO_SRC)):
        stem, ext = os.path.splitext(name)
        if ext.lower() != ".wav":
            continue
        asset_path = "%s/%s" % (dest, stem)
        if not eal.does_asset_exist(asset_path):
            import_file_keep(os.path.join(AUDIO_SRC, name), dest)
        wave = eal.load_asset(asset_path)
        if wave is None:
            warn("audio import failed: " + name)
            continue
        looping = stem.endswith("_loop")
        wave.set_editor_property("looping", looping)
        if not looping and not stem.startswith(("ui_", "grace_", "enemy_felled", "boss_roar", "music_")):
            wave.set_editor_property("attenuation_settings", att)
        eal.save_loaded_asset(wave)
        role = stem
        parts = stem.rsplit("_", 1)
        if len(parts) == 2 and parts[1].isdigit():
            role = parts[0]
        sounds.setdefault(role, []).append(wave)
    log("audio roles: %s" % ", ".join("%s(%d)" % (k, len(v)) for k, v in sorted(sounds.items())))
    return sounds


def stage_title_font():
    """The HUD builds its serif font at runtime from Content/Souls/Fonts/Cinzel.ttf (importing a
    .ttf needs Slate, which the commandlet doesn't run); DefaultGame.ini stages that folder."""
    import shutil
    dest_dir = os.path.join(PROJECT, "Content", "Souls", "Fonts")
    os.makedirs(dest_dir, exist_ok=True)
    shutil.copyfile(FONT_SRC, os.path.join(dest_dir, "Cinzel.ttf"))
    log("title font staged")


def first(sounds, role):
    waves = sounds.get(role) or []
    return waves[0] if waves else None


def configure_knight(bp, montages, weapons, sounds, body_material=None):
    cdo = unreal.get_default_object(bp.generated_class())
    cdo.set_editor_property("SwordComboMontages", [montages[n] for n in ("AM_SwordA", "AM_SwordB", "AM_SwordC") if n in montages])
    for prop, name in (("HeavyAttackMontage", "AM_SwordHeavy"), ("RollMontage", "AM_Roll"), ("BlockMontage", "AM_Block"),
                       ("HealMontage", "AM_Heal"), ("HitMontage", "AM_Hit"), ("ParryMontage", "AM_Parry"),
                       ("StaggerMontage", "AM_Stagger"), ("GuardBreakMontage", "AM_GuardBreak"), ("CriticalMontage", "AM_Critical"),
                       ("SwimMontage", "AM_Swim"), ("SwimIdleMontage", "AM_SwimIdle"), ("ThrowMontage", "AM_Throw"),
                       ("CrouchIdleMontage", "AM_CrouchIdle"), ("CrouchMoveMontage", "AM_CrouchFwd"), ("CrawlMontage", "AM_Crawl"),
                       ("GetUpMontage", "AM_GetUp"), ("TalkMontage", "AM_Talk")):
        if name in montages:
            cdo.set_editor_property(prop, montages[name])
    cdo.set_editor_property("GestureMontages", [montages[n] for n in ("AM_Yes", "AM_No") if n in montages])
    cdo.get_editor_property("mesh").set_editor_property("skeletal_mesh_asset", eal.load_asset(MANNY_MESH))
    if "sword" in weapons:
        place(cdo.get_editor_property("Sword"), weapons["sword"], SWORD_TRANSFORM)
    if "shield" in weapons:
        place(cdo.get_editor_property("Shield"), weapons["shield"], SHIELD_TRANSFORM)
    if body_material:
        cdo.set_editor_property("BodyMaterials", body_material)
    for prop, role in (("SwingSounds", "sword_swing"), ("HitSounds", "sword_hit_flesh"), ("BlockSounds", "shield_block"),
                       ("HurtSounds", "armor_hurt"), ("FootstepSounds", "footstep_grass")):
        cdo.set_editor_property(prop, sounds.get(role, []))
    for prop, role in (("ParrySound", "parry"), ("CriticalSound", "riposte"), ("RollSound", "roll"),
                       ("DrinkSound", "flask_drink"), ("DeathSound", "death")):
        wave = first(sounds, role)
        if wave is not None:
            cdo.set_editor_property(prop, wave)
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    eal.save_loaded_asset(bp)
    return cdo


def build_blueprints(montages, weapons, armour, sounds):
    script = lambda name: unreal.load_class(None, "/Script/UnrealSoulslike." + name)
    template = "/Game/Variant_Combat/Blueprints/BP_CombatCharacter"

    knight = duplicate_blueprint(template, "BP_SoulsKnight", script("SoulsKnight"))
    configure_knight(knight, montages, weapons, sounds, armour.get("player"))
    log("BP_SoulsKnight ready")

    enemy = duplicate_blueprint(template, "BP_SoulsEnemy", script("SoulsEnemy"))
    cdo = configure_knight(enemy, montages, weapons, sounds, armour.get("enemy"))
    cdo.set_editor_property("ai_controller_class", script("SoulsEnemyController"))
    cdo.set_editor_property("auto_possess_ai", unreal.AutoPossessAI.PLACED_IN_WORLD_OR_SPAWNED)
    cdo.set_editor_property("tags", ["Enemy"])
    unreal.BlueprintEditorLibrary.compile_blueprint(enemy)
    eal.save_loaded_asset(enemy)
    log("BP_SoulsEnemy ready")

    boss = duplicate_blueprint(template, "BP_SoulsBoss", script("SoulsBoss"))
    cdo = configure_knight(boss, montages, weapons, sounds, armour.get("boss"))
    cdo.set_editor_property("PhaseTwoMaterials", armour.get("boss2"))
    cdo.set_editor_property("ai_controller_class", script("SoulsEnemyController"))
    cdo.set_editor_property("auto_possess_ai", unreal.AutoPossessAI.PLACED_IN_WORLD_OR_SPAWNED)
    cdo.set_editor_property("tags", ["Enemy", "Boss"])
    unreal.BlueprintEditorLibrary.compile_blueprint(boss)
    eal.save_loaded_asset(boss)
    log("BP_SoulsBoss ready")

    pc = duplicate_blueprint("/Game/Variant_Combat/Blueprints/BP_CombatPlayerController", "BP_SoulsPlayerController")
    pc_cdo = unreal.get_default_object(pc.generated_class())
    pc_cdo.set_editor_property("CharacterClass", knight.generated_class())
    unreal.BlueprintEditorLibrary.compile_blueprint(pc)
    eal.save_loaded_asset(pc)
    log("BP_SoulsPlayerController ready")

    gm = duplicate_blueprint("/Game/Variant_Combat/Blueprints/BP_CombatGameMode", "BP_SoulsGameMode", script("SoulsGameMode"))
    gm_cdo = unreal.get_default_object(gm.generated_class())
    gm_cdo.set_editor_property("default_pawn_class", knight.generated_class())
    gm_cdo.set_editor_property("player_controller_class", pc.generated_class())
    gm_cdo.set_editor_property("hud_class", script("SoulsHUD"))
    gm_cdo.set_editor_property("EnemyClass", enemy.generated_class())
    gm_cdo.set_editor_property("BossClass", boss.generated_class())
    for prop, role in (("GraceSound", "grace_rest"), ("FelledSound", "enemy_felled"), ("DiedSound", "death"),
                       ("BossMusic", "music_boss_loop"), ("AmbientLoop", "ambient_wind_sea_loop"), ("BossRoar", "boss_roar"),
                       ("PickupSound", "ui_confirm")):
        wave = first(sounds, role)
        if wave is not None:
            gm_cdo.set_editor_property(prop, wave)
    unreal.BlueprintEditorLibrary.compile_blueprint(gm)
    eal.save_loaded_asset(gm)
    log("BP_SoulsGameMode ready")
    use_souls_game_mode_in("/Game/Variant_Combat/Lvl_Combat", gm.generated_class())


def dump_input_mappings():
    for path in ("/Game/Input/IMC_Default", "/Game/Input/IMC_MouseLook", "/Game/Variant_Combat/Input/IMC_Combat"):
        imc = eal.load_asset(path)
        if imc is None:
            continue
        try:
            mappings = imc.get_editor_property("default_key_mappings").get_editor_property("mappings")
        except Exception:
            mappings = imc.get_editor_property("mappings")
        log("IMC %s: %s" % (path, ", ".join("%s=%s" % (m.get_editor_property("action").get_name() if m.get_editor_property("action") else None,
                                                        m.get_editor_property("key").get_editor_property("key_name")) for m in mappings)))


# Template bindings that collide with the knight's scheme (RMB block, RB/R heal).
TEMPLATE_UNBIND = [("/Game/Variant_Combat/Input/Actions/IA_ChargedAttack", "RightMouseButton"),
                   ("/Game/Variant_Combat/Input/Actions/IA_ComboAttack", "Gamepad_RightShoulder"),
                   ("/Game/Variant_Combat/Input/Actions/IA_ToggleCameraSide", "R"),
                   ("/Game/Variant_Combat/Input/Actions/IA_ChargedAttack", "Gamepad_RightTriggerAxis")]


def free_knight_keys():
    imc = eal.load_asset("/Game/Variant_Combat/Input/IMC_Combat")
    for action_path, key_name in TEMPLATE_UNBIND:
        key = unreal.Key()
        key.set_editor_property("key_name", key_name)
        imc.unmap_key(eal.load_asset(action_path), key)
    eal.save_loaded_asset(imc)
    log("freed template keys: %s" % ", ".join(k for _, k in TEMPLATE_UNBIND))


def use_souls_game_mode_in(level_path, game_mode_class):
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if not les.load_level(level_path):
        warn("could not load " + level_path)
        return
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    world.get_world_settings().set_editor_property("default_game_mode", game_mode_class)
    les.save_current_level()
    log("%s now uses %s" % (level_path, game_mode_class.get_name()))


def main():
    ensure_dir(SOULS)
    free_knight_keys()
    dump_input_mappings()
    try:
        dump_blueprint_defaults()
    except Exception as exc:
        warn("blueprint dump failed: %s" % exc)

    unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX true")
    if not eal.does_directory_exist(SOULS + "/UAL1"):
        import_file(UAL1_FBX, SOULS + "/UAL1")
    if not eal.does_directory_exist(SOULS + "/UAL2"):
        import_file(UAL2_GLB, SOULS + "/UAL2")

    manny = eal.load_asset(MANNY_MESH)
    ual1_mesh = (assets_of(SOULS + "/UAL1", unreal.SkeletalMesh) or [None])[0]
    ual2_mesh = (assets_of(SOULS + "/UAL2", unreal.SkeletalMesh) or [None])[0]
    log("meshes: manny=%s ual1=%s ual2=%s" % (bool(manny), ual1_mesh.get_name() if ual1_mesh else None, ual2_mesh.get_name() if ual2_mesh else None))
    if not (manny and ual1_mesh and ual2_mesh):
        raise RuntimeError("missing a skeletal mesh")

    ensure_dir(SOULS + "/Rigs")
    ensure_dir(SOULS + "/Anims")
    rig_manny = make_ik_rig("IK_Manny", manny)
    rig_ual1 = make_ik_rig("IK_UAL1", ual1_mesh)
    rig_ual2 = make_ik_rig("IK_UAL2", ual2_mesh)
    rtg1 = make_retargeter("RTG_UAL1_To_Manny", rig_ual1, rig_manny)
    rtg2 = make_retargeter("RTG_UAL2_To_Manny", rig_ual2, rig_manny)

    anims1 = assets_of(SOULS + "/UAL1", unreal.AnimSequence)
    anims2 = assets_of(SOULS + "/UAL2", unreal.AnimSequence)
    log("source clips: ual1=%d ual2=%d" % (len(anims1), len(anims2)))
    retarget(UAL1_CLIPS, anims1, ual1_mesh, manny, rtg1)
    retarget(UAL2_CLIPS, anims2, ual2_mesh, manny, rtg2)

    montages = build_montages()
    weapons = build_weapons()
    build_fx_materials()
    armour = build_armour_materials()
    sounds = import_audio()
    stage_title_font()
    build_blueprints(montages, weapons, armour, sounds)

    eal.save_directory(SOULS, only_if_is_dirty=True, recursive=True)
    log("problems: %d" % len(problems))
    for p in problems:
        log("  - " + p)
    log("SOULS_CHARACTER_OK")


try:
    main()
except Exception as exc:
    import traceback
    log("FAILED: %s\n%s" % (exc, traceback.format_exc()))
