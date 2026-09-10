"""Joint image, intervention and camera supervision; no runtime history."""
import numpy as np
import torch
from bilateral_experiment import tensors
from neural_intervention_training import CASES, make_frame, masks, paired_loss
from neural_lighting_scene import correspondences
from neural_lighting_model import lighting_loss
from neural_lighting_temporal import temporal_error_loss

TRAIN_IDS = tuple(range(16000, 16064))
VALID_IDS = tuple(range(17000, 17012))
TEST_IDS = tuple(range(18000, 18012))
CAMERA_PAIRS = ((-.10, -.05), (.05, .10))
KEYS = ('features', 'source', 'target', 'valid', 'shadow_effect')


def quartet(seed, case, yaws, noise=.0005):
    """Order: base camera 0/1, changed geometry camera 0/1.

    Camera correspondences stay within each static geometry. Object IDs after
    removal/relocation must never be used to reproject across the intervention.
    """
    if case not in CASES or len(yaws) != 2:
        raise ValueError('Expected one intervention and two camera poses')
    rendered = [make_frame(seed, state, yaw=yaw, noise=noise)
                for state in ('base', case) for yaw in yaws]
    frames = [s for s, _ in rendered]
    batch = {k: torch.cat([tensors({k: s[k]})[k] for s in frames]) for k in KEYS}
    regions = []
    for a, b in ((0, 2), (1, 3)):
        regions.append(tuple(torch.from_numpy(m)[None, None] for m in masks(frames[a], frames[b])))
    motion = []
    # Supervise both camera directions, with independently tested visibility.
    for a, b in ((0, 1), (1, 0), (2, 3), (3, 2)):
        iy, ix, visible = correspondences(frames[a], frames[b], rendered[b][1])
        index = torch.from_numpy((iy * frames[b]['source'].shape[1] + ix).astype(np.int64))[None]
        motion.append((a, b, index, torch.from_numpy(visible.astype(np.float32))[None, None]))
    return batch, regions, motion


def losses(pred, example):
    batch, regions, motion = example
    def frame(i):
        return {k: v[i:i+1] for k, v in batch.items()}
    image = sum(lighting_loss(pred[i:i+1], frame(i)) for i in range(4)) / 4
    response = sum(paired_loss(pred[a:a+1], pred[b:b+1], frame(a), frame(b), *region)
                   for (a, b), region in zip(((0, 2), (1, 3)), regions)) / 2
    temporal = sum(temporal_error_loss(pred[a:a+1], batch['target'][a:a+1],
                                     pred[b:b+1], batch['target'][b:b+1], index, visible)
                   for a, b, index, visible in motion) / len(motion)
    return image, response, temporal
