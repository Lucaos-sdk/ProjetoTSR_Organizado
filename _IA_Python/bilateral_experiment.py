"""Isolated scalar bilateral-grid experiment, not the HDRnet implementation.

Synthetic height fields + known fog/UI masks. No game buffers or learned assets.
The teacher is a controlled appearance transform, not path-traced ground truth.
"""
from pathlib import Path
import numpy as np
import torch
from torch import nn
from torch.nn import functional as F

TRAIN_IDS = tuple(range(100, 164))
VALID_IDS = tuple(range(1000, 1012))
TEST_IDS = tuple(range(2000, 2012))
MAX_LOG_GAIN = .25


def make_scene(seed, height=96, width=128, pan=0):
    """Orthographic 2.5D fixture. Pan translates world x by integer pixels.

    Normals/depth are analytic and the UI mask is explicit. Neither is promised
    by the present game hook. Same seed at different pans is the same scene.
    """
    rng = np.random.default_rng(seed)
    yy, xx = np.mgrid[:height, :width].astype(np.float32)
    x = (xx + pan - width/2) / (height/2)
    y = (yy - height/2) / (height/2)
    elevation = np.zeros_like(x)
    dx, dy = np.zeros_like(x), np.zeros_like(x)
    for _ in range(7):
        cx, cy = rng.uniform(-1.4, 1.4, 2)
        radius, amplitude = rng.uniform(.18, .6), rng.uniform(-.25, .5)
        bump = amplitude*np.exp(-((x-cx)**2+(y-cy)**2)/(2*radius**2))
        elevation += bump
        dx -= bump*(x-cx)/radius**2
        dy -= bump*(y-cy)/radius**2
    normals = np.stack([-dx, -dy, np.ones_like(x)], -1)
    normals /= np.linalg.norm(normals, axis=-1, keepdims=True)
    depth = np.maximum(3, rng.uniform(10, 80)-10*elevation)
    palette = rng.uniform(.06, .8, (2, 3))
    texture = .5+.5*np.sin(x*rng.uniform(8, 24)+y*rng.uniform(5, 18))
    albedo = palette[0]*texture[..., None]+palette[1]*(1-texture[..., None])
    # Region-dependent light field: not obtainable from a single normal alone.
    # Its spatial signature is also visible in the original shading.
    lx, ly = rng.uniform(-.8, .8, 2)
    light_field = np.exp(-((x-lx)**2+(y-ly)**2)/rng.uniform(.3, 1.2))
    source_light = .35+.45*normals[..., 2]+.25*light_field
    transmittance = np.exp(-depth/rng.uniform(35, 100))
    fog = rng.uniform(.25, .65, 3)
    source = albedo*source_light[..., None]*transmittance[..., None]+fog*(1-transmittance[..., None])
    # Explicit synthetic sky and thin alpha-cutout silhouettes.
    sky = y < -.75
    cutouts = (y < -.45) & (np.sin(25*x+3*np.sin(7*y)) > .5)
    sky &= ~cutouts
    source[sky] = fog
    depth[sky] = 0
    normals[sky] = 0
    ui = (yy < 9) & (xx > width-35)
    source[ui] = np.where(((xx[ui]//3) % 2)[:, None] == 0, .9, .08)
    valid = ~(sky | ui)
    # Known transmission suppresses the *appearance correction*, not true fog
    # decomposition. Full RGB ratios are preserved by a common scalar gain.
    confidence = transmittance*valid
    gain_signal = .7*normals[..., 0]-.35*normals[..., 1]+.8*(light_field-.4)
    log_gain = MAX_LOG_GAIN*np.tanh(gain_signal)*confidence
    target = source*np.exp(log_gain[..., None])
    features = np.concatenate([source/(1+source), normals, (depth/100)[..., None], valid[..., None]], -1)
    heuristic_log = MAX_LOG_GAIN*np.tanh(.7*normals[..., 0]-.35*normals[..., 1]) * np.exp(-depth/60)*valid
    return {k:np.ascontiguousarray(v, dtype=np.float32) for k, v in dict(
        features=features, source=source, target=target, valid=valid,
        confidence=confidence, heuristic=source*np.exp(heuristic_log[..., None]),
        ui=ui, fog=transmittance<.35).items()}


def tensors(scene):
    out = {}
    for key, value in scene.items():
        if value.ndim == 2:
            value = value[..., None]
        out[key] = torch.from_numpy(value.transpose(2, 0, 1).copy()).unsqueeze(0)
    return out


class BilateralNet(nn.Module):
    """64x64x8 -> 2x8x16x16 (log-gain and confidence logits).

    Scalar restricted transform is deliberately less expressive than HDRnet's
    affine RGB matrices. Keeps chromaticity without teaching that constraint.
    """
    def __init__(self):
        super().__init__()
        self.layers = nn.Sequential(nn.Conv2d(8, 16, 3, 2, 1), nn.ReLU(),
                                    nn.Conv2d(16, 24, 3, 2, 1), nn.ReLU(),
                                    nn.Conv2d(24, 24, 3, 1, 1), nn.ReLU(),
                                    nn.Conv2d(24, 16, 1))
        nn.init.zeros_(self.layers[-1].weight)
        nn.init.zeros_(self.layers[-1].bias)

    def forward(self, low):
        return self.layers(low).reshape(-1, 2, 8, 16, 16)


class PointNet(nn.Module):
    """Ablation: same inputs/teacher but no spatial neighborhood, not V2.3."""
    def __init__(self):
        super().__init__()
        self.layers = nn.Sequential(nn.Conv2d(8, 24, 1), nn.ReLU(), nn.Conv2d(24, 2, 1))
        nn.init.zeros_(self.layers[-1].weight)
        nn.init.zeros_(self.layers[-1].bias)

    def forward(self, features):
        return self.layers(features)


class PointCapacityNet(PointNet):
    """1x1 control with 10,271 parameters vs the grid CNN's 10,256.

    Separates neighborhood access from the advantage of merely more weights.
    """
    def __init__(self):
        nn.Module.__init__(self)
        self.layers = nn.Sequential(nn.Conv2d(8, 96, 1), nn.ReLU(),
                                    nn.Conv2d(96, 95, 1), nn.ReLU(), nn.Conv2d(95, 2, 1))
        nn.init.zeros_(self.layers[-1].weight)
        nn.init.zeros_(self.layers[-1].bias)


def slice_grid(grid, source):
    """Trilinear x/y/luminance sampling, clamped, align_corners=False.

    Third grid axis is intensity, not geometric depth. No temporal memory.
    """
    n, _, h, w = source.shape
    x = (torch.arange(w, device=source.device, dtype=source.dtype)+.5)*2/w-1
    y = (torch.arange(h, device=source.device, dtype=source.dtype)+.5)*2/h-1
    yy, xx = torch.meshgrid(y, x, indexing='ij')
    luma = .2126*source[:, 0]+.7152*source[:, 1]+.0722*source[:, 2]
    guide = luma.clamp_min(0)/(1+luma.clamp_min(0))
    coords = torch.stack([xx.expand(n, h, w), yy.expand(n, h, w), 2*guide-1], -1).unsqueeze(1)
    return F.grid_sample(grid, coords, mode='bilinear', padding_mode='border', align_corners=False).squeeze(2)


def compose(coeff, source, valid):
    confidence = coeff[:, 1:2].sigmoid()
    delta = MAX_LOG_GAIN*coeff[:, :1].tanh()*confidence*valid
    result = source*delta.exp()
    return torch.where(valid > 0, result, source), confidence


def predict(model, batch):
    if isinstance(model, BilateralNet):
        # Training prepack; GPU implementation must also preserve this contract.
        low = F.interpolate(batch['features'], (64, 64), mode='area')
        coeff = slice_grid(model(low), batch['source'])
    else:
        coeff = model(batch['features'])
    return compose(coeff, batch['source'], batch['valid'])


def objective(prediction, confidence, batch):
    error = prediction-batch['target']
    color = (torch.sqrt(error.square()+1e-6)-.001).mean()
    gradient = (error[:, :, 1:]-error[:, :, :-1]).abs().mean() + (error[:, :, :, 1:]-error[:, :, :, :-1]).abs().mean()
    # Confidence is supervised; otherwise zero confidence could hide corrections.
    return color+.1*gradient+.015*(confidence-batch['confidence']).abs().mean()


def slice_numpy(grid, source):
    """Independent eight-corner reference, for correctness tests only."""
    c, d, gh, gw = grid.shape
    h, w, _ = source.shape
    result = np.zeros((h, w, c), np.float64)
    luma = np.maximum(source @ np.array([.2126, .7152, .0722]), 0)
    guide = luma/(1+luma)
    for y in range(h):
        for x in range(w):
            loc = [(x+.5)*gw/w-.5, (y+.5)*gh/h-.5, guide[y, x]*d-.5]
            base = np.floor(loc).astype(int)
            frac = np.array(loc)-base
            for iz in range(2):
                for iy in range(2):
                    for ix in range(2):
                        weight = np.prod([frac[j] if bit else 1-frac[j] for j, bit in enumerate((ix, iy, iz))])
                        result[y, x] += weight*grid[:, np.clip(base[2]+iz, 0, d-1), np.clip(base[1]+iy, 0, gh-1), np.clip(base[0]+ix, 0, gw-1)]
    return result
