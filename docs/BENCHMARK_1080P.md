# Benchmark DX12 na RX 7600 — 1080p

Medição mais recente: [contexto completo por etapa, com recursos persistentes](GPU_STAGE_TIMING.md). Os resultados abaixo documentam o experimento anterior.

Medição local em 05/09/2026, build Release MSVC 19.51, AMD Radeon RX 7600, texturas RGBA32F, entrada 1280×720 e saída 1920×1080. Cada modo executou 10 quadros de aquecimento e 50 amostras, sequencialmente, sem debug layer nas medições abaixo. Todos os 60 quadros de cada modo passaram na comparação com a referência CPU independente.

| Medida | Histórico na saída p50 / p95 | Temporal + bilinear p50 / p95 |
|---|---:|---:|
| Soma dos dispatches GPU | 0,663 / 0,742 ms | 0,516 / 0,554 ms |
| Lote GPU, incluindo cópias | 9,515 / 10,276 ms | 10,383 / 11,202 ms |
| Rotina host, incluindo referência CPU | 282,878 / 321,814 ms | 188,564 / 245,029 ms |

Percentis pelo método nearest-rank, sem amostras de aquecimento. O histórico na saída usa um dispatch; o método anterior usa temporal e ampliação em dois dispatches. O p50 da soma dos shaders aumentou cerca de 28,5% no histórico na saída nesta sessão. Esses tempos não medem qualidade visual.

## Escopo da medição

Queries TIMESTAMP D3D12 medem cada dispatch e o lote desde antes dos uploads até depois dos readbacks de imagens. A frequência vem da própria command queue. A soma dos shaders exclui cópias e barreiras externas aos dispatches. O lote inclui uploads, transições e readbacks, mas exclui a resolução das queries. No método anterior há readbacks entre os dois dispatches: a soma não é o intervalo contínuo entre o início e o fim dos shaders.

O tempo host usa steady_clock do início de Run até a validação final: inclui alocações, gravação/submissão, espera GPU, leitura, referência CPU e comparações. Exclui criação da fixture, compilação dos shaders, escrita do CSV e destruição dos recursos locais ao retornar. Não é tempo puro de CPU nem latência de apresentação.

A fixture é sintética, com padrão HDR, movimento constante (-0,25; 0,125) e quatro fases de jitter. Há espera da GPU e validação CPU entre quadros. Isso cria intervalos ociosos e pode afetar clocks/cache; não reproduz a carga sustentada de um jogo. Somente os históricos são persistentes: entradas, buffers de transferência, descritores e objetos de submissão ainda são criados por quadro.

Não inclui inferência neural, transformação de iluminação/materiais, renderização do jogo ou apresentação. Portanto, **não comprova a meta de pipeline completo abaixo de 3 ms**. O próximo passo é reutilizar recursos e separar o caminho residente na GPU da validação com readback, mantendo a comparação de correção como teste.

## Reproduzir

```powershell
.\build\native-rx7600\Release\tsr_temporal_dx12.exe --adapter 0 --output-history --1080p --benchmark artifacts/temporal-validation/benchmark-output-1080p.csv
.\build\native-rx7600\Release\tsr_temporal_dx12.exe --adapter 0 --1080p --benchmark artifacts/temporal-validation/benchmark-bilinear-1080p.csv
```

Use `--list-adapters` antes para identificar o índice local. `--warmup N --samples N` ajustam as contagens (1..10000). Sem resolução explícita, o benchmark usa 1280×720 → 1920×1080. `--render WxH --output WxH` permite outras resoluções. `--warp` serve para correção; seus tempos não representam hardware dedicado. `--debug` é registrado no CSV e deve ser separado das medições de desempenho.

Preserve também o log: ele identifica o adaptador e confirma o término com sucesso. Um CSV parcial de execução interrompida não constitui resultado validado. As amostras brutas, logs e resumo estão em `artifacts/temporal-validation/benchmark-*`.

## Validação desta alteração

- Build Release concluído.
- 20/20 testes CTest passaram, incluindo benchmarks curtos WARP com um e dois dispatches.
- RX 7600 com debug layer: 2 aquecimentos + 3 amostras em 1080p passaram sem avisos/erros.
- RX 7600 sem debug: 60 quadros por modo, todos comparados com CPU.
- CSVs conferidos: contagens, ordem de quadros, valores finitos, soma dos estágios e duração do lote consistente.

Compatibilidade em outras GPUs permanece pendente de execução física; este benchmark não exige recursos exclusivos da AMD.

Atualização: [recursos persistentes e nova medição](PERSISTENT_RESOURCES.md). Os números acima preservam o experimento anterior à reutilização.
