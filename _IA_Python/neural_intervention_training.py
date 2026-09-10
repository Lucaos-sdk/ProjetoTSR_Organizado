"""Varied layouts and paired supervision for a restricted relighting task."""
import numpy as np
import torch
from neural_lighting_scene import render
from neural_lighting_diagnostics import fitted_depth_normals,receiver_mask

TRAIN_IDS=tuple(range(13000,13064))
VALID_IDS=tuple(range(14000,14012))
TEST_IDS=tuple(range(15000,15012))
CASES=('remove_one','move_one','remove_all')


def layout(seed):
    rng=np.random.default_rng(seed)
    count=int(rng.integers(1,5)); spheres=[]
    for _ in range(count):
        radius=float(rng.uniform(.25,.7))
        for attempt in range(200):
            center=np.array([rng.uniform(-2.4,2.4),radius,rng.uniform(-1.,2.5)])
            if all(np.linalg.norm((center-c)[[0,2]])>radius+r+.15 for c,r,_ in spheres): break
        else: raise RuntimeError('Could not place separated spheres')
        spheres.append((center,radius,rng.uniform(.08,.8,3)))
    return spheres


def change_layout(seed,case):
    spheres=[(c.copy(),r,col.copy()) for c,r,col in layout(seed)]
    index=seed%len(spheres)
    if case=='remove_all': return []
    if case=='remove_one': spheres.pop(index); return spheres
    if case!='move_one': raise ValueError(case)
    rng=np.random.default_rng(seed+93581)
    old,radius,color=spheres[index]
    for _ in range(200):
        center=np.array([rng.uniform(-2.4,2.4),radius,rng.uniform(-1.,2.5)])
        if np.linalg.norm((center-old)[[0,2]])<.9: continue
        if all(i==index or np.linalg.norm((center-c)[[0,2]])>radius+r+.15 for i,(c,r,_) in enumerate(spheres)):
            spheres[index]=(center,radius,color); return spheres
    raise RuntimeError('Could not move sphere')


def make_frame(seed,case='base',yaw=0.,noise=0.):
    spheres=layout(seed) if case=='base' else change_layout(seed,case)
    scene,cam=render(seed,yaw=yaw,depth_noise=noise,spheres_override=spheres)
    normals,_=fitted_depth_normals(scene['features'][...,6]*30,cam,plane_residual=True)
    scene['features'][...,3:6]=normals
    return scene,cam


def masks(a,b):
    receiver=receiver_mask(a,b)
    expected=(b['target']-b['source'])-(a['target']-a['source'])
    changed=receiver&(np.abs(expected).mean(-1)>.005)
    stable=receiver&(np.abs(expected).max(-1)<1e-6)
    return changed.astype(np.float32),stable.astype(np.float32)


def paired_loss(pa,pb,a,b,changed,stable):
    # Algebraically equal to comparing changes in (prediction-source) against
    # changes in (target-source). Excludes pixels changing primary surface.
    difference=((pb-b['target'])-(pa-a['target'])).abs()
    def region(mask):
        return (difference*mask).sum()/(3*mask.sum()).clamp_min(1)
    return .5*region(changed)+.1*region(stable)
