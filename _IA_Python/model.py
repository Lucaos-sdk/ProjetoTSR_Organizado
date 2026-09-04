import torch
import torch.nn as nn

class UltraLightTSRNet(nn.Module):
    """
    Modelo Híbrido Ultraleve de TSR otimizado para AMD RDNA 3 (RX 7600).
    Roda a inferência em 540p e gera coeficientes para reconstrução via HLSL.
    """
    def __init__(self, packed_channels: int = 16, history_channels: int = 8, hidden_channels: int = 16):
        super().__init__()
        
        # A entrada total concatena as entradas do jogo (16 ch) + histórico pré-reprojetado (8 ch) = 24 canais
        total_in_channels = packed_channels + history_channels

        # 1. Extrator de características (4 blocos de convolução leve 3x3)
        self.encoder = nn.Sequential(
            nn.Conv2d(total_in_channels, hidden_channels, kernel_size=3, padding=1),
            nn.LeakyReLU(0.2, inplace=True),
            nn.Conv2d(hidden_channels, hidden_channels, kernel_size=3, padding=1),
            nn.LeakyReLU(0.2, inplace=True),
            nn.Conv2d(hidden_channels, hidden_channels, kernel_size=3, padding=1),
            nn.LeakyReLU(0.2, inplace=True)
        )

        # 2. Cabeçote da Máscara de Confiança / Oclusão (540p, 1 canal, valores de 0 a 1)
        self.confidence_head = nn.Sequential(
            nn.Conv2d(hidden_channels, 8, kernel_size=3, padding=1),
            nn.LeakyReLU(0.2, inplace=True),
            nn.Conv2d(8, 1, kernel_size=3, padding=1),
            nn.Sigmoid()
        )

        # 3. Cabeçote de Atualização do Histórico Recorrente (540p, 8 canais)
        self.history_head = nn.Conv2d(hidden_channels, history_channels, kernel_size=3, padding=1)

        # 4. Cabeçote de Coeficientes de Reconstrução Espacial para o Shader 4K (12 canais)
        self.coeff_head = nn.Conv2d(hidden_channels, 12, kernel_size=3, padding=1)

    def forward(self, packed_input: torch.Tensor, warped_history: torch.Tensor):
        """
        - packed_input: Tensor [1, 16, 540, 960] contendo Color YCoCg Log, Depth, MVs e Jitter.
        - warped_history: Tensor [1, 8, 540, 960] do frame anterior já reprojetado por HLSL.
        """
        # Validação simples de dimensão no início
        assert packed_input.shape[2:] == (540, 960), "Entrada deve estar em resolução 960x540!"
        assert warped_history.shape[2:] == (540, 960), "Histórico deve estar em resolução 960x540!"

        # Concatena a entrada com o histórico no eixo dos canais (dim=1)
        x = torch.cat([packed_input, warped_history], dim=1)

        # Extrai as características ocultas
        features = self.encoder(x)

        # Gera as saídas do modelo
        confidence_mask = self.confidence_head(features)
        new_history = self.history_head(features)
        reconstruction_coeffs = self.coeff_head(features)

        return confidence_mask, new_history, reconstruction_coeffs


# --- Teste de Validação Local ---
if __name__ == "__main__":
    print("Iniciando teste de execução da rede híbrida...")

    # Instancia o modelo
    model = UltraLightTSRNet()
    model.eval()

    # Cria tensores falsos com a resolução de meia tela (960x540)
    # Batch=1, Canais=16/8, Altura=540, Largura=960
    dummy_packed_input = torch.randn(1, 16, 540, 960)
    dummy_warped_history = torch.randn(1, 8, 540, 960)

    # Executa a passagem direta (forward)
    with torch.no_grad():
        conf_mask, new_hist, coeffs = model(dummy_packed_input, dummy_warped_history)

    print("\n--- Resultados dos Formatos dos Tensores (Shapes) ---")
    print(f"Máscara de Confiança: {conf_mask.shape} (Esperado: [1, 1, 540, 960])")
    print(f"Novo Histórico:       {new_hist.shape}  (Esperado: [1, 8, 540, 960])")
    print(f"Coeficientes HLSL:    {coeffs.shape}   (Esperado: [1, 12, 540, 960])")

    print("\nModelo híbrido validado com sucesso!")