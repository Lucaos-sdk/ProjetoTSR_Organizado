# neural-upstream v0.3.0 — revisão em 08/09/2026

Referência fornecida pelo usuário: [repositório](https://github.com/matiasLombo/neural-upstream) e [release v0.3.0](https://github.com/matiasLombo/neural-upstream/releases/tag/v0.3.0), publicada em 02/09/2026. A tag resolve para `333038704896d6e38f735b9ddb6e62210e509cb9`; a revisão principal consultada é `c06c07b27c3c5af0d916c3d9545434735d624bdf`. Foram baixados cinco arquivos de texto da tag, conferidos contra seus blobs Git. Não foram baixados nem executados o add-on binário ou runtimes neurais.

## Utilidade concreta

É uma referência de tratamento de imagem, integração e medição. A versão 0.3.0 intercepta NGX D3D12 e entrega o resultado neural à entrada DLSS. Depende de NVIDIA NGX e do runtime DLSSNR; não é a ponte AMD/HIP encontrada no projeto do Matheus. O código de inicialização procura o núcleo NGX e cria a feature neural. [Código da tag](https://github.com/matiasLombo/neural-upstream/blob/333038704896d6e38f735b9ddb6e62210e509cb9/src/addon.cpp).

### Preservação de HDR e cores

`CSEncode` prepara uma representação limitada da imagem para a rede. `RestoreRange` calcula a razão entre as luminâncias da imagem neural e dessa representação e a aplica à imagem original. Há limites de ganho e proteção de denominadores quase pretos. Transferência de cromaticidade pode ser controlada separadamente. Isso é útil para estudar um modo que preserve as cores do jogo enquanto muda a iluminação aparente; não recupera automaticamente materiais ou sombras físicas. [Shader](https://github.com/matiasLombo/neural-upstream/blob/333038704896d6e38f735b9ddb6e62210e509cb9/src/codec.hlsl.h).

Não se deve adicionar esse encode/decode indiscriminadamente à nossa MLP atual: ela foi treinada para prever ganho a partir de normais, não para consumir uma imagem sRGB limitada. A preparação deve corresponder ao contrato de cada modelo.

### Cadência, movimento e geração de quadros

A release relata que alternar quadros com/sem inferência causou intervalos de renderização desiguais e piorou o pacing com geração de quadros. O autor recomenda inferência em todo quadro nesse cenário. Também registra um flash preto raro ainda sem correção. Esses são relatos do ambiente do autor, não testes da RX 7600 ou do nosso XeFG. [Notas da v0.3.0](https://github.com/matiasLombo/neural-upstream/releases/tag/v0.3.0).

O projeto experimenta reaplicar a diferença neural sobre a imagem atual, com movimento e profundidade, em vez de repetir a imagem anterior inteira. Há uma ressalva concreta: `DeltaInPlace` ainda conserva metade do residual quando a profundidade não confere. Portanto, o código não realiza rejeição total em todos os caminhos. Esse fallback requer avaliação de rastros e superfícies recém-reveladas; não será tratado como solução geral de desoclusão. [Shader da tag](https://github.com/matiasLombo/neural-upstream/blob/333038704896d6e38f735b9ddb6e62210e509cb9/src/codec.hlsl.h).

### Chamadas repetidas e recursos pendentes

O código identifica chamadas repetidas pelo jitter e reaproveita o resultado correspondente. É uma pista útil para diagnóstico, mas jitter isolado não prova identidade de quadro/recurso em qualquer jogo. O caminho de descritores usa um anel de 64 slots com incremento circular; aumentar o anel não substitui comprovação de conclusão por fence. Nossa integração deve manter a associação exata de recursos e a reutilização condicionada à conclusão. O mesmo cuidado se aplica à leitura de timestamps: o profiler externo lê um slot antigo por distância no anel. [Implementação](https://github.com/matiasLombo/neural-upstream/blob/333038704896d6e38f735b9ddb6e62210e509cb9/src/addon.cpp).

## Desempenho publicado e escopo

O README descreve testes numa RTX 4070 Ti, em GTA V Enhanced e Bright Memory: Infinite. Publica 3,27 ms para a rede a 1280×720, além do custo de encode/decode; a discussão de FG usa outro cenário de aproximadamente 4,8 ms. Não são números da RX 7600, nem estimativa da nossa DLL. [README da tag](https://github.com/matiasLombo/neural-upstream/blob/333038704896d6e38f735b9ddb6e62210e509cb9/README.md).

A release corrige a chamada e a frequência do profiler, e declara inválidos os números que o projeto publicava anteriormente. Isso reforça a necessidade de medir cada etapa com conclusão GPU verificada. [Release](https://github.com/matiasLombo/neural-upstream/releases/tag/v0.3.0).

## Diferença para main

Existe um commit posterior que acrescenta hospedagem sem ReShade, via proxy e módulo separado. Foi lido o diff e a descrição do commit, não realizada uma auditoria completa desse novo proxy. Essa mudança não constitui evidência de suporte AMD nem deve ser atribuída ao binário da tag 0.3.0. [Commit posterior](https://github.com/matiasLombo/neural-upstream/commit/c06c07b27c3c5af0d916c3d9545434735d624bdf).

## Decisão para nosso projeto

Usar esta referência para planejar a composição HDR, separação entre alteração de iluminação e cor, avaliação de quadros repetidos e medição por etapa. Para um primeiro experimento com rede maior, manter uma passagem por quadro e medir custo antes de introduzir cadência variável ou multipass. O backend AMD continua exigindo a integração e as versões correspondentes estudadas anteriormente.

Nenhum código externo foi incorporado à produção nesta revisão. A V2.1 permanece a versão pronta para o teste de jogo ainda não realizado pelo usuário. Evidências em `artifacts/external-references/neural-upstream-v030/`: metadados, release, árvore, arquivos verificados, manifesto e diferenças de main. Não houve mudança gráfica que exigisse repetir os testes GPU.
