# Entrada FP16 e textura final fornecida pelo chamador

A cadeia de teste agora aceita cor RGBA16F e movimento RG16F com `--fp16`. Profundidade permanece R32F; cores empacotadas, geometria e histórico permanecem RGBA32F. O modo original FP32 continua disponível.

`OutputAdapterPass::Record` grava a imagem reconstruída em uma textura UAV fornecida pelo chamador, com o mesmo tamanho da reconstrução. O passe apenas carrega/escreve RGBA linear: não aloca recursos, não submete filas, não faz readback e não muda a exposição. Os formatos verificados são RGBA32F e RGBA16F.

## Contrato e limites

- O chamador deve garantir estados SRV/UAV, dimensões, descritores, vida útil e suporte a shader-load/typed-store. A ferramenta de teste consulta o suporte do dispositivo antes de criar os recursos.
- A gravação FP16 exige valores finitos representáveis. Não foi adicionado clamp de HDR, tonemapping ou mudança de gama; overflow e entradas não finitas ainda precisam de uma política do backend.
- Alpha segue o quadro atual e passa pela conversão de formato.
- Não há suporte validado a UNORM, sRGB, PQ, D24S8, MSAA ou regiões deslocadas nesta etapa.
- O destino é fornecido pelo harness, não por um jogo real. Nenhum backend foi instalado ou ativado no OptiScaler.

## Verificação numérica

A referência CPU recebe os valores efetivamente representáveis nas entradas FP16, incluindo movimento quantizado; não compara uma entrada FP16 com a versão FP32 idealizada. Cor e geometria internas continuam comparadas ao algoritmo CPU independente.

A textura final é comparada ao histórico GPU já validado: igualdade no modo FP32 e tolerância de um passo FP16 no modo FP16. A fixture inclui frações não exatamente representáveis, HDR, valores negativos e alpha 0,3333. A leitura usa row pitch e bytes por pixel próprios de cada formato.

Os seis quadros mantêm as verificações de continuidade, mudança de profundidade, dados anteriores indisponíveis e reset. A cadeia completa é gravada antes dos readbacks finais. Não é medição de desempenho nem redução de memória do histórico; o histórico continua FP32.

## Executar

```powershell
.\build\native-rx7600\Release\tsr_input_chain_dx12.exe --debug --fp16 --1080p
.\build\native-rx7600\Release\tsr_input_chain_dx12.exe --debug --1080p
```

O próximo trabalho recomendado é tratar regiões de entrada/saída e encapsular o ciclo de vida do backend, preparando o build do fork OptiScaler. A obtenção de profundidade anterior prevista em câmera móvel continua uma limitação independente do formato.

## Resultado desta etapa

Build Release concluído e 23/23 testes CTest passaram. Na RX 7600, os modos FP16 e FP32 passaram com debug layer em 1280×720 → 1920×1080, nos seis quadros. Cor, geometria, máscara e textura final foram verificadas. Logs: `artifacts/temporal-validation/ctest-fp16-output.log`, `fp16-chain-rx7600-1080p.log` e `fp32-output-rx7600-1080p.log`.

Atualização: [regiões de textura](TEXTURE_REGIONS.md) adiciona origens de leitura/escrita e preservação dos pixels externos, mantendo os formatos desta etapa.
