import sys
import unittest
from pathlib import Path
import numpy as np
import torch
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'_IA_Python'))
from bilateral_experiment import BilateralNet, tensors
from bilateral_edges import (TRAIN_IDS, VALID_IDS, TEST_IDS, stress_scene, guided_log_gain,
                             geometry_weights, edge_error, predict_variant)


def guided_reference(guide, signal, valid, radius, epsilon):
    # Independent explicit clipped-window NumPy oracle, including both stages.
    h, w = guide.shape
    a, b = np.zeros_like(guide), np.zeros_like(guide)
    def window(y, x):
        return np.s_[max(0,y-radius):min(h,y+radius+1), max(0,x-radius):min(w,x+radius+1)]
    for y in range(h):
        for x in range(w):
            sl = window(y,x)
            mask = valid[sl] > 0
            if not mask.any():
                continue
            i, p = guide[sl][mask], signal[sl][mask]
            a[y,x] = ((i*p).mean()-i.mean()*p.mean())/(i.var()+epsilon)
            b[y,x] = p.mean()-a[y,x]*i.mean()
    out = np.zeros_like(guide)
    for y in range(h):
        for x in range(w):
            if not valid[y,x]:
                continue
            sl = window(y,x)
            mask = valid[sl] > 0
            out[y,x] = np.clip(a[sl][mask].mean()*guide[y,x]+b[sl][mask].mean(), -.25, .25)
    return out


class EdgeTests(unittest.TestCase):
    def test_guided_independent_reference_holes_and_borders(self):
        rng = np.random.default_rng(51)
        guide, signal = rng.uniform(0, 1, (7, 11)), rng.uniform(-.2, .2, (7, 11))
        valid = (rng.uniform(0, 1, (7, 11)) > .3).astype(float)
        actual = guided_log_gain(*[torch.tensor(x)[None, None] for x in (guide, signal, valid)])
        np.testing.assert_allclose(actual[0,0].numpy(), guided_reference(guide, signal, valid, 2, 1e-3), atol=1e-12)
        self.assertTrue(torch.equal(guided_log_gain(torch.ones(1,1,3,4), torch.ones(1,1,3,4), torch.zeros(1,1,3,4)), torch.zeros(1,1,3,4)))

    def test_filter_derivative(self):
        torch.manual_seed(7)
        guide = torch.rand(1,1,3,5,dtype=torch.double)
        signal = (torch.rand_like(guide)*.1).requires_grad_()
        self.assertTrue(torch.autograd.gradcheck(lambda p: guided_log_gain(guide,p,torch.ones_like(p)), (signal,)))

    def test_geometry_edges_and_reference_loss(self):
        geometry = torch.zeros(1,4,9,11)
        geometry[:,2] = 1
        geometry[:,3,:,6:] = 1
        weights = geometry_weights(geometry)
        self.assertGreater(float(weights[0,0,4,5]), float(weights[0,0,4,1]))
        self.assertTrue(bool(((weights >= 1) & (weights <= 4)).all()))
        # Matching sharp target must incur zero loss; do not penalize sharpness itself.
        self.assertEqual(float(edge_error(torch.zeros(1,3,9,11), geometry)), 0.)
        error = torch.zeros(1,3,9,11)
        error[:,:,:,6:] = .1
        self.assertGreater(float(edge_error(error, geometry)), float(edge_error(error, geometry*0)))

    def test_identity_masks_and_gain_bound(self):
        batch = tensors(stress_scene(5000, 'combined', height=37, width=51))
        model = BilateralNet()
        actual, _ = predict_variant(model, batch, True)
        self.assertTrue(torch.equal(actual, batch['source']))
        with torch.no_grad():
            model.layers[-1].bias.fill_(100)
        actual, _ = predict_variant(model,batch,True)
        mask = (batch['valid'] == 0).expand_as(actual)
        self.assertTrue(torch.equal(actual[mask],batch['source'][mask]))
        ratio = actual/batch['source']
        self.assertLessEqual(float(ratio.max().detach()), float(np.exp(.25))+1e-6)
        torch.testing.assert_close(ratio[:,:1],ratio[:,2:3],atol=2e-6,rtol=0)

    def test_dataset_separation_exposure_and_context_noise(self):
        self.assertFalse(set(TRAIN_IDS) & set(VALID_IDS+TEST_IDS))
        self.assertFalse(set(VALID_IDS) & set(TEST_IDS))
        a, b = stress_scene(5001), stress_scene(5001,'combined')
        np.testing.assert_allclose(a['target']/a['source'], b['target']/b['source'], atol=2e-7)
        np.testing.assert_array_equal(a['clean_geometry'],b['clean_geometry'])
        self.assertGreater(float(np.abs(a['features'][...,3:7]-b['features'][...,3:7]).sum()),0.)
        np.testing.assert_array_equal(b['features'],stress_scene(5001,'combined')['features'])
        a,b = stress_scene(5002,'local_exposure'), stress_scene(5002,'local_exposure',pan=2)
        mask = (a['valid'][:,2:]*b['valid'][:,:-2]) > 0
        np.testing.assert_allclose(a['target'][:,2:][mask],b['target'][:,:-2][mask],atol=2e-6)


if __name__ == '__main__':
    unittest.main()
