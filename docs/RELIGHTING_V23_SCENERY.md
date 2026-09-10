# Relighting V2.3 — proteção de cenário distante

Implementada em 08/09/2026 após o teste prolongado da V2.2. O usuário relatou aspecto estranho em objetos distantes e enviou capturas desligado/ligado. Na segunda, as árvores parecem recortadas contra a névoa e o morro fica mais claro. As capturas mostram horários diferentes (18:23 e 18:26) e animações distintas: não permitem atribuir toda a diferença ao passe nem medir erro por pixel.

## Mudança na DLL

O passe anterior multiplicava também a cor já composta com névoa. Normais reconstruídas de profundidade não descrevem necessariamente a orientação real de folhas e vegetação distante. A V2.3 reduz o ganho gradualmente nesses casos, mantendo a transformação próxima onde há uma superfície suficientemente contínua.

- Atenuação suave pela distância radial à câmera. Com alcance padrão 80, começa em 20 e chega a zero em 80, em unidades da câmera do jogo. Não se assume equivalência universal com metros. Usa distância radial, evitando que só girar a câmera altere a atenuação de um mesmo ponto.
- Confiança de superfície calculada com a vizinhança 5×5 já usada na suavização. Diferenças relativas de profundidade entre 1% e 2% reduzem o suporte; suporte normalizado entre 0,70 e 0,98 suaviza a intensidade. Bordas finas e descontinuidades recebem menos efeito. Isso também pode reduzir o efeito em superfícies muito inclinadas; é uma heurística conservadora, sem classificação de materiais.
- Fora do alcance, copia o RGB e alpha originais e evita calcular a rede. Não adiciona passe, textura de histórico nem espera de CPU. A imagem entregue ao FSR pode ainda mudar pela reconstrução temporal; identidade é verificada na saída do nosso passe.
- Preserva a composição de cor da V2.2, modelo de 171 parâmetros, direção ancorada e suavização. Não treina outra rede, separa fisicamente névoa/iluminação, cria sombras projetadas ou modifica o modelo neural AMD.

## Como comparar no jogo

Iniciar em DX12 com os ajustes pessoais existentes. `TSR scenery protection` começa ligado. Desmarcar compara com o comportamento visual da V2.2, mantendo os demais controles iguais. `TSR effect reach` começa em 80: valores menores preservam uma parte maior do cenário. F8 continua alternando o passe completo.

Os controles são da sessão. Padrões persistentes opcionais em `[TSRRelighting]`: `SceneryProtection=1` e `EffectReach=80` (20 a 200). Ausência das chaves usa esses padrões, permitindo preservar o INI byte a byte. Alterações reiniciam o histórico do FSR quando o passe está ativo e são registradas como `composition=v23`, `scenery_protection` e `effect_reach`.

Para avaliar a correção visual, comparar parado na mesma paisagem com névoa, olhando árvores e morros distantes, deixando a reconstrução estabilizar após alternar. Depois girar a câmera e observar as bordas da vegetação. O usuário confirmou ter testado a V2.3; o log foi analisado em 09/09. Ainda não há relato visual específico que confirme a correção de árvores/névoa.

## Evidências do teste prolongado da V2.2

O log arquivado `artifacts/temporal-validation/witcher-world-v22-game.log` registra pelo menos 164.335 quadros do passe. Mediana amostrada 0,20064 ms, P95 0,20444 ms, máximo 0,654 ms; nenhum registro de erro identificado pelo analisador. São timestamps esparsos do passe próprio, não tempo do quadro completo ou latência de entrada.

Foram identificados 37 intervalos automáticos completos de bypass: 12 de uma chamada, 24 de duas e um de 171; 32 começam por câmera/recursos não associados, cinco por bindings não suportados. Houve 90 intervalos por desativação nos controles. Não é possível associar essas contagens ao defeito visual das capturas. A associação experimental de câmera permanece uma limitação independente.

SHA256 do log: `3a942936e512211ee9145735fd0a1ab803b8e3e9a23e2694bd8281023677bff9`. Análise em `witcher-world-v22-analysis.txt` e JSON correspondente.

## Validação da V2.3

Na RX 7600 e WARP, testes de distância, monotonicidade, plano próximo, bordas de profundidade, objetos de dois pixels, suavização zero, FP16/FP32, céu, alpha, cache versus leitura direta e distância radial passaram. Maior erro de atenuação GPU/CPU: 0,000481037, incluindo FP16. Cenas além do alcance mantiveram a saída exatamente igual à entrada. O teste radial compara o mesmo raio em três projeções; não substitui uma sequência real de jogo.

Os testes anteriores de composição de cor, faixas numéricas, 14 rotações e suavização de facetas continuam passando. A integração com hooks reais passou em 16 combinações de cena próxima/distante, modo de cor, FP16/FP32 e profundidade R32F/nativa depth-stencil. Houve 68 callbacks de Reset e 55 de Execute, sem notificação manual. O caso distante nativo manteve erro zero, incluindo replay. Testes de seis slots ocupados, descarte de lista, fences e fallback sem espera também passaram.

Seis testes direcionados CTest passaram, incluindo WARP. Evidências: `artifacts/temporal-validation/relighting-v23-rx7600.txt`, `relighting-v23-hooks-rx7600.txt` e `relighting-v23-ctest.txt`. A suíte completa não foi repetida.

### Custo isolado

Cena sintética próxima, RGBA16F, profundidade R32F, suavização e cache ativos, diagnóstico D3D12 ligado; 20 amostras após aquecimento, variantes alternadas. Ambos os modos abaixo usam o shader V2.3, com proteção desligada/ligada: não é comparação direta de binários V2.2 e V2.3.

| Resolução interna | Proteção desligada, mediana | Proteção ligada, mediana | Diferença |
| --- | ---: | ---: | ---: |
| 1476×830 | 0,41508 ms | 0,41964 ms | 0,00456 ms |
| 1920×1080 | 0,69432 ms | 0,70104 ms | 0,00672 ms |

Esses tempos sintéticos não devem ser comparados diretamente à mediana de 0,201 ms do log da V2.2. A sessão V2.3 analisada abaixo fornece uma medição própria em jogo.

### Teste em jogo recebido em 09/09/2026

Log da sessão de 08/09, aproximadamente 20:08–21:11, arquivado em
`artifacts/temporal-validation/witcher-world-v23-game.log`. SHA256
`f6c3525de4c31c309396ae74b8df6150f5b3eaf09aa483bbf2ac90baaec36143`.
Campos `composition=v23`, `placement=pre_fsr`, proteção ativa e alcance 80
confirmam o caminho testado. No fim da sessão, intensidade 0,5 e transferência
de cor 0,1; no início, 0,35 e 0. As opções do usuário foram preservadas.

O contador de quadros gravados atingiu 144.620. Há dois reinícios no contador
de chamadas, portanto não tratamos esse contador como duração exata ou total
universal da sessão. Mediana de timestamps amostrados do passe próprio:
**0,23560 ms**, P95 **0,24292 ms**, máximo **0,78948 ms**. Não é tempo total
de quadro, latência de entrada ou previsão para a futura CNN.

Nenhum registro `[E]` identificado pelo analisador. Foram encontrados 114
intervalos automáticos completos de bypass (111 por câmera/recursos não
associados, três por bindings): 25 de uma chamada, 85 de duas, dois de três,
um de 489 e um de 671. Outros 40 intervalos ocorreram por desativação nos
controles. Os logs não permitem atribuir esses intervalos a uma falha visual;
transições de jogo, menus ou câmera ainda precisam de correlação. Associação
de câmera continua experimental. Relatório reproduzível em
`witcher-world-v23-analysis.json`.

O usuário esclareceu que a prioridade é a nova IA de iluminação. Estes dados
ficam como baseline; não motivaram outro ajuste de aparência na V2.3.

## Instalação

Compilação Release e instalação concluídas em 08/09/2026 às 18:41. Pacote `artifacts/game-test/witcher3-relighting-world-v23`. DLL SHA256 `9b3992a1fd4337a1c235dc3401d59f8bc81614e29585f3f20546b4c9bc6d47d0`, conferido na pasta do jogo após copiar.

Backup verificado da V2.2: `C:\Program Files (x86)\Steam\steamapps\common\The Witcher 3\bin\x64_dx12\TSR-backup-20260908-184145-879`. O INI foi preservado byte a byte, SHA256 `6c4af8272437a9b6daa88ba199e56b4b4032456dd0f3f32a673ee2abd97df348`; intensidade inicial 0,35, proteção ativa, alcance 80 e transferência de cor 0. O arquivo `dx12user.settings` também foi conferido inalterado, SHA256 `c114d2251fb92d83a3223d91eb2dc20632a9351e8f4259d40412fd744b97181f`. Evidência: `artifacts/temporal-validation/relighting-v23-install.txt`.
