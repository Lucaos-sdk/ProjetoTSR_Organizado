# Integração inicial com a submissão do OptiScaler

O [adaptador de parâmetros NGX](NGX_FRAME_ADAPTER.md) está compilado no fork e
testado na cadeia GPU. Sua preparação ainda não está ligada à seleção do backend.

Estado mais recente: a [reexecução temporal em uma fila](TEMPORAL_REPLAY.md)
agora foi testada com reservas por gravação e transições internas repetíveis.
Isso substitui a limitação histórica de detecção apenas, descrita nas etapas
abaixo. Não equivale a suporte a múltiplas filas ou validação em jogo.

## O que está conectado

Os dois caminhos de `ResTrack_Dx12::hkExecuteCommandLists` notificam o registro
TSR **depois** da chamada original. A notificação entrega a fila real e a lista
enviada. Sem registros, há apenas uma consulta atômica, sem bloquear mutex.

O registro mantém o destinatário vivo até Reset ou destruição, recusa registro
duplicado, entrega a primeira submissão uma vez e identifica reenvios por um
callback separado. Permite cancelar trabalho não enviado pelo ticket. Tickets antigos ou de outro
registro não cancelam uma gravação nova. Os callbacks acontecem fora do mutex
e não podem lançar exceções através do hook.

`hkReset` agora acompanha `ID3D12GraphicsCommandList::Reset` (vtable 10) no
mesmo conjunto de hooks de listas. Após sucesso, remove a gravação pendente e
chama `Discarded`, que permite cancelar o lease. Falha de Reset mantém o
registro. Reset depois da submissão não cancela trabalho já enviado. Instalação,
falha de instalação e os dois caminhos de remoção incluem esse hook.

O teste do contexto GPU usa exatamente esse ponto de entrada: registra a lista,
confirma que o histórico ainda não avançou, envia os comandos, recebe a
notificação, sinaliza a fence na fila recebida e só então confirma o frame.
Mantém o bloqueio determinístico de três submissões para detectar reutilização
prematura, além da comparação da imagem final com a referência CPU.

## Limites que impedem ativação em jogo

- O backend TSR ainda não foi registrado em `FeatureProvider_Dx12`/menu.
- O hook existente é instalado pelo rastreamento de recursos; a futura
  inicialização TSR deve garantir que ele está instalado mesmo sem FG.
- Reset está conectado. A destruição tem um adaptador testado com objetos reais,
  que ainda precisa ser instalado na inicialização do futuro backend. Reenvio
  é detectado, mas executar novamente os comandos temporais ainda não é suportado.
  Cancelamento explícito exige que o chamador já tenha
  descartado os comandos e impeça envio concorrente. Não habilitar TSR antes
  de completar esse ciclo de vida e normalizar identidades de listas encapsuladas.
- Uma notificação não significa GPU concluída. É obrigatório manter contexto,
  recursos e descritores até a fence completar, inclusive em troca de backend.
- Falha de Signal, device removed e troca de fila exigem tratamento explícito.
  O teste usa uma única fila; não comprova suporte a múltiplas filas de jogo.
- A conversão dos parâmetros reais de profundidade, movimento, exposição e
  estados das texturas ainda precisa ser conectada e validada. Não assumir
  câmera fixa em um jogo apenas porque o teste sintético a utiliza.

## Compilação do fork

A compilação Release x64 estava bloqueada por cabeçalhos incompatíveis. Foram
adicionados somente os cabeçalhos oficiais da API FidelityFX SDK 1.1.4 em pasta
separada, com licença e commit registrados em `PROVENANCE.md`. As declarações
Vulkan beta usadas pelo wrapper existente foram habilitadas no projeto; isso
não habilita extensões em uma GPU.

A DLL compila. O evento de pós-compilação antigo ainda relata arquivos ausentes
ao copiar dependências FidelityFX, e o linker apresenta aviso de bibliotecas C
de runtime conflitantes. Portanto, a saída não é um pacote validado para jogo.
Nenhum instalador externo foi executado e nenhum jogo foi alterado.

## Verificação

- Suíte completa: 31/31 testes aprovados. Após acrescentar a proteção contra
  tickets de outro registro, os três testes afetados foram repetidos e passaram.
- RX 7600: nove frames 1280×720 → 1920×1080 em FP16 e FP32, com camada de
  depuração, comparação CPU e bloqueio de três submissões; ambos aprovados.
- Teste CPU do registro: roteamento, duplicação, tickets antigos/de outro
  registro, cancelamento, entrega única e retenção do destinatário.
- Contexto DX12 com registro: nove frames, três slots, seis reutilizações,
  saída FP32/FP16, comparação CPU e bloqueio da fila.
- Logs em `artifacts/temporal-validation/`: `ctest-submission.log`,
  `gpu-submission-fp16-rx7600.log`, `gpu-submission-fp32-rx7600.log` e
  `optiscaler-build-observer.log`.

### Validação da etapa Reset

Os três testes afetados passaram (`ctest-reset.log`): registro CPU, contexto WARP
FP32 e contexto WARP FP16. O teste CPU cobre Reset com falha, sucesso, repetição,
novo registro e Reset após submissão. O teste GPU reinicia listas reais e chama
o mesmo ponto de notificação usado pelo hook; verifica descarte no primeiro e
no quinto quadro, preservando histórico e restaurando as transições descartadas.

Na RX 7600, ambos os formatos passaram em 1280×720 → 1920×1080 com depuração,
nove quadros, três slots e comparação CPU (`gpu-reset-fp16-rx7600.log` e
`gpu-reset-fp32-rx7600.log`). O fork recompilou (`optiscaler-build-reset.log`).
O teste independente não carrega a DLL nem valida o Detours dentro de um jogo.

### Destruição e identidade da gravação — 2026-09-06

`TrackListLifetime` instala uma interface COM privada na lista, antes de registrar
trabalho. Ao destruir a lista, a interface notifica o registro sem acessar o
objeto destruído. Ela retém o registro por `shared_ptr`, sem reter a lista e
criar um ciclo. Uma segunda instalação é recusada, preservando a primeira.
O chamador deve serializar instalação/uso e reservar a chave GUID: substituir
esse dado privado também libera a interface, portanto não pode ser feito
enquanto a lista ainda puder executar. O módulo deve permanecer carregado até
a liberação dessas interfaces.

Esse comportamento usa o contrato documentado de
[SetPrivateDataInterface](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12object-setprivatedatainterface).
Não altera a tabela de Release de todos os objetos do jogo.

O registro agora mantém entradas já submetidas até Reset/destruição. Nova
gravação na mesma lista é recusada enquanto a anterior existir. Reenvio chama
`Replayed`, sem executar novamente `Commit` nem avançar o índice do quadro.
Destruição de trabalho não enviado cancela o lease; destruição de uma lista já
enviada não cancela o trabalho da GPU. Recursos em voo ainda precisam de um
proprietário até a fence completar, independente da vida da lista.

**Não confundir detecção com suporte a replay:** o hook avisa após Execute e não
pode desfazer os comandos. O teste de replay envia listas vazias reais. O teste
temporal trata replay como erro; os estados das texturas, fixação dos slots até
fim da gravação, extensão das fences em reenvios e filas distintas ainda devem
ser resolvidos antes da ativação em jogo. A retenção do callback sozinha não
impede que `FrameSlots` reutilize um buffer.

Validação: 32/32 testes passaram (`ctest-lifetime.log`). Após acrescentar a
destruição real de uma lista com trabalho temporal gravado, os quatro testes
afetados passaram novamente (`ctest-lifetime-final.log`). A DLL recompilou
(`optiscaler-build-lifetime.log`), mantendo as limitações de empacotamento acima.
Testes RX 7600: `list-lifetime-rx7600.log`, `gpu-lifetime-fp16-rx7600.log` e
`gpu-lifetime-fp32-rx7600.log`; cadeia temporal 1280×720 → 1920×1080.

Isso é sincronização para reconstrução temporal. Não contém transformação
neural de iluminação/materiais, nem validação visual ou desempenho em jogo.
