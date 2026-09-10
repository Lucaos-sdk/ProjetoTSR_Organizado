"""Frozen-weight domain and raster-size checks, separate from training data."""
import numpy as np
from neural_intervention_training import layout
from neural_lighting_scene import render
from neural_lighting_diagnostics import fitted_depth_normals

SCENE_IDS = tuple(range(21000, 21004))
FAMILIES = ('baseline', 'boxes', 'distant', 'source_light')
RESOLUTIONS = ((96, 64), (192, 128), (384, 256))
YAWS = (-.04, 0., .04)


def make_frame(seed, family, width=96, height=64, yaw=0., remove=False, *, noise=0.):
    if family not in FAMILIES:
        raise ValueError(family)
    spheres = [(c.copy(), r, color.copy()) for c, r, color in layout(seed)]
    boxes = []; light = None
    if family == 'boxes':
        remaining = []
        for i, (center, radius, color) in enumerate(spheres):
            if i % 2 == 0:
                half = radius*np.array([.75, 1.25, .75])
                center[1] = half[1]
                boxes.append((center, half, color))
            else:
                remaining.append((center, radius, color))
        spheres = remaining
    elif family == 'distant':
        for center, _, _ in spheres:
            center[2] += 4.
    elif family == 'source_light':
        # Perturb only input illumination; the requested target rig stays fixed.
        # This network has no input expressing an arbitrary desired light rig.
        rng = np.random.default_rng(seed+94021)
        light = ([rng.uniform(-1.5,1.5), rng.uniform(4.,7.), -3.],
                 85*rng.uniform(.7,1.3,3))
    scene, cam = render(seed, yaw=yaw, width=width, height=height, depth_noise=noise,
        spheres_override=[] if remove else spheres, boxes_override=[] if remove else boxes,
        source_light_override=light)
    scene['features'][...,3:6] = fitted_depth_normals(scene['features'][...,6]*30, cam, plane_residual=True)[0]
    return scene, cam
