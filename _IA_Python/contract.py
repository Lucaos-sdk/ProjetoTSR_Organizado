"""Versioned CPU/ONNX contract; contiguous NCHW, FP16 at the runtime boundary."""
from pathlib import Path
import numpy as np

VERSION = 'tsr-experimental-v1'
HEIGHT, WIDTH = 540, 960
INPUTS = {'packed_input': 16, 'warped_history': 8}
OUTPUTS = {'confidence_mask': 1, 'new_history': 8, 'reconstruction_coeffs': 12}
DEFAULT_MODEL = Path(__file__).resolve().parent / 'tsr_ultralight_540p_fp16.onnx'


def validate_model(path, require_checkpoint=True):
    import onnx
    model = onnx.load(str(path))  # Also detects missing external weight files.
    onnx.checker.check_model(model)
    for nodes, spec in ((model.graph.input, INPUTS), (model.graph.output, OUTPUTS)):
        if [n.name for n in nodes] != list(spec):
            raise ValueError('Tensor names/order differ from the runtime contract')
        for node in nodes:
            tensor = node.type.tensor_type
            shape = [d.dim_value for d in tensor.shape.dim]
            if tensor.elem_type != onnx.TensorProto.FLOAT16 or shape != [1, spec[node.name], HEIGHT, WIDTH]:
                raise ValueError(f'{node.name}: expected FP16 NCHW {spec[node.name]}x{HEIGHT}x{WIDTH}, got {shape}, type {tensor.elem_type}')
    metadata = {p.key: p.value for p in model.metadata_props}
    if metadata.get('tsr.contract') != VERSION:
        raise ValueError('Missing or incompatible TSR contract metadata')
    if require_checkpoint and metadata.get('tsr.weights') != 'checkpoint':
        raise ValueError('Untrained model: allowed only for explicitly requested smoke tests')
    return model


def encode_color(rgb):
    r, g, b = np.moveaxis(np.asarray(rgb), -1, 0)
    value = np.stack((.25*r+.5*g+.25*b, .5*r-.5*b, -.25*r+.5*g-.25*b), axis=-1)
    return np.sign(value) * np.log1p(np.abs(value))


def decode_color(encoded):
    value = np.sign(encoded) * np.expm1(np.abs(encoded))
    y, co, cg = np.moveaxis(value, -1, 0)
    return np.stack((y+co-cg, y+cg, y-co-cg), axis=-1)
