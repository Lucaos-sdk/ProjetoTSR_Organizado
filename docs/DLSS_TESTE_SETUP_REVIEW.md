# Arquivos gerados pelo setup — 08/09/2026

O usuário informou que executou o instalador para estudar os arquivos e forneceu um print. A tela indica `Palworld\Engine\Binaries\Win64` como destino, informa instalação de `version.dll` e configuração e depois pede `nvngx_dlssnr.dll`. As verificações abaixo foram somente de leitura; nenhum instalador, DLL ou script externo foi executado pelo assistente.

## Estado encontrado

As pastas `C:\Users\Administrator\Downloads\dlss teste` e `C:\Users\Administrator\Documents\ProjetoTSR_Organizado\dlss teste` contêm instalador, `version.dll`, `dlssnr_on_amd.ini` e `dlssnr_on_amd.log` vazio. Os arquivos gerados têm data local 07/09 às 23:58.

Na pasta `C:\Program Files (x86)\Steam\steamapps\common\Palworld\Engine\Binaries\Win64` também existem `version.dll`, `dlssnr_on_amd.ini` e log vazio, datados de 08/09 às 00:03. Os hashes da DLL e do INI são iguais aos das cópias de estudo. Portanto, houve cópia desses arquivos para uma pasta do Palworld, apesar de a intenção relatada ser testar fora dos jogos. Não foi observado carregamento por um processo do jogo. A presença dos arquivos e o reconhecimento da GPU não validam inferência.

Não foram removidos arquivos do Palworld, nem revertidas permissões: não há registro anterior nesta tarefa que permita reconstruir todo o estado daquela pasta. O instalador diz no print que concedeu acesso de escrita ao INI/log; essa mensagem não é uma auditoria das ACLs anteriores/atuais.

## Identificação do novo exemplar

| Arquivo | Bytes | SHA256 |
| --- | ---: | --- |
| `dlssnr_on_amd_setup.exe` | 7.401.714 | `66b910da005e000070256b55c41dd5d0e1ae1ea6a8c3a9d0b76edc1749438092` |
| `version.dll` | 7.161.856 | `8230dd8b4687914ad95fe1fc4d9df7aadfca8243d7dcded87b02ba34dbfde592` |
| `dlssnr_on_amd.ini` | 210 | `175c1ca1644a2e1cad9663ff201d47aa8b5b9b4379b1655bc5a19fb1e145feb8` |

A DLL importa HIP 7/D3D12/DXGI e contém código identificado como gfx1102, entre outros alvos AMD. Não é o mesmo exemplar de 7.156.224 bytes analisado anteriormente. Também não tem o hash aceito pela ponte AMD no commit de MatheusGViana revisado. Não é válido transferir automaticamente RVAs, patches ou conclusões de execução entre esses exemplares.

O INI habilita entrada FSR, profundidade, interop, processamento inline e temporal. `InlineWaitMs=200` é uma configuração, não uma medição de custo. Não há arquivo de pesos nem `nvngx_dlssnr.dll` nas duas pastas de estudo inventariadas. O arquivo de pesos de 148 MB recebido anteriormente permanece em `Arquivos necessarios`; não foi copiado para o jogo nem validado com este novo runtime.

## Projeto próprio

A `dxgi.dll` instalada no Witcher foi conferida e mantém o hash da V2.1: `211a8296918fca4bf2237636bd5948ad68f996ef662c58f581d5f44a1426fb63`. O usuário ainda não realizou seu teste em jogo dessa versão. Esta inspeção não alterou o caminho gráfico próprio nem introduziu o runtime externo.

Evidências: `artifacts/temporal-validation/dlss-teste-inventory.json` e `dlss-teste-runtime-static.json`, este último produzido por `tools/inspect_pe_static.py`. A nova DLL é material adicional para análise de compatibilidade; ainda não fornece uma API estável ou comprovação de qualidade/desempenho.
