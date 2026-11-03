"""Import SourceArt into the project, create the demo materials and map, write CharacterAssets.json.

Runs inside the Unreal Editor's Python (UE 5.5+), normally as a commandlet:
  UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script="<abs path to this file>" -unattended -nop4 -nosplash -stdout
Idempotent: re-running replaces assets in place. Prints BUILD_CONTENT_OK on success (tools/build.ps1 checks it).
"""
import json
import os

import unreal

PROJECT_DIR = os.path.abspath(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
SOURCE_ART = os.path.join(PROJECT_DIR, "SourceArt")
ASSETS_JSON = os.path.join(PROJECT_DIR, "Content", "Characters", "CharacterAssets.json")
ENVIRONMENT_JSON = os.path.join(PROJECT_DIR, "Content", "Environment", "EnvironmentAssets.json")

KNIGHT_DIR = "/Game/Characters/Knight"
SKELETONS_DIR = "/Game/Characters/Skeletons"
MATERIALS_DIR = "/Game/Materials"
MAP_PATH = "/Game/Maps/HebridesIsland"
ISLAND_CLASS = "/Script/UnrealSoulslike.HebridesIsland"

SKELETON_NAMES = ["Skeleton_Warrior", "Skeleton_Minion", "Skeleton_Rogue"]
PROPS = {
    "/Game/Props/Knight": ["sword_1handed", "shield_round"],
    "/Game/Props/Skeletons": ["Skeleton_Blade", "Skeleton_Axe", "Skeleton_Shield_Small_A", "Skeleton_Shield_Large_A"],
}
# logical piece name -> (SourceArt/Environment subfolder, FBX name, /Game destination). See SourceArt/Environment/MANIFEST.md.
ENVIRONMENT_PIECES = {
    "wall": ("KayKit_Dungeon", "wall", "/Game/Environment/Dungeon"),
    "wall_broken": ("KayKit_Dungeon", "wall_broken", "/Game/Environment/Dungeon"),
    "wall_cracked": ("KayKit_Dungeon", "wall_cracked", "/Game/Environment/Dungeon"),
    "wall_corner": ("KayKit_Dungeon", "wall_corner", "/Game/Environment/Dungeon"),
    "wall_half": ("KayKit_Dungeon", "wall_half", "/Game/Environment/Dungeon"),
    "wall_arched": ("KayKit_Dungeon", "wall_arched", "/Game/Environment/Dungeon"),
    "gate": ("KayKit_Dungeon", "wall_doorway", "/Game/Environment/Dungeon"),
    "stairs": ("KayKit_Dungeon", "stairs", "/Game/Environment/Dungeon"),
    "pillar": ("KayKit_Dungeon", "pillar", "/Game/Environment/Dungeon"),
    "column": ("KayKit_Dungeon", "column", "/Game/Environment/Dungeon"),
    "bridge": ("KayKit_Dungeon", "floor_foundation_front_and_back", "/Game/Environment/Dungeon"),
    "bridge_pillar": ("KayKit_Dungeon", "pillar", "/Game/Environment/Dungeon"),
    "bridge_railing": ("KayKit_Dungeon", "barrier", "/Game/Environment/Dungeon"),
    "floor": ("KayKit_Dungeon", "floor_tile_large", "/Game/Environment/Dungeon"),
    "house_a": ("KayKit_MedievalHexagon", "building_home_A_green", "/Game/Environment/Hexagon"),
    "house_b": ("KayKit_MedievalHexagon", "building_home_B_green", "/Game/Environment/Hexagon"),
    "tower": ("KayKit_MedievalHexagon", "building_tower_A_green", "/Game/Environment/Hexagon"),
    "ruin": ("KayKit_MedievalHexagon", "building_destroyed", "/Game/Environment/Hexagon"),
    "bridge_arched": ("KayKit_MedievalHexagon", "building_bridge_A", "/Game/Environment/Hexagon"),
}
ANIM_TAKES = {
    "idle": "Idle",
    "walk": "Walking_A",
    "run": "Running_A",
    "light_attack": "1H_Melee_Attack_Slice_Diagonal",
    "heavy_attack": "2H_Melee_Attack_Chop",
    "block": "Blocking",
    "block_hit": "Block_Hit",
    "parry": "Block_Attack",
    "dodge_roll": "Dodge_Forward",
    "hit": "Hit_A",
    "death": "Death_A",
    "heal": "Use_Item",
    # KayKit has no climbing clips; these are the closest stand-ins on the shared rig.
    "jump": "Jump_Idle",
    "climb": "Interact",
    "climb_idle": "Jump_Idle",
    "climb_jump": "Jump_Start",
    "mantle": "Jump_Land",
}

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
eal = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
problems = []


def log(msg):
    unreal.log("[build_content] " + msg)


def warn(msg):
    problems.append(msg)
    unreal.log_warning("[build_content] " + msg)


def object_path(asset):
    return asset.get_path_name() if asset else None


def run_import(filename, destination, options):
    if not os.path.isfile(filename):
        warn("missing source file " + filename)
        return []
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", filename)
    task.set_editor_property("destination_path", destination)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("automated", True)
    task.set_editor_property("save", True)
    task.set_editor_property("options", options)
    asset_tools.import_asset_tasks([task])
    imported = list(task.get_editor_property("imported_object_paths") or [])
    if not imported:
        warn("import produced nothing for " + filename)
    return imported


def fbx_options(skeletal, skeleton=None, animations=False):
    options = unreal.FbxImportUI()
    options.set_editor_property("automated_import_should_detect_type", False)
    options.set_editor_property("import_mesh", True)
    options.set_editor_property("import_materials", True)
    options.set_editor_property("import_textures", True)
    if skeletal:
        options.set_editor_property("import_as_skeletal", True)
        options.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_SKELETAL_MESH)
        options.set_editor_property("import_animations", animations)
        options.set_editor_property("create_physics_asset", False)
        if skeleton:
            options.set_editor_property("skeleton", skeleton)
        sk_data = options.get_editor_property("skeletal_mesh_import_data")
        sk_data.set_editor_property("import_meshes_in_bone_hierarchy", False)
        sk_data.set_editor_property("convert_scene", True)
        sk_data.set_editor_property("convert_scene_unit", True)
        anim_data = options.get_editor_property("anim_sequence_import_data")
        anim_data.set_editor_property("convert_scene", True)
        anim_data.set_editor_property("convert_scene_unit", True)
        anim_data.set_editor_property("animation_length", unreal.FBXAnimationLengthImportType.FBXALIT_EXPORTED_TIME)
    else:
        options.set_editor_property("import_as_skeletal", False)
        options.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_STATIC_MESH)
        options.set_editor_property("import_animations", False)
        sm_data = options.get_editor_property("static_mesh_import_data")
        sm_data.set_editor_property("combine_meshes", True)
        sm_data.set_editor_property("convert_scene", True)
        sm_data.set_editor_property("convert_scene_unit", True)
    return options


def find_imported(paths, cls):
    for p in paths:
        asset = eal.load_asset(p)
        if isinstance(asset, cls):
            return asset
    return None


def assets_in(folder, cls):
    found = []
    if not eal.does_directory_exist(folder):
        return found
    for p in eal.list_assets(folder, recursive=True, include_folder=False):
        asset = eal.load_asset(p)
        if isinstance(asset, cls):
            found.append(asset)
    return found


def match_take(anims, take):
    """Exact asset name first, then UE's '<Mesh>_Anim_<Take>', then the shortest '*_<Take>'."""
    by_name = {a.get_name(): a for a in anims}
    if take in by_name:
        return by_name[take]
    for name, asset in by_name.items():
        if name.endswith("_Anim_" + take):
            return asset
    suffixed = sorted((n for n in by_name if n.endswith("_" + take)), key=len)
    return by_name[suffixed[0]] if suffixed else None


def import_knight():
    log("importing Knight.fbx (mesh + animations)")
    paths = run_import(os.path.join(SOURCE_ART, "Characters", "Knight", "Knight.fbx"), KNIGHT_DIR,
                       fbx_options(skeletal=True, animations=True))
    mesh = find_imported(paths, unreal.SkeletalMesh)
    if mesh is None:
        meshes = assets_in(KNIGHT_DIR, unreal.SkeletalMesh)
        mesh = meshes[0] if meshes else None
    if mesh is None:
        warn("Knight skeletal mesh not found after import")
        return None, None
    return mesh, mesh.get_editor_property("skeleton")


def import_skeletons(shared_skeleton):
    meshes = {}
    for name in SKELETON_NAMES:
        log("importing %s.fbx onto the Knight skeleton" % name)
        paths = run_import(os.path.join(SOURCE_ART, "Characters", "Skeletons", name + ".fbx"), SKELETONS_DIR,
                           fbx_options(skeletal=True, skeleton=shared_skeleton, animations=False))
        mesh = find_imported(paths, unreal.SkeletalMesh)
        if mesh is None:
            mesh = eal.load_asset("%s/%s" % (SKELETONS_DIR, name))
            if not isinstance(mesh, unreal.SkeletalMesh):
                mesh = None
        if mesh is None:
            warn("skeleton mesh %s not found after import" % name)
        else:
            meshes[name] = mesh
    return meshes


def import_props():
    props = {}
    for folder, names in PROPS.items():
        source_dir = os.path.join(SOURCE_ART, "Props", folder.rsplit("/", 1)[-1])
        for name in names:
            log("importing prop " + name)
            paths = run_import(os.path.join(source_dir, name + ".fbx"), folder, fbx_options(skeletal=False))
            mesh = find_imported(paths, unreal.StaticMesh)
            if mesh is None:
                candidate = eal.load_asset("%s/%s" % (folder, name))
                mesh = candidate if isinstance(candidate, unreal.StaticMesh) else None
            if mesh is None:
                warn("prop %s not found after import" % name)
            else:
                props[name] = mesh
    return props


def use_complex_collision(mesh):
    """Complex-as-simple: characters walk on bridge decks/stairs and traces hit the real wall faces."""
    try:
        body = mesh.get_editor_property("body_setup")
        body.set_editor_property("collision_trace_flag", unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
        eal.save_loaded_asset(mesh)
    except Exception as exc:
        warn("could not set complex collision on %s (%s); keeping auto-generated collision" % (mesh.get_name(), exc))


def import_environment():
    """Import the modular building/wall/bridge kit and write EnvironmentAssets.json. Failures only warn."""
    imported = {}
    pieces = {}
    for logical, (pack, name, folder) in ENVIRONMENT_PIECES.items():
        key = (pack, name)
        if key not in imported:
            log("importing environment piece %s/%s" % (pack, name))
            options = fbx_options(skeletal=False)
            options.get_editor_property("static_mesh_import_data").set_editor_property("auto_generate_collision", True)
            paths = run_import(os.path.join(SOURCE_ART, "Environment", pack, name + ".fbx"), folder, options)
            mesh = find_imported(paths, unreal.StaticMesh)
            if mesh is None:
                candidate = eal.load_asset("%s/%s" % (folder, name))
                mesh = candidate if isinstance(candidate, unreal.StaticMesh) else None
            if mesh is None:
                warn("environment piece %s not found after import" % name)
            else:
                use_complex_collision(mesh)
            imported[key] = mesh
        if imported[key] is not None:
            pieces[logical] = object_path(imported[key])

    os.makedirs(os.path.dirname(ENVIRONMENT_JSON), exist_ok=True)
    with open(ENVIRONMENT_JSON, "w", encoding="utf-8") as f:
        json.dump({"pieces": pieces}, f, indent=2)
    log("wrote %s (%d of %d pieces)" % (ENVIRONMENT_JSON, len(pieces), len(ENVIRONMENT_PIECES)))


def fresh_material(name):
    path = "%s/%s" % (MATERIALS_DIR, name)
    if eal.does_asset_exist(path):
        mat = eal.load_asset(path)
        mel.delete_all_material_expressions(mat)
        return mat
    return asset_tools.create_asset(name, MATERIALS_DIR, unreal.Material, unreal.MaterialFactoryNew())


def constant(mat, value, prop, y):
    expr = mel.create_material_expression(mat, unreal.MaterialExpressionConstant, -300, y)
    expr.set_editor_property("r", value)
    mel.connect_material_property(expr, "", prop)


def color_param(mat, default):
    expr = mel.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -400, 0)
    expr.set_editor_property("parameter_name", "Color")
    expr.set_editor_property("default_value", unreal.LinearColor(*default))
    mel.connect_material_property(expr, "", unreal.MaterialProperty.MP_BASE_COLOR)


def finish_material(mat):
    mel.recompile_material(mat)
    eal.save_loaded_asset(mat)
    log("material ready: " + mat.get_path_name())


def create_materials():
    terrain = fresh_material("M_Terrain")
    vc = mel.create_material_expression(terrain, unreal.MaterialExpressionVertexColor, -400, 0)
    mel.connect_material_property(vc, "", unreal.MaterialProperty.MP_BASE_COLOR)
    constant(terrain, 0.9, unreal.MaterialProperty.MP_ROUGHNESS, 200)
    finish_material(terrain)

    stone = fresh_material("M_Stone")
    color_param(stone, (0.35, 0.34, 0.32, 1.0))
    constant(stone, 0.85, unreal.MaterialProperty.MP_ROUGHNESS, 200)
    finish_material(stone)

    water = fresh_material("M_Water")
    color_param(water, (0.02, 0.06, 0.09, 1.0))
    constant(water, 0.06, unreal.MaterialProperty.MP_ROUGHNESS, 200)
    constant(water, 1.0, unreal.MaterialProperty.MP_SPECULAR, 300)
    finish_material(water)


def load_island_class():
    try:
        return unreal.load_class(None, ISLAND_CLASS)
    except Exception as exc:  # the C++ module isn't compiled/loaded
        warn("could not load %s (%s); the map is saved without it and the game mode spawns the island" % (ISLAND_CLASS, exc))
        return None


def spawn_island(island_class):
    if island_class is None:
        return
    location = unreal.Vector(0, 0, 0)
    try:
        actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
        actor = actors.spawn_actor_from_class(island_class, location)
    except Exception:
        actor = unreal.EditorLevelLibrary.spawn_actor_from_class(island_class, location)
    if actor is None:
        warn("spawning HebridesIsland failed")
    else:
        log("placed " + actor.get_name())


def create_map():
    island_class = load_island_class()
    if eal.does_asset_exist(MAP_PATH):
        eal.delete_asset(MAP_PATH)

    created = False
    try:
        level_subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
        created = bool(level_subsystem.new_level(MAP_PATH))
        if created:
            spawn_island(island_class)
            created = bool(level_subsystem.save_current_level())
    except Exception as exc:
        log("LevelEditorSubsystem path failed (%s); falling back to EditorLoadingAndSavingUtils" % exc)
        created = False

    if not created:
        world = unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
        if world is None:
            warn("could not create a blank map")
            return
        spawn_island(island_class)
        if not unreal.EditorLoadingAndSavingUtils.save_map(world, MAP_PATH):
            warn("could not save " + MAP_PATH)
            return
    log("map saved: " + MAP_PATH)


def attachment(socket, mesh):
    return {"socket": socket, "mesh": object_path(mesh)} if mesh else None


def character_entry(mesh, attachments, name=None):
    if mesh is None:
        return None
    entry = {"mesh": object_path(mesh), "attachments": [a for a in attachments if a]}
    if name:
        entry = {"name": name, **entry}
    return entry


def write_assets_json(knight, skeletons, props):
    anims = assets_in(KNIGHT_DIR, unreal.AnimSequence)
    anim_paths = {}
    for key, take in ANIM_TAKES.items():
        asset = match_take(anims, take)
        if asset is None:
            warn("no animation asset for take '%s' (%s)" % (take, key))
        else:
            anim_paths[key] = object_path(asset)

    data = {}
    knight_entry = character_entry(knight, [attachment("handslot.r", props.get("sword_1handed")),
                                            attachment("handslot.l", props.get("shield_round"))])
    if knight_entry:
        data["knight"] = knight_entry

    loadouts = {
        "Skeleton_Warrior": [("handslot.r", "Skeleton_Blade"), ("handslot.l", "Skeleton_Shield_Small_A")],
        "Skeleton_Minion": [("handslot.r", "Skeleton_Axe")],
        "Skeleton_Rogue": [("handslot.r", "Skeleton_Blade")],
    }
    skeleton_entries = []
    for name in SKELETON_NAMES:
        entry = character_entry(skeletons.get(name), [attachment(s, props.get(p)) for s, p in loadouts[name]], name)
        if entry:
            skeleton_entries.append(entry)
    if skeleton_entries:
        data["skeletons"] = skeleton_entries

    boss = character_entry(skeletons.get("Skeleton_Warrior"),
                           [attachment("handslot.r", props.get("Skeleton_Axe")),
                            attachment("handslot.l", props.get("Skeleton_Shield_Large_A"))])
    if boss:
        data["boss"] = boss
    if anim_paths:
        data["anims"] = anim_paths

    os.makedirs(os.path.dirname(ASSETS_JSON), exist_ok=True)
    with open(ASSETS_JSON, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=2)
    log("wrote %s (%d anims, %d skeletons)" % (ASSETS_JSON, len(anim_paths), len(skeleton_entries)))
    return data


def main():
    log("project dir: " + PROJECT_DIR)
    # Legacy FBX importer, so FbxImportUI options (bone-hierarchy meshes, skeleton reuse) are honoured.
    unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX false")

    knight, shared_skeleton = import_knight()
    skeletons = import_skeletons(shared_skeleton) if shared_skeleton else {}
    if shared_skeleton is None:
        warn("skipping skeleton enemies: no shared skeleton from Knight.fbx")
    props = import_props()
    try:
        import_environment()
    except Exception as exc:  # the game falls back to primitive shapes without the kit
        warn("environment import failed: %s" % exc)
    create_materials()
    create_map()
    data = write_assets_json(knight, skeletons, props)

    if problems:
        log("finished with %d problem(s):" % len(problems))
        for p in problems:
            log("  - " + p)
    if not data:
        raise RuntimeError("build_content: no character assets were produced")
    print("BUILD_CONTENT_OK")


main()
