"""Supplementary capacity-matched control, without retraining the candidate."""
import argparse
import json
from pathlib import Path
import sys
import numpy as np
import torch
from train_bilateral_experiment import evaluate, temporal, sha

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'_IA_Python'))
from bilateral_experiment import TRAIN_IDS, TEST_IDS, PointCapacityNet, make_scene, tensors, predict, objective


def run():
    parser = argparse.ArgumentParser()
    parser.add_argument('--artifact', type=Path, default=ROOT/'artifacts/bilateral-regional-v1')
    out = parser.parse_args().artifact
    original = json.loads((out/'metrics.json').read_text())
    steps = original['training']['bilateral']['steps']
    result_path = out/'capacity-control.json'
    if result_path.exists():
        raise FileExistsError('Preserve the previous control before repeating.')
    torch.set_num_threads(2)
    torch.manual_seed(20260908)
    torch.use_deterministic_algorithms(True)
    data = {s:tensors(make_scene(s)) for s in TRAIN_IDS}
    model = PointCapacityNet()
    optimizer = torch.optim.Adam(model.parameters(), lr=.002)
    rng = np.random.default_rng(351)
    curve = []
    for step in range(1, steps+1):
        ids = rng.choice(TRAIN_IDS, 4, replace=False)
        batch = {k:torch.cat([data[s][k] for s in ids]) for k in data[TRAIN_IDS[0]]}
        optimizer.zero_grad(set_to_none=True)
        pred, confidence = predict(model, batch)
        loss = objective(pred, confidence, batch)
        if not torch.isfinite(loss):
            raise RuntimeError('Nonfinite loss')
        loss.backward()
        optimizer.step()
        if step == 1 or step % 100 == 0:
            curve.append(dict(step=step, loss=loss.item()))
            print('capacity_control', step, loss.item(), flush=True)
    model.eval()
    checkpoint = out/'point_capacity.pt'
    torch.save(model.state_dict(), checkpoint)
    held = evaluate(model, TEST_IDS)
    candidate_mae = original['held_out']['bilateral']['mean']['mae']
    result = dict(parameters=sum(p.numel() for p in model.parameters()), steps=steps, seed=20260908,
                  checkpoint_sha256=sha(checkpoint), curve=curve, held_out=held, temporal=temporal(model),
                  candidate_mae=candidate_mae, candidate_improvement_percent=100*(1-candidate_mae/held['mean']['mae']),
                  scope='Supplementary same-input, same-loss, same-batches 1x1 control. No candidate retraining or checkpoint selection.',
                  limitations='Single seed and fixed training schedule; not an architecture superiority proof.')
    result_path.write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(dict(mean=held['mean'], candidate_improvement_percent=result['candidate_improvement_percent']), indent=2))


if __name__ == '__main__':
    run()
