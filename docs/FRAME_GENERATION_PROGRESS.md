# Geração de quadros — RX 7600

## Primeiro teste em jogo: geração confirmada, erro pendente

Sessão de 06/09/2026, aproximadamente 20:32–20:42. Das 60 amostras de estado do present anterior, 55 informam consulta bem-sucedida, geração habilitada, dois quadros enviados e interpolação bem-sucedida. Quatro informam geração desabilitada/um quadro; uma informa histórico insuficiente no início. O log mantém FSR 4.1.1 e registra encerramento normal.

**A sessão não está livre de falhas:** há 3.282 ocorrências de `Shader_Dx12::CreateBufferResource CreateCommittedResource result: FFFFFFFF80070057`, entre 20:34:12.614 e 20:34:56.889. O código corresponde a argumento inválido. O helper não registra nome do passe nem descrição da textura; portanto, o log atual não determina qual combinação de formato, dimensões, flags ou estado causou a falha. O intervalo coincide com o menu aberto e alterações de configuração, sem demonstrar causalidade. Não atribuir o problema à interpolação ou à profundidade sem reprodução/diagnóstico adicional.

A entrada de reconstrução aparece em 1280×720 e depois 1476×830, com saída 1920×1080. Há 90 avisos, incluindo transições de geração e entrada do menu. Essas alterações tornam inadequada uma comparação direta de desempenho com a configuração anterior. Não foram medidas latência, duração GPU ou apresentação física.

Evidência: `artifacts/temporal-validation/witcher-framegen-first-game.log` e resumo `.json`; SHA256 `7bc652717ac168254ca5a47492297564abbba6bb297a81a44af994a520985eb2`. O log e as configurações do jogo foram apenas lidos nesta análise; nenhuma alteração foi aplicada. Próxima correção deve identificar/reproduzir o descritor inválido antes de modificar o helper compartilhado.

## Resultado da investigação em 06/09/2026

O [RTX40MFG-Unlock](https://github.com/dashdogy/RTX40MFG-Unlock) exige RTX 40 e uma integração Streamline DLSS FG funcional. É uma referência para verificação de capacidade e seleção de multiplicadores, não um gerador portável para AMD. O [guia NVIDIA](https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuideDLSS_G.md) documenta o contrato de integração DLSS-G; esse contrato não implementa interpolação para outra arquitetura.

No runtime Intel já presente no projeto, a consulta real `xefgSwapChainD3D12CreateContext` seguida de `xefgSwapChainGetProperties` na RX 7600 retornou sucesso e **maxSupportedInterpolations=1**. Isso significa um quadro gerado mais um real: **2×**. Não oferece MFG 3×/4× nesse dispositivo/runtime. Evidência: `artifacts/temporal-validation/framegen-capabilities-rx7600.log`. O teste consulta capacidade; não valida apresentação ou qualidade de interpolação.

O [SDK Intel](https://github.com/intel/xess) oferece FG para GPUs não Intel compatíveis, enquanto seus [releases](https://github.com/intel/xess/releases) distinguem o suporte MFG em Arc. O [OptiFG](https://github.com/optiscaler/OptiScaler/wiki/OptiFG) fornece um caminho experimental via entrada do upscaler. O limite medido localmente orientou o pacote de teste; não foram forçadas identidades de outra GPU nem multiplicadores não suportados.

## Mudanças em código na DLL

- Corrigida a leitura de `XeFGPath` e `XeLLPath`: ambas escreviam em `XeSSLibrary`, sobrescrevendo o caminho do upscaler. Agora cada componente recebe seu próprio caminho.
- A aplicação da quantidade de quadros XeFG valida capacidade e só atualiza a quantidade ativa após sucesso da API. A falha preserva o último valor aceito e interrompe a geração para recuperação. A normalização também trata valores abaixo de 1.
- Corrigida a inicialização que sobrescrevia o limite de um quadro quando `ForceXeLL` estava ativo.
- O setter do backend FSR recusa mais de um quadro gerado; ele anteriormente retornava sucesso para qualquer quantidade sem implementá-la.
- Os seletores MFG limitam seus índices ao tamanho real das listas, evitando acesso fora dos limites.
- Acrescentadas amostras limitadas de `GetLastPresentStatus`: `TSR FG previous-present`, com retorno da consulta, geração habilitada, quadros enviados e resultado da interpolação. São dados do present anterior informado pelo SDK, não confirmação de exibição física nem medição de latência. A consulta pode estar indisponível no início.

Essas mudanças melhoram integração, recuperação e observabilidade. O gerador continua sendo da Intel; não há algoritmo MFG próprio ou transformação neural própria de iluminação/material.

## Validação

DLL Release compilada; permanecem os avisos anteriores de herança, CRT e cópia de dependências do fork. **36/36 testes CTest passaram**, incluindo política de quantidade: zero, capacidade ausente, quantidade excedida, valor máximo uint32, pedido válido 4× em capacidade simulada compatível e falha que não altera o valor ativo. O teste simulado 4× não demonstra suporte 4× na RX 7600.

O pacote verifica hashes e assinaturas Intel de `libxess_fg.dll` e `libxell.dll`, e inclui as licenças. Logs: `build-dll-framegen.log`, `ctest-framegen.log`, `framegen-capabilities-rx7600.log`.

## Primeiro pacote em jogo

Pasta: `artifacts/game-test/witcher3-framegen-2x`. A DLL instalada mantém a reconstrução FSR 4.1.1 já testada e habilita entrada OptiFG/upscaler + saída XeFG, com um quadro gerado. XeLL acompanha o runtime; sua eficácia na latência do jogo ainda precisa ser medida.

A entrada do jogo permanece XeSS Qualidade em DX12, 1920×1080. Nitidez extra permanece desligada. OptiFG HUDFix fica desligado neste primeiro teste; a interpolação da interface usa o comportamento padrão recomendado pelo SDK Intel. Se minimapa, texto ou marcadores apresentarem distorções, isso é uma limitação a corrigir, não evidência de qualidade aprovada. Auto detecção de HUD e composição poderão ser comparadas depois, conforme o resultado.

O runtime de FG é carregado desta pasta do projeto; o FSR ainda usa a pasta `witcher3-fsr411`. Não mover essas pastas durante o teste. DLL e configuração anteriores foram salvas pelo instalador. O registro `installation.json` e o atalho de restauração apontam para o backup específico.

O próximo teste precisa confirmar no `TSR_FrameGen.log` criação/ativação XeFG e amostras bem-sucedidas com `frames_sent=2`, além da inspeção visual de câmera, HUD e fluidez. Retornos de API não comprovam espaçamento físico dos quadros. Não há ainda ganho FPS, custo GPU ou latência quantificados neste caminho. Os cerca de 10 ms observados pelo usuário eram o tempo de quadro completo, não a medição isolada do FSR.

## O que falta para multigeração própria

Atualização de 07/09: a [sessão com a correção de texturas](TEXTURE_ALLOCATION_FIX.md) terminou sem erros de alocação. Houve 30 amostras de interpolação 2× bem-sucedida, 32 desativadas e uma inicial sem histórico suficiente. Houve reativações e avisos de contador; portanto não se afirma geração contínua durante toda a sessão. O limite de amostragem também impede observar o status de present até o encerramento. A reconstrução permaneceu FSR 4.1.1 em 1476×830 → 1920×1080 nas amostras registradas.

Gerar 3×/4× exige imagens distintas em instantes intermediários diferentes, tratamento de oclusões e HUD, capacidade de buffers e sincronização, e apresentação com espaçamento correto. Repetir o mesmo quadro interpolado ou mudar um número no menu não atende a esse objetivo. Esta instalação oferece apenas o teste 2× descrito acima.
