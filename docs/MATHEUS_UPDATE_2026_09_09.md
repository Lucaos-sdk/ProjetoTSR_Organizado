# Revisão da atualização Matheus — 9 de setembro de 2026

Escopo: leitura estática de fontes e relatos do autor, sem executar instalador,
DLL, HIP ou modelos externos. Não altera a instalação do Witcher 3 nem o README
geral, cuja atualização foi adiada pelo usuário.

Snapshot fixado em [`7b9dcb9c3864fc82fd4e0f7473a6f27c459a0707`](https://github.com/MatheusGViana/dlss-5-amd-project/tree/7b9dcb9c3864fc82fd4e0f7473a6f27c459a0707).
O README declara esta a atualização final desse projeto experimental e considera
uma possível reescrita futura. Não é anúncio de novo modelo treinado validado.
Anteriormente examinamos `e58ff27a3d5ac86184875e01b317ff1463a247c1`.
A API de comparação reporta histórico divergente; a listagem de diferenças
recebida tem limite de 300 arquivos. A árvore completa do novo commit tem 8.091
entradas e não veio truncada. Não tratamos a lista limitada como inventário completo.

Fontes preservadas em `artifacts/external-references/matheus-update-20260909`:
metadados, árvores, comparação e textos com hashes dos blobs Git verificados.
São referências de auditoria, não dependências da nossa DLL.

## Mudanças úteis e seus limites

| Parte | Evidência da atualização | Consequência para nosso projeto |
| --- | --- | --- |
| Ajustes da interface, v2.18 | Adia alterações de sliders até terminar a edição. | Evitar reconstruir recursos a cada posição do mouse quando houver configuração neural dinâmica. |
| Estado, v2.19 | Troca estado `thread_local` por estado protegido no backend compartilhado. | Manter configurações e recursos coerentes quando chamadas vêm de threads diferentes. Não comprova ausência de travamentos. |
| Submissão, v2.21–22 | Liga à fila que submeteu a lista; publica trabalho HIP depois de `ExecuteCommandLists`. Passagem única acompanha conclusão de forma assíncrona. | Reforça observar submissão real e preservar recursos até conclusão. Nosso caminho HLSL já grava na lista do jogo e não usa worker HIP. |
| Espera | Código atual ainda pode aguardar até 16 ms por trabalho já submetido; multipass tem espera sequencial de até 5 s por passagem. | Não copiar essas esperas para nosso caminho de baixa latência. O aumento de 4 para 16 ms busca continuidade, não aceleração de inferência. |
| Compilador, v2.24 | Relata correção de `D3DCompile` privado usando System32 quando o jogo carrega compilador antigo incompatível com o shader FP16. | Útil para futura investigação de compilação dinâmica. O nosso shader incorporado não exige importar patches por endereço privado. Testes isolados do autor não substituem validação no jogo. |
| Codificação, v2.25 | Auto/Linear equivalentes; sRGB e Gamma 2.2 decodificam e recodificam em duas passagens com duas texturas FP16. | Codificação faz parte do contrato de entrada. Não escolher curva para parecer melhor; conversão incorreta altera o comportamento da rede. Não implementa HDR10/PQ ou transformação de gamut. |
| Neural lighting, v2.17 | Ativa flag privada e força Tone; ensaio sintético mede mudança e escurecimento. | Não demonstra relighting correto, isolamento de pele, restauração de kernels ausentes ou equivalência NVIDIA. |

O runtime depende de hash e endereços internos conhecidos. A existência de fonte
da ponte não equivale a uma API estável nem a pesos treinados e reproduzíveis.
As notas v2.24/v2.25 distinguem testes isolados de confirmação visual em jogos;
mantemos essa distinção. Relatos de teste são do autor, não testes feitos aqui.

## Iluminação espacial: documentação versus execução ativa

O texto `analysis/rtgi-native/STATUS.md` descreve uma cadeia de profundidade,
radiância, traçado, histórico temporal, denoise e fusão. No commit analisado,
`RtgiNative.cpp` carrega e executa somente `GatherCS` e `ResolveCS`, a partir de
`experimental_lighting`. Exige um ponteiro de movimento não nulo, mas essas
duas chamadas não vinculam o buffer de movimento. `validHistory` não prova
execução de acumulação temporal. Há shaders históricos no repositório que não
estão conectados a este caminho.

O [shader ativo](https://github.com/MatheusGViana/dlss-5-amd-project/blob/7b9dcb9c3864fc82fd4e0f7473a6f27c459a0707/analysis/experimental-lighting/Lighting.hlsl)
reconstrói posições e normais de profundidade, amostra uma espiral determinística
de 8 a 40 vizinhos, estima contribuição local e oclusão, filtra por distância
de profundidade e compõe com a cor. Usa FOV e plano distante configurados, com
modelo de profundidade próprio. Não é uma rede neural nem geometria completa;
não recupera informação fora da tela. Não basta transplantar seu modelo de
câmera para a profundidade do Witcher 3.

Ideias aproveitáveis: filtrar sinais de iluminação separadamente da cor,
respeitar descontinuidades de profundidade, limitar alcance e distinguir entradas
de cor codificada/linear. Implementação própria do experimento de bordas usa
essas restrições gerais e a formulação pública de filtro guiado; não copiamos
código, patches, shaders ou pesos desse pacote para a DLL.

## Decisão aplicada nesta rodada

Prosseguir na ablação offline: grade original, loss de bordas, filtro guiado e
combinação, mais rede ponto a ponto com capacidade equivalente. Todas recebem
o mesmo conjunto novo de cenas, exposição regional e ruído de geometria. Medir
erro de cor, bordas, regiões protegidas e erro reprojetado; não escolher apenas
pela impressão de nitidez. Resultados em `artifacts/bilateral-edges-v2`.

A V2.3 instalada permanece a referência de jogo. Integração neural depende de
benefício visual e temporal sustentado e de medição real textura→tensor→rede→
composição na RX 7600. Não há inferência DirectML ou promessa sub-ms nesta rodada.

Referências específicas: [v2.17](https://github.com/MatheusGViana/dlss-5-amd-project/blob/7b9dcb9c3864fc82fd4e0f7473a6f27c459a0707/analysis/README-v217.md),
[v2.22](https://github.com/MatheusGViana/dlss-5-amd-project/blob/7b9dcb9c3864fc82fd4e0f7473a6f27c459a0707/analysis/README-v222.md),
[v2.24](https://github.com/MatheusGViana/dlss-5-amd-project/blob/7b9dcb9c3864fc82fd4e0f7473a6f27c459a0707/analysis/README-v224.md),
[v2.25](https://github.com/MatheusGViana/dlss-5-amd-project/blob/7b9dcb9c3864fc82fd4e0f7473a6f27c459a0707/analysis/README-v225.md),
[AmdPreSr.cpp](https://github.com/MatheusGViana/dlss-5-amd-project/blob/7b9dcb9c3864fc82fd4e0f7473a6f27c459a0707/OptiScaler-DLSSNR-PreSR-Multipass-main/OptiScaler/dlssnr/amd/AmdPreSr.cpp).
