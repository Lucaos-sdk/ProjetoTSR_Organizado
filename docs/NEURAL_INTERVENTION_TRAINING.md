# Treinamento com intervenções de geometria

Rodada de 09/09/2026, após o diagnóstico de que o ganho em imagem estática não
se traduzia em resposta correta quando objetos eram removidos ou deslocados.
O objetivo é testar dados e supervisão mais adequados, mantendo a arquitetura.
Não altera a DLL V2.3, pesos AMD, configurações do jogo ou README geral.

## O que mudou

`_IA_Python/neural_intervention_training.py` gera cenas com uma a quatro esferas
em posições x/z aleatórias, raios e cores variados. Rejeita sobreposição entre
esferas; mantém objetos apoiados no chão. Para cada cena, remove um objeto,
desloca um objeto por pelo menos 0,9 unidade ou remove todos. Com uma esfera,
as duas operações de remoção são equivalentes e permanecem na distribuição de
treino/avaliação como casos separados.

As intervenções mantêm câmera, iluminação, névoa, chão e materiais restantes.
Alvos continuam sendo o segundo render sob outra iluminação, com sombras
calculadas por raios. Não são alvos de curvas de tom ou imagens geradas por
outra IA. Ainda são materiais difusos simples, um chão e esferas; não personagens,
interiores realistas, reflexos ou um modelo de materiais.

Normais de entrada agora usam o ajuste de profundidade inversa 5×5 com aceitação
por consistência de plano, do diagnóstico anterior. São reconstruídas da
profundidade observada, não normais exatas fornecidas pelo renderizador. A CNN
permanece com 38.787 parâmetros, mesma saída residual RGB e mesma proteção por
máscara sintética conhecida. Nenhuma nova dependência ou runtime foi instalado.

## Comparação controlada

Dois modelos partem de pesos novos idênticos e recebem os mesmos pares, na
mesma ordem, durante 1.600 passos Adam, taxa 0,002, seed 20260909 e seed de batches
436. Batch: dois pares, quatro frames. Sem seleção de checkpoint pela validação
ou teste; usamos o passo final fixado no protocolo antes do treino.

- **Controle:** dados diversificados e normais robustas, loss de imagem original.
- **Com pares:** mesmos dados e loss, mais supervisão explícita da mudança do
  residual em receptores fixos do chão e conservação onde a mudança esperada é zero.

A loss adicional é `0,5 * MAE de resposta em regiões alteradas + 0,1 * MAE em
regiões estáveis`. Máscaras e targets supervisionam o treinamento; não são
entradas da rede na inferência. O cálculo é equivalente a comparar mudanças
em `(predição − entrada)` com mudanças em `(referência − entrada)`.

Incluímos entrada sem correção e pesos espaciais antigos congelados como
referências secundárias. O modelo antigo recebe as novas normais e teve outro
orçamento/dataset de treino: somente controle versus pares isola a nova loss.
Não é comparação com a DLL V2.3.

## Dados e protocolo

64 cenas de treino (13000–13063), 12 de validação (14000–14011), 12 de teste
(15000–15011). IDs disjuntos das rodadas anteriores. Cada cena de treino tem duas
poses, três intervenções: 384 pares, 512 combinações de cena/pose/estado,
incluindo repetições de conteúdo quando ambas as remoções são equivalentes.
Resolução 96×64. Nenhuma animação de objeto ou vetor de movimento real é usado.

Treino em yaw ±0,1 com ruído relativo de profundidade de 0,05%. Avaliação estática
em yaw 0,07, com profundidade limpa e ruído de 0,2%. Avaliação temporal: 48 pares
de câmera em perspectiva por condição, mesma geometria, correspondência por
posição, profundidade e ID. Reprojeção nearest mantém resíduo subpixel.

Critérios previamente fixados para o modelo com pares, nas duas condições:

1. Erro da resposta pelo menos 20% menor que resposta zero.
2. Erro da resposta pelo menos 10% menor que o controle.
3. MAE de imagem e erro temporal não mais que 5% piores que o controle.
4. Alteração média indevida em regiões estáveis menor ou igual a 0,003.
5. Conservação exata das regiões protegidas.

Esses critérios não aprovam automaticamente integração: faltam domínio de jogo,
resolução de execução, entradas disponíveis, custo/VRAM na GPU e preservação
de materiais/identidade. O protocolo mantém `game_integration_allowed=false`.

## Resultado em cenas reservadas

36 pares de intervenção por condição, 2.639 pixels com alteração relevante e
118.008 pixels estáveis acumulados. São amostras repetidas de imagens, não uma
estimativa de cobertura de jogos. A resposta abaixo é a mudança do residual,
não a diferença de RGB bruto ou a qualidade de toda sombra.

| Caminho, profundidade limpa | MAE da resposta | Mudança indevida em regiões estáveis | MAE da imagem |
| --- | ---: | ---: | ---: |
| Entrada / resposta zero | 0,02308765 | 0 | 0,05050490 |
| IA anterior com novas normais | 0,03645806 | 0,00530663 | 0,02922339 |
| Dados novos, loss de imagem (controle) | 0,03606661 | 0,00299964 | 0,01659051 |
| Dados novos + supervisão por pares | 0,01418652 | 0,00187323 | 0,02023114 |

A supervisão por pares reduziu o erro de resposta em **60,67% frente ao controle**
e **38,55% frente à resposta zero**. Todos os três tipos de intervenção melhoraram
frente à resposta zero: remoção de um objeto, deslocamento e remoção de todos.
Com profundidade ruidosa, erro de resposta 0,01420750; a vantagem foi mantida.
Também passou na validação separada: resposta 0,01750664 versus zero 0,03443780.

Os dados novos sem a loss adicional melhoraram bastante a imagem estática, mas
continuaram sem produzir resposta adequada à intervenção. Isto reforça a diferença
entre aproximar uma imagem e acompanhar uma mudança de geometria. A comparação
controle/pares mantém a quantidade de dados e o orçamento iguais.

**Limitação decisiva:** o modelo com pares perdeu 21,94% em MAE de imagem frente
ao controle. O erro temporal limpo ficou 28,46% maior. Não foi possível conservar
todos os ganhos ao mesmo tempo nesta rodada.

| Erro temporal reprojetado | Controle | Com pares |
| --- | ---: | ---: |
| Profundidade limpa | 0,00910896 | 0,01170100 |
| Profundidade ruidosa | 0,01073719 | 0,01238116 |

O modelo com pares passou nos critérios de resposta, regiões estáveis e proteção
exata, mas **falhou na preservação da imagem e estabilidade temporal**. Não foi
exportado, medido na RX 7600 nem instalado. `synthetic_gates_passed=false` e
`game_integration_allowed=false` permanecem no resultado.

O comparativo visual foi inspecionado: o primeiro seed reservado tem uma esfera;
por isso as linhas remover um/remover todos coincidem. Ainda há suavização e
diferenças de aparência frente à referência. Não escolher outro exemplo mais
favorável nem apresentar isso como resultado de jogo. Pesos, dados procedurais
e manifesto de fontes foram conferidos; nenhuma troca na V2.3.

## Próximo experimento

Congelar estes dois checkpoints como referências. O objetivo seguinte é treinar
imagem, resposta às intervenções e coerência de câmera em conjunto, com orçamento
comparável e novas cenas de avaliação. Usar validação para decidir ajustes e
reservar outro teste final, sem procurar parâmetros que passem nestes mesmos
casos. A estabilidade não deve ser presumida por usar normais melhores.

Antes de ampliar para Blender, definir e verificar esse protocolo conjunto;
o diagnóstico anterior já mostrou que apenas continuar treino temporal pode
piorar a generalização. A família de esferas não demonstra materiais ou iluminação
genérica. A compatibilidade do futuro contrato do jogo, escalas de execução e
custo GPU continuam pendentes.

## Reprodução e evidência

`tools/train_neural_interventions.py` grava protocolo antes do treino, checkpoints,
curvas, hash dos pares gerados, métricas por cena/intervenção, manifesto de fontes
e comparativo visual em `artifacts/neural-intervention-training-v1`.
Não foi salvo NPZ completo; dados são reproduzíveis pelas fontes e seeds.
Tempos de treinamento são CPU, não latência de inferência na RX 7600.

Três novos testes passaram: distribuição/separação de objetos e cenas; resposta
exata da referência e gradientes da loss; repetibilidade, proteção e independência
do alvo em relação ao ruído observado.

```powershell
.\_IA_Python\venv\Scripts\python.exe -m unittest discover -s tests -p test_neural_intervention_training.py -v
.\_IA_Python\venv\Scripts\python.exe tools/train_neural_interventions.py --out artifacts/neural-intervention-replay --steps 1600
```

O runner recusa sobrescrever diretório existente e usa como referência secundária
o checkpoint próprio `artifacts/neural-lighting-v1/spatial.pt`. Não executa
modelos ou binários externos.
