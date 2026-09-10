# Primeiro experimento de iluminação treinada — 7 de setembro de 2026

Foi treinada uma rede própria de 171 parâmetros (3 → 24 tanh → 3), com NumPy, para prever ganho RGB a partir de normais conhecidas. A aplicação multiplica a cor linear pelo ganho previsto. É uma tarefa controlada de iluminação difusa fixa; não modifica pesos do FSR 4.1.1 e não está instalada no jogo.

## Experimento reproduzível

Executar `python tools/train_relighting_fixture.py` com NumPy e Pillow. `--self-test` verifica as derivadas do treinamento por diferenças finitas e o bypass exato. A execução completa verifica também serialização dos pesos, valores finitos e comparação contra baselines.

- Treino: 12 superfícies sintéticas, sementes 100–111, 49.152 amostras; 4.000 passos Adam, lote 512, semente fixa.
- Entrada: normal analítica de uma superfície formada por relevos gaussianos. Cores/albedo procedurais, iluminação original neutra.
- Alvo: renderização analítica da mesma superfície e albedo com luz direcional quente e preenchimento frio.
- Avaliação: quatro superfícies reservadas, sementes 900–903, sem seleção de checkpoint por esses resultados.
- Baselines: imagem original e ganho RGB global ajustado somente nos dados de treino.
- Erro: média quadrática RGB em espaço linear, antes de qualquer conversão para exibição ou recorte.

| Cena reservada | Imagem original | Ganho global | Rede treinada |
| --- | ---: | ---: | ---: |
| 900 | 0,0106933 | 0,0037406 | 0,00001307 |
| 901 | 0,0241153 | 0,0133494 | 0,00004814 |
| 902 | 0,0184549 | 0,0071180 | 0,00001275 |
| 903 | 0,0234465 | 0,0176851 | 0,00003553 |

Resultados completos, pesos e comparação visual: `artifacts/relighting-fixture-v1/`. A figura usa a mesma conversão linear → sRGB nas três colunas de cor; a quarta mostra erro absoluto linear ×8. Não há ajuste automático de contraste. SHA256 dos pesos: `0780d885539985580e9d4427403d5c517d6583009ec91a9570d22dfc4f679224`.

## O que isso comprova e o que falta

A rede aprendeu uma função de iluminação dependente da orientação da superfície, em vez de aplicar apenas uma correção global de cor. A referência é deliberadamente simples: a própria fórmula analítica usada para gerar o alvo já resolve esta tarefa sem aprendizado. Portanto este resultado valida o processo de treinamento e avaliação, não a necessidade de uma rede nem qualidade próxima ao DLSS 5.

Não há sombras projetadas, reflexos, materiais aprendidos, mudança de exposição, HUD, avaliação temporal, inferência GPU ou orçamento de ms medido. As normais são exatas e não são fornecidas pelo contrato atual do jogo. As cenas de avaliação são novas amostras do mesmo gerador; não demonstram generalização para jogos reais. Nenhum ganho de qualidade em The Witcher 3 foi demonstrado por este experimento.

Esse próximo experimento foi implementado: [iluminação por profundidade em DirectX 12](RELIGHTING_DX12.md), com execução na RX 7600, comparação GPU/CPU, bordas e sequência curta em movimento. As limitações acima descrevem este primeiro treino; a evolução ainda usa projeção sintética conhecida e não valida dados reais do jogo.
