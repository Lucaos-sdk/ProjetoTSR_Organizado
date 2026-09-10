# Pacote externo AMD Pre-SR Multipass v1.2

**Atualização posterior:** o usuário forneceu um repositório com código da ponte AMD e indícios de relação com este pacote. Veja a [revisão dos quatro repositórios](NEURAL_REPOSITORIES_REVIEW.md). A inspeção abaixo descreve o material local recebido inicialmente; a descoberta de código da ponte não inclui o fonte completo do modelo/runtime, e o hash exigido pelo código mais recente difere das DLLs locais.

Inspeção local em 07/09/2026 da pasta `Arquivos necessarios`. O usuário esclareceu que obteve o material no Google Drive; não forneceu um repositório de origem. O rótulo do pacote e as mensagens de versão não confirmam autoria ou procedência. Os scripts foram lidos como documentos, não executados. Nenhuma DLL foi carregada e nenhum arquivo do jogo foi alterado nesta revisão.

## Conteúdo verificável

| Arquivo | Resultado |
| --- | --- |
| `OptiScaler-AMD-PreSR-Multipass-v1.2/OptiScaler.dll` | PE x64, 25.889.280 bytes; versão interna `10.0.0-dev (amd-presr-multipass-local) (20260907_075847)`; SHA256 `07a1e2ca3fbf6c9c9a2923a755603c69fabf115b0904c92f10efe95fdb2b0caa` |
| `dlssnr_amd_pass1.dll`, `pass2`, `pass3` no pacote | Três cópias idênticas, 7.156.224 bytes cada; SHA256 `e145ff963b1ef6146aee39a7b074268eb1b031a94d3aadbbcaf9da37e91fa9ad` |
| `Arquivos necessarios/dlssnr_on_amd_weights.bin` | 147.689.451 bytes; SHA256 `6bf8dc931ef3ccffe18c82de26ab374156e7f19539ffcf8eabaa25dca5cf15ab`, igual ao valor esperado pelo pacote |
| Arquivo de mesmo nome dentro do pacote | Apenas 134 bytes: apontador Git LFS que referencia o hash/tamanho do arquivo completo acima |
| `dinput8.dll` na pasta superior | Exporta `DirectInput8Create` e funções ImGui; contém referências a REFramework. Isso não estabelece sua necessidade para o Witcher |
| `dlss-enabler-headless.dll` na pasta superior | Metadados identificam DLSS Enabler 4.9.0.15, com exports de interposição gráfica/NGX; não demonstra integração com o modelo próprio |

A tabela inferida do arquivo grande começa com `DLSSNRW1`, declara 153 entradas e termina no byte 5.673. Os intervalos declarados cabem no conteúdo restante; o maior fim é exatamente 147.683.778 bytes, o tamanho desse conteúdo. Isso verifica estrutura básica e correspondência ao hash fornecido. Não verifica grafo, tipos de tensores, origem dos pesos ou identidade de um modelo NVIDIA. O arquivo não foi desserializado em um runtime neural.

Das 24 entradas de `SHA256SUMS.txt`, 19 conferem. Divergem o apontador dos pesos, `LEIA-ME-AMD.md` e três licenças (`FidelityFX_v1_LICENSE.md`, `FidelityFX_v2_LICENSE.md`, `XeSS_LICENSE.txt`). A causa das diferenças dos documentos não foi determinada. Conferir esse manifesto não autentica a origem, pois veio junto com o pacote.

## O que o material indica sobre a integração

O INI ativa `[DlssNr] Enabled=true`, `RunBeforeSR=true`, `Passes=1`, `LocalTone=0`, `LocalStructure=1`, `SkinStructure=1`, `ApplyAfterRR=false`. Mensagens internas do OptiScaler descrevem resolução interna antes do SR, histórico temporal independente por passagem, espera pela fila do jogo e notificação de envio. Também indicam recusas para vetores de movimento na resolução de saída e origem de sub-região de cor diferente de zero. São pistas estáticas, não execução observada.

**Multipass não significa geração de múltiplos quadros.** Neste pacote, a configuração e as mensagens descrevem reaplicar o processamento neural, com instâncias/históricos independentes. O próprio texto interno informa aumento do tempo de GPU e memória com mais passagens. O fato de haver três DLLs não demonstra três modelos diferentes; seus bytes são idênticos.

A DLL de passagem importa `amdhip64_7.dll`, D3D12, DXGI e D3DCompiler. Contém bundles ELF identificados como gfx1100/1101/1102/1200/1201 e alvos genéricos gfx9/gfx10-3/gfx11. Há portanto código identificado para gfx1102, arquitetura da RX 7600 já documentada na revisão anterior. Compatibilidade e desempenho nessa GPU continuam sem teste. HIP 7 foi localizado em `C:\Windows\System32\amdhip64_7.dll`; sua presença não valida o backend.

Os 17 exports nomeados da DLL de passagem são funções de proxy de `version.dll`. Não foi encontrada uma API neural pública nesses exports, nem headers ou fontes de implementação na pasta. O pacote OptiScaler pode usar outra forma de acesso interno; a leitura de strings não determina seu contrato. Não há base para chamar endereços internos ou mapear os pesos diretamente no nosso shader de 171 parâmetros.

## Implicações para o projeto

1. O material é mais útil que o instalador/README recebidos antes: agora há o arquivo completo de pesos e um binário que descreve integração antes do SR. Pode servir como referência para uma avaliação comparativa do processamento neural externo.
2. Nosso passe já executa na resolução interna antes do FSR. A nova evidência não exige mudar sua posição. Históricos independentes, dimensões de vetores de movimento, identidade da fila, estados e vida útil dos recursos são contratos relevantes para um backend neural maior.
3. Este arquivo de aproximadamente 148 MB não substitui os pesos da nossa MLP. O tamanho não determina latência, memória de execução ou qualidade. Um teste isolado de uma passagem precisaria medir esses resultados na RX 7600 antes de avaliar duas ou três.
4. Para adaptar a integração, falta fonte ou contrato verificável do backend; para reutilizar o modelo, faltam arquitetura e termos de uso identificados. As licenças agrupadas de OptiScaler/AMD/Intel não estabelecem, por si, os termos dos pesos externos.
5. O instalador incluso copiaria o apontador de 134 bytes para o jogo e substituiria nossa DLL, INI e bibliotecas de terceiros. Ele não busca automaticamente o arquivo completo na pasta superior. Por isso não é uma atualização utilizável diretamente sobre a V2.1. Se houver uma futura avaliação externa, ela deve usar o arquivo completo já conferido e um pacote de comparação com restauração e configurações verificadas.

A revisão não demonstrou sombreamento novo, desempenho, ausência de falhas ou equivalência com DLSS 5. Também não acrescentou esse modelo à DLL própria. A V2.1 instalada permanece a versão testada anteriormente.

## Evidências

- `artifacts/temporal-validation/external-presr-inventory.json`: caminhos, tamanhos e hashes dos 29 arquivos recebidos.
- `external-presr-pass-static.json`, `external-presr-optiscaler-static.json`: cabeçalhos PE, imports e bundles, produzidos por `tools/inspect_pe_static.py`.
- `external-presr-abi.json`: exports nomeados, mensagens selecionadas e comparação completa do manifesto.
- `external-presr-integration-strings.json`: mensagens de integração em ASCII/UTF-16.
- `external-presr-weights-structure.json`: tabela de intervalos inferida, sem carregar o modelo.

Não houve mudança de processamento gráfico; testes GPU não foram repetidos para esta inspeção.
