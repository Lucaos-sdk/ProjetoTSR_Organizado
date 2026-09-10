# Relighting V2.2 — iluminação com preservação de cores

Implementada em 08/09/2026. O usuário autorizou novas mudanças após jogar com a V2.1. A V2.2 introduz composição com controle separado de iluminação e cor, inspirada no estudo de `RestoreRange` do [neural-upstream](https://github.com/matiasLombo/neural-upstream/blob/333038704896d6e38f735b9ddb6e62210e509cb9/src/codec.hlsl.h). A implementação é própria e adaptada à nossa MLP de ganho RGB; não copia o backend NGX nem carrega a DLL/rede externa.

## O que muda visualmente

Na V2.1, cada canal recebia um ganho diferente previsto pelo modelo, alterando iluminação e coloração ao mesmo tempo. A V2.2 começa com ganho comum aos três canais: preserva suas proporções enquanto altera o brilho guiado pela superfície. Isso permite manter melhor a cor original da pele, roupas e vegetação, sem eliminar a alteração de iluminação aparente. Essa expectativa precisa de avaliação no jogo; os testes abaixo verificam propriedades matemáticas e casos sintéticos.

No menu:

- `Preserve game colors`: transferência de cor 0, padrão da V2.2.
- `V2.1 colored light`: transferência 1, processamento RGB anterior.
- `TSR color transfer`: valores intermediários misturam as duas composições.
- F8 continua comparando efeito ligado/desligado. Intensidade, suavização e direção de luz permanecem disponíveis.

O controle de cor é da sessão, assim como a suavização. Pode-se definir o padrão persistente com `ColorTransferPercent=0..100` em `[TSRRelighting]`; a ausência da chave significa 0. A instalação preserva o INI existente. Alterar o controle de cor reinicia o histórico do FSR para evitar misturar composições diferentes e registra o valor no log.

## Cálculo e limites

Se `c` é o RGB linear e `g` o ganho RGB previsto pela rede, calculamos `s = L(max(c,0) * g) / L(max(c,0))`, com proteção de preto e pesos de luminância Rec.709. O ganho aplicado é a mistura de `s` e `g` controlada pelo slider. Para entradas positivas sem saturação, o modo de cor preservada tem a mesma luminância da versão colorida, porém mantém as proporções RGB originais. Não é um filtro de nitidez nem uma rede treinada nova.

Os cálculos normalizam os valores antes da divisão. Um teste identificou que a divisão por valores FP32 extremos podia usar recíproco subnormal que a GPU zerava; o cálculo agora reduz numerador e denominador juntos nesses casos. No modo novo, quando a faixa de saída é excedida, limita-se o trio RGB em conjunto. Em transferência 1, conserva-se a composição anterior. Alpha, céu e bordas continuam preservados.

O modelo `world_v2` de 171 parâmetros, orientação fixa, suavização geométrica e cache de profundidade permanecem. O teste numérico de valores HDR não comprova compatibilidade completa com todos os espaços de cor/monitores HDR. A integração continua rejeitando as entradas não lineares já identificadas. Não acrescentamos material físico, sombras projetadas, geração de quadros ou o modelo externo de 148 MB.

## Testes concluídos

`tsr_relighting_world_dx12` compara os modos 0, 0,5 e 1 contra referência CPU independente, em FP32 e FP16. Inclui duas orientações de iluminação, cores de pele simuladas, primárias saturadas, brilho acima de 1, preto, valores negativos, extremos de formato, céu e bordas. Também verifica preservação das proporções RGB e equivalência de luminância fora da saturação. Maior erro relativo observado contra a referência: 0,000856084; maior erro nas proporções normalizadas: 0,000777765, incluindo quantização FP16.

Os testes anteriores de rotações, facetas e independência do quadro anterior continuam passando. O teste de hooks reais foi ampliado para os dois modos de cor: oito combinações de modo, FP16/FP32 e profundidade simples/nativa. Verifica replay, alpha, seis slots pendentes, descarte de lista, bypass sem espera e reutilização após fences. Maior erro absoluto GPU/CPU: 0,000488579. Houve 36 callbacks de Reset e 31 de Execute, sem notificações manuais nesse teste.

Seis testes direcionados CTest passaram, incluindo execução em WARP. Os testes de composição e hooks também passaram na RX 7600. A suíte completa não foi repetida. Evidências: `artifacts/temporal-validation/relighting-v22-*.txt`.

### Custo isolado na RX 7600

RGBA16F, profundidade R32F, suavização 1, cache ativo, diagnóstico D3D12 ligado. Vinte amostras por modo após aquecimento, ordem alternada. Cada modo é repetido antes de coletar o timestamp concluído, evitando atribuir a ele a medição do modo anterior.

| Resolução interna | Cores preservadas, mediana | Composição anterior, mediana | Acréscimo |
| --- | ---: | ---: | ---: |
| 1476×830 | 0,35012 ms | 0,33768 ms | 0,01244 ms |
| 1920×1080 | 0,58324 ms | 0,56092 ms | 0,02232 ms |

Esses números medem o passe próprio em uma cena sintética. Não são latência de entrada, tempo de quadro completo ou previsão de FPS. A V2.2 ainda precisa de avaliação visual e medição dentro do jogo.

## Sessão de jogo anterior, V2.1

O log arquivado `artifacts/temporal-validation/witcher-world-v21-game.log` registra pelo menos 88.197 quadros do passe, mediana amostrada 0,194 ms, P95 0,20364 ms, máximo 0,62924 ms e nenhum registro `[E]`. Encerra normalmente com detach. Hash do log: `26fa75392f9eebfb15572a113aa1b25be85309634f573206eb545e67dcf3a701`.

O analisador identifica 89 intervalos automáticos completos (843 chamadas em bypass): 13 de uma chamada, 74 de duas e dois maiores, de 104 e 578. O motivo de entrada é câmera/recursos não associados em 86 e bindings não suportados em três. Há ainda 18 intervalos por desativação dos controles. Não se pode atribuir artefatos visuais a essas contagens sem correlação com o relato/captura. A V2.2 não afirma resolver a associação experimental de câmera.

## Instalação

Instalada em 08/09/2026 às 06:38. Pacote `artifacts/game-test/witcher3-relighting-world-v22`, DLL SHA256 `52a9f1677f84bb0d59782a258cae01cdce7c1567fa0f053fe74ccfeaac2b40f4`. Compilação Release concluída. INI preservado byte a byte, SHA256 `6c4af8272437a9b6daa88ba199e56b4b4032456dd0f3f32a673ee2abd97df348`; intensidade persistida 0,35 e novo modo de cor 0 por padrão.

Backup verificado da V2.1: `C:\Program Files (x86)\Steam\steamapps\common\The Witcher 3\bin\x64_dx12\TSR-backup-20260908-063827-355`. A versão instalada foi conferida pelo instalador. O próximo teste no jogo deve comparar os dois botões de cor na mesma cena; F8 compara com o passe inteiro desligado. Manter XeSS Ultra Qualidade e os ajustes existentes.
