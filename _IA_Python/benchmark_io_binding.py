"""Synchronized host-observed ONNX latency, not GPU timestamps or full TSR frame time."""
import argparse
import time
import numpy as np
from contract import DEFAULT_MODEL, INPUTS, OUTPUTS, HEIGHT, WIDTH, validate_model


def benchmark(path, iterations=200, warmup=30, allow_untrained=False):
    import onnxruntime as ort
    if iterations < 1 or warmup < 0:
        raise ValueError('iterations must be positive and warmup nonnegative')
    validate_model(path, require_checkpoint=not allow_untrained)
    if 'DmlExecutionProvider' not in ort.get_available_providers():
        raise RuntimeError('DirectML unavailable; CPU fallback cannot measure this GPU experiment')
    options = ort.SessionOptions()
    options.enable_mem_pattern = False
    options.execution_mode = ort.ExecutionMode.ORT_SEQUENTIAL
    options.add_session_config_entry('session.disable_cpu_ep_fallback', '1')
    session = ort.InferenceSession(str(path), sess_options=options, providers=['DmlExecutionProvider'])
    session.disable_fallback()
    binding = session.io_binding()
    rng = np.random.default_rng(0)
    # Keep these alive until every run and synchronization has finished.
    values = []
    try:
        for name, channels in INPUTS.items():
            data = rng.normal(0, .1, (1, channels, HEIGHT, WIDTH)).astype(np.float16)
            value = ort.OrtValue.ortvalue_from_numpy(data, 'dml', 0)
            values.append(value)
            binding.bind_ortvalue_input(name, value)
        for name in OUTPUTS:
            binding.bind_output(name, 'dml', 0)
    except Exception as exc:
        raise RuntimeError('This ONNX Runtime build does not support Python DML device binding; use a native D3D12 harness. No CPU substitute was measured.') from exc
    binding.synchronize_inputs()
    session.run_with_iobinding(binding)
    binding.synchronize_outputs()
    outputs = binding.get_outputs()
    for name, value in zip(OUTPUTS, outputs):
        binding.bind_ortvalue_output(name, value)
    samples = []
    for i in range(warmup + iterations):
        start = time.perf_counter_ns()
        session.run_with_iobinding(binding)
        binding.synchronize_outputs()
        elapsed = (time.perf_counter_ns() - start) / 1e6
        if i >= warmup:
            samples.append(elapsed)
    for output in binding.copy_outputs_to_cpu():
        if not np.isfinite(output).all():
            raise RuntimeError('Non-finite output invalidates the benchmark')
    print(f'ORT {ort.__version__}; model={path}; n={iterations}; synchronized host latency')
    print(f'mean={np.mean(samples):.3f} ms; median={np.median(samples):.3f} ms; p95={np.percentile(samples,95):.3f} ms')
    print('Excludes PrePack, reprojection, reconstruction and game integration. Does not establish image quality.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--model', type=str, default=str(DEFAULT_MODEL))
    parser.add_argument('--iterations', type=int, default=200)
    parser.add_argument('--warmup', type=int, default=30)
    parser.add_argument('--allow-untrained', action='store_true')
    args = parser.parse_args()
    benchmark(args.model, args.iterations, args.warmup, args.allow_untrained)
