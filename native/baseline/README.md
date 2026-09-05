# Baseline espacial DX12

Primeiro estágio isolado do plano de reconstrução. Executa um shader bilinear em buffers Float4 e compara cada componente da saída com uma referência CPU em dupla precisão. Não usa ONNX, OptiScaler, pesos neurais ou arquivos do jogo.

## Compilar no Windows

Requisitos: Visual Studio 2022 com desenvolvimento Desktop C++, Windows SDK e CMake. No PowerShell, na raiz do repositório:

```powershell
cmake -S native/baseline -B build/native -A x64
cmake --build build/native --config Release
ctest --test-dir build/native -C Release --output-on-failure
.\build\native\Release\tsr_dx12_baseline.exe --full
```

O CTest executa WARP (DX12 por software). O último comando seleciona o primeiro adaptador de hardware compatível e imprime seu nome; confirme que mostra a RX 7600. Não há fallback silencioso para WARP.

Para validar barreiras e recursos, instale o recurso opcional **Graphics Tools** do Windows e execute:

```powershell
.\build\native\Release\tsr_dx12_baseline.exe --debug --full
```

`--debug` exige a debug layer; falha claramente quando ela não está instalada. Avisos/erros de validação fazem o teste falhar. Para executar sem GPU dedicada: `--warp`. Não compare os tempos WARP com os da RX 7600.

## O que verifica

- Shader HLSL compilado com `D3DCompile`, cs_5_1, warnings tratados como erros.
- Buffers upload/default/readback; cópia de entrada; transição para leitura; dispatch; transição da saída para cópia; readback somente depois da fence.
- Coordenadas por centro de pixel, clamp nas bordas, HDR e negativos, alpha e dimensões não múltiplas do tamanho do grupo.
- Casos 1x1, 2x2, identidade, dimensões ímpares e largura unitária. `--full` acrescenta 1920x1080 → 3840x2160.
- Comparação por componente, tolerância absoluta 0,0005 + relativa 0,00005; NaN/Inf causam falha.
- Timestamps GPU em volta de um único dispatch. São diagnósticos, sem warm-up/estatística, e excluem transferências, alocação, compilação e jogo. Não demonstram o budget do TSR.

O uso de `GetGPUVirtualAddress` aqui é para root descriptors D3D12 de buffers estruturados, onde é válido. Não representa um ponteiro de alocação do ONNX Runtime/DirectML.

## Limites e próximo estágio

Este baseline usa buffers estruturados sintéticos, não texturas reais do jogo. Ainda faltam entrada por textura, motion/depth/jitter/exposure/reset, reprojeção/disoclusão e histórico por instância. A referência espacial não recebe esses dados porque não os usa. Treinamento e integração neural continuam pendentes.

A CI compila no Linux (somente referência CPU) e Windows (CPU + WARP). Uma passagem WARP não certifica a GPU AMD ou a debug layer. Não substitua a DLL do jogo por este executável.

## Referências

- [Microsoft: barreiras de recursos](https://learn.microsoft.com/en-us/windows/win32/direct3d12/using-resource-barriers-to-synchronize-resource-states-in-direct3d-12)
- [Microsoft: execução e sincronização](https://learn.microsoft.com/en-us/windows/win32/direct3d12/executing-and-synchronizing-command-lists)
- [Microsoft: readback](https://learn.microsoft.com/en-us/windows/win32/direct3d12/readback-data-using-heaps)
