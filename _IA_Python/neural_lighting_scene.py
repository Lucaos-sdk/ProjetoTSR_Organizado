"""Paired perspective renders for learning a change of DIRECT illumination.

Analytic sphere/plane geometry, Lambertian surfaces and shadow rays to sampled
lights. No path-traced indirect light, recovered game materials or external assets.
Ground truth has full geometry; the network only sees color and depth context.
"""
import numpy as np
from neural_scene_geometry import box_hit, box_normal

TRAIN_IDS = tuple(range(6000, 6064))
VALID_IDS = tuple(range(7000, 7012))
TEST_IDS = tuple(range(8000, 8012))


def normalize(value):
    return value/np.maximum(np.linalg.norm(value, axis=-1, keepdims=True), 1e-12)


def sphere_hit(origin, direction, center, radius):
    """Nearest positive intersection; direction must be normalized."""
    oc = origin-center
    b = np.sum(oc*direction, -1)
    c = np.sum(oc*oc, -1)-radius*radius
    discriminant = b*b-c
    root = np.sqrt(np.maximum(discriminant, 0))
    near, far = -b-root, -b+root
    distance = np.where(near > 1e-4, near, far)
    return np.where((discriminant >= 0) & (distance > 1e-4), distance, np.inf)


def scene_spec(seed):
    rng = np.random.default_rng(seed)
    spheres = []
    # Three separate footprints; same scene for all camera poses.
    for x in (-1.65, 0., 1.65):
        radius = rng.uniform(.35, .75)
        center = np.array([x+rng.uniform(-.2,.2),radius,rng.uniform(-.7,1.4)])
        spheres.append((center,radius,rng.uniform(.08,.8,3)))
    return spheres, rng.uniform(.2,.65,3), rng.uniform(9.,20.)


def camera(height, width, yaw=0.):
    origin = np.array([.3*np.sin(yaw),2.4,-6.5])
    forward = normalize(np.array([0.,.7,0.])-origin)
    # Rotation of the viewing direction really changes the projection.
    rotation = np.array([[np.cos(yaw),0,np.sin(yaw)],[0,1,0],[-np.sin(yaw),0,np.cos(yaw)]])
    forward = rotation@forward
    right = normalize(np.cross(np.array([0.,1.,0.]),forward))
    up = normalize(np.cross(forward,right))
    basis = np.stack([right,up,forward],axis=1)
    fy = height/(2*np.tan(np.deg2rad(54)/2))
    return dict(origin=origin,basis=basis,fx=fy,fy=fy,cx=(width-1)/2,cy=(height-1)/2)


def reconstruct_positions(depth, cam):
    h,w = depth.shape
    y,x = np.mgrid[:h,:w]
    local = np.stack([(x-cam['cx'])/cam['fx'],-(y-cam['cy'])/cam['fy'],np.ones_like(x)],-1)*depth[...,None]
    return local@cam['basis'].T+cam['origin']


def depth_normals(depth, cam):
    """Shorter one-sided position derivative, rejecting discontinuities.

    General edge selection also seen in Matheus' spatial shader, implemented
    independently with exact fixture intrinsics. This is not V2.3's 5x5 fit.
    """
    p = reconstruct_positions(depth,cam)
    pad = np.pad(p,((1,1),(1,1),(0,0)),mode='edge')
    z = np.pad(depth,1,mode='edge')
    right,left = pad[1:-1,2:]-p,p-pad[1:-1,:-2]
    down,up = pad[2:,1:-1]-p,p-pad[:-2,1:-1]
    def select(a,b,za,zb):
        va = (za > 0) & (np.abs(za-depth) < .1*np.maximum(depth,1e-6))
        vb = (zb > 0) & (np.abs(zb-depth) < .1*np.maximum(depth,1e-6))
        choose = va & (~vb | (np.sum(a*a,-1) < np.sum(b*b,-1)))
        return np.where(choose[...,None],a,b), va|vb
    dx,vx = select(right,left,z[1:-1,2:],z[1:-1,:-2])
    dy,vy = select(down,up,z[2:,1:-1],z[:-2,1:-1])
    n = normalize(np.cross(dx,dy))
    n = np.where((np.sum(n*(p-cam['origin']),-1)>0)[...,None],-n,n)
    valid = (depth>0)&vx&vy&(np.linalg.norm(n,axis=-1)>.5)
    return np.where(valid[...,None],n,0),valid


def direct_light(points, normals, albedo, spheres, center, energy, *, boxes=()):
    total = np.zeros_like(points)
    unoccluded = np.zeros_like(points)
    visibility = np.zeros(points.shape[:2])
    # Four deterministic area-light samples. This is a low-sample reference,
    # not converged photorealistic path tracing.
    for ox,oz in ((-.22,-.22),(.22,-.22),(-.22,.22),(.22,.22)):
        light = np.asarray(center)+[ox,0,oz]
        vector = light-points
        distance = np.linalg.norm(vector,axis=-1)
        direction = vector/np.maximum(distance[...,None],1e-8)
        facing = np.maximum(np.sum(normals*direction,-1),0)
        visible = np.ones_like(distance,dtype=bool)
        start = points+normals*1e-3
        for sphere,radius,_ in spheres:
            hit = sphere_hit(start,direction,sphere,radius)
            visible &= hit >= distance-2e-3
        for box,half_extent,_ in boxes:
            hit = box_hit(start,direction,box,half_extent)
            visible &= hit >= distance-2e-3
        irradiance = np.asarray(energy)*facing[...,None]/np.maximum(distance[...,None]**2,.01)
        contribution = albedo/np.pi*irradiance/4
        total += contribution*visible[...,None]
        unoccluded += contribution
        visibility += visible/4
    return total,unoccluded,visibility


def render(seed, yaw=0., height=64, width=96, depth_noise=0., *, spheres_override=None,
           boxes_override=None, source_light_override=None):
    spheres,ground,fog_length = scene_spec(seed)
    # Counterfactual geometry only: preserve seed-derived ground, fog and lights.
    # None is byte-compatible with the original fixture; [] removes all spheres.
    if spheres_override is not None:
        spheres=[]
        for center,radius,color in spheres_override:
            center=np.asarray(center,dtype=float); color=np.asarray(color,dtype=float)
            if center.shape!=(3,) or color.shape!=(3,) or not np.isfinite(center).all() or not np.isfinite(color).all() or not np.isfinite(radius) or radius<=0:
                raise ValueError('Invalid sphere override')
            spheres.append((center.copy(),float(radius),color.copy()))
    boxes=[]
    for center,half_extent,color in (() if boxes_override is None else boxes_override):
        center=np.asarray(center,dtype=float); half_extent=np.asarray(half_extent,dtype=float); color=np.asarray(color,dtype=float)
        if any(v.shape!=(3,) or not np.isfinite(v).all() for v in (center,half_extent,color)) or np.any(half_extent<=0):
            raise ValueError('Invalid box override')
        boxes.append((center.copy(),half_extent.copy(),color.copy()))
    source_center,source_energy=[-3.,6.,-3.],[85.,85.,85.]
    if source_light_override is not None:
        source_center,source_energy=(np.asarray(v,dtype=float) for v in source_light_override)
        if any(v.shape!=(3,) or not np.isfinite(v).all() for v in (source_center,source_energy)) or np.any(source_energy<0):
            raise ValueError('Invalid source light')
    cam = camera(height,width,yaw)
    y,x = np.mgrid[:height,:width]
    directions = normalize(np.stack([(x-cam['cx'])/cam['fx'],-(y-cam['cy'])/cam['fy'],np.ones_like(x)],-1)@cam['basis'].T)
    origins = np.broadcast_to(cam['origin'],directions.shape)
    # Ground plane y=0. Analytic primary rays, not 2D image transforms.
    denominator = directions[...,1]
    ground_t = np.divide(-cam['origin'][1],denominator,out=np.full((height,width),np.inf),where=denominator < -1e-6)
    distance = np.where(ground_t>0,ground_t,np.inf)
    ids = np.where(np.isfinite(distance),0,-1)
    for index,(center,radius,_) in enumerate(spheres,1):
        candidate = sphere_hit(origins,directions,center,radius)
        closer = candidate < distance
        distance = np.where(closer,candidate,distance)
        ids = np.where(closer,index,ids)
    for index,(center,half_extent,_) in enumerate(boxes,len(spheres)+1):
        candidate = box_hit(origins,directions,center,half_extent)
        closer = candidate < distance
        distance = np.where(closer,candidate,distance)
        ids = np.where(closer,index,ids)
    valid = np.isfinite(distance)&(distance<35.)
    ids = np.where(valid,ids,-1)
    points = origins+directions*np.where(valid,distance,0)[...,None]
    normals = np.zeros_like(points); normals[...,1]=1
    checker = ((np.floor(points[...,0]*2)+np.floor(points[...,2]*2))%2)*.15+.85
    albedo = ground*checker[...,None]
    for index,(center,radius,color) in enumerate(spheres,1):
        mask = ids==index
        normals[mask] = (points[mask]-center)/radius
        albedo[mask] = color
    for index,(center,half_extent,color) in enumerate(boxes,len(spheres)+1):
        mask = ids==index
        normals[mask] = box_normal(points[mask],center,half_extent)
        albedo[mask] = color
    normals[~valid]=0
    # Input already has shading and cast shadows. Reference changes the light
    # rig, so new shadows are not recoverable by a global exposure adjustment.
    base,_,_ = direct_light(points,normals,albedo,spheres,source_center,source_energy,boxes=boxes)
    key,key_free,key_visibility = direct_light(points,normals,albedo,spheres,[3.,5.,-1.],[110.,88.,68.],boxes=boxes)
    fill,_,_ = direct_light(points,normals,albedo,spheres,[-3.,3.,2.],[16.,24.,40.],boxes=boxes)
    source = albedo*.12+base
    target = albedo*.12+key+fill
    target_free = albedo*.12+key_free+fill
    transmission = np.exp(-np.where(valid,distance,0)/fog_length)
    fog = np.array([.26,.32,.4])
    for rgb in (source,target,target_free):
        rgb[:] = rgb*transmission[...,None]+fog*(1-transmission[...,None])
        rgb[~valid] = fog
    depth = np.sum((points-cam['origin'])*cam['basis'][:,2],-1)
    depth[~valid]=0
    observed = depth.copy()
    if depth_noise:
        rng = np.random.default_rng(np.random.SeedSequence([seed,round((yaw+1)*10000)]))
        observed *= np.maximum(.1,1+rng.normal(0,depth_noise,depth.shape))
    reconstructed,normal_valid = depth_normals(observed,cam)
    ui = (y < 5)&(x > width-20)
    for rgb in (source,target,target_free):
        rgb[ui] = np.where(((x[ui]//2)%2)[:,None],.85,.04)
    effect_valid = valid&~ui
    features = np.concatenate([source/(1+source),reconstructed,(observed/30)[...,None],effect_valid[...,None]],-1)
    arrays = dict(source=source,target=target,target_unshadowed=target_free,features=features,
        valid=effect_valid,depth=depth,normal=normals,normal_valid=normal_valid,
        world=points,object_id=ids,shadow=(key_visibility<.99)&effect_valid,
        shadow_effect=np.abs(target-target_free).mean(-1),ui=ui,
        clean_geometry=np.concatenate([normals,(depth/30)[...,None]],-1))
    return {key:np.ascontiguousarray(value,dtype=np.float32) for key,value in arrays.items()},cam


def project(world, cam):
    local = (world-cam['origin'])@cam['basis']
    safe = np.maximum(local[...,2],1e-8)
    return np.stack([cam['fx']*local[...,0]/safe+cam['cx'],
                     -cam['fy']*local[...,1]/safe+cam['cy']],-1),local[...,2]


def correspondences(a, b, camera_b):
    """Nearest pixel correspondences with depth/ID and subpixel-distance guards.

    Used for a sampled diagnostic, NOT a production temporal/history buffer.
    """
    h,w = b['depth'].shape
    xy,z = project(a['world'],camera_b)
    ij = np.rint(xy).astype(np.int64)
    inside = (ij[...,0]>=0)&(ij[...,0]<w)&(ij[...,1]>=0)&(ij[...,1]<h)&(z>0)
    ix,iy = ij[...,0].clip(0,w-1),ij[...,1].clip(0,h-1)
    visible = inside&(a['valid']>0)&(b['valid'][iy,ix]>0)
    visible &= np.abs(b['depth'][iy,ix]-z) < .01*np.maximum(z,.1)
    visible &= a['object_id']==b['object_id'][iy,ix]
    visible &= np.linalg.norm(xy-ij,axis=-1)<.35
    return iy,ix,visible
