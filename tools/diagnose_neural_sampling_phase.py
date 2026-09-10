"""Frozen geometry-only phase intervention plus a fixed-grid camera control."""
import argparse
import hashlib
import json
from pathlib import Path
import sys
import time
import numpy as np
import torch
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'_IA_Python'))
from bilateral_experiment import tensors
from neural_balance_selection import SEEDS
from neural_generalization_scene import make_frame
from neural_lighting_scene import correspondences, project
from neural_lighting_model import LightingNet
from neural_regional_inference import predict_regional
from neural_sampling_phase import reduce_geometry, prepare, predict_phase
from diagnose_neural_generalization import write_json

IDS = tuple(range(26000,26004))
FAMILIES = ('baseline','boxes')
SIZES = ((192,128),(384,256))
PHASES = ((-.5,0.),(-.25,0.),(.25,0.),(.5,0.),(0.,-.5),(0.,-.25),(0.,.25),(0.,.5))
YAWS = (-.04,-.001,0.,.001,.04)
PAIRS = ((1,2,'micro'),(2,3,'micro'),(0,2,'standard'),(2,4,'standard'))


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def metric(value, mask):
    values = value[mask]
    return dict(abs_sum=float(values.astype(np.float64).sum()), samples=int(values.size))


def pooled(values):
    n = sum(v['samples'] for v in values)
    return sum(v['abs_sum'] for v in values)/n if n else None


def as_image(value):
    return value[0].permute(1,2,0).numpy()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--out', type=Path, default=ROOT/'artifacts/neural-sampling-phase-v1')
    args = parser.parse_args(); args.out.mkdir(parents=True,exist_ok=False)
    torch.set_num_threads(2); torch.use_deterministic_algorithms(True)
    models = {}; paths = {}; hashes = {}
    prior = json.loads((ROOT/'artifacts/neural-balance-v1/training.json').read_text())
    trained = json.loads((ROOT/'artifacts/neural-regional-training-v1/training.json').read_text())
    for seed in SEEDS:
        for variant in ('frozen','joint'):
            name = f'{seed}_{variant}'
            path = ROOT/f'artifacts/neural-balance-v1/{seed}_t025.pt' if variant=='frozen' else ROOT/f'artifacts/neural-regional-training-v1/{name}.pt'
            expected = prior[f'{seed}_t025']['sha256'] if variant=='frozen' else trained[name]['sha256']
            if digest(path) != expected: raise RuntimeError('Checkpoint mismatch')
            model = LightingNet(); model.load_state_dict(torch.load(path,map_location='cpu',weights_only=True)); model.eval()
            models[name] = model; paths[name] = path; hashes[name] = expected
    source_names = ['tools/diagnose_neural_sampling_phase.py','_IA_Python/neural_sampling_phase.py',
        '_IA_Python/neural_regional_inference.py','_IA_Python/neural_generalization_scene.py',
        '_IA_Python/neural_lighting_scene.py','_IA_Python/neural_lighting_model.py',
        '_IA_Python/neural_lighting_diagnostics.py','_IA_Python/neural_scene_geometry.py',
        '_IA_Python/neural_intervention_training.py','_IA_Python/neural_balance_selection.py',
        '_IA_Python/bilateral_experiment.py','tools/diagnose_neural_generalization.py',
        'tools/train_neural_interventions.py',
        'tests/test_neural_sampling_phase.py']
    sources = {p:digest(ROOT/p) for p in source_names}
    protocol = dict(ids=IDS,families=FAMILIES,resolutions=SIZES,depth_noise=0.,remove=False,
        checkpoints=hashes,phases_source_pixels=PHASES,yaws=YAWS,camera_pairs=PAIRS,
        intervention='Only fitted full-resolution normals and observed depth change sampling phase. Color, validity and output lattice fixed.',
        alternative='Raw area means of four geometry channels, without normal renormalization or validity weighting. Not a production-safe surface filter.',
        static_reference='Each reducer is compared with its OWN phase-zero output. No target or raster motion; masked absolute output change.',
        camera_reference='Fixed phase zero for both reducers; reproject prediction-minus-own-target error on same-geometry correspondences.',
        diagnostics_only=True,game_integration_allowed=False,
        criteria=dict(phase_output_ratio_max=.8,camera_temporal_ratio_max=.8,image_ratio_max=1.05,
            protected_change_max=0.,require_every_model_family_resolution=True,
            meaning='A promising sampling follow-up only; not generalization, intervention-response or deployment approval.'),
        limitations=['An artificial geometry-only phase perturbation is not evidence that the runtime grid origin changes.',
            'Nearest-exact ties are asymmetric at integer even scales: negative quarter-pixel shifts may select the preceding pixel.',
            'Area reduction changes input distribution and can mix unrelated surfaces and invalid geometry.',
            'Four new scene seeds, two related families, clean depth, fixed light rig, static objects, CPU float32.',
            'Same order of error magnitudes is a scale comparison, not a causal attribution or a fraction explained.'])
    write_json(args.out/'protocol.json',protocol)
    write_json(args.out/'source-manifest.json',sources)
    groups = {}; input_hash = hashlib.sha256(); start = time.perf_counter()
    verification = dict(baseline_exact=True,repeated_phase_exact=True,protected_exact=True,inputs_unchanged=True)
    with torch.inference_mode():
        for family in FAMILIES:
            for width,height in SIZES:
                key = f'{family}_{width}x{height}'
                rows = []; camera_rows = []; input_rows = []; image_rows = []; motion_rows = []
                for scene_id in IDS:
                    frames = []; outputs = []
                    for yaw in YAWS:
                        scene,cam = make_frame(scene_id,family,width,height,yaw,noise=0.)
                        input_hash.update(str((scene_id,family,width,height,yaw)).encode())
                        for k in ('features','source','target','valid','depth','object_id'): input_hash.update(scene[k].tobytes())
                        batch = tensors({k:scene[k] for k in ('features','source','valid')})
                        original_geometry = batch['features'][:,3:7].clone()
                        prepared = prepare(batch); low_valid = as_image(prepared[1])[...,0]>0
                        zeros = {m:reduce_geometry(original_geometry,(64,96),method=m) for m in ('center','area')}
                        preds = {}
                        for name,model in models.items():
                            preds[name] = {}
                            for method in ('center','area'):
                                pred_tensor = predict_phase(model,batch,zeros[method],prepared)
                                pred = as_image(pred_tensor); preds[name][method] = pred
                                if not np.array_equal(pred[scene['valid']==0],scene['source'][scene['valid']==0]):
                                    raise RuntimeError('Protected pixels changed')
                                if yaw == 0.:
                                    image_rows.append(dict(scene=scene_id,model=name,method=method,
                                        image=metric(np.abs(pred-scene['target']),np.ones_like(scene['valid'],dtype=bool))))
                                    repeat = predict_phase(model,batch,zeros[method],prepared)
                                    if not torch.equal(pred_tensor,repeat): raise RuntimeError('Same-phase drift')
                                    if method=='center' and not torch.equal(pred_tensor,predict_regional(model,batch)):
                                        raise RuntimeError('Phase zero differs from current inference')
                        if yaw == 0.:
                            for method in ('center','area'):
                                for dx,dy in PHASES:
                                    geo = reduce_geometry(original_geometry,(64,96),(dx,dy),method)
                                    change = as_image((geo-zeros[method]).abs())
                                    input_rows.append(dict(scene=scene_id,method=method,phase=[dx,dy],
                                        normal=metric(change[...,:3],low_valid),depth=metric(30*change[...,3],low_valid)))
                                    for name,model in models.items():
                                        pred = as_image(predict_phase(model,batch,geo,prepared))
                                        delta = np.abs(pred-preds[name][method])
                                        rows.append(dict(scene=scene_id,model=name,method=method,phase=[dx,dy],
                                            output=metric(delta,scene['valid']>0)))
                        if not torch.equal(batch['features'][:,3:7],original_geometry): raise RuntimeError('Input mutated')
                        frames.append((scene,cam)); outputs.append(preds)
                    for a,b,scale in PAIRS:
                        scene_a,_ = frames[a]; scene_b,cam_b = frames[b]
                        iy,ix,visible = correspondences(scene_a,scene_b,cam_b)
                        if not visible.any(): raise RuntimeError('No correspondences')
                        xy,_ = project(scene_a['world'],cam_b)
                        yy,xx = np.mgrid[:height,:width]
                        displacement = np.linalg.norm(xy-np.stack([xx,yy],-1),axis=-1)
                        motion_rows.append(dict(scene=scene_id,scale=scale,pair=[a,b],visible_pixels=int(visible.sum()),
                            valid_pixels=int((scene_a['valid']>0).sum()),
                            displacement_median=float(np.median(displacement[visible])),
                            displacement_p95=float(np.percentile(displacement[visible],95))))
                        for name in models:
                            for method in ('center','area'):
                                ea = outputs[a][name][method]-scene_a['target']
                                eb = outputs[b][name][method]-scene_b['target']
                                camera_rows.append(dict(scene=scene_id,model=name,method=method,scale=scale,pair=[a,b],
                                    temporal=metric(np.abs(ea-eb[iy,ix]),visible)))
                    print(key,scene_id,'phase and camera complete',flush=True)
                summary = {}; assessment = {}
                for name in models:
                    summary[name] = {}
                    for method in ('center','area'):
                        match = lambda r: r['model']==name and r['method']==method
                        summary[name][method] = dict(
                            phase_mae=pooled([r['output'] for r in rows if match(r)]),
                            image_mae=pooled([r['image'] for r in image_rows if match(r)]),
                            phase_by_offset={str(p):pooled([r['output'] for r in rows if match(r) and r['phase']==list(p)]) for p in PHASES},
                            **{s+'_temporal':pooled([r['temporal'] for r in camera_rows if match(r) and r['scale']==s]) for s in ('micro','standard')})
                    center,area = summary[name]['center'],summary[name]['area']
                    ratios = {k:area[k]/center[k] if center[k] else None for k in ('phase_mae','image_mae','micro_temporal','standard_temporal')}
                    checks = {k:ratios[k] is not None and ratios[k] <= limit for k,limit in
                              (('phase_mae',.8),('image_mae',1.05),('micro_temporal',.8),('standard_temporal',.8))}
                    assessment[name] = dict(ratios=ratios,checks=checks,passed=all(checks.values()))
                input_summary = {m:{k:pooled([r[k] for r in input_rows if r['method']==m]) for k in ('normal','depth')} for m in ('center','area')}
                groups[key] = dict(summary=summary,assessment=assessment,input_summary=input_summary,
                    rows=rows,camera_rows=camera_rows,input_rows=input_rows,image_rows=image_rows,motion_rows=motion_rows)
                write_json(args.out/(key+'.json'),groups[key])
                print(key,json.dumps(assessment),flush=True)
    verification['sources_unchanged'] = all(digest(ROOT/p)==v for p,v in sources.items())
    verification['checkpoints_unchanged'] = all(digest(paths[n])==v for n,v in hashes.items())
    if not all(verification.values()): raise RuntimeError('Verification failed')
    write_json(args.out/'verification.json',verification)
    result = dict(protocol=protocol,groups=groups,inputs_sha256=input_hash.hexdigest(),
        cpu_wall_seconds=time.perf_counter()-start,game_integration_allowed=False,
        all_diagnostic_checks_passed=all(a['passed'] for g in groups.values() for a in g['assessment'].values()))
    write_json(args.out/'metrics.json',result)
    print(json.dumps(dict(cpu_wall_seconds=result['cpu_wall_seconds'],all_diagnostic_checks_passed=result['all_diagnostic_checks_passed'],verification=verification)),flush=True)


if __name__ == '__main__': main()
