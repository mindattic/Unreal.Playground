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
              "Sword_Attack": "A_SwordAttack"}
UAL2_CLIPS = {"Sword_Regular_A": "A_SwordA", "Sword_Regular_B": "A_SwordB", "Sword_Regular_C": "A_SwordC",
              "Sword_Block": "A_Block", "Consume": "A_Consume", "Idle_Shield_Break": "A_GuardBreak"}


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
MONTAGES = {"A_SwordA": "AM_SwordA", "A_SwordB": "AM_SwordB", "A_SwordC": "AM_SwordC",
            "A_SwordAttack": "AM_SwordHeavy", "A_Roll": "AM_Roll", "A_Block": "AM_Block",
            "A_Consume": "AM_Heal", "A_HitChest": "AM_Hit"}


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
    for anim_name, montage_name in MONTAGES.items():
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


# Fitted to SKM_Manny hand_r by eye: blade along the hand's -Z, grip (mesh z=-0.75) in the fist.
SWORD_TRANSFORM = {"location": (0.0, 0.0, -41.0), "rotation": (180.0, 0.0, 0.0), "scale": 55.0}
SHIELD_TRANSFORM = {"location": (0.0, 0.0, 0.0), "rotation": (0.0, 0.0, 0.0), "scale": 1.25}


def place(component, mesh, spec):
    component.set_editor_property("static_mesh", mesh)
    component.set_editor_property("relative_location", unreal.Vector(*spec["location"]))
    component.set_editor_property("relative_rotation", unreal.Rotator(*spec["rotation"]))
    component.set_editor_property("relative_scale3d", unreal.Vector(spec["scale"], spec["scale"], spec["scale"]))


def build_blueprints(montages, weapons):
    knight_class = unreal.load_class(None, "/Script/UnrealSoulslike.SoulsKnight")
    bp = duplicate_blueprint("/Game/Variant_Combat/Blueprints/BP_CombatCharacter", "BP_SoulsKnight", knight_class)
    cdo = unreal.get_default_object(bp.generated_class())
    cdo.set_editor_property("SwordComboMontages", [montages[n] for n in ("AM_SwordA", "AM_SwordB", "AM_SwordC") if n in montages])
    for prop, name in (("HeavyAttackMontage", "AM_SwordHeavy"), ("RollMontage", "AM_Roll"), ("BlockMontage", "AM_Block"),
                       ("HealMontage", "AM_Heal"), ("HitMontage", "AM_Hit")):
        if name in montages:
            cdo.set_editor_property(prop, montages[name])
    cdo.get_editor_property("mesh").set_editor_property("skeletal_mesh_asset", eal.load_asset(MANNY_MESH))
    if "sword" in weapons:
        place(cdo.get_editor_property("Sword"), weapons["sword"], SWORD_TRANSFORM)
    if "shield" in weapons:
        place(cdo.get_editor_property("Shield"), weapons["shield"], SHIELD_TRANSFORM)
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    eal.save_loaded_asset(bp)
    log("BP_SoulsKnight ready (parent %s)" % knight_class.get_name())

    gm = duplicate_blueprint("/Game/Variant_Combat/Blueprints/BP_CombatGameMode", "BP_SoulsGameMode")
    gm_cdo = unreal.get_default_object(gm.generated_class())
    gm_cdo.set_editor_property("default_pawn_class", bp.generated_class())
    unreal.BlueprintEditorLibrary.compile_blueprint(gm)
    eal.save_loaded_asset(gm)
    log("BP_SoulsGameMode ready")


def main():
    ensure_dir(SOULS)
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
    build_blueprints(montages, weapons)

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
