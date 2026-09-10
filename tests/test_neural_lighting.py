import sys
import unittest
from pathlib import Path
import numpy as np
import torch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'_IA_Python'))
from bilateral_experiment import tensors
from neural_lighting_scene import (sphere_hit,camera,reconstruct_positions,depth_normals,
    render,project,correspondences,TRAIN_IDS,VALID_IDS,TEST_IDS,direct_light)
from neural_lighting_model import LightingNet,PointLightingNet,predict_lighting,lighting_loss


class LightingTests(unittest.TestCase):
    def test_ray_sphere_known_distances_miss_inside(self):
        origins=np.array([[0,0,-3],[0,0,-3],[0,0,0]],dtype=float)
        rays=np.array([[0,0,1],[0,1,0],[1,0,0]],dtype=float)
        hits=sphere_hit(origins,rays,np.zeros(3),1.)
        self.assertEqual(hits[0],2.)
        self.assertTrue(np.isinf(hits[1]))
        self.assertEqual(hits[2],1.)

    def test_occluder_changes_light_not_albedo(self):
        pos=np.array([[[0.,0.,0.]]]); normal=np.array([[[0.,1.,0.]]]); albedo=np.ones((1,1,3))*.5
        clear,_,_=direct_light(pos,normal,albedo,[],[0,4,0],[50,50,50])
        shaded,free,visibility=direct_light(pos,normal,albedo,[(np.array([0,2,0]),.75,np.ones(3))],[0,4,0],[50,50,50])
        np.testing.assert_allclose(free,clear,atol=1e-12)
        self.assertGreater(float(clear.mean()),0.)
        self.assertEqual(float(shaded.max()),0.)
        self.assertEqual(float(visibility.max()),0.)

    def test_perspective_roundtrip_rotated_camera(self):
        cam=camera(37,51,.13)
        y,x=np.mgrid[:37,:51]
        depth=np.ones((37,51))*8
        world=reconstruct_positions(depth,cam)
        xy,z=project(world,cam)
        np.testing.assert_allclose(xy,np.stack([x,y],-1),atol=1e-12)
        np.testing.assert_allclose(z,depth,atol=1e-12)
        normal,valid=depth_normals(depth,cam)
        expected=-cam['basis'][:,2]
        np.testing.assert_allclose(normal[valid],np.broadcast_to(expected,normal[valid].shape),atol=1e-10)

    def test_scene_shadows_masks_normals_and_reprojection(self):
        a,cam=render(8001)
        b,cam_b=render(8001,yaw=.04)
        self.assertGreater(int((a['shadow_effect']>.005).sum()),10)
        self.assertGreater(float(np.abs(a['source']-a['target']).mean()),.01)
        protected=a['valid']==0
        np.testing.assert_array_equal(a['source'][protected],a['target'][protected])
        plane=(a['object_id']==0)&(a['normal_valid']>0)
        estimated=a['features'][...,3:6][plane]
        # Some pixels border a sphere; require the median plane normal accurate.
        angle=np.rad2deg(np.arccos(np.clip(estimated[:,1],-1,1)))
        self.assertLess(float(np.median(angle)),.02)
        iy,ix,visible=correspondences(a,b,cam_b)
        self.assertGreater(int(visible.sum()),500)
        np.testing.assert_array_equal(a['object_id'][visible],b['object_id'][iy,ix][visible])
        self.assertGreater(float(np.abs(a['source']-b['source']).mean()),.001)
        for value in a.values(): self.assertTrue(np.isfinite(value).all())

    def test_disjoint_scenes_and_noise_not_in_target(self):
        self.assertFalse(set(TRAIN_IDS)&set(VALID_IDS+TEST_IDS))
        self.assertFalse(set(VALID_IDS)&set(TEST_IDS))
        a,_=render(8002); b,_=render(8002,depth_noise=.001)
        np.testing.assert_array_equal(a['target'],b['target'])
        self.assertGreater(float(np.abs(a['features']-b['features']).sum()),0.)

    def test_identity_odd_sizes_bounds_and_gradients(self):
        data,_=render(8003,height=33,width=49)
        batch=tensors(data)
        for model in (LightingNet(),PointLightingNet()):
            pred=predict_lighting(model,batch)
            self.assertTrue(torch.equal(pred,batch['source']))
            loss=lighting_loss(pred,batch)
            loss.backward()
            self.assertTrue(all(p.grad is not None and torch.isfinite(p.grad).all() for p in model.parameters()))
        model=LightingNet()
        with torch.no_grad(): model.output.bias.copy_(torch.tensor([100.,-100.,100.]))
        pred=predict_lighting(model,batch)
        mask=(batch['valid']==0).expand_as(pred)
        self.assertTrue(torch.equal(pred[mask],batch['source'][mask]))
        self.assertLessEqual(float((pred-batch['source']).abs().max().detach()),.500001)
        self.assertGreaterEqual(float(pred.min().detach()),0.)


if __name__=='__main__': unittest.main()
