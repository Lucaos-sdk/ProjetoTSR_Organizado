"""Separate geometry magnitude/direction/depth using frozen networks."""
import argparse
import json
from pathlib import Path
import time
import hashlib
import numpy as np
import torch
from diagnose_neural_sampling_phase import (ROOT,PHASES,YAWS,PAIRS,FAMILIES,SIZES,
    SEEDS,LightingNet,make_frame,tensors,prepare,predict_phase,predict_regional,
    correspondences,metric,pooled,as_image,digest,write_json)
from neural_geometry_ablation import geometry_variants,VARIANTS

IDS=tuple(range(27000,27004))


def normal_change(a,b,valid):
    a=as_image(a)[:,:,:3]; b=as_image(b)[:,:,:3]
    la=np.linalg.norm(a,axis=-1); lb=np.linalg.norm(b,axis=-1)
    eligible=valid&(la>1e-6)&(lb>1e-6)
    angle=np.arctan2(np.linalg.norm(np.cross(a,b),axis=-1),(a*b).sum(-1))
    return dict(components=metric(np.abs(a-b),valid),length=metric(np.abs(la-lb),valid),
                angle_radians=metric(angle,eligible),excluded_angles=int((valid&~eligible).sum()))


def compare(candidate,reference):
    ratios={k:candidate[k]/reference[k] if reference[k] else None
            for k in ('phase','image','micro','standard')}
    checks={k:r is not None and r <= (1.05 if k=='image' else .8) for k,r in ratios.items()}
    return dict(ratios=ratios,checks=checks,passed=all(checks.values()))


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--out',type=Path,default=ROOT/'artifacts/neural-geometry-ablation-v1')
    args=parser.parse_args(); args.out.mkdir(parents=True,exist_ok=False)
    torch.set_num_threads(2); torch.use_deterministic_algorithms(True)
    models={}; paths={}; hashes={}
    prior=json.loads((ROOT/'artifacts/neural-balance-v1/training.json').read_text())
    trained=json.loads((ROOT/'artifacts/neural-regional-training-v1/training.json').read_text())
    for seed in SEEDS:
        for kind in ('frozen','joint'):
            n=f'{seed}_{kind}'
            p=ROOT/f'artifacts/neural-balance-v1/{seed}_t025.pt' if kind=='frozen' else ROOT/f'artifacts/neural-regional-training-v1/{n}.pt'
            expected=prior[f'{seed}_t025']['sha256'] if kind=='frozen' else trained[n]['sha256']
            if digest(p)!=expected: raise RuntimeError('Checkpoint mismatch')
            m=LightingNet(); m.load_state_dict(torch.load(p,map_location='cpu',weights_only=True)); m.eval()
            models[n]=m; paths[n]=p; hashes[n]=expected
    sources=list(json.loads((ROOT/'artifacts/neural-sampling-phase-v1/source-manifest.json').read_text()))
    sources+=['tools/diagnose_neural_sampling_phase.py','tools/diagnose_neural_geometry_ablation.py',
              '_IA_Python/neural_geometry_ablation.py','tests/test_neural_geometry_ablation.py']
    manifest={p:digest(ROOT/p) for p in sources}
    protocol=dict(ids=IDS,families=FAMILIES,resolutions=SIZES,checkpoints=hashes,
        phases_source_pixels=PHASES,yaws=YAWS,camera_pairs=PAIRS,depth_noise=0.,remove=False,
        variants=dict(center='center normal and depth',depth_area='center normal + area depth',
            normal_area='area normal + center depth',normal_unit='unit area normal + center depth',
            magnitude_only='center direction * area normal length + center depth; missing center direction stays zero',
            area='raw area normal and depth',area_unit='unit area normal + area depth',
            postfit='5x5 plane-residual fit AFTER area depth reduction, adjusted intrinsics; area depth'),
        fixed='Color area reduction, center validity, residual lifting and model weights unchanged in every variant.',
        phase='Each variant versus its own phase zero. Perturb geometry only, not the output lattice.',
        camera='All inputs evolve naturally, phase zero fixed; reproject prediction-minus-own-target error.',
        normalization='Length <=1e-6 becomes zero. Angular metrics exclude missing normals and record excluded counts.',
        criteria=dict(phase_ratio_max=.8,micro_ratio_max=.8,standard_ratio_max=.8,image_ratio_max=1.05,
            protected_change=0.,require_every_group_and_checkpoint=True),
        contrasts=['each alternative vs center','normal_unit vs normal_area','area_unit vs area','postfit vs area_unit'],
        cautions=['Renormalization fixes length, not mixed direction; performance changes do not uniquely identify a cause.',
            'Area depth can lie between unrelated surfaces; fitting later changes spatial support and does not guarantee correct geometry.',
            'Same FOV and same low grid: 4x4 vs 2x2 source pixels does not imply four times the scene-space footprint.',
            'Only synthetic clean fixed-light static-object scenes. No intervention-response, temporal history or GPU test.'],
        game_integration_allowed=False)
    write_json(args.out/'protocol.json',protocol); write_json(args.out/'source-manifest.json',manifest)
    start=time.perf_counter(); groups={}; input_hash=hashlib.sha256()
    with torch.inference_mode():
        for family in FAMILIES:
            for w,h in SIZES:
                key=f'{family}_{w}x{h}'
                phase_rows=[]; camera_rows=[]; image_rows=[]; normal_rows=[]; length_rows=[]
                for seed in IDS:
                    frames=[]; outputs=[]
                    for yaw in YAWS:
                        scene,cam=make_frame(seed,family,w,h,yaw,noise=0.)
                        input_hash.update(str((seed,family,w,h,yaw)).encode())
                        for k in ('features','source','target','valid','depth','object_id'): input_hash.update(scene[k].tobytes())
                        batch=tensors({k:scene[k] for k in ('features','source','valid')})
                        geo=batch['features'][:,3:7]; saved=geo.clone(); prepared=prepare(batch)
                        valid=scene['valid']>0; low_valid=as_image(prepared[1])[...,0]>0
                        variants=geometry_variants(geo,cam)
                        preds={n:{} for n in models}
                        for v,g in variants.items():
                            if yaw==0.:
                                length=np.linalg.norm(as_image(g)[...,:3],axis=-1)
                                length_rows.append(dict(scene=seed,variant=v,length=metric(length,low_valid),
                                    short_count=int((low_valid&(length<.9)).sum()),zero_count=int((low_valid&(length<=1e-6)).sum()),
                                    valid_count=int(low_valid.sum())))
                            for n,m in models.items():
                                tensor=predict_phase(m,batch,g,prepared); pred=as_image(tensor); preds[n][v]=pred
                                if not np.array_equal(pred[~valid],scene['source'][~valid]): raise RuntimeError('Protected pixels changed')
                                if yaw==0.:
                                    if not torch.equal(tensor,predict_phase(m,batch,g,prepared)): raise RuntimeError('Same-phase drift')
                                    if v=='center' and not torch.equal(tensor,predict_regional(m,batch)): raise RuntimeError('Baseline mismatch')
                                    image_rows.append(dict(scene=seed,model=n,variant=v,image=metric(np.abs(pred-scene['target']),np.ones_like(valid))))
                        if yaw==0.:
                            for phase in PHASES:
                                shifted=geometry_variants(geo,cam,phase=phase)
                                for v,g in shifted.items():
                                    normal_rows.append(dict(scene=seed,variant=v,phase=phase,**normal_change(variants[v],g,low_valid)))
                                    for n,m in models.items():
                                        pred=as_image(predict_phase(m,batch,g,prepared))
                                        if not np.array_equal(pred[~valid],scene['source'][~valid]): raise RuntimeError('Phase protection failed')
                                        phase_rows.append(dict(scene=seed,model=n,variant=v,phase=phase,
                                            output=metric(np.abs(pred-preds[n][v]),valid)))
                        if not torch.equal(geo,saved): raise RuntimeError('Input mutation')
                        frames.append((scene,cam)); outputs.append(preds)
                    for a,b,scale in PAIRS:
                        sa,_=frames[a]; sb,cb=frames[b]; iy,ix,visible=correspondences(sa,sb,cb)
                        if not visible.any(): raise RuntimeError('No correspondences')
                        for n in models:
                            for v in VARIANTS:
                                ea=outputs[a][n][v]-sa['target']; eb=outputs[b][n][v]-sb['target']
                                camera_rows.append(dict(scene=seed,model=n,variant=v,scale=scale,pair=[a,b],
                                    temporal=metric(np.abs(ea-eb[iy,ix]),visible)))
                    print(key,seed,'complete',flush=True)
                summary={}; assessment={}
                for n in models:
                    summary[n]={}
                    for v in VARIANTS:
                        match=lambda r:r['model']==n and r['variant']==v
                        summary[n][v]=dict(phase=pooled([r['output'] for r in phase_rows if match(r)]),
                            image=pooled([r['image'] for r in image_rows if match(r)]),
                            **{s:pooled([r['temporal'] for r in camera_rows if match(r) and r['scale']==s]) for s in ('micro','standard')})
                    s=summary[n]
                    assessment[n]={f'{v}_vs_center':compare(s[v],s['center']) for v in VARIANTS if v!='center'}
                    for a,b in (('normal_unit','normal_area'),('area_unit','area'),('postfit','area_unit')):
                        assessment[n][f'{a}_vs_{b}']=compare(s[a],s[b])
                normal_summary={v:{k:pooled([r[k] for r in normal_rows if r['variant']==v]) for k in ('components','length','angle_radians')} for v in VARIANTS}
                for v in VARIANTS:
                    nr=[r for r in normal_rows if r['variant']==v]; lr=[r for r in length_rows if r['variant']==v]
                    normal_summary[v].update(mean_length=pooled([r['length'] for r in lr]),
                        zero_count=sum(r['zero_count'] for r in lr),short_count=sum(r['short_count'] for r in lr),
                        valid_count=sum(r['valid_count'] for r in lr),excluded_phase_angles=sum(r['excluded_angles'] for r in nr))
                groups[key]=dict(summary=summary,assessment=assessment,normal_summary=normal_summary,
                    image_rows=image_rows,phase_rows=phase_rows,camera_rows=camera_rows,normal_rows=normal_rows,length_rows=length_rows)
                write_json(args.out/(key+'.json'),groups[key])
                print(key,'written',flush=True)
    verification=dict(sources_unchanged=all(digest(ROOT/p)==v for p,v in manifest.items()),
        weights_unchanged=all(digest(paths[n])==v for n,v in hashes.items()),
        baseline_exact=True,repeated_phase_exact=True,protected_exact=True,inputs_unchanged=True)
    if not all(verification.values()): raise RuntimeError('Verification failed')
    write_json(args.out/'verification.json',verification)
    write_json(args.out/'metrics.json',dict(protocol=protocol,groups=groups,inputs_sha256=input_hash.hexdigest(),
        cpu_wall_seconds=time.perf_counter()-start,game_integration_allowed=False))
    print('Finished',time.perf_counter()-start,'CPU seconds',flush=True)


if __name__=='__main__': main()
