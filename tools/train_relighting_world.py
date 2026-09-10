"""V2: same controlled diffuse target, trained on the entire normal sphere.

This is a small learned lighting transform, not learned game materials/shadows.
Held-out orientations are only evaluated after the fixed training schedule.
"""
from pathlib import Path
import json
import hashlib
import numpy as np
from train_relighting_fixture import normalize, illumination, initialize, forward, loss_grad, self_test


def train():
    self_test()
    out = Path(__file__).resolve().parents[1]/'artifacts/relighting-world-v2'
    out.mkdir(parents=True, exist_ok=True)
    rng = np.random.default_rng(20260907)
    x = normalize(rng.normal(size=(65536, 3))).astype(np.float32)
    y = np.log(illumination(x, True)/illumination(x)).astype(np.float32)
    p = initialize(72)
    m, v = [{k: np.zeros_like(a) for k, a in p.items()} for _ in range(2)]
    steps = 12000
    for step in range(1, steps+1):
        indices = rng.integers(0, len(x), size=512)
        _, grads = loss_grad(p, x[indices], y[indices])
        rate = .003 if step <= 9000 else .001
        for k in p:
            m[k] = .9*m[k]+.1*grads[k]
            v[k] = .999*v[k]+.001*grads[k]**2
            p[k] -= rate*(m[k]/(1-.9**step))/(np.sqrt(v[k]/(1-.999**step))+1e-8)
    held = normalize(np.random.default_rng(9871).normal(size=(32768, 3))).astype(np.float32)
    target = np.log(illumination(held, True)/illumination(held)).astype(np.float32)
    prediction = forward(p, held)[0]
    error = prediction-target
    mse = float(np.mean(error**2))
    assert np.isfinite(prediction).all() and mse < .0015, mse
    np.savez(out/'weights.npz', **p)
    packed = np.zeros((49, 4), np.float32)
    packed[:24, :3] = p['w1'].T
    packed[:24, 3] = p['b1']
    packed[24:48, :3] = p['w2']
    packed[48, :3] = p['b2']
    packed.astype('<f4').tofile(out/'weights.bin')
    metrics = dict(model='world_v2', parameters=171, training_seed=20260907, initialization_seed=72,
        train_normals=len(x), held_out_seed=9871, held_out_normals=len(held), steps=steps,
        full_sphere=True, log_gain_mse=mse, log_gain_p95_abs=float(np.quantile(abs(error), .95)),
        log_gain_max_abs=float(abs(error).max()),
        negative_z_mse=float(np.mean(error[held[:, 2]<0]**2)),
        positive_z_mse=float(np.mean(error[held[:, 2]>=0]**2)),
        weights_sha256=hashlib.sha256((out/'weights.bin').read_bytes()).hexdigest(),
        limitations=['Analytic fixed diffuse target, no game training data.',
                     'No cast shadows, physical material recovery or generative image synthesis.',
                     'Quality in game requires visual comparison.'])
    (out/'metrics.json').write_text(json.dumps(metrics, indent=2)+'\n')
    print(json.dumps(metrics, indent=2))


if __name__ == '__main__':
    train()
