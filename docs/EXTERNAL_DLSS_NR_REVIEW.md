# Referência externa: DLSS-NR-on-AMD 0.2.11

Inspeção em 05/09/2026 da pasta fornecida pelo autor e do [repositório público](https://github.com/danielblnc/DLSS-NR-on-AMD).

## O que foi recebido

A pasta `DLSS-NR-on-AMD-0.2.11` contém exatamente dois arquivos: `README.md` (2342 bytes) e `.github/FUNDING.yml`. Não há fontes C++/HLSL/Python, pesos, DLL, instalador ou arquivo de licença nessa pasta. O inventário com SHA-256 foi salvo em `artifacts/temporal-validation/external-prototype-inventory.json`. A pasta foi preservada sem alterações.

A árvore pública consultada também mostrou README e configuração de financiamento. A [release v0.2.11](https://github.com/danielblnc/DLSS-NR-on-AMD/releases/tag/v0.2.11) anuncia um instalador; os binários não foram executados nem inspecionados nesta revisão.

## Alegações e limites

O README local descreve dependência de `nvngx_dlssnr.dll` 310.8.0.0 obtida separadamente, instalação em jogo DX12 com FSR e controles de intensidade. Relata teste em RX 9070 XT e apresenta RX 7000 como não testada. A versão atual do README público sugere suporte esperado à RX 7000, mas isso não é nossa validação da RX 7600.

Não temos evidência independente da qualidade, velocidade, implementação interna ou compatibilidade desse binário. A documentação não fornece uma API para integrar ao OptiScaler. Não há código disponível no material recebido para adaptar, nem licença de código identificada. Portanto, não incorporamos uma implementação externa nem declaramos equivalência com DLSS 5.

## Decisão para nosso projeto

Usar essa referência para planejar comparações controladas e controles de intensidade, mantendo o desenvolvimento verificável do próprio backend. Um eventual teste do instalador externo seria uma avaliação separada, com suas dependências, e não provaria a integração do nosso projeto.

O próximo teste em jogo do nosso código deve primeiro demonstrar saída válida, estabilidade, tratamento de formatos e recuperação para o backend normal. A transformação neural exige um modelo e validação próprios, ainda ausentes. Consulte [o caminho para o primeiro teste](FIRST_GAME_TEST.md).

## Evidência recebida posteriormente

O usuário acrescentou o instalador na raiz. A [inspeção estática do binário e confronto com o dump](DLSS_NR_BINARY_REVIEW.md) confirmou uma DLL embutida, imports HIP/D3D12 e código identificado para gfx1102. Isso amplia a evidência da revisão inicial, sem fornecer código-fonte nem validar execução.
