"""Download the CC0 Poly Haven art used by the island map (tools/build_island.py) into
SourceArt/Environment/PolyHaven. Host Python, stdlib only:

  python -I tools/fetch_island_art.py

Idempotent: files already present with the right MD5 are skipped. Everything is 2k (textures as
JPG; models as glTF + their 2k JPG textures) to keep the repo a sane size. Downloads are data only;
nothing from them is executed.
"""
import hashlib
import json
import os
import sys
import urllib.request

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEST = os.path.join(REPO, "SourceArt", "Environment", "PolyHaven")
API = "https://api.polyhaven.com"
UA = {"User-Agent": "Unreal.Soulslike island art fetcher (UNR-T12)"}

# Terrain / surface texture sets: id -> the Poly Haven maps the terrain material actually samples.
FULL = ("Diffuse", "nor_gl", "arm", "Displacement")
TEXTURES = {
    "sparse_grass": ("Diffuse",),       # heather-brown patches in the turf
    "coast_sand_01": FULL,              # pebbly sand: detail / variation on the beach
    "aerial_beach_01": FULL,            # pale wind-rippled shell sand: the Hebridean beach
    "damp_beach_sand": ("Diffuse",),    # wet sand at the waterline / sea floor
    "seaside_rock": FULL,               # grey coastal rock for steep slopes and cliffs
    "rocky_trail": FULL,                # worn path / disturbed ground around ruins
}

# Scanned models (glTF 2k).
MODELS = [
    "rock_moss_set_01", "rock_moss_set_02", "boulder_01", "rock_07", "rock_face_02",
    "mountainside", "modular_fort_01", "dead_tree_trunk",
]


def get_json(url):
    with urllib.request.urlopen(urllib.request.Request(url, headers=UA), timeout=60) as r:
        return json.loads(r.read().decode("utf-8"))


def md5_of(path):
    h = hashlib.md5()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def fetch(url, path, md5):
    if os.path.exists(path) and (not md5 or md5_of(path) == md5):
        return False
    os.makedirs(os.path.dirname(path), exist_ok=True)
    tmp = path + ".part"
    with urllib.request.urlopen(urllib.request.Request(url, headers=UA), timeout=300) as r, open(tmp, "wb") as f:
        while True:
            chunk = r.read(1 << 20)
            if not chunk:
                break
            f.write(chunk)
    if md5 and md5_of(tmp) != md5:
        os.remove(tmp)
        raise RuntimeError("MD5 mismatch for " + url)
    os.replace(tmp, path)
    return True


# ambientCG surfaces (CC0): id -> maps kept from the 2K-JPG zip (the rest of the zip is discarded).
AMBIENTCG = {
    "Grass004": ("Color", "NormalGL", "Roughness", "AmbientOcclusion"),  # the island's main turf
    "Grass003": ("Color",),  # darker olive grass for variation and the moss on props
}


def fetch_ambientcg(manifest):
    import io
    import zipfile
    for aid in AMBIENTCG:
        folder = os.path.join(REPO, "SourceArt", "Environment", "AmbientCG", aid)
        wanted = ["%s_2K-JPG_%s.jpg" % (aid, m) for m in AMBIENTCG[aid]]
        if all(os.path.exists(os.path.join(folder, w)) for w in wanted):
            print("ok         " + aid)
        else:
            url = "https://ambientcg.com/get?file=%s_2K-JPG.zip" % aid
            with urllib.request.urlopen(urllib.request.Request(url, headers=UA), timeout=300) as r:
                data = r.read()
            os.makedirs(folder, exist_ok=True)
            with zipfile.ZipFile(io.BytesIO(data)) as z:
                for name in z.namelist():
                    base = os.path.basename(name)
                    if base in wanted:  # only the known texture files, never paths from the archive
                        with open(os.path.join(folder, base), "wb") as f:
                            f.write(z.read(name))
            print("downloaded " + aid)
        manifest["ambientcg"][aid] = wanted


def main():
    manifest = {"textures": {}, "models": {}, "ambientcg": {}}
    fetch_ambientcg(manifest)
    for tid in TEXTURES:
        files = get_json("%s/files/%s" % (API, tid))
        out = {}
        for key in TEXTURES[tid]:
            entry = files[key]["2k"]["jpg"]
            name = os.path.basename(entry["url"])
            path = os.path.join(DEST, "Textures", tid, name)
            got = fetch(entry["url"], path, entry.get("md5"))
            out[key] = name
            print(("downloaded " if got else "ok         ") + os.path.relpath(path, REPO))
        manifest["textures"][tid] = out
    for mid in MODELS:
        gl = get_json("%s/files/%s" % (API, mid))["gltf"]["2k"]["gltf"]
        folder = os.path.join(DEST, "Models", mid)
        main_name = os.path.basename(gl["url"])
        fetch(gl["url"], os.path.join(folder, main_name), gl.get("md5"))
        names = [main_name]
        for rel, entry in gl["include"].items():
            path = os.path.join(folder, *rel.split("/"))
            got = fetch(entry["url"], path, entry.get("md5"))
            names.append(rel)
            print(("downloaded " if got else "ok         ") + os.path.relpath(path, REPO))
        manifest["models"][mid] = {"gltf": main_name, "files": sorted(names)}
    with open(os.path.join(DEST, "polyhaven_manifest.json"), "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2, sort_keys=True)
    print("FETCH_OK")


if __name__ == "__main__":
    sys.exit(main())
