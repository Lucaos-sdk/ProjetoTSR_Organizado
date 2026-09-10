# Reexecução temporal em uma fila

## Alteração

O contexto GPU agora mantém reservas por gravação, além das fences. A gravação
reserva o próprio conjunto e, quando lê histórico, também o conjunto de origem.
Concluir a GPU não basta para reutilizar esses conjuntos: enquanto uma lista
puder executar novamente, os descritores e texturas referenciados permanecem
reservados. O armazenamento desse controle é criado junto com o contexto.

`EndRecording(lease)` remove essas reservas após Reset/destruição. A fence
continua protegendo qualquer uso em voo. `Replay(lease, fence)` estende a
proteção do destino e da origem, sem trocar o histórico mais recente nem
avançar os parâmetros do quadro. Tickets encerrados/antigos são rejeitados.
`SafeToDestroy` exige tanto conclusão GPU quanto encerramento das gravações.

`GpuFrameContext` sempre habilita esse modo. O teste antigo de `FrameSlots`
mantém o modo padrão sem retenção de gravações, para seu contrato de envio
único; esse modo não serve para reenvio pelo jogo.

As quatro texturas internas começam e terminam cada gravação em COMMON.
O histórico lido também retorna a COMMON. Assim, executar novamente a mesma
sequência usa estados internos compatíveis, inclusive na primeira gravação.
O teste restaura também sua saída externa para UAV depois da cópia diagnóstica.

O registro de submissão chama `RecordingEnded` após Reset/destruição de uma
gravação submetida; chama `Discarded` apenas se ela nunca foi enviada.

## Evidência

O teste integrado executa nove quadros lógicos, cada um duas vezes, mais um
reenvio de um quadro antigo depois de outro mais recente: 19 execuções.
Verifica a imagem final contra a referência CPU, exposição alternada, reset,
retenção após conclusão GPU, descarte, destruição real e seis reutilizações.
As três primeiras execuções ficam bloqueadas por uma fence de controle.
Antes de reenviar a mesma lista, o teste aguarda sua execução anterior terminar;
reenvio da mesma lista ainda em voo não é permitido pelo DX12.

- 32/32 testes passaram: `artifacts/temporal-validation/ctest-replay.log`.
- A suíte final inclui debug layer nos dois testes WARP do contexto GPU.
- RX 7600, debug layer, 1280×720 → 1920×1080, FP16 e FP32:
  `gpu-replay-fp16-rx7600.log` e `gpu-replay-fp32-rx7600.log`.
- Fork recompilado: `optiscaler-build-replay.log`. Persistem os avisos e as
  dependências ausentes do empacotamento descritos na integração OptiScaler.

## Contrato ainda obrigatório para o adaptador de jogo

A primeira tentativa de reenviar listas ainda em voo foi rejeitada pela debug
layer da RX 7600. O harness foi corrigido para aguardar a execução anterior,
e os testes WARP passaram a usar debug layer explicitamente. Os logs de
hardware citados acima são da versão corrigida.

O teste usa uma fila serializada e entradas externas estáveis. O chamador deve
garantir que a execução anterior da mesma lista já concluiu, além de
garantir a fila correta ANTES de Execute, estados externos compatíveis em cada
execução, ordem dos produtores/consumidores e manutenção dos recursos/heaps.
A checagem de fila no callback do teste é diagnóstica: ocorre depois de Execute
e não pode corrigir um envio errado. Não há suporte a filas concorrentes.

O contexto não possui a fila nem sua fence. O futuro backend precisa manter o
contexto vivo após sua desativação até encerrar gravações e concluir todas as
fences. Retenção do callback no registro não substitui essa propriedade; o
callback do teste referencia um contexto cujo tempo de vida é garantido pelo
harness. O teste não valida mudanças dos dados externos entre reenvios.

Ainda faltam o adaptador de parâmetros reais, a seleção experimental no
OptiScaler, instalação dos hooks independente de FG e teste dentro do jogo.
Esta etapa não implementa iluminação/material neural.
