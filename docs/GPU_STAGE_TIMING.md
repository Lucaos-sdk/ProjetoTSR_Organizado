# Tempo por etapa na RX 7600

Medição local de 06/09/2026, build Release, entrada 1280×720 e saída 1920×1080. O contexto reutiliza texturas internas RGBA32F e descritores. FP16 abaixo se refere apenas às texturas externas de cor, movimento e saída; não implica cálculo interno em meia precisão.

| Etapa | FP16 mediana / p95 (ms) | FP32 mediana / p95 (ms) |
|---|---:|---:|
| Conversão de entrada | 0,112 / 0,117 | 0,171 / 0,177 |
| Reconstrução temporal | 0,654 / 0,690 | 0,643 / 0,681 |
| Conversão de saída | 0,211 / 0,259 | 0,239 / 0,291 |
| Intervalo total | **0,981 / 1,028** | **1,084 / 1,105** |

Cada execução tem 20 aquecimentos e 100 amostras. Mediana é a média das duas amostras centrais; p95 usa nearest-rank. Os percentis de etapas não precisam somar o percentil do total.

## O que foi medido

Quatro timestamps delimitam três intervalos contínuos dentro de `GpuFrameContext::Record`. Incluem os dispatches e suas transições de recursos, desde a preparação interna até o retorno ao estado COMMON. A frequência vem da própria fila DirectX 12 (100 MHz nesta placa). A leitura ocorre somente após a fence confirmar a execução, inclusive em reexecuções da lista.

O objeto `Timing` cria e mantém um heap próprio de quatro timestamps; não aceita índices ou heaps arbitrários. É opcional e deve permanecer vivo até o término de todas as execuções gravadas. O chamador resolve as consultas e sincroniza sua leitura. O contexto continua sem submeter comandos, esperar ou fazer leitura de volta para CPU.

O benchmark reexecuta a última lista do teste, com imagem, parâmetros e histórico fixos. A comparação da saída com CPU passa antes e após as repetições. O teste base também verifica descarte, destruição, retenção de histórico e reexecução de comandos antigos.

**Esta é uma medição sintética com reutilização de cache, não um teste de desempenho em jogo.** Não inclui inferência neural, transformação de iluminação/materiais, renderização do jogo, apresentação, CPU ou transferências de validação. O readback da imagem continua no lote, mas fora dos timestamps; ele e as esperas entre repetições podem afetar cache e frequência. Uma única sessão não caracteriza variação térmica nem carga sustentada.

O resultado está abaixo do orçamento desejado de 2–3 ms para este protótipo isolado. Não comprova que um pipeline neural completo cumprirá esse orçamento, nem equivalência visual com FSR 4.1.1 ou DLSS 5. O FrameTime de aproximadamente 10 ms mostrado no overlay é o tempo do quadro completo e não pode ser atribuído a estas etapas.

## Reprodução e evidências

```powershell
.\build\native-rx7600\Release\tsr_gpu_context_dx12.exe --1080p --fp16 --benchmark artifacts/temporal-validation/stage-rx7600-fp16.csv
.\build\native-rx7600\Release\tsr_gpu_context_dx12.exe --1080p --benchmark artifacts/temporal-validation/stage-rx7600-fp32.csv
```

Preservar também o log que identifica a placa, formatos, modo e término com sucesso. CSV parcial não comprova validação. Dados desta sessão: `artifacts/temporal-validation/stage-rx7600-*.csv`, logs correspondentes e `stage-timing-summary.json` com hashes e estatísticas.

Validação: 35/35 testes CTest passaram, incluindo o novo benchmark com WARP, FP16 e debug layer. A execução adicional na RX 7600 com debug layer e comparação CPU também passou. Os 300 registros dos três CSVs foram conferidos quanto a contagem, ordem, valores finitos e soma dos intervalos. Os números da tabela excluem a execução com debug.

Próximo passo de integração: suportar e validar o formato real de profundidade e as convenções de exposição do Witcher 3 antes de medir o processamento próprio dentro do jogo. O backend instalado continua sendo o diagnóstico com FSR 2.1.2; esta alteração não instala outro renderizador no jogo.
