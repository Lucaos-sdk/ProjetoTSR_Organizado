# TSR neural experimental — estado real do projeto

Prioridade de uso: **saída final 1080p**, com resolução interna configurável. O [harness agora permite escolher resoluções e GPU DX12](docs/RESOLUTIONS_AND_GPUS.md); 1440p, ultrawide e 4K seguem disponíveis como testes. Isso ainda não configura o jogo nem torna o modelo neural compatível com toda resolução.

Protótipo de pesquisa para reconstrução temporal DX12 na RX 7600. **Ainda não é um upscaler utilizável no jogo.** A integração TSR incompleta foi retirada do fluxo de avaliação do OptiScaler, que volta a seguir seu caminho normal. Isso não certifica o build completo do fork.

A [direção confirmada do projeto](docs/PROJECT_DIRECTION.md) inclui transformação neural de iluminação e materiais inspirada na proposta do DLSS 5, além de upscaling. Essa ambição ainda não tem modelo treinado, qualidade ou viabilidade de desempenho demonstrados.

Evidência local mais recente: [saída estável e comparação visual sintética](docs/STABLE_GRID_REVIEW.md), com compensação de jitter, readback real da RX 7600 e métricas contra referência analítica.

O [experimento cúbico opcional](docs/RECONSTRUCTION_REVIEW.md) foi comparado ao bilinear; não melhorou as métricas globais dessa sequência, então o bilinear continua como padrão.

O novo [histórico na resolução de saída](docs/OUTPUT_HISTORY_REVIEW.md), habilitado por `--output-history`, preserva amostras de detalhe entre fases de jitter e melhorou a sequência sintética. Permanece opcional: aloca 510 MiB de histórico em 4K nesta RX 7600 e ainda não tem custo GPU ou qualidade no jogo validados.

A auditoria e o plano técnico estão em [docs/AUDITORIA.md](docs/AUDITORIA.md).

## O que funciona nesta revisão

- Exportação estática FP16 autocontida, com validação de nomes, shapes, tipos e origem dos pesos.
- Exportação exige `state_dict` compatível; pesos aleatórios somente com `--allow-untrained`.
- Benchmark exige DirectML, rejeita fallback CPU e mede latência observada pela CPU com sincronização das saídas. A compatibilidade do binding Python depende do pacote ORT; se indisponível, aborta.
- Testes de transformação HDR e exportação ONNX. Nenhum deles comprova qualidade temporal.

## Testar a base Python

```bash
python -m pip install -r _IA_Python/requirements.txt
python -m unittest discover -s tests -v
python _IA_Python/export_onnx.py --allow-untrained
```

O último comando cria apenas um modelo de teste. Não o copie para o jogo. Para exportar pesos treinados:

```bash
python _IA_Python/export_onnx.py --checkpoint caminho/treinado.pt
```

Um checkpoint carregado com sucesso não equivale a qualidade validada. Nesta revisão não há dataset, treinamento ou checkpoint de qualidade fornecidos.

No Windows, instale `onnxruntime-directml` em ambiente dedicado e execute:

```bat
python _IA_Python/benchmark_io_binding.py --allow-untrained
```

O benchmark não inclui os shaders nem a integração ao jogo. Não é uma medição por timestamps D3D12.

## Pastas

| Pasta | Estado |
|---|---|
| `_IA_Python` | Modelo e ferramentas experimentais; os ONNX antigos têm pesos externos ausentes |
| `_Shaders` | Esboços com correções pontuais; ainda incompatíveis com o layout NCHW do runtime |
| `_OptiScaler_Source` | Fork com bypass TSR removido; engine antiga preservada para reimplementação, excluída do build normal |
| `_Build_Final_Jogo` | Conteúdo legado incompleto; não é uma distribuição validada |

Não há DLL nova validada nesta revisão. O antigo `build.bat` aponta para um arquivo movido e não constitui um build reproduzível. Consulte a auditoria antes de tentar distribuir o projeto.

## Baseline nativo e atualização local

O novo [baseline espacial DX12](native/baseline/README.md) compila separadamente do fork e compara saída bilinear GPU/CPU, com modo WARP e execução em hardware. Ele ainda não implementa TSR temporal.

A evolução local inclui entrada/saída Texture2D RGBA32F (`--textures`), validação na RX 7600 com debug layer e o [contrato explícito de quadros](docs/FRAME_CONTRACT.md). O [baseline temporal](docs/TEMPORAL_BASELINE.md) agora implementa reprojeção, rejeição por profundidade, exposição e histórico por instância para câmera fixa, seguido de ampliação espacial GPU. Ainda não entrega TSR neural nem integração no jogo.

Para atualizar um clone Git limpo no Windows, execute `powershell -File .\tools\Update-Local.ps1`. O script verifica o origin, recusa alterações locais e atualiza somente por fast-forward. Não executa reset, clean ou stash. Se sua pasta veio de ZIP, faça um clone novo em outra pasta, preservando os arquivos anteriores:

```powershell
git clone --branch fix/tsr-validation-and-safe-fallback https://github.com/Lucaos-sdk/ProjetoTSR_Organizado.git ProjetoTSR_Atualizado
```

O [benchmark local em 1080p](docs/BENCHMARK_1080P.md) separa o custo dos shaders, das cópias GPU e da validação CPU. Inclui amostras na RX 7600; ainda não mede o pipeline completo no jogo.

A etapa de [recursos persistentes](docs/PERSISTENT_RESOURCES.md) elimina novas alocações GPU em quadros de mesmo tamanho e mantém a validação completa, preparando a separação do processamento e do diagnóstico.

O [caminho para o primeiro teste em jogo](docs/FIRST_GAME_TEST.md) detalha o passe GPU separado e os bloqueios restantes do backend OptiScaler. A [análise do DLSS-NR-on-AMD recebido](docs/EXTERNAL_DLSS_NR_REVIEW.md) distingue documentação de implementação verificável.

O [adaptador de entrada GPU](docs/INPUT_ADAPTER.md) converte profundidade e movimento com parâmetros explícitos; validado separadamente na RX 7600 em 1080p e no WARP.

A [cadeia GPU de entrada e reconstrução](docs/GPU_INPUT_CHAIN.md) valida seis quadros sem readbacks intermediários na RX 7600, com saída 1080p.

A [etapa FP16 e saída](docs/FP16_OUTPUT.md) amplia os formatos da cadeia GPU e escreve a imagem final em uma textura fornecida pelo chamador.

A cadeia também suporta [regiões deslocadas de entrada e saída](docs/TEXTURE_REGIONS.md), verificando a preservação dos pixels externos.

O [controle por quadro e fence](docs/FRAME_SLOTS.md) foi validado com três conjuntos persistentes e nove quadros na RX 7600, impedindo reutilização antes da conclusão dos consumidores.

O [contexto GPU unificado](docs/GPU_FRAME_CONTEXT.md) reúne a cadeia completa com recursos persistentes, entradas/saída externas e controle por fence, preparando o adaptador OptiScaler.

A [notificação de submissão do OptiScaler](docs/OPTISCALER_SUBMISSION.md) conecta o envio real de comandos ao registro TSR. A DLL compila, mas o backend ainda não está habilitado para uso em jogo.

A [reexecução temporal em uma fila](docs/TEMPORAL_REPLAY.md) mantém buffers reservados enquanto a gravação existir e valida 19 execuções para nove quadros na RX 7600.

O [adaptador de parâmetros NGX](docs/NGX_FRAME_ADAPTER.md) prepara os dados para o contexto GPU e exige convenções explícitas, com modo sem histórico quando a câmera não é conhecida.

O [primeiro teste no The Witcher 3](docs/WITCHER3_PROBE.md) usa uma opção de diagnóstico selecionável, renderizada por FSR 2.1.2, para registrar os dados reais necessários ao backend próprio.
