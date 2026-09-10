# Teste de fluidez 2×

Abra o Witcher 3 em DirectX 12 e mantenha XeSS Qualidade em 1920×1080 nas opções do jogo. Carregue um save e pressione Insert.

O OptiScaler deve mostrar FSR 4.1.1 como reconstrução, entrada de geração OptiFG/Upscaler e saída XeFG, com Active habilitado. O objetivo é apresentar um quadro gerado entre dois quadros reais. **Não é multigeração 3× ou 4×**: a consulta na RX 7600 retornou limite de 2× neste runtime.

Teste por um ou dois minutos, girando a câmera, correndo e observando o minimapa, legendas, folhagem e contornos. Depois feche o jogo e avise para analisarmos `TSR_FrameGen.log`. O ganho e a qualidade ainda precisam ser confirmados em jogo; geração de quadros não reduz automaticamente o tempo do quadro real ou a latência dos comandos.

Se precisar voltar ao teste anterior com FSR 4.1.1 sem geração, feche o jogo e execute **Restaurar FSR sem geracao.cmd**. Ele preserva o backup e interrompe se detectar alterações posteriores nos arquivos.

Não mova esta pasta nem `witcher3-fsr411`: a instalação carrega as bibliotecas dessas pastas do projeto. A DLL contém correções de carregamento das bibliotecas, validação de quantidade e diagnóstico de geração. Upscaling e interpolação continuam sendo algoritmos da AMD e da Intel, respectivamente.
