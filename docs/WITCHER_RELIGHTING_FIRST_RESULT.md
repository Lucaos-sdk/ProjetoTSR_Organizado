# Primeiro resultado visual confirmado — 07/09/2026

O usuário jogou uma sessão longa com `witcher3-relighting-hooks-fix`, considerou o efeito visual interessante e relatou dois defeitos: a sombra/aparência do chão muda de maneira estranha dependendo do ângulo da câmera; a pele de alguns personagens parece poligonal em vez de lisa. Não há captura de imagem ou sequência desses defeitos nesta sessão. O relato positivo confirma percepção do efeito, não equivalência a DLSS 5 nem melhoria universal.

## Evidência de execução

- DLL instalada confirmada por SHA256: `5A9899A4496620B566DD9229C64B7665F294893232DBC0B5FE812139AE539996`.
- Sessão registrada de 15:06:56 a 15:46:23, encerramento normal, zero linhas `[E]`.
- Hooks de envio e Reset instalados com sucesso, independentes do HUD fix.
- Pelo menos **132.601 quadros gravados** pelo passe próprio no último registro amostrado. A contagem não inclui necessariamente os quadros entre a última amostra e o encerramento.
- Tempo do passe nas amostras: mediana **0,09048 ms**, percentil 95 **0,0926 ms**, máximo **0,19288 ms**. São timestamps do passe próprio e suas barreiras, lidos após conclusão; não são latência de entrada, tempo total do jogo ou medição de todos os quadros.
- Nas amostras disponíveis do FSR: versão 4.1.1, resultado OK, entrada 1476×830 e saída 1920×1080, com `own_neural=true` após ativação. Esse log do FSR para de amostrar após 18.000 chamadas; não alegar confirmação individual das chamadas posteriores.
- 232 amostras de câmera com candidato único, todas com atraso de uma publicação, idade mediana 16 ms e máxima 32 ms. Associação continua experimental por jitter.

O log do passe contém amostras periódicas **e** mudanças de estado. Por isso 469 registros ativos em 730 registros não significam 64% dos quadros ativos. O contador acumulado é a evidência de quantidade de trabalho.

## Interrupções separadas da avaliação visual

O analisador foi ampliado para contar intervalos completos ativo → bypass → ativo, sem cruzar reinícios de contador. Houve 150 intervalos cuja razão inicial não era o controle desligado, somando 372 chamadas: cinco de uma chamada, 143 de duas, um de 42 e um de 39. Destes, 149 começaram com câmera/recursos sem correspondência e um com bindings não suportados. Outros 97 intervalos começaram com o controle desativado/intensidade zero e são contabilizados separadamente.

As mudanças do passe também reiniciam o histórico do FSR; isso oferece uma hipótese para piscadas/reconstrução transitória. Não prova que sejam a causa dos dois defeitos visuais relatados. A razão atual combina várias condições de câmera e recursos; os dados não distinguem reset explícito, ausência de candidato, origem ou identidade de textura em cada interrupção. Não reutilizar câmera antiga ou retirar as verificações para esconder o problema.

## Prioridades de correção

1. **Iluminação estável em relação à cena.** O shader atual usa normais no espaço da câmera: o comportamento do chão é compatível com essa limitação conhecida. O modelo foi treinado para o hemisfério Z positivo nesse espaço. Simplesmente girar essas normais para o mundo pode produzir entradas fora do domínio de treino; será necessário adaptar/validar modelo e convenção de orientação junto à correção, com cenas sintéticas rotacionadas.
2. **Superfícies suaves.** A aparência facetada é compatível com normais estimadas diretamente da profundidade, sem as normais suavizadas e detalhes do material usados pelo jogo. Avaliar uma estimativa espacial que suavize variações internas e preserve silhuetas/descontinuidades; testar pele/superfície curva e bordas. Intensidade menor pode atenuar, mas não demonstra correção da causa. Atribuição ainda é hipótese sem comparação de imagem.
3. **Interrupções curtas.** Distinguir as causas na ponte e corrigir transições preservando a validade das entradas. Tratar separadamente dos problemas de orientação e suavidade.

Esta análise preservou a DLL que já produziu resultado para servir de comparação. Não foram alterados shader, modelo ou configurações do jogo. O trabalho desta etapa foi arquivar os dados, quantificar execução/custo/interrupções e registrar os defeitos relatados.

Arquivos: `artifacts/temporal-validation/witcher-relighting-active-game.log`, `.json` e `witcher-relighting-active-analysis.txt`. SHA256 do log: `12dac755d2603b484e6d8abc267be558c252e9d1c09e492e8db77a36108a75eb`.
