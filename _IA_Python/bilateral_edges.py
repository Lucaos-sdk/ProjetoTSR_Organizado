"""Offline edge/robustness ablation. No game hook or runtime dependency.

The guided filter is our masked scalar implementation of the local linear
model in He et al., Guided Image Filtering (ECCV 2010). It filters log gain,
never the RGB image. A luminance guide cannot guarantee geometry preservation.
"""
import numpy as np
import torch
from torch.nn import functional as F
from bilateral_experiment import MAX_LOG_GAIN, make_scene, slice_grid

TRAIN_IDS = tuple(range(3000, 3064))
VALID_IDS = tuple(range(4000, 4012))
TEST_IDS = tuple(range(5000, 5012))
CASES = ('clean', 'local_exposure', 'geometry_noise', 'combined')


def stress_scene(seed, case='clean', pan=0, phase=0., height=96, width=128):
    """Same controlled teacher; exposure affects both source and reference.

    Geometry noise affects context only; clean geometry weights the training
    loss. Known UI/sky masks remain oracle inputs. This is NOT 3D rotation.
    """
    if case not in CASES:
        raise ValueError(f'Unknown stress case: {case}')
    scene = make_scene(seed, height, width, pan)
    if case in ('local_exposure', 'combined'):
        yy, xx = np.mgrid[:height, :width].astype(np.float32)
        field = np.exp(.6*np.sin((xx+pan)*2/width + yy/height + phase))
        field = np.where(scene['valid'] > 0, field, 1.)
        for name in ('source', 'target', 'heuristic'):
            scene[name] = np.ascontiguousarray(scene[name]*field[..., None], dtype=np.float32)
    features = scene['features']
    features[..., :3] = scene['source']/(1+scene['source'])
    scene['clean_geometry'] = features[..., 3:7].copy()
    if case in ('geometry_noise', 'combined'):
        # Changing pan/phase produces new sensor noise, not world-locked noise.
        rng = np.random.default_rng(np.random.SeedSequence([seed, pan, round(phase*1000)]))
        normals = features[..., 3:6] + rng.normal(0, .025, (height, width, 3))
        normals /= np.maximum(np.linalg.norm(normals, axis=-1, keepdims=True), 1e-8)
        normals[features[..., 6] == 0] = 0
        features[..., 3:6] = normals
        features[..., 6] *= np.maximum(.1, 1+rng.normal(0, .01, (height, width)))
    return scene


def sobel(value):
    kernel = value.new_tensor([[-1, 0, 1], [-2, 0, 2], [-1, 0, 1]])/8
    channels = value.shape[1]
    padded = F.pad(value, (1, 1, 1, 1), mode='replicate')
    return (F.conv2d(padded, kernel[None, None].expand(channels, 1, 3, 3), groups=channels),
            F.conv2d(padded, kernel.T[None, None].expand(channels, 1, 3, 3), groups=channels))


def geometry_weights(clean_geometry):
    """Bounded 1..4 weights; detached reference normals + normalized depth."""
    gx, gy = sobel(clean_geometry.detach())
    normal_edge = (gx[:, :3].abs()+gy[:, :3].abs()).mean(1, keepdim=True)
    depth_edge = gx[:, 3:4].abs()+gy[:, 3:4].abs()
    return 1+3*(normal_edge+8*depth_edge).clamp(0, 1)


def edge_error(error, clean_geometry):
    gx, gy = sobel(error)
    weights = geometry_weights(clean_geometry)
    return ((gx.abs()+gy.abs())*weights).sum()/(weights.sum()*error.shape[1])


def guided_log_gain(guide, signal, valid, radius=2, epsilon=1e-3):
    """Masked two-stage local regression, clipped windows at image borders.

    Invalid samples enter neither regression nor coefficient averaging.
    Disconnected valid regions within a window can still influence each other.
    """
    if radius < 0 or epsilon <= 0:
        raise ValueError('radius must be nonnegative and epsilon positive')
    kernel = 2*radius+1
    def box(value):
        return F.avg_pool2d(value, kernel, stride=1, padding=radius, count_include_pad=True)
    count = box(valid)
    def mean(value):
        return box(value*valid)/count.clamp_min(1e-12)
    mi, mp = mean(guide), mean(signal)
    variance = (mean(guide.square())-mi.square()).clamp_min(0)
    covariance = mean(guide*signal)-mi*mp
    a = covariance/(variance+epsilon)
    b = mp-a*mi
    result = mean(a)*guide+mean(b)
    return torch.where(valid > 0, result.clamp(-MAX_LOG_GAIN, MAX_LOG_GAIN), 0.)


def predict_variant(model, batch, guided=False):
    low = F.interpolate(batch['features'], (64, 64), mode='area')
    coeff = slice_grid(model(low), batch['source'])
    confidence = coeff[:, 1:2].sigmoid()
    delta = MAX_LOG_GAIN*coeff[:, :1].tanh()*confidence*batch['valid']
    if guided:
        rgb = batch['source']
        luma = (.2126*rgb[:, :1]+.7152*rgb[:, 1:2]+.0722*rgb[:, 2:3]).clamp_min(0)
        delta = guided_log_gain(luma/(1+luma), delta, batch['valid'])
    return torch.where(batch['valid'] > 0, batch['source']*delta.exp(), batch['source']), confidence
