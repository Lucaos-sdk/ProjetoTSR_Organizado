import sys
import unittest
from pathlib import Path
import numpy as np
import torch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'_IA_Python'))
from neural_geometry_ablation import unit_normal,reduced_camera,geometry_variants,VARIANTS
from neural_lighting_scene import reconstruct_positions,project


class GeometryAblationTests(unittest.TestCase):
    def test_normalization_preserves_direction_and_handles_cancellation(self):
        n=torch.tensor([[[[.3,0.,1e-8]],[[.4,0.,0.]],[[0.,0.,0.]]]])
        expected=torch.tensor([[[[.6,0.,0.]],[[.8,0.,0.]],[[0.,0.,0.]]]])
        torch.testing.assert_close(unit_normal(n),expected)

    def test_low_camera_projects_to_shifted_full_resolution_centers(self):
        cam=dict(origin=np.zeros(3),basis=np.eye(3),fx=40.,fy=38.,cx=23.5,cy=15.5)
        for phase in ((0.,0.),(.5,-.25)):
            low=reduced_camera(cam,(32,48),(8,12),phase)
            world=reconstruct_positions(np.full((8,12),3.),low)
            xy,_=project(world,cam)
            y,x=np.mgrid[:8,:12]
            np.testing.assert_allclose(xy,np.stack([(x+.5)*4-.5+phase[0],(y+.5)*4-.5+phase[1]],-1),atol=1e-6)

    def test_ablations_change_only_named_channels(self):
        cam=dict(origin=np.zeros(3),basis=np.eye(3),fx=20.,fy=20.,cx=7.5,cy=7.5)
        g=torch.rand(1,4,16,16,generator=torch.Generator().manual_seed(47))
        g[:,:3]=unit_normal(g[:,:3]); g[:,3]=.1
        values=geometry_variants(g,cam,(8,8))
        self.assertEqual(set(values),set(VARIANTS))
        torch.testing.assert_close(values['depth_area'][:,:3],values['center'][:,:3],rtol=0,atol=0)
        for n in ('normal_area','normal_unit','magnitude_only'):
            torch.testing.assert_close(values[n][:,3:],values['center'][:,3:],rtol=0,atol=0)
        for n in ('area_unit','postfit'):
            torch.testing.assert_close(values[n][:,3:],values['area'][:,3:],rtol=0,atol=0)
        torch.testing.assert_close(unit_normal(values['magnitude_only'][:,:3]),values['center'][:,:3])
        torch.testing.assert_close(torch.linalg.vector_norm(values['area_unit'][:,:3],dim=1),torch.ones(1,8,8))
        expected=torch.zeros(1,3,6,6); expected[:,2]=-1
        torch.testing.assert_close(values['postfit'][:,:3,1:-1,1:-1],expected,atol=1e-6,rtol=0)


if __name__=='__main__': unittest.main()
