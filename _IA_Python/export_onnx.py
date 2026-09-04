"""Export a checkpoint, or explicitly labelled random weights for smoke testing."""
import argparse
import hashlib
from pathlib import Path
import onnx
import torch
from model import UltraLightTSRNet
from contract import DEFAULT_MODEL, VERSION, HEIGHT, WIDTH, INPUTS, OUTPUTS, validate_model


def export_model(output, checkpoint=None, allow_untrained=False):
    if checkpoint is None and not allow_untrained:
        raise ValueError('Provide --checkpoint or explicitly use --allow-untrained (no image-quality claim)')
    torch.manual_seed(0)
    model = UltraLightTSRNet().eval()
    if checkpoint:
        state = torch.load(checkpoint, map_location='cpu', weights_only=True)
        model.load_state_dict(state, strict=True)
    model.half()
    dummy = tuple(torch.zeros(1, c, HEIGHT, WIDTH, dtype=torch.float16) for c in INPUTS.values())
    output = Path(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    with torch.inference_mode():
        torch.onnx.export(model, dummy, str(output), dynamo=False, opset_version=17,
                          input_names=list(INPUTS), output_names=list(OUTPUTS),
                          do_constant_folding=True, external_data=False)
    graph = onnx.load(str(output))
    metadata = {'tsr.contract': VERSION, 'tsr.weights': 'checkpoint' if checkpoint else 'random-untrained'}
    if checkpoint:
        metadata['tsr.checkpoint_sha256'] = hashlib.sha256(Path(checkpoint).read_bytes()).hexdigest()
    onnx.helper.set_model_props(graph, metadata)
    onnx.save_model(graph, str(output), save_as_external_data=False)
    validate_model(output, require_checkpoint=not allow_untrained)
    return output


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--checkpoint', type=Path, help='Strict PyTorch state_dict; loading it does not prove quality')
    parser.add_argument('--allow-untrained', action='store_true')
    parser.add_argument('--output', type=Path, default=DEFAULT_MODEL)
    args = parser.parse_args()
    print(export_model(args.output, args.checkpoint, args.allow_untrained))
