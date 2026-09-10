# Diagnóstico: resposta da IA à geometria e sensibilidade das normais

09/09/2026. Rodada com pesos congelados, sem treinamento, instalação de DLL,
commit ou alteração do README geral. O objetivo é testar a capacidade que os
ganhos estáticos anteriores não comprovavam: acompanhar alterações de sombras
quando a geometria muda.

## Intervenções em objetos

Doze cenas novas (10000–10011), quatro intervenções por cena: remover a esfera
central, deslocá-la para esquerda/direita e remover todas. Cada intervenção
mantém câmera, chão, cores dos materiais remanescentes, luzes e névoa. O argumento
opcional `spheres_override` em `render` permite trocar somente a geometria.
O caminho padrão permanece numericamente igual: o hash de todos os 192 frames
do treinamento inicial foi recalculado e coincidiu com o registro original.

Comparamos somente pontos do chão visíveis em ambas as imagens, com máscara
conjunta reduzida em dois pixels para excluir contornos de visibilidade. Posições
3D e profundidade desses receptores são verificadas iguais. Pixels que passaram
de objeto para chão não entram na comparação.

A métrica mede a mudança do **residual de iluminação**:

```text
esperado = (referência B − entrada B) − (referência A − entrada A)
previsto = (IA B − entrada B) − (IA A − entrada A)
```

Isso evita dar crédito à rede por apenas repassar sombras que já mudaram na
entrada. Regiões relevantes têm mudança média RGB > 0,005. Regiões inalteradas
têm mudança máxima < 0,000001. Medimos erro de resposta e alterações indevidas
nessas últimas regiões. Os erros são agrupados por número de pixels relevantes;
cosine/gain no JSON são resumos por caso ponderados, não regressão global única.

## Resultado de 48 pares

| Caminho | MAE da resposta | Alteração média onde a correção deveria permanecer igual |
| --- | ---: | ---: |
| Nenhuma adaptação do residual | 0,03570945 | 0 |
| Rede sem vizinhança | 0,03430418 | 0 |
| Rede espacial inicial | 0,03995006 | 0,00489755 |
| Rede com continuação temporal | 0,05005758 | 0,00712112 |

A resposta da rede espacial é **11,9% pior que não adaptar o residual**, apesar
do ganho anterior em MAE estático. Ela reage à geometria, mas a reação não é
suficientemente correta/localizada. Somente o deslocamento para direita teve
melhora média frente à resposta zero; remoção, deslocamento à esquerda e remoção
de todos pioraram. Nenhuma rede cumpriu o critério diagnóstico de 20% de ganho.

O comparativo de remoção de todos mostra correções residuais inadequadas no chão.
É compatível com dependência excessiva do layout e aprendizado insuficiente da
relação entre objetos e luz. Não prova, sozinho, o mecanismo interno de memorização
nem que aumentar a arquitetura resolveria. Os ganhos estáticos anteriores não
devem ser apresentados como prova de relighting que generaliza.

Artefatos: `artifacts/neural-lighting-diagnostics-v1`, com métricas por par,
normal ablation, PNG e par float NPZ da primeira cena escolhida previamente.

## Quanto vem das normais?

Mesmos pesos e imagens, trocando somente canais de normal. Avaliamos derivadas
de profundidade versus normais exatas do renderizador, com profundidade limpa
e ruído relativo de 0,2%. Normais exatas são dados privilegiados indisponíveis
nesse contrato do jogo; não são um novo modelo treinado nem um limite ótimo.

Para a rede espacial, erro temporal amostrado em 48 pares de câmera:

| Entrada | Normais reconstruídas | Normais exatas |
| --- | ---: | ---: |
| Profundidade limpa | 0,00851386 | 0,00644069 |
| Profundidade com ruído | 0,01392971 | 0,00644128 |

Há sensibilidade relevante às normais. Mesmo com normais exatas, a instabilidade
não desaparece. O teste mantém reprojeção nearest, resolução baixa e resíduos
subpixel; não atribui toda a diferença restante à rede. O teste de erro relativo
à própria referência evita confundir a mudança legítima de imagem com flicker.

## Transferência do ajuste geométrico da V2.3

Implementamos uma contraparte CPU do ajuste de plano em profundidade inversa
5×5, com pesos binomiais e rejeição de descontinuidades. O teste inicial, em
novas cenas 11000–11011, **não passou**: reduziu erros geométricos, mas piorou a
métrica temporal. O limite absoluto de 2% em profundidade aceita poucas amostras
em planos inclinados quando a imagem tem apenas 96×64. A cobertura de ajuste
limpo ficou em aproximadamente 6% dos pixels válidos.

Não mudamos os resultados ou thresholds daquele ensaio. Eles permanecem em
`artifacts/neural-fitted-normals-v1`.

Adicionamos uma segunda regra: estimar inicialmente um plano e rejeitar amostras
pela diferença em relação à profundidade inversa **prevista pelo plano**, não
pela diferença legítima de profundidade devido à inclinação. Isso preserva
vizinhos de superfícies inclinadas e continua rejeitando camadas desconectadas.
É opção experimental em `fitted_depth_normals`, sem alterar o render padrão.

## Regra de consistência do plano — cenas novas 12000–12011

Mantidos pesos e targets. Comparação do método direto com a nova regra:

| Métrica | Direto | Consistência de plano |
| --- | ---: | ---: |
| Média dos P95 de erro angular, profundidade limpa | 6,12° | 0,99° |
| Média dos P95 de erro angular, profundidade ruidosa | 13,97° | 2,83° |
| MAE RGB em pixels válidos, limpa | 0,04518759 | 0,04525732 |
| MAE RGB em pixels válidos, ruidosa | 0,04636256 | 0,04449747 |
| Erro temporal, limpa | 0,00765107 | 0,00768526 |
| Erro temporal, ruidosa | 0,01333404 | 0,01266660 |

Cobertura do ajuste: 85,3% limpa / 86,1% ruidosa. A geometria melhorou claramente,
mas o erro temporal ruidoso caiu apenas **5,0%**, abaixo dos 20% exigidos nesse
ensaio; limpo ficou 0,45% pior. Não confundir a grande redução de erro angular
com uma grande melhoria visual da rede. Candidata ainda não aprovada.

Artefatos: `artifacts/neural-plane-normals-v1`. IDs de cena separados em cada
rodada; não comparar médias absolutas entre rodadas como se fossem as mesmas
cenas. Esta é uma sequência de diagnósticos motivados pelos resultados anteriores,
não confirmação independente e definitiva de qualidade.

## Testes e reprodução

13 testes da família neural ao final: preservação dos frames originais, ausência
de mutação de geometria, receptores fixos, resposta perfeita da referência,
controle sem resposta, máscaras, entradas de normal, plano inclinado, profundidade
com ruído, degrau de profundidade, dados inválidos, raios/câmera e loss temporal.
Asserções iniciais de cobertura foram corrigidas para tratar o perímetro inválido
do estimador e verificar um ponto analítico do plano, sem relaxar critérios
de qualidade dos experimentos. Não foram repetidos build/testes GPU, pois só
mudaram ferramentas Python offline.

```powershell
.\_IA_Python\venv\Scripts\python.exe -m unittest discover -s tests -p 'test_neural_lighting*.py' -v
.\_IA_Python\venv\Scripts\python.exe tools/diagnose_neural_lighting.py
.\_IA_Python\venv\Scripts\python.exe tools/diagnose_fitted_normals.py
.\_IA_Python\venv\Scripts\python.exe tools/diagnose_fitted_normals.py --plane-residual
```

Os runners exigem os checkpoints próprios anteriores e recusam sobrescrever
seus diretórios de saída. Manifestos de fontes registram cada momento da
rodada; a adição posterior de funções opcionais não reescreve manifestos antigos.

## Próxima decisão

O gargalo não é apenas escolher outra loss: precisamos de treinamento e avaliação
com variações de layout/oclusores e geometria de entrada consistente. A regra
de plano é candidata para gerar essas entradas; ela não é uma nova IA de sombras
nem uma atualização instalada. Avaliar treinamento com pares de intervenção
e várias resoluções, separando cenas e objetos, antes de investir em um dataset
grande no Blender. Nenhum executável Blender foi localizado no PATH ou na pasta
padrão consultada; isso não é uma busca exaustiva e nada foi instalado.

Na futura base Blender, separar mudanças desejadas de iluminação de diferenças
acidentais entre renderizadores, mantendo câmera, exposição, assets e máscaras
alinhados. Loss perceptual/adversarial e histórico de inferência permanecem
propostas não implementadas. A prioridade continua iluminação neural com resposta
geométrica verificável, não nitidez ou outro ajuste da V2.3.
