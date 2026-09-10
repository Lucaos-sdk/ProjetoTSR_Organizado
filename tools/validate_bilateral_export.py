"""Check trained CNN export using ONNX's CPU reference, not DirectML."""
import argparse
import copy
import json
from pathlib import Path
import sys
import numpy as np
import torch
from torch.nn import functional as F
from onnx.reference import ReferenceEvaluator

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'_IA_Python'))
from bilateral_experiment import BilateralNet, TEST_IDS, make_scene, tensors, slice_grid, compose, predict


def run():
    parser = argparse.ArgumentParser()
    parser.add_argument('--artifact', type=Path, default=ROOT/'artifacts/bilateral-regional-v1')
    args = parser.parse_args()
    torch.set_num_threads(2)
    model = BilateralNet().eval()
    model.load_state_dict(torch.load(args.artifact/'bilateral.pt', weights_only=True))
    half = copy.deepcopy(model).half()
    reference = ReferenceEvaluator(str(args.artifact/'bilateral_grid_fp16.onnx'))
    grid_errors, image_errors = [], []
    with torch.inference_mode():
        for seed in TEST_IDS:
            batch = tensors(make_scene(seed))
            low = F.interpolate(batch['features'], (64, 64), mode='area').half()
            expected_grid = half(low).float()
            exported_grid = torch.from_numpy(reference.run(None, {'context_64':low.numpy()})[0]).float()
            grid_errors.append((exported_grid-expected_grid).abs().max().item())
            actual = compose(slice_grid(exported_grid, batch['source']), batch['source'], batch['valid'])[0]
            expected = predict(model, batch)[0]
            image_errors.append((actual-expected).abs().max().item())
    result = dict(scenes=list(TEST_IDS), backend='ONNX ReferenceEvaluator CPU',
                  grid_fp16_max_abs_difference=max(grid_errors),
                  final_rgb_vs_fp32_max_abs_difference=max(image_errors),
                  finite=bool(np.isfinite(grid_errors+image_errors).all()),
                  tolerances=dict(grid_absolute=.05, final_rgb_absolute=.002),
                  directml_tested=False, gpu_timing_ms=None,
                  scope='FP16 CNN, FP32 slicing/composition. No texture prepack or GPU validation.')
    result['passed'] = result['finite'] and max(grid_errors)<.05 and max(image_errors)<.002
    (args.artifact/'export-validation.json').write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(result, indent=2))
    if not result['passed']:
        raise SystemExit(1)


if __name__ == '__main__':
    run()
