# Integração gráfica experimental — 07/09/2026

Esta versão conecta o modelo próprio de 171 parâmetros à imagem real recebida pelo OptiScaler. O passe lê cor HDR e profundidade, reconstrói uma normal local e prevê um ganho RGB de iluminação. Sua saída temporária é entregue ao FSR; a textura original do jogo e o modelo AMD permanecem intactos. O passe ocorre antes da reconstrução, e não é RCAS/nitidez.

## Limites da experiência

O modelo `fixture_v1` aprendeu uma transformação sintética de iluminação difusa. Não recupera materiais físicos, não conhece as luzes da cena e não faz renderização neural comparável ao DLSS 5. A iluminação é relativa à câmera: pode parecer acompanhar a visão ao girar. A qualidade em jogo ainda precisa de comparação visual; a existência de inferência não prova melhoria.

A câmera é selecionada pelo candidato único de jitter no histórico validado, sem fórmula fixa de atraso. A chamada XeSS guarda em um escopo de thread as identidades exatas da lista de comandos e das texturas; o passe exige essas mesmas identidades e dimensões. Isso vincula recursos à chamada, mas **não converte o candidato de câmera em uma identidade explícita de quadro Streamline**. O log conserva `camera_match=unique_jitter_experimental`. Esta é uma opção experimental, desativada por padrão e ativada no pacote de teste autorizado para The Witcher 3.

O caminho atual aceita entrada XeSS DX12, cor linear RGBA16F/RGBA32F e profundidade R32F, R32_TYPELESS ou R32G8X24_TYPELESS (plano de profundidade R32_FLOAT_X8X24_TYPELESS). Exige texturas de uma camada/mip/amostra, dimensões iguais à renderização e origens zero. Outros caminhos, formatos, sub-regiões, resets e câmeras ausentes/ambíguas usam a imagem original. Não se promete compatibilidade universal nem suporte a qualquer resolução/placa já validado.

## Controles de teste

- Iniciar The Witcher 3 em DX12; manter XeSS Ultra Qualidade e as configurações pessoais atuais.
- **F8** alterna o passe durante a partida. O pacote começa ligado com intensidade 0,35.
- No menu do FSR dentro do OptiScaler há `TSR experimental relighting (F8)`, `TSR intensity`, estado `ACTIVE/BYPASS`, contador e tempo GPU do último passe concluído.
- F8/intensidade valem para a sessão. O início é configurado em `[TSRRelighting]`, `Enabled=1`, `StrengthPercent=35`, no INI. Para desativação persistente: `Enabled=0`.
- Comparar parado, alternando F8, olhando chão, paredes e rosto/roupas. Depois girar a câmera e observar mudanças de iluminação, cintilação ou rastros. Não usar ganho de FPS gerados como prova de menor latência.

O histórico do FSR é reiniciado ao mudar entre passe ativo/inativo ou alterar a intensidade, para evitar mistura entre as duas aparências. A troca ainda pode ter um breve período de reconstrução.

## Segurança gráfica e custo

O passe usa os estados de leitura que o backend FSR já estabelece para cor/profundidade. Não muda as entradas. Sua saída faz o ciclo leitura → UAV → leitura e segue assim após a chamada ao FSR, inclusive em replay. Há seis slots, com descritores imutáveis por gravação, referências COM para entradas, saída, programa/pesos e fences por fila real. A notificação ocorre depois do `ExecuteCommandLists` do jogo. Reset bem-sucedido/destruição encerra a possibilidade de replay; isso sozinho não libera recursos que a GPU ainda usa.

O passe exige confirmação de instalação dos hooks de envio e Reset. Se não há slot livre, registra o motivo e usa a imagem original sem espera de CPU. Slots ocupados na destruição do backend são retidos até encerrarem a gravação e todos os fences; falha de sinalização impede reutilização. A lista do jogo nunca é fechada, reiniciada ou enviada pelo passe. Alocação/compilação ocorre na inicialização ou mudanças de tamanho, não a cada quadro após aquecimento.

Dois timestamps medem o passe próprio e suas barreiras; os resultados só são lidos quando o slot está livre e a GPU terminou. `gpu_ms` não inclui o FSR, frame generation, o quadro inteiro ou latência de entrada. Ainda não há medição desta versão dentro do jogo no momento da preparação do pacote.

## Validação reproduzível

`tsr_game_relighting_dx12` usa a mesma classe de produção e os mesmos pesos/shader embutidos. Compara saída GPU com referência CPU em dimensões ímpares, FP16/FP32, profundidade simples e depth/stencil nativa, céu, descontinuidades, alpha e entradas inválidas. Repete a lista realmente na GPU, rejeita gravação duplicada e prende seis envios atrás de um fence para verificar que destruir listas não libera buffers antes da conclusão. A camada de diagnóstico fica ativa. Só é filtrado o aviso de otimização do clear: o teste deliberadamente limpa pixels de profundidade com valores diferentes.

O teste passou em WARP e RX 7600. O maior erro absoluto na comparação FP16 foi 0,000488222; FP32 na RX 7600 foi 0,00000208616. Esses números medem concordância de implementação, não qualidade artística ou fidelidade ao jogo. Evidências ficam em `artifacts/temporal-validation/game-relighting-*.log`.

`tools/embed_game_relighting.py` gera o header a partir do HLSL compartilhado e dos pesos do experimento. A DLL não carrega shaders/pesos de arquivos externos em execução. `tools/analyze_camera_bridge.py` distingue amostras de inferência ativa, razões de bypass, contador de gravações e tempo esparso do passe; não infere qualidade visual a partir dos logs.

## Instalação concluída

Pacote `artifacts/game-test/witcher3-graphical-relighting`, instalado em 07/09/2026 às 10:05. DLL SHA256 `9D490DD1B33A2616576D5807C652389B9EB7400DBC5090626EC80C71DF60CC78`; INI SHA256 `7B6E13A63F919580C55D80DF587253FE2B9DEDB353EEB3BFEFA26E106078FE21`. Os bytes do INI anterior foram preservados, com apenas a seção TSR acrescentada. Backup verificado: `C:\Program Files (x86)\Steam\steamapps\common\The Witcher 3\bin\x64_dx12\TSR-backup-20260907-100538-618`.

Compilação Release concluída (avisos preexistentes de herança/CRT e cópias de SDK). Passaram os testes do runtime em RX 7600 e WARP, incluindo retenção de seis envios; os cinco testes de câmera/histórico/observer/destruição/textura; e a regressão do shader anterior em 321×181. Esta etapa não repetiu a suíte completa de 40 testes registrados. A DLL instalada ainda aguarda a primeira confirmação visual e log do usuário no jogo.
