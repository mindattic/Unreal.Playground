"""Closes the knight's hands: the sword fist round the hilt, the left fist round the shield grip.

The animation library the knight is retargeted from (Quaternius UAL) animates open hands, so the
fingers splay off the weapons. This pins every finger joint of both hands, in every retargeted
clip under /Game/Souls/Anims, and in the template's unarmed locomotion the knight walks, runs, jumps
and idles with, to a fist (USoulsAnimTools::BakeBoneOffsets).

build_souls_character.py calls bake_all() after retargeting; to re-bake on its own:
UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=tools/bake_grip.py
"""
import json
import os

import unreal

PROJECT = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
LOG = os.path.join(PROJECT, "Saved", "bake_grip.log")
FOLDERS = ("/Game/Souls/Anims", "/Game/Characters/Mannequins/Anims/Unarmed", "/Game/Characters/Mannequins/Anims/Death")

# Per joint (proximal, middle, distal), as (pitch, yaw, roll) on top of the reference pose.
GRIP = {
    "finger_axis": "yaw",
    "finger_curl": (-62.0, -78.0, -55.0),
    "thumb": ((0.0, -25.0, 0.0), (0.0, -35.0, 0.0), (0.0, -30.0, 0.0)),
    "left_sign": 1.0,
}
FINGERS = ("index", "middle", "ring", "pinky")


def grip():
    # Saved/grip_override.json (same keys) lets the fit be tuned without editing this file.
    spec = dict(GRIP)
    path = os.path.join(PROJECT, "Saved", "grip_override.json")
    if os.path.exists(path):
        with open(path, encoding="utf-8") as handle:
            spec.update(json.load(handle))
    return spec


def rot(axis, angle):
    return unreal.Rotator(**{"pitch": 0.0, "yaw": 0.0, "roll": 0.0, axis: angle})


def offsets_for(side, spec):
    sign = 1.0 if side == "r" else spec["left_sign"]
    bones, offsets = [], []
    for finger in FINGERS:
        for joint, angle in zip(("01", "02", "03"), spec["finger_curl"]):
            bones.append("%s_%s_%s" % (finger, joint, side))
            offsets.append(rot(spec["finger_axis"], angle * sign))
    for joint, (p, y, r) in zip(("01", "02", "03"), spec["thumb"]):
        bones.append("thumb_%s_%s" % (joint, side))
        offsets.append(unreal.Rotator(pitch=p * sign, yaw=y * sign, roll=r * sign))
    return bones, offsets


def bake_all(log=None):
    spec = grip()
    eal = unreal.EditorAssetLibrary
    lines = []
    paths = [p for folder in FOLDERS if eal.does_directory_exist(folder)
             for p in eal.list_assets(folder, recursive=True, include_folder=False)]
    for path in paths:
        seq = eal.load_asset(path)
        if not isinstance(seq, unreal.AnimSequence):
            continue
        changed = 0
        for side in ("r", "l"):
            bones, offsets = offsets_for(side, spec)
            changed += unreal.SoulsAnimTools.bake_bone_offsets(seq, bones, offsets)
        eal.save_loaded_asset(seq, only_if_is_dirty=False)
        lines.append("grip %s: %d bones" % (seq.get_name(), changed))
    if log:
        for line in lines:
            log(line)
    return lines


if __name__ == "__main__":
    result = bake_all()
    with open(LOG, "w", encoding="utf-8") as handle:
        handle.write("\n".join(result) + "\nBAKE_GRIP_OK\n")
