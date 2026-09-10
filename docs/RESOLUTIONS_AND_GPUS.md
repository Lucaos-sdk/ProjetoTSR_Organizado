# Resoluções e GPUs — foco de uso em 1080p

## Direção confirmada

A resolução final de uso do autor será **1920×1080**. A resolução interna deve ser configurável; 4K continua sendo cenário de teste e opção, não requisito de uso. O objetivo de transformação neural de iluminação/materiais permanece, inclusive para renderização nativa em 1080p.

## Opções implementadas no harness

```powershell
# Listar os adaptadores locais e suporte básico a DX12
.\build\native\Release\tsr_temporal_dx12.exe --list-adapters

# Atalho de teste: entrada 720p, saída 1080p, histórico na saída
.\build\native\Release\tsr_temporal_dx12.exe --debug --output-history --1080p

# Entrada e saída nativas em 1080p
.\build\native\Release\tsr_temporal_dx12.exe --debug --output-history --render 1920x1080 --output 1920x1080

# Entrada 540p, saída 1080p
.\build\native\Release\tsr_temporal_dx12.exe --debug --output-history --render 960x540 --output 1920x1080

# Escolher o índice de GPU obtido da listagem
.\build\native\Release\tsr_temporal_dx12.exe --adapter 0 --debug --output-history --render 1706x960 --output 2560x1440
```

`--render` e `--output` devem ser fornecidos juntos. `--1080p` é apenas um atalho de dimensões 1280×720 → 1920×1080; não representa um preset de qualidade treinado/certificado. Com dimensões explícitas, o executável roda quatro quadros sintéticos nessas dimensões. Sem dimensões explícitas, continua executando a suíte; `--full` expande essa suíte. `--visual-dir` continua exportando a cena visual padronizada 96×54 → 192×108 para comparações pareadas.

O modo de histórico na saída agora admite escala nativa e fatores de ampliação fracionários independentes por eixo. O antigo limite de 2× foi removido. Dimensões válidas ficam em [1,16384] por eixo, sujeitas à capacidade e à memória disponível; aceitar sintaticamente um tamanho não garante que sua alocação seja viável. A redução de resolução é recusada nesse modo porque não há filtro de minificação validado. Mudanças de resolução invalidam o histórico. Proporções diferentes são mapeadas diretamente, sem letterbox automático.

Essas opções controlam o executável de teste. Não alteram as configurações do jogo e não representam uma integração TSR pronta no The Witcher 3.

## Validação na RX 7600

Todos os casos abaixo passaram com debug layer, comparação CPU/GPU e histórico na saída:

| Entrada | Saída | Alocação dos quatro recursos de histórico |
|---|---|---:|
| 1280×720 | 1920×1080 | 127,5 MiB |
| 960×540 | 1920×1080 | 127,5 MiB |
| 1920×1080 | 1920×1080 | 127,5 MiB |
| 1706×960 | 2560×1440 | 230 MiB |
| 1720×720 | 3440×1440 | 310,5 MiB |
| 1920×1080 | 3840×2160 | 510 MiB |

Valores reportados por `GetResourceAllocationInfo`; não incluem entradas, readbacks, jogo ou consumo total. Logs `artifacts/temporal-validation/rx7600-1080p.log`, `rx7600-native1080.log`, `rx7600-540to1080.log`, `rx7600-1440p.log`, `rx7600-ultrawide.log` e `rx7600-resolution-regression.log`.

A escala fracionária revelou perda de precisão ao somar deslocamento subpixel a índices grandes em FP32. O shader agora separa a parte inteira e fracionária do deslocamento antes de acessar o histórico. A tolerância CPU/GPU foi preservada. A regressão larga 1706×2 → 2560×3 integra o CTest, junto de casos nativos/fracionários pequenos e 720p → 1080p com WARP.

## Compatibilidade de hardware

O harness usa APIs padrão de **DirectX 12**, shader model 5.1 e texturas RGBA32F. Não contém caminho exclusivo para AMD, CUDA, Tensor Cores ou WMMA. A inicialização exige dispositivo DX12 em feature level 11_0 ou superior e verifica suporte a Texture2D, leitura no shader e escrita UAV no formato usado. Falhas de dispositivo, formato ou alocação são reportadas.

- AMD Radeon RX 7600: validada localmente.
- WARP Microsoft: validado por testes automatizados; é software, não substitui certificação de outra GPU.
- Outras AMD, NVIDIA e Intel com os recursos exigidos: alvo de compatibilidade, **ainda não validadas neste projeto**.
- Placas sem DX12/recursos exigidos: não suportadas por este backend. Suporte a outros sistemas/APIs requer implementação adicional.

`--adapter N` seleciona exatamente o índice de hardware listado; índice inexistente ou incompatível falha sem trocar para outra GPU. Sem índice, seleciona o primeiro hardware DX12 compatível. `--warp` é explícito e não pode ser combinado com `--adapter`. A disponibilidade básica exibida na listagem não substitui a checagem completa de formato durante a execução.

## Limite da camada neural

Resoluções configuráveis estão implementadas **no harness e nos shaders temporais/espaciais**. O ONNX experimental existente continua com contrato estático de tensores em 960×540 e não está ligado a esse pipeline. Não afirmar que a rede já aceita toda resolução. A futura arquitetura de transformação de iluminação/materiais terá de definir resolução interna, redimensionamento/tiling ou shapes dinâmicos, custo e memória em conjunto com treinamento e runtime.
