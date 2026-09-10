import sys
import unittest
from pathlib import Path
import torch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / '_IA_Python'))
from neural_joint_training import TRAIN_IDS, VALID_IDS, TEST_IDS, CAMERA_PAIRS, quartet, losses


class JointTrainingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        torch.set_num_threads(2)
        cls.example = quartet(16001, 'move_one', CAMERA_PAIRS[0])

    def test_separate_geometry_and_camera_correspondences(self):
        batch, regions, motion = self.example
        self.assertEqual(batch['features'].shape, (4, 8, 64, 96))
        self.assertEqual([(a, b) for a, b, _, _ in motion], [(0, 1), (1, 0), (2, 3), (3, 2)])
        for _, _, index, visible in motion:
            self.assertGreater(float(visible.sum()), 100)
            self.assertGreaterEqual(int(index.min()), 0)
            self.assertLess(int(index.max()), 64*96)
        for changed, stable in regions:
            self.assertFalse(bool(((changed > 0) & (stable > 0)).any()))
        self.assertFalse(set(TRAIN_IDS) & set(VALID_IDS + TEST_IDS))
        self.assertFalse(set(VALID_IDS) & set(TEST_IDS))
        self.assertGreater(min(TRAIN_IDS), 15011)

    def test_reference_is_optimum_for_all_three_objectives(self):
        values = losses(self.example[0]['target'], self.example)
        for value in values:
            self.assertAlmostEqual(float(value), 0., places=7)

    def test_temporal_penalty_trains_both_scene_states(self):
        batch = self.example[0]
        pred = batch['target'].clone()
        pred[1] += .1
        pred[3] += .2
        pred.requires_grad_()
        _, _, temporal = losses(pred, self.example)
        self.assertGreater(float(temporal.detach()), .1)
        temporal.backward()
        self.assertTrue(bool(torch.isfinite(pred.grad).all()))
        for grad in pred.grad:
            self.assertGreater(float(grad.abs().sum()), 0.)


if __name__ == '__main__':
    unittest.main()
