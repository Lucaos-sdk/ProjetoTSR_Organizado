"""Supervise the actual regional input/output chain at multiple raster sizes."""
import numpy as np
import torch
from bilateral_experiment import tensors
from neural_generalization_scene import make_frame
from neural_intervention_training import masks
from neural_lighting_scene import correspondences
from neural_joint_training import KEYS, losses

TRAIN_IDS=tuple(range(23000,23032))
VALID_IDS=tuple(range(24000,24006))
TEST_IDS=tuple(range(25000,25008))
TRAIN_SIZES=((96,64),(144,96),(192,128))
EVAL_SIZES=((144,96),(192,128),(384,256))
FAMILIES=('baseline','boxes')


def edge_mask(scene):
    ids=scene['object_id']; edge=np.zeros(ids.shape,dtype=bool)
    h=ids[:,1:]!=ids[:,:-1]; v=ids[1:]!=ids[:-1]
    edge[:,1:]|=h; edge[:,:-1]|=h; edge[1:]|=v; edge[:-1]|=v
    return (edge & (scene['valid']>0)).astype(np.float32)


def quartet(seed,family,width,height):
    yaws=(-.10,-.05) if seed%2==0 else (.05,.10)
    rendered=[make_frame(seed,family,width,height,yaw,remove,noise=.0005)
              for remove in (False,True) for yaw in yaws]
    scenes=[s for s,_ in rendered]
    batch={k:torch.cat([tensors({k:s[k]})[k] for s in scenes]) for k in KEYS}
    regions=[tuple(torch.from_numpy(m)[None,None] for m in masks(scenes[a],scenes[b]))
             for a,b in ((0,2),(1,3))]
    motion=[]
    for a,b in ((0,1),(1,0),(2,3),(3,2)):
        iy,ix,visible=correspondences(scenes[a],scenes[b],rendered[b][1])
        motion.append((a,b,torch.from_numpy((iy*width+ix).astype(np.int64))[None],
                       torch.from_numpy(visible.astype(np.float32))[None,None]))
    edges=torch.cat([torch.from_numpy(edge_mask(s))[None,None] for s in scenes])
    return batch,regions,motion,edges


def regional_losses(pred,example):
    image,response,temporal=losses(pred,example[:3])
    batch,regions,_,edges=example
    error=pred-batch['target']
    edge=(error.abs()*edges).sum()/(3*edges.sum()).clamp_min(1)
    stable=pred.new_zeros(())
    for (a,b),(_,mask) in zip(((0,2),(1,3)),regions):
        stable+=((error[b:b+1]-error[a:a+1]).abs()*mask).sum()/(3*mask.sum()).clamp_min(1)
    return image,response,temporal,edge,stable/2
