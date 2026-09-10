# Quatro referências de renderização neural — 07–08/09/2026

Foram consultadas as árvores públicas dos quatro links fornecidos pelo usuário e baixados 32 arquivos de texto para leitura. Cada arquivo baixado foi conferido contra o identificador de blob Git da versão fixada. Não foram executados instaladores, scripts ou DLLs externos. A DLL V2.1 do jogo não foi alterada.

## Resultado para o projeto

**A referência mais relevante é o projeto de MatheusGViana:** agora há código de uma ponte entre OptiScaler/D3D12 e o runtime neural AMD. Isso amplia a revisão anterior, que tinha apenas binários. O núcleo neural continua sendo uma DLL externa chamada por endereços internos; não encontramos nessa ponte o treinamento e a implementação completa de um modelo que substitua diretamente nossa MLP.

### DLSS5-Universal

Versão `6ca34279b7f5f43c3e605ee73e14ac31b25e4a8e`. A árvore consultada tem três arquivos: README, LICENSE e `manager.py`. O programa Python contém somente uma chamada de impressão de texto de teste. Portanto, essa árvore não fornece a implementação de instalação universal ou de renderização anunciada pelo README. Não usamos suas tabelas de compatibilidade ou desempenho como validação. [Código inspecionado](https://github.com/victorhall-io2091m3/DLSS5-Universal/blob/6ca34279b7f5f43c3e605ee73e14ac31b25e4a8e/manager.py).

### DLSS5Kit

Versão `7e7d33164751c33951cb8703bf66fa55047857ca`. Há código de instalação, seleção de caminhos de integração, diagnóstico, detecção de GPU e testes. O README identifica NVIDIA RTX como alvo; não é um backend neural AMD. A lógica de `routes.py` distingue chamadas NGX D3D12 reais, uma ponte para outras APIs e um alimentador sintético. É uma referência útil para diagnosticar por que um efeito não é chamado. `sources.py` também deixa claro que as implementações neurais usadas são obtidas externamente. [Rotas](https://github.com/UgurInanc12/DLSS5Kit/blob/7e7d33164751c33951cb8703bf66fa55047857ca/dlss5kit/routes.py), [dependências externas](https://github.com/UgurInanc12/DLSS5Kit/blob/7e7d33164751c33951cb8703bf66fa55047857ca/dlss5kit/sources.py).

### NeuralPipelineStudio

Versão `7095277110d00a2db3bae2d65c74684ba6c616b1`. O material inspecionado oferece interface, presets, gerenciamento de arquivos e escrita de configurações. O preset AMD/Intel descreve uso de FSR/XeSS; isso não demonstra execução da rede DLSS-NR na RX 7600. [Preset](https://github.com/brescale/NeuralPipelineStudio/blob/7095277110d00a2db3bae2d65c74684ba6c616b1/presets/08_AMD_Intel_NonRTX_FSR.json).

Há diferenças relevantes entre a apresentação e o código: `GenerateChainShaders` apaga os arquivos antigos de cadeia e não chama o gerador privado de código. O gerador privado contém filtros de reamostragem e contraste, não uma rede neural. [Implementação](https://github.com/brescale/NeuralPipelineStudio/blob/7095277110d00a2db3bae2d65c74684ba6c616b1/src/NeuralPipelineStudio/Core/ShaderGenerator.cs).

O relatório de VRAM é calculado com constantes e fórmulas estimativas. Há valores de fallback para memória/GPU; esse relatório não garante um limite real de alocação nem prevenção de perda de dispositivo. Não adotaremos seus multiplicadores, contagens de passagens ou estimativas de memória como orçamento medido da nossa DLL. [VramEngine](https://github.com/brescale/NeuralPipelineStudio/blob/7095277110d00a2db3bae2d65c74684ba6c616b1/src/NeuralPipelineStudio/Core/VramEngine.cs).

### dlss-5-amd-project

Versão `e58ff27a3d5ac86184875e01b317ff1463a247c1`. `AmdPreSr.cpp` implementa preparação de cor na área ativa, guias de movimento/profundidade, instâncias de runtime, gravação e notificações de submissão. `AmdBridge.cpp` conecta esse caminho ao OptiScaler. Essas são referências concretas para a integração que buscamos. [Backend](https://github.com/MatheusGViana/dlss-5-amd-project/blob/e58ff27a3d5ac86184875e01b317ff1463a247c1/OptiScaler-DLSSNR-PreSR-Multipass-main/OptiScaler/dlssnr/amd/AmdPreSr.cpp).

A ponte verifica o hash da DLL e usa RVAs, estados globais e layouts reconstruídos. O processamento depende de uma versão binária específica, não de uma API neural estável. Há esperas no caminho de submissão: o código consulta conclusão do worker e dorme em intervalos de 1 ms, com limite de 5 s. Isso não significa que cada quadro custe 5 s; significa que não devemos transplantar o caminho e presumir o comportamento sem espera da nossa V2.1. Não foi medido seu desempenho nesta máquina.

O documento atual descreve a v1.6 e correções de resolução, timeout, histórico e perda de dispositivo. Também registra `DXGI_ERROR_DEVICE_HUNG` ainda em investigação; a correção de acesso inválido no menu não demonstra correção da perda de GPU original. Os logs sintéticos publicados pelo projeto são evidência de terceiros e não medição da RX 7600. [Notas e limitações](https://github.com/MatheusGViana/dlss-5-amd-project/blob/e58ff27a3d5ac86184875e01b317ff1463a247c1/analysis/LEIA-ME-AMD.md).

## Relação com a pasta do Google Drive

Conferimos os blobs Git contra os arquivos locais, sem baixar os binários remotos. O INI local é byte a byte igual ao INI de um diretório de teste publicado. O apontador de pesos também é igual ao publicado. Nomes, configuração e notas indicam relação com a mesma família de integração; não provam quem distribuiu o arquivo do Drive ou a proveniência exata da DLL.

**As versões binárias diferem:** as DLLs locais `pass1/2/3` têm hash `e145ff963b1ef6146aee39a7b074268eb1b031a94d3aadbbcaf9da37e91fa9ad`; o código atual exige `fe96f5897861602d0b06fe07f7d56ad643a9b963a891e7f60d6a3c1971a21f75`. Não são peças intercambiáveis. [Hash esperado](https://github.com/MatheusGViana/dlss-5-amd-project/blob/e58ff27a3d5ac86184875e01b317ff1463a247c1/OptiScaler-DLSSNR-PreSR-Multipass-main/OptiScaler/dlssnr/amd/RuntimeHash.h).

`prepare_runtime.py` explica que as cópias são derivadas de uma DLL específica, com alterações no instalador de hooks, notificação e fallback de timeout. A leitura desse código esclarece o método de integração, mas não equivale a ter o fonte do motor HIP ou do modelo. O script não foi executado. [Preparação do runtime](https://github.com/MatheusGViana/dlss-5-amd-project/blob/e58ff27a3d5ac86184875e01b317ff1463a247c1/analysis/prepare_runtime.py).

## Decisão técnica

Priorizar o código do Matheus como referência para um experimento separado de **uma passagem**: versão binária correspondente, dimensões ativas, movimento/profundidade, reset e conclusão real precisam ser verificáveis. Antes de colocá-lo no jogo, a avaliação deve cobrir saída, timeout, mudança de resolução, filas e custo de GPU/espera separadamente. O código encontrado torna essa investigação mais concreta; não transforma automaticamente o runtime externo em IA leve nem em sombreamento próprio.

Continuamos com a V2.1 pronta para o teste que o usuário ainda não realizou. A inferência própria já ocorre antes do FSR. Nesta revisão não foi incorporado código de terceiros ao caminho de produção e não houve motivo para repetir os testes GPU da versão instalada.

Referências locais: `artifacts/external-references/github-neural-review/` contém metadados de commit, árvores completas, arquivos de texto conferidos, `download-manifest.json` e `dlss-5-amd-project/local-package-matches.json`. As conclusões se referem às versões acima, não a toda atualização futura dos repositórios.
