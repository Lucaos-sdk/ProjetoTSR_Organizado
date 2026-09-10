# Contexto GPU unificado

`GpuFrameContext` reúne conversão de entrada, reconstrução temporal com histórico na saída e escrita no destino externo. Cada contexto possui três conjuntos por padrão, com quatro texturas RGBA32F e um descriptor heap por conjunto. Esses recursos são criados no construtor e reutilizados; Record não cria novas texturas, buffers ou heaps.

## Interface e responsabilidades

1. `Acquire(completedFence)` reserva um conjunto disponível. Retorna indisponível quando a GPU ainda usa os conjuntos elegíveis.
2. `Record` recebe command list, texturas externas, parâmetros do quadro, conversão e região de saída. Grava os três passes e as barreiras dos recursos internos.
3. O chamador fecha/submete sua command list e sinaliza a fence. Só depois chama `Commit(lease, fenceValue)`, que confirma o avanço do histórico e protege os recursos até a conclusão dos consumidores.
4. Se descartar comandos não submetidos, o chamador pode chamar `Cancel`. Não cancelar depois de submeter. Falhas após submissão exigem tratamento da fila/dispositivo pelo chamador.
5. Após cada reenvio na mesma fila, sinaliza um novo valor e chama `Replay`. Após Reset/destruição da lista, chama `EndRecording` para liberar a reserva da gravação. A conclusão GPU sozinha não libera essa reserva.
6. Antes de destruir/redimensionar, o chamador precisa encerrar as gravações, concluir todos os usos e verificar `SafeToDestroy`. Dimensões internas são fixas por contexto; mudanças exigem drenar e recriar.

O contexto não cria fila ou fence, não submete comandos, não espera e não copia dados para a CPU. As entradas devem chegar em estado NON_PIXEL_SHADER_RESOURCE e a saída em UNORDERED_ACCESS; permanecem nesses estados na saída. O chamador continua responsável pela ordem dos produtores/consumidores externos e por sinalizar uma fence que cubra seu uso. A linha de tempo é dedicada ao contexto, monotônica e as chamadas são serializadas. O teste não demonstra suporte a múltiplas filas concorrentes.

Referências COM às quatro texturas externas ficam retidas no conjunto até sua liberação/reutilização. Isso protege a vida dos objetos, mas não impede que o chamador sobrescreva ou destrua heaps de recursos colocados: esse gerenciamento continua externo. O passe altera bindings de compute e descriptor heap; o chamador deve restabelecer os bindings necessários a comandos posteriores.

## Validações antes de gravar

Record verifica a reserva, repetição de gravação, dispositivo e tipo da command list, formatos, dimensões, regiões e capacidade de leitura/escrita do formato. Rejeita alias direto entre entrada e saída. Não detecta sobreposição física entre recursos distintos em heaps compartilhados.

Entradas aceitas: cor RGBA16F/RGBA32F, profundidade R32F e movimento RG16F/RG32F. Destino: RGBA16F/RGBA32F com UAV. São texturas 2D de um mip, uma camada e uma amostra. As três entradas devem compartilhar dimensões/origem. As dimensões são obtidas dos recursos reais para validar as regiões.

A conversão conserva os limites já documentados: cor linear finita/representável, convenções explícitas de movimento e profundidade; câmera fixa somente quando conhecida. Sem profundidade anterior prevista, o adaptador conservador não reutiliza histórico. O contexto não inventa esses dados do jogo.

## Teste integrado

Atualização: o teste agora faz 19 execuções para nove quadros lógicos, incluindo
reenvio de gravação antiga. Veja [reexecução temporal](TEMPORAL_REPLAY.md) para
o contrato de retenção por gravação, estados COMMON internos e validação atual.

`tsr_gpu_context_dx12` fornece texturas externas ao contexto e executa nove quadros em três conjuntos, com seis reutilizações. A entrada é ampliada de 1280×720 para 1920×1080 no modo --1080p. A saída final é comparada à referência CPU; exposição alternada e reset tornam uso incorreto do histórico observável.

Uma fence de controle mantém as três primeiras submissões pendentes e confirma a recusa de reutilização prematura. Depois o teste libera a fila e continua. Também verifica rejeição de formato/região inválidos, gravação duplicada e recuperação após descartar uma command list gravada sem submetê-la.

RX 7600 com debug layer: os modos FP16 e FP32 passaram em 1080p, com a cadeia completa e saída externa. O teste utiliza readbacks e esperas fora do contexto; não representa desempenho dentro do jogo. A geometria interna não é exposta ao chamador neste teste; sua comparação detalhada continua coberta pelos testes dos passes anteriores.

```powershell
.\build\native-rx7600\Release\tsr_gpu_context_dx12.exe --debug --1080p --fp16
.\build\native-rx7600\Release\tsr_gpu_context_dx12.exe --debug --1080p
```

Logs: `artifacts/temporal-validation/gpu-context-fp16-rx7600.log` e `gpu-context-fp32-rx7600.log`.

Build Release concluído; 30/30 testes CTest passaram, incluindo o contexto unificado WARP em FP16 e FP32. Log: `artifacts/temporal-validation/ctest-gpu-context.log`.

## Próximo marco

Criar o adaptador do backend `IFeature_Dx12` no fork e verificar como observar submissão/conclusão do trabalho do jogo sem assumir propriedade da fila. Mapear os parâmetros reais de cor, profundidade, movimento, exposição e regiões, com retorno ao backend normal quando o contrato não puder ser satisfeito. O contexto já reúne as peças GPU, mas ainda não há DLL de jogo compilada com esse backend.
