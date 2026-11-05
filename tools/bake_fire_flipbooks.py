"""Bakes the fire flipbooks (UNR-T75) from a real Mantaflow fire simulation in Blender (4.4, headless).

No paid or downloaded art: a gas domain with one burning fuel source (fire + smoke), simulated with
noise up-res, rendered with Cycles volumes through an orthographic camera, then packed into sub-UV
sheets that tools/build_torch_fire.py imports. Every frame is the WHOLE fire at that instant, so a
sprite playing the sheet is a living, licking fire (not a blob); a sprite picks a random start frame.

  SourceArt/FX/Fire/T_FireFlipbook.png   4096x4096, 8x8 cells of 512, 64 frames, seamless loop.
      R = flame emission (the integrated "flame" field along the view, normalised; 1 = white-hot core)
      G = temperature-weighted emission (the hottest gas only: the materials use R vs G for the
          deep-red -> orange -> yellow-white ramp)
      B = smoke / soot density (lets the flame darken at the sooty tips)
      A = coverage (max of the three): soft edges for depth sorting and the field-fire sprites
  SourceArt/FX/Fire/T_SmokeFlipbook.png  2048x2048, 8x8 cells of 256, 64 frames, seamless loop.
      RGB = the smoke lit by a sun from above and the fire's own glow from below (grey; tinted in the
            material), A = opacity. Faded to nothing at the cell's edges so no frame shows the box.

Loop: the sim runs past the loop and the last LOOP_BLEND frames are cross-faded into the first ones
(out[i] = mix(seq[i + N], seq[i], i / LOOP_BLEND)), so frame 63 flows into frame 0 without a pop.

Run once by hand when you want to re-bake (the PNGs are committed; a fresh clone doesn't need Blender).
One Blender process per simulation (a second Mantaflow bake in the same process sometimes fails with
"'NoneType' object has no attribute 'getDataPointer'"; just run it again):
  blender -b --factory-startup -P tools/bake_fire_flipbooks.py -- --sim fire
  blender -b --factory-startup -P tools/bake_fire_flipbooks.py -- --sim smoke
  blender -b --factory-startup -P tools/bake_fire_flipbooks.py -- --stage pack
Options: --stage all|sim|render|pack, --work <dir>, --quick (low-res preview). Work files (the VDB cache,
the EXR frames and a .blend per sim, a few GB) go to <work> (default Saved/FireBake). Renders on the GPU
(OptiX / CUDA) when there is one.
"""
import math
import os
import sys

import bpy
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT = os.path.normpath(os.path.join(HERE, ".."))
OUT = os.path.join(PROJECT, "SourceArt", "FX", "Fire")

ARGS = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []


def arg(name, default):
    return ARGS[ARGS.index(name) + 1] if name in ARGS else default


STAGE = arg("--stage", "all")
QUICK = "--quick" in ARGS
WORK = arg("--work", os.path.join(PROJECT, "Saved", "FireBake"))
ONLY = arg("--sim", "")      # fire | smoke: one sim only

FPS = 30
LOOP = 64                    # frames in the sheet
LOOP_BLEND = 16              # frames cross-faded at the loop seam
# Sim frames before the loop starts (the fire / plume has to reach full height first): per sim, so
# FIRST and LAST are set by use_sim().
FIRST = 0
LAST = 0


def use_sim(spec):
    global FIRST, LAST
    FIRST = spec["warmup"] + 1
    LAST = spec["warmup"] + LOOP + LOOP_BLEND
NOISE_UPRES = 2              # wavelet noise up-res (fine licking detail)

# Domain: 2 x 2 x 2 Blender units, the source at the bottom centre. The camera frames the whole square.
DOMAIN_SIZE = 2.0

# Two simulations. "fire": a tall, licking, turbulent flame from a small fuel source (the flipbook every
# fire in the game is built from). "smoke": a hot, dense, billowing plume (the smoke flipbook).
SIMS = {
    "fire": {
        "warmup": 45,
        "res": 48 if QUICK else 112,
        "px": 128 if QUICK else 512,
        "samples": 16 if QUICK else 48,
        "domain": dict(alpha=0.25, beta=4.2, vorticity=0.35, dissolve_speed=18, use_dissolve_smoke=True,
                       burning_rate=0.3, flame_smoke=0.45, flame_vorticity=1.6, flame_ignition=1.25,
                       flame_max_temp=3.0, noise_strength=2.0, noise_pos_scale=2.6, time_scale=0.85),
        "flow": dict(flow_type="BOTH", fuel_amount=1.1, temperature=2.2, density=0.45, surface_distance=0.5,
                     velocity_normal=0.25, velocity_z=1.5),
        "burner": dict(radius=0.2, z=0.12, squash=0.5),
    },
    "smoke": {
        "warmup": 90,
        "res": 48 if QUICK else 80,
        "px": 128 if QUICK else 256,
        "samples": 24 if QUICK else 96,
        "domain": dict(alpha=1.0, beta=1.4, vorticity=0.18, dissolve_speed=60, use_dissolve_smoke=True,
                       burning_rate=0.75, flame_smoke=1.0, flame_vorticity=0.5, flame_ignition=1.5,
                       flame_max_temp=3.0, noise_strength=1.2, noise_pos_scale=1.6, time_scale=0.7),
        "flow": dict(flow_type="SMOKE", fuel_amount=0.0, temperature=1.4, density=1.6, surface_distance=0.8,
                     velocity_normal=0.2, velocity_z=0.6),
        "burner": dict(radius=0.32, z=0.18, squash=0.5),
    },
}


def sim_dir(name):
    return os.path.join(WORK, name)


def log(msg):
    print("[fire-bake] " + msg, flush=True)


# ----------------------------------------------------------------------------------------------
# Scene
# ----------------------------------------------------------------------------------------------

def clear_scene():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    scene.render.fps = FPS
    scene.frame_start = 1
    scene.frame_end = LAST
    return scene


def make_domain(name, spec):
    bpy.ops.mesh.primitive_cube_add(size=DOMAIN_SIZE, location=(0.0, 0.0, DOMAIN_SIZE * 0.5))
    domain = bpy.context.active_object
    domain.name = "FireDomain"
    mod = domain.modifiers.new("Fluid", "FLUID")
    mod.fluid_type = "DOMAIN"
    ds = mod.domain_settings
    ds.domain_type = "GAS"
    ds.resolution_max = spec["res"]
    ds.use_adaptive_domain = False
    ds.cfl_condition = 2.0
    ds.timesteps_max = 4
    ds.timesteps_min = 1
    ds.use_dissolve_smoke_log = True
    for key, value in spec["domain"].items():
        setattr(ds, key, value)
    ds.use_noise = True
    ds.noise_scale = NOISE_UPRES
    ds.cache_directory = os.path.join(sim_dir(name), "cache")
    ds.cache_type = "ALL"
    ds.cache_data_format = "OPENVDB"
    ds.cache_frame_start = 1
    ds.cache_frame_end = LAST
    # Open at the top and sides: gas leaves instead of piling up against the walls.
    for side in ("use_collision_border_top", "use_collision_border_front", "use_collision_border_back",
                 "use_collision_border_left", "use_collision_border_right"):
        if hasattr(ds, side):
            setattr(ds, side, False)
    if hasattr(ds, "use_collision_border_bottom"):
        ds.use_collision_border_bottom = True
    return domain


def make_flow(spec):
    """A squat, lumpy source: fuel / smoke from its surface, broken up by a drifting cloud texture."""
    b = spec["burner"]
    f = spec["flow"]
    bpy.ops.mesh.primitive_uv_sphere_add(radius=b["radius"], location=(0.0, 0.0, b["z"]), segments=24, ring_count=12)
    flow = bpy.context.active_object
    flow.name = "Burner"
    flow.scale = (1.0, 1.0, b["squash"])
    mod = flow.modifiers.new("Fluid", "FLUID")
    mod.fluid_type = "FLOW"
    fs = mod.flow_settings
    fs.flow_type = f["flow_type"]
    fs.flow_behavior = "INFLOW"
    fs.flow_source = "MESH"
    fs.surface_distance = f["surface_distance"]
    fs.volume_density = 0.0
    if f["flow_type"] != "SMOKE":
        fs.fuel_amount = f["fuel_amount"]
    fs.temperature = f["temperature"]
    fs.density = f["density"]
    fs.smoke_color = (0.35, 0.33, 0.31)
    fs.subframes = 2
    fs.use_initial_velocity = True
    fs.velocity_normal = f["velocity_normal"]
    fs.velocity_coord = (0.0, 0.0, f["velocity_z"])
    # Patchy emission that drifts over time: the fire breathes and its tongues come and go.
    tex = bpy.data.textures.new("FuelNoise", type="CLOUDS")
    tex.noise_scale = 0.16
    tex.noise_depth = 3
    tex.noise_basis = "BLENDER_ORIGINAL"
    fs.use_texture = True
    fs.noise_texture = tex
    fs.texture_map_type = "AUTO"
    fs.texture_size = 1.0
    fs.texture_offset = 0.0
    fs.keyframe_insert("texture_offset", frame=1)
    fs.texture_offset = 7.0
    fs.keyframe_insert("texture_offset", frame=LAST)
    act = flow.animation_data.action if flow.animation_data else None
    if act:
        for fc in getattr(act, "fcurves", []):
            for kp in fc.keyframe_points:
                kp.interpolation = "LINEAR"
    flow.hide_render = True
    return flow


def bake(name, domain, spec):
    os.makedirs(os.path.join(sim_dir(name), "cache"), exist_ok=True)
    bpy.context.view_layer.objects.active = domain
    domain.select_set(True)
    log("%s: baking %d frames at res %d (noise x%d)" % (name, LAST, spec["res"], NOISE_UPRES))
    with bpy.context.temp_override(object=domain, active_object=domain, selected_objects=[domain]):
        result = bpy.ops.fluid.bake_all()
    log("%s: bake %s" % (name, result))


# ----------------------------------------------------------------------------------------------
# Render
# ----------------------------------------------------------------------------------------------

def use_gpu(scene):
    scene.render.engine = "CYCLES"
    try:
        prefs = bpy.context.preferences.addons["cycles"].preferences
        for kind in ("OPTIX", "CUDA"):
            try:
                prefs.compute_device_type = kind
            except TypeError:
                continue
            prefs.get_devices()
            gpus = [d for d in prefs.devices if d.type == kind]
            if gpus:
                for d in prefs.devices:
                    d.use = d.type == kind
                scene.cycles.device = "GPU"
                log("rendering on %s: %s" % (kind, ", ".join(d.name for d in gpus)))
                return
    except Exception as exc:
        log("no GPU (%s), CPU render" % exc)
    scene.cycles.device = "CPU"


def setup_render(scene, px, samples):
    use_gpu(scene)
    scene.cycles.samples = samples
    scene.cycles.use_denoising = False
    scene.cycles.volume_step_rate = 0.5
    scene.cycles.volume_max_steps = 2048
    scene.cycles.max_bounces = 4
    scene.cycles.volume_bounces = 1
    scene.cycles.transparent_max_bounces = 8
    scene.render.film_transparent = True
    scene.render.resolution_x = px
    scene.render.resolution_y = px
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = "OPEN_EXR"
    scene.render.image_settings.color_mode = "RGBA"
    scene.render.image_settings.color_depth = "32"
    scene.view_settings.view_transform = "Standard"
    scene.view_settings.look = "None"
    scene.view_settings.exposure = 0.0
    scene.view_settings.gamma = 1.0
    scene.render.use_compositing = False
    scene.render.use_sequencer = False


def make_camera(scene):
    cam_data = bpy.data.cameras.new("FireCam")
    cam_data.type = "ORTHO"
    cam_data.ortho_scale = DOMAIN_SIZE
    cam = bpy.data.objects.new("FireCam", cam_data)
    scene.collection.objects.link(cam)
    cam.location = (0.0, -10.0, DOMAIN_SIZE * 0.5)
    cam.rotation_euler = (math.radians(90.0), 0.0, 0.0)
    scene.camera = cam
    return cam


def attr(nodes, name):
    a = nodes.new("ShaderNodeAttribute")
    a.attribute_name = name
    a.attribute_type = "GEOMETRY"
    return a


def math_node(nodes, links, op, a, b=None, value=None):
    m = nodes.new("ShaderNodeMath")
    m.operation = op
    links.new(a, m.inputs[0])
    if b is not None:
        links.new(b, m.inputs[1])
    elif value is not None:
        m.inputs[1].default_value = value
    return m.outputs[0]


def volume_material(kind):
    """kind: 'flame' (emission only: R flame, G hot flame, B smoke density, no absorption) or 'smoke' (lit)."""
    mat = bpy.data.materials.new("Vol_" + kind)
    mat.use_nodes = True
    nodes = mat.node_tree.nodes
    links = mat.node_tree.links
    nodes.clear()
    out = nodes.new("ShaderNodeOutputMaterial")
    if kind == "flame":
        flame = attr(nodes, "flame").outputs["Fac"]
        temp = attr(nodes, "temperature").outputs["Fac"]
        dens = attr(nodes, "density").outputs["Fac"]
        # The hot core: flame weighted by how hot the gas is (temperature^2).
        hot = math_node(nodes, links, "MULTIPLY", flame, math_node(nodes, links, "POWER", temp, value=2.0))
        comb = nodes.new("ShaderNodeCombineXYZ")
        links.new(flame, comb.inputs[0])
        links.new(hot, comb.inputs[1])
        links.new(math_node(nodes, links, "MULTIPLY", dens, value=0.25), comb.inputs[2])
        emit = nodes.new("ShaderNodeEmission")
        links.new(comb.outputs[0], emit.inputs["Color"])
        emit.inputs["Strength"].default_value = 1.0
        links.new(emit.outputs[0], out.inputs["Volume"])
    else:
        vol = nodes.new("ShaderNodeVolumePrincipled")
        vol.inputs["Color"].default_value = (0.5, 0.5, 0.5, 1.0)
        links.new(attr(nodes, "density").outputs["Fac"], vol.inputs["Density"])
        vol.inputs["Density"].default_value = 1.0
        mul = math_node(nodes, links, "MULTIPLY", attr(nodes, "density").outputs["Fac"], value=14.0)
        links.new(mul, vol.inputs["Density"])
        vol.inputs["Anisotropy"].default_value = 0.2
        vol.inputs["Blackbody Intensity"].default_value = 0.0
        vol.inputs["Emission Strength"].default_value = 0.0
        links.new(vol.outputs[0], out.inputs["Volume"])
    return mat


def smoke_lights(scene):
    sun_data = bpy.data.lights.new("Sun", type="SUN")
    sun_data.energy = 3.0
    sun = bpy.data.objects.new("Sun", sun_data)
    sun.rotation_euler = (math.radians(35.0), math.radians(-25.0), 0.0)
    scene.collection.objects.link(sun)
    glow_data = bpy.data.lights.new("FireGlow", type="POINT")
    glow_data.energy = 60.0
    glow_data.shadow_soft_size = 0.4
    glow = bpy.data.objects.new("FireGlow", glow_data)
    glow.location = (0.0, -0.2, 0.25)
    scene.collection.objects.link(glow)
    world = bpy.data.worlds.new("World")
    world.use_nodes = True
    bg = world.node_tree.nodes.get("Background")
    bg.inputs["Color"].default_value = (0.18, 0.19, 0.21, 1.0)
    bg.inputs["Strength"].default_value = 0.6
    scene.world = world


def render_pass(scene, domain, name, spec):
    """name: 'fire' renders the flame pass (emission), 'smoke' the lit smoke pass."""
    kind = "flame" if name == "fire" else "smoke"
    setup_render(scene, spec["px"], spec["samples"])
    domain.data.materials.clear()
    domain.data.materials.append(volume_material(kind))
    folder = os.path.join(sim_dir(name), "frames")
    os.makedirs(folder, exist_ok=True)
    for frame in range(FIRST, LAST + 1):
        path = os.path.join(folder, "%04d.exr" % frame)
        if os.path.exists(path):
            continue
        scene.frame_set(frame)
        scene.render.filepath = path
        bpy.ops.render.render(write_still=True)
        log("%s frame %d/%d" % (name, frame, LAST))


def render_all(name, scene, domain, spec):
    make_camera(scene)
    smoke_lights(scene)
    render_pass(scene, domain, name, spec)


# ----------------------------------------------------------------------------------------------
# Pack
# ----------------------------------------------------------------------------------------------

def load_exr(path):
    img = bpy.data.images.load(path, check_existing=False)
    w, h = img.size
    px = np.empty(w * h * 4, dtype=np.float32)
    img.pixels.foreach_get(px)
    bpy.data.images.remove(img)
    return px.reshape(h, w, 4)  # bottom row first (Blender's convention)


def looped(name):
    seq = [load_exr(os.path.join(sim_dir(name), "frames", "%04d.exr" % f)) for f in range(FIRST, LAST + 1)]
    out = []
    for i in range(LOOP):
        if i < LOOP_BLEND:
            w = (i + 0.5) / LOOP_BLEND
            out.append(seq[i] * w + seq[i + LOOP] * (1.0 - w))
        else:
            out.append(seq[i])
    return out


def edge_fade(px, top=0.12, side=0.06, bottom=0.0):
    """1 inside, 0 at the cell's border (array is bottom-row-first)."""
    y = (np.arange(px) + 0.5) / px
    x = y
    fy = np.clip(y / bottom, 0, 1) if bottom > 0 else np.ones_like(y)
    fy = fy * np.clip((1.0 - y) / top, 0, 1)
    fx = np.clip(x / side, 0, 1) * np.clip((1.0 - x) / side, 0, 1)
    f = np.outer(fy, fx)
    return f * f * (3 - 2 * f)


def save_png(rgba, path):
    h, w, _ = rgba.shape
    img = bpy.data.images.new(os.path.basename(path), w, h, alpha=True, float_buffer=False)
    img.colorspace_settings.name = "Non-Color"
    img.pixels.foreach_set(np.clip(rgba, 0, 1).astype(np.float32).ravel())
    img.filepath_raw = path
    img.file_format = "PNG"
    img.save()
    bpy.data.images.remove(img)
    log("wrote %s (%dx%d)" % (path, w, h))


def atlas(frames, cells=8):
    px = frames[0].shape[0]
    sheet = np.zeros((px * cells, px * cells, 4), dtype=np.float32)
    for i, f in enumerate(frames):
        col = i % cells
        row = i // cells                     # row 0 = top of the sheet
        y0 = (cells - 1 - row) * px          # arrays are bottom-row-first
        sheet[y0:y0 + px, col * px:(col + 1) * px] = f
    return sheet


def pack_flame():
    frames = looped("fire")
    px = frames[0].shape[0]
    fade = edge_fade(px, top=0.10, side=0.05)[..., None]
    rgb = [f[..., :3] * fade for f in frames]
    # Normalise on the bright end of the whole loop (not per frame: the fire keeps its breathing).
    # (Every 4th pixel of every frame: plenty for the percentiles, a quarter of the memory.)
    sample = np.concatenate([f[::2, ::2].reshape(-1, 3) for f in rgb])

    def norm(channel, pct):
        values = sample[:, channel]
        values = values[values > 1e-4]
        return float(np.percentile(values, pct)) if values.size else 1.0

    norm_r, norm_g, norm_b = norm(0, 99.7), norm(1, 99.7), norm(2, 99.5)
    del sample
    log("flame norms r=%.4f g=%.4f b=%.4f" % (norm_r, norm_g, norm_b))
    out = []
    for f in rgb:
        r = 1.0 - np.exp(-1.3 * f[..., 0] / norm_r)          # soft shoulder: the core saturates to 1
        g = 1.0 - np.exp(-1.5 * f[..., 1] / norm_g)
        b = 1.0 - np.exp(-1.6 * f[..., 2] / norm_b)
        # Stored with a 1/2.2 curve so the dim red licks keep precision in 8 bits / BC7.
        r, g, b = (np.power(np.clip(c, 0, 1), 1 / 2.2) for c in (r, g, b))
        a = np.maximum(np.maximum(r, b * 0.6), g)
        out.append(np.dstack([r, g, b, a]))
    save_png(atlas(out), os.path.join(OUT, "T_FireFlipbook.png"))


def pack_smoke():
    frames = looped("smoke")
    px = frames[0].shape[0]
    fade = edge_fade(px, top=0.15, side=0.08, bottom=0.25)
    out = []
    for f in frames:
        a = np.clip(f[..., 3] * fade, 0, 1)
        rgb = np.where(f[..., 3:4] > 1e-4, f[..., :3] / np.maximum(f[..., 3:4], 1e-4), 0.0)  # un-premultiply
        rgb = np.power(np.clip(rgb * 1.6, 0, 1), 1 / 2.2)
        out.append(np.dstack([rgb, a]))
    save_png(atlas(out), os.path.join(OUT, "T_SmokeFlipbook.png"))


def main():
    os.makedirs(OUT, exist_ok=True)
    # Mantaflow writes its wavelet noise tile (waveletNoiseTile.bin) to the current directory.
    os.makedirs(WORK, exist_ok=True)
    os.chdir(WORK)
    names = [ONLY] if ONLY else list(SIMS)
    for name in names:
        spec = SIMS[name]
        use_sim(spec)
        # The baked flags live in the .blend: a later --stage render reopens it (a fresh scene would not
        # read the cache back).
        blend = os.path.join(sim_dir(name), "sim.blend")
        if STAGE in ("all", "sim"):
            clear_scene()
            domain = make_domain(name, spec)
            make_flow(spec)
            bake(name, domain, spec)
            bpy.ops.wm.save_as_mainfile(filepath=blend)
        if STAGE in ("all", "render"):
            if STAGE == "render":
                bpy.ops.wm.open_mainfile(filepath=blend)
            scene = bpy.context.scene
            render_all(name, scene, scene.objects["FireDomain"], spec)
    if STAGE in ("all", "pack"):
        if "fire" in names:
            use_sim(SIMS["fire"])
            pack_flame()
        if "smoke" in names:
            use_sim(SIMS["smoke"])
            pack_smoke()
    log("FIRE_BAKE_OK")


main()
