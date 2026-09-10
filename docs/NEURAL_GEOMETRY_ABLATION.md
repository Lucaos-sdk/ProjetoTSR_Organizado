# Normais: direção, módulo e ordem de reconstrução — 10/09/2026

## Decisão

Executamos as variantes sugeridas, com pesos congelados e novas cenas.
Renormalizar reduz a sensibilidade à fase artificial, mas piora a fidelidade
de imagem em todos os 16 grupos de comparação frente à média bruta e piora
o erro temporal de movimento pequeno em 14 deles. Reconstruir normais depois
de reduzir a profundidade também não fecha a estabilidade. **Nenhuma variante
passou todos os critérios; a inferência oficial e a DLL permanecem iguais.**

Artefatos: `artifacts/neural-geometry-ablation-v1`. Implementação isolada em
`_IA_Python/neural_geometry_ablation.py`; execução por
`tools/diagnose_neural_geometry_ablation.py`. Tempo interno total: 100,89 s
em CPU/float32, não latência GPU. Sem treino ou alteração de checkpoints.

## Hipóteses e limites da explicação recebida

Uma média de normais pode perder módulo e misturar direções. Renormalizar
corrige o módulo quando existe uma direção não nula; não desfaz a mistura.
Porém, profundidade escalar também pode criar geometria inexistente: a média
entre superfícies a 2 e 20 unidades não representa necessariamente uma
superfície a 11. Ajustar um plano sobre essa média não garante recuperá-las.

As entradas 192×128 e 384×256 usam o mesmo campo de visão e a mesma saída
regional 96×64. Logo, os blocos de 2×2 e 4×4 cobrem aproximadamente o mesmo
ângulo/região da cena, com densidades de amostragem diferentes. A quantidade
de pixels não demonstra, sozinha, maior chance de cruzar uma superfície.

Uma falha da renormalização também não prova que a direção seja a causa
restante: distribuição de entradas aprendida, normais ausentes, reconstrução
do residual e amostragem das métricas continuam possíveis fatores. Ganhos
diferentes em movimentos pequenos e grandes não estabelecem exatamente duas
causas, nem tornam histórico temporal uma solução já comprovada.

## Protocolo anterior à execução

- Quatro novos IDs 27000–27003, duas famílias (esferas e blocos), duas resoluções.
- Mesmos quatro checkpoints da rodada anterior: t025 A/B e conjuntos regionais
  A/B, todos congelados. Sem escolher pesos por estes resultados.
- Profundidade limpa, luz fixa e objetos estáticos. Não há pares de remoção.
- Câmeras em yaw −0,04, −0,001, 0, +0,001 e +0,04, mesmas correspondências e
  métricas de erro contra o alvo próprio de cada quadro da rodada de fase.
- Com câmera parada: oito fases ±0,25/±0,5 pixel da entrada, um eixo por vez;
  apenas geometria muda fase. Cada variante compara com sua própria fase zero.
- Cor reduzida por área, validade central e ampliação bilinear do residual
  permanecem fixas. No teste com câmera, a fase é sempre zero.

| Variante | Normal | Profundidade |
| --- | --- | --- |
| `center` | centro, controle atual | centro |
| `depth_area` | centro | média por área |
| `normal_area` | média bruta | centro |
| `normal_unit` | média renormalizada | centro |
| `magnitude_only` | direção do centro × módulo da média | centro |
| `area` | média bruta | média por área |
| `area_unit` | média renormalizada | média por área |
| `postfit` | ajuste de plano 5×5 após reduzir profundidade | média por área |

Módulos ≤1e−6 resultam em normal zero, sem inventar direção. `magnitude_only`
mantém zero se o centro não tem direção; isso está explicitado porque não é
possível isolar direção de um vetor ausente.

Para `postfit`, intrínsecos da câmera foram convertidos para a grade menor:
`fx'=fx/sx`, `cx'=(cx+0,5−fase_x)/sx−0,5`, e equivalente em Y. A origem e a
base mundial são mantidas. Reutilizamos o ajuste por resíduo do plano e seu
fallback existente. O suporte de 5×5 na grade menor é maior que o de 5×5 na
entrada; portanto esse tratamento muda também o suporte espacial do ajuste.

Critérios pré-fixados: ≥20% de redução da variação de fase e do erro temporal
em cada amplitude, aumento de MAE de imagem ≤5%, proteção exata. Comparamos
todas as alternativas contra `center`, além dos contrastes isolados
`normal_unit/normal_area`, `area_unit/area` e `postfit/area_unit`. Exigimos
consistência em todos os modelos/famílias/resoluções. Não alteramos critérios
após ver resultados. Um eventual sucesso ainda não aprovaria integração.

## Resultados da imagem produzida pela rede

Intervalos percentuais nas 16 comparações por tratamento (4 checkpoints ×
2 famílias × 2 resoluções). **Negativo significa menos erro/variação.**

| Tratamento contra `center` | Fase artificial | Movimento pequeno | Movimento maior | MAE de imagem |
| --- | ---: | ---: | ---: | ---: |
| Só profundidade por área | −9,70% a +0,71% | −1,27% a +0,53% | −1,78% a +1,23% | −0,45% a +0,21% |
| Só normal por área | −52,47% a +38,84% | −36,31% a −5,36% | −11,03% a +0,41% | −6,14% a +2,19% |
| Só normal renormalizada | −75,11% a −1,35% | −31,66% a −4,00% | −11,31% a −1,86% | −0,43% a +10,20% |
| Só módulo da média | −22,14% a +59,94% | −16,23% a +0,48% | −6,66% a +5,23% | −6,59% a −1,27% |
| Área bruta completa | −56,43% a +22,94% | −36,91% a −5,94% | −12,86% a +1,57% | −6,19% a +1,91% |
| Área com normal unitária | −80,10% a −24,91% | −31,97% a −4,46% | −11,36% a −1,74% | −0,30% a +10,07% |
| Reconstrução posterior | −65,08% a +66,93% | −13,92% a +20,65% | −14,62% a +19,26% | −23,11% a −1,08% |

**O contraste direto solicitado, `area_unit` contra `area`:**

- Variação artificial de fase cai 20,44–56,27%, passando o limiar nos 16 casos.
- MAE de imagem aumenta 3,66–8,01% nos 16 casos; 8 ultrapassam o limite de 5%.
- Movimento pequeno vai de −0,91% a +11,67%; piora em 14/16 casos.
- Movimento maior vai de −7,32% a +6,54%; piora em 10/16 casos.
- Nenhum caso alcança ≥20% de melhora temporal em qualquer amplitude.

O contraste que mantém profundidade central (`normal_unit/normal_area`) conta
a mesma história: fase melhora 14,52–49,56%, imagem piora 3,65–8,05% e não há
ganho temporal consistente. A profundidade por área sozinha altera pouco a
saída neste domínio; as intervenções nas normais têm efeito muito maior.
Isso localiza sensibilidade, não demonstra que profundidade seja irrelevante
para outros cenários ou redes.

`postfit` melhora MAE estático contra o controle, mas tem regressões temporais
de até 20,65% no movimento pequeno e 19,26% no maior. Contra `area_unit`,
o movimento pequeno piora em todos os casos, entre 5,96% e 63,58%.
Todas as alternativas falham o conjunto de critérios contra o controle.

Os percentuais não precisam repetir a rodada de fase: usamos outros quatro
IDs. Por exemplo, a média bruta não excedeu +5% de MAE neste conjunto, embora
tenha excedido no anterior. Nenhuma rodada substitui ou apaga a outra.

## O que acontece com os vetores

Agora registramos separadamente MAE de componentes, variação de comprimento,
ângulo entre normais e contagem de normais ausentes. Ângulos usam `atan2` do
módulo do produto vetorial e do produto escalar; pares sem direção são
excluídos e contados. Logo, comparar ângulos de tratamentos com cobertura
diferente exige cuidado. Essas medidas são de estabilidade, não erro angular
contra uma normal verdadeira.

Em esferas 384×256, o MAE das componentes entre fases foi:

| Centro | Média bruta | Média renormalizada |
| ---: | ---: | ---: |
| 0,00163012 | 0,00178869 | 0,00093435 |

A renormalização elimina praticamente a variação de comprimento e preserva
a direção da média: sua variação angular é a mesma, aproximadamente 0,001938
radiano. Isso sustenta a participação do módulo na inversão do **indicador
de componentes**, mas a melhora desse indicador não implica melhora da
iluminação ou da estabilidade com movimento real. Nos blocos novos em
384×256, média bruta e centro já ficaram muito próximos, sem repetir exatamente
a inversão anterior.

Também encontramos uma diferença de cobertura:

| Grupo | Normais zero no centro | Normais zero após `postfit` |
| --- | ---: | ---: |
| Esferas 192×128 | 3,29% | 15,58% |
| Esferas 384×256 | 0,00% | 15,51% |
| Blocos 192×128 | 3,34% | 14,90% |
| Blocos 384×256 | 0,00% | 14,72% |

Percentuais dentro da máscara reduzida de efeito válido, na fase zero. Essa
máscara não garante que a reconstrução da normal seja válida. A média bruta
e sua versão unitária não tiveram vetores nulos nesses centros; isso também
não garante que representem a superfície correta. O método posterior muda
cobertura além de direção/suporte, portanto não é uma troca equivalente sem
efeitos colaterais.

## Verificação e próximos passos

42 testes neurais passaram. Os três novos verificam renormalização/cancelamento,
coordenadas e fase na câmera reduzida, isolamento de canais e reconstrução de
um plano frontal conhecido. O experimento exige igualdade exata com o caminho
atual no controle, repetição da mesma fase, preservação de pixels protegidos,
entradas intactas e hashes inalterados de fontes e checkpoints.

Nada foi treinado, exportado para FP16 ou instalado no jogo. DLL conferida:
`9B3992A1FD4337A1C235DC3401D59F8BC81614E29585F3F20546B4C9BC6D47D0`.

O próximo diagnóstico deve comparar normais reconstruídas e analíticas
**somente como controle explicativo**, registrando máscaras comuns e normais
ausentes. Isso ajuda a separar falha de reconstrução de sensibilidade da rede.
Também falta separar a variação do residual antes e depois da ampliação.
Manter testes de câmera real como medida principal, sem usar fase estática
como único substituto de estabilidade.

Histórico reprojetado continua uma hipótese de etapa posterior. Ele exige
validade de movimento/desoclusão e medição de ghosting; não deve apenas esconder
instabilidade por acumulação. O contrato atual do passe no jogo ainda precisa
ser ampliado para consumir movimento. Antes de promover qualquer candidato,
retomar intervenções com remoção de objetos, outros domínios, GPU e orçamento.
