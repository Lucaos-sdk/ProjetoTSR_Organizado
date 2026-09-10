# Experimento de reconstrução cúbica

## Decisão

**Bilinear permanece como padrão.** A opção `--cubic` testa Catmull-Rom com limitação de halos. A execução é numericamente correta, mas o experimento visual não demonstrou melhora global: na sequência atual, aumentou MSE em 0,71% e a variação do erro em 1,23% contra a saída temporal bilinear.

## Implementação e validação

- Reconstrução separável com suporte 4×4, usando a mesma grade sem jitter. O resultado é limitado ao mínimo/máximo dos quatro vizinhos centrais por componente. Não há clamp global em [0,1], preservando HDR/negativos.
- Referência CPU em dupla precisão usa um kernel por distância, enquanto HLSL usa pesos polinomiais. Testes com constante HDR, identidade, pesos conhecidos de um impulso, alpha e limites em degraus.
- Na primeira execução 4K, o pixel 42243 diferiu porque arredondamento de coordenada selecionou a célula vizinha para limitar halos. Foi definida uma convenção de arredondamento a centros inteiros quando a distância é menor que 1e-6 pixel, aplicada na CPU e GPU. A tolerância de comparação não foi aumentada. A sequência 4K permanece na regressão automática.
- RX 7600: suíte completa e 16 quadros visuais passaram com debug layer. O bilinear foi executado novamente na mesma sequência para comparação. Os dois modos compartilham o histórico temporal; o experimento muda apenas a reconstrução final.
- Não foi medido o custo GPU do filtro. Suporte 4×4 implica mais amostras que bilinear; não há alegação sobre o orçamento de 3 ms.

## Resultado visual

| Saída temporal | MSE médio RGB linear | Variação média do erro entre quadros |
|---|---:|---:|
| Bilinear | 0,002602478 | 0,001245897 |
| Cúbica limitada | 0,002620956 | 0,001261183 |

Métricas da mesma cena sintética 96×54 → 192×108, 16 quadros. A definição e os limites das métricas estão em `STABLE_GRID_REVIEW.md`. A referência permanece amostragem pontual analítica, não ground truth com integração de área.

Inspeção da comparação: algumas bordas ficam mais marcadas, mas a linha fina continua espalhada e muito menos definida que a referência. A limitação de halos não recupera informação ausente na entrada ou perdida durante a acumulação em baixa resolução. Portanto, nitidez aparente não é critério suficiente para promover este filtro ao padrão.

## Reproduzir

```powershell
.\build\native\Release\tsr_temporal_dx12.exe --debug --full --visual-dir artifacts/detail-review/bilinear
.\build\native\Release\tsr_temporal_dx12.exe --debug --cubic --full --visual-dir artifacts/detail-review/cubic
python tools/compare_reconstruction.py artifacts/detail-review/bilinear artifacts/detail-review/cubic artifacts/detail-review/comparison
```

Saídas locais: `artifacts/detail-review/comparison/comparacao.png`, `sequencia.gif` e `summary.json`. Logs em `artifacts/temporal-validation/rx7600-cubic.log` e `ctest-cubic.log`.

## Próximo foco

Investigar preservação das amostras de jitter no histórico/reconstrução em resolução de saída, em vez de depender apenas de um filtro final mais nítido. Ampliar cenas e medir regiões de detalhe, desoclusão e transparência antes de promover alterações. Isso continua sendo infraestrutura para a futura transformação neural de iluminação e materiais, que ainda não tem dataset ou modelo treinado.
