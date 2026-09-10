# Recursos persistentes no protótipo DX12

Esta etapa prepara a execução temporal para uma futura integração ao OptiScaler. Não altera o fork nem ativa a engine antiga.

Cada instância agora reutiliza texturas de entrada/saída, buffers de upload e readback, descriptor heap, command allocator/list, fence, evento e queries de timestamp. O histórico já era persistente. O valor da fence aumenta a cada submissão; a rotina espera sua conclusão antes de regravar comandos, descritores ou dados. Há apenas um quadro em execução por instância, sem paralelismo entre quadros.

Mudanças de resolução recriam apenas os recursos dependentes do tamanho. Alterar a resolução de entrada mantendo a saída fixa conserva o armazenamento do histórico, mas invalida seu conteúdo lógico. Resets e saltos de índice também invalidam o histórico sem exigir nova alocação quando os tamanhos são iguais.

As leituras de diagnóstico do histórico foram movidas para depois da reconstrução espacial. Assim, os dois shaders do modo bilinear podem consumir o resultado na GPU antes dessas transferências. O número de cópias de imagens permanece igual: o teste ainda valida todos os quadros contra a CPU. As cópias só devem sair do caminho normal quando a interface de entrada/saída GPU estiver separada da rotina de diagnóstico.

O CSV agora registra `resource_creations`: chamadas de criação de texturas/buffers GPU naquele quadro, incluindo transferências e timestamps. Não conta objetos COM de controle nem alocações CPU. O benchmark exige zero novas criações após o primeiro quadro, com resolução fixa. As suítes também exigem zero novas criações em quadros consecutivos de mesmo tamanho, inclusive resets.

## Validação

- Build Release e 20/20 testes CTest passaram.
- Suítes completas temporal/bilinear e histórico na saída passaram na RX 7600 com debug layer, incluindo saída 4K.
- Novo caso cobre resolução de entrada dinâmica com saída fixa, retorno ao tamanho anterior e continuidade após a mudança.
- Logs preservados em `artifacts/temporal-validation/ctest-persistent.log` e `persistent-*-debug.log`.

Ainda há uploads, readbacks, comparação CPU e sincronização por quadro. Não representa um plugin de jogo pronto, FSR 4.1 modificado, inferência neural ou transformação de materiais/iluminação.

## Medição em 1080p

RX 7600, entrada 1280×720 e saída 1920×1080, Release sem debug, 10 aquecimentos + 50 amostras por modo. Todas as saídas passaram na comparação GPU/CPU; todos os quadros após o primeiro tiveram zero novas criações de recursos GPU.

| Modo | Shaders p50 / p95 (ms) | Lote GPU com cópias p50 / p95 (ms) | Host com validação p50 / p95 (ms) |
|---|---:|---:|---:|
| Histórico na saída | 0.656 / 0.744 | 9.713 / 10.968 | 271.664 / 291.953 |
| Temporal + bilinear | 0.484 / 0.570 | 8.551 / 9.598 | 159.449 / 194.298 |

A medição anterior do shader de histórico na saída foi 0,663 / 0,742 ms; a atual é praticamente equivalente. Não há evidência de ganho relevante de tempo GPU nesta sessão. O benefício verificado é a reutilização de armazenamento e objetos, necessária para separar o processamento de sua instrumentação. As amostras continuam serializadas por validação CPU, com intervalos ociosos; não comprovam desempenho sustentado no jogo.

Dados brutos e logs: `artifacts/temporal-validation/persistent-*-1080p.*`; resumo: `persistent-summary.json`. Percentis nearest-rank, excluindo aquecimento. Consulte [o escopo dos temporizadores](BENCHMARK_1080P.md).
