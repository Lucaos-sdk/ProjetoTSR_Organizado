# Primeiro teste no The Witcher 3 — diagnóstico

**Resultado atualizado:** o teste pela entrada XeSS ativou o diagnóstico,
com 30 amostras e nenhum erro registrado. Veja os dados reais e as limitações
em [primeira captura do jogo](WITCHER3_FIRST_CAPTURE.md). As notas de tentativas
anteriores abaixo permanecem como histórico.

**Este pacote usa FSR 2.1.2 para renderizar.** Ainda não ativa o TSR temporal
próprio nem transformação neural de iluminação/materiais. O objetivo é verificar
o carregamento do fork e obter os formatos/metadados reais usados pelo jogo.

A opção do menu chama-se **TSR diagnostics (FSR 2.1.2)**. O código de configuração
é `tsr_probe`. Essa opção funciona pela interface DX12 do OptiScaler; não contém
uma exceção de renderização específica para The Witcher. Apenas o instalador
local aponta para sua pasta Steam.

Instalação local concluída e hashes conferidos em 2026-09-06. Backup:
`bin/x64_dx12/TSR-backup-20260906-084637-222`. O jogo ainda não foi executado
para validar este pacote. Evidências: `optiscaler-build-probe-final.log`,
`probe-install-tests.log` e `witcher-probe-install.log` em
`artifacts/temporal-validation`; manifesto instalado em
`artifacts/game-test/witcher3-tsr-probe/installation.json`.

## Como testar

1. Abra The Witcher 3 pela Steam em **DirectX 12**.
2. Use saída **1920×1080** e **XeSS / Qualidade** como entrada do OptiScaler.
   Esse perfil já foi aplicado em `Documents/The Witcher 3/dx12user.settings`.
3. Pressione **Insert** para abrir o menu do OptiScaler e confira
   **TSR diagnostics (FSR 2.1.2)**.
   Se o menu ainda pedir para selecionar um upscaler, o diagnóstico não está
   ativo: informe isso antes de repetir o teste de um minuto.
4. Carregue um save e jogue por cerca de um minuto, movimentando a câmera.
5. Feche o jogo e informe se abriu, se a imagem ficou correta e se houve travamento.

O log fica em `bin/x64_dx12/TSR_Probe.log`. As linhas `TSR_PROBE` mostram o
renderizador efetivo, formatos e dimensões de texturas, presença de parâmetros,
escala de movimento, jitter, pré-exposição e resultados de Evaluate. Estados
registrados como configurados não são medições do estado real da GPU. Não há
captura de imagens nem envio automático de dados. Há no máximo 64 snapshots
por contexto, distribuídos pela execução para incluir a cena após carregamento.

Após a primeira tentativa, o log mostrou erros repetidos do Streamline e ainda
nenhuma amostra TSR_PROBE. Em 2026-09-06, a configuração do jogo foi alterada de
XeSS para FSR 2 Qualidade, desligando Streamline, DLSS/DLSSG e Reflex para isolar
o teste. Isso ainda precisa de uma nova execução para confirmar o resultado.
Backup da configuração e registro das oito alterações em
`artifacts/game-test/witcher3-settings-20260906-085957-120`.
O log anterior foi preservado como
`artifacts/temporal-validation/witcher-before-fsr-settings.log`.

## Alterações e recuperação

Correção após o teste FSR: a captura do usuário mostrou `FSR Hooks: Don't Exist`
e nenhum contexto de upscaling. O log preservado em
`artifacts/temporal-validation/witcher-fsr-no-context.log` tem zero amostras
`TSR_PROBE v1` e zero exceções Streamline. A entrada foi trocada para XeSS
Qualidade (`AAMode=7`, `XESSQuality=2`), mantendo Streamline desligado. Novo
backup: `artifacts/game-test/witcher3-settings-20260906-134940-788`.
O funcionamento desta entrada ainda precisa ser confirmado no jogo.

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
