"""Controlled diffuse-relighting experiment; not a game model or FSR modification.

NumPy training of normal -> RGB log-gain, with known normals and fixed lights.
Scenes have no cast shadows, specular BRDF, exposure changes, UI or motion.
Held-out scenes are never used for training or selecting a checkpoint.
"""
import argparse
import hashlib
import json
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw


def normalize(x):
    return x / np.maximum(np.linalg.norm(x, axis=-1, keepdims=True), 1e-8)


def illumination(normals, target=False):
    if target:
        key = np.maximum(normals @ normalize(np.array([-.75, .35, .65])), 0)[..., None]
        fill = np.maximum(normals @ normalize(np.array([.7, -.2, .4])), 0)[..., None]
        return .14 + key * np.array([.95, .66, .40]) + fill * np.array([.12, .24, .43])
    key = np.maximum(normals @ normalize(np.array([.35, -.45, .85])), 0)[..., None]
    return np.repeat(.30 + .65 * key, 3, axis=-1)


def scene(seed, size=160):
    rng = np.random.default_rng(seed)
    y, x = np.mgrid[-1:1:complex(size), -1:1:complex(size)]
    dx, dy = np.zeros_like(x), np.zeros_like(y)
    for _ in range(9):
        cx, cy = rng.uniform(-1, 1, 2)
        radius, amp = rng.uniform(.12, .45), rng.uniform(-.35, .55)
        bump = amp * np.exp(-((x-cx)**2 + (y-cy)**2) / (2*radius**2))
        dx -= bump * (x-cx) / radius**2
        dy -= bump * (y-cy) / radius**2
    normals = normalize(np.stack([-dx, -dy, np.ones_like(x)], -1)).astype(np.float32)
    palette = rng.uniform(.12, .85, (2, 3))
    blend = (.5 + .5*np.sin(x*rng.uniform(5, 12) + y*rng.uniform(3, 8)))[..., None]
    albedo = (palette[0]*blend + palette[1]*(1-blend)).astype(np.float32)
    source = (albedo * illumination(normals)).astype(np.float32)
    target = (albedo * illumination(normals, True)).astype(np.float32)
    return normals, source, target


def initialize(seed=41):
    rng = np.random.default_rng(seed)
    return dict(w1=rng.normal(0, .3, (3, 24)).astype(np.float32), b1=np.zeros(24, np.float32),
                w2=rng.normal(0, .05, (24, 3)).astype(np.float32), b2=np.zeros(3, np.float32))


def forward(p, x):
    hidden = np.tanh(x @ p['w1'] + p['b1'])
    return hidden @ p['w2'] + p['b2'], hidden


def loss_grad(p, x, y):
    prediction, hidden = forward(p, x)
    error = prediction-y
    delta = (2/error.size)*error
    back = (delta @ p['w2'].T)*(1-hidden*hidden)
    return float(np.mean(error*error)), dict(w1=x.T@back, b1=back.sum(0),
                                            w2=hidden.T@delta, b2=delta.sum(0))


def apply(p, normals, source, strength=1.):
    if not 0 <= strength <= 1:
        raise ValueError('strength must be in [0,1]')
    if strength == 0:
        return source.copy()
    log_gain, _ = forward(p, normals)
    return source * np.exp(np.clip(log_gain, -2, 2)*strength)


def self_test():
    # Independent finite differences check the handwritten training derivatives.
    p = {k: v.astype(np.float64) for k, v in initialize().items()}
    rng = np.random.default_rng(7)
    x, y = rng.normal(size=(8, 3)), rng.normal(size=(8, 3))
    _, grads = loss_grad(p, x, y)
    for key, value in p.items():
        for index in [0, value.size//2, value.size-1]:
            original = value.flat[index]
            value.flat[index] = original+1e-5
            plus = loss_grad(p, x, y)[0]
            value.flat[index] = original-1e-5
            minus = loss_grad(p, x, y)[0]
            value.flat[index] = original
            np.testing.assert_allclose(grads[key].flat[index], (plus-minus)/2e-5, rtol=1e-5, atol=1e-8)
    normals, source, _ = scene(3, 16)
    assert np.array_equal(apply(p, normals, source, 0), source)
    print('PASS gradient finite differences and exact bypass')


def train(output, steps):
    output.mkdir(parents=True, exist_ok=True)
    rng = np.random.default_rng(51)
    train_seeds, test_seeds = list(range(100, 112)), list(range(900, 904))
    inputs, labels, colors, targets = [], [], [], []
    for seed in train_seeds:
        n, src, dst = scene(seed)
        index = rng.choice(n.shape[0]*n.shape[1], 4096, replace=False)
        inputs.append(n.reshape(-1, 3)[index])
        labels.append(np.log(illumination(n, True)/illumination(n)).reshape(-1, 3)[index])
        colors.append(src.reshape(-1, 3)[index]); targets.append(dst.reshape(-1, 3)[index])
    x, y = np.concatenate(inputs), np.concatenate(labels).astype(np.float32)
    source, target = np.concatenate(colors), np.concatenate(targets)
    global_gain = (source*target).sum(0)/(source*source).sum(0)
    p = initialize()
    initial_loss = loss_grad(p, x, y)[0]
    m, v = [{k: np.zeros_like(a) for k, a in p.items()} for _ in range(2)]
    for step in range(1, steps+1):
        indices = rng.integers(0, len(x), size=512)
        _, grads = loss_grad(p, x[indices], y[indices])
        for k in p:
            m[k] = .9*m[k] + .1*grads[k]
            v[k] = .999*v[k] + .001*grads[k]**2
            p[k] -= .003*(m[k]/(1-.9**step))/(np.sqrt(v[k]/(1-.999**step))+1e-8)
    np.savez(output/'weights.npz', **p)
    with np.load(output/'weights.npz', allow_pickle=False) as saved:
        loaded = {k: saved[k].copy() for k in p}
    np.testing.assert_array_equal(forward(p, x[:32])[0], forward(loaded, x[:32])[0])
    results, panels = [], []
    for seed in test_seeds:
        n, src, dst = scene(seed)
        prediction = apply(loaded, n, src)
        assert np.isfinite(prediction).all()
        mse = lambda a: float(np.mean((a.astype(np.float64)-dst)**2))
        result = dict(seed=seed, identity_mse=mse(src), global_gain_mse=mse(src*global_gain), trained_mse=mse(prediction))
        results.append(result)
        panels.append((src, prediction, dst, np.abs(prediction-dst)*8))
    canvas = Image.new('RGB', (640, 4*184+42), '#181c24')
    draw = ImageDraw.Draw(canvas)
    draw.text((8, 5), 'SYNTHETIC ONLY - known normals, fixed diffuse lighting', fill='white')
    for i, title in enumerate(['Input', 'Trained (171 params)', 'Analytic target', 'Absolute error x8']):
        draw.text((i*160+4, 25), title, fill='white')
    for row, items in enumerate(panels):
        for col, item in enumerate(items):
            # Fixed display transfer shared by all color panels; no auto-contrast.
            display = np.clip(item, 0, 1)
            if col != 3:
                display = np.where(display <= .0031308, 12.92*display, 1.055*display**(1/2.4)-.055)
            canvas.paste(Image.fromarray(np.uint8(np.round(display*255))), (col*160, 42+row*184))
        draw.text((5, 204+row*184), f'Held-out scene {test_seeds[row]}', fill='white')
    canvas.save(output/'comparison.png')
    report = dict(experiment='known-normal fixed-light diffuse log-gain MLP', seed=41, steps=steps,
                  parameters=sum(a.size for a in p.values()), train_scene_seeds=train_seeds,
                  held_out_scene_seeds=test_seeds, sampled_training_pixels=len(x),
                  initial_train_log_gain_mse=initial_loss, final_train_log_gain_mse=loss_grad(p, x, y)[0],
                  trained_global_gain=global_gain.tolist(), scenes=results,
                  weights_sha256=hashlib.sha256((output/'weights.npz').read_bytes()).hexdigest(),
                  limitations=['Known normals required; not available in current game contract.',
                               'Fixed diffuse lights only; no shadows, specular, learned materials or temporal validation.',
                               'No game integration, GPU timing or superiority to FSR/DLSS demonstrated.'])
    (output/'metrics.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(report, indent=2))
    if not all(r['trained_mse'] < min(r['identity_mse'], r['global_gain_mse']) for r in results):
        raise RuntimeError('Trained model failed the held-out baseline comparison')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=Path('artifacts/relighting-fixture-v1'))
    parser.add_argument('--steps', type=int, default=4000)
    parser.add_argument('--self-test', action='store_true')
    args = parser.parse_args()
    self_test()
    if not args.self_test:
        if args.steps < 1:
            parser.error('--steps must be positive')
        train(args.output, args.steps)
