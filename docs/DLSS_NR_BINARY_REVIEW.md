# Inspeção estática do instalador externo

Data: 05/09/2026. O executável fornecido pelo usuário foi lido como dados. Não foi executado, instalado, carregado como DLL ou colocado em uma pasta de jogo. Nenhum peso NVIDIA foi extraído. A inspeção anterior de uma pasta contendo somente README permanece válida para aquela pasta; esta revisão acrescenta evidência do executável recebido depois.

## Identificação local

- Arquivo: `dlssnr_on_amd_setup.exe`, 4.737.763 bytes.
- SHA-256: `dbf7e40ca6940924ffe0960b350f3d55cb99ed27dde84aec47cdf8a0b7303a74`.
- Authenticode: não assinado, conforme Get-AuthenticodeSignature. Isso isoladamente não determina segurança ou origem.
- Relatório reproduzível: `artifacts/temporal-validation/dlssnr-setup-static.json`.
- Ferramenta própria: `tools/inspect_pe_static.py`; lê cabeçalhos PE, imports, diretório de offload e strings selecionadas, sem executar o arquivo.

## Verificado diretamente no arquivo

| Evidência | Resultado | Limite |
|---|---|---|
| Cabeçalhos PE x64 | EXE no início; DLL embutida em 0x3A800 | Não prova como o instalador a utiliza |
| Imports da DLL embutida | D3D12, DXGI, D3DCompiler e amdhip64_7.dll | Importar uma função não prova sua execução |
| APIs HIP importadas | Importação/mapeamento de memória externa, lançamento de kernels, eventos e sincronização | Não demonstra a ordem ou correção da sincronização |
| Bundle Clang em 721920 | Quatro entradas com cabeçalho ELF, destinadas a gfx1100/1101/1102/1201 | Não valida instruções nem execução de cada kernel |
| Entrada gfx1102 | 1.183.864 bytes, offset absoluto 3.085.312 | Alvo compilado não equivale a desempenho ou funcionamento validados |
| Mensagens internas | Pesos externos, geometria/movimento/exposição, interop e modos inline/assíncrono | Strings são pistas; não constituem rastreamento em execução |

A [documentação da AMD](https://rocm.docs.amd.com/projects/install-on-windows/en/latest/reference/system-requirements.html) associa RX 7600 a gfx1102. Portanto, há código identificado para o alvo da placa do usuário. Não concluímos que a rede execute corretamente ou em velocidade útil nela.

## Confronto com o dump de terceiros

O [README do dump](https://github.com/0x-punk/DLSS-NR-on-AMD-DUMP-) declara análise da Alpha 0.2.10, com 4.789.475 bytes e SHA-256 `5a28d907bba471504c6c7be6901a4eb6610f46b4d86ecb23b0430ba8bf3fc5d5`. É outro arquivo. Nosso exemplar confirma algumas características estruturais descritas, mas não autoriza transferir todas as conclusões.

O [texto sobre o caminho HIP](https://github.com/0x-punk/DLSS-NR-on-AMD-DUMP-/blob/master/hip_processing_path.md) menciona ainda outro tamanho (4.689.622 bytes) e diz que HIP é resolvido dinamicamente, sem import estático. No exemplar local, a tabela da DLL embutida contém imports estáticos de amdhip64_7.dll. Isso ressalta a necessidade de vincular cada conclusão ao arquivo exato.

O dump descreve uma rede e faz afirmações fortes de funcionamento e segurança. A leitura de cabeçalhos, símbolos e mensagens não basta para verificar identidade do modelo, precisão numérica, grafo completo, ausência de comportamento malicioso, qualidade visual ou FPS. Essas conclusões não foram adotadas por esta revisão.

## Aplicação ao nosso projeto

1. Manter o backend temporal DX12/OptiScaler como caminho principal verificável e orientado a múltiplos fabricantes.
2. Considerar HIP como backend neural opcional para AMD, caso um experimento futuro demonstre benefício. Não tornar o projeto inteiro dependente de HIP apenas porque esse binário o usa.
3. Projetar importação de recursos, identidade de device, estados, vida útil e sincronização explicitamente. O executável fornece pistas de interop; a implementação deve seguir documentação oficial e testes próprios, sem copiar esperas ativas de GPU a partir de strings.
4. Manter um teste neural separado do primeiro teste temporal em jogo. Um modelo treinado e compatível continua necessário; o instalador não fornece uma API pública ou código-fonte que possamos simplesmente ligar ao nosso backend.

Esta descoberta justifica investigar mais o caminho HIP, mas não elimina os bloqueios descritos em [primeiro teste em jogo](FIRST_GAME_TEST.md), nem fornece uma data de entrega ou equivalência com DLSS 5. Nenhum código binário, shader ou peso de terceiros foi incorporado ao projeto.

## Verificação da análise

O hash permaneceu igual após a inspeção. O parser rejeitou amostras vazias/truncadas básicas; os dois cabeçalhos, quatro entradas ELF e import HIP foram conferidos no relatório. Não se trata de auditoria completa de segurança nem de decompilação. Como não houve mudança no processamento gráfico, a suíte GPU não foi repetida nesta etapa.
