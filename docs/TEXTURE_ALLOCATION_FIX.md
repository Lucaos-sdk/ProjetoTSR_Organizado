# Correção de alocação de texturas — 7 de setembro de 2026

A sessão anterior de The Witcher 3 registrou 3.282 erros `E_INVALIDARG` em `Shader_Dx12::CreateBufferResource`. O log antigo não contém descritores suficientes para atribuir todos esses erros a uma causa específica.

Foi encontrada uma combinação inválida no helper: converter uma textura de profundidade para R32_FLOAT preservava ALLOW_DEPTH_STENCIL e DENY_SHADER_RESOURCE, mesmo quando o destino precisava de UAV/RT. A conversão explícita agora remove essas flags incompatíveis. As demais conversões continuam com sua política existente. O cache considera o descritor completo, uma alocação malsucedida preserva o recurso anterior e o novo log registra formato, dimensões, flags, estado, amostras e mips.

O teste `tsr_shader_texture_desc_dx12` cria profundidade nos formatos 19 e 44, reproduz E_INVALIDARG com a política antiga e verifica alocação e descritor da saída corrigida. Passou na RX 7600 e no WARP. O teste deriva o descritor de `GetDesc`, assim como o helper real, pois o driver resolve o alinhamento padrão para um valor concreto. A suíte completa passou: 37/37. A responsabilidade por aguardar usos anteriores na GPU antes de redimensionar continua sendo do chamador.

Pacote instalado: `artifacts/game-test/witcher3-texture-fix`. A configuração foi copiada byte a byte da instalação atual e verificada antes/depois: SHA256 `7194A507252947460AE54BBFBFE1DEC50174F1E191B0B77531F958E2F2F5C890`. O arquivo de configurações do jogo não foi alterado.

DLL SHA256: `BB6F082CACC0CAAEB6550D266F19D473D8323D272335C3DA294667BC321CFEA5`.

Backup: `C:\Program Files (x86)\Steam\steamapps\common\The Witcher 3\bin\x64_dx12\TSR-backup-20260907-010018-233`. A restauração é suportada por `tools/restore_witcher_probe.ps1 -Backup <pasta>`, com proteção contra alterações posteriores nos arquivos.

A correção não implica redução comprovada de tempo de quadro nem alteração neural da imagem. Os 6 ms informados pelo usuário são uma observação do overlay, sem medição independente de latência de entrada.

## Sessão posterior à correção

Em 07/09/2026, o log cobre 01:03:10–01:21:08, aproximadamente 18 minutos entre carregamento e encerramento da DLL, incluindo menus/transições. O jogo já estava fechado na análise e o SHA256 da DLL instalada coincide com o pacote corrigido.

- **Zero linhas de erro e zero erros de alocação de textura**, contra 3.282 erros de alocação na sessão anterior. Isto confirma ausência de recorrência nesta sessão, não prova que todos os erros anteriores tinham a mesma causa.
- As 33 amostras de dispatch FSR registram provider 4.1.1, resultado OK, entrada 1476×830 e saída 1920×1080. Nenhuma mudança da resolução interna foi registrada; a transição de qualidade da sessão anterior não foi reproduzida no log.
- Das 63 amostras de status do present anterior, 30 mostram geração habilitada, dois quadros enviados e interpolação com sucesso; 32 mostram geração desabilitada e um quadro enviado; uma amostra inicial habilitada apresenta resultado 3, de histórico insuficiente. Todas as consultas retornam sucesso.
- Há desligamentos e reativações explícitos do XeFG com sucesso. Sem contexto adicional, não atribuir todos a menus nem afirmar geração contínua. A amostragem de status termina em sample=36000 às 01:10:54; as ativações posteriores são registradas, mas não há amostras de present até o fim da sessão.
- 105 avisos, incluindo 63 de salto de contador na geração e 14 de entrada no menu; os demais incluem componentes opcionais e encerramento de janela. O log termina com DLL_PROCESS_DETACH e descarregamento normal, sem erro registrado.

Evidências preservadas: `artifacts/temporal-validation/witcher-texture-fix-game.log` e `.json`. SHA256 do log: `E02D61C22FB89815DC40F48E6B49B744939BC01DF89131B681E80FA151B13418`. O status do SDK não mede apresentação física, espaçamento de quadros, custo GPU ou latência de entrada. Nesta análise não foram alterados arquivos do jogo.
