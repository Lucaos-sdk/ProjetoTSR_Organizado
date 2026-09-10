# Grade bilateral regional — experimento isolado V1

08/09/2026. Etapas de dados/treinamento executadas em Python/PyTorch **CPU**, sem instalar nova DLL. A V2.3 permanece como baseline de jogo. Não há benchmark DirectML/RX 7600 desta rede.

## Hipótese e correções ao roteiro

A [pesquisa HDRnet](https://groups.csail.mit.edu/graphics/hdrnet/) aprende transformações locais a partir de pares de imagens, prevendo uma grade em baixa resolução e aplicando-a em alta resolução. Inspiramo-nos nessa separação; não copiamos código ou pesos do HDRnet. A representação não garante preservação de névoa, contornos, estabilidade temporal ou execução abaixo de 1 ms na RX 7600.

O slicing interpola **trilinearmente** em x, y e intensidade. O terceiro eixo não é profundidade geométrica. Uma transformação afim de RGB pode mudar cor e produzir halos; neste primeiro experimento usamos um ganho escalar limitado e confiança, menos expressivos, para preservar proporções RGB por construção.

Na integração futura, “alta resolução” significa a resolução interna recebida antes do FSR, não obrigatoriamente a resolução final do monitor. Entradas nativas do caminho atual são lineares FP16/FP32, não presumimos RGBA8. Uma barreira não é uma conversão de layout de tensor: os cálculos de empacotamento são trabalho separado.

Também não é necessário bloquear a CPU com eventos/fences entre todos os passes da mesma lista/fila. A ordem e as barreiras adequadas controlam dependências nesse caminho; fences controlam conclusão/reutilização e dependências entre filas quando necessário. `RecordDispatch` não elimina a responsabilidade por lifetime, replay e recursos pendentes. [Microsoft: barreiras D3D12](https://learn.microsoft.com/en-us/windows/win32/direct3d12/using-resource-barriers-to-synchronize-resource-states-in-direct3d-12).

## Dados gerados e limites da referência

88 cenas procedurais 2,5D, 128×96: 64 treino, 12 validação e 12 teste. Seeds de cena disjuntos, sem seleção de checkpoint pelo teste. Arquivo `paired_scenes.npz` guarda cor, profundidade normalizada/normais no contexto, alvo, confiança, máscara conhecida e baseline analítico. IDs e hash em `dataset.json`.

As cenas contêm relevos suaves, albedo com padrões, luz regional, composição com névoa, céu e recortes finos. UI é um padrão sintético com máscara exata fornecida à composição. Normais são analíticas. Não são personagens, materiais de jogo, partículas reais ou geometria capturada. Não há demonstração de detecção automática de UI nem de qualidade das normais reconstruídas no jogo.

O alvo é uma correção escalar conhecida, baseada em normal, campo regional de iluminação e transmitância de névoa. É uma referência controlada de **aparência**, não ground truth de path tracing, iluminação indireta, sombras projetadas ou extração de materiais. A transmitância reduz a correção, mas não torna a névoa fisicamente separada da superfície. Ajustar um frame em um editor tampouco o transformaria em verdade física.

## Modelo e treinamento

Rede própria com 10.256 parâmetros. Entrada 1×8×64×64: RGB comprimido por `c/(1+c)`, três normais, profundidade/100 e máscara válida. Duas convoluções com redução espacial, mais uma convolução local e projeção produzem 1×2×8×16×16: logits de ganho/confiança. A redução para 64×64 é escolha inicial de experimento, não comprovação de que 256×256 seria caro demais ou que 64×64 basta para cenas reais.

O slicing usa luminância comprimida como guia. A composição é `RGB * exp(0.25 * tanh(gain) * sigmoid(confidence) * valid)`. Fora da máscara, mantém exatamente a entrada. Preservar proporções não garante que o resultado caiba em qualquer gamut/faixa de saída; integração HDR precisa de validação própria.

800 passos Adam, batch 4, seed 20260908, learning rate 0,002, execução determinística CPU com duas threads. Loss combina Charbonnier, erro de gradiente e supervisão da confiança. Não usamos SSIM nem loss temporal nesta fase. Treinamos uma comparação 1×1 com as mesmas entradas, alvo, batches e loss: 266 parâmetros, sem vizinhança. Ela **não é a MLP V2.3** e não permite concluir superioridade sobre a DLL atual.

## Resultado inicial em cenas reservadas

| Método | Erro absoluto médio RGB linear contra o alvo |
| --- | ---: |
| Sem correção | 0,01222379 |
| Heurística analítica aproximada | 0,01026441 |
| Rede ponto a ponto pequena | 0,00954903 |
| Grade bilateral | 0,00904909 |

A grade reduziu MAE em 25,97% frente à entrada e 5,24% frente à rede pequena. Isso mede aproximação da referência sintética, não “26% mais qualidade no jogo”. A comparação com a rede pequena confunde contexto e número de parâmetros; há controle suplementar com capacidade semelhante, registrado abaixo.

O erro de gradiente da grade foi 0,00190483, contra 0,00151884 da rede pequena: **25,41% pior** nessa métrica. A inspeção de `comparison.png` mostra regiões onde a correção ainda não acompanha o alvo. A máscara conhecida manteve céu/UI exatos; isso não mede descoberta automática de regiões protegidas.

Em 24 pares com deslocamento horizontal conhecido, avaliamos diferença do **erro reprojetado**, excluindo pixels sem correspondência e UI. Média da grade 0,00069034 e pior par 0,00146269; rede ponto a ponto zero nessa fixture translacional. A grade não é invariável ao deslocamento do enquadramento. Não houve teste de rotação, exposição, objetos móveis ou movimento do jogo. Variância bruta da luminância sem reprojeção confundiria movimento legítimo com flicker.

Os cinco critérios sintéticos inicialmente definidos passaram, mas são um filtro inicial: não incluem todos os riscos visuais, equivalência de capacidade, tempo GPU ou consumo de memória. O campo `game_integration_allowed` permanece falso.

### Controle suplementar de capacidade

Treinamos depois uma rede 1×1 com 10.271 parâmetros, praticamente o mesmo tamanho da grade (10.256), usando o mesmo alvo, loss, batches, taxa e 800 passos. Sem retreinar ou selecionar outro checkpoint da candidata. MAE 0,00933239: a vantagem da grade ficou em **3,04%**, abaixo dos 5% exigidos na comparação inicial com a rede pequena. Erro de gradiente do controle 0,00185182, ainda menor que o da grade; variação temporal zero na fixture translacional. Uma seed e um cronograma fixo não demonstram superioridade geral de arquitetura. Resultado em `capacity-control.json`.

A decisão é continuar como pesquisa isolada: há um ganho modesto de aproximação, mas não evidência suficiente para substituir a V2.3 no jogo. O controle suplementar foi motivado pela diferença de capacidade do primeiro baseline e é reportado separadamente, sem alterar os critérios ou resultados originais.

## Exportação e testes

Quatro testes passaram: slicing contra referência NumPy de oito cantos, incluindo dimensões ímpares/preto/HDR; derivada por diferenças finitas; identidade/proteção exata/limite de ganho; separação de cenas e correspondência do movimento conhecido.

ONNX FP16 da **CNN apenas**, opset 17, 22.216 bytes, operações Conv/Relu/Reshape/Constant. Empacotamento, slicing e composição não estão nesse ONNX. O checker passou. Em 12 cenas, o executor de referência ONNX CPU foi comparado com PyTorch: maior diferença nos logits FP16 0,03125; maior diferença final RGB contra o caminho FP32 0,0000667572. Tolerâncias usadas: 0,05 nos logits, 0,002 no RGB; não são medidas de qualidade artística. Não executado em ONNX Runtime/DirectML.

Hash ONNX: `b0cea90038f47bacbb3abb42686b8c87e02478132e3cf5ee143a122ec5723009`. Pesos, export-validation, curvas de treino, métricas por cena e comparativo visual em `artifacts/bilateral-regional-v1`. Logs em `artifacts/temporal-validation/bilateral-regional-v1-*.txt`.

## Orçamento de memória e próximos critérios

A grade FP16 ocupa 8.192 bytes e o contexto reduzido, 65.536 bytes. Isso não é o pico total de VRAM: faltam buffers temporários/persistentes do runtime, conversões, saídas, descritores, alinhamento e frames simultâneos.

Somente seis texturas de saída RGBA16F custariam aproximadamente 56,08 MiB em 1476×830 ou 94,92 MiB em 1920×1080. Portanto, **menos de 50 MB para a integração inteira não está demonstrado**, mesmo com uma rede pequena. Se o orçamento se refere apenas ao incremento sobre os slots já existentes, isso deve ser contabilizado separadamente. Não sacrificar a proteção de lifetime para cumprir esse número.

Antes de levar à DLL: resolver a piora em bordas, ampliar avaliação temporal e de domínio, verificar utilidade do contexto com controle de capacidade e definir como obter entradas confiáveis do jogo. Só então medir textura→tensor→rede→composição na RX 7600 com timestamps e pico real de memória. Não há promessa sub-ms ou equivalência ao DLSS 5.

## Reprodução

Com as dependências de `_IA_Python/requirements.txt` e Pillow:

```powershell
.\_IA_Python\venv\Scripts\python.exe -m unittest discover -s tests -p test_bilateral_experiment.py -v
.\_IA_Python\venv\Scripts\python.exe tools/train_bilateral_experiment.py --out artifacts/bilateral-regional-replay --steps 800
.\_IA_Python\venv\Scripts\python.exe tools/validate_bilateral_export.py --artifact artifacts/bilateral-regional-replay
.\_IA_Python\venv\Scripts\python.exe tools/train_bilateral_capacity_control.py --artifact artifacts/bilateral-regional-replay
```

O treino recusa sobrescrever diretório existente. Não executar modelos/dados externos. Os dados deste experimento são gerados localmente pelo código próprio.
