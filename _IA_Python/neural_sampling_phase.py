"""Offline geometry-phase interventions; does not change deployed inference.

Phase is in FULL-RES pixel units, (dx, dy). Color, validity and output lattice
stay fixed. Fractional boxes integrate piecewise-constant source pixels exactly.
Only integer downsampling is supported so phase zero matches PyTorch area.
"""
import math
import torch
from torch.nn import functional as F


def _check(x, shape, phase):
    if len(shape) != 2 or any(n < 1 or int(n) != n for n in shape):
        raise ValueError('Expected positive integer output dimensions')
    if any(n < m or n % m for n, m in zip(x.shape[-2:], shape)):
        raise ValueError('This diagnostic requires integer downsampling ratios')
    if len(phase) != 2 or not all(math.isfinite(p) and abs(p) <= .5 for p in phase):
        raise ValueError('Phase must be finite and within half a source pixel')


def _weights(n, m, phase, x):
    # Coordinates denote pixel boundaries, with border replication outside raster.
    a = torch.arange(m, dtype=x.dtype, device=x.device)*(n//m)+phase
    b = a+n//m
    p = torch.arange(n, dtype=x.dtype, device=x.device)
    w = (torch.minimum(b[:, None], p+1)-torch.maximum(a[:, None], p)).clamp_min(0)
    w[:, 0] += (-a).clamp_min(0)
    w[:, -1] += (b-n).clamp_min(0)
    return w/(n//m)


def reduce_geometry(x, shape, phase=(0., 0.), method='center'):
    _check(x, shape, phase)
    h, w = x.shape[-2:]; oh, ow = shape; dx, dy = phase
    if method == 'center':
        # floor selects the positive-side sample at exact half-pixel ties,
        # matching nearest-exact. grid_sample nearest has different tie rules.
        iy = (((torch.arange(oh, device=x.device)+.5)*(h//oh))+dy).floor().long().clamp(0,h-1)
        ix = (((torch.arange(ow, device=x.device)+.5)*(w//ow))+dx).floor().long().clamp(0,w-1)
        return x.index_select(-2, iy).index_select(-1, ix)
    if method == 'area':
        wy = _weights(h, oh, dy, x); wx = _weights(w, ow, dx, x)
        return torch.matmul(torch.matmul(wy, x), wx.T)
    raise ValueError(method)


def prepare(batch, shape=(64,96)):
    source = F.interpolate(batch['source'], shape, mode='area')
    valid = F.interpolate(batch['valid'], shape, mode='nearest-exact')
    return source, valid


def predict_phase(model, batch, low_geometry, prepared):
    source, valid = prepared
    features = torch.cat([source/(1+source), low_geometry, valid], 1)
    residual = torch.where(valid > 0, .5*model(features).tanh(), 0.)
    lifted = F.interpolate(residual, batch['source'].shape[-2:], mode='bilinear', align_corners=False)
    return torch.where(batch['valid'] > 0, (batch['source']+lifted).clamp_min(0), batch['source'])
