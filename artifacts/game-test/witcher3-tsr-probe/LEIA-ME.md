# Primeiro teste no The Witcher 3 — diagnóstico

**Este pacote usa FSR 2.1.2 para renderizar.** Ainda não ativa o TSR temporal
próprio nem transformação neural de iluminação/materiais. O objetivo é verificar
o carregamento do fork e obter os formatos/metadados reais usados pelo jogo.

A opção do menu chama-se **TSR diagnostics (FSR 2.1.2)**. O código de configuração
é `tsr_probe`. Essa opção funciona pela interface DX12 do OptiScaler; não contém
uma exceção de renderização específica para The Witcher. Apenas o instalador
local aponta para sua pasta Steam.

## Como testar

1. Abra The Witcher 3 pela Steam em **DirectX 12**.
2. Use saída **1920×1080**. Se DLSS estiver disponível, selecione-o como entrada
   do OptiScaler. Isso não significa que o renderizador final será DLSS.
3. Pressione **Insert** para abrir o menu do OptiScaler e confira
   **TSR diagnostics (FSR 2.1.2)**.
4. Carregue um save e jogue por cerca de um minuto, movimentando a câmera.
5. Feche o jogo e informe se abriu, se a imagem ficou correta e se houve travamento.

O log fica em `bin/x64_dx12/TSR_Probe.log`. As linhas `TSR_PROBE` mostram o
renderizador efetivo, formatos e dimensões de texturas, presença de parâmetros,
escala de movimento, jitter, pré-exposição e resultados de Evaluate. Estados
registrados como configurados não são medições do estado real da GPU. Não há
captura de imagens nem envio automático de dados. Há no máximo 64 snapshots
por contexto, distribuídos pela execução para incluir a cena após carregamento.

## Alterações e recuperação

O instalador substitui apenas `dxgi.dll` e `OptiScaler.ini`, após criar e verificar
um backup `TSR-backup-...` na mesma pasta. Preserva as dependências já instaladas.
Seleciona o diagnóstico, desliga FG/XeFG para isolar o teste e habilita log Info.
Não modifica saves nem as configurações gráficas do jogo.

`tools/restore_witcher_probe.ps1 -Backup <pasta-do-backup>` restaura e verifica os
dois arquivos anteriores. Se os arquivos instalados forem modificados depois,
o script recusa sobrescrevê-los automaticamente. O backup permanece disponível.

Instalação e restauração foram testadas em uma pasta de ensaio, incluindo caminhos
com espaços, recusa de pacote alterado e proteção de alterações posteriores.
Compilar a DLL não comprova compatibilidade com a cena do jogo: este é o teste
que produzirá essa evidência. O suporte a outros jogos será implementado por
contratos de API/recursos; não há garantia de compatibilidade universal sem teste.
