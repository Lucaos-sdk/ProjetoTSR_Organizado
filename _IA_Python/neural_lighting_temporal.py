"""Training-only reprojection of errors against each frame's own reference."""
import torch


def temporal_error_loss(pred_a,target_a,pred_b,target_b,index,visible):
    n,c,h,w=pred_a.shape
    error_a=pred_a-target_a
    error_b=(pred_b-target_b).flatten(2)
    mapped=torch.gather(error_b,2,index.reshape(n,1,-1).expand(-1,c,-1)).reshape(n,c,h,w)
    difference=(error_a-mapped).abs()*visible
    return difference.sum()/(visible.sum()*c).clamp_min(1)
