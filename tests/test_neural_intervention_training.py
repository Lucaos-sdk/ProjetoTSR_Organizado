import sys
import unittest
from pathlib import Path
import numpy as np
import torch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'_IA_Python'))
from bilateral_experiment import tensors
from neural_intervention_training import TRAIN_IDS,VALID_IDS,TEST_IDS,layout,change_layout,make_frame,masks,paired_loss


class InterventionTrainingTests(unittest.TestCase):
    def test_layout_separation_and_changes(self):
        counts=set()
        for seed in TRAIN_IDS:
            spheres=layout(seed); counts.add(len(spheres))
            for i,(c,r,col) in enumerate(spheres):
                self.assertEqual(c[1],r)
                for d,s,_ in spheres[:i]: self.assertGreater(float(np.linalg.norm((c-d)[[0,2]])),r+s)
            moved=change_layout(seed,'move_one'); idx=seed%len(spheres)
            self.assertGreater(float(np.linalg.norm(moved[idx][0]-spheres[idx][0])),.9)
            np.testing.assert_array_equal(moved[idx][2],spheres[idx][2])
            self.assertEqual(len(change_layout(seed,'remove_one')),len(spheres)-1)
            self.assertEqual(change_layout(seed,'remove_all'),[])
        self.assertEqual(counts,{1,2,3,4})
        self.assertFalse(set(TRAIN_IDS)&set(VALID_IDS+TEST_IDS)); self.assertFalse(set(VALID_IDS)&set(TEST_IDS))

    def test_reference_pair_and_gradient(self):
        a,_=make_frame(15001); b,_=make_frame(15001,'remove_all')
        change,stable=masks(a,b)
        self.assertGreater(float(change.sum()),0)
        self.assertFalse(((change>0)&(stable>0)).any())
        ba,bb=tensors(a),tensors(b)
        cm=torch.from_numpy(change)[None,None]; sm=torch.from_numpy(stable)[None,None]
        self.assertEqual(float(paired_loss(ba['target'],bb['target'],ba,bb,cm,sm)),0.)
        pa=ba['source'].clone().requires_grad_(); pb=bb['source'].clone().requires_grad_()
        loss=paired_loss(pa,pb,ba,bb,cm,sm)
        self.assertGreater(float(loss.detach()),0.)
        loss.backward()
        self.assertTrue(torch.isfinite(pa.grad).all()); self.assertGreater(float(pa.grad.abs().sum()),0.)
        self.assertEqual(float(paired_loss(pa,pb,ba,bb,cm*0,sm*0).detach()),0.)

    def test_same_scene_seed_and_noise_reference(self):
        a,_=make_frame(15002); b,_=make_frame(15002,noise=.002)
        np.testing.assert_array_equal(a['target'],b['target'])
        np.testing.assert_array_equal(a['features'],make_frame(15002)[0]['features'])
        self.assertTrue(all(np.isfinite(v).all() for v in b.values()))
        mask=a['valid']==0
        np.testing.assert_array_equal(a['source'][mask],a['target'][mask])


if __name__=='__main__': unittest.main()
