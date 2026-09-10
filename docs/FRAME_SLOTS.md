# Controle de recursos por quadro e fence

Foi implementado `FrameSlots`, um controlador independente de API para reservar conjuntos de recursos e impedir reutilização antes da conclusão GPU. Ele usa uma linha de tempo monotônica dedicada ao contexto; as chamadas precisam ser serializadas pelo chamador.

## Regras

Atualização: `GpuFrameContext` usa o modo com retenção por gravação, que exige
`EndRecording` além da conclusão GPU. `Replay` estende as fences sem trocar o
histórico atual. O modo padrão do teste legado abaixo continua sendo envio
único. Veja [reexecução temporal](TEMPORAL_REPLAY.md).

- Uma reserva identifica conjunto, geração e contexto. Reservas antigas ou de outro contexto são rejeitadas.
- Há no máximo uma reserva em gravação. Submissões anteriores podem continuar em execução.
- O histórico mais recente fica reservado mesmo depois de sua produção terminar, pois o próximo quadro poderá consumi-lo.
- Ao confirmar um quadro que lê histórico, a fence de liberação do conjunto anterior é estendida até a conclusão desse consumidor. A conclusão da escrita anterior, sozinha, não permite sobrescrita.
- Sem conjunto livre, Acquire retorna indisponível; não espera, aloca ou sobrescreve recursos. Esperar ou adiar é decisão do chamador.
- Cancel só é válido depois de descartar comandos ainda não submetidos. Commit deve receber o valor de uma fence que cubra todos os usos dos recursos; um erro após submissão não pode ser tratado como simples cancelamento.
- Para redimensionar ou destruir recursos, é necessário não haver reserva aberta e todos os consumidores terem concluído. O controlador não libera recursos por conta própria.
- Valor de conclusão UINT64_MAX é tratado como remoção de dispositivo. Valores regressivos ou incompatíveis com a linha de tempo são rejeitados.

## Exercício DX12

`tsr_frame_slots_dx12` usa três conjuntos persistentes. Cada conjunto tem allocator/list, descriptor heap, texturas de cor/geometria e buffers de diagnóstico. Executa nove quadros e reutiliza os conjuntos seis vezes, sem recriar esses objetos GPU.

O teste suspende temporariamente a fila com uma fence de controle, submete três quadros e exige que uma quarta reserva seja recusada. Depois libera a GPU, aguarda a conclusão e continua reutilizando os conjuntos. Isso força a condição de ocupação e evita que o teste dependa de a GPU ser lenta o suficiente.

A saída temporal e sua geometria são comparadas à referência CPU antes de reutilizar cada buffer de diagnóstico e no encerramento. A exposição alterna entre quadros e há reset explícito, tornando o uso incorreto do histórico observável. O teste drena a fila antes de destruir os recursos.

## Evidência

- Testes CPU: vida útil do consumidor, conjunto ocupado, cancelamento, reserva antiga, contexto errado, fence inválida e liberação após conclusão.
- RX 7600 com debug layer: nove quadros em resolução nativa 1920×1080, três conjuntos e seis reutilizações passaram na comparação CPU. A reserva prematura foi recusada conforme esperado.
- Log de hardware: `artifacts/temporal-validation/frame-slots-rx7600.log`.
- Executar: `tsr_frame_slots_dx12.exe --debug --1080p` ou `--warp --debug` para o cenário pequeno.

Build Release concluído; 28/28 testes CTest passaram, incluindo controle CPU e execução WARP. Log: `artifacts/temporal-validation/ctest-frame-slots.log`.

## Limites e próximo passo

Este teste exercita o passe temporal com entradas preparadas, não a cadeia completa de conversão/saída/regiões. Há readbacks de diagnóstico e esperas do harness; não é medição de desempenho ou execução dentro do jogo. Três quadros submetidos não significam três dispatches executados em paralelo.

O controlador ainda não está conectado ao OptiScaler. É preciso reunir conversão, temporal e saída em um contexto GPU, incluir os recursos externos na política de vida útil e integrar a fence real do chamador. O fork recebe a command list do jogo, portanto não basta copiar a fila e as esperas usadas neste teste. Formatos/dados reais e câmera móvel permanecem pendentes.
