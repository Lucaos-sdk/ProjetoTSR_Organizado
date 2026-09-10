import copy
import sys
import unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from diagnose_neural_generalization import gates


class GeneralizationGateTests(unittest.TestCase):
    def values(self):
        return dict(image_mae=.1,temporal=.01,protected_max_change=0.,shadow_mae=.1,
                    edge_mae=.1,gradient_mae=.1,affected_scenes=4,
                    response=dict(changed_pixels=200,response_mae=.02,zero_response_mae=.04,unchanged_drift=.001))

    def test_insufficient_response_is_inconclusive_not_passed(self):
        control=self.values(); candidate=copy.deepcopy(control); candidate['temporal']=.005
        self.assertEqual(gates(candidate,control)['status'],'passed')
        candidate['response']['changed_pixels']=99
        self.assertEqual(gates(candidate,control)['status'],'inconclusive')
        self.assertIsNone(gates(candidate,control)['checks']['response_zero'])

    def test_known_failure_still_fails_with_missing_response(self):
        control=self.values(); candidate=copy.deepcopy(control)
        candidate['response'].update(changed_pixels=0,response_mae=None,zero_response_mae=None)
        candidate['affected_scenes']=0; candidate['image_mae']=.12
        self.assertEqual(gates(candidate,control)['status'],'failed')


if __name__=='__main__': unittest.main()
