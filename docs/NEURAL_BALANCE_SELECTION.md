# Equilíbrio entre fidelidade e coerência temporal

Rodada de 09/09/2026, após o [treino conjunto](NEURAL_JOINT_TRAINING.md) reduzir
o erro temporal, mas ultrapassar o limite de regressão de imagem. O objetivo
é testar se uma penalidade temporal menor recupera fidelidade sem perder a
resposta às intervenções nem a coerência de câmera.

## Protocolo fixado antes da execução

Mesma CNN de 38.787 parâmetros, mesmas losses de imagem e resposta, mesmos
384 quartetos de treino (64 cenas, IDs 16000–16063), câmera e ruído da rodada
anterior. Para cada uma de duas inicializações novas, treinamos quatro modelos:

| Variante | Peso da loss temporal |
| --- | ---: |
| Controle com pares | 0 |
| t025 | 0,25 |
| t050 | 0,50 |
| t100 | 1,00 |

Todos recebem 1.600 passos Adam, taxa 0,002 e exatamente a mesma sequência de
quartetos (seed de amostragem 732). Os pesos iniciais são idênticos dentro de
cada inicialização. Seeds de inicialização: 20260911 e 20260912; são IDs, não
datas da execução. Não continuamos pesos já expostos à avaliação anterior.
São oito treinos com o mesmo orçamento em passos, sem escolha de checkpoint.

Validação: 16 cenas novas, IDs 19000–19015. Teste final: 24 cenas novas, IDs
20000–20023. Em cada conjunto, avaliam-se imagens, intervenções e câmera com
profundidade limpa e ruído de 0,2%. Imagem estática em yaw 0,07. Movimento de
câmera em cinco poses, nos quatro estados da geometria, como na rodada anterior.

## Seleção e critérios

Uma configuração precisa cumprir todos os critérios em **cada inicialização
e cada condição de ruído**, sem esconder um resultado ruim pela média:

- Resposta às intervenções pelo menos 20% melhor que resposta zero, e não mais
  que 5% pior que o controle com pares da mesma inicialização.
- MAE de imagem no máximo 5% pior e erro temporal pelo menos 20% menor.
- Alteração indevida em regiões estáveis até 0,003; conservação exata da região
  explicitamente protegida pela máscara de validade.
- Erro nas sombras, nas bordas geométricas e nos gradientes no máximo 5% pior.

Os três últimos critérios de detalhe complementam os da rodada anterior; não
mudam as losses de treinamento. Bordas são pixels válidos nos dois lados de
uma mudança de ID de superfície entre vizinhos. Não incluem todas as bordas
de textura ou sombra. Gradiente mede diferenças do erro RGB entre vizinhos
válidos, nos dois eixos. Ambos são agregados pela quantidade de amostras.
O erro de sombra usa a métrica preexistente: média das MAEs por imagem na região
onde a oclusão da luz de referência produz diferença acima de 0,005.

Entre configurações elegíveis, selecionamos a menor razão média de erro de
imagem frente ao controle. Se nenhuma for elegível, fixamos apenas um candidato
diagnóstico: aquele com a menor violação máxima dos limites normalizados. Esse
caminho não permite aprovação, mesmo se passar no teste final. Desempate pela
razão média de imagem e depois nome da variante.

`selection.json` é gravado antes de renderizar qualquer cena do teste final.
Apenas o peso selecionado e o controle, nas duas inicializações, são avaliados
nesse teste. Não há ensemble, escolha da melhor inicialização ou busca posterior
de parâmetros no teste. Exigir aprovação tanto na validação quanto no teste
continua sendo condição necessária, mas insuficiente, para integração no jogo.

## Reprodução e limites

```powershell
.\_IA_Python\venv\Scripts\python.exe -m unittest discover -s tests -p test_neural_balance_selection.py -v
.\_IA_Python\venv\Scripts\python.exe tools/train_neural_balance.py --out artifacts/neural-balance-replay --steps 1600
```

O runner recusa sobrescrever a pasta de saída. Salva protocolo e manifesto das
fontes antes do treino, pesos e curvas de cada modelo, validação, decisão fixada,
hash dos dados/ordem, métricas finais por cena e comparativo da primeira cena
de teste nas duas inicializações. Saída desta rodada: `artifacts/neural-balance-v1`.

Continuamos no domínio limitado de esferas difusas e chão com luzes fixas.
Não é treino com capturas do jogo, síntese de materiais ou iluminação indireta.
As sequências têm câmera móvel sobre geometria estática, não animação de objetos.
A inferência segue sem histórico; a preservação de UI/céu vem da máscara explícita.
Não há alteração da V2.3, exportação FP16, medição de GPU/VRAM ou instalação.

## Seleção na validação

Os oito modelos completaram os 1.600 passos. A variante **t025** foi a única
a passar nos nove critérios nas duas inicializações e duas condições. Sua razão
média de erro de imagem foi 0,977823, aproximadamente 2,22% menor que os controles.
Esse valor é da validação usada na seleção, não uma estimativa independente final.

| Peso temporal | Elegível na validação | Razão média de MAE de imagem | Critérios reprovados |
| --- | --- | ---: | --- |
| 0,25 | Sim | 0,977823 | Nenhum |
| 0,50 | Não | 1,050546 | Imagem, sombras e bordas |
| 1,00 | Não | 1,022748 | Sombras e bordas |

Os critérios de detalhes revelaram regressões que a média global de imagem
não mostraria sozinha. A configuração 1,00, por exemplo, conservou a média global
dentro do limite, mas não conservou sombras e bordas em todas as inicializações.

Decisão congelada em `selection.json`, SHA256
`a6166a3d36dc2a1eba99e5d7aabe9e235a30086aef8d534b208b81128d2a9fc0`.
Ela escolhe a configuração de loss para os dois modelos, sem escolher a melhor
inicialização e sem misturar suas saídas.

Os 22 testes neurais passaram. Os três novos verificam a medição de bordas e
gradientes, a reprovação quando apenas uma inicialização viola um limite e a
prioridade da elegibilidade sobre uma média de imagem aparentemente melhor.

## Resultado no teste final independente

**t025 passou nos nove critérios também no teste final**, em ambas as
inicializações e condições de ruído. `synthetic_gates_passed=true`.
`game_integration_allowed=false` permanece: aprovação desta tarefa sintética
não demonstra generalização ao jogo nem viabilidade de execução na RX 7600.

Intervalos abaixo são mínimo e máximo entre as quatro combinações de
inicialização/ruído, frente ao respectivo controle com pares. Não são intervalos
de confiança, nem comparação com a DLL V2.3 ou com FSR/DLSS.

| Métrica | Redução do erro no teste |
| --- | ---: |
| Coerência temporal reprojetada | 31,49% a 38,32% |
| Resposta à remoção/deslocamento de objetos | 4,19% a 9,69% |
| Imagem geral | 0,74% a 2,54% |
| Região de sombras | 2,57% a 10,03% |
| Bordas geométricas | 6,79% a 13,10% |
| Gradientes entre vizinhos válidos | 1,97% a 3,82% |

A melhora global de imagem é pequena: razão média 0,983975, cerca de 1,60%
de redução do erro. O ganho principal é obter coerência de câmera junto com
conservação/melhoria da fidelidade, sem a regressão que reprovou a rodada anterior.
Não se deve comparar diretamente os 31–38% desta rodada aos 73% da anterior:
as cenas e inicializações de avaliação são diferentes, e esta seleção também
exige preservação de detalhes. Os controles válidos estão dentro de cada rodada.

| Profundidade limpa | Controle A | t025 A | Controle B | t025 B |
| --- | ---: | ---: | ---: | ---: |
| MAE de imagem | 0,02567332 | 0,02510300 | 0,02518639 | 0,02499969 |
| MAE de resposta | 0,02061096 | 0,01964412 | 0,02065817 | 0,01865716 |
| Erro temporal | 0,01030997 | 0,00635920 | 0,00976051 | 0,00651929 |
| MAE em sombras | 0,04679821 | 0,04237099 | 0,04191411 | 0,04083640 |
| MAE em bordas | 0,02079847 | 0,01938640 | 0,02273959 | 0,01976095 |

O erro de resposta zero foi 0,03435643. A redução da candidata frente a ele
ficou entre 42,79% e 45,88%. A alteração indevida média em regiões estáveis
ficou entre 0,00179546 e 0,00208354; as áreas protegidas permaneceram exatas.
Há 72 pares de intervenção por condição, 5.587 pixels alterados e 234.855
estáveis acumulados. A avaliação temporal usa 384 pares por condição, 768 no
total. Essas amostras repetem superfícies e não representam jogos independentes.

O comparativo da primeira cena reservada foi inspecionado: contém três esferas
e mostra os dois controles e as duas candidatas, sem escolher o melhor seed.
Persistem suavização, diferenças de cor e sombras aproximadas frente ao render
de referência. Passar os critérios relativos não significa reproduzir esse
render com fidelidade absoluta nem transformar personagens de um jogo.

## Integridade e continuidade

Foram verificados os hashes das 11 fontes registradas, dos oito checkpoints
e da seleção congelada. O hash dos quartetos e o da ordem de treinamento
coincidem com os da rodada conjunta anterior, confirmando a reutilização
exata dos dados. Conferências em `verification.json`.

Pesos escolhidos, mantidos separados:

- `20260911_t025.pt`: SHA256 `bbc0f8abdd0ef39c5f077ba98d57bd3a02438460b39148733c0ab05f38fcac49`.
- `20260912_t025.pt`: SHA256 `4d56f29cc64afc20d6019d333322b9ddbe3b9bc495e7048dfbb2669acb4421f6`.

A DLL do jogo continua com SHA256
`9B3992A1FD4337A1C235DC3401D59F8BC81614E29585F3F20546B4C9BC6D47D0`.
Não houve exportação, cópia de pesos para o jogo ou alteração de configurações.

Próximo passo recomendado: congelar t025 como referência desta tarefa e ampliar
o teste para geometrias, distâncias e iluminação mais variadas. Começar por
um diagnóstico dos pesos congelados, preservando a referência física e
comparando ambas as inicializações, para localizar a perda de generalização.
Se for necessário treinar nessa nova família, separar novamente treino,
validação e teste. Não continuar procurando coeficientes nos IDs já avaliados.

Também será necessário testar resoluções maiores: resultados em 96×64 não
garantem comportamento nem campo de visão contextual adequados na resolução
interna do jogo. Com essa avaliação visual, avançar ao executável GPU isolado
para conversões, precisão FP16, tempo e VRAM, antes da integração na DLL.
