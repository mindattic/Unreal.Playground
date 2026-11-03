"""Install Epic's Third Person C++ template (all variants) into this project, the way the editor's
New Project wizard would: copy its code (TP_ThirdPerson -> UnrealSoulslike), its content and the
shared content packs (Characters/Mannequins, Input, LevelPrototyping). Engine-supplied content is
not committed; re-run this after a fresh clone.

Run: python -I tools/install_thirdperson_template.py [UE root]   (default: D:/Games/UE_5.8)
"""
import os
import re
import shutil
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ENGINE = sys.argv[1] if len(sys.argv) > 1 else r"D:\Games\UE_5.8"
TEMPLATE = os.path.join(ENGINE, "Templates", "TP_ThirdPerson")
SHARED = os.path.join(ENGINE, "Templates", "TemplateResources", "High")
SHARED_PACKS = ["Characters", "Input", "LevelPrototyping"]

SOURCE_FROM = os.path.join(TEMPLATE, "Source", "TP_ThirdPerson")
SOURCE_TO = os.path.join(ROOT, "Source", "UnrealSoulslike", "ThirdPerson")
SKIP_SOURCE = {"TP_ThirdPerson.h", "TP_ThirdPerson.cpp", "TP_ThirdPerson.Build.cs"}


def rename(text):
    text = text.replace("TP_THIRDPERSON", "UNREALSOULSLIKE")
    return re.sub("TP_ThirdPerson", "UnrealSoulslike", text, flags=re.IGNORECASE)


def copy_source():
    count = 0
    for folder, _, files in os.walk(SOURCE_FROM):
        rel = os.path.relpath(folder, SOURCE_FROM)
        for name in files:
            if rel == "." and name in SKIP_SOURCE:
                continue
            src = os.path.join(folder, name)
            dst_dir = os.path.join(SOURCE_TO, rel) if rel != "." else SOURCE_TO
            os.makedirs(dst_dir, exist_ok=True)
            with open(src, encoding="utf-8-sig") as f:
                text = f.read()
            with open(os.path.join(dst_dir, rename(name)), "w", encoding="utf-8", newline="") as f:
                f.write(rename(text))
            count += 1
    print("source files:", count)


def copy_tree(src, dst):
    count = 0
    for folder, _, files in os.walk(src):
        rel = os.path.relpath(folder, src)
        out = os.path.join(dst, rel) if rel != "." else dst
        os.makedirs(out, exist_ok=True)
        for name in files:
            shutil.copy2(os.path.join(folder, name), os.path.join(out, name))
            count += 1
    return count


def copy_content():
    content = os.path.join(ROOT, "Content")
    print("template content files:", copy_tree(os.path.join(TEMPLATE, "Content"), content))
    for pack in SHARED_PACKS:
        print(pack, "files:", copy_tree(os.path.join(SHARED, pack, "Content"), os.path.join(content, pack)))


if __name__ == "__main__":
    if not os.path.isdir(SOURCE_FROM):
        sys.exit("Third Person template not found at " + TEMPLATE)
    copy_source()
    copy_content()
