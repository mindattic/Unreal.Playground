"""Generate the procedural source art for the island (host Python with numpy + Pillow):

  python -I tools/generate_island_art.py

Writes SourceArt/Environment/Generated/:
  T_WaterNormal.png   1024^2 tileable ocean wave normal map (tangent space, OpenGL green-up)
  T_IslandNoise.png   1024^2 tileable fbm noise, three scales in R/G/B (macro variation, foam)
  SM_GrassClump_A.obj / _B.obj / _C.obj   blade-geometry grass clumps (cm, Z-up: UE OBJ import keeps Z up)
Deterministic (fixed seeds), so re-running reproduces the committed files.
"""
import math
import os

import numpy as np
from PIL import Image

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(REPO, "SourceArt", "Environment", "Generated")


def water_normal(size=1024, seed=3):
    rng = np.random.default_rng(seed)
    y, x = np.mgrid[0:size, 0:size].astype(np.float64) / size
    dhdx = np.zeros((size, size))
    dhdy = np.zeros((size, size))
    wind = math.radians(25.0)
    waves = []
    for _ in range(160):
        # Integer wave vectors keep the texture tileable.
        freq = math.exp(rng.uniform(math.log(3.0), math.log(64.0)))
        ang = wind + rng.normal(0.0, 0.55)
        kx, ky = round(freq * math.cos(ang)), round(freq * math.sin(ang))
        if kx == 0 and ky == 0:
            continue
        k = math.hypot(kx, ky)
        amp = 1.0 / (k ** 1.9)
        waves.append((kx, ky, amp, rng.uniform(0, 2 * math.pi), rng.uniform(1.6, 3.2)))
    for kx, ky, amp, phase, sharp in waves:
        theta = 2 * math.pi * (kx * x + ky * y) + phase
        # Slightly peaked crests: d/dtheta of (1 - |cos|^sharp)-ish shape via a sharpened sine.
        s = np.sin(theta)
        c = np.cos(theta)
        deriv = amp * c * (1.0 + 0.35 * s)
        dhdx += deriv * 2 * math.pi * kx
        dhdy += deriv * 2 * math.pi * ky
    strength = 0.11
    nx, ny, nz = -dhdx * strength, -dhdy * strength, np.ones_like(dhdx)
    n = np.sqrt(nx * nx + ny * ny + nz * nz)
    rgb = np.stack([nx / n, ny / n, nz / n], axis=-1) * 0.5 + 0.5
    # Image rows go down; flip Y so green is "up" (OpenGL convention, UE flips on import if asked).
    rgb[..., 1] = 1.0 - rgb[..., 1]
    return Image.fromarray(np.clip(rgb * 255.0 + 0.5, 0, 255).astype(np.uint8), "RGB")


def fft_noise(size, beta, seed, low_cut=1.0):
    rng = np.random.default_rng(seed)
    white = rng.normal(size=(size, size))
    f = np.fft.fft2(white)
    fy = np.fft.fftfreq(size)[:, None] * size
    fx = np.fft.fftfreq(size)[None, :] * size
    r = np.sqrt(fx * fx + fy * fy)
    r[0, 0] = 1.0
    filt = 1.0 / np.power(np.maximum(r, low_cut), beta)
    filt[0, 0] = 0.0
    out = np.real(np.fft.ifft2(f * filt))
    out = (out - out.mean()) / (out.std() * 3.0) * 0.5 + 0.5
    return np.clip(out, 0.0, 1.0)


def noise_texture(size=1024):
    r = fft_noise(size, 1.9, 11, low_cut=2.0)   # broad macro blotches
    g = fft_noise(size, 1.5, 12, low_cut=6.0)   # medium
    b = fft_noise(size, 1.1, 13, low_cut=24.0)  # fine
    rgb = np.stack([r, g, b], axis=-1)
    return Image.fromarray((rgb * 255.0 + 0.5).astype(np.uint8), "RGB")


def grass_clump(path, seed, blades, height, spread, lean, width):
    """Blade-geometry clump. Writes Z-up centimetres (the UE OBJ importer only mirrors Y). UV: u across the blade, v 0 (root) .. 1 (tip)."""
    rng = np.random.default_rng(seed)
    verts, uvs, norms, faces = [], [], [], []
    segs = 4
    for _ in range(blades):
        r = spread * math.sqrt(rng.uniform(0, 1))
        a = rng.uniform(0, 2 * math.pi)
        base = np.array([r * math.cos(a), 0.0, r * math.sin(a)])
        h = height * rng.uniform(0.55, 1.0)
        facing = rng.uniform(0, 2 * math.pi)
        side = np.array([math.cos(facing), 0.0, math.sin(facing)])
        # Lean outward from the clump centre plus a shared wind lean along +X.
        out = np.array([math.cos(a), 0.0, math.sin(a)])
        bend_dir = out * rng.uniform(0.3, 1.0) + np.array([lean, 0.0, 0.0])
        bend = rng.uniform(0.25, 0.6) * h
        w = width * rng.uniform(0.7, 1.3)
        start = len(verts)
        for i in range(segs + 1):
            t = i / segs
            centre = base + np.array([0.0, h * t, 0.0]) + bend_dir * bend * t * t
            half = 0.5 * w * (1.0 - t) ** 0.8 if i < segs else 0.0
            if i < segs:
                verts.append(centre - side * half)
                verts.append(centre + side * half)
                uvs.append((0.0, t))
                uvs.append((1.0, t))
                # Normals tilted toward up so clumps light like the turf beneath them.
                fwd = np.cross(side, np.array([0.0, 1.0, 0.0]))
                nrm = fwd * 0.35 + np.array([0.0, 1.0, 0.0])
                nrm /= np.linalg.norm(nrm)
                norms.append(nrm)
                norms.append(nrm)
            else:
                verts.append(centre)
                uvs.append((0.5, 1.0))
                norms.append(np.array([0.0, 1.0, 0.0]))
        for i in range(segs - 1):
            a0 = start + 2 * i
            faces.append((a0, a0 + 1, a0 + 3))
            faces.append((a0, a0 + 3, a0 + 2))
        tip = start + 2 * segs
        faces.append((tip - 2, tip - 1, tip))
    with open(path, "w", encoding="ascii", newline="\n") as f:
        f.write("# Procedural grass clump (tools/generate_island_art.py), Z-up, centimetres\n")
        f.write("o GrassClump\n")
        for v in verts:
            f.write("v %.4f %.4f %.4f\n" % (v[0], v[2], v[1]))
        for uv in uvs:
            f.write("vt %.4f %.4f\n" % uv)
        for n in norms:
            f.write("vn %.4f %.4f %.4f\n" % (n[0], n[2], n[1]))
        for a, b, c in faces:
            # One-sided triangles; the grass material is two-sided.
            f.write("f %d/%d/%d %d/%d/%d %d/%d/%d\n" % (a + 1, a + 1, a + 1, b + 1, b + 1, b + 1, c + 1, c + 1, c + 1))
    return len(faces)


def main():
    os.makedirs(OUT, exist_ok=True)
    water_normal().save(os.path.join(OUT, "T_WaterNormal.png"))
    noise_texture().save(os.path.join(OUT, "T_IslandNoise.png"))
    tris = [
        grass_clump(os.path.join(OUT, "SM_GrassClump_A.obj"), 21, blades=34, height=38.0, spread=14.0, lean=0.45, width=2.2),
        grass_clump(os.path.join(OUT, "SM_GrassClump_B.obj"), 22, blades=22, height=60.0, spread=12.0, lean=0.6, width=1.8),
        grass_clump(os.path.join(OUT, "SM_GrassClump_C.obj"), 23, blades=40, height=22.0, spread=18.0, lean=0.3, width=2.6),
    ]
    print("grass triangles:", tris)
    print("GENERATE_OK")


if __name__ == "__main__":
    main()
