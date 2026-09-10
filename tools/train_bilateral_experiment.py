"""Run isolated stages 1/2. CPU timings are NOT DirectML/RX 7600 timings."""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import sys
import time
import numpy as np
import torch
from torch.nn import functional as F
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'_IA_Python'))
from bilateral_experiment import (TRAIN_IDS, VALID_IDS, TEST_IDS, BilateralNet, PointNet,
                                 make_scene, tensors, predict, objective)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def evaluate(model, ids):
    rows = []
    with torch.inference_mode():
        for seed in ids:
            batch = tensors(make_scene(seed))
            prediction, confidence = predict(model, batch)
            error = prediction-batch['target']
            row = dict(seed=seed, mae=error.abs().mean().item(), mse=error.square().mean().item(),
                       identity_mae=(batch['source']-batch['target']).abs().mean().item(),
                       analytic_heuristic_mae=(batch['heuristic']-batch['target']).abs().mean().item(),
                       confidence_mae=(confidence-batch['confidence']).abs().mean().item())
            row['gradient_mae'] = ((error[:, :, 1:]-error[:, :, :-1]).abs().mean()+(error[:, :, :, 1:]-error[:, :, :, :-1]).abs().mean()).item()
            for name in ['ui', 'fog']:
                mask = batch[name].bool().expand_as(error)
                row[name+'_target_mae'] = error[mask].abs().mean().item() if mask.any() else None
            mask = (batch['valid'] == 0).expand_as(error)
            row['protected_max_change'] = (prediction-batch['source'])[mask].abs().max().item()
            rows.append(row)
    means = {k:float(np.mean([r[k] for r in rows if r[k] is not None]))
             for k in rows[0] if k != 'seed' and any(r[k] is not None for r in rows)}
    return dict(mean=means, scenes=rows)


def temporal(model):
    values = []
    with torch.inference_mode():
        for seed in TEST_IDS:
            frames = [tensors(make_scene(seed, pan=pan)) for pan in (0, 2, 4)]
            errors = [predict(model, b)[0]-b['target'] for b in frames]
            for i in (0, 1):
                # B(x)=A(x+2); only overlapping visible non-UI samples.
                mask = (frames[i]['valid'][:, :, :, 2:]*frames[i+1]['valid'][:, :, :, :-2]) > 0
                delta = errors[i][:, :, :, 2:]-errors[i+1][:, :, :, :-2]
                values.append(delta[mask.expand_as(delta)].abs().mean().item())
    return dict(mean_warped_error_delta=float(np.mean(values)), max_sequence_pair=float(max(values)),
                pairs=len(values), motion='known integer horizontal translation; common valid pixels',
                limitations='No rotations, moving objects, exposure changes, or game motion buffers.')


def montage(model, path):
    # Display-only tone mapping; all metrics use original linear floats.
    labels = ['Input', 'Controlled target', 'Bilateral prediction', 'Error x8']
    canvas = Image.new('RGB', (128*4, (96+20)*3), '#141b23')
    draw = ImageDraw.Draw(canvas)
    with torch.inference_mode():
        for row, seed in enumerate(TEST_IDS[:3]):
            batch = tensors(make_scene(seed))
            pred = predict(model, batch)[0]
            samples = [batch['source'], batch['target'], pred, (pred-batch['target']).abs()*8]
            for col, (label, value) in enumerate(zip(labels, samples)):
                rgb = value[0].permute(1, 2, 0).numpy()
                rgb = np.clip(rgb, 0, 1)**(1/2.2)
                image = Image.fromarray(np.uint8(np.round(rgb*255)))
                canvas.paste(image, (col*128, row*116+20))
                draw.text((col*128+3, row*116+3), label, fill='white')
    canvas.resize((1024, 696)).save(path)


def export_grid(model, out):
    import onnx
    low = F.interpolate(tensors(make_scene(VALID_IDS[0]))['features'], (64, 64), mode='area')
    half = copy.deepcopy(model).half().eval()
    with torch.inference_mode():
        fp16_difference = (half(low.half()).float()-model(low)).abs().max().item()
    path = out/'bilateral_grid_fp16.onnx'
    # Export just the CNN; the 3D slicing belongs in a separate compute shader.
    torch.onnx.export(half, low.half(), str(path), input_names=['context_64'], output_names=['grid_logits'],
                      opset_version=17, dynamo=False, external_data=False)
    graph = onnx.load(path)
    onnx.checker.check_model(graph, full_check=True)
    assert all(x.data_type == onnx.TensorProto.FLOAT16 for x in graph.graph.initializer)
    return dict(path=path.name, sha256=sha(path), bytes=path.stat().st_size, opset=17,
                input_shape=[1, 8, 64, 64], output_shape=[1, 2, 8, 16, 16],
                operators=sorted({n.op_type for n in graph.graph.node}),
                pytorch_fp16_grid_max_abs_difference=fp16_difference,
                onnx_checker_passed=True, onnx_runtime_execution=False, directml_execution=False)


def run():
    parser = argparse.ArgumentParser()
    parser.add_argument('--out', type=Path, default=ROOT/'artifacts/bilateral-regional-v1')
    parser.add_argument('--steps', type=int, default=800)
    args = parser.parse_args()
    if args.steps < 1:
        parser.error('steps must be positive')
    args.out.mkdir(parents=True, exist_ok=False)
    torch.set_num_threads(2)
    torch.manual_seed(20260908)
    torch.use_deterministic_algorithms(True)
    data = {seed:tensors(make_scene(seed)) for seed in TRAIN_IDS}
    # Persist the paired float dataset and scene IDs, not only illustrations.
    all_ids = TRAIN_IDS+VALID_IDS+TEST_IDS
    pairs = {seed:make_scene(seed) for seed in all_ids}
    np.savez_compressed(args.out/'paired_scenes.npz', ids=np.array(all_ids),
                        **{key:np.stack([pairs[s][key] for s in all_ids]) for key in pairs[all_ids[0]]})
    dataset = dict(train=list(TRAIN_IDS), validation=list(VALID_IDS), test=list(TEST_IDS),
                   resolution=[128, 96], source='procedural 2.5D analytic fixtures, no game captures',
                   teacher='bounded scalar appearance correction with regional light and known fog transmission',
                   ui_mask='explicit oracle mask; not automatic detection',
                   normal_source='analytic fixture normals, not reconstructed game depth',
                   npz_sha256=sha(args.out/'paired_scenes.npz'))
    (args.out/'dataset.json').write_text(json.dumps(dataset, indent=2)+'\n', encoding='utf-8')
    models = {'bilateral':BilateralNet(), 'point_ablation':PointNet()}
    training = {}
    # Fixed steps, no test-based checkpoint or hyperparameter selection.
    for name, model in models.items():
        optimizer = torch.optim.Adam(model.parameters(), lr=.002)
        rng = np.random.default_rng(351)
        start = time.perf_counter()
        curve = []
        for step in range(1, args.steps+1):
            ids = rng.choice(TRAIN_IDS, 4, replace=False)
            batch = {k:torch.cat([data[s][k] for s in ids]) for k in data[TRAIN_IDS[0]]}
            optimizer.zero_grad(set_to_none=True)
            prediction, confidence = predict(model, batch)
            loss = objective(prediction, confidence, batch)
            if not torch.isfinite(loss):
                raise RuntimeError('Nonfinite training loss')
            loss.backward()
            optimizer.step()
            if step == 1 or step % 100 == 0:
                curve.append(dict(step=step, loss=loss.item()))
                print(name, step, round(loss.item(), 6), flush=True)
        model.eval()
        checkpoint = args.out/(name+'.pt')
        torch.save(model.state_dict(), checkpoint)
        training[name] = dict(parameters=sum(p.numel() for p in model.parameters()),
                              steps=args.steps, cpu_training_seconds=time.perf_counter()-start,
                              curve=curve, checkpoint_sha256=sha(checkpoint))
    validation = {name:evaluate(model, VALID_IDS) for name, model in models.items()}
    tests = {name:evaluate(model, TEST_IDS) for name, model in models.items()}
    temporal_results = {name:temporal(model) for name, model in models.items()}
    candidate = tests['bilateral']['mean']
    gates = dict(improves_identity_20_percent=candidate['mae'] <= .8*candidate['identity_mae'],
                 beats_point_ablation_5_percent=candidate['mae'] <= .95*tests['point_ablation']['mean']['mae'],
                 beats_analytic_heuristic=candidate['mae'] < candidate['analytic_heuristic_mae'],
                 exact_protected_regions=candidate['protected_max_change'] == 0,
                 translated_error_delta_below_001=temporal_results['bilateral']['mean_warped_error_delta'] <= .001)
    exported = export_grid(models['bilateral'], args.out)
    montage(models['bilateral'], args.out/'comparison.png')
    metrics = dict(experiment='bilateral_regional_v1', seed=20260908, torch=torch.__version__, device='CPU',
                   training=training, validation=validation, held_out=tests, temporal=temporal_results,
                   predeclared_synthetic_gates=gates, synthetic_gates_passed=all(gates.values()),
                   export=exported, gpu_benchmark_ms=None, gpu_vram_peak_bytes=None,
                   game_integration_allowed=False,
                   memory_accounting=dict(grid_fp16_bytes=2*8*16*16*2, low_input_fp16_bytes=8*64*64*2,
                       six_rgba16f_outputs_1080p_bytes=6*1920*1080*8,
                       scope='Known tensor/output sizes only; excludes DML scratch, descriptors, driver allocations.'),
                   limitations=['Synthetic appearance target, not physical ground truth or game material training.',
                                'No RX 7600 runtime or sub-ms performance claim.',
                                'UI mask and analytic normals are supplied by fixture.',
                                'ONNX CNN only; texture conversion/slicing not exported or integrated.',
                                'V2.3 is unchanged; point ablation is separately trained, not V2.3.'])
    (args.out/'metrics.json').write_text(json.dumps(metrics, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(dict(held_out={k:v['mean'] for k,v in tests.items()}, gates=gates,
                          temporal=temporal_results, export=exported), indent=2), flush=True)


if __name__ == '__main__':
    run()
