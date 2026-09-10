# Baseline espacial DX12

O executável temporal também oferece `--output-history`: [acumulação experimental na grade de saída](../../docs/OUTPUT_HISTORY_REVIEW.md), com resolução nativa e ampliação fracionária. É independente de `--cubic` e mantém o caminho anterior como padrão. O atalho `--1080p` configura 720p → 1080p; [outras resoluções e GPUs](../../docs/RESOLUTIONS_AND_GPUS.md) podem ser selecionadas explicitamente.

No executável temporal, `--cubic` habilita um [experimento de reconstrução Catmull-Rom limitada](../../docs/RECONSTRUCTION_REVIEW.md). Bilinear continua sendo o padrão; a opção não implica ganho de qualidade demonstrado.

O [baseline temporal de câmera fixa](../../docs/TEMPORAL_BASELINE.md) está disponível no executável separado `tsr_temporal_dx12 --debug --full`, com reprojeção, rejeição por profundidade e históricos alternados por instância. Os testes e comandos espaciais abaixo permanecem disponíveis.

Estágio isolado do plano de reconstrução. Executa um shader bilinear em buffers Float4 ou texturas RGBA32F (`--textures`) e compara cada componente da saída com uma referência CPU em dupla precisão. Não usa ONNX, OptiScaler, pesos neurais ou arquivos do jogo. O [contrato de quadros](../../docs/FRAME_CONTRACT.md) define cor, depth, motion, jitter, exposição e reset; somente cor é consumida pelo shader espacial.

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
.\build\native\Release\tsr_dx12_baseline.exe --textures --debug --full
```

`--debug` exige a debug layer; falha claramente quando ela não está instalada. Avisos/erros de validação fazem o teste falhar. Para executar sem GPU dedicada: `--warp`. Não compare os tempos WARP com os da RX 7600.

## O que verifica

- Shader HLSL compilado com `D3DCompile`, cs_5_1, warnings tratados como erros.
- Buffers upload/default/readback; cópia de entrada; transição para leitura; dispatch; transição da saída para cópia; readback somente depois da fence.
- Coordenadas por centro de pixel, clamp nas bordas, HDR e negativos, alpha e dimensões não múltiplas do tamanho do grupo.
- Casos 1x1, 2x2, identidade, dimensões ímpares e largura unitária, além de linhas/colunas longas para detectar erros de coordenadas em alta resolução. `--full` acrescenta 1920x1080 → 3840x2160; o CTest também executa esse caso em WARP.
- Comparação por componente, tolerância absoluta 0,0005 + relativa 0,00005; NaN/Inf causam falha.
- Timestamps GPU em volta de um único dispatch. São diagnósticos, sem warm-up/estatística, e excluem transferências, alocação, compilação e jogo. Não demonstram o budget do TSR.

O uso de `GetGPUVirtualAddress` aqui é para root descriptors D3D12 de buffers estruturados, onde é válido. Não representa um ponteiro de alocação do ONNX Runtime/DirectML.

## Limites e próximo estágio

A validação local na RX 7600 revelou perda de precisão ao dividir coordenadas grandes em ponto flutuante: no caso 1080p → 4K, o pixel 513 retornava 14,49798584 em vez de 14,5. O shader agora separa quociente e resto inteiros antes de calcular a fração de interpolação. A referência CPU e as tolerâncias permanecem iguais. Os buffers default começam em COMMON e recebem transições explícitas antes da cópia/dispatch.

Este baseline usa dados sintéticos em buffers ou texturas DX12 próprias. O caminho por textura usa SRV/UAV e respeita o RowPitch nas cópias; o CTest inclui ambos os caminhos com e sem 4K. O executável temporal consome motion/depth/jitter/exposure/reset; ainda faltam câmera móvel, reconstrução temporal numa grade estável, recursos reais do jogo, treinamento e integração neural. A referência espacial continua sem consumir dados temporais.

A CI compila no Linux (somente referência CPU) e Windows (CPU + WARP). Uma passagem WARP não certifica a GPU AMD ou a debug layer. Não substitua a DLL do jogo por este executável.

## Referências

- [Microsoft: barreiras de recursos](https://learn.microsoft.com/en-us/windows/win32/direct3d12/using-resource-barriers-to-synchronize-resource-states-in-direct3d-12)
- [Microsoft: execução e sincronização](https://learn.microsoft.com/en-us/windows/win32/direct3d12/executing-and-synchronizing-command-lists)
- [Microsoft: readback](https://learn.microsoft.com/en-us/windows/win32/direct3d12/readback-data-using-heaps)
