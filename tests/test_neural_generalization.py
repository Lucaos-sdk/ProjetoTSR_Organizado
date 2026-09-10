import hashlib
import sys
import unittest
from pathlib import Path
import numpy as np
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'_IA_Python'))
from neural_scene_geometry import box_hit, box_normal
from neural_lighting_scene import render
from neural_generalization_scene import SCENE_IDS, make_frame
from neural_lighting_diagnostics import receiver_mask


class GeneralizationTests(unittest.TestCase):
    def test_box_intersections_parallel_inside_and_miss(self):
        center=np.zeros(3); half=np.ones(3)
        origins=np.array([[0.,0.,-3.],[0.,0.,0.],[2.,0.,-3.],[1.,0.,-3.],[0.,0.,-3.]])
        directions=np.array([[0.,0.,1.]]*4+[[0.,0.,-1.]])
        got=box_hit(origins,directions,center,half)
        np.testing.assert_array_equal(got, [2.,1.,np.inf,2.,np.inf])
        np.testing.assert_array_equal(box_normal(np.array([[0.,0.,-1.],[1.,.2,.3]]),center,half),[[0,0,-1],[1,0,0]])
        self.assertTrue(np.isinf(box_hit(np.zeros((1,3)),np.zeros((1,3)),center,half)[0]))

    def test_historical_render_is_byte_compatible(self):
        cases=[(21000,0.,0.,'8a901b6f5133e5c5bbe397a94b1c6025eee1e368addd799de00077644c3fe73c'),
               (21001,.07,.002,'b3cb3f866993fdbec71467c7a68e08db8e340bc3f766202e574ecdbc0436e61d'),
               (21002,-.04,0.,'668fa030f7d4fe7af6e4cc8ab58dda1c4fd2ab93ed1f20905f62c192df15ea03')]
        for seed,yaw,noise,expected in cases:
            scene,_=render(seed,yaw=yaw,height=32,width=48,depth_noise=noise)
            self.assertEqual(hashlib.sha256(b''.join(v.tobytes() for v in scene.values())).hexdigest(),expected)

    def test_changed_source_light_does_not_change_target(self):
        a,_=make_frame(SCENE_IDS[0],'baseline'); b,_=make_frame(SCENE_IDS[0],'source_light')
        np.testing.assert_array_equal(a['target'],b['target'])
        np.testing.assert_array_equal(a['depth'],b['depth'])
        self.assertGreater(float(np.abs(a['source']-b['source']).mean()),.001)

    def test_boxes_cast_shadows_and_remove_preserves_receivers(self):
        scene,_=make_frame(21001,'boxes'); empty,_=make_frame(21001,'boxes',remove=True)
        common=receiver_mask(scene,empty)
        changed=np.abs(scene['target']-empty['target']).mean(-1)
        self.assertGreater(int((common & (changed>.005)).sum()),0)
        self.assertGreater(float(scene['shadow_effect'].max()),.005)
        self.assertTrue(all(np.isfinite(v).all() for v in scene.values()))
        np.testing.assert_array_equal(scene['target'][scene['valid']==0],scene['source'][scene['valid']==0])
        with self.assertRaises(ValueError):
            render(0,boxes_override=[([0,0,0],[1,0,1],[1,1,1])])


if __name__=='__main__': unittest.main()
