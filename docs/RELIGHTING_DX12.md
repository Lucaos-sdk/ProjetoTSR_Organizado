# Iluminação própria executada na RX 7600

Em 07/09/2026, o modelo treinado de 171 parâmetros passou a executar em um passe de cálculo DirectX 12. A entrada é cor linear e profundidade Z linear positiva, com projeção pinhole conhecida; as normais são estimadas no shader. Este protótipo é independente e ainda não foi ligado à DLL do jogo. Os pesos originais do FSR permanecem intactos.

## Resultado medido

RX 7600, saída 1920×1080, RGBA32F, cinco repetições de aquecimento e 50 amostras:

| Medida | Resultado |
| --- | ---: |
| Mediana GPU do passe | 0,26204 ms |
| Percentil 95 GPU | 0,26340 ms |
| Maior diferença absoluta GPU/CPU | 0,00002745 |
| Erro RGB quadrático da imagem original contra alvo | 0,00820843 |
| Erro RGB quadrático da saída GPU contra alvo | 0,00010370 |
| Erro do modelo com normais exatas contra alvo | 0,00006306 |

O intervalo medido por timestamps inclui reconstrução das normais e inferência, excluindo upload, readback, espera CPU, FSR, frame generation e jogo. É a mesma cena reproduzida, com cópia de saída entre repetições e sem concorrência do jogo. Não é uma previsão de latência em jogo. A medição principal não usa a camada de debug.

Esses erros medem aproximação de uma iluminação-alvo definida por nós, não qualidade perceptiva ou superioridade a FSR/DLSS. A função-alvo é difusa e analítica, suficientemente simples para também ser calculada sem rede. A contribuição deste marco é levar pesos treinados ao runtime GPU e validar o caminho profundidade → orientação → ganho de iluminação com dados conhecidos.

## Imagens e validação

Comparação: `artifacts/relighting-depth-v1/comparison.png`. Sequência: `artifacts/relighting-depth-v1/motion-comparison.gif`. As imagens são renderizações sintéticas próprias, não capturas de jogo. As colunas usam a mesma transferência de cor; não há ajuste automático de contraste. A animação exibe saída GPU, com cinco quadros de movimento horizontal e retorno para facilitar inspeção. A paleta GIF é apenas para apresentação; as métricas usam floats antes da quantização.

O gerador de avaliação usa interseções analíticas de raios com esferas e plano, diferente dos relevos gaussianos do treino. Cor e profundidade compartilham grade e projeção. A mudança de aparência resulta de direção/cor de iluminação fixa; não há sombras projetadas, reflexos ou materiais aprendidos.

Validações executadas:

- Dez casos na RX 7600: 640×360, 1920×1080, 321×181, bypass e profundidade inválida em 65×37, e cinco quadros em 320×180. Os nove casos pequenos usaram a camada de debug DirectX, sem avisos/erros. Todas as saídas passaram GPU/CPU, finitude e preservação exata do alfa.
- Bypass de intensidade zero conserva todos os canais exatamente; pixels sem suporte geométrico preservam a entrada. Profundidade NaN, infinita, zero e negativa não contamina a saída.
- Caso 321×181 também passou em WARP com debug, diferença máxima GPU/CPU de 0,0000004173. Não comprova suporte a todas as GPUs.
- A reconstrução de um plano inclinado em perspectiva foi comparada a uma normal analítica independente e passou com erro absoluto por componente abaixo de 0,00002.
- Nos cinco quadros em movimento, o erro da variação entre quadros contra a variação do alvo ficou aproximadamente entre 0,000245 e 0,000257, contra 0,000506–0,000541 na entrada. Esta medida em espaço de tela inclui desoclusões; não é uma métrica perceptiva de flicker nem prova de estabilidade temporal geral. O passe não tem histórico temporal.

A inspeção inicial encontrou contornos artificiais ao preservar integralmente pixels junto a descontinuidades. O shader final usa diferença central quando há suporte dos dois lados e unilateral quando só um lado pertence à mesma superfície. Nunca calcula a derivada atravessando um salto de profundidade acima do limite relativo. Quando falta suporte em um dos eixos, mantém a imagem original. Isso reduz o contorno, mas não garante ausência de artefatos em geometria real, transparências ou vegetação.

## Código e reprodução

- `native/baseline/Relighting.hlsl`: estimativa de normais e inferência dos pesos.
- `native/baseline/relighting_pass.h`: gravação do passe, sem alocações, envios de fila ou esperas por quadro. O chamador controla estados, descritores e vida útil dos recursos.
- `native/baseline/relighting_dx12.cpp`: harness com upload, execução, fences, timestamps e leitura de saída.
- `tools/relighting_depth_experiment.py`: gera fixtures, referência NumPy, métricas e comparações visuais.

Com Python contendo NumPy/Pillow e build CMake já configurado:

```powershell
python tools/relighting_depth_experiment.py prepare
cmake --build build/native-rx7600 --config Release --target tsr_relighting_dx12
foreach ($caseName in @('preview','odd','bypass','invalid-depth','motion-0','motion-1','motion-2','motion-3','motion-4')) {
    ./build/native-rx7600/Release/tsr_relighting_dx12.exe --fixture "artifacts/relighting-depth-v1/$caseName" --debug --samples 3
    if ($LASTEXITCODE -ne 0) { throw "Falha em $caseName" }
}
./build/native-rx7600/Release/tsr_relighting_dx12.exe --fixture artifacts/relighting-depth-v1/1080p --samples 50
if ($LASTEXITCODE -ne 0) { throw 'Falha em 1080p' }
./build/native-rx7600/Release/tsr_relighting_dx12.exe --fixture artifacts/relighting-depth-v1/odd --warp --debug --samples 3
if ($LASTEXITCODE -ne 0) { throw 'Falha em WARP' }
python tools/relighting_depth_experiment.py evaluate
```

O prepare requer os pesos já treinados em `artifacts/relighting-fixture-v1/weights.npz`, reproduzíveis por `tools/train_relighting_fixture.py`. Resultados e hashes estão em `artifacts/relighting-depth-v1/results.json` e `provenance.json`; os logs ficam em `artifacts/temporal-validation/relighting-*.log`.

## Próximo marco em jogo

Obter e verificar projeção da câmera, conversão de profundidade e cor, e identificar onde há imagem sem interface para consumir os recursos reais em um passe desligável. Defaults de câmera do upscaler não demonstram a projeção real do jogo. A iluminação deste modelo é fixa no espaço da câmera; sem orientação de câmera e um modelo mais abrangente, a luz não permaneceria estável no mundo durante rotações. Essa limitação também precisa ser resolvida antes de tratar o efeito como melhoria de iluminação em jogo.

Esta etapa não instala efeito em The Witcher, não solicita novo teste manual e não altera a configuração existente. Não há ainda treinamento com imagens de jogos, iluminação dinâmica, sombras, HDR/exposição variável ou integração temporal validada.
