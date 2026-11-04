"""Re-applies the sword/shield fit to the built character blueprints without rebuilding them.

Run after changing SWORD_TRANSFORM / SHIELD_TRANSFORM in tools/build_souls_character.py:
UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=tools/refit_weapons.py
"""
import os

import unreal

PROJECT = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
LOG = os.path.join(PROJECT, "Saved", "refit_weapons.log")
BLUEPRINTS = ("BP_SoulsKnight", "BP_SoulsEnemy", "BP_SoulsBoss", "BP_SoulsShadowBoss")


def load_fit():
    # Reads the constants by parsing, so the big build script's import-time side effects never run.
    path = os.path.join(PROJECT, "tools", "build_souls_character.py")
    fit = {}
    with open(path, encoding="utf-8") as handle:
        for line in handle:
            if line.startswith(("SWORD_TRANSFORM =", "SHIELD_TRANSFORM =")):
                name, value = line.split("=", 1)
                fit[name.strip()] = eval(value.strip(), {"__builtins__": {}})
    return fit["SWORD_TRANSFORM"], fit["SHIELD_TRANSFORM"]


def place(component, spec):
    component.set_editor_property("relative_location", unreal.Vector(*spec["location"]))
    component.set_editor_property("relative_rotation", unreal.Rotator(*spec["rotation"]))
    component.set_editor_property("relative_scale3d", unreal.Vector(spec["scale"], spec["scale"], spec["scale"]))


def main():
    sword, shield = load_fit()
    eal = unreal.EditorAssetLibrary
    lines = []
    for name in BLUEPRINTS:
        path = "/Game/Souls/" + name
        if not eal.does_asset_exist(path):
            lines.append("skip %s (missing)" % name)
            continue
        bp = eal.load_asset(path)
        cdo = unreal.get_default_object(unreal.load_object(None, path + "." + name + "_C"))
        body = cdo.get_editor_property("mesh").get_editor_property("skeletal_mesh_asset")
        if body is not None and not body.get_name().startswith("SKM_Manny"):
            # Another body (the Ranger, UNR-T64): tools/build_bestiary.py fits its hands from these numbers.
            lines.append("skip %s (wears %s; rerun tools/build_bestiary.py to refit it)" % (name, body.get_name()))
            continue
        place(cdo.get_editor_property("Sword"), sword)
        place(cdo.get_editor_property("Shield"), shield)
        unreal.BlueprintEditorLibrary.compile_blueprint(bp)
        eal.save_loaded_asset(bp, only_if_is_dirty=False)
        lines.append("OK %s" % name)
    with open(LOG, "w", encoding="utf-8") as handle:
        handle.write("\n".join(lines) + "\n")


main()
