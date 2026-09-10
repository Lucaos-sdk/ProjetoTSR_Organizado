"""Offline magnitude/direction/depth ablations; no deployed pipeline changes."""
import numpy as np
import torch
from neural_sampling_phase import reduce_geometry
from neural_lighting_diagnostics import fitted_depth_normals

VARIANTS = ('center','depth_area','normal_area','normal_unit','magnitude_only',
            'area','area_unit','postfit')


def unit_normal(normal):
    length = torch.linalg.vector_norm(normal,dim=1,keepdim=True)
    # No invented direction for cancellation or missing normals.
    return torch.where(length>1e-6,normal/length.clamp_min(1e-6),0.)


def reduced_camera(camera, source_shape, shape, phase=(0.,0.)):
    sy,sx = (a/b for a,b in zip(source_shape,shape))
    dx,dy = phase
    result = dict(camera)
    result.update(fx=camera['fx']/sx,fy=camera['fy']/sy,
                  cx=(camera['cx']+.5-dx)/sx-.5,cy=(camera['cy']+.5-dy)/sy-.5)
    return result


def geometry_variants(geometry,camera,shape=(64,96),phase=(0.,0.)):
    if geometry.device.type!='cpu' or geometry.shape[0]!=1:
        raise ValueError('CPU single-frame diagnostic only')
    center = reduce_geometry(geometry,shape,phase,'center')
    area = reduce_geometry(geometry,shape,phase,'area')
    cn,cd = center[:,:3],center[:,3:4]
    an,ad = area[:,:3],area[:,3:4]
    un = unit_normal(an)
    magnitude = torch.linalg.vector_norm(an,dim=1,keepdim=True)
    low_camera = reduced_camera(camera,geometry.shape[-2:],shape,phase)
    fitted,_ = fitted_depth_normals(ad[0,0].numpy()*30,low_camera,plane_residual=True)
    fitted = torch.from_numpy(np.ascontiguousarray(fitted,dtype=np.float32)).permute(2,0,1)[None]
    join = lambda n,d: torch.cat([n,d],1)
    return dict(center=center,depth_area=join(cn,ad),normal_area=join(an,cd),
                normal_unit=join(un,cd),magnitude_only=join(unit_normal(cn)*magnitude,cd),
                area=area,area_unit=join(un,ad),postfit=join(fitted,ad))
