# Histórico na resolução de saída

Atualização posterior: a restrição original de 2× foi removida. Agora há [resolução nativa, ampliação fracionária e escolha de GPU](RESOLUTIONS_AND_GPUS.md), com prioridade de uso em saída 1080p. Os resultados visuais de 2× abaixo permanecem como evidência da etapa original.

## Resultado e decisão

O modo opcional `--output-history` mantém cor e peso de amostras na grade estável de saída. Na sequência sintética atual, preservou a linha fina que o histórico em baixa resolução espalhava. O erro médio diminuiu 75,1% e a variação do erro entre quadros diminuiu 20,7% em relação ao temporal bilinear anterior.

**Permanece experimental e não altera o padrão.** A validação cobre poucas cenas sintéticas; ainda não há desempenho, qualidade ou consumo total de memória medidos no jogo.

## O que muda

- Duas texturas de cor e duas de geometria/peso persistem na resolução de saída. Em cada chamada, leitura e escrita alternam de lado.
- Suporta resolução nativa e ampliação por fatores independentes em ambos os eixos. Minificação e a combinação com `--cubic` são recusadas explicitamente.
- Cada centro da saída busca a amostra mais próxima na grade de entrada, descontando o jitter. O peso da amostra é o produto de dois filtros triangulares de raio 0,5 pixel de entrada. Amostras fora da entrada têm peso zero.
- A cor anterior é reprojetada na grade estável com `outputIndex + (outputSize / renderSize) * motion`, separando partes inteira/fracionária para preservar precisão. Não há delta de jitter no endereço do histórico, pois ele já está na grade de saída.
- Motion, depth e profundidade anterior prevista vêm da amostra de entrada mais próxima. Cada tap anterior de peso bilinear positivo deve ter peso acumulado positivo e profundidade compatível; caso contrário o histórico do pixel é rejeitado.
- Cor e massa anteriores são interpoladas com pesos, a massa decai por 0,9, e RGB anterior é ajustado pela razão de exposição. A nova amostra é combinada pela massa total. A massa armazenada é limitada a 4. Alpha continua vindo da reconstrução espacial do quadro atual.
- Sem histórico utilizável nem amostra com peso positivo, a saída usa bilinear do quadro atual, mas armazena massa zero. Assim, o preenchimento provisório não se transforma numa amostra de detalhe válida.
- Empates a meio pixel têm convenção explícita em CPU/HLSL dentro de 1e-6 pixel para estabilizar seleção de vizinhos. HDR e negativos não são limitados a [0,1].

O shader produz diretamente a saída final; não existe passe de ampliação após a acumulação desse modo. Ele ainda não usa rede neural ou confiança aprendida.

## Validação RX 7600

`--debug --output-history --full --visual-dir artifacts/output-history-review/output`:

- **37 quadros** aprovados, sem warnings/erros da debug layer, incluindo quatro quadros com histórico 3840×2160 e 16 quadros visuais.
- Referência CPU independente em dupla precisão; cor, massa/profundidade e saída comparadas por componente, com máscara de validade exata.
- Teste com valor esperado de uma linha fina: depois de quatro fases de jitter, pixel da linha ≈1 e vizinho ≈0. A verificação não depende somente de concordância GPU/CPU.
- Reset, exposição, desoclusão, duas instâncias intercaladas, redimensionamento, índice descontínuo, motion fora da tela, rejeição de escala sem avançar histórico e reprojeção de câmera sintética.
- Erro máximo GPU/CPU observado na suíte: 1,90735e-06.
- **CTest: 15/15** passou, incluindo referências CPU, WARP completo e regressões bilinear/cúbica.

Logs: `artifacts/temporal-validation/rx7600-output-history.log`, `rx7600-output-history-baseline.log` e `ctest-output-history.log`. Estes resultados pertencem ao código local; ainda não houve CI remota desta alteração.

## Comparação visual pareada

Mesma cena, fases, movimento e referência analítica de `STABLE_GRID_REVIEW.md`. O script confirma que as imagens de referência são idênticas antes de montar a comparação.

| Método GPU | MSE médio RGB linear | Média da variação do erro |
|---|---:|---:|
| Histórico de entrada + bilinear | 0,002602478 | 0,001245897 |
| Histórico de saída | 0,000649269 | 0,000988535 |

Na folha de quadros 0, 7 e 15, a linha fina emerge com definição próxima da referência após receber as fases necessárias. O primeiro quadro ainda não possui as amostras faltantes. Há diferenças restantes nas bordas do objeto móvel. A melhora de MSE não é uma porcentagem de melhora perceptual geral nem uma comparação com FSR/DLSS.

Os BMPs são resultados reais de readback. Prévias limitam os valores a [0,1] e convertem para sRGB; métricas são calculadas em RGB linear antes disso. A referência é pontual analítica, não uma imagem com integração de área. A animação é lenta para inspeção, sem relação com latência medida.

## Memória e limitações

A RX 7600 reportou **510 MiB** de alocação para os quatro recursos de histórico em 4K, via `GetResourceAllocationInfo`. Isso não inclui entrada, upload, readback, referências CPU, outras instâncias, recursos do jogo ou overhead total do processo. O payload matemático dos quatro planos RGBA32F é 506,25 MiB; o valor reportado inclui o layout/alinhamento dos recursos.

O harness é síncrono e usa readback a cada quadro. O resultado não demonstra que o modo caiba no orçamento de 3 ms. Não foram feitas otimizações FP16 ou compactação de depth/peso nesta etapa.

Ainda faltam validação ampla de rotação, objetos deformáveis, transparência/partículas, reatividade, mudanças rápidas de iluminação, falhas de motion e sequências longas. Selecionar geometria pela amostra mais próxima pode falhar em silhuetas finas; rejeitar qualquer tap sem massa pode descartar histórico útil. A massa e seu decaimento são parâmetros heurísticos, não uma garantia de confiança.

Próximo foco: ampliar a avaliação por região (detalhe, borda, desoclusão e mudança de iluminação) e medir custo GPU/memória com metodologia repetível antes de promover este modo. O objetivo de transformação neural de iluminação/materiais continua sem dataset e sem modelo treinado.

## Reproduzir

```powershell
.\build\native\Release\tsr_temporal_dx12.exe --debug --visual-dir artifacts/output-history-review/bilinear
.\build\native\Release\tsr_temporal_dx12.exe --debug --output-history --full --visual-dir artifacts/output-history-review/output
python tools/compare_reconstruction.py artifacts/output-history-review/bilinear artifacts/output-history-review/output artifacts/output-history-review/comparison --candidate-key output_history --candidate-title "Historico na saida (GPU)"
```

Comparação: `artifacts/output-history-review/comparison/comparacao.png`, `sequencia.gif`, `summary.json`.
