"""Downloads the raw CC0 sources for the bestiary (UNR-T64/T67) into Saved/BestiarySource/.

  python -I tools/fetch_bestiary.py

No login anywhere: Poly Pizza serves its GLBs from static.poly.pizza, and the two Quaternius itch.io
packs are $0 "name your own price" downloads (the same requests the "No thanks, just take me to the
downloads" button makes). tools/convert_bestiary.py (Blender) turns these into the committed
SourceArt/Characters/Bestiary/*.glb; licences and URLs are in SourceArt/Characters/CREDITS.md.
"""
import http.cookiejar
import json
import os
import re
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "Saved", "BestiarySource")

# Poly Pizza (Quaternius uploads marked CC0 1.0): name -> (model page, file id)
POLY_PIZZA = {
    "Skeleton": ("https://poly.pizza/m/yq5ATpujSt", "a089d0ae-5a26-448b-b038-4884d64099e3"),
    "Orc": ("https://poly.pizza/m/5vO2YJsPEf", "52a479b3-a635-4a23-92cb-6697eaa5eed5"),
    "Zombie": ("https://poly.pizza/m/VlXjG0N8Eg", "c4002f69-6979-42e8-ad6e-2f4e14fc3a9d"),
    "Demon": ("https://poly.pizza/m/S7jYW6Amye", "6fbb8914-bd11-45e1-a906-51ace2ca5d2d"),
}
# itch.io (Quaternius, asset licence on the page and License_Standard.txt in the zip: CC0 1.0)
ITCH = {
    "UniversalBaseCharacters": "https://quaternius.itch.io/universal-base-characters",
    "ModularOutfitsFantasy": "https://quaternius.itch.io/modular-character-outfits-fantasy",
}

UA = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/130.0 Safari/537.36"
jar = http.cookiejar.CookieJar()
opener = urllib.request.build_opener(urllib.request.HTTPCookieProcessor(jar))
opener.addheaders = [("User-Agent", UA)]


def request(url, data=None, headers=None):
    for attempt in range(12):
        body = urllib.parse.urlencode(data).encode() if data is not None else None
        try:
            with opener.open(urllib.request.Request(url, data=body, headers=headers or {})) as r:
                return r.read()
        except urllib.error.HTTPError as exc:
            if exc.code != 429:
                raise
            time.sleep(30 + 15 * attempt)  # itch.io rate-limits bursts
    raise RuntimeError("rate limited: " + url)


def save(url, dest):
    with opener.open(url) as r, open(dest, "wb") as f:
        while True:
            chunk = r.read(1 << 20)
            if not chunk:
                break
            f.write(chunk)


def itch_free_download(page, dest_dir):
    html = request(page).decode("utf-8", "replace")
    csrf = re.search(r'name="csrf_token" value="([^"]+)"', html).group(1)
    download_page = json.loads(request(page + "/download_url", {"csrf_token": csrf}, {"X-Requested-With": "XMLHttpRequest"}))["url"]
    html = request(download_page, headers={"Referer": page}).decode("utf-8", "replace")
    found = re.search(r'name="csrf_token" value="([^"]+)"', html)
    csrf = found.group(1) if found else csrf
    out = []
    for upload_id, title in re.findall(r'data-upload_id="(\d+)".{0,600}?title="([^"]+)"', html, re.S):
        if "[Standard]" not in title:
            continue  # the [Source] zips are the paid tier
        reply = json.loads(request("%s/file/%s?source=game_download" % (page, upload_id), {"csrf_token": csrf},
                                   {"X-Requested-With": "XMLHttpRequest", "Referer": download_page}))
        dest = os.path.join(dest_dir, title)
        print("  %s" % title)
        save(reply["url"], dest)
        out.append(dest)
        time.sleep(3)
    return out


def main():
    os.makedirs(OUT, exist_ok=True)
    for name, (page, file_id) in POLY_PIZZA.items():
        dest = os.path.join(OUT, name + ".glb")
        if not os.path.exists(dest):
            print("poly.pizza %s (%s)" % (name, page))
            save("https://static.poly.pizza/%s.glb" % file_id, dest)
    for name, page in ITCH.items():
        folder = os.path.join(OUT, name)
        if os.path.isdir(folder):
            continue
        print("itch.io %s" % page)
        os.makedirs(folder + "_zip", exist_ok=True)
        for archive in itch_free_download(page, folder + "_zip"):
            with zipfile.ZipFile(archive) as z:
                z.extractall(folder)
    print("FETCH_BESTIARY_OK -> %s" % OUT)


if __name__ == "__main__":
    sys.exit(main())
