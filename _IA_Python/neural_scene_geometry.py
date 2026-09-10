"""Analytic boxes for an offline generalization diagnostic."""
import numpy as np


def box_hit(origin, direction, center, half_extent):
    local = origin - center
    parallel = np.abs(direction) < 1e-12
    outside = np.any(parallel & (np.abs(local) > half_extent), axis=-1)
    a = np.divide(-half_extent-local, direction, out=np.zeros_like(direction), where=~parallel)
    b = np.divide(half_extent-local, direction, out=np.zeros_like(direction), where=~parallel)
    near = np.max(np.where(parallel, -np.inf, np.minimum(a, b)), axis=-1)
    far = np.min(np.where(parallel, np.inf, np.maximum(a, b)), axis=-1)
    distance = np.where(near > 1e-4, near, far)
    valid = ~outside & (far >= near) & (distance > 1e-4) & np.any(~parallel, axis=-1)
    return np.where(valid, distance, np.inf)


def box_normal(points, center, half_extent):
    local = (points-center) / half_extent
    axis = np.argmax(np.abs(local), axis=-1)
    return np.eye(3)[axis] * np.take_along_axis(np.sign(local), axis[..., None], axis=-1)
