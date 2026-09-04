# Auditoria técnica — base cfa3641aa4c3bb1cf11e714e3cfca0977ea71c0f

A arquitetura híbrida é uma hipótese de pesquisa razoável. O código inicial, porém, não implementa o pipeline descrito. A revisão atual corrige a exportação e contém a integração inválida; não entrega uma rede treinada nem uma DLL funcional de TSR.

## Problemas confirmados

| Prioridade | Evidência no código original | Consequência / tratamento |
|---|---|---|
| P0 | `TryEvaluateOptiFeature` retorna Success após `ExecutarFrame`, antes da avaliação normal; nenhum buffer de saída NGX é passado ao TSR | Pode deixar a imagem sem produção válida. Bypass removido |
| P0 | ONNX de 4.399 bytes referencia `tsr_ultralight_540p.onnx.data`, ausente nas duas pastas | Checker falha por pesos ausentes; arquivos legados não são distribuíveis |
| P0 | Exportador instancia modelo aleatório, não carrega checkpoint e exporta FP32; C++ declara FP16 | Exportador refeito com checkpoint obrigatório ou teste explicitamente não treinado, FP16 e pesos embutidos |
| P0 | `TSREngine::Dispatch` converte `GetGPUVirtualAddress()` para `void*` | Não é uma alocação opaca do provider DML; precisa de `CreateGPUAllocationFromD3DResource` e liberação correspondente |
| P0 | `CreateTensor` recebe quantidade de elementos na sobrecarga `void*` | Essa sobrecarga espera bytes; FP16 precisa de 2 bytes por elemento |
| P0 | `TSRManager` cria Texture2D RGBA para tensores de 16, 8 e 12 canais; chama com 1920x1080 | Não corresponde a buffers lineares contíguos NCHW 960x540 |
| P0 | Inicialização ignora device/queue, usa adaptador 0 e não chama DisableMemPattern | Não estabelece interoperabilidade com a fila do jogo e omite configuração obrigatória do DML |
| P0 | Nenhuma gravação real de PrePack, reprojeção ou reconstrução no manager | Inferência lê entradas sem conteúdo definido e não produz a saída do jogo |
| P1 | Barreiras sempre assumem COMMON, apenas dois recursos recebem transições, HRESULTs ignorados e ponteiros não liberados | Estados incorretos, falhas de alocação ignoradas e vazamentos; implementação antiga isolada do build |
| P1 | `Run` não grava no command list recebido; preprocessamento ainda não submetido não está ordenado antes da inferência | Exige projeto explícito de filas, submissões e fences; barreiras sozinhas não resolvem |
| P1 | PrePack escreve 8 canais em texturas, rede exige 16; jitter é zero | Contrato sem implementação; precisa definir significado e empacotamento dos canais antes de treinar |
| P1 | Reconstrução usa só três coeficientes como residual arbitrário `*0.1`; não existe reprojeção 4K | Não implementa os 12 coeficientes descritos nem estabilidade temporal |
| P1 | `uint33` na entrada HLSL; `log1p` no shader e clipping da luminância negativa | Tipo corrigido e transformação signed-log em todos os canais; compilação HLSL ainda não validada |
| P1 | Caminhos `$(ProjectDir)external` apontam um nível abaixo e há caminho absoluto de outro PC | Ajustados para SolutionDir e removida referência pessoal; demais dependências ainda precisam de build Windows |
| P1 | `build.bat` aponta para TSREngine.cpp fora de OptiScaler/; não há import libs `.lib` versionadas | Não há build standalone reproduzível; script legado não foi certificado |
| P2 | Benchmark original usa outro filename, não rejeita fallback e não sincroniza explicitamente saídas | Refeito para falhar claramente e reportar latência limitada à inferência |

A engine antiga permanece como referência, mas não é compilada no OptiScaler normal. Não basta reativar a entrada no vcxproj: todos os bloqueios de interoperabilidade precisam ser resolvidos antes.

## Contrato de tensores validado

Todos os tensores são NCHW, batch 1, FP16 contíguo. Estes tamanhos são payloads, sem alinhamentos adicionais exigidos pelas APIs.

| Nome | Shape | Bytes |
|---|---|---:|
| packed_input | 1×16×540×960 | 16.588.800 |
| warped_history | 1×8×540×960 | 8.294.400 |
| confidence_mask | 1×1×540×960 | 1.036.800 |
| new_history | 1×8×540×960 | 8.294.400 |
| reconstruction_coeffs | 1×12×540×960 | 12.441.600 |

A definição semântica dos 16 canais e dos 12 coeficientes ainda precisa ser fechada em conjunto com o treinamento e os shaders. A validação ONNX verifica somente o contrato estrutural. O shader PrePack atual não satisfaz esse contrato e não deve ser ligado diretamente à inferência.

A transformação de cor de referência usa Y=.25R+.5G+.25B, Co=.5R-.5B, Cg=-.25R+.5G-.25B, seguida de sign(x)·ln(1+|x|). A inversa aplica sign(x)·expm1(|x|), depois R=Y+Co−Cg, G=Y+Cg, B=Y−Co−Cg. Isto não garante ausência de overflow na quantização FP16 nem estabilidade para qualquer HDR. Exposição/pre-exposição precisam de contrato próprio.

## Desempenho e qualidade

A rede tem 12.245 parâmetros e aproximadamente 6,308 bilhões de MACs por quadro 540p nas convoluções (sem contar ativações e movimentação de dados). Ser pequena em parâmetros não comprova baixa latência. FP16/DirectML tampouco comprova uso de INT8 ou instruções WMMA específicas.

O número informado de 0,445 ms não pôde ser reproduzido aqui. O artefato antigo não carrega por falta de pesos; não há log verificável associado ao hardware, driver e modelo. A ferramenta corrigida mede tempo observado pela CPU com sincronização, não tempo puro de GPU. Precisamos medir também PrePack, reprojeção, reconstrução, barreiras e concorrência com o jogo.

## Sequência necessária para chegar a um TSR real

1. **Baseline espacial em harness DX12:** color/depth/motion/jitter/exposure/reset explícitos; saída bilinear correta, sem rede. Build reproduzível Windows e debug layer sem erros. Depois implementar reprojeção, disoclusão, reset e ping-pong de histórico por instância.
2. **Contrato e referência diferenciável:** definir 16 canais e 12 coeficientes com reconstrução Python equivalente ao HLSL. Comparar numericamente coordenadas, jitter e transforms em fixtures sintéticas. Não reduzir profundidade/movimento indiscriminadamente por bilinear nas bordas.
3. **Treinamento:** obter sequências com entradas de baixa resolução, ground truth de alta resolução, MVs/depth/jitter e exposição correspondentes; separar treino/validação por cena. Treinar com perdas espacial e temporal, testar cortes de câmera, partículas, transparências e objetos finos. Checkpoint, seeds e métricas versionados.
4. **Interoperabilidade:** buffers D3D12 lineares, allocations DML com RAII, DML1 no mesmo device/queue, validação de shapes/dtypes, garantia de vida útil, submissão ordenada e fences. ORT não insere inferência magicamente no command list aberto do jogo; considerar DirectML nativo para gravação direta.
5. **Backend OptiScaler:** integrar como `IFeature_Dx12` por contexto, não em RestoreRoot; identificar semântica NGX de buffers, dimensões, motion scale, profundidade invertida, jitter/reset e exposição. Retornar sucesso somente após produzir a saída. Definir recuperação antes de alterar recursos do jogo.
6. **Validação RX 7600/The Witcher 3:** timestamps por pass e total, distribuição de frametimes com jogo, memória, capturas pareadas e métricas temporais. Comparar com baseline na mesma entrada. Só então otimizar arquitetura, resolução e quantização.

Dados que faltam para as fases de treinamento/validação: checkpoint treinado (se existe), scripts/dataset que o produziram, log original do benchmark, versões de Windows/driver/ORT e captura de sequência com os buffers. A RX 7600 do usuário é o alvo; não está acessível neste ambiente Linux.

## Validação realizada nesta revisão

- Python 3.12, PyTorch 2.14.0+cpu, ONNX 1.22.0, NumPy 2.3.5.
- Quatro testes: roundtrip HDR incluindo negativos/preto; rejeição do ONNX legado; bloqueio de exportação aleatória implícita; exportação real 960x540 com verificação FP16/pesos embutidos e rejeição para uso treinado.
- `compileall` e `git diff --check`.
- Sem build MSVC/DXC, execução DML ou teste dentro do jogo. As alterações C++ restauram o fluxo normal por inspeção, sem certificação de runtime.

## Referências primárias

- [ONNX Runtime — DirectML](https://onnxruntime.ai/docs/execution-providers/DirectML-ExecutionProvider.html): device/queue e configuração da sessão.
- [ONNX Runtime — device tensors](https://onnxruntime.ai/docs/performance/device-tensor.html).
- `_OptiScaler_Source/include/dml_provider_factory.h`: DML1, CreateGPUAllocationFromD3DResource e FreeGPUAllocation.
- `_OptiScaler_Source/include/onnxruntime_cxx_api.h`: overload de CreateTensor com tamanho em bytes.
