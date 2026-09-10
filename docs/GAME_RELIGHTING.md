# Integração gráfica experimental — 07/09/2026

**Versão atual instalada: [Relighting V2.3](RELIGHTING_V23_SCENERY.md), 08/09 às 18:41.** Proteção gradual de cenário distante e bordas de profundidade, ligada por padrão, após o relato de vegetação recortada contra a névoa na V2.2. Mantém preservação de cores, modelo, direção fixa e suavização. A sessão V2.2 registrou mais de 164 mil quadros, mediana amostrada de 0,201 ms do passe próprio e nenhum erro identificado. V2.3 validada em testes sintéticos na RX 7600 e WARP; avaliação visual no jogo pendente. DLL SHA256 `9b3992a1fd4337a1c235dc3401d59f8bc81614e29585f3f20546b4c9bc6d47d0`; backup anterior `TSR-backup-20260908-184145-879`. Os detalhes de V1 abaixo são históricos.

**Resultado posterior à correção:** [sessão longa com execução e efeito visual confirmados](WITCHER_RELIGHTING_FIRST_RESULT.md). Mais de 132 mil quadros registrados, mediana amostrada de 0,09048 ms para o passe próprio. O usuário gostou do efeito, com defeitos de iluminação dependente do ângulo e pele facetada. A análise distingue esses relatos das interrupções breves observadas nos logs.

**Correção após o primeiro teste de jogo:** a primeira DLL não executou nenhum quadro do passe. A inicialização de envio/Reset dependia de HUD fix, desativado na configuração usada com XeFG. A versão `witcher3-relighting-hooks-fix` inicializa esses dois hooks diretamente no backend FSR, sem ativar HUD fix ou descoberta de recursos. O teste agora exerce os hooks reais, não apenas notificações manuais; detalhes no fim deste documento.

Esta versão conecta o modelo próprio de 171 parâmetros à imagem real recebida pelo OptiScaler. O passe lê cor HDR e profundidade, reconstrói uma normal local e prevê um ganho RGB de iluminação. Sua saída temporária é entregue ao FSR; a textura original do jogo e o modelo AMD permanecem intactos. O passe ocorre antes da reconstrução, e não é RCAS/nitidez.

## Limites da experiência

O modelo `fixture_v1` aprendeu uma transformação sintética de iluminação difusa. Não recupera materiais físicos, não conhece as luzes da cena e não faz renderização neural comparável ao DLSS 5. A iluminação é relativa à câmera: pode parecer acompanhar a visão ao girar. A qualidade em jogo ainda precisa de comparação visual; a existência de inferência não prova melhoria.

A câmera é selecionada pelo candidato único de jitter no histórico validado, sem fórmula fixa de atraso. A chamada XeSS guarda em um escopo de thread as identidades exatas da lista de comandos e das texturas; o passe exige essas mesmas identidades e dimensões. Isso vincula recursos à chamada, mas **não converte o candidato de câmera em uma identidade explícita de quadro Streamline**. O log conserva `camera_match=unique_jitter_experimental`. Esta é uma opção experimental, desativada por padrão e ativada no pacote de teste autorizado para The Witcher 3.

O caminho atual aceita entrada XeSS DX12, cor linear RGBA16F/RGBA32F e profundidade R32F, R32_TYPELESS ou R32G8X24_TYPELESS (plano de profundidade R32_FLOAT_X8X24_TYPELESS). Exige texturas de uma camada/mip/amostra, dimensões iguais à renderização e origens zero. Outros caminhos, formatos, sub-regiões, resets e câmeras ausentes/ambíguas usam a imagem original. Não se promete compatibilidade universal nem suporte a qualquer resolução/placa já validado.

## Controles de teste

- Iniciar The Witcher 3 em DX12; manter XeSS Ultra Qualidade e as configurações pessoais atuais.
- **F8** alterna o passe durante a partida. O pacote começa ligado com intensidade 0,35.
- No menu do FSR dentro do OptiScaler há `TSR experimental relighting (F8)`, `TSR intensity`, estado `ACTIVE/BYPASS`, contador e tempo GPU do último passe concluído.
- F8/intensidade valem para a sessão. O início é configurado em `[TSRRelighting]`, `Enabled=1`, `StrengthPercent=35`, no INI. Para desativação persistente: `Enabled=0`.
- Comparar parado, alternando F8, olhando chão, paredes e rosto/roupas. Depois girar a câmera e observar mudanças de iluminação, cintilação ou rastros. Não usar ganho de FPS gerados como prova de menor latência.

O histórico do FSR é reiniciado ao mudar entre passe ativo/inativo ou alterar a intensidade, para evitar mistura entre as duas aparências. A troca ainda pode ter um breve período de reconstrução.

## Segurança gráfica e custo

O passe usa os estados de leitura que o backend FSR já estabelece para cor/profundidade. Não muda as entradas. Sua saída faz o ciclo leitura → UAV → leitura e segue assim após a chamada ao FSR, inclusive em replay. Há seis slots, com descritores imutáveis por gravação, referências COM para entradas, saída, programa/pesos e fences por fila real. A notificação ocorre depois do `ExecuteCommandLists` do jogo. Reset bem-sucedido/destruição encerra a possibilidade de replay; isso sozinho não libera recursos que a GPU ainda usa.

O passe exige confirmação de instalação dos hooks de envio e Reset. Se não há slot livre, registra o motivo e usa a imagem original sem espera de CPU. Slots ocupados na destruição do backend são retidos até encerrarem a gravação e todos os fences; falha de sinalização impede reutilização. A lista do jogo nunca é fechada, reiniciada ou enviada pelo passe. Alocação/compilação ocorre na inicialização ou mudanças de tamanho, não a cada quadro após aquecimento.

Dois timestamps medem o passe próprio e suas barreiras; os resultados só são lidos quando o slot está livre e a GPU terminou. `gpu_ms` não inclui o FSR, frame generation, o quadro inteiro ou latência de entrada. Ainda não há medição desta versão dentro do jogo no momento da preparação do pacote.

## Validação reproduzível

`tsr_game_relighting_dx12` usa a mesma classe de produção e os mesmos pesos/shader embutidos. Compara saída GPU com referência CPU em dimensões ímpares, FP16/FP32, profundidade simples e depth/stencil nativa, céu, descontinuidades, alpha e entradas inválidas. Repete a lista realmente na GPU, rejeita gravação duplicada e prende seis envios atrás de um fence para verificar que destruir listas não libera buffers antes da conclusão. A camada de diagnóstico fica ativa. Só é filtrado o aviso de otimização do clear: o teste deliberadamente limpa pixels de profundidade com valores diferentes.

O teste passou em WARP e RX 7600. O maior erro absoluto na comparação FP16 foi 0,000488222; FP32 na RX 7600 foi 0,00000208616. Esses números medem concordância de implementação, não qualidade artística ou fidelidade ao jogo. Evidências ficam em `artifacts/temporal-validation/game-relighting-*.log`.

`tools/embed_game_relighting.py` gera o header a partir do HLSL compartilhado e dos pesos do experimento. A DLL não carrega shaders/pesos de arquivos externos em execução. `tools/analyze_camera_bridge.py` distingue amostras de inferência ativa, razões de bypass, contador de gravações e tempo esparso do passe; não infere qualidade visual a partir dos logs.

## Instalação concluída

Pacote `artifacts/game-test/witcher3-graphical-relighting`, instalado em 07/09/2026 às 10:05. DLL SHA256 `9D490DD1B33A2616576D5807C652389B9EB7400DBC5090626EC80C71DF60CC78`; INI SHA256 `7B6E13A63F919580C55D80DF587253FE2B9DEDB353EEB3BFEFA26E106078FE21`. Os bytes do INI anterior foram preservados, com apenas a seção TSR acrescentada. Backup verificado: `C:\Program Files (x86)\Steam\steamapps\common\The Witcher 3\bin\x64_dx12\TSR-backup-20260907-100538-618`.

Compilação Release concluída (avisos preexistentes de herança/CRT e cópias de SDK). Passaram os testes do runtime em RX 7600 e WARP, incluindo retenção de seis envios; os cinco testes de câmera/histórico/observer/destruição/textura; e a regressão do shader anterior em 321×181. Esta etapa não repetiu a suíte completa de 40 testes registrados. A DLL instalada ainda aguarda a primeira confirmação visual e log do usuário no jogo.

## Falha identificada no jogo e correção dos hooks

Captura `witcher-relighting-blocked-game.log`, SHA256 `beffa14fc80d02057fa079e4c268c4e0b62ce98918c41c7d1343dd164e201f7b`. Foram 59 amostras do passe: zero ativas, zero quadros gravados, 43 bloqueadas por `nonlinear_color_or_hooks_unavailable`, nove desativadas pelo controle e sete sem câmera/recursos correspondentes. A intensidade variou de 0,35 a 1 e houve alternância ligado/desligado: o controle chegava ao backend, porém nada era despachado. Não se deve atribuir a ausência de efeito a uma mudança sutil de iluminação.

`D3D12Hooks::HookToDevice` só instalava os hooks do ResTrack quando `FGDisableHUDFix` era falso. O INI do teste tem `DisableHUDFix=true`; as três opções de cor não linear estavam em `auto`, cujo padrão é falso. `FFXFeatureDx12::InitInternal` agora chama `EnsureTsrSubmissionHooks` diretamente. Esse caminho instala somente ExecuteCommandLists e Reset, com a mesma rotina de instalação atômica testada isoladamente. Inicialização repetida é idempotente. Desativar os hooks de HUD não remove os dois hooks de ciclo de vida; eles são retirados junto aos hooks do dispositivo.

O menu mostra um motivo legível de bypass e o log separa `submission_hooks_unavailable` de `nonlinear_color`. F8 também registra a mudança de controle. A flag de pronto só muda após commit bem-sucedido.

Validação adicional `tsr_game_relighting_hooks_dx12`: **20 callbacks reais de Reset e 19 de envio, zero notificações manuais**, em WARP e RX 7600. O mesmo teste comparou pixels GPU/CPU, repetiu comandos, reteve seis envios antes do fence e verificou instalação repetida e remoção dos hooks. Os resultados e erros máximos são os mesmos da implementação gráfica anterior. Esse executável não usa HUD fix ou frame generation. Logs: `relighting-hooks-warp.log` e `relighting-hooks-rx7600.log`. O alvo depende da biblioteca Detours local do OptiScaler; é registrado pelo CMake quando essa biblioteca está disponível, não pelo baseline CI que não a distribui.

A correção não muda o shader/modelo, nem requer ativar HUD fix ou alterar XeSS/FSR/XeFG. Ainda é necessário confirmar `active=true`, contador crescente e imagem no jogo após instalar a DLL corrigida.

Correção instalada em 07/09/2026 às 15:03. Pacote `artifacts/game-test/witcher3-relighting-hooks-fix`, DLL SHA256 `5A9899A4496620B566DD9229C64B7665F294893232DBC0B5FE812139AE539996`. INI pessoal preservado integralmente, SHA256 `6C4AF8272437A9B6DAA88BA199E56B4B4032456DD0F3F32A673EE2ABD97DF348`; inclui os ajustes salvos pelo usuário durante o teste. Backup verificado `C:\Program Files (x86)\Steam\steamapps\common\The Witcher 3\bin\x64_dx12\TSR-backup-20260907-150341-196`. Compilação Release terminou com sucesso. Os cinco testes direcionados de câmera, histórico, observer, hooks reais e destruição passaram. A confirmação em jogo desta correção permanece pendente.
