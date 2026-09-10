import sys
import unittest
from pathlib import Path
import torch
from torch.nn import functional as F
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'_IA_Python'))
from neural_sampling_phase import reduce_geometry, prepare, predict_phase
from neural_regional_inference import predict_regional
from neural_lighting_model import LightingNet


class SamplingPhaseTests(unittest.TestCase):
    def test_zero_phase_matches_existing_operators(self):
        x = torch.rand(2,4,24,36, generator=torch.Generator().manual_seed(8))
        for shape in ((12,18),(6,9)):
            torch.testing.assert_close(reduce_geometry(x,shape), F.interpolate(x,shape,mode='nearest-exact'),rtol=0,atol=0)
            torch.testing.assert_close(reduce_geometry(x,shape,method='area'), F.interpolate(x,shape,mode='area'))

    def test_fractional_box_known_integral_and_center_tie(self):
        x = torch.arange(8,dtype=torch.float32).reshape(1,1,1,8).expand(1,1,4,8)
        # First 4-pixel box shifted +.5: [.5,4.5], mean = 2.
        self.assertEqual(float(reduce_geometry(x,(1,2),(.5,0),'area')[0,0,0,0]),2.)
        self.assertEqual(float(reduce_geometry(x,(1,2),(-.5,0),'area')[0,0,0,0]),1.125)
        self.assertEqual(float(reduce_geometry(x,(1,2),(-.25,0))[0,0,0,0]),1.)
        self.assertEqual(float(reduce_geometry(x,(1,2),(.25,0))[0,0,0,0]),2.)

    def test_constant_including_border_is_phase_invariant(self):
        x = torch.full((1,4,8,12),3.5)
        for method in ('area','center'):
            for phase in ((-.5,.5),(.25,-.25)):
                self.assertTrue(torch.equal(reduce_geometry(x,(2,3),phase,method),torch.full((1,4,2,3),3.5)))

    def test_full_prediction_matches_baseline_and_preserves_protection(self):
        torch.set_num_threads(2)
        model = LightingNet().eval()
        with torch.no_grad(): model.output.weight.fill_(.02)
        source = torch.rand(1,3,128,192)
        valid = torch.ones(1,1,128,192); valid[:,:,:6,:] = 0
        batch = dict(source=source,valid=valid,features=torch.rand(1,8,128,192))
        with torch.inference_mode():
            actual = predict_phase(model,batch,reduce_geometry(batch['features'][:,3:7],(64,96)),prepare(batch))
            torch.testing.assert_close(actual,predict_regional(model,batch),rtol=0,atol=0)
            area = predict_phase(model,batch,reduce_geometry(batch['features'][:,3:7],(64,96),(.5,-.5),'area'),prepare(batch))
        self.assertTrue(torch.equal(area[:,:,:6,:],source[:,:,:6,:]))

    def test_reject_ambiguous_noninteger_scale_or_invalid_phase(self):
        x = torch.zeros(1,4,10,10)
        for shape, phase in (((3,3),(0,0)),((5,5),(float('nan'),0)),((5,5),(1,0))):
            with self.assertRaises(ValueError): reduce_geometry(x,shape,phase)


if __name__ == '__main__': unittest.main()
