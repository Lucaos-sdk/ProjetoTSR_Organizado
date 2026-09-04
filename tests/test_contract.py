import sys
import unittest
from pathlib import Path
import numpy as np
import onnx
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / '_IA_Python'))
from contract import encode_color, decode_color, validate_model


class ColorTests(unittest.TestCase):
    def test_hdr_negative_and_black_roundtrip(self):
        rgb = np.array([[0., 0., 0.], [10000., .01, -2.], [-1., -4., -8.], [1., 0., 0.], [0., 1., 0.], [0., 0., 1.]])
        np.testing.assert_allclose(decode_color(encode_color(rgb)), rgb, rtol=1e-10, atol=1e-10)

    def test_legacy_model_is_not_deployable(self):
        path = Path(__file__).resolve().parents[1] / '_IA_Python/tsr_ultralight_540p.onnx'
        with self.assertRaises((ValueError, OSError, onnx.checker.ValidationError)):
            validate_model(path)


if __name__ == '__main__':
    unittest.main()
