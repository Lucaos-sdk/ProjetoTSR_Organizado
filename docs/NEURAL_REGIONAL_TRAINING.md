# Treinamento pela cadeia regional em várias escalas

Rodada de 10/09/2026, após o [diagnóstico de escala](NEURAL_GENERALIZATION.md).
Objetivo: treinar a CNN através do mesmo caminho de redução de entradas e
aplicação do residual que será avaliado. Antes, os pesos foram treinados em
96×64 e somente adaptados para esse caminho na inferência.

## Protocolo registrado antes do treino

Cada um dos dois checkpoints t025 é preservado como referência congelada e
copiado para dois treinos de continuação com orçamento igual. Quatro modelos
treinados, mais as duas referências congeladas e a entrada sem correção na
avaliação. Não se escolhe a melhor inicialização.

- **Controle:** loss de imagem e resposta às intervenções, reforço de
  conservação das regiões estáveis e erro em bordas geométricas.
- **Conjunto:** mesmos termos, mais 0,5 × erro temporal reprojetado.

Ambos usam a reconstrução regional existente: altura interna 64 com aspecto
preservado, redução da cor por área, amostragem da geometria pelo centro e
ampliação bilinear do residual sobre a cor original. Não há filtro guiado,
novas camadas ou histórico na inferência nesta rodada.

As losses de imagem e resposta são as anteriores. Acrescentamos 0,1 × MAE nas
bordas e 0,2 × diferença de erro nos receptores estáveis. A loss de resposta
já tinha peso 0,1 nas regiões estáveis, totalizando 0,3 nesse componente. Todas
as máscaras de supervisão e referências são usadas somente no treinamento
e na avaliação; não são entradas privilegiadas da rede em execução.

A referência correta minimiza todos os termos. A coerência temporal compara
erros contra a referência própria de cada frame, em pontos reprojetados da
mesma geometria. Não se reprojectam IDs entre objetos removidos e sobreviventes.
Os termos de imagem, borda e resposta são calculados após reconstruir o residual
na resolução do frame, não somente sobre a pequena imagem analisada pela rede.

## Dados e comparação

32 cenas novas (23000–23031), esferas/chão e mistura com blocos. Três tamanhos
de treino: 96×64, 144×96, 192×128. Cada cena/família/escala tem um quarteto:
duas câmeras com geometria original e as mesmas câmeras após remover todos os
objetos. 192 quartetos, com reutilização de conteúdo entre famílias e estados.
As câmeras são −0,10/−0,05 em seeds pares e +0,05/+0,10 em seeds ímpares.
Ruído relativo de profundidade de 0,05%, mantendo alvo e cor original limpos.

1.200 passos Adam, taxa 0,0005, seed de amostragem 984. Todos os quatro treinos
recebem os mesmos quartetos na mesma ordem, partindo do mesmo checkpoint dentro
de cada inicialização. Usa-se o passo final fixado: sem seleção de checkpoint,
busca de coeficientes ou ajuste após ver validação/teste.

Validação: seis cenas novas, 24000–24005. Teste: oito cenas novas, 25000–25007.
Nas duas famílias, avaliam-se 144×96, 192×128 e 384×256, profundidade limpa e
ruído de 0,2%. O maior tamanho não foi usado no treino. Câmeras −0,04/0/+0,04,
geometria original e removida. Métricas estáticas usam a pose central; métricas
temporais usam pares consecutivos dentro do mesmo estado da geometria.

## Critérios

Cada modelo conjunto é comparado **ao congelado e ao controle treinado** da
mesma inicialização, em cada família, resolução e condição de ruído:

- Erro temporal pelo menos 20% menor.
- Erros de imagem, resposta, sombras, bordas e gradientes no máximo 5% maiores.
- Resposta às intervenções pelo menos 20% melhor que resposta zero.
- Alteração indevida em regiões estáveis até 0,003 e proteção exata das áreas
  explicitamente excluídas pela validade.

Mantém-se o mínimo de 100 pixels alterados e duas cenas com resposta relevante;
amostras insuficientes não aprovam o critério causal. A aprovação completa exige
todos os critérios em todos os grupos da validação e teste, nas duas
inicializações e frente às duas referências. Mesmo aprovação sintética não
autoriza integração de jogo sem as outras etapas.

## Reprodução e limites

```powershell
.\_IA_Python\venv\Scripts\python.exe -m unittest discover -s tests -p test_neural_regional_training.py -v
.\_IA_Python\venv\Scripts\python.exe tools/train_neural_regional.py --out artifacts/regional-training-replay --steps 1200
```

Saída desta execução: `artifacts/neural-regional-training-v1`. Protocolo e
manifesto são gravados antes do treino; dados, ordem, pesos e resultados têm
hashes. Os resultados de cada grupo são salvos durante a avaliação. Diretórios
existentes não são sobrescritos.

Continua sendo iluminação direta difusa, com poucos tipos de geometria e luz
alvo fixa. Não inclui troca de materiais, iluminação indireta, movimento de
objetos, remoção isolada/deslocamento de um objeto, nem variação de luz de
entrada/distância nesta rodada. 384×256 ainda é inferior à entrada real do jogo.
O teste é CPU FP32; custo GPU, VRAM, FP16 e integração D3D12 continuam separados.

## Resultado da execução

Os quatro treinos completaram 1.200 passos. Tempos de parede de treinamento
em CPU: 57,55 / 61,61 segundos na inicialização A (controle/conjunto) e
57,99 / 58,50 na B. Esses tempos não são latência GPU nem comparação de custo
entre os caminhos de inferência, que continuam usando a mesma arquitetura.

No teste final, cada referência tem 24 comparações: duas famílias, três
resoluções, duas condições de ruído e duas inicializações. Cada grupo tem
16 imagens centrais, oito pares de remoção e 32 pares de câmera. As imagens
repetem superfícies e seeds; não são 24 conjuntos de jogos independentes.

### Frente aos pesos congelados, pelo mesmo caminho regional

| Métrica | Resultado do modelo conjunto nas 24 comparações |
| --- | --- |
| MAE de imagem | 15,54% a 34,42% menor |
| MAE de resposta às remoções | 13,07% a 23,70% menor |
| MAE na região de sombras | 4,77% a 26,50% menor |
| MAE em bordas geométricas | 2,68% a 17,25% menor |
| Erro temporal | De 8,74% menor até 28,15% maior |
| Alteração indevida nas regiões estáveis | 0,00264184 a 0,00308316 |

A redução média das razões de erro de imagem foi 25,84%, calculada com peso
igual para as 24 comparações. O erro temporal médio nessas razões aumentou
7,86%. Esses intervalos não são intervalos de confiança nem ganho de FPS.

A imagem e a resposta às mudanças de geometria melhoraram consistentemente.
Porém, **nenhuma comparação atingiu 20% de melhora temporal frente ao congelado**;
parte delas de fato piorou. Em seis comparações, regiões estáveis ultrapassaram
0,003. Não apresentar o ganho estático como resolução do problema temporal.

### Frente ao controle com o mesmo orçamento de treino

Adicionar a supervisão temporal reduziu o erro temporal em **20,25% a 26,77%**
nas 24 comparações do teste. Isso confirma um efeito favorável desse termo
dentro do novo treinamento, mas não uma vantagem temporal sobre o modelo antigo.

O custo foi aumento do MAE de imagem entre 2,99% e 7,12%; oito comparações
ultrapassaram o limite de 5%. A resposta ficou entre 1,16% pior e 1,92% melhor,
sem ultrapassar a tolerância; sombras pioraram até 3,09%, ainda dentro do limite
no teste. Houve também seis falhas no limite absoluto das regiões estáveis.

Na validação separada, a comparação com congelados falhou apenas no critério
temporal (24 de 24). Frente ao controle treinado, oito comparações falharam
em tempo/coerência e 14 em sombras. Isso não motivou seleção de novos pesos:
ambas as avaliações usam o passo final previamente fixado.

**Resultado completo reprovado:** `validation_passed=false`,
`held_out_passed=false`, `synthetic_gates_passed=false` e
`game_integration_allowed=false`. Preservamos todos os checkpoints, incluindo
os controles, como evidência das trocas entre qualidade estática e movimento.

Exemplo numérico, blocos em 384×256 com profundidade limpa:

| Modelo | MAE de imagem | Erro temporal | Alteração indevida |
| --- | ---: | ---: | ---: |
| Congelado A | 0,03340623 | 0,00543980 | 0,00460675 |
| Controle treinado A | 0,02393211 | 0,00655219 | 0,00306898 |
| Conjunto A | 0,02465823 | 0,00496425 | 0,00302340 |
| Congelado B | 0,03168312 | 0,00421947 | 0,00320633 |
| Controle treinado B | 0,02586808 | 0,00654003 | 0,00313705 |
| Conjunto B | 0,02671561 | 0,00501680 | 0,00300532 |

O comparativo da primeira cena reservada, com esferas e depois blocos, foi
inspecionado. Os modelos treinados aproximam melhor a distribuição de cor/luz
do alvo, mas persistem diferenças e sombras suavizadas. A imagem de comparação
mostra congelados e conjuntos, não os controles treinados; estes têm métricas
e pesos próprios salvos. Quadros estáticos não demonstram estabilidade.

## Diagnóstico posterior: onde fica o erro temporal?

Antes de escolher um filtro de reconstrução, localizamos os erros em
384×256, profundidade limpa, nas mesmas oito cenas finais e duas famílias.
Esse diagnóstico posterior **não é nova validação independente**, nem seleção
de checkpoint. Compara apenas congelados e conjuntos e não altera os pesos.

`tools/diagnose_regional_temporal_regions.py` divide as correspondências
visíveis em regiões disjuntas: faixa de borda geométrica dilatada dois pixels
em qualquer dos dois quadros, chão fora dessa faixa, e interior de objetos.
Os contadores e somas são verificados contra o conjunto completo de pixels.
Saída: `artifacts/neural-regional-temporal-regions-v1`.

| Região | Fração de amostras RGB | Fração do erro temporal, conjunto A / B |
| --- | ---: | ---: |
| Faixa de bordas | 2,66% | 2,66% / 2,67% |
| Interior do chão | 96,16% | 95,67% / 95,45% |
| Interior de objetos | 1,17% | 1,66% / 1,88% |

O erro médio por amostra do chão ficou 0,00485131 (A) e 0,00487178 (B), enquanto
nas bordas ficou 0,00487599 e 0,00491571. Logo, o problema medido não está
desproporcionalmente concentrado nas bordas. O chão domina a imagem e o erro:
isso descreve esta fixture, não todos os jogos. Corrigir apenas silhuetas não
eliminaria o erro distribuído pelo interior neste conjunto.

## Verificação e próximo passo

34 testes neurais passaram. Os três novos verificam referência como ótimo das
losses, máscaras/correspondências corretas, gradientes através da reconstrução
regional e ruído limitado à entrada. Os renders históricos com hashes locais
pré-registrados continuaram compatíveis. Fontes e checkpoints são conferidos
em `verification.json`. Nenhuma DLL ou configuração do jogo foi alterada.

O treinamento pela cadeia real melhorou a tarefa de iluminação, mas não fechou
a estabilidade. A próxima comparação deve investigar **sensibilidade à fase
de amostragem e a movimentos pequenos da câmera** com pesos congelados, antes
de outra busca de coeficientes de loss. Separar o efeito da redução da cor,
amostragem da geometria, reconstrução de normais e ampliação do residual;
usar uma referência de reprojeção para distinguir amostragem de erro neural.

Uma hipótese concreta é tornar a entrada geométrica reduzida mais consistente
entre quadros, tratando descontinuidades sem alternar abruptamente amostras.
A localização do erro não prova que essa hipótese é a causa: ela deve ser
comparada com o caminho atual em outras cenas e deslocamentos controlados.
Reconstrução guiada por geometria continua relevante para silhuetas, mas não
é sustentada como solução única pelo diagnóstico do interior do chão.

Depois de estabilizar essa cadeia, retomar resoluções maiores, variações de
luz/distância e movimento de objetos. Exportação FP16, benchmark GPU isolado
e integração em jogo permanecem posteriores à validação visual.
