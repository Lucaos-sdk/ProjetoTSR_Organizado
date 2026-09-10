import copy
import sys
import unittest
from pathlib import Path
import numpy as np
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / '_IA_Python'))
from neural_balance_selection import SEEDS, VALID_IDS, TEST_IDS, model_name, fidelity_metrics, comparisons, select_validation


class BalanceSelectionTests(unittest.TestCase):
    def test_fidelity_detects_geometry_edges_and_gradient(self):
        target = np.zeros((3, 4, 3), np.float32)
        scene = dict(target=target, valid=np.ones((3, 4)), object_id=np.array([[0, 0, 1, 1]]*3))
        self.assertEqual(fidelity_metrics(target, scene)['edge_abs_sum'], 0)
        shifted = target.copy(); shifted[:, 2:] = 1
        m = fidelity_metrics(shifted, scene)
        self.assertEqual(m['edge_samples'], 18)
        self.assertEqual(m['edge_abs_sum'], 9)
        self.assertEqual(m['gradient_abs_sum'], 9)
        scene['valid'][:] = 0
        self.assertEqual(fidelity_metrics(shifted, scene)['edge_samples'], 0)
        self.assertEqual(fidelity_metrics(shifted, scene)['gradient_samples'], 0)

    def test_one_bad_seed_cannot_hide_in_average(self):
        q, m, d = {}, {}, {}
        for seed in SEEDS:
            for variant in ('paired', 't050'):
                name = model_name(seed, variant)
                value = dict(response=dict(response_mae=.02, zero_response_mae=.04, unchanged_drift=.001),
                             image_mae=.1, shadow_mae=.1, protected_max_change=0.)
                q[name] = {c: copy.deepcopy(value) for c in ('clean', 'noisy')}
                m[name] = {c: dict(mean=.005 if variant == 't050' else .01) for c in ('clean', 'noisy')}
                d[name] = {c: dict(edge_mae=.1, gradient_mae=.1) for c in ('clean', 'noisy')}
        self.assertTrue(comparisons(q, m, d, 't050')['passed'])
        q[model_name(SEEDS[1], 't050')]['noisy']['image_mae'] = .106
        report = comparisons(q, m, d, 't050')
        self.assertFalse(report['passed']); self.assertFalse(report['gates']['image'])
        self.assertFalse(select_validation({'t050': report})['validation_eligible'])

    def test_selection_requires_eligibility_before_image_ranking(self):
        reports = dict(a=dict(passed=False, worst_violation=1.1, mean_image_ratio=.8),
                       b=dict(passed=True, worst_violation=.9, mean_image_ratio=1.02),
                       c=dict(passed=True, worst_violation=.95, mean_image_ratio=1.01))
        self.assertEqual(select_validation(reports)['variant'], 'c')
        self.assertFalse(set(VALID_IDS) & set(TEST_IDS))
        self.assertGreater(min(VALID_IDS), 18011)


if __name__ == '__main__':
    unittest.main()
