# Continuidade para outra IA ou colaborador

Atualizado em 10/09/2026. Contexto técnico compartilhável, sem depender da
conversa privada. Leia o README e os relatórios vinculados antes de editar.

## Prioridades e estado

O usuário quer iluminação aprendida com diferença significativa via OptiScaler,
na RX 7600/8 GB e 1080p. Não é pedido para apenas acrescentar nitidez/color grading.
Compatibilidade ampla é ambição; não prometer funcionamento em qualquer jogo.

- V2.3 de 171 parâmetros permanece no jogo, separada da CNN espacial.
- CNN: 38.787 parâmetros, residual RGB limitado, ainda offline.
- Último diagnóstico: `artifacts/neural-geometry-ablation-v1`; 42 testes neurais
  passaram. Nenhuma variante nova está aprovada para integrar.
- Ler `NEURAL_GEOMETRY_ABLATION.md`, `NEURAL_SAMPLING_PHASE.md`,
  `NEURAL_REGIONAL_TRAINING.md` e `NEURAL_GENERALIZATION.md`, nessa ordem.
- `TSREngine.cpp` antigo não implementa o caminho atual. Não reativá-lo como atalho.

## Checkpoints usados no último diagnóstico

- `artifacts/neural-balance-v1/20260911_t025.pt`
- `artifacts/neural-balance-v1/20260912_t025.pt`
- `artifacts/neural-regional-training-v1/20260911_joint.pt`
- `artifacts/neural-regional-training-v1/20260912_joint.pt`

Os scripts leem `training.json`, manifests anteriores e verificam SHA256.
Não renomear arquivos históricos sem revisar dependências. Outros checkpoints
são controles/histórico, não versões automaticamente superiores.

## Próximos passos recomendados

1. Protocolo com pesos congelados e cenas separadas: comparar normais
   reconstruídas e analíticas como controle, com máscaras comuns e ausências.
2. Separar variação do residual antes/depois da ampliação e erro de reprojeção.
3. Decidir entre corrigir entradas/reconstrução e investigar histórico. Acumulação
   exige movimento, desoclusão, reset e medição de ghosting.
4. Retomar remoção de objetos, luz/distância, ruído e resoluções. Só depois:
   exportação FP16, timestamps GPU, memória e integração da CNN.

Não precisa repetir todos os treinos históricos. Cada etapa deve produzir
conclusão delimitada antes de gastar outra rodada de treinamento.

## Disciplina experimental

Registrar previamente hipótese, IDs, pesos, operadores, máscaras, métricas e
critérios. Controles de treino com orçamento e inicialização equivalentes.
Selecionar pela validação; não ajustar loss no teste reservado. Preservar falhas.

Métrica temporal existente: reprojetar erros `predição − alvo do próprio quadro`,
permitindo mudanças legítimas de luz. Correspondências nearest e máscaras têm
limitações e variam por amplitude/resolução. MAE estático, fase artificial e
estabilidade em movimento não são intercambiáveis.

Os critérios recentes exigiram pelo menos 20% de ganho temporal, imagem no
máximo 5% pior e outras verificações conforme protocolo. Não alterar limiares
depois de ver falhas. Sucesso sintético não é sucesso em jogo.

## Integração

Respeitar recursos, estados, heaps e vidas úteis D3D12. O passe registra na
lista do chamador; não assume posse, não fecha/submete a lista e não espera
CPU por quadro. Fences de submissão real não podem virar contadores presumidos.

O contrato `native/integration/game_relighting_dx12.h` ainda não consome motion
para a CNN. Não presumir normais exatas, albedo/roughness ou máscaras perfeitas.
Antes de inferência ONNX/DirectML, medir conversões, sincronização e VRAM.
FP16 não garante latência de poucos ms.

A V2.3 não modifica pesos AMD. Seu tempo amostrado de ~0,236 ms não estima
custo da CNN. Os 6–10 ms relatados pelo usuário em sessões com configurações
diferentes não são benchmark controlado.

## Colaboração

Usar branch própria e nova pasta `--out`. Não sobrescrever resultados ou
instalar DLL experimental para estudar código offline. Scripts de instalação
são específicos da máquina e constituem uma ação separada.

Ao relatar, indicar mudança, controle, resultado e falhas/limites. O proprietário
quer progresso real; não ocultar tradeoffs com um “sucesso” genérico. Este arquivo
é orientação do projeto, não conteúdo privado da conversa nem promessa de prazo.
