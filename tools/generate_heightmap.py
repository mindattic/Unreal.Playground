"""Generate the Hebrides island heightmap, landmark file and a colour preview.

Run: python -I tools/generate_heightmap.py   (from the repo root)
Outputs:
  Content/Terrain/HebridesHeightmap.png   16-bit greyscale, RES x RES, sea level = SEA_LEVEL (for humans/tools)
  Content/Terrain/HebridesHeightmap.r16   same samples, raw little-endian uint16 (what the game reads)
  Content/Terrain/HebridesLandmarks.json  normalized (u, v) positions the game spawns things at
  docs/island_preview.png                 colour preview for humans
"""
import json
import os

import numpy as np
from PIL import Image

RES = 257
SEED = 1745
SEA_LEVEL = 0.20
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# (u, v) in [0, 1]; v grows southward in the image, the game maps it to +Y.
LANDMARKS = {
    "landing_beach": (0.50, 0.84),
    "player_starts": [(0.47, 0.82), (0.53, 0.82), (0.47, 0.79), (0.53, 0.79)],
    "crofts": [(0.40, 0.72), (0.46, 0.68), (0.35, 0.66)],
    "broch": (0.66, 0.68),
    "standing_stones": (0.28, 0.48),
    "keep": (0.56, 0.36),
    "skeleton_spawns": [
        (0.42, 0.66), (0.37, 0.70), (0.63, 0.64), (0.69, 0.71), (0.33, 0.47),
        (0.25, 0.45), (0.33, 0.43), (0.70, 0.50), (0.48, 0.50), (0.60, 0.44),
        (0.51, 0.40), (0.62, 0.33), (0.50, 0.31),
    ],
    "boss": (0.56, 0.34),
}

rng = np.random.default_rng(SEED)


def value_noise(shape, cells):
    grid = rng.random((cells + 2, cells + 2))
    ys, xs = np.mgrid[0:shape[0], 0:shape[1]]
    fx = xs / (shape[1] - 1) * cells
    fy = ys / (shape[0] - 1) * cells
    x0, y0 = fx.astype(int), fy.astype(int)
    tx, ty = fx - x0, fy - y0
    tx = tx * tx * (3 - 2 * tx)
    ty = ty * ty * (3 - 2 * ty)
    a = grid[y0, x0] * (1 - tx) + grid[y0, x0 + 1] * tx
    b = grid[y0 + 1, x0] * (1 - tx) + grid[y0 + 1, x0 + 1] * tx
    return a * (1 - ty) + b * ty


def fbm(shape, base_cells, octaves, gain=0.5):
    total = np.zeros(shape)
    amp, norm = 1.0, 0.0
    for o in range(octaves):
        total += value_noise(shape, base_cells * 2 ** o) * amp
        norm += amp
        amp *= gain
    return total / norm


def smoothstep(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0, 1)
    return t * t * (3 - 2 * t)


shape = (RES, RES)
ys, xs = np.mgrid[0:RES, 0:RES] / (RES - 1)
edge_noise = (fbm(shape, 10, 3) - 0.5) * 0.05


def blend_disc(h, centre, radius, target, falloff):
    u, v = centre
    d = np.hypot(xs - u, ys - v) + edge_noise
    w = 1 - smoothstep(radius, radius + falloff, d)
    return h * (1 - w) + target * w


# Ragged coastline: warp the radial distance with two octaves of noise, plus two headlands.
warp = (fbm(shape, 4, 4) - 0.5) * 0.34 + (fbm(shape, 14, 2) - 0.5) * 0.08
r = np.hypot((xs - 0.5) * 1.1, (ys - 0.52)) + warp
r -= 0.10 * np.exp(-(((xs - 0.80) / 0.07) ** 2 + ((ys - 0.30) / 0.05) ** 2))
r -= 0.08 * np.exp(-(((xs - 0.22) / 0.05) ** 2 + ((ys - 0.78) / 0.07) ** 2))
mask = 1 - smoothstep(0.30, 0.40, r)

# Low, rolling moor with one central rise; the Hebrides are mostly under 100 m.
hills = fbm(shape, 4, 6)
rise = np.exp(-(((xs - 0.55) / 0.16) ** 2 + ((ys - 0.40) / 0.14) ** 2))
h = mask * (SEA_LEVEL + 0.02 + 0.22 * hills ** 2.0 + 0.22 * rise)

# Sea loch winding in from the west coast.
loch_y = 0.56 + 0.05 * np.sin(xs * 14.0)
loch_w = 0.018 + 0.012 * (1 - xs / 0.45)
loch = (1 - smoothstep(loch_w, loch_w + 0.03, np.abs(ys - loch_y))) * (1 - smoothstep(0.30, 0.45, xs))
h = h * (1 - loch) + (SEA_LEVEL - 0.08) * loch

# Gentle landing beach, flat ground for the crofts, raised plateau for the keep.
h = blend_disc(h, LANDMARKS["landing_beach"], 0.06, SEA_LEVEL + 0.012, 0.06)
for c in LANDMARKS["crofts"]:
    h = blend_disc(h, c, 0.03, SEA_LEVEL + 0.04, 0.03)
h = blend_disc(h, LANDMARKS["broch"], 0.035, SEA_LEVEL + 0.06, 0.03)
h = blend_disc(h, LANDMARKS["standing_stones"], 0.04, SEA_LEVEL + 0.08, 0.04)
h = blend_disc(h, LANDMARKS["keep"], 0.07, SEA_LEVEL + 0.20, 0.05)

# The map edge is always open sea.
edge = smoothstep(0.0, 0.06, np.minimum.reduce([xs, ys, 1 - xs, 1 - ys]))
h = h * edge
h = np.clip(h, 0, 1)

terrain_dir = os.path.join(ROOT, "Content", "Terrain")
os.makedirs(terrain_dir, exist_ok=True)
samples = (h * 65535).astype(np.uint16)
Image.fromarray(samples).save(os.path.join(terrain_dir, "HebridesHeightmap.png"))
# Raw little-endian 16-bit, row-major: what the game reads (C++ and C# alike, no PNG decoder).
samples.astype("<u2").tofile(os.path.join(terrain_dir, "HebridesHeightmap.r16"))

with open(os.path.join(terrain_dir, "HebridesLandmarks.json"), "w", encoding="utf-8") as f:
    json.dump({"resolution": RES, "sea_level": SEA_LEVEL, "seed": SEED, **LANDMARKS}, f, indent=2)

# Preview: sea, sand, machair grass, heather, rock.
rgb = np.zeros(shape + (3,), dtype=np.uint8)
above = h - SEA_LEVEL
rgb[:] = (28, 62, 92)
rgb[above > -0.03] = (52, 98, 128)
rgb[above > 0] = (214, 200, 160)
rgb[above > 0.015] = (110, 140, 70)
rgb[above > 0.09] = (96, 80, 92)
rgb[above > 0.22] = (128, 126, 120)
preview = Image.fromarray(rgb).resize((RES * 3, RES * 3), Image.NEAREST)
docs_dir = os.path.join(ROOT, "docs")
os.makedirs(docs_dir, exist_ok=True)
preview.save(os.path.join(docs_dir, "island_preview.png"))
print(f"land fraction {np.mean(h > SEA_LEVEL):.2f}, max height {h.max():.3f}")
