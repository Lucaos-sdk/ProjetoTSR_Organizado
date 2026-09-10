"""Fixed-weight causal-response diagnostics, not a new training objective."""
import numpy as np
from neural_lighting_scene import scene_spec,render,depth_normals,reconstruct_positions,normalize

CASES=('remove_middle','move_middle_left','move_middle_right','remove_all')


def intervention(seed,case):
    original,_,_=scene_spec(seed)
    spheres=[(c.copy(),r,col.copy()) for c,r,col in original]
    if case=='remove_middle': spheres.pop(1)
    elif case=='remove_all': spheres=[]
    elif case in ('move_middle_left','move_middle_right'):
        # Keep the same object/material/radius, change only its ground footprint.
        spheres[1][0][0]+= -1.05 if case.endswith('left') else 1.05
        spheres[1][0][2]-=.8
    else: raise ValueError(f'Unknown intervention {case}')
    return spheres


def receiver_mask(a,b,radius=2):
    mask=(a['object_id']==0)&(b['object_id']==0)&(a['valid']>0)&(b['valid']>0)
    # Remove primary-visibility boundaries from the receiver measurement.
    padded=np.pad(mask,radius,constant_values=False)
    windows=np.lib.stride_tricks.sliding_window_view(padded,(2*radius+1,2*radius+1))
    return windows.all(axis=(-1,-2))


def response_metrics(pred_a,pred_b,a,b):
    receiver=receiver_mask(a,b)
    expected=(b['target']-b['source'])-(a['target']-a['source'])
    actual=(pred_b-b['source'])-(pred_a-a['source'])
    changed=receiver&(np.abs(expected).mean(-1)>.005)
    unchanged=receiver&(np.abs(expected).max(-1)<1e-6)
    row=dict(receiver_pixels=int(receiver.sum()),changed_pixels=int(changed.sum()),
             unchanged_pixels=int(unchanged.sum()),
             unchanged_residual_drift=float(np.abs(actual[unchanged]).mean()) if unchanged.any() else None)
    if changed.any():
        x,y=expected[changed].astype(np.float64),actual[changed].astype(np.float64)
        dot=float((x*y).sum()); energy=float((x*x).sum()); predicted_energy=float((y*y).sum())
        row.update(response_mae=float(np.abs(y-x).mean()),zero_response_mae=float(np.abs(x).mean()),
                   response_gain=dot/max(energy,1e-12),
                   cosine=dot/max(np.sqrt(energy*predicted_energy),1e-12))
    else:
        row.update(response_mae=None,zero_response_mae=None,response_gain=None,cosine=None)
    return row,expected,actual,changed


def normal_variant(scene,oracle):
    result={k:v.copy() for k,v in scene.items()}
    if oracle:
        # Deliberate privileged-data diagnostic, never production input.
        result['features'][...,3:6]=result['normal']
    return result


def fitted_depth_normals(depth,cam,relative_limit=.02,plane_residual=False):
    """Experimental CPU counterpart of our V2.3 inverse-depth plane fit.

    Exact perspective intrinsics, 5x5 binomial weights, rejected depth jumps.
    Returns an observed-data estimate, not an analytic normal from the renderer.
    """
    fallback,valid=depth_normals(depth,cam)
    h,w=depth.shape
    padded=np.pad(depth,2,constant_values=0)
    center_q=np.divide(1.,depth,out=np.zeros_like(depth,dtype=np.float64),where=depth>0)
    if plane_residual:
        positions=reconstruct_positions(depth,cam)-cam['origin']
        denominator=np.sum(fallback*positions,-1)
        local_normal=fallback@cam['basis']
        # Inverse depth is affine on a perspective plane. Judge deviation from
        # the estimated plane, not its legitimate depth slope across pixels.
        ax=np.divide(local_normal[...,0],cam['fx']*denominator,out=np.zeros_like(depth,dtype=float),where=np.abs(denominator)>1e-8)
        ay=np.divide(-local_normal[...,1],cam['fy']*denominator,out=np.zeros_like(depth,dtype=float),where=np.abs(denominator)>1e-8)
    total,sx,sy,sq,sxx,sxy,syy,sxq,syq=[np.zeros((h,w),np.float64) for _ in range(9)]
    weights=(1,4,6,4,1)
    for dy in range(-2,3):
        for dx in range(-2,3):
            z=padded[dy+2:dy+2+h,dx+2:dx+2+w]
            q=np.divide(1.,z,out=np.zeros_like(z,dtype=np.float64),where=z>0)-center_q
            residual=np.abs(q-ax*dx-ay*dy)<=relative_limit*center_q if plane_residual else np.abs(z-depth)<=relative_limit*depth
            mask=(depth>0)&(z>0)&residual
            weight=weights[dx+2]*weights[dy+2]*mask
            total+=weight; sx+=weight*dx; sy+=weight*dy; sq+=weight*q
            sxx+=weight*dx*dx; sxy+=weight*dx*dy; syy+=weight*dy*dy
            sxq+=weight*dx*q; syq+=weight*dy*q
    inv=1/np.maximum(total,1)
    mx,my,mq=sx*inv,sy*inv,sq*inv
    xx,xy,yy=sxx*inv-mx*mx,sxy*inv-mx*my,syy*inv-my*my
    xq,yq=sxq*inv-mx*mq,syq*inv-my*mq
    determinant=xx*yy-xy*xy
    safe=np.maximum(determinant,1e-8)
    a,b=(xq*yy-yq*xy)/safe,(yq*xx-xq*xy)/safe
    q=center_q+mq-a*mx-b*my
    y,x=np.mgrid[:h,:w]
    local=np.stack([a*cam['fx'],-b*cam['fy'],q-a*(x-cam['cx'])-b*(y-cam['cy'])],-1)
    fitted=normalize(local@cam['basis'].T)
    positions=reconstruct_positions(depth,cam)
    fitted=np.where((np.sum(fitted*(positions-cam['origin']),-1)>0)[...,None],-fitted,fitted)
    use=valid&(determinant>1e-4)&(q>0)&(np.abs(q-center_q)<=.02*center_q)
    use&=np.sum(fitted*fallback,-1)>.2
    return np.where(use[...,None],fitted,fallback),use
