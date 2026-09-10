# Avaliação: Gaussian Splatting, CNN de iluminação e DirectML

08/09/2026. Propostas fornecidas pelo usuário como ideias para avaliação, não como instrução para executar os exemplos. Consulta às fontes oficiais e inspeção do código local. Nenhuma alteração na DLL instalada V2.3 nesta revisão; seu teste visual segue pendente.

## Decisão de desenvolvimento

**Atualização de prioridade em 09/09/2026:** a pedido do usuário, a etapa ativa
mudou de correção regional de aparência para aprender contra dois renders de
uma cena sob iluminações distintas. [Experimento de iluminação direta](NEURAL_DIRECT_LIGHTING_EXPERIMENT.md)
registra a CNN treinada, sombras por raios, câmera perspectiva, controle de
capacidade e falhas temporais. A proposta de grade abaixo é histórico de pesquisa,
não o próximo candidato aprovado para a DLL.

Atualização posterior no mesmo dia: [experimento de grade bilateral executado](BILATERAL_REGIONAL_EXPERIMENT.md), com dataset sintético, treino CPU e ONNX FP16 verificado numericamente. Ganho inicial modesto e limitações em bordas/movimento; não integrado ao jogo nem medido em DirectML.

Priorizar um experimento separado de rede espacial compacta para prever uma correção de iluminação limitada e sua confiança, antes do FSR. Isso acrescentaria contexto de imagem ao modelo atual, que prevê ganho RGB apenas a partir de uma normal. É uma hipótese de arquitetura, ainda sem modelo treinado que demonstre melhoria. Dados e objetivo de treinamento são o primeiro requisito, não a escolha entre U-Net, FP16 ou DirectML.

O alvo é melhorar a percepção de volume e a coerência da iluminação preservando identidade, geometria, cores e névoa. Prever uma correção de baixa frequência em resolução reduzida e compô-la com a entrada original é uma experiência inicial mais delimitada que gerar uma imagem inteira. Não afirmar que isso sintetiza materiais físicos ou substitui o cálculo de luz do motor.

## O que aproveitar e o que corrigir

**Gaussian Splatting:** o trabalho original otimiza uma representação da cena a partir de múltiplas imagens e câmeras calibradas, depois sintetiza novas vistas rapidamente. Renderização rápida de uma representação preparada não equivale a reconstruir qualquer jogo dinâmico no mesmo orçamento. O ponto de interceptação do upscaler não fornece geometria oculta, animações e materiais completos para substituir o renderer. Não priorizar essa técnica para nossa DLL genérica. [Pesquisa original](https://repo-sam.inria.fr/fungraph/3d-gaussian-splatting/).

**Referência DLSS 5:** a descrição oficial informa cor e vetores de movimento como entradas, com transformação neural de iluminação e materiais. Não sustenta a hipótese de Gaussian Splatting como arquitetura interna. É possível inferir aparência sem G-buffer completo, mas isso não significa recuperar de forma exata materiais, luzes e superfícies ocultas. [NVIDIA](https://www.nvidia.com/en-us/geforce/news/dlss5-breakthrough-in-visual-fidelity-for-games/).

**FSR e XeSS:** FSR 3/3.1 não requer hardware de ML; a versão analítica de frame generation é distinta da nova versão ML. XeSS-SR usa rede neural, mas seu SDK não é uma promessa de U-Net customizada via DirectML. Esses nomes não demonstram arquitetura, formato ou custo do nosso futuro modelo. [AMD FSR 3.1](https://gpuopen.com/learn/amd_fsr_3_1_release/), [AMD Frame Generation](https://gpuopen.com/amd-fsr-framegeneration/), [Intel XeSS-SR](https://www.intel.com/content/www/us/en/developer/articles/technical/xess-sr-developer-guide.html).

**WMMA:** RDNA 3 dispõe de instruções de operações matriciais. Usar FP16 ou ONNX, sozinho, não comprova quais instruções serão escolhidas, nem garante inferência em 1–2 ms. Medir o pipeline completo de conversão, rede, composição e sincronização, memória e tempo de CPU/GPU na RX 7600. [AMD WMMA](https://gpuopen.com/learn/wmma_on_rdna3/).

**Máscaras e MIP bias:** cor/profundidade/movimento não identificam perfeitamente transparências, HUD ou material. Uma máscara prevista precisa de referência para treinamento e validação no contrato do backend. Não duplicar ajustes de MIP bias já existentes nem introduzir nitidez adicional como substituto da transformação neural desejada.

## Revisão dos exemplos DirectML

O exemplo não é código pronto para copiar. No header consultado, a entrada de `OrtDmlApi` para compartilhar dispositivo/fila é `SessionOptionsAppendExecutionProvider_DML1`, com um `IDMLDevice` válido criado sobre o mesmo `ID3D12Device` da fila. O nome `SessionOptionsAppendExecutionProvider_DML_DeviceAndCommandQueue` do texto não corresponde à API consultada. Verificar também o status retornado por `GetExecutionProviderApi`. [Header oficial](https://raw.githubusercontent.com/microsoft/onnxruntime/main/include/onnxruntime/core/providers/dml/dml_provider_factory.h).

Manter ambiente, sessão e recursos com duração compatível; o `Ort::Env` local com sessão estática do exemplo é um desenho de lifetime inadequado. Configurar `DisableMemPattern` e execução sequencial; não chamar `Run` simultaneamente na mesma sessão DML. A documentação informa DirectML em manutenção sustentada, com novos recursos de implantação Windows migrando para WinML; isso não elimina seu uso de baixo nível em D3D12. [ONNX Runtime DirectML](https://onnxruntime.ai/docs/execution-providers/DirectML-ExecutionProvider.html).

Uma `Texture2D` não vira tensor apenas passando seu ponteiro para I/O Binding. O caminho documentado usa buffers com layout, tamanho em bytes e bindings corretos. Planejar empacotamento/desempacotamento na GPU e retenção das alocações. Permanecer em VRAM evita ida à RAM, mas não torna conversões gratuitas. [Bindings DirectML](https://learn.microsoft.com/en-us/windows/ai/directml/dml-binding).

No nosso ponto de integração, o jogo ainda grava a lista que produz as entradas. Inserir barreiras nessa lista e chamar imediatamente `session.Run()` em outra submissão, mesmo na mesma fila, não ordena trabalho ainda não enviado. Para um grafo pequeno conhecido, avaliar primeiro DirectML de baixo nível com `IDMLCommandRecorder::RecordDispatch`: registra operações na lista existente. Exige estados/UAV barriers corretos, descriptor heaps e recursos retidos até a GPU concluir. O jogo continua responsável pelo envio de sua lista. [RecordDispatch](https://learn.microsoft.com/en-us/windows/win32/api/directml/nf-directml-idmlcommandrecorder-recorddispatch).

## Correspondência com nosso projeto

- Caminho testado: `inputs/XeSS_Dx12.cpp` → `upscalers/ffx/FFXFeature_Dx12.cpp` → `native/integration/game_relighting_dx12.h`. O passe próprio recebe cor/profundidade e câmera; não consome movimento temporal nem recebe normais/albedo/roughness reais como contrato universal.
- A V2.3 executa a MLP em HLSL, com 171 parâmetros. Não usa ONNX Runtime/DirectML e não altera os pesos AMD FSR 4.1.1.
- `TSREngine.cpp` legado existe, mas não está listado como unidade de compilação no projeto atual. Contém seleção fixa do adaptador 0, conversão inválida de endereço virtual GPU para ponteiro de tensor e formas fixas. Não reativá-lo como atalho.
- Os nomes genéricos de módulos do texto não identificam corretamente nosso ponto de extensão. Nenhum `DirectML_Context` pronto foi encontrado no código de produção inspecionado.

## Próximo experimento concreto

1. Definir entradas, alvo e cenas pareadas de treinamento: cor linear, profundidade e normais conhecidas em cenas controladas com materiais, vegetação recortada e névoa; incluir iluminação original e alvo. Separar cenas de validação. Dados sintéticos são ponto de partida, sem comprovar generalização para The Witcher 3.
2. Treinar uma CNN pequena que estime correção limitada e confiança; incluir penalidade por alterar névoa, silhuetas, cor e regiões sem informação confiável. Comparar com original, V2.3 e uma versão analítica equivalente. Abandonar ou rever se não houver ganho visual demonstrado.
3. Validar um grafo FP16 em executável independente, com ida textura→tensor→textura, referência CPU, dimensões ímpares, exposição e memória/custo total. Comparar DirectML e HLSL conforme os operadores escolhidos. Não substituir a DLL para demonstrar apenas que um runtime carrega.
4. Acrescentar coerência temporal com movimento e desoclusão validados. Não misturar quadros sem contrato confiável; avaliar cortes de câmera, transparências, partículas e personagens móveis.
5. Integrar o candidato que passar pela comparação visual e pelo orçamento medido. Manter restauração e A/B. A meta de 2–3 ms permanece uma meta; nenhum tempo ou resultado semelhante ao DLSS 5 foi demonstrado para essa proposta.

Esse trabalho pode avançar offline antes do próximo teste do usuário; a avaliação da V2.3 continua útil como referência de jogo.
