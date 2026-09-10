# Primeiro perfil visual — nitidez moderada

Já aplicado em 06/09/2026. Abra The Witcher 3 em DirectX 12, mantenha XeSS Qualidade e 1920×1080, e carregue um save. O backend permanece TSR diagnostics / FSR 2.1.2.

Este perfil ativa o RCAS já existente no OptiScaler, com intensidade 0,25. Pode tornar texturas e contornos mais definidos. A melhora percebida, halos, cintilação e custo devem ser avaliados no jogo; não foram medidos automaticamente. Não é inferência neural, alteração de iluminação ou implementação de FSR 4.1.1 / DLSS 5.

Para comparar:

1. Observe cabelo, folhagens e texturas na mesma posição, parado e girando a câmera.
2. Feche o jogo e execute **Restaurar visual anterior.cmd** nesta pasta.
3. Abra o mesmo save e compare na mesma posição.
4. Para voltar ao perfil, feche o jogo e execute **Ativar nitidez moderada.cmd**.

Os atalhos apenas alteram as sete opções deste perfil. Preservam outras configurações. Se uma dessas sete opções tiver sido alterada manualmente no menu, o script avisa e interrompe para não sobrescrever a escolha. O backup inicial integral está em `OptiScaler.before.ini`; cada aplicação também cria um backup e um registro.

Configuração: Sharpness.Shader=rcas, OverrideSharpness=true, Sharpness=0.25; CAS.Enabled=true, MotionSharpnessEnabled=false, ContrastEnabled=false, SharpenerDebug=false. O código existente desativa a nitidez interna do upscaler ao usar o passe externo, evitando a aplicação dos dois filtros nesse caminho.

A DLL não foi substituída. A configuração do jogo não foi alterada. Os 0,98 ms medidos anteriormente pertencem ao protótipo sintético e não estimam o custo deste perfil em jogo.
