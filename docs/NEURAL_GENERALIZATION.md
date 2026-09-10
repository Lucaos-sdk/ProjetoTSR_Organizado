# Generalização e escala da inferência neural

Rodada de 09/09/2026, após a [seleção de t025](NEURAL_BALANCE_SELECTION.md).
Dois experimentos foram concluídos sem retreinamento, ajuste de pesos ou
instalação no jogo: diagnóstico de domínio/resolução e uma hipótese de correção
da escala de análise. Todos os checkpoints permanecem congelados.

## 1. Diagnóstico de domínio e resolução

`tools/diagnose_neural_generalization.py` avalia ambas as candidatas t025 e
seus respectivos controles com pares, mais a entrada sem correção. Quatro
cenas novas, IDs 21000–21003. Cada cena é renderizada em 96×64, 192×128 e
384×256, mantendo aspecto 3:2 e FOV vertical; não é redimensionamento de uma
captura pronta. Profundidade limpa, normais reconstruídas, câmera em yaw
−0,04/0/+0,04, geometria original e remoção de todos os objetos.

Quatro fatores, avaliados separadamente:

- **Baseline:** esferas e chão, família original de treinamento.
- **Blocos:** substitui esferas alternadas por caixas apoiadas no chão,
  com quinas e alturas diferentes. Raios primários e de sombra intersectam caixas.
- **Distância:** desloca as esferas quatro unidades para longe no eixo z.
- **Luz de entrada:** varia posição, intensidade e cor da luz de entrada,
  mantendo o mesmo render de referência. A rede não recebe um comando de luz
  desejada; variar arbitrariamente a luz-alvo seria exigir informação inexistente.

São 12 combinações domínio/resolução, 288 renders, oito imagens centrais,
quatro intervenções e 16 pares temporais por combinação. Os nove critérios
mantêm limites relativos da rodada anterior, aplicados ao controle da mesma
inicialização e combinação. O erro nas sombras aqui é ponderado por amostras,
ao contrário da média por imagem da rodada anterior. Não comparar os números
absolutos entre esses protocolos como se fossem idênticos.

Resposta causal requer pelo menos 100 pixels alterados e duas cenas com
alteração relevante. Cobertura insuficiente produz resultado inconclusivo;
outro critério já reprovado continua produzindo falha. Não transformar ausência
de amostras em aprovação. Proteção de UI/céu é explícita, não aprendida.

### Resultado do diagnóstico

| Família | 96×64 | 192×128 | 384×256 |
| --- | --- | --- | --- |
| Esferas / baseline | A e B passaram | A e B falharam | A e B falharam |
| Blocos | A falhou; B passou | A e B falharam | A e B falharam |
| Objetos distantes | A e B inconclusivos | A e B falharam | A e B falharam |
| Luz de entrada alterada | A falhou; B passou | A e B falharam | A e B falharam |

O caso distante em 96×64 teve apenas 39 pixels alterados acumulados: insuficiente
para a conclusão causal prevista. Em resoluções maiores a cobertura aumentou,
mas os critérios completos não passaram. Falhas frequentes foram resposta
insuficiente às remoções e perda da vantagem temporal de pelo menos 20%.

Mesmo a família original perde qualidade quando executada diretamente em mais
pixels. Em 384×256, o MAE de imagem foi 0,05411615 (t025 A) e 0,05859700 (t025 B),
contra 0,05594281 da entrada sem correção. B ficou pior que a entrada nessa média.
A resposta de A foi 0,03294529, pior que resposta zero, 0,03265116; B obteve
0,02971894, sem alcançar a melhora mínima de 20% frente a zero.

Isso localiza um problema de escala além de variedade de dados. Uma CNN com
vizinhança fixa em pixels analisa uma fração menor da cena quando a resolução
aumenta. Esse é um mecanismo plausível, não uma prova de causa exclusiva: o
footprint das normais, as bordas, o padrão de amostragem e a máscara de UI também
têm tamanho fixo em pixels. As comparações visuais mostram diferenças amplas
de tom, suavização, faixas e sombras incorretas. Não há validação em 1080p.

## 2. Hipótese: análise regional com escala fixa

Implementada em `_IA_Python/neural_regional_inference.py`. A rede analisa uma
imagem com altura interna 64, preservando o aspecto. Cor é reduzida por área;
normais/depth e validade usam amostra pelo centro do pixel. A rede prevê seu
residual RGB limitado, que é ampliado bilinearmente e somado à **cor original
de alta resolução**, com clamp e máscara de validade originais.

Não ampliamos a imagem pequena inteira: o detalhe da entrada não é substituído
por uma versão reduzida. O teste de residual constante verifica justamente
essa distinção. Em altura até 64, o caminho original permanece idêntico.
Não há novas camadas, sharpening, treinamento ou informação privilegiada.

Essa é uma primeira hipótese, não um runtime de produção. A ampliação bilinear
do residual não respeita automaticamente silhuetas; a seleção de geometria
pelo centro pode variar com a câmera. A análise recebe normais calculadas na
resolução original, sem refazer o ajuste no domínio reduzido. Esses são fatores
a separar nos próximos testes, não causas individuais já demonstradas.

### Protocolo separado e resultado

`tools/diagnose_neural_regional.py`, quatro cenas novas (22000–22003), esferas
e blocos, resoluções 192×128 e 384×256. Mesmos três ângulos de câmera e dois
estados de geometria. Compara execução direta e regional dos **mesmos pesos
t025 A/B**, sem escolher a melhor inicialização. São 96 renders, com 16 pares
temporais e quatro intervenções por combinação domínio/resolução.

| Comparação regional versus direta | Resultado nas duas famílias e inicializações |
| --- | --- |
| MAE de imagem, 192×128 | Redução de 21,54% a 30,04% |
| MAE de imagem, 384×256 | Redução de 33,48% a 42,41% |
| Resposta às remoções, todas as combinações | Redução do erro de 0,56% a 23,72% |
| Erro temporal, 384×256 | Redução de 20,03% a 31,94% |
| Erro temporal, 192×128 | Desde redução de 4,88% até aumento de 6,47%; nenhum atingiu 20% |

São intervalos entre as combinações deste pequeno diagnóstico, não intervalos
estatísticos. O ganho é frente à execução direta que falhou em escala, não
frente a uma referência de iluminação já correta, nem frente ao FSR ou DLSS.

O MAE geral regional ficou entre 0,02764222 e 0,02838794 nas oito combinações,
mais consistente entre as duas resoluções que a execução direta. A resposta
frente a zero melhorou pelo menos 26,45% em todos esses casos. As métricas
de sombras, bordas e gradientes passaram nos limites relativos.

**Ainda reprovado como caminho geral.** Em 192×128, ambas as inicializações
falham na melhora temporal exigida. A também altera excessivamente regiões
que deveriam ficar estáveis: desvio médio de 0,00342–0,00370, acima de 0,003.
B passa todos os critérios nas duas famílias a 384×256, mas não nas resoluções
menores. Não escolher B e ignorar as outras falhas. `all_checks_passed=false`
e `game_integration_allowed=false`.

O comparativo da primeira cena foi inspecionado. A abordagem regional recupera
parte da estrutura e do tom perdidos pela execução direta, mas persiste diferença
de sombras frente à referência. Quadros estáticos não demonstram estabilidade;
essa conclusão vem das sequências reprojetadas. Nenhuma imagem é captura de jogo.

## Validação e reprodução

31 testes neurais passaram, incluindo interseção/normal das caixas, sombras,
preservação do alvo na troca de luz, compatibilidade de três renders antigos
com hashes registrados localmente antes da mudança, inferência na mesma escala,
imagem original preservada e tratamento de cobertura causal insuficiente.

```powershell
.\_IA_Python\venv\Scripts\python.exe -m unittest discover -s tests -p 'test_neural_*.py' -v
.\_IA_Python\venv\Scripts\python.exe tools/diagnose_neural_generalization.py --out artifacts/generalization-replay
.\_IA_Python\venv\Scripts\python.exe tools/diagnose_neural_regional.py --out artifacts/regional-replay
```

Artefatos: `artifacts/neural-generalization-v1` e
`artifacts/neural-regional-inference-v1`. Ambos preservam protocolo anterior
à avaliação, hashes dos pesos, fontes e dados, métricas por cena e comparativos.
Os runners recusam sobrescrita. Tempos totais observados em CPU foram cerca
de 108 e 54 segundos, respectivamente; não são latência de inferência GPU.

O renderizador histórico recebeu argumentos opcionais para caixas e luz de
entrada. Seu arquivo mudou, portanto manifestos antigos de fontes refletem
uma revisão anterior. Os checkpoints antigos não foram alterados; três casos
do caminho padrão continuaram byte a byte iguais neste ambiente. Não reescrever
manifestos antigos para fingir que a fonte sempre foi esta revisão.

## Próximo passo recomendado

Usar o caminho regional como **hipótese a treinar e validar**, preservando os
pesos atuais como controle. O treino precisa incluir a cadeia real de redução
de entradas e ampliação do residual, diferentes escalas, blocos e movimento
de câmera. A supervisão deve medir tanto o resultado na resolução final quanto
a resposta às intervenções e a conservação de regiões estáveis. Reservar novas
cenas para seleção e teste, sem ajustar parâmetros aos IDs deste diagnóstico.

Separar numa comparação controlada a reconstrução guiada por geometria da
simples ampliação bilinear, para medir vazamento pelas silhuetas. Depois ampliar
luz/distância e ruído, ausentes do segundo diagnóstico. Apenas reduzir a imagem
da IA não resolve a estabilidade por si só.

O teste GPU continua posterior à validação visual desse contrato. Permanecem
pendentes resolução interna real do jogo, precisão FP16, conversões, custo GPU,
VRAM, sincronização e integração. Nenhuma alteração na DLL V2.3 ou no FSR.
