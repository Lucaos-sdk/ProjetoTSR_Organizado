# Contrato de quadros do harness — v2

Implementado em `native/baseline/frame_contract.h`. Este contrato descreve entradas canônicas do experimento; não afirma que os buffers NGX/The Witcher 3 já tenham estas convenções. O adaptador do jogo terá de convertê-los explicitamente. Não define os 16 canais nem os 12 coeficientes da rede.

## Coordenadas e planos

Origem no canto superior esquerdo, X para a direita e Y para baixo. Planos CPU contíguos, row-major, todos na resolução `renderSize`; `outputSize` é independente. Dimensões entre 1 e 16384. Sem MSAA, recortes, arrays ou mipmaps nesta revisão.

| Entrada | Representação canônica | Significado |
|---|---|---|
| color | float32 RGBA; textura DX12 R32G32B32A32_FLOAT | RGB linear de cena multiplicado por `preExposure`, antes de tonemapping; alpha independente, interpolado sem premultiplicar. HDR e negativos são permitidos; todos os componentes devem ser finitos |
| depth | float32 por pixel | Z positivo no espaço da câmera, em metros, finito; não é depth de hardware nem distância radial. Reversed-Z deve ser convertido antes. Céu exige uma convenção finita definida pelo futuro adaptador |
| motion | float32 XY por pixel | Deslocamento do quadro atual para o anterior, em pixels da resolução de renderização, com Y positivo para baixo; exclui jitter. Valores finitos, inclusive negativos |
| jitterPixels | float32 XY | Deslocamento da amostra em relação ao centro do pixel, em pixels de renderização, cada eixo em [-0,5; 0,5]. A amostra atual representa `(x+0,5+jx, y+0,5+jy)` |
| preExposure | float32 positivo e finito | `storedRGB = sceneRGB * preExposure`; não altera alpha |
| frameIndex | uint64 | Índice da sequência por instância; zero exige reset |
| reset | bool | Primeiro quadro, corte, descontinuidade ou mudança de resolução devem invalidar todo o histórico da instância |
| depthReprojection | enum explícito | `FixedCamera` usa Z atual na comparação; `PredictedPreviousZ` exige o plano adicional abaixo |
| predictedPreviousDepth | float32 por pixel atual | Z da superfície atual expresso na câmera anterior, incluindo movimento da superfície quando necessário. Não é a textura de depth anterior amostrada. Zero invalida a projeção anterior; demais valores devem ser positivos e finitos |

Todos os planos são obrigatórios em `FrameInput`, inclusive no teste espacial, e são verificados antes das alocações GPU. A fixture fornece depth=10 m, movimento zero, jitter zero, exposição 1 e reset=true. Essa validação verifica estrutura/faixas; não comprova que movimento ou profundidade estejam fisicamente corretos.

O plano `predictedPreviousDepth` é obrigatório somente no modo `PredictedPreviousZ`; no modo `FixedCamera` deve estar vazio, evitando que seja ignorado silenciosamente. O novo contrato estende os planos do harness, sem alterar o contrato ONNX. As fixtures de câmera calculam projeção e profundidade na CPU; o adaptador do jogo ainda terá de produzi-las corretamente.

## Consumo implementado nesta etapa

`--textures` transfere somente color para uma Texture2D SRV e grava outra Texture2D por UAV. Usa quatro leituras `Load` com clamp explícito e interpolação bilinear FP32. Upload e readback usam `GetCopyableFootprints`, com cópia linha a linha pelo RowPitch, descritores mantidos vivos até a fence e transições explícitas. Não usa endereço virtual como descritor de textura.

O teste espacial preserva a escala de exposição recebida e reconstrói a grade fornecida. Não remove jitter, não lê depth/motion no shader e não acumula histórico. O executável separado [tsr_temporal_dx12](TEMPORAL_BASELINE.md) agora consome esses campos na GPU para reprojeção e histórico por instância sob hipótese de câmera fixa. Não há entrada de recursos pertencentes ao jogo nem integração OptiScaler nesta etapa.

## Regras temporais

- Cada instância mantém resolução, índice, jitter e exposição anteriores junto dos dois históricos. Reset impede leitura do histórico velho; alteração de resolução ou índice descontínuo também invalida. `ValidateFrame` verifica um quadro isolado; o contexto temporal verifica a continuidade entre chamadas.
- Para resoluções iguais, o endereço anterior na grade jitterizada, em coordenadas de índice de pixel, será `currentIndex + motion + currentJitter - previousJitter`. O sinal e as unidades devem ser testados com translações conhecidas antes de usar dados do jogo.
- Antes de misturar RGB armazenado, ajustar o histórico por `currentPreExposure / previousPreExposure`. Alpha não recebe essa razão.
- Não interpolar profundidade/movimento por bilinear através de silhuetas indiscriminadamente. A política de seleção e a máscara de validade devem ter referência CPU e testes de borda.
- Comparar Z atual e Z anterior diretamente não cobre movimento da câmera. O modo `PredictedPreviousZ` compara o histórico com a profundidade anterior prevista da superfície atual, aceitando translação de câmera nas fixtures. Rotação, deformação e obtenção dos dados no jogo continuam pendentes.

## Evidência local

Windows 10.0.26200, MSVC 19.51, AMD Radeon RX 7600, execução em hardware. `--textures --debug --full` passou nos nove casos, incluindo 1920×1080 → 3840×2160, com erro absoluto máximo de 3,8147e-06 no caso 4K. O caminho por buffers também passou com debug. CTest: seis testes aprovados (referência, contrato e dois modos WARP com/sem 4K).

Logs locais em `artifacts/tsr-dx12-baseline-windows-33932564108/`: `rx7600-textures-debug-full.log`, `rx7600-buffers-regression.log` e `ctest-textures.log`. Estes resultados pertencem à árvore local modificada, não ao executável original do artefato do GitHub. A CI remota desta alteração ainda não foi executada.

Os timestamps medem um dispatch isolado, sem estatística, transferências ou jogo. Não comprovam a meta de 3 ms do pipeline completo.

Referência de layout: [Microsoft — GetCopyableFootprints](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-getcopyablefootprints).
