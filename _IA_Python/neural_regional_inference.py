"""Fixed vertical analysis scale; lift only the learned RGB correction."""
import torch
from torch.nn import functional as F
from neural_lighting_model import predict_lighting


def predict_regional(model, batch, internal_height=64):
    if internal_height < 1:
        raise ValueError('internal_height must be positive')
    source=batch['source']; height,width=source.shape[-2:]
    if height <= internal_height:
        return predict_lighting(model,batch)
    shape=(internal_height,max(1,round(width*internal_height/height)))
    low_source=F.interpolate(source,shape,mode='area')
    # World normals and camera-depth context use sample centers, avoiding
    # averaging two unrelated surfaces at a depth discontinuity. No oracle data.
    low_geometry=F.interpolate(batch['features'][:,3:7],shape,mode='nearest-exact')
    low_valid=F.interpolate(batch['valid'],shape,mode='nearest-exact')
    features=torch.cat([low_source/(1+low_source),low_geometry,low_valid],1)
    residual=torch.where(low_valid>0,.5*model(features).tanh(),0.)
    lifted=F.interpolate(residual,(height,width),mode='bilinear',align_corners=False)
    return torch.where(batch['valid']>0,(source+lifted).clamp_min(0),source)
