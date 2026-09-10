# Ponte de câmera Streamline → entrada XeSS

Atualização de 07/09/2026, 10:05: a [integração gráfica experimental](GAME_RELIGHTING.md) foi compilada, testada isoladamente e instalada com F8 para comparação. O histórico abaixo descreve as versões anteriores sem execução do passe. A associação de câmera por jitter continua experimental; agora os recursos são vinculados ao escopo exato da chamada XeSS. A versão nova registra a execução em `TSR graphical relighting` e o resultado no `TSR FFX dispatch`. Linhas antigas de auditoria de câmera com `own_neural=false` descrevem a etapa de coleta anterior ao passe e não devem ser usadas para concluir seu estado nesta versão.

## Implementação

A DLL agora observa os dados originais de `slSetConstants` do Streamline 1 e 2 e inspeciona um candidato de câmera na entrada real `xessD3D12Execute`. A captura acontece independentemente do backend de geração de quadros selecionado, antes de qualquer ajuste de câmera feito pela integração FG existente.

São copiadas matrizes de projeção e inversa, base de orientação, jitter, identificadores de quadro/viewport e flags de profundidade. A captura não retém ponteiros para a estrutura do jogo. Um mutex protege o snapshot; o candidato mais recente não é tratado como associação automática a uma imagem. As consultas XeSS registram dimensões, formatos, origens das texturas, flags, escala de exposição e jitter. Identificadores de recursos e tags SL1 também são registrados para investigar a correspondência. São metadados locais; não há captura de pixels, envio externo ou alteração dos recursos.

O contrato geométrico aceita projeções pinhole canônicas com matriz inversa consistente, profundidade convencional/invertida, planos finitos/infinitos e convenções de Z positivo ou negativo. Deriva os parâmetros da própria matriz, sem substituir dados ausentes por 60 graus ou distâncias presumidas. Projeção ortográfica, matriz inválida, flags desconhecidas e bases não ortonormais são identificadas. A transformação CPU de normais entre orientações de câmera foi testada, mas ainda não foi ligada ao shader do jogo.

## Limite atual da conexão

**Esta versão conecta e valida metadados; não executa o modelo de iluminação dentro do jogo.** A entrada XeSS inspecionada não fornece identificador de quadro Streamline nem viewport. Idade baixa, jitter igual e reutilização da mesma textura são evidência para investigação, não uma identidade de quadro comprovada. O snapshot é apenas um candidato de diagnóstico. Os registros mantêm `frame_association=unverified`, `viewport_association=unverified` e `own_neural=false`.

Ainda faltam correspondência explícita câmera/quadro, convenções reais de cor/profundidade, domínio de iluminação do modelo durante rotações, integração do passe e gestão de recursos GPU nesse caminho. Não se anuncia efeito habilitado automaticamente nem comparação visual em jogo nesta versão.

## Validação e próximo teste

38/38 testes CTest passaram. O teste novo cobre projeção assimétrica, ambas as convenções de Z, profundidade normal/invertida, plano distante finito/infinito, reconstrução de pontos conhecidos, rejeição de NaN/inversa incorreta/flags inválidas, base degenerada e uma normal fixa no mundo sob rotação de câmera.

Código: `native/integration/camera_contract.h`, `native/baseline/camera_contract_tests.cpp`, `TsrCameraBridge.h` na integração TSR; hooks em `Streamline_Hooks.cpp` e `XeSS_Dx12.cpp`. Os SDKs locais `external/streamline/sl_consts.h` e `external/streamline1/sl1_consts.h` documentam matrizes row-major sem jitter e os vetores de câmera. Não são usadas convenções inferidas de um nome de jogo.

O próximo teste manual precisa apenas de uma sessão curta em The Witcher com XeSS Ultra Qualidade: mover o personagem, girar a câmera e abrir/fechar o menu. A expectativa é coletar metadados mantendo a imagem existente. Não é um teste de melhoria visual. O log usado continua sendo `TSR_FrameGen.log`, preservando as configurações atuais.

Análise reproduzível: `python tools/analyze_camera_bridge.py <log> --output <resumo.json>`. O script distingue ausência de câmera, validade geométrica e associação ainda não comprovada. As amostras são limitadas às primeiras chamadas e uma a cada 600; suas contagens não representam todos os quadros.

Pacote: `artifacts/game-test/witcher3-camera-bridge`. O manifest registra hashes e o registro de instalação identifica o backup. A configuração é copiada exatamente da instalação corrente e verificada antes/depois. A restauração usa o instalador existente com proteção contra alterações posteriores.

Instalação concluída em 07/09/2026: DLL SHA256 `051E865AF501BCD4192D332C070C827B923A130AED3074E250E1712962C1841D`; INI preservado com SHA256 `7194A507252947460AE54BBFBFE1DEC50174F1E191B0B77531F958E2F2F5C890`. Backup verificado em `C:\Program Files (x86)\Steam\steamapps\common\The Witcher 3\bin\x64_dx12\TSR-backup-20260907-015329-512`. Build Release passou, mantendo avisos anteriores de herança/CRT e cópia de dependências. A execução desta ponte no jogo ainda aguarda a sessão manual.

## Primeira sessão e correção da instalação dos hooks

A sessão de 07/09/2026, 01:57:35–02:17:02, usou a DLL esperada e terminou normalmente, sem linhas de erro. Registrou 89 amostras da entrada XeSS e nenhuma amostra de câmera ou tag. A reconstrução FSR 4.1.1 continuou retornando sucesso nas amostras. Isso não permite concluir que o jogo deixa de fornecer câmera.

A inspeção encontrou uma falha na implementação da ponte: os callbacks já capturavam metadados independentemente da geração, porém a **instalação** dos hooks `slSetConstants`, `slSetTag` e `slEvaluateFeature` ainda estava condicionada a `IsSL1AndFGActive()`. Essa condição exige entrada de geração DLSSG; a sessão usa entrada Upscaler/OptiFG. O log confirmava existência dos exports, não instalação dos respectivos hooks. Portanto a captura nem chegou a observar esse caminho do jogo.

A correção instala todos os exports SL1 resolvidos independentemente do backend FG. As operações de geração dentro dos callbacks continuam condicionadas ao modo correspondente. Isso também alinha instalação e remoção: a remoção já tentava desassociar todos esses ponteiros resolvidos. Acrescentado registro após commit bem-sucedido: `TSR camera hooks committed`, distinguindo exports encontrados de hooks efetivamente instalados. O analisador agora explicita essa distinção quando não há dados de câmera.

Evidência preservada: `artifacts/temporal-validation/witcher-camera-bridge-game.log`, SHA256 `8b51826d2a9a476eb52b4a86a5875a3a415aca684bafc9a2892ebe4fee2a837b`, e resumo `.json`. A versão corrigida está no pacote `witcher3-camera-bridge-fix`; ela ainda mantém a inferência neural desligada. Não é justificável pedir repetição com a DLL antiga.

A correção foi compilada e instalada em 07/09 às 09:08. SHA256 da DLL: `B04E072F7D2004DDBE15204204CF5A15EB43B18281087258C01BCFDEC66F5140`. O INI continua com o mesmo hash anterior. Backup verificado: `C:\Program Files (x86)\Steam\steamapps\common\The Witcher 3\bin\x64_dx12\TSR-backup-20260907-090817-983`. O teste de contrato de câmera foi reexecutado e passou; a confirmação de instalação dos hooks está presente no binário compilado. A suíte completa anterior tinha 38 testes aprovados; ela não foi repetida para esta alteração restrita de instalação dos hooks. O próximo log deverá primeiro confirmar `TSR camera hooks committed`, antes de avaliar a presença de callbacks de câmera. O fato de a instalação compilar não garante que o jogo vá enviar matrizes nesta configuração.

## Captura confirmada e descompasso de jitter

Sessão posterior: 07/09, 09:11:39–09:14:14, DLL instalada com o hash esperado. Houve confirmação de commit dos três hooks, 13 amostras de câmera SL1 no viewport 0, 12 amostras XeSS e 11 candidatos de câmera. Nove candidatos passaram o contrato de projeção; dois candidatos iniciais foram rejeitados porque a matriz indica profundidade convencional enquanto a flag declara profundidade invertida. As 11 bases de orientação passaram a verificação. Não houve linhas de erro; o encerramento foi normal. Nenhuma tag SL1 foi registrada nesta captura.

Na partida, os dados observados incluem near=0,2, far=6500, aspecto 1,7777778, FOV inicialmente aproximadamente 60° e alteração de FOV/orientação durante a sessão. Para 1476×830, um candidato tem fx=719,0175, fy=718,80096, cx=737,5, cy=414,5; profundidade A=-0,000030755997 e B=0,20000616, Z positivo. Esses valores são evidência desta captura, não defaults a fixar para jogos ou cenas futuras.

A idade do candidato foi 0–16 ms, mas o jitter não coincide nas nove amostras com projeção válida. `tools/inspect_camera_jitter.py` examinou bases Halton 2/3, ambas as ordens e sinais, índices 0–65535 e tolerância absoluta 0,000002. Uma hipótese explicou os nove pares: X=Halton base 3−0,5; Y=0,5−Halton base 2. Em todos os pares, o índice XeSS é o índice do último candidato SL1 menos um. Exemplos: candidato 591/XeSS 590, 1191/1190, até 5472/5471. O índice Halton é uma hipótese numérica sobre a fase, não o identificador de quadro Streamline. Não usar simplesmente `frame−1` como associação universal.

Isso justifica conservar um histórico curto de câmeras e pesquisar um candidato correspondente, recusando ausência, ambiguidades, reset, dados antigos e mudanças de viewport. Ainda será preciso associar explicitamente recursos/quadro; frescor e jitter iguais isoladamente não autorizam inferência. A matriz/pose anterior não foi integralmente registrada em cada chamada XeSS, portanto não pode ser reconstruída de forma confiável apenas deste log amostrado. Nenhuma mudança na DLL ou nas configurações do jogo foi feita ao analisar esta sessão.

Arquivos: `witcher-camera-hook-fix-game.log` e `.json`, `witcher-camera-jitter-analysis.json`, todos em `artifacts/temporal-validation/`. SHA256 do log: `84c5c93eb1e5027f2e7ea9278e31432dac4ec516919bcdfb72d4c9c814caa595`. O modelo de iluminação continua desligado no jogo.

## Segunda captura e histórico de candidatos

A sessão adicional encerrada em 07/09 às 09:29:02 registrou 49 amostras de câmera, 24 entradas XeSS e 23 candidatos; 21 candidatos têm projeção válida, e os dois iniciais não. Todas as 23 bases testadas são válidas. Não há linhas de erro. Os 21 pares válidos ajustam-se à mesma hipótese Halton com câmera uma posição à frente do XeSS. Juntamente com a sessão anterior, são 30 pares observados consistentes com esse descompasso. Arquivos `witcher-camera-extra-game.log/.json` e `witcher-camera-extra-jitter.json`, em `artifacts/temporal-validation/`; SHA256 do log `1f372dc390692a25322b54efafdad1ec5f5e5469f36f30a4bc2b3e03b0edeb1f`.

Implementado `native/integration/camera_history.h`: buffer fixo de oito cópias completas de câmera, com serial e timestamp. A busca compara jitter em ambos os eixos com tolerância 0,000002, exige projeção/base válidas, rejeita tempos futuros e idades acima de 100 ms, e retorna candidato somente quando há uma correspondência única. Não presume Halton nem atraso de um quadro. Reset, câmera inválida, mudança de source/viewport, retorno do contador de quadros/serial ou do relógio invalidam o histórico. Callbacks duplicados que conservam o mesmo jitter são considerados ambíguos, sem escolher arbitrariamente uma pose.

A ponte registra o histórico a cada callback de câmera; um reset da entrada XeSS também o limpa. A busca é feita apenas nas amostras de diagnóstico (primeiras três e uma a cada 600), mantendo a validação completa das entradas do histórico na captura de câmera. O registro `TSR camera history` mostra status, quantidade de correspondências, serial/quadro/viewport do candidato e idade. O buffer não retém recursos gráficos e é protegido pelo mutex da ponte.

O teste `camera_history` passou para ausência, atrasos de um e três quadros, dados velhos/futuros, ambiguidade, reset, troca de viewport/source, câmera inválida, reinício de contador/relógio e descarte circular. Os casos são independentes do gerador Halton, para que um atraso fixo não passe como solução geral.

**Uma correspondência única de jitter continua sendo apenas um candidato.** O registro conserva `association=unverified` e `own_neural=false`: falta vínculo explícito aos recursos/quadro do jogo, integração GPU e validação do modelo com iluminação real. Esta mudança não habilita iluminação nem representa ganho visual. Os logs anteriores guardam somente amostras esparsas; não permitem reconstruir as poses intermediárias que o novo histórico passará a reter em execução.

Versão com histórico compilada e instalada em 07/09 às 09:36: pacote `artifacts/game-test/witcher3-camera-history`, DLL SHA256 `A6F7B2B2303022947D10DB73B174DC3B498C8A79C3662CF63E0F103D4E81B48C`. INI preservado com o mesmo hash anterior. Backup verificado `C:\Program Files (x86)\Steam\steamapps\common\The Witcher 3\bin\x64_dx12\TSR-backup-20260907-093615-650`. Os dois testes direcionados (`camera_contract` e `camera_history`) passaram; logs `ctest-camera-history.log` e `build-dll-camera-history.log`. Nenhum teste de jogo com esta versão havia ocorrido no momento da instalação.

## Histórico observado no jogo

Sessão de 07/09, 09:37:27–09:40:16, DLL confirmada pelo hash acima, encerramento normal e zero linhas de erro/erros de textura. Das 14 amostras de busca, as três iniciais não tinham candidato válido e as 11 restantes encontraram **exatamente um candidato**, em todos os casos uma publicação anterior à câmera mais recente. Não houve ambiguidade nas amostras. A idade das câmeras selecionadas foi 0–32 ms, mediana 16 ms; não confundir com a idade da câmera mais recente, cuja mediana era 0 ms. As câmeras selecionadas passaram a validação de projeção e base antes de serem retornadas pela busca.

O FSR 4.1.1 retornou OK nas 14 amostras registradas, 1476×830 → 1920×1080. O XeFG teve dez amostras de interpolação 2× bem-sucedida, cinco desativadas e uma inicial com histórico insuficiente. São amostras de API, não métricas de latência, apresentação física ou qualidade visual.

O resultado valida o funcionamento da **busca de candidatos em execução real** e supera a escolha anterior da câmera mais recente. Ainda não estabelece identidade explícita de recursos/quadro: o registro permanece `association=unverified`, e o modelo não foi executado. A etapa seguinte é validar essa associação junto às texturas e preparar a execução gráfica controlada do passe; repetir esta mesma coleta isolada não acrescenta a ligação GPU que falta.

Evidências: `artifacts/temporal-validation/witcher-camera-history-game.log` e `.json`, SHA256 do log `5894daa99b7826fd073a43ccf55f19528e68367a7f96f0505017c53405ce6b53`. Nesta análise foram apenas preservados os registros e atualizado o resumo/documentação; arquivos do jogo não foram alterados.
