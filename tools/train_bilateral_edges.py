"""Fixed-budget four-way edge ablation + matched-capacity point control."""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import sys
import time
import numpy as np
import torch
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'_IA_Python'))
from bilateral_experiment import BilateralNet, PointCapacityNet, tensors, predict, objective
from bilateral_edges import (TRAIN_IDS, VALID_IDS, TEST_IDS, CASES, stress_scene,
                             predict_variant, edge_error)

VARIANTS = dict(baseline=(False, False), edge_loss=(True, False),
                guided=(False, True), edge_guided=(True, True), point_capacity=(False, False))


def infer(name, model, batch):
    return predict(model, batch) if name == 'point_capacity' else predict_variant(model, batch, VARIANTS[name][1])


def evaluate(name, model, ids):
    cases = {}
    with torch.inference_mode():
        for case in CASES:
            rows = []
            for seed in ids:
                batch = tensors(stress_scene(seed, case))
                pred, _ = infer(name, model, batch)
                error = pred-batch['target']
                protected = (batch['valid'] == 0).expand_as(pred)
                fog = batch['fog'].bool().expand_as(pred)
                rows.append(dict(seed=seed, mae=float(error.abs().mean()),
                    gradient_mae=float((error[:,:,1:]-error[:,:,:-1]).abs().mean()+(error[:,:,:,1:]-error[:,:,:,:-1]).abs().mean()),
                    geometry_weighted_sobel=float(edge_error(error, batch['clean_geometry'])),
                    protected_max_change=float((pred-batch['source'])[protected].abs().max()),
                    fog_mae=float(error[fog].abs().mean()) if fog.any() else None,
                    identity_mae=float((batch['source']-batch['target']).abs().mean())))
            means = {key:float(np.mean([row[key] for row in rows if row[key] is not None]))
                     for key in rows[0] if key != 'seed' and any(row[key] is not None for row in rows)}
            cases[case] = dict(mean=means, scenes=rows)
    return cases


def temporal(name, model):
    result = {}
    with torch.inference_mode():
        for case in CASES:
            rows = []
            for seed in TEST_IDS:
                frames = [tensors(stress_scene(seed,case,pan=2*i,
                          phase=.08*i if case in ('local_exposure','combined') else 0.)) for i in range(3)]
                errors = [infer(name,model,b)[0]-b['target'] for b in frames]
                for i in range(2):
                    mask = (frames[i]['valid'][:,:,:,2:]*frames[i+1]['valid'][:,:,:,:-2]) > 0
                    delta = errors[i][:,:,:,2:]-errors[i+1][:,:,:,:-2]
                    rows.append(dict(seed=seed,pair=i,warped_error_delta=float(delta[mask.expand_as(delta)].abs().mean())))
            result[case] = dict(mean=float(np.mean([row['warped_error_delta'] for row in rows])),
                                maximum_pair=max(row['warped_error_delta'] for row in rows), pairs=rows)
    return result


def montage(models, out):
    names = list(models)
    labels = ['Input', 'Target']+names
    canvas = Image.new('RGB', (128*len(labels), 116*4), '#17202a')
    draw = ImageDraw.Draw(canvas)
    with torch.inference_mode():
        for row, (seed,case) in enumerate([(TEST_IDS[0],'clean'),(TEST_IDS[0],'combined'),
                                            (TEST_IDS[1],'clean'),(TEST_IDS[1],'combined')]):
            b = tensors(stress_scene(seed,case))
            images = [b['source'],b['target']]+[infer(n,models[n],b)[0] for n in names]
            for col,(label,value) in enumerate(zip(labels,images)):
                rgb = value[0].permute(1,2,0).numpy().clip(0,1)**(1/2.2)
                canvas.paste(Image.fromarray(np.uint8(np.round(rgb*255))), (128*col,116*row+20))
                draw.text((128*col+2,116*row+2),label,fill='white')
    canvas.save(out/'comparison.png')


def run():
    p = argparse.ArgumentParser()
    p.add_argument('--out',type=Path,default=ROOT/'artifacts/bilateral-edges-v2')
    p.add_argument('--steps',type=int,default=800)
    args = p.parse_args()
    if args.steps < 1:
        p.error('steps must be positive')
    args.out.mkdir(parents=True, exist_ok=False)
    torch.set_num_threads(2)
    torch.use_deterministic_algorithms(True)
    torch.manual_seed(20260909)
    initial = BilateralNet()
    models = {name:copy.deepcopy(initial) for name in VARIANTS if name != 'point_capacity'}
    models['point_capacity'] = PointCapacityNet()
    protocol = dict(seed=20260909, batch_seed=351, steps=args.steps, batch_size=4, learning_rate=.002,
        train=list(TRAIN_IDS),validation=list(VALID_IDS),test=list(TEST_IDS),cases=list(CASES),
        grid_initialization='identical copied weights for all four variants',
        training_cases='case cycles clean/exposure/noise/combined every step; same batches for all variants',
        loss='V1 Charbonnier + finite gradient + confidence; edge variants additionally 0.15 * geometry-weighted Sobel error',
        geometry_weight='1+3*clamp(normal Sobel magnitude + 8*normalized-depth Sobel magnitude,0,1); clean reference, detached',
        guided_filter=dict(radius=2,epsilon=.001,signal='bounded scalar log gain after trilinear slicing',guide='luma/(1+luma)'),
        gates=dict(mae_gain_over_point=.05,max_baseline_edge_regression=.01,
                   max_baseline_stress_mae_regression=.05,max_baseline_temporal_regression=.05,
                   clean_warped_error_delta_limit=.001,exact_protection=True),
        checkpoint_selection='fixed final step, no selection or tuning using validation/test',
        rotation_3d_tested=False,gpu_benchmark_ms=None,game_integration_allowed=False)
    (args.out/'protocol.json').write_text(json.dumps(protocol,indent=2)+'\n',encoding='utf-8')
    data = {(seed,case):tensors(stress_scene(seed,case)) for seed in TRAIN_IDS for case in CASES}
    digest = hashlib.sha256()
    for key in sorted(data):
        digest.update(str(key).encode())
        for field,value in sorted(data[key].items()):
            digest.update(field.encode()); digest.update(value.numpy().tobytes())
    training = {}
    for name,model in models.items():
        opt = torch.optim.Adam(model.parameters(),lr=.002)
        rng = np.random.default_rng(351)
        start = time.perf_counter()
        curve = []
        for step in range(1,args.steps+1):
            seeds = rng.choice(TRAIN_IDS,4,replace=False)
            case = CASES[(step-1)%len(CASES)]
            batch = {key:torch.cat([data[(s,case)][key] for s in seeds]) for key in data[(seeds[0],case)]}
            opt.zero_grad(set_to_none=True)
            pred,conf = infer(name,model,batch)
            loss = objective(pred,conf,batch)
            if VARIANTS[name][0]:
                loss = loss+.15*edge_error(pred-batch['target'],batch['clean_geometry'])
            if not torch.isfinite(loss):
                raise RuntimeError('Nonfinite loss')
            loss.backward(); opt.step()
            if step == 1 or step % 200 == 0:
                curve.append(dict(step=step,loss=float(loss.detach())))
                print(name,step,round(float(loss.detach()),7),flush=True)
        model.eval()
        checkpoint = args.out/(name+'.pt')
        torch.save(model.state_dict(),checkpoint)
        training[name] = dict(parameters=sum(v.numel() for v in model.parameters()),
            cpu_training_seconds=time.perf_counter()-start,curve=curve,
            checkpoint_sha256=hashlib.sha256(checkpoint.read_bytes()).hexdigest())
    validation = {n:evaluate(n,m,VALID_IDS) for n,m in models.items()}
    heldout = {n:evaluate(n,m,TEST_IDS) for n,m in models.items()}
    movement = {n:temporal(n,m) for n,m in models.items()}
    gates = {}
    for name in list(models)[:-1]:
        clean = heldout[name]['clean']['mean']
        gates[name] = dict(
            beats_capacity_mae_5_percent=clean['mae'] <= .95*heldout['point_capacity']['clean']['mean']['mae'],
            baseline_edges_preserved=all(clean[k] <= 1.01*heldout['baseline']['clean']['mean'][k] for k in ('gradient_mae','geometry_weighted_sobel')),
            stress_no_regression=all(heldout[name][c]['mean']['mae'] <= 1.05*heldout['baseline'][c]['mean']['mae'] for c in CASES),
            protected_exact=all(heldout[name][c]['mean']['protected_max_change'] == 0 for c in CASES),
            temporal_no_regression=all(movement[name][c]['mean'] <= 1.05*movement['baseline'][c]['mean'] for c in CASES),
            clean_temporal_below_001=movement[name]['clean']['mean'] <= .001)
    result = dict(protocol=protocol,torch=torch.__version__,training_data_sha256=digest.hexdigest(),
        training=training,validation=validation,held_out=heldout,temporal=movement,gates=gates,
        offline_gates_passed={n:all(v.values()) for n,v in gates.items()},game_integration_allowed=False,
        limitations=['Synthetic 2.5D scalar appearance teacher, oracle UI/sky masks and clean geometry for loss.',
                     'Guided variant adds filtering after slicing; it does not replace slicing.',
                     'Temporal metric is warped prediction ERROR; exposure phase and geometry noise can change between frames.',
                     'No 3D rotation, moving objects, measured game buffers, DirectML latency, VRAM measurement or model export.',
                     'Single seed and fixed budget, no statistical superiority claim. New scene split; not directly comparable to V1.'])
    (args.out/'metrics.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    source_files = ['_IA_Python/bilateral_experiment.py','_IA_Python/bilateral_edges.py',
                    'tools/train_bilateral_edges.py','tests/test_bilateral_edges.py']
    (args.out/'source-manifest.json').write_text(json.dumps({f:hashlib.sha256((ROOT/f).read_bytes()).hexdigest() for f in source_files},indent=2)+'\n',encoding='utf-8')
    montage(models,args.out)
    print(json.dumps(dict(clean={n:v['clean']['mean'] for n,v in heldout.items()},
        temporal={n:{c:v[c]['mean'] for c in CASES} for n,v in movement.items()},gates=gates),indent=2),flush=True)


if __name__ == '__main__':
    run()
