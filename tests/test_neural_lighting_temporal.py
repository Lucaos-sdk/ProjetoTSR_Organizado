import sys
import unittest
from pathlib import Path
import torch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'_IA_Python'))
from neural_lighting_temporal import temporal_error_loss


class TemporalLightingTests(unittest.TestCase):
    def test_reference_changes_are_not_penalized(self):
        a=torch.ones(1,3,2,3); b=a*2
        indices=torch.tensor([[[2,1,0],[5,4,3]]])
        self.assertEqual(float(temporal_error_loss(a,a,b,b,indices,torch.ones(1,1,2,3))),0.)

    def test_correspondence_mask_and_gradient(self):
        a=torch.zeros(1,3,1,3,requires_grad=True)
        b=torch.tensor([[[[3.,2.,1.]],[[3.,2.,1.]],[[3.,2.,1.]]]],requires_grad=True)
        indices=torch.tensor([[[2,1,0]]]); mask=torch.tensor([[[[1.,0.,0.]]]])
        loss=temporal_error_loss(a,torch.zeros_like(a),b,torch.zeros_like(b),indices,mask)
        self.assertEqual(float(loss.detach()),1.)
        loss.backward()
        self.assertTrue(torch.equal(b.grad[:,:,:,:2],torch.zeros(1,3,1,2)))
        self.assertGreater(float(b.grad[:,:,:,2:].abs().sum()),0.)
        self.assertEqual(float(temporal_error_loss(a,a,b,b,indices,mask*0).detach()),0.)


if __name__=='__main__': unittest.main()
