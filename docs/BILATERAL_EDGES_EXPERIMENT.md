# Experimento de bordas e robustez — 09/09/2026

**Resultado:** loss de geometria + filtro guiado melhora bordas e estabilidade
da grade neste conjunto sintético, mas ainda não supera suficientemente o
controle ponto a ponto para justificar integração. A V2.3 do jogo não mudou.

## O que foi implementado

- `_IA_Python/bilateral_edges.py`: Sobel, ponderação por profundidade/normais,
  filtro guiado escalar com máscara e gerador de variações.
- `tools/train_bilateral_edges.py`: ablação de quatro variantes + controle
  de capacidade, mesmo orçamento, métricas por cena e reprodução.
- `tests/test_bilateral_edges.py`: referência NumPy independente, derivadas,
  bordas, máscara, ganho limitado e correspondência dos dados.

A [formulação de filtro guiado](https://people.csail.mit.edu/kaiming/eccv10/index.html)
usa um modelo linear local guiado por outra imagem. Implementamos a versão
escalar com máscara, duas médias locais, raio 2 e epsilon 0,001. A guia é
luminância comprimida `Y/(1+Y)`; o sinal filtrado é o **logaritmo do ganho**,
após o slicing trilinear. Não filtramos RGB, nem substituímos o slicing por um
operador de joint upsampling. Ganho permanece limitado a `exp(±0,25)` e pixels
fora da máscara conservam exatamente a entrada. Essa proteção exige máscara
conhecida; não é detecção automática de UI. Regiões válidas desconectadas dentro
da janela ainda podem influenciar umas às outras.

A nova loss soma `0,15 * erro Sobel ponderado` à loss V1. Compara gradientes da
predição e do alvo; não pune o gradiente verdadeiro da cena. Pesos entre 1 e 4
derivam de profundidade normalizada e normais **limpas de referência**, sem
gradiente de treino através dos pesos. Não usamos Laplaciano nem SSIM aqui.

## Protocolo fixado antes do treinamento

64 cenas de treino (3000–3063), 12 validação (4000–4011), 12 teste (5000–5011),
disjuntas das cenas V1. As quatro grades partem dos mesmos pesos. Seed 20260909,
Adam 0,002, 800 passos, batch 4 e sequência de batches idêntica. Controle 1×1
tem 10.271 parâmetros; cada grade tem 10.256. Nenhum checkpoint foi escolhido
por validação ou teste; usamos o passo final fixado. Uma seed não estabelece
superioridade estatística de arquitetura ou convergência ótima.

Casos alternados no treino e avaliados separadamente: limpo; exposição regional;
ruído em profundidade/normais; ambos. Exposição multiplica entrada e alvo pelo
mesmo campo e preserva a máscara. Ruído afeta só contexto geométrico: desvio
0,025 nas normais seguido de renormalização, 1% relativo na profundidade. Não
adicionamos ruído RGB nem treinamos denoising fotográfico. Os alvos continuam
sendo transformações escalares sintéticas de aparência, não iluminação física.

Os 256 pares/condições de treino são reproduzíveis por seeds e código. O hash
dos tensores gerados, manifesto de fontes, curvas, pesos e protocolo estão em
`artifacts/bilateral-edges-v2`. Não duplicamos o NPZ da V1.

## Cenas de teste limpas

| Variante | MAE RGB linear | Erro de gradiente | Sobel ponderado | Erro temporal reprojetado |
| --- | ---: | ---: | ---: | ---: |
| Grade de referência desta rodada | 0,00930250 | 0,00138235 | 0,00214333 | 0,00053380 |
| Somente loss de geometria | 0,00943482 | 0,00136808 | 0,00214112 | 0,00050228 |
| Somente filtro guiado | 0,00935569 | 0,00132843 | 0,00207212 | 0,00045278 |
| Loss + filtro guiado | 0,00917190 | 0,00127631 | 0,00201924 | 0,00036318 |
| Controle ponto a ponto de tamanho semelhante | 0,00934716 | 0,00115971 | 0,00185358 | 0,00000000 |

Combinação versus grade de referência: **7,67% menos erro de gradiente**, 5,79%
menos Sobel ponderado, 31,96% menos variação do erro reprojetado, mas somente
1,40% menos MAE RGB. Frente ao controle ponto a ponto, a vantagem MAE é 1,87%,
abaixo dos 5% definidos; o controle ainda tem melhores bordas limpas. Não
comparar diretamente os números à V1: cenas, seed e distribuição de treino
mudaram. Não traduzir percentual de erro sintético em percentual de qualidade
de jogo ou FPS.

## Exposição e ruído em movimento

São 24 pares por condição, com translação horizontal de dois pixels. Comparamos
o **erro de cada predição contra seu próprio alvo** depois de reprojetar, usando
a interseção das máscaras válidas. Nos casos de exposição a fase muda 0,08 por
frame; nos casos de ruído há nova amostra geométrica. Isso não é variância da
imagem bruta nem medição direta de flicker perceptual.

| Variante | MAE com exposição + ruído | Erro temporal com exposição + ruído |
| --- | ---: | ---: |
| Grade de referência | 0,01445597 | 0,00098132 |
| Somente loss | 0,01473871 | 0,00086945 |
| Somente filtro | 0,01452840 | 0,00082418 |
| Loss + filtro | 0,01430877 | 0,00066906 |
| Controle ponto a ponto | 0,01454256 | 0,00138558 |

A grade combinada é mais robusta que o controle ponto a ponto nessa condição
ruidosa, embora perca em contornos limpos. A loss sozinha e o filtro sozinho
não melhoraram a cor. A média não substitui inspeção dos piores casos, registrados
por cena em `metrics.json`. O comparativo visual de quatro linhas mostra duas
cenas, cada uma limpa e combinada; inspecionado, mas são padrões e relevos
sintéticos simples, não personagens ou vegetação realista.

Todas as variantes conservaram exatamente as regiões protegidas em teste.
Os critérios de bordas, estresse e movimento passaram; **nenhuma passou o
critério completo**, pois nenhuma alcançou 5% de ganho MAE frente ao controle
de capacidade. `game_integration_allowed` continua falso.

## Verificação e limites

Nove testes passaram: cinco novos e quatro anteriores. O filtro foi comparado
com uma implementação NumPy de janelas explícitas, incluindo bordas e buracos;
derivadas verificadas por diferenças finitas. Testamos identidade, limite de
ganho, proporções RGB, pesos de contorno e ausência de contaminação das referências
limpas pelo ruído de entrada.

Treinamento e avaliação foram em PyTorch CPU. Não exportamos estas variantes
para ONNX, nem medimos DirectML, VRAM ou GPU. Raio 2 em 128×96 não determina o
raio correto para 1476×830; sua resolução de execução e custo precisam de avaliação.
Não houve rotação 3D, reprojeção perspectiva, objetos móveis, desoclusão real,
frames do jogo ou teste de normais reconstruídas do buffer do Witcher.

## Onde continuar

1. Congelar esta rodada como resultado, sem ajustar hiperparâmetros nos mesmos
   testes para tentar fazê-los passar. A combinação é hipótese para a próxima
   avaliação, não versão de produção aprovada.
2. Substituir o gerador simples por geometria e câmera conhecidas, com rotação
   perspectiva e reprojeção verificável; incluir recortes, objetos que ocultam
   outros, névoa e exposição variável. Rotacionar uma imagem 2D não satisfaz isso.
3. Treinar/avaliar novas cenas com normais reconstruídas de profundidade, próximas
   ao contrato real, e comparar loss de borda versus preservação regional. Dados
   e referência são agora o gargalo mais importante que aumentar a rede.
4. Exigir ganho também nos contornos difíceis e estabilidade, depois medir o
   caminho completo na RX 7600 em executável isolado. Só então integrar.

Reprodução:

```powershell
.\_IA_Python\venv\Scripts\python.exe -m unittest discover -s tests -p 'test_bilateral*.py' -v
.\_IA_Python\venv\Scripts\python.exe tools/train_bilateral_edges.py --out artifacts/bilateral-edges-replay --steps 800
```

O runner recusa sobrescrever diretório existente. Referência externa revista
nesta rodada: [atualização Matheus](MATHEUS_UPDATE_2026_09_09.md).
