# Teste FSR 4.1.1

Abra The Witcher 3 em DirectX 12. Mantenha XeSS Qualidade e 1920×1080 no menu do jogo; o OptiScaler faz a troca do processamento internamente.

Carregue um save e pressione Insert. O backend deve mostrar **FSR 4.1.1**. Jogue por um ou dois minutos, observando cabelo, vegetação e contornos ao girar a câmera. Depois feche o jogo para analisarmos `TSR_FSR411.log`.

A nitidez extra foi desativada. Agora o teste é da reconstrução do FSR 4.1.1 da AMD, com a integração de API corrigida em nossa DLL. Não há transformação neural própria de iluminação ou materiais.

Não mova esta pasta: a configuração carrega o binário assinado da AMD daqui. A instalação preserva a versão anterior em uma pasta `TSR-backup-*` dentro da pasta DX12 do jogo. O atalho de restauração desta pasta permite voltar após fechar o jogo; se houver edições posteriores na configuração, ele interrompe para preservá-las.

O teste isolado na RX 7600 passou. O funcionamento, qualidade e custo dentro do Witcher 3 ainda dependem deste teste.
