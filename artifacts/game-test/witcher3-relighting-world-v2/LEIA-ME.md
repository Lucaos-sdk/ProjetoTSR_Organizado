# Relighting V2 — luz fixa e superfície suavizada

Esta versão atende aos dois defeitos relatados no primeiro teste visual: iluminação do chão variando com o ângulo e aparência facetada na pele. A correção muda o modelo e o shader, mantendo a inferência antes do FSR e os controles de comparação. A confirmação visual no jogo ainda é necessária; os números abaixo são testes sintéticos/isolados.

## Mudanças

**Direção fixa em relação à cena.** O primeiro quadro aceito pelo passe fornece uma orientação de referência. As normais de cada quadro são transformadas para essa referência, usando a base validada da câmera correspondente. Mover/girar a câmera ou alternar F8 não muda a orientação da luz. O botão `Set light direction from current view` permite escolher outra direção; o histórico do FSR reinicia nessa troca. O referencial é recriado também na recriação do backend ou troca de origem/viewport da câmera. Não depende de assumir que o eixo vertical do jogo é Y ou Z.

**Treino para todas as orientações.** O modelo `world_v2` continua pequeno, com 171 parâmetros, mas foi treinado com 65.536 normais distribuídas pela esfera completa, em 12.000 passos. Foi avaliado depois do treino em 32.768 orientações independentes, incluindo Z negativo. A transformação alvo é a mesma iluminação difusa analítica do primeiro experimento. MSE de log-ganho no conjunto separado: 0,000268793; percentil 95 do erro absoluto: 0,032787. Não foram usados dados de jogos, materiais reais ou imagens DLSS. Pesos empacotados: SHA256 `bd8474d08460d47204642fb4e8e1a3a9bbf33f9e56615adf72af4a7d80ce4177`.

**Suavização geométrica.** Em uma vizinhança 5×5, o shader ajusta um plano à profundidade inversa, com pesos espaciais e rejeição de diferenças grandes de profundidade. Profundidade inversa é afim numa superfície plana projetada em perspectiva. Amostras relativas ao centro reduzem cancelamento numérico. Se o ajuste fica degenerado ou diverge demais da normal local, é usada a estimativa original. Não há desfoque da textura de cor. Céu, alpha e bordas de profundidade são protegidos pelos testes. O controle `TSR surface smoothing` vai de 0 a 1 e começa em 1.

A orientação da normal passa a usar o raio até o pixel em vez do sinal isolado de Z. Isso evita inversões indevidas em faces inclinadas fora do centro da tela. Não há mistura de imagens antigas no novo passe; a estabilidade vem da direção fixa e da estimativa espacial mais suave. O FSR continua fazendo sua própria reconstrução temporal.

**Diagnóstico de interrupções.** Os registros agora distinguem reset de entrada, sub-região e resultado da busca de câmera, além de informar se as identidades de lista/cor/profundidade coincidem. As proteções de dados ausentes/ambíguos são mantidas. Esta versão não afirma ter eliminado todas as interrupções automáticas observadas anteriormente.

## Testes e custo

O teste `tsr_relighting_world_dx12` usa o mesmo shader, pesos, constante de orientação e classe de produção:

- 14 rotações de câmera, duas orientações fixas de superfície, incluindo a metade Z negativa do referencial. Maior erro contra a iluminação esperada na RX 7600: **0,00000238419**; sem a transformação de orientação, o erro no mesmo teste chega a **0,266927**. Valida a invariância da implementação, não a qualidade visual num jogo.
- Superfície curva aproximada por uma malha de triângulos com oito pixels por célula: MSE da iluminação contra a superfície ideal caiu de **0,00000218852** para **0,00000140669**, redução de **35,72%**. Essa porcentagem não é uma medida de melhoria geral nem garantia de eliminar todas as facetas de personagens.
- Degraus de profundidade e faixa de céu: maior erro de **0,0000000596046**. Alpha preservado. Renderizar o mesmo quadro depois de outra cena produziu bytes idênticos, verificando independência do quadro anterior.
- Os casos acima passaram em WARP e RX 7600. Os testes do caminho com hooks reais também verificam profundidade nativa, FP16/FP32, replay, seis envios pendentes e reutilização somente após fences.

Medição isolada na RX 7600, saída RGBA16F, profundidade R32F, suavização 1, camada de diagnóstico ativa, cinco aquecimentos e 20 amostras:

| Resolução do passe | Mediana | P95 | Máximo |
| --- | ---: | ---: | ---: |
| 1476×830, entrada usada no teste do Witcher | 0,42548 ms | 0,42796 ms | 0,43148 ms |
| 1920×1080 | 0,72652 ms | 0,86768 ms | 0,88804 ms |

Inclui execução e barreiras do passe próprio; não inclui envio/cópias do teste, FSR, FG, latência de entrada ou o quadro inteiro. A antiga mediana de 0,09 ms era da V1 dentro do jogo e não pode ser tratada como uma comparação de desempenho equivalente. O custo dentro do jogo com a V2 ainda será medido.

## Uso e limites

Manter DX12 e XeSS Ultra Qualidade. F8 compara original/efeito. Deixar `TSR surface smoothing` em 1 para a primeira comparação. A intensidade existente é preservada. Para escolher outra direção, posicionar a câmera e clicar em `Set light direction from current view`; depois girar a câmera para avaliar a fixação da luz. Suavização e direção são controles da sessão.

O efeito ainda é uma transformação difusa de cor guiada pela profundidade. Não cria sombras físicas projetadas, não recupera normais/roughness reais de materiais e não gera novos detalhes/rostos. Não equivale a DLSS 5. O modelo pequeno aprendeu uma função analítica; treinar a esfera completa corrige seu domínio de entrada, não demonstra capacidade generativa. Melhorias em pele real, transparências e cenas complexas precisam de avaliação visual.

O caminho atual continua restrito aos contratos aceitos de XeSS DX12, câmera por candidato único de jitter, formatos e regiões de textura já documentados. Associação de câmera permanece experimental. O backend não espera a GPU na CPU; os seis slots e fences anteriores são preservados.

Reprodução: `tools/train_relighting_world.py`, `tools/embed_game_relighting.py`; evidências em `artifacts/relighting-world-v2/metrics.json`, `artifacts/temporal-validation/relighting-world-*.log` e `relighting-v2-hooks-*.log`. Os PPM do teste mostram somente uma superfície sintética, não capturas de jogo.

## Instalação

Instalada em 07/09/2026 às 16:14, pacote `artifacts/game-test/witcher3-relighting-world-v2`. DLL SHA256 `E7DF8477F1C9F272D6F82B120AD83C9C68BD5CB23F22227244A8EF702ED09C11`. INI preservado integralmente, SHA256 `6C4AF8272437A9B6DAA88BA199E56B4B4032456DD0F3F32A673EE2ABD97DF348`, intensidade inicial existente 0,35. Suavização padrão 1, F8 e controles de sessão disponíveis.

Backup verificado: `C:\Program Files (x86)\Steam\steamapps\common\The Witcher 3\bin\x64_dx12\TSR-backup-20260907-161420-248`. A versão anterior que o usuário testou está preservada. Compilação Release concluída, seis testes direcionados CTest aprovados e testes de hooks reais na RX 7600 aprovados com o modelo final. A suíte completa não foi repetida. A avaliação da V2 dentro do jogo está pendente.
