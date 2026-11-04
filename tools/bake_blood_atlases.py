"""Bakes the blood FX sheets (UNR-T56) from the CC0 sources in SourceArt/FX/Blood/src (credits:
SourceArt/FX/Blood/CREDITS.md) into the three atlases tools/build_blood_fx.py imports:

  T_BloodSpurtAtlas.png  2048x1024, 4x2 cells of 512: overcrafted's splatter-splash frames 0..5 (base
                         at the bottom centre of each cell), then frame 5 thinning out twice
  T_BloodMistAtlas.png   1024x1024, 4x4 cells of 256: Sinestesia's 16-frame blood splash, halved
  T_BloodDecals.png      2048x2048, 2x2 cells of 1024: four dense patches of ExileGL's splatter,
                         each faded out towards a ragged round edge so the decal box never shows

Only the alpha (shape) and red (light/dark) channels are used; the materials do the colour.

Plain CPython with Pillow, run once by hand when the sources change (the results are committed):
  python -I tools/bake_blood_atlases.py
"""
import math
import os
import random
import sys

from PIL import Image, ImageChops, ImageFilter

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "SourceArt", "FX", "Blood")
SRC = os.path.join(ROOT, "src")


def spurt_atlas():
    frames = [Image.open(os.path.join(SRC, "overcrafted", "splattersplash_%d.png" % i)).convert("RGBA") for i in range(6)]
    box = None
    for f in frames:
        b = f.getbbox()
        box = b if box is None else (min(box[0], b[0]), min(box[1], b[1]), max(box[2], b[2]), max(box[3], b[3]))
    # Square the crop around the splash's bottom-centre so the base sits at the bottom middle of the cell.
    w = box[2] - box[0]
    h = box[3] - box[1]
    side = max(w, h)
    cx = (box[0] + box[2]) // 2
    crop = (cx - side // 2, box[3] - side, cx + side // 2, box[3])
    cells = []
    for f in frames:
        c = Image.new("RGBA", (side, side), (0, 0, 0, 0))
        c.paste(f.crop(crop), (0, 0))
        cells.append(c.resize((512, 512), Image.LANCZOS))
    for keep in (0.6, 0.3):
        last = cells[5].copy()
        a = last.getchannel("A").point(lambda v, k=keep: int(max(0, v - 255 * (1 - k))))
        last.putalpha(a)
        cells.append(last)
    out = Image.new("RGBA", (2048, 1024), (0, 0, 0, 0))
    for i, c in enumerate(cells):
        out.paste(c, ((i % 4) * 512, (i // 4) * 512))
    return out


def mist_atlas():
    return Image.open(os.path.join(SRC, "sinestesia", "blood_splash_4x4.png")).convert("RGBA").resize((1024, 1024), Image.LANCZOS)


def decal_atlas():
    src = Image.open(os.path.join(SRC, "exilegl", "blood.png")).convert("RGBA")
    # Centres of four dense patches (picked from a 100 px density map of the 1600x1200 sheet).
    centres = [(650, 520), (1150, 850), (360, 650), (1250, 360)]
    half = 350
    rnd = random.Random(56)
    out = Image.new("RGBA", (2048, 2048), (0, 0, 0, 0))
    for i, (x, y) in enumerate(centres):
        patch = src.crop((x - half, y - half, x + half, y + half)).resize((1024, 1024), Image.LANCZOS)
        # Ragged round falloff: full inside ~55% of the radius, gone by ~95%, wobbling with angle.
        wob = [rnd.uniform(-0.08, 0.08) for _ in range(12)]
        mask = Image.new("L", (1024, 1024), 0)
        px = mask.load()
        for v in range(1024):
            for u in range(1024):
                dx = (u - 511.5) / 511.5
                dy = (v - 511.5) / 511.5
                r = math.hypot(dx, dy)
                ang = (math.atan2(dy, dx) + math.pi) / (2 * math.pi) * 12
                k = int(ang) % 12
                t = ang - int(ang)
                edge = 0.95 + wob[k] * (1 - t) + wob[(k + 1) % 12] * t
                inner = edge - 0.4
                f = 1.0 if r <= inner else (0.0 if r >= edge else 1.0 - (r - inner) / (edge - inner))
                px[u, v] = int(255 * f * f * (3 - 2 * f))
        mask = mask.filter(ImageFilter.GaussianBlur(6))
        # Erode rather than fade: thin spatter vanishes towards the edge, the drops left stay solid.
        loss = mask.point(lambda m: int(min(255, (255 - m) * 1.4)))
        patch.putalpha(ImageChops.subtract(patch.getchannel("A"), loss))
        out.paste(patch, ((i % 2) * 1024, (i // 2) * 1024))
    return out


def main():
    for name, make in (("T_BloodSpurtAtlas", spurt_atlas), ("T_BloodMistAtlas", mist_atlas), ("T_BloodDecals", decal_atlas)):
        img = make()
        path = os.path.join(ROOT, name + ".png")
        img.save(path, optimize=True)
        print("%s %dx%d" % (path, img.size[0], img.size[1]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
