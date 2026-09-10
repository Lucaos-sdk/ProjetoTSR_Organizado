import sys
import unittest
from pathlib import Path
import torch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'_IA_Python'))
from neural_lighting_model import LightingNet,predict_lighting
from neural_regional_inference import predict_regional


class RegionalInferenceTests(unittest.TestCase):
    def setUp(self):
        torch.set_num_threads(2); torch.manual_seed(123)

    def batch(self,h,w):
        source=torch.rand(1,3,h,w)
        valid=torch.ones(1,1,h,w); valid[:,:,:3]=0
        return dict(source=source,valid=valid,features=torch.cat([source/(1+source),torch.rand(1,4,h,w),valid],1))

    def test_same_scale_preserves_original_path(self):
        model=LightingNet(); torch.nn.init.normal_(model.output.weight,std=.1)
        batch=self.batch(64,96)
        with torch.no_grad():
            self.assertTrue(torch.equal(predict_lighting(model,batch),predict_regional(model,batch)))

    def test_larger_odd_size_identity_and_protection(self):
        model=LightingNet(); batch=self.batch(135,241)
        with torch.no_grad():
            self.assertTrue(torch.equal(predict_regional(model,batch),batch['source']))
            model.output.bias.fill_(.4)
            output=predict_regional(model,batch)
        self.assertEqual(output.shape,batch['source'].shape)
        self.assertTrue(torch.isfinite(output).all())
        self.assertTrue(torch.equal(output[:,:,:3],batch['source'][:,:,:3]))
        self.assertGreater(float((output-batch['source']).abs().sum()),0.)

    def test_lifts_residual_instead_of_blurring_source_texture(self):
        model=LightingNet(); model.output.bias.data.fill_(.1)
        batch=self.batch(128,192); batch['valid'].fill_(1); batch['features'][:,-1:].fill_(1)
        with torch.no_grad(): output=predict_regional(model,batch)
        expected=.5*torch.tanh(torch.tensor(.1))
        self.assertTrue(torch.allclose(output-batch['source'],torch.full_like(output,expected),atol=1e-7))


if __name__=='__main__': unittest.main()
