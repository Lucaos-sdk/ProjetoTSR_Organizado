# Treino conjunto de iluminação, intervenções e câmera

Experimento de 09/09/2026, continuação do [treino com intervenções](NEURAL_INTERVENTION_TRAINING.md).
O objetivo é reduzir a instabilidade temporal sem perder a resposta aprendida
quando objetos são removidos ou deslocados. Não é uma alteração da V2.3 instalada.

## Comparação controlada

Duas CNNs próprias de 38.787 parâmetros partem de pesos novos idênticos. Ambas
recebem exatamente os mesmos quartetos, na mesma ordem, durante 1.600 passos
Adam com taxa 0,002. A única diferença de objetivo é a penalidade temporal:

- **Pares:** loss de imagem + loss de resposta às intervenções.
- **Conjunto:** mesmas losses + 1,0 × loss de coerência dos erros entre câmeras.

Cada quarteto tem quatro imagens: geometria original em duas poses e geometria
alterada nas mesmas duas poses. A loss de intervenção compara os dois estados
na mesma câmera, apenas em receptores fixos do chão. A loss temporal compara
duas câmeras da mesma geometria, nas duas direções. Nunca usa IDs de objetos
removidos/realocados como se fossem correspondências entre estados diferentes.

O termo temporal compara `(predição − referência)` de cada imagem após
reprojeção. Assim, não exige RGB idêntico quando a referência muda legitimamente.
Ainda existe erro de amostragem porque a correspondência usa o pixel mais próximo.
Esta rodada não acrescenta histórico à inferência: a rede continua recebendo
apenas o quadro atual, sem máscaras de supervisão ou referência de treinamento.

## Dados e critérios registrados antes do treino

64 cenas de treino (16000–16063), 12 de validação (17000–17011) e 12 de teste
(18000–18011), disjuntas das rodadas anteriores. Resolução 96×64. Um chão e uma
a quatro esferas difusas, sombras calculadas por raios, luzes fixas, névoa e
normais estimadas da profundidade observada pelo ajuste de plano já validado.

Treino: pares de yaw `(-0,10; -0,05)` e `(0,05; 0,10)`, três intervenções,
384 quartetos; ruído relativo de profundidade 0,05%. A mesma imagem base é
reutilizada entre intervenções; remover um/remover todos coincide quando há
apenas uma esfera. Isso não representa 1.536 imagens independentes.

Teste estático: yaw 0,07, 48 imagens e 36 pares de intervenção por condição.
Teste temporal: cinco poses de −0,08 a 0,08 em todos os quatro estados de cada
cena, 192 pares por condição, não apenas a geometria original. Profundidade
limpa e com ruído relativo de 0,2%. Não há animação de objetos nessas sequências.

Critérios para a rede conjunta, exigidos nas duas condições:

1. Erro de resposta pelo menos 20% menor que resposta zero.
2. Erro de resposta e MAE de imagem não mais que 5% piores que o controle com pares.
3. Erro temporal pelo menos 20% menor que o controle com pares.
4. Alteração indevida em regiões estáveis até 0,003 e proteção exata de UI/céu.

Semente de inicialização 20260910 (identificador, não data da execução), semente
de amostragem 732. Usamos o passo final fixado; não há busca de coeficientes,
seleção de checkpoint nem ajuste baseado no teste. A validação separada é
reportada para diagnóstico. Uma única inicialização não estabelece significância
estatística ou generalização para jogos.

## Reprodução

```powershell
.\_IA_Python\venv\Scripts\python.exe -m unittest discover -s tests -p test_neural_joint_training.py -v
.\_IA_Python\venv\Scripts\python.exe tools/train_neural_joint.py --out artifacts/neural-joint-replay --steps 1600
```

O runner recusa sobrescrever a pasta de saída. `protocol.json` e o manifesto
SHA256 das fontes são gravados antes do treino. A saída inclui pesos próprios,
curvas, hash dos dados e da sequência de amostragem, métricas individuais e
um comparativo da primeira cena reservada, sem escolher o exemplo mais favorável.
Não depende de checkpoints de terceiros nem executa binários externos.

Mesmo que passe os critérios sintéticos, a integração continua pendente de
dados representativos do jogo, precisão de execução, custo GPU, VRAM e contrato
de recursos. Tempo de treino em CPU não é latência de inferência na RX 7600.

## Resultado da rodada

Saída completa: `artifacts/neural-joint-training-v1`. As duas redes completaram
1.600 passos, com treinamento em CPU de aproximadamente 75,9 e 68,5 segundos.
São tempos de parede observados, não um benchmark de velocidade entre redes;
o orçamento controlado é o número de passos e os exemplos recebidos.

| Métrica no teste reservado (menor é melhor) | Controle com pares | Treino conjunto | Diferença |
| --- | ---: | ---: | ---: |
| Erro temporal, profundidade limpa | 0,01080579 | 0,00289304 | −73,23% |
| Erro temporal, profundidade ruidosa | 0,01142430 | 0,00319692 | −72,02% |
| MAE da resposta às intervenções, limpa | 0,02341775 | 0,02080632 | −11,15% |
| MAE da resposta às intervenções, ruidosa | 0,02346062 | 0,02080303 | −11,33% |
| MAE geral da imagem, limpa | 0,02499322 | 0,02649755 | +6,02% |
| MAE geral da imagem, ruidosa | 0,02504796 | 0,02646078 | +5,64% |

O erro de resposta da rede conjunta ficou 35,95% abaixo da resposta zero na
condição limpa. Os três tipos de intervenção melhoraram frente à resposta
zero. O erro temporal diminuiu sem apagar esse ganho de resposta; isso atende
ao objetivo principal que a rodada anterior não alcançou. A alteração indevida
média em regiões estáveis ficou em 0,00151215 (limpa) e 0,00153875 (ruidosa).
As regiões explicitamente protegidas foram conservadas exatamente.

**Cinco dos seis critérios passaram. O critério de imagem falhou nas duas
condições.** Não aumentamos o limite de 5% depois de obter os resultados.
`synthetic_gates_passed=false` e `game_integration_allowed=false`.

A resposta é agregada sobre 2.366 pixels alterados e 116.154 pixels estáveis em
36 pares por condição. O erro temporal agrega 192 pares por condição ponderados
pela quantidade de correspondências válidas. O resultado não é uma redução
medida de piscadas no jogo, nem uma comparação direta com pesos da rodada
anterior, que utilizou outras cenas e outro esquema de batches.

Na validação separada, a conclusão sobre resposta é menos favorável: MAE limpo
0,01718024 contra 0,01606630 do controle (aproximadamente 6,9% pior). O MAE de
imagem ficou 0,03130491 contra 0,02994482 (4,5% pior). Não selecionamos novos
pesos a partir disso. A diferença entre validação e teste reforça que ainda
precisamos de mais cenas e inicializações, não de uma alegação de ganho universal.

O comparativo da primeira cena de teste foi inspecionado. Essa cena tem apenas
uma esfera; por isso remover um e remover todos produz linhas iguais. Permanecem
diferenças de brilho e estrutura frente à referência, incluindo suavização.
Uma imagem estática não demonstra a métrica temporal; os valores acima vêm das
sequências reprojetadas. Nenhuma dessas imagens é captura do The Witcher 3.

Os 19 testes da família neural passaram, incluindo três novos sobre o contrato
dos quartetos, referência como ótimo dos três objetivos e propagação da loss
temporal para os dois estados da cena. A DLL do jogo permanece com SHA256
`9B3992A1FD4337A1C235DC3401D59F8BC81614E29585F3F20546B4C9BC6D47D0`.

## Próxima decisão

Preservar estes pesos e o teste 18000–18011 como evidência fechada. A supervisão
temporal conjunta merece continuar: melhorou bastante a coerência nesta tarefa.
A próxima rodada deve buscar recuperar a fidelidade estática, usando uma pequena
comparação pré-definida de pesos das losses na validação, com controle de mesmo
orçamento e mais de uma inicialização. Avaliar a escolha final em outros IDs,
sem ajustar parâmetros para passar no teste já visto. Relatar também bordas e
regiões de sombra para não esconder perda de detalhes numa média global.

Em seguida, ampliar a família de cenas e a variação de iluminação antes de
considerar resultados transferíveis ao jogo. Esta CNN ainda aprende um problema
restrito de iluminação difusa; não produz novos materiais, não substitui a
iluminação do motor e não tem custo na RX 7600 demonstrado. A integração GPU
continua depois dessa validação, sem promover um candidato reprovado.
