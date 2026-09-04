import time
import numpy as np
import onnxruntime as ort

def run_io_binding_benchmark():
    onnx_filename = "tsr_ultralight_540p_fp16.onnx"
    
    # Inicializa a sessão com DirectML
    providers = ['DmlExecutionProvider', 'CPUExecutionProvider']
    session = ort.InferenceSession(onnx_filename, providers=providers)

    print(f"Executando benchmark VRAM pura via: {session.get_providers()[0]}")

    # Cria matrizes iniciais NumPy em FP16
    packed_np = np.random.randn(1, 16, 540, 960).astype(np.float16)
    history_np = np.random.randn(1, 8, 540, 960).astype(np.float16)

    # Inicializa o objeto I/O Binding do ONNX Runtime
    io_binding = session.io_binding()

    # Move os tensores de entrada para a VRAM (dispositivo DirectML)
    packed_input_ort = ort.OrtValue.ortvalue_from_numpy(packed_np, 'dml', 0)
    warped_history_ort = ort.OrtValue.ortvalue_from_numpy(history_np, 'dml', 0)

    io_binding.bind_ortvalue_input('packed_input', packed_input_ort)
    io_binding.bind_ortvalue_input('warped_history', warped_history_ort)

    # Instrui o DirectML a alocar os tensores de saída diretamente na VRAM
    io_binding.bind_output('confidence_mask', 'dml')
    io_binding.bind_output('new_history', 'dml')
    io_binding.bind_output('reconstruction_coeffs', 'dml')

    # Warm-up (aquecimento da GPU)
    print("Aquecendo a GPU com tensores fixados na VRAM...")
    for _ in range(30):
        session.run_with_iobinding(io_binding)

    # Medição de tempo sem o gargalo do barramento PCIe
    num_iterations = 200
    times = []

    print(f"Iniciando {num_iterations} iterações (Sem cópia CPU <-> GPU)...")
    for _ in range(num_iterations):
        start_time = time.perf_counter()
        session.run_with_iobinding(io_binding)
        end_time = time.perf_counter()
        
        elapsed_ms = (end_time - start_time) * 1000.0
        times.append(elapsed_ms)

    avg_time = np.mean(times)
    min_time = np.min(times)
    max_time = np.max(times)

    print("\n--- Resultados do Benchmark VRAM (I/O Binding / DirectML) ---")
    print(f"Tempo médio de inferência: {avg_time:.3f} ms")
    print(f"Tempo mínimo:              {min_time:.3f} ms")
    print(f"Tempo máximo:              {max_time:.3f} ms")
    print("-------------------------------------------------------------")

if __name__ == "__main__":
    run_io_binding_benchmark()