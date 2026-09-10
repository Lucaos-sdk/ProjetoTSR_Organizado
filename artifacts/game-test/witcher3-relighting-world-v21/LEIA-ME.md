# Relighting V2.1 — resultado no jogo e redução de custo

## Retorno visual e sessão da V2

Em 07/09/2026, o usuário confirmou que a iluminação do chão ao girar a câmera e a aparência facetada da pele foram corrigidas até onde percebeu. É uma avaliação visual dessa sessão, não uma garantia para todos os jogos, personagens ou ângulos.

O log preservado em `artifacts/temporal-validation/witcher-world-v2-game.log` registra pelo menos **59.041 quadros** do passe, 122 amostras de estado ativo e nenhum registro `[E]`. Mediana amostrada do passe próprio: **0,25818 ms**, P95 **0,2622 ms**, máximo **0,30212 ms**. São tempos da última execução GPU concluída, amostrados no log; não representam latência de entrada, FSR/FG ou tempo do quadro completo. SHA256 do log: `e4d2b293d9624874ccd9f22b4c203ee769ad14f42b650b646756f2ba77a942ac`.

Foram identificados dez intervalos automáticos completos de interrupção: cinco de uma chamada e cinco de duas. Nove começam com câmera ausente, um com bindings não suportados. Intervalos cruzando reinicialização de contador são excluídos. O usuário também alternou controles; esses eventos não devem ser apresentados como prova da causa de piscadas visuais. A associação por câmera única/jitter permanece experimental.

## Mudança na DLL

A V2.1 compartilha a profundidade decodificada entre os 64 pixels de cada grupo de execução. Um bloco de 12×12 inclui a margem necessária ao filtro 5×5, evitando repetir leituras e conversões da mesma profundidade para cada pixel vizinho. Todos os threads participam da carga e da sincronização, mesmo em dimensões que não sejam múltiplas de oito; o retorno dos pixels fora da imagem ocorre somente depois da barreira.

Pesos, função de iluminação, ajuste de normais e integração antes do FSR continuam sendo os da V2. Nenhuma nova textura de histórico, etapa de envio ou espera na CPU foi acrescentada. A versão de leitura direta é compilada apenas como referência de comparação nos testes; produção usa o cache.

## Comparação no mesmo teste e na mesma RX 7600

As versões com cache e com leitura direta foram alternadas a cada iteração para reduzir viés de aquecimento. Cada versão teve cinco aquecimentos e vinte amostras. Entrada de profundidade R32F, saída RGBA16F, suavização 1, diagnóstico D3D12 ativo. Cena sintética curva, sem FSR/FG e sem o jogo.

| Resolução interna do passe | Leitura direta, mediana | Cache, mediana | Redução |
| --- | ---: | ---: | ---: |
| 1476×830 | 0,42800 ms | 0,33904 ms | 20,8% |
| 1920×1080 | 0,73148 ms | 0,56520 ms | 22,7% |

P95 com cache: 0,34232 / 0,56584 ms, respectivamente. Esses resultados não são uma previsão de ganho de FPS do jogo. A comparação pertinente é entre as duas variantes desta execução; o 0,25818 ms da sessão real anterior tem outra carga e contexto.

Diferença máxima entre cache e leitura direta: **zero** nos casos comparados em FP16 e FP32, incluindo superfície facetada, descontinuidade de profundidade, céu, dimensões ímpares e grupos parciais. O teste também mantém a invariância em 14 rotações, redução do erro em superfícies facetadas, alpha e independência da imagem anterior. Isso verifica esses casos, não todas as imagens possíveis.

Seis testes CTest passaram (câmera/histórico, observador de envio, mundo em WARP, hooks reais em WARP e vida útil de listas). Na RX 7600, os hooks reais também passaram com profundidade nativa, FP16/FP32, replay, seis slots pendentes, descarte de listas e reutilização após fences. A suíte completa não foi repetida. Evidências: `artifacts/temporal-validation/world-v2-cache-*.txt`.

## IA e próximo avanço visual

O passe já recebe a imagem interna antes da super-resolução. Na configuração registrada, processa 1476×830 e o FSR reconstrói 1920×1080. A otimização deste documento acelera esse passe; não é uma nova geração de sombreamento.

A rede atual tem 171 parâmetros e aproxima uma função analítica de ganho RGB a partir da normal. Não conhece luzes do jogo, materiais, objetos fora da tela nem oclusão da geometria vizinha. Portanto, não cria sombras físicas ou detalhes generativos comparáveis a DLSS 5. O comentário do usuário sobre outro mod não foi verificado como descrição daquele software; o posicionamento do nosso passe é verificável no código local.

O próximo avanço proposto é sombreamento de contato guiado pelo contexto geométrico: ensinar um modelo pequeno a estimar oclusão local a partir de profundidade e normais vizinhas. Exige referência geométrica para o treino e avaliação em cenas separadas, além de comparar custo e qualidade contra uma solução analítica. Precisa preservar planos, céu, silhuetas, estabilidade em movimento e o sombreamento já existente do jogo. Dados somente de tela não recuperam geometria oculta; a rede não deve ser tratada como se tivesse acesso a ela. **Essa capacidade ainda não está implementada nesta versão.**

## Uso

Manter DX12, XeSS Ultra Qualidade e os ajustes pessoais. F8 continua permitindo comparar o efeito ligado/desligado. O objetivo desta atualização é manter a aparência corrigida com custo menor. A medição da V2.1 dentro do jogo ainda depende de uma nova sessão.

## Instalação verificada

Instalada em 07/09/2026 às 23:41, pacote `artifacts/game-test/witcher3-relighting-world-v21`. Compilação Release concluída. SHA256 da DLL: `211A8296918FCA4BF2237636BD5948AD68F996EF662C58F581D5F44A1426FB63`.

O INI foi preservado byte a byte, SHA256 `6C4AF8272437A9B6DAA88BA199E56B4B4032456DD0F3F32A673EE2ABD97DF348`. Intensidade persistida 0,35; a alteração para 0,3 observada no log era da sessão, não estava gravada nesse arquivo. Os caminhos de bibliotecas AMD/Intel existentes foram preservados.

Backup da V2: `C:\Program Files (x86)\Steam\steamapps\common\The Witcher 3\bin\x64_dx12\TSR-backup-20260907-234114-260`. Os hashes dos arquivos do jogo foram conferidos contra o estado anterior à preparação antes da instalação, e os arquivos instalados foram verificados pelo instalador.
