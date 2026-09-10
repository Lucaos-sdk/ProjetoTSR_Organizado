"""Small preregistered loss sweep, validation selection, then untouched test."""
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
sys.path.insert(0, str(ROOT / '_IA_Python'))
from neural_joint_training import TRAIN_IDS, CAMERA_PAIRS, quartet, losses
from neural_intervention_training import CASES, make_frame
from neural_lighting_model import LightingNet, predict_lighting
from neural_balance_selection import VARIANTS, SEEDS, VALID_IDS, TEST_IDS, model_name, fidelity_metrics, comparisons, select_validation
from train_neural_interventions import evaluate, infer
from train_neural_joint import temporal_evaluation, hash_tensors


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2, allow_nan=False)+'\n', encoding='utf-8')


def details(models, ids):
    result = {n: {} for n in models}
    with torch.inference_mode():
        for condition, noise in (('clean', 0.), ('noisy', .002)):
            rows = {n: [] for n in models}
            for seed in ids:
                for case in ('base',) + CASES:
                    scene, _ = make_frame(seed, case, yaw=.07, noise=noise)
                    for n, model in models.items():
                        rows[n].append(dict(seed=seed, case=case, **fidelity_metrics(infer(model, scene), scene)))
            for n, values in rows.items():
                metrics = dict(rows=values)
                for metric in ('edge', 'gradient'):
                    samples = sum(r[metric+'_samples'] for r in values)
                    if samples == 0:
                        raise RuntimeError('No samples for fidelity evaluation')
                    metrics[metric+'_mae'] = sum(r[metric+'_abs_sum'] for r in values) / samples
                    metrics[metric+'_samples'] = samples
                result[n][condition] = metrics
    return result


def evaluate_split(models, ids, label):
    print(label, 'image and response', flush=True)
    quality = evaluate(models, ids)
    print(label, 'camera sequences', flush=True)
    motion = temporal_evaluation(models, ids)
    print(label, 'edges and gradients', flush=True)
    detail = details(models, ids)
    return dict(quality=quality, motion=motion, detail=detail)


def visual(models, chosen, out):
    canvas = Image.new('RGB', (96*6, 84*4), '#18212b'); draw = ImageDraw.Draw(canvas)
    labels = ('Entrada', 'Referencia', 'Pares A', 'Escolhida A', 'Pares B', 'Escolhida B')
    with torch.inference_mode():
        for row, case in enumerate(('base',) + CASES):
            scene, _ = make_frame(TEST_IDS[0], case, yaw=.07)
            values = [scene['source'], scene['target']]
            for seed in SEEDS:
                values += [infer(models[model_name(seed, v)], scene) for v in ('paired', chosen)]
            for col, (label, value) in enumerate(zip(labels, values)):
                rgb = value.clip(0); rgb = (rgb/(1+rgb))**(1/2.2)
                canvas.paste(Image.fromarray(np.uint8(np.round(rgb*255))), (96*col, 84*row+20))
                draw.text((96*col+2, 84*row+3), label, fill='white')
    canvas.resize((1152, 672), Image.Resampling.NEAREST).save(out/'comparison.png')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--out', type=Path, default=ROOT/'artifacts/neural-balance-v1')
    parser.add_argument('--steps', type=int, default=1600)
    args = parser.parse_args()
    if args.steps < 1: parser.error('steps must be positive')
    args.out.mkdir(parents=True, exist_ok=False)
    torch.set_num_threads(2); torch.use_deterministic_algorithms(True)
    protocol = dict(train=list(TRAIN_IDS), validation=list(VALID_IDS), test=list(TEST_IDS),
        initialization_seeds=SEEDS, batch_seed=732, steps=args.steps, learning_rate=.002,
        variants=VARIANTS, objective='unchanged image + response + lambda * camera error',
        dataset='same 384 quartets as joint-v1; no new training images or model architecture',
        train_camera_pairs=CAMERA_PAIRS, train_depth_noise=.0005, evaluation_depth_noise=[0., .002],
        budget='eight fresh models; identical initial weights within each seed, same examples/order and final step for all',
        selection='all constraints in BOTH initializations and noise conditions; select eligible minimum mean image ratio',
        no_eligible='freeze minimum worst normalized violation as diagnostic only; final test cannot override failed validation',
        gates=dict(response_vs_zero=.8, response_ratio_max=1.05, image_ratio_max=1.05,
                   temporal_ratio_max=.8, unchanged_max=.003, protected_exact=True,
                   shadow_ratio_max=1.05, edge_ratio_max=1.05, gradient_ratio_max=1.05),
        final_test='only selected lambda and paired control for both seeds; no ensemble, best-seed selection or retuning',
        game_integration_allowed=False)
    write_json(args.out/'protocol.json', protocol)
    paths = ['_IA_Python/neural_balance_selection.py', '_IA_Python/neural_joint_training.py',
             '_IA_Python/neural_lighting_temporal.py', '_IA_Python/neural_intervention_training.py',
             '_IA_Python/neural_lighting_scene.py', '_IA_Python/neural_lighting_model.py',
             '_IA_Python/neural_lighting_diagnostics.py', 'tools/train_neural_interventions.py',
             'tools/train_neural_joint.py', 'tools/train_neural_balance.py', 'tests/test_neural_balance_selection.py']
    write_json(args.out/'source-manifest.json', {p: hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in paths})
    examples = []; digest = hashlib.sha256()
    for count, scene_id in enumerate(TRAIN_IDS, 1):
        for yaws in CAMERA_PAIRS:
            for case in CASES:
                example = quartet(scene_id, case, yaws)
                examples.append(example)
                digest.update(str((scene_id, case, yaws)).encode()); hash_tensors(digest, example)
        if count % 16 == 0: print('dataset', count, '/', len(TRAIN_IDS), flush=True)
    schedule = np.random.default_rng(732).integers(len(examples), size=args.steps)
    models = {}; training = {}
    for seed in SEEDS:
        torch.manual_seed(seed); initial = LightingNet()
        for variant, weight in VARIANTS.items():
            name = model_name(seed, variant); model = copy.deepcopy(initial)
            optimizer = torch.optim.Adam(model.parameters(), lr=.002)
            start = time.perf_counter(); curve = []
            for step, index in enumerate(schedule, 1):
                example = examples[index]; optimizer.zero_grad(set_to_none=True)
                pred = predict_lighting(model, example[0])
                image, response, temporal = losses(pred, example)
                loss = image + response + weight*temporal
                if not torch.isfinite(loss): raise RuntimeError('Nonfinite loss')
                loss.backward(); optimizer.step()
                if step == 1 or step % 400 == 0:
                    curve.append(dict(step=step, image=float(image.detach()), response=float(response.detach()), temporal=float(temporal.detach())))
                    print(name, curve[-1], flush=True)
            model.eval(); models[name] = model
            path = args.out/(name+'.pt'); torch.save(model.state_dict(), path)
            training[name] = dict(cpu_seconds=time.perf_counter()-start, curve=curve,
                parameters=sum(p.numel() for p in model.parameters()), sha256=hashlib.sha256(path.read_bytes()).hexdigest())
            write_json(args.out/'training.json', training)
    validation = evaluate_split(models, VALID_IDS, 'validation')
    write_json(args.out/'validation.json', validation)
    reports = {v: comparisons(validation['quality'], validation['motion'], validation['detail'], v)
               for v in VARIANTS if v != 'paired'}
    selection = select_validation(reports)
    # Durable decision before any final-test rendering or inference.
    write_json(args.out/'selection.json', selection)
    print('Frozen selection', json.dumps(selection), flush=True)
    chosen = selection['variant']
    test_models = {model_name(s, v): models[model_name(s, v)] for s in SEEDS for v in ('paired', chosen)}
    final = evaluate_split(test_models, TEST_IDS, 'held out')
    test_report = comparisons(final['quality'], final['motion'], final['detail'], chosen)
    result = dict(protocol=protocol, training_data_sha256=digest.hexdigest(),
        schedule_sha256=hashlib.sha256(schedule.tobytes()).hexdigest(), training=training,
        selection_sha256=hashlib.sha256((args.out/'selection.json').read_bytes()).hexdigest(),
        selection=selection, held_out=final, test_report=test_report,
        synthetic_gates_passed=selection['validation_eligible'] and test_report['passed'],
        game_integration_allowed=False, torch=torch.__version__,
        limitations=['Same small diffuse sphere/plane family and fixed lights; no game data or materials synthesis.',
                     'Two initialization seeds only, identical training sample schedule; not broad statistical confidence.',
                     'Targets and geometry masks in training/evaluation only, with explicit UI/sky preservation.',
                     'Static geometries under camera motion, nearest reprojection and no inference history.',
                     'CPU FP32 only; no measured GPU/VRAM cost, FP16 export or DLL installation.'])
    write_json(args.out/'metrics.json', result)
    visual(test_models, chosen, args.out)
    print(json.dumps(dict(selection=chosen, validation_eligible=selection['validation_eligible'],
                         test_report=test_report, synthetic_gates_passed=result['synthetic_gates_passed']), indent=2), flush=True)


if __name__ == '__main__': main()
