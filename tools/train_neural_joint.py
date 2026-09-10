"""Fixed-budget paired control versus joint camera/intervention training."""
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
from neural_joint_training import TRAIN_IDS, VALID_IDS, TEST_IDS, CAMERA_PAIRS, quartet, losses
from neural_intervention_training import CASES, make_frame
from neural_lighting_scene import correspondences
from neural_lighting_model import LightingNet, predict_lighting
from train_neural_interventions import evaluate, infer


def temporal_evaluation(models, ids):
    """Evaluate static geometry camera motion in every intervention state."""
    result = {n: {} for n in models}
    with torch.inference_mode():
        for condition, noise in (('clean', 0.), ('noisy', .002)):
            rows = {n: [] for n in models}
            for seed in ids:
                for case in ('base',) + CASES:
                    frames = [make_frame(seed, case, yaw=y, noise=noise)
                              for y in (-.08, -.04, 0., .04, .08)]
                    errors = {n: [infer(m, s) - s['target'] for s, _ in frames]
                              for n, m in models.items()}
                    for i in range(4):
                        iy, ix, mask = correspondences(frames[i][0], frames[i+1][0], frames[i+1][1])
                        if not mask.any():
                            raise RuntimeError('Empty temporal evaluation mask')
                        for n in models:
                            error = np.abs(errors[n][i] - errors[n][i+1][iy, ix])[mask].mean()
                            rows[n].append(dict(seed=seed, case=case, pair=i,
                                                samples=int(mask.sum()), error=float(error)))
            for n, rs in rows.items():
                def mean(selected):
                    return float(np.average([r['error'] for r in selected], weights=[r['samples'] for r in selected]))
                result[n][condition] = dict(mean=mean(rs), pairs=rs,
                    by_case={c: mean([r for r in rs if r['case'] == c]) for c in ('base',) + CASES})
    return result


def visual(models, out):
    # First reserved scene, chosen by ID before seeing errors; no best-case selection.
    canvas = Image.new('RGB', (96*4, 84*4), '#18212b')
    draw = ImageDraw.Draw(canvas)
    with torch.inference_mode():
        for row, case in enumerate(('base',) + CASES):
            scene, _ = make_frame(TEST_IDS[0], case, yaw=.07)
            values = [scene['source'], scene['target'], infer(models['paired'], scene), infer(models['joint'], scene)]
            for col, (label, value) in enumerate(zip(('Entrada', 'Referencia', 'Pares', 'Conjunto'), values)):
                rgb = value.clip(0)
                rgb = (rgb / (1+rgb)) ** (1/2.2)
                canvas.paste(Image.fromarray(np.uint8(np.round(rgb*255))), (col*96, row*84+20))
                draw.text((col*96+2, row*84+3), label, fill='white')
    canvas.resize((768, 672), Image.Resampling.NEAREST).save(out / 'comparison.png')


def hash_tensors(digest, value):
    if isinstance(value, torch.Tensor):
        digest.update(str((tuple(value.shape), value.dtype)).encode())
        digest.update(value.numpy().tobytes())
    elif isinstance(value, dict):
        for k, v in value.items():
            digest.update(k.encode()); hash_tensors(digest, v)
    elif isinstance(value, (tuple, list)):
        for v in value:
            hash_tensors(digest, v)
    else:
        digest.update(str(value).encode())


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--out', type=Path, default=ROOT / 'artifacts/neural-joint-training-v1')
    parser.add_argument('--steps', type=int, default=1600)
    args = parser.parse_args()
    if args.steps < 1:
        parser.error('steps must be positive')
    args.out.mkdir(parents=True, exist_ok=False)
    torch.set_num_threads(2)
    torch.use_deterministic_algorithms(True)
    torch.manual_seed(20260910)
    initial = LightingNet()
    models = {'identity': None, 'paired': initial, 'joint': copy.deepcopy(initial)}
    protocol = dict(train=list(TRAIN_IDS), validation=list(VALID_IDS), test=list(TEST_IDS),
        steps=args.steps, learning_rate=.002, seed=20260910, batch_seed=732,
        batch='one quartet = four frames; same sampled quartets and identical fresh weights for both networks',
        train_camera_pairs=CAMERA_PAIRS, train_depth_noise=.0005, test_depth_noise=[0., .002],
        paired_objective='image + paired response; existing image and response losses unchanged',
        joint_objective='image + paired response + 1.0 * bidirectional camera error loss',
        temporal='same static geometry, both camera directions in each intervention state; errors against each own target',
        inference='single frame; no oracle masks, target images or temporal state at inference',
        checkpoint='fixed final step; no validation or test selection; one coefficient fixed before training',
        gates=dict(response_vs_zero=.8, max_response_regression=1.05, max_image_regression=1.05,
                   temporal_vs_paired=.8, max_unchanged_drift=.003),
        integration='all synthetic gates necessary but not sufficient; real data, GPU timing, memory and runtime still required',
        game_integration_allowed=False)
    (args.out / 'protocol.json').write_text(json.dumps(protocol, indent=2)+'\n', encoding='utf-8')
    files = ['_IA_Python/neural_joint_training.py', '_IA_Python/neural_lighting_temporal.py',
             '_IA_Python/neural_lighting_scene.py', '_IA_Python/neural_lighting_model.py',
             '_IA_Python/neural_lighting_diagnostics.py', '_IA_Python/neural_intervention_training.py',
             'tools/train_neural_joint.py', 'tools/train_neural_interventions.py', 'tests/test_neural_joint_training.py']
    (args.out / 'source-manifest.json').write_text(json.dumps(
        {f: hashlib.sha256((ROOT/f).read_bytes()).hexdigest() for f in files}, indent=2)+'\n', encoding='utf-8')
    examples = []
    digest = hashlib.sha256()
    for count, seed in enumerate(TRAIN_IDS, 1):
        for yaws in CAMERA_PAIRS:
            for case in CASES:
                example = quartet(seed, case, yaws)
                examples.append(example)
                digest.update(str((seed, case, yaws)).encode()); hash_tensors(digest, example)
        if count % 16 == 0:
            print('dataset', count, '/', len(TRAIN_IDS), flush=True)
    rng = np.random.default_rng(732)
    schedule = rng.integers(len(examples), size=args.steps)
    training = {}
    for name in ('paired', 'joint'):
        model = models[name]
        opt = torch.optim.Adam(model.parameters(), lr=.002)
        start = time.perf_counter(); curve = []
        for step, index in enumerate(schedule, 1):
            example = examples[index]
            opt.zero_grad(set_to_none=True)
            pred = predict_lighting(model, example[0])
            image, response, temporal = losses(pred, example)
            loss = image + response + (temporal if name == 'joint' else 0.)
            if not torch.isfinite(loss):
                raise RuntimeError('Nonfinite loss')
            loss.backward(); opt.step()
            if step == 1 or step % 400 == 0:
                curve.append(dict(step=step, image=float(image.detach()), response=float(response.detach()),
                                  temporal=float(temporal.detach())))
                print(name, curve[-1], flush=True)
        model.eval()
        path = args.out / (name + '.pt')
        torch.save(model.state_dict(), path)
        training[name] = dict(cpu_seconds=time.perf_counter()-start, curve=curve,
                             parameters=sum(p.numel() for p in model.parameters()),
                             sha256=hashlib.sha256(path.read_bytes()).hexdigest())
    print('Evaluating reserved scenes', flush=True)
    validation = evaluate(models, VALID_IDS)
    heldout = evaluate(models, TEST_IDS)
    motion = temporal_evaluation(models, TEST_IDS)
    j, p = heldout['joint'], heldout['paired']
    conditions = ('clean', 'noisy')
    gates = dict(
        response_beats_zero_20pct=all(j[k]['response']['response_mae'] <= .8*j[k]['response']['zero_response_mae'] for k in conditions),
        response_preserved=all(j[k]['response']['response_mae'] <= 1.05*p[k]['response']['response_mae'] for k in conditions),
        image_preserved=all(j[k]['image_mae'] <= 1.05*p[k]['image_mae'] for k in conditions),
        temporal_improved_20pct=all(motion['joint'][k]['mean'] <= .8*motion['paired'][k]['mean'] for k in conditions),
        unchanged_drift_below_003=all(j[k]['response']['unchanged_drift'] <= .003 for k in conditions),
        protected_exact=all(j[k]['protected_max_change'] == 0 for k in conditions))
    result = dict(protocol=protocol, training_data_sha256=digest.hexdigest(),
        schedule_sha256=hashlib.sha256(schedule.tobytes()).hexdigest(), quartets=len(examples),
        training=training, validation=validation, held_out=heldout, temporal=motion, gates=gates,
        synthetic_gates_passed=all(gates.values()), game_integration_allowed=False, torch=torch.__version__,
        limitations=['64x96 diffuse spheres/plane with fixed illumination; no real game scenes, new material synthesis or indirect light.',
                     'One training seed, 12 test scenes from the same family; this does not establish broad generalization.',
                     'Static geometry camera sequences, not dynamic-object motion-vector validation.',
                     'Nearest-pixel reprojection retains sampling error; no history at inference.',
                     'Protected UI/sky copied by explicit validity rule, not learned segmentation.',
                     'CPU FP32 training only. No ONNX/FP16 validation, GPU timing, VRAM measurement or game installation.'])
    (args.out / 'metrics.json').write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    visual(models, args.out)
    print(json.dumps(dict(summary={n: {k: dict(response=v['response'], image=v['image_mae'])
          for k, v in cases.items()} for n, cases in heldout.items()},
          temporal={n: {k: v['mean'] for k, v in cases.items()} for n, cases in motion.items()}, gates=gates), indent=2), flush=True)


if __name__ == '__main__':
    main()
