import sys
import unittest
from pathlib import Path
import numpy as np
import torch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'_IA_Python'))
from neural_regional_training import TRAIN_IDS,VALID_IDS,TEST_IDS,quartet,regional_losses
from neural_generalization_scene import make_frame
from neural_regional_inference import predict_regional
from neural_lighting_model import LightingNet


class RegionalTrainingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        torch.set_num_threads(2)
        cls.example=quartet(23001,'boxes',144,96)

    def test_target_optimum_and_mask_separation(self):
        batch,regions,motion,edges=self.example
        self.assertEqual(batch['features'].shape,(4,8,96,144))
        self.assertGreater(float(edges.sum()),0)
        self.assertEqual([(a,b) for a,b,_,_ in motion],[(0,1),(1,0),(2,3),(3,2)])
        for value in regional_losses(batch['target'],self.example):
            self.assertAlmostEqual(float(value),0.,places=7)
        self.assertFalse(set(TRAIN_IDS)&set(VALID_IDS+TEST_IDS))
        self.assertFalse(set(VALID_IDS)&set(TEST_IDS))
        self.assertGreater(min(TRAIN_IDS),22003)

    def test_gradient_flows_through_actual_reconstruction(self):
        model=LightingNet(); pred=predict_regional(model,self.example[0])
        terms=regional_losses(pred,self.example)
        objective=terms[0]+terms[1]+.5*terms[2]+.1*terms[3]+.2*terms[4]
        objective.backward()
        self.assertTrue(torch.isfinite(model.output.weight.grad).all())
        self.assertGreater(float(model.output.weight.grad.abs().sum()),0.)

    def test_depth_noise_changes_input_not_reference(self):
        a,_=make_frame(23000,'boxes',144,96)
        b,_=make_frame(23000,'boxes',144,96,noise=.002)
        np.testing.assert_array_equal(a['source'],b['source'])
        np.testing.assert_array_equal(a['target'],b['target'])
        self.assertGreater(float(np.abs(a['features']-b['features']).sum()),0.)


if __name__=='__main__': unittest.main()
