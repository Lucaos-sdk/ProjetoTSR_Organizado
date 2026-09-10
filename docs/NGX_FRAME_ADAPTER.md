# Preparação dos parâmetros NGX para TSR

`ReadNgxFrame` lê a interface virtual `NVSDK_NGX_Parameter` do SDK já incluído
no fork. `PrepareTsrNgxFrame`, compilado dentro da DLL, oferece essa preparação
com retorno falso e motivo quando os metadados não atendem ao contrato.
Nenhuma dessas funções grava comandos, altera NGX, avança histórico ou escolhe
o upscaler. A saída da preparação no fork só é atribuída após sucesso.

## Mapeamento

| Entrada | Tratamento |
| --- | --- |
| Render subrect | Dimensões dinâmicas; se o par inteiro faltar, usa dimensões confirmadas na criação. Par incompleto é recusado. |
| Saída | Tamanho informado pelo contexto de integração, independente de 1080p. |
| Cor, profundidade, movimento, destino | Get de recurso DX12, com alternativa void*. Ausência, nulo e alias direto com destino são recusados. |
| Jitter | Pixels de renderização, finitos no intervalo suportado ±0,5. |
| MV scale | Escalas explícitas, finitas; vetores em resolução de saída só são aceitos quando ela coincide com a entrada. |
| MV jittered | Exige correção explícita fornecida pela integração. Não calcula a correção a partir apenas do jitter atual. |
| Pré-exposição | Escalar positivo explícito; mantém a convenção de cor pré-exposta. Não lê exposure texture nem calcula exposição automática. |
| Regiões | Origens comuns para cor/profundidade/movimento; origem de saída independente. Origens diferentes nas entradas são recusadas. |
| Reset | Reset do jogo, primeiro quadro ou modo sem histórico. |

Formatos, dimensões físicas, capacidade de UAV, bounds das regiões, dispositivos
e estados reais ainda são responsabilidades da validação GPU e da integração.
Os ponteiros resultantes são emprestados; a preparação não os retém.

## Informações que não podem ser adivinhadas

A integração deve confirmar cor linear e profundidade linear ou fornecer os
coeficientes explícitos de `depth = A + B / Z`. O sinal/forma desses coeficientes
inclui a convenção de profundidade invertida; o flag sozinho não fornece near/far.
O adaptador não copia planos de câmera padrão do backend FSR existente.

Sem câmera fixa confirmada, o adaptador marca **somente quadro atual**, força
reset e produz geometria sem profundidade anterior prevista. Isso permite
testar entrada/saída sem inventar informação temporal. Não é uma substituição
de qualidade para FSR: a reconstrução temporal em câmera móvel continua
dependendo de um caminho validado para reprojeção da profundidade anterior.

`fixedCameraConfirmed` só está habilitado na cena sintética conhecida dos
testes. Não deve ser habilitado globalmente para jogos. O índice do quadro é
fornecido pelo contexto e deve avançar na primeira submissão bem-sucedida,
nunca em Evaluate malsucedido ou em reenvio.

## Testes e estado

Validação local: **34/34 testes aprovados**. Na RX 7600, os modos temporal FP16,
temporal FP32 e somente quadro atual FP32 passaram em 1280×720 → 1920×1080,
com debug layer e comparação CPU. O teste usa parâmetros NGX sintéticos e
texturas GPU reais; não constitui captura ou avaliação de um jogo.

O teste CPU implementa a interface virtual NGX real com armazenamento de teste,
sem DLL NGX externa. Cobre dados ausentes/inválidos, pares incompletos, movimento
em resolução incompatível, jitter, exposição, modo conservador, projeção,
fallback de ponteiro e ausência de alterações nos parâmetros durante a leitura.

O harness GPU passa as texturas reais e parâmetros dessa interface pelo
adaptador antes de `GpuFrameContext::Record`. Compara a saída com a referência
CPU nos modos temporal sintético e somente quadro atual, mantendo os testes de
descarte, destruição e 19 execuções para nove quadros lógicos.

Logs em `artifacts/temporal-validation/`: `ctest-ngx.log`,
`optiscaler-build-ngx.log`, `gpu-ngx-fp16-rx7600.log`,
`gpu-ngx-fp32-rx7600.log` e `gpu-ngx-current-rx7600.log`.

O backend ainda não foi registrado no menu/factory nem executado em jogo. Ainda
faltam integrar propriedade do contexto/fence, estados das texturas do jogo e
instalar os hooks mesmo sem FG. Permanecem os avisos de empacotamento do fork.
Esta preparação não contém modelo neural ou transformação de iluminação.
