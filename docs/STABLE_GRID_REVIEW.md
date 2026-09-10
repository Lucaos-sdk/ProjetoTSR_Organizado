# Saída estável e avaliação visual sintética

## Alteração

O shader bilinear agora recebe jitter em pixels de entrada. Para cada centro da saída calcula `(outputIndex + 0,5) * renderSize / outputSize - 0,5 - currentJitter`. A subtração ocorre na parte fracionária das coordenadas, preservando a correção de precisão da RX 7600. Todos os chamadores espaciais inicializam jitter zero; o pipeline temporal envia o jitter atual.

O histórico permanece na resolução/grade jitterizada de entrada, com reprojeção entre grades. Somente a saída é amostrada numa grade sem jitter. Isso não transforma o histórico em uma acumulação de detalhes 4K: ainda são acumulação em baixa resolução e ampliação bilinear.

## Testes

- Quatro fases de jitter, incluindo ±0,5, avaliadas numa rampa analítica e escala não inteira 9×5 → 17×11. Valores interiores conhecidos verificam sinal e escala sem depender apenas da concordância CPU/GPU. Nas bordas mantém-se clamp.
- RX 7600, `--debug --full --visual-dir artifacts/stable-grid-visual`: 41 quadros da suíte e 16 quadros visuais passaram, sem warnings/erros da debug layer. Erro máximo GPU/CPU na saída: 7,86781e-06.
- CTest: 9/9 aprovados, incluindo CPU, WARP e regressões espaciais.
- Log: `artifacts/temporal-validation/rx7600-stable-grid.log`; CTest: `ctest-stable-grid.log` na mesma pasta.

## Sequência visual

Cena analítica 96×54 → 192×108, 16 quadros, quatro fases de jitter. Contém fundo suave, linha fina estática e retângulo móvel com motion/depth conhecidos. A referência é amostrada diretamente na grade de saída, sem passar pela entrada em baixa resolução. É uma referência de amostras pontuais; não usa integração de área/antialiasing como ground truth.

Comparações: bilinear CPU sem compensação; bilinear CPU com compensação; saída temporal real da GPU. Os BMPs vêm do readback e das referências identificadas; o script apenas monta as imagens. Métricas são calculadas em RGB linear antes de clipping. As prévias usam clipping em [0,1] e conversão sRGB; não são avaliação HDR.

| Método | MSE médio RGB linear | Média da variação do erro entre quadros |
|---|---:|---:|
| Bilinear sem compensação (CPU) | 0,00390047 | 0,00424398 |
| Bilinear compensado (CPU) | 0,00300411 | 0,00367396 |
| Temporal compensado (GPU) | 0,00260248 | 0,00124590 |

A variação é `mean((error_t - error_t-1)^2)` em coordenadas de tela, excluindo o primeiro quadro. Subtrair a referência de cada quadro considera o movimento conhecido da cena, mas essa métrica ainda não é uma medida perceptual geral de flicker nem uma métrica compensada por fluxo.

Nesta sequência, o temporal reduz MSE em aproximadamente 13,4% e a variação do erro em 66,1% contra o bilinear compensado. Não generalizar esses resultados para outros conteúdos, FSR/DLSS ou jogos.

## Inspeção visual

A folha de comparação mostra a linha fina ausente em algumas fases da entrada espacial, enquanto o temporal retém uma versão fraca e espalhada dela após acumulação. O fundo e as bordas continuam suavizados; a saída não recupera a nitidez da referência. Métricas menores não equivalem a detalhes resolvidos ou ausência de ghosting. O próximo trabalho de qualidade deve avaliar acumulação de detalhes e controle de histórico em sequências mais diversas.

Arquivos gerados em `artifacts/stable-grid-visual/`: `comparacao.png`, `sequencia.gif`, `metrics.csv`, `summary.json` e BMPs por quadro. A animação é lenta para inspeção (180 ms/quadro), não representa desempenho medido.

## Reproduzir

```powershell
.\build\native\Release\tsr_temporal_dx12.exe --debug --full --visual-dir artifacts/stable-grid-visual
python tools/render_temporal_review.py artifacts/stable-grid-visual
```

O renderizador requer Pillow. Nenhuma rede treinada ou transformação de iluminação/materiais foi adicionada nesta etapa. O pipeline não foi integrado ao jogo nem medido contra a meta de 3 ms.
