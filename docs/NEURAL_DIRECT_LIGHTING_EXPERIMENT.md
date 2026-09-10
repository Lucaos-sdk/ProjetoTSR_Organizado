# IA de iluminação: mudança de objetivo e primeiro treinamento

09/09/2026. O usuário esclareceu que quer avançar na IA capaz de transformar
iluminação, não continuar ajustando o efeito visual V2.3. A prioridade passa a
ser aprender contra uma referência de cena renderizada sob outra iluminação.
A V2.3 permanece baseline instalado; o README geral continua adiado.

## Diferença para o trabalho anterior

V2.3: MLP de 171 parâmetros que aprende uma função de normal para ganho.
Grade bilateral: contexto regional para aproximar um ganho escalar sintético.
Ambas são redes treinadas, mas suas tarefas não demonstram síntese de sombras
ou materiais. Torná-las maiores não corrigiria automaticamente essa limitação.

Novo experimento: dois renders da mesma cena 3D, mantendo geometria, materiais
difusos e névoa, com luzes em posições e cores diferentes. Raios de visibilidade
calculam sombras projetadas por outros objetos. A rede prevê um residual RGB
espacial para aproximar o segundo render. Não é uma curva de tom usada como
referência e não modifica os pesos AMD FSR.

Isso também não é uma implementação de DLSS 5 ou prova de relighting genérico
de jogos. Ainda é uma tarefa controlada com fortes limitações de cena.

## Referência 3D e entradas

Renderizador próprio NumPy, câmera em perspectiva de FOV conhecido, plano e
três esferas de posição, raio e cor variados. Interseções analíticas e quatro
amostras determinísticas de luz por área: difuso Lambertiano, queda quadrática
com distância e visibilidade por raios. Entrada e referência já têm sombras;
a referência muda a luz principal e acrescenta preenchimento colorido.

O termo ambiente constante e a névoa exponencial são aproximações explícitas.
Não há path tracing convergido, iluminação indireta, reflexos, BRDF especular,
pele, roughness aprendida, personagens, textura de jogo ou criação de materiais.
As esferas ocupam três faixas de posição conhecidas; isso é um prior forte e
pode facilitar memorização espacial. Doze cenas novas não demonstram cobertura
de ambientes arbitrários nem solução para oclusores fora da tela.

Entradas da rede: RGB comprimido, três componentes de normal mundial reconstruída
de profundidade observada, Z da câmera / 30 e máscara válida. Não recebe albedo,
normais analíticas, máscaras de sombras, visibilidade das luzes ou geometria
oculta. Essas informações existem apenas no renderizador/loss/avaliação.
UI/céu têm máscara sintética conhecida, sem detecção automática.

A reconstrução de normal escolhe derivadas de posição que menos atravessam uma
descontinuidade, ideia também presente no shader espacial do Matheus, implementada
independentemente com intrínsecos exatos e teste numérico. Não copiamos sua
aproximação de profundidade/FOV, runtime HIP, patches por endereço ou pesos.
Fonte fixada: [Lighting.hlsl](https://github.com/MatheusGViana/dlss-5-amd-project/blob/7b9dcb9c3864fc82fd4e0f7473a6f27c459a0707/analysis/experimental-lighting/Lighting.hlsl).

Esta reconstrução não é o ajuste de plano 5×5 da V2.3. O contrato do futuro
runtime ainda precisa alinhar coordenadas, exposição, origem de câmera e formatos
com o treinamento, usando os dados realmente disponíveis no hook.

## Rede e treinamento inicial

CNN com encoder/decoder e conexões entre escalas, **38.787 parâmetros**, saída
residual RGB limitada a ±0,5 em luz linear. Conserva exatamente a entrada fora
da máscara; essa restrição não prova preservação de identidade dentro dela.
Não usa grade bilateral, guided filter, nitidez ou histórico de inferência.

Controle 1×1 sem vizinhança: 38.967 parâmetros, mesmas entradas e loss. Outro
controle ajusta apenas três ganhos RGB constantes aos dados de treino. Não
confundir esse último com todo filtro possível ou com a própria V2.3.

64 cenas de treino × três poses = 192 pares, 96×64, normais reconstruídas de
profundidade com ruído relativo de 0,05%. Doze cenas de validação e doze de teste,
IDs disjuntos; avaliação estática em duas poses não usadas no treino e com
profundidade limpa/ruído de 0,2%. Mil passos Adam, batch 4, taxa 0,002, sequência
de batches igual; checkpoint final fixado, sem seleção pelo teste. Uma seed,
sem estudo de convergência ou superioridade estatística.

Loss Charbonnier, gradientes e peso adicional em pixels onde raios mostram uma
sombra relevante. A máscara de sombras supervisiona o treino, nunca a inferência.
Runtimes de treinamento são CPU e não representam custo da RX 7600.

## Resultado inicial — cenas de teste limpas

| Caminho | MAE RGB contra o segundo render | MAE em sombras | Erro de gradiente |
| --- | ---: | ---: | ---: |
| Entrada original | 0,05111298 | 0,05583765 | 0,01439048 |
| Ganho RGB constante | 0,05144787 | 0,06849245 | 0,01544151 |
| Rede sem vizinhança | 0,03310230 | 0,08448590 | 0,01823549 |
| Rede espacial | 0,02877111 | 0,04204819 | 0,01290017 |

Rede espacial versus rede sem vizinhança: 13,08% menos MAE RGB e 50,23% menos
erro nas regiões de sombra. Frente à entrada original, redução de 43,71% no
MAE global e 24,70% em sombras. Percentuais medem aproximação da referência
sintética; não representam qualidade no Witcher ou comparação com DLSS.

A rede sem vizinhança melhora a média global, mas piora sombras. Isso justifica
medir regiões difíceis separadamente, em vez de declarar sucesso por MAE global.
Com ruído de profundidade, a rede espacial teve MAE 0,03155429.

## Problema revelado pelo movimento

48 pares com rotação e pequeno deslocamento reais da câmera em perspectiva.
Correspondência usa posição 3D, reprojeção, ID de superfície, profundidade e
distância subpixel. Métrica compara erros contra cada referência, não força
imagens diferentes a serem iguais. Amostragem nearest ainda introduz resíduo
subpixel; são diagnósticos, não um buffer temporal pronto para produção.

Erro reprojetado médio: rede espacial **0,00743273**, controle ponto a ponto
**0,00446499**. A candidata piorou cerca de 66,5% nessa métrica e falhou no
critério temporal. A inspeção visual mostra erros no chão, halos e alterações
amplas de cor; uma referência 3D melhor não torna automaticamente boa a saída.

Decisão: acrescentar supervisão entre vistas durante o treino e comparar com
continuação de treino sem essa loss. Não instalar esta primeira candidata.

## Artefatos e verificações

`artifacts/neural-lighting-v1`: protocolo, pesos de ambas as redes, métricas por
frame, hashes de fontes e dados procedurais, comparação PNG e giro de câmera GIF.
O hash dos dados permite reprodução; não foi salvo NPZ completo. A avaliação
visual usa a mesma exposição e curva de apresentação em todas as colunas;
métricas usam floats lineares, sem normalização individual da imagem.

Oito testes, incluindo os temporais posteriores, verificam raios conhecidos,
oclusão da luz sem mudança do material, projeção/reconstrução, normais de plano,
correspondência de superfícies, máscaras, identidades e derivadas. Eles não
substituem validação artística ou do runtime D3D12.

Reprodução do primeiro treino (novo diretório obrigatório):

```powershell
.\_IA_Python\venv\Scripts\python.exe -m unittest discover -s tests -p 'test_neural_lighting*.py' -v
.\_IA_Python\venv\Scripts\python.exe tools/train_neural_lighting.py --out artifacts/neural-lighting-replay --steps 1000
```

## Continuação controlada com supervisão temporal

Partindo dos mesmos pesos espaciais, duas cópias receberam 600 passos adicionais
com os mesmos pares (quatro frames por batch) e learning rate 0,0005. Uma usa
somente loss de imagem; outra acrescenta `0,5 * erro entre vistas reprojetado`.
A comparação penaliza diferença entre **erros relativos aos próprios alvos**,
sem penalizar mudanças legítimas de iluminação/perspectiva. Não mistura frames
na inferência; altera somente os pesos treinados.

Esta continuação foi motivada pelo primeiro resultado, e por isso usa novas
cenas de avaliação, IDs 9000–9011. Os pesos iniciais congelados também foram
avaliados nelas. Não comparar diretamente seus números com as cenas 8000–8011.

| Mesmas novas cenas | MAE RGB | MAE de sombra | Erro reprojetado |
| --- | ---: | ---: | ---: |
| Pesos iniciais congelados | 0,03182156 | 0,04447079 | 0,00871632 |
| Mais treino, sem loss temporal | 0,03946947 | 0,04933343 | 0,01007394 |
| Mais treino, com loss temporal | 0,03491763 | 0,05038283 | 0,00972695 |

Supervisão temporal reduziu o erro temporal apenas 3,44% frente à continuação
controle; critério fixado exigia 20%. Ambos ficaram piores que os pesos iniciais
congelados. Melhorar a loss durante o treino não se converteu em generalização.
Não há fundamento para promover estes pesos ou declarar o problema corrigido.
As imagens ainda mostram alterações amplas de cor, suavização e erros no chão.

Artefatos: `artifacts/neural-lighting-temporal-v1`, com três caminhos comparados,
48 pares por caminho, métricas por frame, protocolo e hashes. Runner:
`tools/train_neural_lighting_temporal.py`; depende do checkpoint inicial e recusa
sobrescrever sua saída. Oito testes passaram. Depois do treino, uma conversão
de tensor para escalar no teste ganhou `.detach()` para remover um aviso do
PyTorch; não alterou loss, treino ou pesos. Manifesto original foi preservado.

## Decisão e ponto de continuidade

A nova tarefa e rede são a direção ativa; a grade bilateral não é apresentada
como avanço principal em relighting. Nenhum candidato foi exportado para o
runtime do jogo, medido na RX 7600 ou instalado. Os dois experimentos permanecem
com `game_integration_allowed=false`; DLL instalada conferida com o hash V2.3.

Próximo teste prioritário: mover/remover oclusores e variar a disposição dos
objetos, mantendo regiões receptoras iguais, para verificar se sombras previstas
acompanham a geometria ou se a rede memorizou localização. Também comparar
normais analíticas/reconstruídas como ablação de diagnóstico, sem passar as
analíticas por entradas disponíveis no jogo. Separar aliasing/reprojeção de
instabilidade da rede antes de ajustar outra loss nas mesmas cenas.

Próximos requisitos além da coerência: variar número e disposição dos objetos,
oclusores fora da tela, materiais e iluminação; incluir dados do jogo com
referência útil; definir o contrato de escala/cor da inferência; medir a cadeia
GPU completa. A meta de 2–3 ms não foi medida para esta rede. A resolução do
treino não é prova de que basta ampliar o residual para 1080p.
