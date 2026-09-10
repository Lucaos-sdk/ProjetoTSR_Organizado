# Caminho para o primeiro teste em jogo

**Atualização 2026-09-06:** o [diagnóstico para The Witcher 3](WITCHER3_PROBE.md)
foi compilado e instalado na pasta DX12 com backup verificado de `dxgi.dll` e
`OptiScaler.ini`. Ele renderiza por FSR 2.1.2 e coleta metadados para a integração.
O primeiro teste pelo usuário está pendente. A reconstrução temporal própria e
a etapa neural descritas no plano abaixo ainda não estão ativadas em jogo.

O primeiro teste será uma reconstrução temporal experimental pelo OptiScaler na RX 7600, em saída 1920×1080. Não será uma demonstração de transformação neural equivalente ao DLSS 5. Ainda não há DLL do nosso backend pronta para instalar.

## Avanço desta etapa

`native/baseline/temporal_pass.h` agora contém `TemporalPass::Record`. O construtor prepara shaders/PSO; Record apenas grava o passe temporal na command list fornecida pelo chamador, com quatro SRVs, dois UAVs e parâmetros explícitos. Não cria recursos, não submete filas, não espera fences, não copia imagens e não executa a referência CPU. O harness existente chama esse mesmo passe e continua responsável por uploads, readbacks, validação e sincronização.

O chamador precisa garantir formatos RGBA32F e layout de geometria, dimensões, estados, descritores e vida útil até a GPU terminar. O passe altera bindings de compute e descriptor heap; não restaura o estado anterior. O histórico e seu avanço continuam sob controle do chamador. O modo temporal de baixa resolução ainda precisa do passe espacial separado; o modo de histórico na saída produz diretamente a imagem ampliada.

No fork local, `IFeature_Dx12::EvaluateInternal` recebe a command list do jogo. `FeatureProvider_Dx12` instancia um backend por contexto; o backend FSR2 existente mostra a obtenção de cor, movimento, saída e jitter. Este é o ponto de integração a implementar, preservando o fluxo normal e sem reativar o bypass antigo.

## Marcos restantes para uma DLL de teste

1. **Adaptador de recursos do jogo:** aceitar os formatos reais de cor/profundidade/movimento, converter as convenções e empacotar geometria na GPU. O protótipo exige profundidade Z linear positiva e profundidade anterior prevista; dados NGX genéricos não podem ser presumidos equivalentes. Definir rejeição conservadora quando os dados necessários não existirem. Gravar também a conversão para a saída real do jogo.
2. **Ciclo de vida do backend:** recursos e descritores por contexto/quadro em voo, estados corretos, reset, redimensionamento e liberação somente após uso GPU. A espera síncrona do harness não deve ser copiada para a execução normal do jogo.
3. **Build e recuperação:** compilar o fork com o novo backend selecionável, manter o backend normal disponível e só indicar sucesso quando houver saída gravada válida. Testar falhas antes de modificar os recursos do jogo.
4. **Primeiro jogo:** confirmar título/versão/modo DX12, executar uma cena repetível em 1080p, conferir estabilidade, imagem, memória e frametimes e registrar como restaurar a configuração anterior. The Witcher 3 é o candidato anterior do plano; aguarda confirmação do usuário.

Não há prazo de calendário defensável enquanto os formatos reais e o build do backend não estiverem validados. A próxima previsão deve ser feita após o primeiro adaptador GPU e build do fork, quando os bloqueios forem conhecidos. O protótipo externo recebido não fornece fontes que removam esses marcos.

## Etapa neural posterior

Não há dataset, checkpoint treinado ou inferência neural integrada. Iluminação/materiais precisam de um experimento offline verificável, estabilidade temporal e orçamento medido na RX 7600. Essa pesquisa não tem prazo nem equivalência visual garantidos. Ter a primeira DLL temporal testável não significa que essa etapa esteja concluída.

## Validação da extração

Build Release concluído; 20/20 testes CTest passaram. As duas suítes completas (temporal + bilinear e histórico na saída) passaram na RX 7600 com debug layer, incluindo saída 4K e comparação CPU. Logs: `artifacts/temporal-validation/ctest-record-pass.log`, `record-pass-output-debug.log` e `record-pass-bilinear-debug.log`. Isso valida o passe usado pelo harness; o backend do OptiScaler ainda não foi implementado nem compilado nesta etapa.

## Referência de backend neural

A [inspeção do instalador externo](DLSS_NR_BINARY_REVIEW.md) encontrou imports de memória externa HIP e um alvo gfx1102. HIP passa a ser uma opção a avaliar para inferência AMD; não altera a prioridade da integração temporal DX12 nem substitui a validação de um modelo.

## Progresso do adaptador de entrada

A [primeira conversão GPU](INPUT_ADAPTER.md) foi implementada e validada em R32F/RG32F/RGBA32F, com profundidade normal/invertida e movimento explícito. É uma etapa do marco 1; formatos reais do jogo e profundidade anterior prevista ainda faltam. O [encadeamento de conversão e reconstrução](GPU_INPUT_CHAIN.md) passou em seis quadros na GPU, inclusive com saída 1080p.

## Progresso dos formatos e da saída

A cadeia agora possui [entrada FP16 e passe de saída](FP16_OUTPUT.md) para textura fornecida pelo chamador. Ainda falta conectar os recursos reais do jogo, regiões e ciclo de vida ao backend OptiScaler.

## Progresso das regiões

A [leitura e escrita em regiões deslocadas](TEXTURE_REGIONS.md) foi implementada na cadeia, com preservação do exterior em FP16/FP32. O ciclo de vida por contexto e quadros em voo permanece como próximo marco de runtime. Entradas com origens diferentes entre si ainda não são aceitas.

## Progresso de quadros em voo

O [controle de conjuntos por fence](FRAME_SLOTS.md) passou com três conjuntos persistentes e nove quadros na RX 7600. A política considera a conclusão dos consumidores do histórico. Falta reunir a cadeia completa nesse contexto e conectar a submissão/fence do chamador OptiScaler.

## Contexto unificado

O [GpuFrameContext](GPU_FRAME_CONTEXT.md) agora reúne os três passes com recursos persistentes e controle por fence. A cadeia completa foi validada com texturas externas na RX 7600. O próximo marco é o adaptador IFeature_Dx12 e o vínculo com a submissão/conclusão do trabalho no fork, não a ativação da engine antiga.
