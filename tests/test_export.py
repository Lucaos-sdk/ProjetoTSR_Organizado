import sys
import tempfile
import unittest
from pathlib import Path
import onnx
import torch
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / '_IA_Python'))
from export_onnx import export_model
from contract import validate_model


class ExportTests(unittest.TestCase):
    def test_no_implicit_random_export(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / 'model.onnx'
            with self.assertRaises(ValueError):
                export_model(path)
            self.assertFalse(path.exists())

    def test_self_contained_fp16_smoke_export(self):
        torch.set_num_threads(2)
        with tempfile.TemporaryDirectory() as temp:
            path = export_model(Path(temp) / 'model.onnx', allow_untrained=True)
            graph = validate_model(path, require_checkpoint=False)
            self.assertFalse(any(t.data_location == onnx.TensorProto.EXTERNAL for t in graph.graph.initializer))
            self.assertTrue(all(t.data_type == onnx.TensorProto.FLOAT16 for t in graph.graph.initializer))
            with self.assertRaises(ValueError):
                validate_model(path)


if __name__ == '__main__':
    unittest.main()
