# Projeto TSR — reconstrução temporal e iluminação neural experimental

**Estado consolidado em 10/09/2026.** Código, testes, pesos próprios, evidências
e decisões para continuar o projeto com outras pessoas ou IAs. Não é uma
distribuição final nem uma implementação equivalente ao DLSS 5.

O objetivo é transformar significativamente a iluminação e, futuramente, os
materiais dos jogos com uma IA leve integrada pelo OptiScaler. Referência de
uso: **Radeon RX 7600, 8 GB, saída 1920×1080**. Outras placas e resoluções são
objetivos de portabilidade, não compatibilidade universal já comprovada.
Nitidez adicional, sozinha, não atende ao objetivo do usuário.

## Comece aqui

1. [Guia de continuidade para colaboradores e IA](docs/AI_HANDOFF.md).
2. [Direção consolidada](docs/PROJECT_DIRECTION.md).
3. Último resultado: [direção, módulo e profundidade](docs/NEURAL_GEOMETRY_ABLATION.md).
4. Contexto anterior: [fase de amostragem](docs/NEURAL_SAMPLING_PHASE.md),
   [treino regional](docs/NEURAL_REGIONAL_TRAINING.md) e
   [generalização](docs/NEURAL_GENERALIZATION.md).
5. [Índice de todos os relatórios](docs/DOCUMENTATION_INDEX.md).

Documentos antigos são registros datados: frases como “teste pendente” ou
“próximo passo” descrevem aquele momento. Não substituem este estado consolidado.

## Estado real: jogo e pesquisa são frentes diferentes

| Frente | Estado |
| --- | --- |
| V2.3 no The Witcher 3 DX12 | Passe próprio no fork do OptiScaler, MLP de 171 parâmetros em HLSL antes do FSR. Testado pelo usuário, com logs. |
| FSR 4.1.1 | Integração de API/provedor registrada. Não alteramos os pesos neurais proprietários da AMD. |
| Nova IA espacial | CNN própria de 38.787 parâmetros, treinada contra iluminação/sombras de cenas 3D sintéticas. Ainda offline, CPU/float32. |
| Histórico temporal | Existe no harness DX12 separado; não equivale a histórico integrado na nova CNN do jogo. |
| Frame generation | XeFG testado; capacidade local consultada: um quadro interpolado, ou 2×. MFG próprio 3×/4× não implementado. |
| Qualidade/performance finais | Nenhuma equivalência a DLSS 5 demonstrada. Custo GPU/VRAM da CNN espacial ainda não medido. |

O caminho não consiste em transformar os pesos do FSR em DLSS. Adicionamos
uma etapa própria ao fluxo do OptiScaler. Essa etapa soma custo ao jogo; não
elimina automaticamente os shaders de iluminação que a engine já executou.

### V2.3: baseline que permanece instalado

A [V2.3](docs/RELIGHTING_V23_SCENERY.md) usa normais reconstruídas de profundidade,
orientação mundial, composição que preserva cor e atenuação por distância e
confiança para proteger vegetação/névoa distante. O usuário relatou melhora dos
defeitos de chão e pele após a V2. A correção específica do cenário distante na
V2.3 ainda não tem confirmação visual isolada.

- F8 alterna o passe; o menu tem controles próprios de relighting.
- Configuração registrada: entrada XeSS Ultra Quality 1476×830, FSR 4.1.1,
  saída 1920×1080. Alterações do usuário podem mudar essa configuração.
- O log V2.3 chegou a 144.620 quadros; mediana amostrada do passe **0,23560 ms**.
  Não é tempo total de quadro, latência de entrada ou custo da CNN nova.
- Associação de câmera é experimental, com bypasses e restrições documentadas.
  Ausência de erros em um log não certifica todos os jogos/transições.
- SHA256 da DLL instalada, conferido ao terminar os diagnósticos:
  `9B3992A1FD4337A1C235DC3401D59F8BC81614E29585F3F20546B4C9BC6D47D0`.

Os experimentos neurais recentes não substituíram essa DLL.

## A nova IA: arquitetura, dados e limites

CNN espacial multiescala com downsampling, conexões entre escalas e saída
residual RGB limitada. Entradas: `RGB/(1+RGB)`, normal reconstruída XYZ,
profundidade de câmera normalizada e validade — oito canais. A saída usa
`0.5*tanh`, soma à cor original e respeita a máscara de proteção.

Os alvos vêm de um renderizador sintético com câmera perspectiva, plano,
esferas/blocos, luzes de área, sombras por interseção, névoa e interface protegida.
Há layouts variados e remoção/movimento de oclusores para testar resposta à
geometria. Ainda não há dataset de jogos reais, materiais complexos aprendidos,
iluminação indireta geral ou validação em personagens animados.

```text
Cor + profundidade na resolução de entrada
  -> normais por ajuste de plano 5x5 na profundidade ORIGINAL
  -> cor por área; geometria e validade por centro
  -> CNN em altura 64, mantendo proporção
  -> ampliação bilinear do residual RGB
  -> soma à cor original com proteção dos pixels inválidos
```

Normais analíticas, IDs e máscaras perfeitas são instrumentos da fixture;
não presumir sua disponibilidade no jogo. O contrato atual de relighting
consome cor, profundidade e câmera, ainda sem movimento para acumulação neural.

## Histórico: implementações, tentativas e resultados

Percentuais comparam controles de cada experimento: não são ganhos de FPS ou
comparações com DLSS. Os relatórios preservam detalhes e falhas.

| Etapa | Resultado e decisão |
| --- | --- |
| [Auditoria inicial](docs/AUDITORIA.md) | Corrigidos contratos de exportação/validação; caminho TSR incompleto retirado da avaliação normal. Pesos aleatórios não demonstram qualidade. |
| [DX12 independente](native/baseline/README.md) | Bilinear GPU/CPU, buffers/texturas, barreiras, fences, readback, WARP/RX 7600. Corrigida precisão de coordenadas em alta resolução. |
| [Base temporal](docs/TEMPORAL_BASELINE.md) | Reprojeção, profundidade, exposição/reset e histórico por instância. |
| [Grade estável](docs/STABLE_GRID_REVIEW.md) | Compensação de jitter e comparação sintética. |
| [Reconstrução cúbica](docs/RECONSTRUCTION_REVIEW.md) | Catmull-Rom limitada não melhorou o conjunto; bilinear continuou padrão. |
| [Histórico na saída](docs/OUTPUT_HISTORY_REVIEW.md) | Melhorou detalhe sintético; opcional, com 510 MiB de histórico em 4K no teste registrado. |
| [Integração GPU](docs/GPU_FRAME_CONTEXT.md) | Adaptadores, slots, recursos persistentes, descritores e vida útil vinculada à submissão real. |
| [FSR 4.1.1](docs/FSR411_INTEGRATION.md) | Extensão de versão/API e provedor funcionando nos registros do jogo. |
| [Geração de quadros](docs/FRAME_GENERATION_PROGRESS.md) | Correções de configuração/capacidade XeFG; runtime local limitado a 2×. |
| [Primeira rede de luz](docs/RELIGHTING_FIXTURE.md) | Iluminação difusa controlada, depois HLSL/DX12. |
| [Integração gráfica](docs/WITCHER_RELIGHTING_FIRST_RESULT.md) | Efeito visível; problemas de chão/pele motivaram a V2. |
| [V2](docs/RELIGHTING_WORLD_V2.md), [otimização](docs/RELIGHTING_V2_OPTIMIZATION.md) | Orientação mundial e reconstrução de normais mais estável. |
| [V2.2](docs/RELIGHTING_V22_COLOR.md), [V2.3](docs/RELIGHTING_V23_SCENERY.md) | Preservação de cor e cenário distante; baseline separado da nova CNN. |
| [Grade bilateral](docs/BILATERAL_REGIONAL_EXPERIMENT.md) | CNN de 10.256 parâmetros; vantagem de cor de 3,04% sobre controle semelhante. Alvo ainda era transformação de aparência. |
| [Bordas/filtro guiado](docs/BILATERAL_EDGES_EXPERIMENT.md) | Melhoras em bordas/movimento, vantagem de cor de apenas 1,87%; não aprovado. |
| [Iluminação 3D aprendida](docs/NEURAL_DIRECT_LIGHTING_EXPERIMENT.md) | CNN de 38.787 parâmetros e sombras calculadas: melhor estático, instabilidade temporal. Continuação temporal não resolveu. |
| [Intervenções/normais](docs/NEURAL_CAUSAL_DIAGNOSTICS.md) | Remover/mover oclusores mostrou resposta insuficiente. Normais exatas ajudaram, sem eliminar erros. Ajuste por resíduo de plano melhorou geometria, sem fechar estabilidade. |
| [Treino por intervenções](docs/NEURAL_INTERVENTION_TRAINING.md) | Resposta melhorou 60,67% contra controle; imagem piorou 21,94% e temporal 28,46%. |
| [Treino conjunto](docs/NEURAL_JOINT_TRAINING.md) | Temporal melhorou 73,23%, imagem piorou 6,02%, acima do limite de 5%. |
| [Balanceamento](docs/NEURAL_BALANCE_SELECTION.md) | Oito treinos, duas inicializações; t025 passou no domínio 96×64 de esferas, não em jogos. |
| [Generalização](docs/NEURAL_GENERALIZATION.md) | Outras resoluções, blocos, distância/luz revelaram falhas. Inferência regional ajudou sem passar uniformemente. |
| [Treino regional](docs/NEURAL_REGIONAL_TRAINING.md) | Imagem melhorou 15–34% e resposta 13–24% frente aos congelados; temporal inconsistente. Erro não concentrado desproporcionalmente na faixa de bordas. |
| [Fase](docs/NEURAL_SAMPLING_PHASE.md) | Área geométrica ajudou movimento pequeno em 11–34%, mas imagem regrediu até 7%; fase artificial não melhorou uniformemente. |
| [Direção/módulo](docs/NEURAL_GEOMETRY_ABLATION.md) | Oito variantes. Renormalização melhorou fase, mas piorou imagem 3,66–8,01% contra área bruta e movimento pequeno em 14/16 casos. Reconstrução posterior também falhou. |

## Último diagnóstico e próxima decisão

Quatro novos IDs 27000–27003, duas famílias/resoluções, quatro checkpoints
congelados. Comparações: centro, área só em profundidade, área só em normais,
renormalização, módulo isolado, área completa e reconstrução após reduzir.

- O módulo participa da métrica de normais, mas corrigi-lo não garante melhor
  iluminação ou estabilidade com câmera real.
- Alterar apenas profundidade teve pouco efeito nesse domínio; normais tiveram
  influência maior. Não generalizar isso para toda rede/cena.
- Reconstrução posterior deixou cerca de 15% das células válidas sem normal,
  além de mudar o suporte espacial do ajuste.
- Nenhuma variante passou todos os critérios. Não reajustamos limiares depois
  de ver resultados nem promovemos um checkpoint reprovado.

**Próximo passo:** comparar normais reconstruídas e analíticas como controle
explicativo, com máscaras comuns/contagem de ausências; separar variação do
residual antes e depois da ampliação. Depois decidir entre corrigir entradas,
reconstrução ou estudar histórico reprojetado. Retomar intervenções, outros
domínios e benchmark GPU antes da integração da CNN no jogo.

## Conceitos considerados e limites importantes

- **Pré-upscale:** imagem menor pode reduzir custo, mas conversões, estados,
  buffers e sincronização também custam. A meta de poucos ms não está validada.
- **HDRnet/grade bilateral:** já experimentada; transformação afim de cor não
  equivale a iluminação física completa.
- **Normais/depth:** média vetorial mistura direção/módulo; média escalar também
  pode inventar profundidades entre superfícies. Reconstrução posterior exige
  intrínsecos corretos e muda suporte/validade.
- **Fase versus movimento:** deslocar artificialmente a grade mede sensibilidade;
  não prova que a origem oscile no jogo. Métricas diferentes não constituem uma
  decomposição causal automática.
- **Histórico:** hipótese futura para CNN, com desoclusão, reset e teste de
  ghosting; não apenas acumular para esconder instabilidade.
- **Gaussian Splatting:** não escolhido para uma DLL universal; representações
  treinadas de cena não surgem automaticamente de buffers de jogos arbitrários.
- **DirectML/ONNX:** candidato de runtime, não integração concluída da CNN.
  Compartilhar fila/dispositivo não resolve sincronização. Não presumir
  zero-copy, WMMA ou latência sub-2 ms por usar FP16.
- **D3D12:** o passe grava na lista recebida, sem fechá-la/submetê-la ou esperar
  CPU por quadro. Recursos/heaps precisam sobreviver à submissão real.
- **MFG:** desbloqueios NVIDIA não implementam interpolação AMD. Capacidade
  medida do runtime local: um quadro gerado por real.
- **Projetos externos:** análises registram fontes/limitações; não incorporamos
  runtime neural privado NVIDIA/terceiros à CNN própria. Veja o índice.

## Mapa do código

| Caminho | Responsabilidade |
| --- | --- |
| `_IA_Python/neural_lighting_model.py` | CNN, controle ponto a ponto, residual e loss de imagem. |
| `_IA_Python/neural_lighting_scene.py` | Renderizador, câmera e correspondências. |
| `_IA_Python/neural_lighting_diagnostics.py` | Normais e métricas de intervenção. |
| `_IA_Python/neural_regional_inference.py` | Cadeia regional de referência. |
| `_IA_Python/neural_sampling_phase.py`, `neural_geometry_ablation.py` | Ablações offline, não runtime do jogo. |
| `tools/train_neural_*.py`, `tools/diagnose_*.py` | Treinos e diagnósticos com saídas separadas. |
| `native/baseline/`, `native/integration/` | Harnesses GPU/CPU e contratos de integração. |
| `_OptiScaler_Source/` | Fork e dependências com licenças; mudanças de integração. |
| `tests/`, `artifacts/`, `docs/` | Testes, evidências/checkpoints e relatórios. |
| `_Shaders/`, `_Build_Final_Jogo/` | Legado, não pacote final validado. |

`TSREngine.cpp` é legado, não a nova CNN. Não reativá-lo como atalho. ONNX
antigos e o antigo `build.bat` não constituem uma entrega pronta da IA espacial.

## Reproduzir a pesquisa Python

Na raiz do clone, em ambiente separado:

```powershell
python -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r _IA_Python/requirements-bilateral.txt
.\.venv\Scripts\python.exe -m unittest discover -s tests -p 'test_neural_*.py' -v
.\.venv\Scripts\python.exe tools/diagnose_neural_geometry_ablation.py --out artifacts/neural-geometry-ablation-reproduction
```

Última regressão neural: **42 testes passaram**; a suíte Python completa passou
**55 testes** na preparação desta publicação. Ambiente observado: Windows,
Python 3.14.7, PyTorch 2.13.0+cpu, NumPy 2.5.2, ONNX 1.22.0, Pillow 12.3.0.
Dependências aceitam intervalos; não prometemos hashes idênticos em outro
ambiente. Um teste de render usa hashes locais estritos: investigar diferenças
numéricas antes de atualizar seus valores de referência.

O diagnóstico exige checkpoints/manifests históricos já incluídos, verifica
hashes e usa `torch.load(..., weights_only=True)`. Uma saída existente é
recusada. Ler `protocol.json`, `metrics.json`, `verification.json` e JSONs por
grupo. Não sobrescrever a evidência original.

Treinamento: ler os relatórios e `--help` de `train_neural_balance.py` e
`train_neural_regional.py`. Não é necessário repetir todos os treinos para
estudar o projeto. Checkpoints históricos incluem controles reprovados.

### DX12 e jogo

Windows: Visual Studio com C++, Windows SDK e CMake. Instruções completas no
[README nativo](native/baseline/README.md):

```powershell
cmake -S native/baseline -B build/native -A x64
cmake --build build/native --config Release
ctest --test-dir build/native -C Release --output-on-failure
.\build\native\Release\tsr_dx12_baseline.exe --full
```

Linux valida referências CPU; WARP valida DX12 por software, não performance
AMD. CI não é benchmark de jogo nem certificação visual da CNN. Scripts
`install_*`, `configure_*` e `set_witcher_*` alteram instalação/configurações
locais: leia-os antes de usar. O clone não instala nada automaticamente.

Na preparação desta publicação, os **42 testes CTest do build local Release
existente passaram**. Isso não foi uma recompilação integral do fork.
Detalhes das verificações em [publicação consolidada](docs/PUBLICATION_2026_09_10.md).

## Publicação, evidências e licenças

Incluímos código, pesos próprios, métricas, protocolos, imagens sintéticas,
logs técnicos e dados binários de referência. O clone é grande por preservar
evidência histórica e o SDK/fork já versionado. Caminhos Windows em JSONs/logs
são proveniência, não instruções portáveis para uma nova máquina.

Ficam fora do Git: ambientes virtuais, builds locais, instaladores baixados,
`Arquivos necessarios`, `DLSS-NR-on-AMD-0.2.11`, `dlss teste`, caches de fontes
externas e DLLs/EXEs/ZIPs sob `artifacts/`. Permanecem preservados localmente.
O contexto pessoal bruto é substituído por documentação técnica. Relatórios
registram fontes/hashes; menções a binários omitidos não significam que estejam
distribuídos neste clone.

Legado já versionado foi preservado, inclusive binários antigos; não é versão
recomendada. Licenças externas permanecem nos diretórios correspondentes.
Não atribuímos uma nova licença global aos componentes de terceiros. Cabeçalhos
FidelityFX API 1.1.4 têm [proveniência e licença](_OptiScaler_Source/external/FidelityFX-API-1.1.4/PROVENANCE.md).

Para colaborar: branch própria, protocolo antes do experimento, resultado
revisável e testes. Preservar resultados negativos. Melhor métrica sintética
não significa equivalência a DLSS 5 nem validação no jogo.
