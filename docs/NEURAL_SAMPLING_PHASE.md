# Sensibilidade de amostragem geométrica — 10/09/2026

## Resultado e decisão

A hipótese sugerida pelo usuário foi implementada e medida com pesos congelados.
Existe sensibilidade à fase artificial de amostragem. A média por área melhora
o erro temporal com câmera em movimento neste conjunto, mas não resolve o
problema nem passa todos os critérios: em duas comparações o erro de imagem
aumenta aproximadamente 7%. **Não alterar a inferência oficial, treinar novamente
ou instalar estes operadores na DLL com base neste resultado.**

Saídas reproduzíveis: `artifacts/neural-sampling-phase-v1`. Código:
`_IA_Python/neural_sampling_phase.py` e `tools/diagnose_neural_sampling_phase.py`.
O diagnóstico foi executado em CPU/float32, sem inferência DirectML ou medição
de tempo na RX 7600. Tempo interno total do experimento: 57,86 s.

## O que o código existente realmente faz

1. Renderiza cor e profundidade na resolução de entrada.
2. Reconstrói normais **nessa profundidade original**, por ajuste de plano 5×5
   com critério de resíduo do plano. Não faz o ajuste na profundidade reduzida.
3. Reduz a cor por área para altura 64, mantendo a proporção da imagem.
4. Reduz normais, profundidade e validade por `nearest-exact`.
5. Executa a CNN e amplia seu residual RGB por bilinear; soma à cor original.

Com dimensões fixas, o mapa de índices da redução não muda sozinho entre quadros.
O movimento da câmera altera os valores e a cobertura das superfícies nesse mapa.
O novo teste muda a fase **deliberadamente**, como uma intervenção no operador.

A localização anterior mostrou erro sem concentração desproporcional na faixa
de borda de dois pixels. Isso não exclui toda influência de oclusões: a máscara
é limitada e o campo receptivo da rede pode espalhar a resposta de uma borda.
Também não prova, por si só, que o gargalo esteja exclusivamente antes da CNN.

## Protocolo registrado antes de executar

- Quatro novos IDs, 26000–26003; esferas (`baseline`) e blocos (`boxes`).
- Entradas 192×128 e 384×256, reduzidas para 96×64: fatores inteiros 2× e 4×.
- Profundidade limpa, luz fixa, objetos parados, sem pares de remoção nesta rodada.
- Quatro checkpoints: t025 A/B e treinados conjuntos regionais A/B. Nenhum peso
  é atualizado ou escolhido a partir dos resultados.
- Fase `(dx,dy)` em **pixels da entrada**, não células da grade reduzida:
  ±0,25 e ±0,5 em cada eixo separadamente; oito intervenções.
- Comparação `center` contra média `area` bruta dos quatro canais geométricos.
  Sem renormalizar normais, ponderar validade ou selecionar superfícies: esses
  seriam outros tratamentos e não foram misturados ao teste.

### Teste estático

Câmera, geometria, cor, máscara de validade e grade de saída ficam fixas.
Somente a fase usada para ler profundidade e normais é alterada. Assim, não se
desloca a imagem final nem se mede um desalinhamento intencional da grade de saída
como se fosse flicker. É um teste da **assimetria entre cor e geometria**;
não testa o deslocamento conjunto de todos os canais.

Cada operador é comparado à sua própria saída na fase zero. Medimos MAE das
componentes da normal, MAE de profundidade em unidades da cena e MAE RGB da saída.
Amostras geométricas usam a mesma máscara de validade reduzida do controle;
saídas usam a validade em alta resolução. Essas métricas não são perdas contra
o alvo: com a cena parada, medem apenas a variação induzida pelo operador.

Integramos caixas fracionárias exatamente sobre pixels considerados constantes,
com extensão do pixel de borda. Não há pré-deslocamento bilinear da textura.
O teste é restrito a fatores inteiros, nos quais a fase zero corresponde à
redução `area` atual. Não extrapolar para escalas não inteiras sem verificar
o contrato de amostragem.

`nearest-exact` escolhe `floor((i+0,5)*escala+fase)`. Nos fatores pares testados,
os centros caem num empate entre pixels. Fases negativas trocam a amostra;
as positivas até meio pixel a mantêm. Por isso registramos cada direção,
além da média das oito intervenções. Não interpretamos a média como ruído
aleatório típico de um jogo.

### Controle com movimento real

Mantemos a fase zero durante todo o movimento e renderizamos cinco câmeras:
parâmetro yaw −0,04, −0,001, 0, +0,001 e +0,04 radianos. Esse parâmetro da
fixture também altera ligeiramente a origem da câmera. Comparamos pares
negativo→zero e zero→positivo para cada amplitude, sempre com geometria igual.

Métrica: reprojetar o **erro em relação ao próprio alvo de cada quadro**, com
os filtros existentes de visibilidade, profundidade, ID e proximidade de pixel.
Isso permite a iluminação de referência variar legitimamente com a câmera.
A reprojeção usa pixels mais próximos; ainda existe erro de amostragem nela.

Nas correspondências aceitas, os deslocamentos medianos pequenos foram cerca
de 0,136 pixel em 192×128 e 0,265 pixel em 384×256. Os maiores ficaram em
5,10–5,12 e 10,73–10,90 pixels, respectivamente. Máscaras e contagens estão
salvas; as duas amplitudes e resoluções não têm populações idênticas.

### Critérios fixos do diagnóstico

Para considerar a média bruta uma alternativa promissora consistente:
reduzir em pelo menos 20% a variação artificial de fase e o erro temporal
em ambas as amplitudes; permitir no máximo 5% de aumento de MAE de imagem;
manter pixels protegidos exatos. Exigir isso em cada modelo/família/resolução,
sem esconder falhas em uma média geral. Mesmo um sucesso aqui não aprovaria
integração: faltam resposta a intervenções, outros domínios e orçamento GPU.

## Resultados

Variações percentuais de `area` frente a `center`, intervalos dos quatro
checkpoints. **Negativo significa menos erro/variação; positivo significa mais.**

| Grupo | Fase artificial | Movimento pequeno | Movimento maior | Erro de imagem |
| --- | ---: | ---: | ---: | ---: |
| Esferas 192×128 | −56,55% a −51,19% | −19,07% a −13,89% | −8,42% a −2,74% | −5,94% a +6,75% |
| Blocos 192×128 | −57,09% a −51,59% | −16,30% a −10,94% | −12,71% a −6,22% | −5,76% a +7,08% |
| Esferas 384×256 | −7,19% a +16,33% | −33,71% a −27,11% | −11,11% a −1,93% | −5,06% a −3,99% |
| Blocos 384×256 | −6,31% a +16,01% | −20,08% a −17,04% | −14,47% a −6,13% | −4,43% a −3,51% |

Das 16 comparações, 8 falham no limiar de fase, 11 no de movimento pequeno,
16 no de movimento maior e 2 no de imagem. Nenhuma passa o conjunto completo.
As duas regressões de imagem acima de 5% são do conjunto B em 192×128.
Não reajustamos os limiares depois de observar isso.

Na entrada reduzida, a média por área diminui a variação de profundidade nos
quatro grupos. Para normais, ela diminui a variação em 192×128, mas **aumenta**
em 384×256. Reduzir a variação de um canal não garante reduzir a da saída.

| Entrada geométrica | Centro: esferas / blocos | Área: esferas / blocos |
| --- | ---: | ---: |
| MAE de componentes normais, 192×128 | 0,007003 / 0,006928 | 0,003678 / 0,003602 |
| MAE de componentes normais, 384×256 | 0,001684 / 0,001586 | 0,001838 / 0,001806 |
| MAE de profundidade, 192×128 | 0,153971 / 0,152713 | 0,102111 / 0,103939 |
| MAE de profundidade, 384×256 | 0,077993 / 0,076196 | 0,051340 / 0,051409 |

O MAE RGB artificial com centro ficou entre 0,000586 e 0,004587. Com câmera
real, o controle ficou entre 0,000685 e 0,000989 para movimento pequeno,
e 0,004491–0,009005 para o maior. Em comparações correspondentes, a razão
entre fase artificial e erro do movimento maior é 0,113–0,617. **Não é uma
fração causal explicada:** as perturbações e máscaras são diferentes.

O teste confirma a sensibilidade a esta intervenção, não confirma que uma
origem de grade oscilante seja a causa dos erros reais. O desacordo em 384×256
— fase por vezes pior, movimento melhor — é uma razão concreta para não usar
só o teste estático como critério para uma correção temporal.

## Verificação e limitações

39 testes neurais passaram, incluindo cinco novos: equivalência na fase zero,
integral fracionária conhecida e empate de centro, campo constante invariante,
inferência compatível/proteção exata e rejeição de escalas/fases fora do contrato.
O experimento também exigiu repetição idêntica da mesma fase, compatibilidade
bit a bit com a inferência atual e ausência de mutação das entradas.
Manifesto de fontes anterior à execução e hashes dos quatro checkpoints foram
verificados novamente ao terminar.

Nenhum teste mede qualidade em jogo, precisão FP16, latência GPU ou VRAM.
A média bruta pode criar profundidades entre superfícies e reduzir o módulo
das normais; não deve ser promovida silenciosamente a filtro de produção.
Não houve treinamento, troca da V2.3, alteração de configuração ou commit.
SHA256 da DLL instalada conferido:
`9B3992A1FD4337A1C235DC3401D59F8BC81614E29585F3F20546B4C9BC6D47D0`.

## Próximo passo recomendado

Continuar o diagnóstico com os pesos congelados, sem outra busca de loss.
Separar intervenções **apenas nas normais** e **apenas na profundidade**;
verificar também a participação da redução de cor e da ampliação bilinear do
residual. Usar o teste de câmera como medida principal, e fase como diagnóstico
auxiliar. Comparar normais reconstruídas com normais analíticas somente como
controle explicativo, nunca como uma entrada disponível no jogo.

Se a redução geométrica continuar sendo uma fonte relevante, comparar uma
redução que respeite validade e superfícies com a média bruta e o centro, sob
critérios registrados antes da execução. Verificar imagem, sombras e resposta
a remoção de objetos para não trocar sensibilidade à geometria por suavização.
Só depois retomar treinamento, generalização mais ampla, exportação e benchmark
GPU. A meta continua sendo iluminação aprendida e estável, não adicionar nitidez.
