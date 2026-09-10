# Conversão e reconstrução encadeadas na GPU

O teste `tsr_input_chain_dx12` grava seis quadros na mesma command list e faz uma única submissão. Cada quadro executa `InputAdapterPass::Record`, transições dos resultados para leitura por shader e `TemporalPass::Record` com histórico na resolução de saída. O histórico produzido na GPU alimenta o quadro seguinte. Não há readback entre conversão e reconstrução nem entre os seis quadros.

Uploads das entradas sintéticas fazem parte da sequência. As leituras de cor e geometria são gravadas somente após o último quadro e verificadas após uma única espera da fence. A referência CPU é calculada separadamente para comparação e nunca fornece o resultado do quadro anterior à GPU.

## Cenários e critérios

| Quadro | Situação | Histórico esperado |
|---|---|---|
| 0 | Reset inicial, recursos anteriores preenchidos com valores deliberadamente incorretos | Rejeitado |
| 1 | Continuidade com profundidade compatível | Aceito no interior válido |
| 2 | Profundidade muda de 2 para 5 | Rejeitado |
| 3 | Profundidade anterior prevista indisponível | Rejeitado |
| 4 | Reset explícito | Rejeitado |
| 5 | Continuidade após reset | Aceito no interior válido |

A sequência alterna profundidade normal/invertida, usa quatro fases de jitter, escala de movimento por eixo e ampliação de fator não inteiro. Os resultados de cor, profundidade/massa e máscara de aceitação são comparados à referência CPU. Uma verificação adicional exige histórico aceito nos quadros 1/5 e nenhum histórico aceito nos demais; isso detecta um caminho que simplesmente ignorasse a etapa temporal.

## Validação

- RX 7600 com debug layer: 17×9 → 26×14 passou; 230 pixels aceitaram histórico em cada quadro de continuidade.
- RX 7600 com debug layer: 1280×720 → 1920×1080 passou; 2.068.682 pixels aceitaram histórico nos quadros 1 e 5.
- WARP com debug layer: cenário pequeno passou com a mesma máscara.
- Build Release concluído; 22/22 testes CTest passaram.
- Cor e geometria conferidas nos seis quadros, sem avisos/erros da camada de diagnóstico.

Execução: `tsr_input_chain_dx12.exe --debug --1080p`; para WARP: `tsr_input_chain_dx12.exe --warp --debug`.

Logs: `artifacts/temporal-validation/input-chain-rx7600.log`, `input-chain-rx7600-1080p.log` e `ctest-input-chain.log`.

## Limites e próximo avanço

É um teste de correção da composição dos passes, não uma medição de FPS ou memória de jogo. Retém recursos distintos de cada quadro para comparar todos os resultados no final, em vez de usar o anel de recursos que o backend real precisará. Não altera a implementação de recursos persistentes do harness temporal anterior.

Os formatos continuam R32F/RG32F/RGBA32F; a cadeia ainda usa o caso controlado de câmera fixa para obter a profundidade anterior prevista. Falta suportar os formatos e sub-retângulos reais recebidos pelo OptiScaler, converter a saída para o destino do jogo e implementar o ciclo de vida por contexto/quadro em voo. O teste não executa inferência neural nem cria uma DLL instalável no jogo.

O próximo avanço recomendado é cobrir FP16 e a escrita na textura final fornecida pelo chamador, antes de conectar um backend ao fork.

## Atualização de formatos e destino

A [etapa FP16 e saída externa](FP16_OUTPUT.md) acrescenta entradas RGBA16F/RG16F e gravação em textura final RGBA16F/RGBA32F fornecida pelo chamador, mantendo o histórico interno FP32. Sub-retângulos e integração ao jogo continuam pendentes.
