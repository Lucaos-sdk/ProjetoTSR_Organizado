import sys
import unittest
from pathlib import Path
import numpy as np
import torch
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'_IA_Python'))
from bilateral_experiment import (TRAIN_IDS, VALID_IDS, TEST_IDS, BilateralNet,
                                 make_scene, tensors, predict, slice_grid, slice_numpy, compose)


class BilateralTests(unittest.TestCase):
    def test_slicing_independent_reference_odd_sizes_hdr(self):
        rng = np.random.default_rng(5)
        grid = rng.normal(size=(2, 8, 16, 16)).astype(np.float32)
        source = rng.uniform(0, 10, (7, 11, 3)).astype(np.float32)
        source[0, 0] = 0
        source[-1, -1] = 10000
        actual = slice_grid(torch.from_numpy(grid)[None], torch.from_numpy(source.transpose(2, 0, 1).copy())[None])
        np.testing.assert_allclose(actual[0].permute(1, 2, 0).numpy(), slice_numpy(grid, source), atol=5e-6)

    def test_identity_and_exact_mask_preservation(self):
        batch = tensors(make_scene(11, 37, 51))
        prediction, _ = predict(BilateralNet(), batch)
        torch.testing.assert_close(prediction, batch['source'], atol=0, rtol=0)
        coeff = torch.full((1, 2, 37, 51), 100.)
        prediction, _ = compose(coeff, batch['source'], batch['valid'])
        mask = batch['valid'].expand_as(prediction) == 0
        self.assertTrue(torch.equal(prediction[mask], batch['source'][mask]))
        ratio = prediction/batch['source']
        torch.testing.assert_close(ratio[:, :1], ratio[:, 2:3], atol=2e-6, rtol=0)
        self.assertLessEqual(float(ratio.max()), np.exp(.25)+1e-6)

    def test_no_scene_leakage_and_known_pan(self):
        self.assertFalse(set(TRAIN_IDS) & set(VALID_IDS+TEST_IDS))
        self.assertFalse(set(VALID_IDS) & set(TEST_IDS))
        a, b = make_scene(2000), make_scene(2000, pan=2)
        mask = (a['valid'][:, 2:] > 0) & (b['valid'][:, :-2] > 0)
        np.testing.assert_allclose(a['target'][:, 2:][mask], b['target'][:, :-2][mask], atol=2e-6)
        np.testing.assert_array_equal(a['source'], make_scene(2000)['source'])

    def test_slicing_gradient(self):
        torch.manual_seed(3)
        grid = torch.randn(1, 2, 8, 16, 16, dtype=torch.float64, requires_grad=True)
        source = torch.full((1, 3, 3, 5), .4, dtype=torch.float64)
        out = slice_grid(grid, source).square().sum()
        out.backward()
        index = tuple(torch.nonzero(grid.grad.abs() == grid.grad.abs().max())[0].tolist())
        expected = grid.grad[index].item()
        with torch.no_grad():
            original = grid[index].item()
            grid[index] = original+1e-5
            plus = slice_grid(grid, source).square().sum().item()
            grid[index] = original-1e-5
            minus = slice_grid(grid, source).square().sum().item()
        self.assertAlmostEqual(expected, (plus-minus)/2e-5, places=7)


if __name__ == '__main__':
    unittest.main()
