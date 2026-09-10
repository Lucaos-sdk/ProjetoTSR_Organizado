# Direção do projeto — reconstrução e transformação neural

## Separação de normais e profundidade — 10/09/2026

O [diagnóstico de módulo, direção e ordem de reconstrução](NEURAL_GEOMETRY_ABLATION.md)
comparou oito variantes em quatro novas cenas com os mesmos quatro checkpoints.
Renormalizar a média reduziu a variação artificial de fase em 20,44–56,27%, mas
piorou o MAE de imagem em 3,66–8,01% contra a média bruta; o erro temporal de
movimento pequeno piorou em 14/16 comparações. Alterar só a profundidade teve
pouco efeito. Reconstruir normais depois da redução melhorou MAE estático, mas
introduziu mais normais ausentes e regressões temporais. Nenhuma alternativa
passou todos os critérios. Próxima comparação: normais reconstruídas contra
analíticas como controle, com cobertura comum, e residual antes/depois da
ampliação. 42 testes passaram; sem treino ou mudança da DLL instalada.

## Último diagnóstico — 10/09/2026

A [hipótese de fase de amostragem](NEURAL_SAMPLING_PHASE.md) foi testada em
quatro novas cenas, duas famílias e duas resoluções, com quatro checkpoints
congelados. As normais são reconstruídas antes da redução. A média bruta da
geometria reduziu o erro temporal em 10,94–33,71% para movimentos pequenos e
1,93–14,47% para movimentos maiores, mas aumentou o erro de imagem em até
7,08%. A fase artificial também não melhorou uniformemente em 384×256.
Nenhuma comparação cumpriu todos os critérios. Isso justifica aprofundar o
diagnóstico, não adotar a média nem atribuir todo o problema a uma grade
oscilante. Próximo passo: separar normais/profundidade/cor e reconstrução do
residual, com câmera real e pesos congelados. 39 testes passaram; a DLL V2.3
mantém o mesmo hash. Não houve treinamento ou instalação nesta rodada.

## Estado atual — 09/09/2026

A [V2.3 instalada](RELIGHTING_V23_SCENERY.md) executa uma MLP própria de 171 parâmetros em HLSL antes do FSR, com orientação ancorada, preservação de cores e proteção de cenário distante. V2.2 teve execução e efeito observados no jogo; a validação visual da V2.3 está pendente. Isso atualiza as etapas históricas abaixo: já há integração gráfica, mas não há síntese de materiais ou equivalência ao DLSS 5 demonstrada.

Após avaliar as propostas do usuário, a [decisão para o próximo experimento](NEURAL_NEXT_STEP_REVIEW.md) é investigar correção neural de iluminação com contexto espacial e treinamento verificável. Gaussian Splatting não é prioridade para a DLL. DirectML é candidato de runtime a medir, sem garantia de WMMA ou de 1–2 ms; os exemplos recebidos não são integração pronta. Nenhuma nova DLL foi instalada nessa avaliação.

O [primeiro experimento bilateral](BILATERAL_REGIONAL_EXPERIMENT.md) já foi treinado em CPU: 10.256 parâmetros, dados sintéticos separados por cena, slicing validado e exportação FP16 verificada. Reduziu o erro contra o alvo controlado, mas a vantagem sobre uma rede ponto a ponto de tamanho semelhante foi apenas 3,04%, com limitações nas bordas e no movimento. Não é candidato aprovado para integração; orçamento GPU/memória e generalização ao jogo estão pendentes.

Na [segunda rodada offline](BILATERAL_EDGES_EXPERIMENT.md), implementamos loss de bordas orientada por geometria e filtro guiado sobre a correção, com exposição regional e ruído. A combinação melhorou bordas e movimento frente à grade de referência, mas teve apenas 1,87% de vantagem de cor sobre o controle de capacidade; não passou o critério completo de integração. Nove testes passaram. A [atualização do projeto Matheus](MATHEUS_UPDATE_2026_09_09.md) foi revista por fonte fixada; não incorporamos seu runtime privado. Próximo experimento: geometria/câmera perspectiva, rotação e normais reconstruídas. Nenhuma mudança na DLL do jogo nesta rodada.

**Prioridade esclarecida pelo usuário em 09/09:** avançar na IA que aprende iluminação, não em novos ajustes de aparência da V2.3. Foi implementado e treinado um [novo experimento contra renders 3D com sombras calculadas](NEURAL_DIRECT_LIGHTING_EXPERIMENT.md): CNN espacial de 38.787 parâmetros, entradas de cor e geometria reconstruída, residual RGB. Melhorou a aproximação estática frente ao controle ponto a ponto, mas falhou em coerência ao girar a câmera. Continuação com supervisão temporal e controle de mesmo orçamento também não generalizou melhor. Próximo diagnóstico: mover/remover oclusores e variar layout; verificar sombras dependentes da geometria e erros de reconstrução, antes da integração. Nenhum desses pesos foi instalado.

O teste V2.3 do usuário foi recebido e analisado: contador chegou a 144.620 quadros, mediana amostrada do passe 0,23560 ms e nenhum registro `[E]` no analisador. Há bypasses automáticos e associação experimental de câmera; detalhes em [V2.3](RELIGHTING_V23_SCENERY.md). Os dados confirmam execução do baseline, não qualidade/performance da nova CNN.

Continuidade de 09/09: o [diagnóstico de intervenções e normais](NEURAL_CAUSAL_DIAGNOSTICS.md)
foi executado. Ao mover/remover objetos, a resposta da rede espacial ficou pior
que manter o residual, impedindo interpretar o ganho estático como relighting
generalizado. Normais exatas reduziram a instabilidade, mas não a eliminaram.
Uma regra de ajuste por consistência de plano melhorou a geometria de entrada,
sem cumprir o critério temporal da CNN congelada. Próximo passo: dados com
variações de layout/oclusores e entradas geométricas consistentes. Sem mudança
de DLL, treinamento, commit ou README geral nesta rodada.

Atualização seguinte: [treinamento com intervenções](NEURAL_INTERVENTION_TRAINING.md)
executado com duas CNNs idênticas, 64 layouts aleatórios e normais reconstruídas
robustas. Supervisão por pares reduziu erro de resposta em 60,67% frente ao controle
e 38,55% frente à resposta zero, mas piorou MAE global e erro temporal. A nova rede
passou a responder melhor às mudanças da geometria neste domínio restrito; ainda
não é candidata aprovada para a DLL. Próximo passo: treino conjunto de imagem,
intervenções e câmera, mantendo controles e novas cenas reservadas.

O [treino conjunto](NEURAL_JOINT_TRAINING.md) foi concluído: duas redes de 38.787
parâmetros, quartetos com intervenção e duas câmeras, pesos iniciais e 1.600
passos iguais. No novo teste reservado, a supervisão temporal reduziu o erro
temporal em 73,23% (72,02% com ruído), mantendo resposta às intervenções melhor
que o controle. O MAE de imagem aumentou 6,02%, acima do limite de 5%; cinco de
seis critérios passaram. Validação e teste também divergem no ganho de resposta.
Ainda não aprovado para integração. Próxima rodada: equilibrar fidelidade e
coerência com seleção pela validação, outras inicializações e novo teste final.
Dezenove testes passaram; a V2.3 instalada permanece intacta.

A [comparação de pesos de treinamento](NEURAL_BALANCE_SELECTION.md) avançou:
oito treinos, duas inicializações, seleção em 16 cenas novas e teste final em
outras 24. Peso temporal 0,25 passou nos nove critérios nas duas inicializações,
na validação e no teste. Frente aos controles de mesmo orçamento, o erro temporal
caiu 31,49–38,32%, a resposta às intervenções melhorou 4,19–9,69% e o erro global
de imagem diminuiu 0,74–2,54%; bordas e sombras também melhoraram. Vinte e dois
testes passaram. Esse é o novo baseline da tarefa sintética, ainda sem aprovação
para o jogo. Próximo passo: diagnóstico em geometrias/iluminação e resoluções
mais variadas, seguido de validação de execução GPU isolada. V2.3 preservada.

O [diagnóstico de generalização e escala](NEURAL_GENERALIZATION.md) foi concluído
com pesos congelados. A aprovação de t025 permanece restrita ao domínio pequeno:
blocos/luz variada não passaram consistentemente, e executar diretamente em
192×128/384×256 perdeu resposta e vantagem temporal. Foi implementada uma
inferência regional com altura interna 64 e residual aplicado à cor original.
Em outras cenas, ela reduziu o erro de imagem em 21,54–42,41% frente à execução
direta, mas ainda falhou na estabilidade em 192×128 e em regiões que deveriam
ficar inalteradas. Trinta e um testes passaram; nenhuma nova aprovação de jogo.
Próximo passo: treinar e validar essa cadeia de redução/reconstrução em várias
escalas, incluindo preservação de bordas, antes do teste GPU e da integração.

Em 10/09, foi concluído o [treinamento pela cadeia regional](NEURAL_REGIONAL_TRAINING.md):
quatro continuações com controles de mesmo orçamento, 32 cenas novas, três
escalas e ruído, mais validação/teste separados. O modelo conjunto reduziu erro
de imagem em 15,54–34,42% e de resposta em 13,07–23,70% frente aos pesos
congelados no teste. O erro temporal ainda não passou e piorou em parte dos
casos. Frente ao controle treinado, houve ganho temporal, com perda de imagem.
34 testes passaram; nenhuma aprovação de jogo. Um diagnóstico posterior mostrou
erro distribuído no interior do chão, não concentrado nas bordas. Próximo passo:
isolar a sensibilidade à amostragem e a pequenos movimentos de câmera, antes de
tratar interpolação guiada nas silhuetas como solução geral. V2.3 preservada.

## Intenção confirmada pelo autor

Criar uma solução experimental para RX 7600 que busque qualidade visual inspirada no DLSS 5, incluindo **transformação neural de iluminação e materiais**, além de reconstrução 1080p → 4K e estabilidade temporal. Não é apenas aumentar nitidez. Este é um projeto independente; não é uma implementação oficial de AMD FSR ou NVIDIA DLSS.

Atualização de uso: o autor pretende jogar com **saída final 1920×1080**. A resolução interna é configurável; 4K permanece como opção e teste. O objetivo de compatibilidade inclui outras GPUs DX12 e resoluções, sujeito a validação. Consulte [resoluções e GPUs](RESOLUTIONS_AND_GPUS.md) para distinguir o suporte do harness do contrato neural ainda estático.

A [descrição oficial do DLSS 5 pela NVIDIA](https://www.nvidia.com/en-us/geforce/news/dlss5-breakthrough-in-visual-fidelity-for-games/) apresenta renderização neural que acrescenta iluminação e materiais à imagem. Essa referência de produto não demonstra que nosso modelo, hardware ou acesso aos dados permita reproduzir o resultado. Não há equivalência de arquitetura, qualidade ou desempenho demonstrada.

## Como isso altera o plano

A reconstrução temporal continua sendo a base. O produto desejado também exige um modelo de transformação visual consistente no tempo, com dados e objetivos próprios. A UltraLightTSRNet atual não implementa relighting ou síntese de materiais validada. Seu contrato estrutural de tensores não determina uma solução para essa tarefa. Não usar pesos aleatórios como demonstração de melhoria.

Desenvolvimento por etapas verificáveis:

1. **Base geométrica e temporal:** reprojeção, profundidade anterior prevista, desoclusão, reset, exposição e grade de saída estável. A base atual já tem validação numérica de câmera fixa e translação sintética; rotação, objetos dinâmicos e dados do jogo ainda faltam.
2. **Avaliação visual reproduzível:** sequências pareadas, referência conhecida, inspeção de ghosting/flicker e erros em bordas, além de comparação numérica. Separar sequências por cena para evitar validar no material de treinamento. Registrar memória e tempos por etapa.
3. **Primeiro experimento de transformação neural, offline:** iniciado com uma tarefa de iluminação difusa fixa e normais sintéticas conhecidas; há treinamento reproduzível, pesos e quatro cenas reservadas em [experimento de iluminação](RELIGHTING_FIXTURE.md). O [passe DirectX 12](RELIGHTING_DX12.md) já estima normais de profundidade sintética e executa esses pesos na RX 7600, com comparação visual, GPU/CPU e custo isolado medido. Ainda faltam dados do jogo, orientação de câmera, preservação de estrutura em cenas reais e estabilidade temporal abrangente.
4. **Controle do resultado:** preservar interface/texto, silhuetas, identidade e intenção visual mediante restrições de treino e máscaras quando disponíveis. Prever intensidade configurável e bypass verificável. Comparar qualidade contra a imagem original e contra upscaling sem transformação, mantendo câmera e cena iguais.
5. **Modelo adequado ao orçamento:** usar medições para decidir resolução interna, arquitetura, formatos e eventual destilação. A rede proposta inicialmente pode precisar mudar; não presumir que seus 12 coeficientes representem iluminação e materiais.
6. **Runtime e jogo:** implementar interoperabilidade D3D12/DirectML correta, depois integrar por contexto OptiScaler e validar The Witcher 3. Confirmar quais recursos o jogo realmente fornece; color/depth/motion não equivalem automaticamente a normais, albedo, roughness ou iluminação separada.

## Restrições que continuam valendo

- GPU alvo RX 7600 8 GB. Memória deve ser medida com o jogo em execução, não apenas no harness isolado.
- Menos de 3 ms continua como meta original de pipeline, sem comprovação. A transformação neural adiciona trabalho; sua viabilidade dentro desse mesmo orçamento deve ser medida, não presumida nem redefinida silenciosamente.
- A demonstração atual é numérica e sintética. Não estabelece qualidade próxima de DLSS 5, superioridade a FSR/DLSS, reconstrução fotorrealista ou desempenho no jogo.
- Próximo avanço concreto: grade de saída estável/avaliação visual temporal, ampliando as fixtures de câmera e explicitando dados necessários para objetos móveis. A fase neural deve começar com tarefa/dataset verificáveis, não com uma integração prematura da engine antiga.

## Atualização de integração

A gravação do passe temporal foi separada do harness em `TemporalPass::Record`. O caminho atual e os marcos para a primeira DLL estão em [primeiro teste em jogo](FIRST_GAME_TEST.md). A [referência externa recebida](EXTERNAL_DLSS_NR_REVIEW.md) contém documentação, sem código reutilizável no material inspecionado.
