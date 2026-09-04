import torch
import onnx
import onnxruntime as ort
from model import UltraLightTSRNet

def export_to_onnx():
    print("Iniciando a exportação para o formato ONNX...")

    # Instancia o modelo e coloca em modo de avaliação
    model = UltraLightTSRNet()
    model.eval()

    # Define tensores de entrada com dimensões estáticas (fixas)
    # Importante: O DirectML performa melhor com shapes fixos no RDNA 3
    dummy_packed_input = torch.randn(1, 16, 540, 960, dtype=torch.float32)
    dummy_warped_history = torch.randn(1, 8, 540, 960, dtype=torch.float32)

    onnx_filename = "tsr_ultralight_540p.onnx"

    # Exportação ONNX
    torch.onnx.export(
        model,
        (dummy_packed_input, dummy_warped_history),
        onnx_filename,
        export_params=True,
        opset_version=17,             # Opset 17 é altamente estável no DirectML
        do_constant_folding=True,      # Otimiza constantes no grafo
        input_names=['packed_input', 'warped_history'],
        output_names=['confidence_mask', 'new_history', 'reconstruction_coeffs']
    )

    print(f"Modelo exportado com sucesso: {onnx_filename}")

    # Validação do arquivo ONNX gerado
    print("Validando a estrutura do grafo ONNX...")
    onnx_model = onnx.load(onnx_filename)
    onnx.checker.check_model(onnx_model)
    print("Grafo ONNX validado sem erros!")

    # Teste rápido de carregamento no ONNX Runtime com DirectML
    print("Testando inicialização do modelo no DirectML Execution Provider...")
    providers = ['DmlExecutionProvider', 'CPUExecutionProvider']
    session = ort.InferenceSession(onnx_filename, providers=providers)

    print(f"Provedor ativo na inferência: {session.get_providers()[0]}")
    print("Exportação e teste no DirectML concluídos com êxito!")

if __name__ == "__main__":
    export_to_onnx()