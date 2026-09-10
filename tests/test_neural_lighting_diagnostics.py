import sys
import unittest
from pathlib import Path
import numpy as np
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'_IA_Python'))
from neural_lighting_scene import render,scene_spec,camera,depth_normals
from neural_lighting_diagnostics import intervention,receiver_mask,response_metrics,normal_variant,fitted_depth_normals


class DiagnosticTests(unittest.TestCase):
    def test_default_override_compatibility_and_no_mutation(self):
        a,_=render(10000); spheres,_,_=scene_spec(10000)
        b,_=render(10000,spheres_override=spheres)
        for k in a: np.testing.assert_array_equal(a[k],b[k])
        moved=intervention(10000,'move_middle_left')
        np.testing.assert_array_equal(spheres[1][0],scene_spec(10000)[0][1][0])
        self.assertNotEqual(float(spheres[1][0][0]),float(moved[1][0][0]))
        empty,_=render(10000,spheres_override=[])
        self.assertFalse((empty['object_id']>0).any())

    def test_fixed_receivers_and_reference_response(self):
        a,_=render(10001); b,_=render(10001,spheres_override=intervention(10001,'remove_middle'))
        mask=receiver_mask(a,b)
        self.assertGreater(int(mask.sum()),1000)
        np.testing.assert_array_equal(a['world'][mask],b['world'][mask])
        np.testing.assert_array_equal(a['depth'][mask],b['depth'][mask])
        truth,*_=response_metrics(a['target'],b['target'],a,b)
        identity,*_=response_metrics(a['source'],b['source'],a,b)
        self.assertGreater(truth['changed_pixels'],0)
        self.assertEqual(truth['response_mae'],0.)
        self.assertAlmostEqual(truth['response_gain'],1.)
        self.assertAlmostEqual(truth['cosine'],1.)
        self.assertEqual(identity['response_mae'],identity['zero_response_mae'])
        self.assertEqual(identity['response_gain'],0.)
        self.assertEqual(identity['unchanged_residual_drift'],0.)

    def test_same_input_negative_control_and_normal_ablation(self):
        a,_=render(10002,depth_noise=.002)
        row,*_=response_metrics(a['target'],a['target'],a,a)
        self.assertEqual(row['changed_pixels'],0)
        self.assertIsNone(row['response_mae'])
        b=normal_variant(a,True)
        np.testing.assert_array_equal(a['target'],b['target'])
        self.assertFalse(np.array_equal(a['features'][...,3:6],b['features'][...,3:6]))
        self.assertTrue(np.isfinite(b['features']).all())
        with self.assertRaises(ValueError): render(1,spheres_override=[([0,0,0],-1,[1,1,1])])

    def test_inverse_depth_fit_noise_and_depth_step(self):
        cam=camera(41,63,.12)
        clean=np.full((41,63),8.)
        noisy=clean*(1+np.random.default_rng(15).normal(0,.002,clean.shape))
        direct,_=depth_normals(noisy,cam); fit,used=fitted_depth_normals(noisy,cam)
        expected=-cam['basis'][:,2]
        angle=lambda n: np.rad2deg(np.arccos((n@expected).clip(-1,1)))
        # The original derivative estimator marks the image perimeter invalid.
        self.assertGreater(float(used[1:-1,1:-1].mean()),.95)
        self.assertLess(float(np.median(angle(fit))),.5*float(np.median(angle(direct))))
        step=clean.copy(); step[:,32:]=20
        normals,_=fitted_depth_normals(step,cam)
        self.assertLess(float(angle(normals)[3:-3,29:35].max()),.01)
        invalid,_=fitted_depth_normals(np.zeros_like(clean),cam)
        self.assertTrue(np.isfinite(invalid).all())
        self.assertEqual(float(np.abs(invalid).max()),0.)

    def test_sloped_plane_keeps_valid_neighbors(self):
        cam=camera(41,63,.12)
        yy,xx=np.mgrid[:41,:63]
        depth=1/(.125+.003*(yy-cam['cy']))
        baseline,old_mask=fitted_depth_normals(depth,cam)
        robust,mask=fitted_depth_normals(depth,cam,plane_residual=True)
        expected=np.array([0.,-.003*cam['fy'],.125])@cam['basis'].T
        expected=-expected/np.linalg.norm(expected)
        # At the center, a one-pixel depth change exceeds the old 2% rule.
        self.assertFalse(bool(old_mask[20,31]))
        self.assertGreater(float(mask[2:-2,2:-2].mean()),.99)
        np.testing.assert_allclose(robust[mask],np.broadcast_to(expected,robust[mask].shape),atol=1e-10)
        # The plane-residual rule must still reject a separate depth layer.
        flat=np.full((41,63),8.); flat[:,32:]=20.
        normals,_=fitted_depth_normals(flat,cam,plane_residual=True)
        target=-cam['basis'][:,2]
        self.assertLess(float(np.rad2deg(np.arccos((normals[3:-3,29:35]@target).clip(-1,1))).max()),.01)


if __name__=='__main__': unittest.main()
