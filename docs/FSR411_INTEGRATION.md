# Integração FSR 4.1.1 — teste no Witcher 3

## Primeiro teste em jogo confirmado

Sessão de 06/09/2026, 19:46–19:50: o log confirma carregamento do binário AMD do pacote, seleção do provedor 4.1.1 com a extensão de API e 26 amostras de despacho com retorno OK, até o quadro amostrado 13800. Todas informam entrada 1280×720, saída 1920×1080, autoexposição, pré-exposição padrão 1 e profundidade DXGI 19. Não há registros `[E]` nem fallback; há encerramento `DLL_PROCESS_DETACH`.

Os 24 avisos incluem banners, bibliotecas opcionais ausentes, recursos DLSS/FG indisponíveis e um aviso de entrada do menu. Não são 24 falhas de renderização. O teste confirma integração funcional pelos retornos registrados, sem medir qualidade visual, tempo GPU por etapa ou identificar independentemente o modelo interno do driver. O contador de quadros não deve ser convertido em FPS deste teste.

Evidência preservada em `artifacts/temporal-validation/witcher-fsr411-first-game.log` e resumo `.json`; SHA256 do log: `2b2705c414faa30145211c003b97e00111dc5db9d115e0309c7dc1ab896cb644`. Nenhuma configuração foi alterada ao analisar esta sessão.

## Mudança na DLL

O backend FFX agora encadeia o descritor de versão da API de upscaling para provedores 4.1.1 ou posteriores. A extensão segue o contrato público do SDK 2.1, sem substituir os cabeçalhos legados usados por frame generation. A compatibilidade de tamanho, offset e valores do descritor é conferida contra o SDK atual no teste nativo.

A enumeração DX12 recusa consultas malsucedidas, listas vazias e contagens inválidas antes de escolher um provedor. Os ponteiros temporários do descritor são removidos após criar o contexto. Buffers auxiliares e ponteiros de recursos da avaliação foram inicializados com nulo. O log registra a versão selecionada e amostras limitadas dos despachos aceitos, com dimensões, exposição e formato da profundidade.

O algoritmo e os pesos do FSR 4.1.1 são da AMD. Esta é uma alteração real de integração em nossa DLL, mas não constitui melhoria dos pesos, iluminação neural própria ou equivalência ao DLSS 5. Também não comprova melhora visual em relação ao FSR 4.1.1 original.

## Validação local

- DLL Release compilada. Permanecem avisos do fork relativos a herança, CRT e empacotamento de dependências; o pacote local seleciona os arquivos necessários explicitamente.
- 35/35 testes de regressão nativos passaram.
- RX 7600: o SDK anunciou provedores 4.1.1, 3.1.5 e 2.3.4. Foi escolhido explicitamente o ID de 4.1.1 no teste.
- Oito despachos com 1280×720 → 1920×1080 passaram com cabeçalhos atuais; outros oito passaram com os cabeçalhos legados usados pelo fork e a extensão de versão.
- Cada leitura de saída esperou a fence e conferiu RGB finito e não vazio. Esses critérios verificam funcionamento básico, não qualidade visual nem equivalência pixel a pixel com um modelo de referência.
- A assinatura Authenticode do binário de upscaling foi validada como Advanced Micro Devices. SHA256: `D0DCCCC74A43C44BA435B7A369B456E0970D8A4464E4BD683119B374F2C9FB46`.

Logs: `artifacts/temporal-validation/fsr-sdk-rx7600.log`, `fsr-legacy-rx7600.log`, `build-dll-fsr411.log` e `ctest-fsr411.log`. Não houve medição de desempenho dessa integração em jogo. A seleção de um provedor não é prova independente do modelo interno executado pelo driver.

## Pacote de jogo

`artifacts/game-test/witcher3-fsr411` contém a DLL compilada, configuração, binário assinado da AMD, licença e hashes. O instalador copia apenas `dxgi.dll` e `OptiScaler.ini` para o jogo e preserva os originais. O caminho de biblioteca na configuração aponta para o binário AMD nesta pasta do projeto: **não mover nem apagar o pacote durante o teste**.

O jogo permanece em DX12, XeSS Qualidade e 1080p. A entrada XeSS será processada pelo backend FFX, com provedor de índice 0, correspondente a 4.1.1 na enumeração local. Nitidez extra e geração de quadros ficam desativadas para avaliar a reconstrução. Não foi forçado modelo de outra arquitetura.

Depois de carregar um save, o menu deve identificar FSR 4.1.1. O arquivo `TSR_FSR411.log` permitirá confirmar `TSR FFX: selected provider=4.1.1` e `TSR FFX dispatch: provider=4.1.1 ... result=OK`. Ausência dessas linhas ou fallback para FSR 2/3 significa que o teste de FSR 4.1.1 ainda não foi confirmado no jogo.

O adaptador próprio de reconstrução temporal permanece separado. O backend FFX usa as convenções de compatibilidade do OptiScaler, incluindo autoexposição e valores padrão de câmera/pré-exposição quando ausentes. Esses padrões não foram convertidos em supostos dados medidos do jogo.

## Fontes e direção

- [FSR 4.1.1 e contrato da API](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/blob/main/Kits/FidelityFX/docs/techniques/super-resolution-ml.md): exige binário assinado e informa referência de 1,998 ms na RX 7600, saída 1080p, modo Performance, sem RCAS e em clocks máximos. Isso não é nosso benchmark nem previsão para XeSS Qualidade no Witcher 3.
- [Radiance Cache](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/blob/main/Kits/FidelityFX/docs/techniques/radiance-cache.md): precisa de dados de interseções, materiais e amostras de radiância produzidas pelo path tracer. A entrada de upscaling atual não fornece esse conjunto; não basta carregar outra DLL.
- [Frame interpolation ML](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/blob/main/Kits/FidelityFX/docs/techniques/frame-interpolation-ml.md): geração de quadros é uma etapa distinta, com seus próprios dados de câmera e requisitos de hardware.
- Os links de configuração, wiki, compatibilidade, OptiFG, releases e a issue Decky-Framegen #197 foram consultados como contexto. A decisão de integração se baseia no contrato público AMD e na execução local; sugestões comunitárias de forçar modelos não substituem essa validação.

Próxima avaliação: confirmar o provedor no jogo, comparar a reconstrução em movimento e medir seu custo. Só então avaliar uma etapa própria adicional contra essa base neural, com ganhos e limitações demonstráveis.
