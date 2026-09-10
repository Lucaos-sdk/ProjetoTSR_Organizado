# Regiões de textura na cadeia GPU

A conversão de entrada aceita uma origem X/Y dentro das texturas de cor, movimento e profundidade. O resultado empacotado continua começando em (0,0), com o tamanho de renderização; os vetores de movimento permanecem em pixels dessa área, sem deslocamento artificial pelo tamanho da textura maior.

O passe de saída aceita tamanho, origem de leitura e origem de escrita distintos. Antes de gravar comandos, os passes validam que as regiões cabem nos tamanhos informados pelo chamador. A verificação evita soma de coordenadas que poderia transbordar um inteiro. O chamador ainda deve garantir que as dimensões informadas correspondam aos recursos e descritores reais.

## Teste de preservação

`tsr_input_chain_dx12 --regions` coloca a imagem de entrada na origem (2,3), em uma textura com cinco colunas e sete linhas extras. As bordas recebem dados deliberadamente diferentes; a referência CPU continua usando apenas a imagem útil. Isso detecta leitura na origem errada ou uso indevido da área externa.

O destino tem sete colunas e cinco linhas extras. O teste copia uma região da reconstrução começando em (1,1) para (3,2), preservando o restante do destino. A textura final é inicialmente preenchida com um padrão conhecido e todos os pixels fora da região são comparados exatamente ao padrão depois da execução. A cópia interna é verificada pixel a pixel, com a tolerância de formato FP16 quando aplicável.

São seis quadros na mesma submissão, mantendo os testes de histórico, rejeição por profundidade, informação anterior indisponível e reset. As cópias de diagnóstico só ocorrem após o processamento. A origem de leitura do passe de saída é intencionalmente não zero; o teste recorta uma borda da reconstrução para exercitar esse caso.

## Escopo atual

- Entradas precisam compartilhar tamanho e origem. Origens ou resoluções diferentes para cor/movimento/profundidade continuam pendentes.
- Texturas 2D sem MSAA; formatos FP16/FP32 já documentados. Camadas, mipmaps e formatos de profundidade combinados não foram adicionados.
- Não altera o histórico para armazená-lo em texturas maiores; o histórico continua no tamanho da saída reconstruída.
- As verificações por tamanho são pré-condições do chamador, não introspecção dos recursos escondidos pelos descritores.
- Não conecta o código ao OptiScaler nem implementa gerenciamento de vários quadros em voo.

## Execução e evidência

```powershell
.\build\native-rx7600\Release\tsr_input_chain_dx12.exe --debug --regions --fp16 --1080p
.\build\native-rx7600\Release\tsr_input_chain_dx12.exe --debug --regions --1080p
```

Na RX 7600, os dois modos passaram com debug layer para entrada útil 1280×720 e reconstrução 1920×1080. O destino ampliado mede 1927×1085; a cópia de teste ocupa 1918×1078, com os pixels restantes preservados. Logs: `artifacts/temporal-validation/regions-fp16-rx7600.log` e `regions-fp32-rx7600.log`.

O próximo avanço recomendado é reunir os passes em um contexto com recursos e descritores reutilizáveis por quadro em voo, antes do backend no fork. O teste atual retém imagens dos seis quadros para comparar os resultados; essa política não deve ser copiada para o runtime do jogo.

Build Release concluído; 26/26 testes CTest passaram, incluindo limites/overflow de regiões e a cadeia WARP com regiões em FP16/FP32. Log completo: `artifacts/temporal-validation/ctest-regions.log`.
