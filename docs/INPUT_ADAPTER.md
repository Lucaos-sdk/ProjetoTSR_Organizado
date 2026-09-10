# Adaptador de entrada GPU — primeira etapa

Implementado `InputAdapterPass::Record`, que grava uma conversão na command list fornecida. Não submete fila, espera fence ou lê imagens na CPU. O chamador fornece três SRVs (cor, profundidade, movimento) e dois UAVs RGBA32F (cor convertida e geometria empacotada).

## Contrato explícito

- Cor linear RGB multiplicada por escala positiva; alpha preservado. Não há conversão sRGB/PQ, tonemapping ou autoexposição.
- Movimento convertido por eixo para pixels da resolução de renderização; uma correção de jitter explícita é subtraída depois da escala. Direção/sinal devem ser configurados para atual → anterior. Não são inferidos do jogo.
- Profundidade linear positiva ou perspectiva `d = A + B/Z`, invertida na GPU como `Z = B/(d-A)`. A/B são fornecidos pelo chamador, não estimados. Isso permite profundidade normal e invertida, mas não cobre qualquer projeção arbitrária.
- Todas as entradas usam a mesma resolução e origem. Sub-retângulos, movimento na resolução de saída, MSAA e conversões de formatos compactados continuam pendentes.
- Geometria inválida (NaN/infinito, profundidade não positiva ou fora de [0,1] no modo projetado, movimento não finito) recebe zero. Os shaders temporais agora também rejeitam profundidade histórica não positiva. Cor não finita ainda é responsabilidade do chamador.
- Sem profundidade anterior prevista, o componente que habilita reprojeção fica zero por padrão. Somente o modo de câmera comprovadamente fixa usa Z atual como Z anterior. Não assumir câmera fixa em um jogo em movimento. A captura/conversão da profundidade anterior prevista permanece pendente.

A referência CPU independente usa double e é confrontada com valores analíticos conhecidos. O shader usa float. `ValidateConversion` rejeita dimensões e parâmetros inválidos antes de gravar comandos. O chamador é responsável por formatos compatíveis, dimensões reais, estados e vida útil dos recursos/descritores.

## Validação realizada

- Cor Texture2D RGBA32F, profundidade R32F, movimento RG32F e saídas RGBA32F.
- Seis cenários: profundidade linear, projetada normal e invertida, cada uma com câmera fixa habilitada/desabilitada.
- Teste 17×9 cobre bordas de dispatch e row pitch não trivial; teste 1920×1080 cobre o tamanho de uso do projeto.
- Movimento com escala diferente por eixo, inversão de sinal e correção de jitter; cor HDR/negativa e alpha preservado.
- Amostras de profundidade/movimento inválidos verificadas.
- RX 7600 com debug layer passou nos seis cenários, inclusive em 1080p. WARP também passou com debug layer.
- Suíte completa: 21/21 testes CTest passaram; após o ajuste do tamanho configurável, o novo executável foi novamente validado em WARP e RX 7600. Regressões temporais completas passaram na RX 7600 com debug, até saída 4K.

Executar: `tsr_input_adapter_dx12.exe --debug --1080p`. Logs em `artifacts/temporal-validation/input-adapter-*` e `ctest-input-adapter.log`.

## O que falta para o jogo

O adaptador também foi validado em uma [cadeia GPU de seis quadros](GPU_INPUT_CHAIN.md), sem readback intermediário. Ainda não está conectado ao OptiScaler. Próximo passo: adicionar os formatos efetivamente recebidos pelo jogo (por exemplo, FP16), sub-retângulos e saída no formato real. Depois implementar o backend por contexto e seu ciclo de vida com quadros em voo.

Não há nova DLL de jogo ou inferência neural nesta etapa. A conversão não é modificação do FSR 4.1 e não demonstra equivalência visual com DLSS 5.
