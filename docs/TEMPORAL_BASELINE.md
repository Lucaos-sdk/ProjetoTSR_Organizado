# Baseline temporal DX12 — profundidade anterior explícita

Um caminho opcional posterior agora mantém [histórico na resolução de saída](OUTPUT_HISTORY_REVIEW.md), preservando amostras de diferentes fases de jitter. O modo padrão descrito abaixo continua usando histórico em baixa resolução.

A etapa seguinte já adiciona [grade de saída estável e avaliação visual](STABLE_GRID_REVIEW.md), com compensação do jitter no passe espacial. Essa documentação contém a evidência mais recente, incluindo 57 quadros na RX 7600 e a comparação sintética.

Atualização: o modo `PredictedPreviousZ` amplia o baseline inicial de câmera fixa. Ele recebe por pixel atual a profundidade da superfície na câmera anterior, usa esse valor no teste de desoclusão e trata zero como projeção inválida. O modo fixo permanece explícito para regressão. Fixtures pinhole independentes calculam raios, posições e motion em dupla precisão para translações de câmera em X/Y/Z.

Validação atual: **37 quadros** na RX 7600 com `--debug --full`, sem warnings/erros; inclui câmera avançando, deslocamento XYZ com jitter, desoclusão, projeção inválida e câmera móvel 1080p → 4K. Erro máximo temporal 3,8147e-06; saída 7,62939e-06. CTest **9/9** passou. Logs: `artifacts/temporal-validation/rx7600-camera-debug-full.log` e `ctest-camera.log`. A evidência de 29 quadros abaixo registra a etapa anterior.

Executável independente `tsr_temporal_dx12`, sem ONNX ou OptiScaler. Usa o contrato de `FRAME_CONTRACT.md`. É um teste numérico de reprojeção/acumulação em texturas próprias, não uma distribuição para The Witcher 3.

## Executar

```powershell
cmake -S native/baseline -B build/native -A x64
cmake --build build/native --config Release
ctest --test-dir build/native -C Release --output-on-failure
.\build\native\Release\tsr_temporal_dx12.exe --debug --full
```

`--warp` seleciona software explicitamente. `--debug` exige Graphics Tools e falha em warnings/erros da debug layer. `--full` acrescenta três quadros 1920×1080 com reconstrução espacial 3840×2160. O CTest executa referência CPU e WARP, inclusive a sequência grande.

## Algoritmo implementado

1. Validar todos os planos e parâmetros antes de alterar o estado da instância.
2. Invalidar histórico no primeiro uso, reset, alteração de resolução de entrada/saída ou índice não consecutivo.
3. Fazer upload da cor e de uma textura auxiliar RGBA32F com `(depth, motionX, motionY, predictedPreviousZ)`. No modo fixo, o último canal recebe depth atual. O empacotamento é interno ao harness, não é o tensor neural.
4. Calcular endereço anterior `pixelIndex + motion + currentJitter - previousJitter`. Rejeitar posições fora do intervalo de centros `[0, dimensão-1]`, sem clamp que reutilize bordas indevidamente.
5. Para cada tap bilinear de peso positivo, comparar a profundidade do histórico ao Z anterior previsto da superfície atual: rejeitar se qualquer diferença exceder `0,01 m + 0,01 * predictedPreviousZ`. Zero no Z previsto invalida todo o histórico do pixel. Não interpolar depth para esse teste.
6. Quando válido, RGB = `0,25 * current + 0,75 * reprojectedHistory * currentPreExposure / previousPreExposure`. Alpha vem integralmente do quadro atual. Quando inválido, copiar o quadro atual. Gravar Z atual e máscara binária em uma segunda textura.
7. A cor temporal na resolução de renderização alimenta diretamente o shader espacial GPU, que subtrai o jitter atual das coordenadas de leitura e produz uma grade de saída estável. O histórico continua em baixa resolução; não há histórico 4K de detalhes.

Cada `TemporalGpu` possui dois pares persistentes de texturas (cor + geometria), além dos metadados anteriores. O par de escrita alterna a cada quadro concluído; leitura e escrita nunca usam a mesma textura. Duas instâncias compartilham device/queue no teste, mas não histórico. As chamadas são síncronas, serializadas; não há promessa de thread safety.

Todos os recursos e descritores permanecem vivos até a fence. As transições UAV → cópia → SRV tornam explícita a dependência entre produção, leitura de validação e reconstrução. Os slots persistem em SRV entre chamadas e são recriados quando a resolução de entrada muda. Falhas GPU/validação encerram o executável; recuperação de device loss em um backend de produção não está implementada.

## Validação local

Windows, MSVC 19.51, **AMD Radeon RX 7600 [hardware]**:

- 29 quadros comparados GPU/CPU com `--debug --full`; nenhum warning/erro da debug layer.
- Máscaras de aceitação comparadas exatamente; cor temporal e saída ampliada comparadas componente a componente com a tolerância espacial existente.
- Referência independente em dupla precisão e valores esperados explícitos para mistura, alpha, exposição, sinais de movimento/jitter, reset e isolamento.
- Casos de bordas, taps fracionários cruzando profundidade, mudança de tamanho, índice descontínuo, ping-pong repetido, 1×1 e rejeição de NaN sem avançar histórico.
- Sequência grande com HDR/negativos, movimento fracionário, jitter, exposição e borda de profundidade em movimento.
- Erro máximo observado na cor temporal: 0 nestas fixtures; saída ampliada: 7,62939e-06.
- CTest: **9/9** aprovados, incluindo os testes espaciais anteriores.

Logs locais: `artifacts/temporal-validation/rx7600-debug-full.log` e `artifacts/temporal-validation/ctest.log`. Resultados da árvore local; ainda não houve execução remota da CI destas alterações.

## Limites e sequência seguinte

A comparação direta de Z no modo fixo só é válida sem mudança de Z da superfície correspondente. O modo de profundidade anterior prevista remove essa hipótese quando o dado fornecido está correto. A validação atual cobre cenas planas e translação de câmera; não certifica rotação, geometria arbitrária, objetos deformáveis ou a extração dos dados no jogo. A regra conservadora nas bordas pode rejeitar histórico válido.

Não há neighborhood clamp, reactive mask, tratamento de transparências/partículas, detecção automática de corte, confiança aprendida, rede treinada ou avaliação perceptual/temporal. Peso 0,75 e limiar de profundidade são parâmetros de baseline; podem produzir ghosting em conteúdo real. Concordância CPU/GPU não demonstra qualidade visual.

O harness faz readback de histórico, máscara e saída a cada quadro e aloca recursos transitórios para validação. Não é um caminho otimizado nem mede o orçamento de 3 ms. O próximo avanço é reconstrução numa grade estável e avaliação visual de sequências, ampliando a cobertura de câmera e objetos, antes de integração no jogo. A direção de longo prazo inclui [transformação neural de iluminação e materiais](PROJECT_DIRECTION.md).
