# Primeiro diagnóstico ativo no The Witcher 3

Execução de 2026-09-06, aproximadamente 13:50–13:54, RX 7600, DX12.
A entrada do jogo foi XeSS Qualidade e o backend selecionado foi TSR diagnostics,
que renderizou por FSR 2.1.2. O encerramento registra DLL_PROCESS_DETACH.

## Resultado

- 30 snapshots `TSR_PROBE v1`; os 30 resultados de Evaluate amostrados retornaram true.
- Zero linhas `fsr_evaluate_returned=false`, zero registros `[E]` e zero exceções Streamline.
- Há 27 linhas de aviso, incluindo banners, componentes opcionais ausentes,
  limitações DLSS e avisos de entrada do menu. Não equivalem a 27 falhas de renderização.
- Renderização informada: 1280×720; destino/display: 1920×1080.
- A captura alcança evaluation=15000; isso não é uma medição de FPS ou latência.

O teste confirma que a entrada XeSS chega ao backend de diagnóstico em jogo.
Não valida qualidade do TSR próprio, inferência neural ou ausência de artefatos:
não houve comparação pixel a pixel da saída do jogo, e Evaluate true é o retorno
do backend, não uma leitura validada da GPU.

## Contrato observado

| Recurso/dado | Observação |
| --- | --- |
| Cor e saída | RGBA16F (DXGI 10) |
| Movimento | RG16F (DXGI 34), escala informada 1280/720 |
| Profundidade | R32G8X24_TYPELESS (DXGI 19), recurso com depth/stencil |
| Tamanhos físicos de entrada | 1920×1080 em amostras iniciais e 1280×720 em outras; destino 1920×1080 |
| Regiões amostradas | Origem zero, render subrect 1280×720 |
| Flags de criação | 75: HDR, MVLowRes, DepthInverted e AutoExposure |
| Pré-exposição NGX | Ausente nas amostras |
| Exposure texture | Ausente nas amostras |
| Near/far FSR | Ausentes nas amostras |
| Estados configurados | -1/ausentes; não há medição dos estados reais |

A tipagem DXGI foi conferida no header do Windows SDK local. O contexto TSR
atual aceita profundidade R32_FLOAT, não este recurso tipado genericamente com
stencil. Será necessário validar uma SRV adequada e as transições por plano,
além de tratar a convenção de profundidade/exposição da entrada XeSS. Não usar
valores inventados apenas para satisfazer o adaptador. As dimensões físicas
variáveis confirmam a necessidade de respeitar subrects e reinicialização.

## Evidência preservada

- `artifacts/temporal-validation/witcher-xess-probe-success.log`
- `artifacts/temporal-validation/witcher-xess-probe-summary.json`
- SHA256 do log: `0538E2CF3187E3B8BAC2EDF65CAFDDAA261B350DB74E71C3D61C933F30CC1AD6`

Nenhuma configuração do jogo foi alterada durante esta análise.
