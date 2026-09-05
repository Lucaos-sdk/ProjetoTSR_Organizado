# TSR neural experimental — estado real do projeto

Protótipo de pesquisa para reconstrução temporal DX12 na RX 7600. **Ainda não é um upscaler utilizável no jogo.** A integração TSR incompleta foi retirada do fluxo de avaliação do OptiScaler, que volta a seguir seu caminho normal. Isso não certifica o build completo do fork.

A auditoria e o plano técnico estão em [docs/AUDITORIA.md](docs/AUDITORIA.md).

## O que funciona nesta revisão

- Exportação estática FP16 autocontida, com validação de nomes, shapes, tipos e origem dos pesos.
- Exportação exige `state_dict` compatível; pesos aleatórios somente com `--allow-untrained`.
- Benchmark exige DirectML, rejeita fallback CPU e mede latência observada pela CPU com sincronização das saídas. A compatibilidade do binding Python depende do pacote ORT; se indisponível, aborta.
- Testes de transformação HDR e exportação ONNX. Nenhum deles comprova qualidade temporal.

## Testar a base Python

```bash
python -m pip install -r _IA_Python/requirements.txt
python -m unittest discover -s tests -v
python _IA_Python/export_onnx.py --allow-untrained
```

O último comando cria apenas um modelo de teste. Não o copie para o jogo. Para exportar pesos treinados:

```bash
python _IA_Python/export_onnx.py --checkpoint caminho/treinado.pt
```

Um checkpoint carregado com sucesso não equivale a qualidade validada. Nesta revisão não há dataset, treinamento ou checkpoint de qualidade fornecidos.

No Windows, instale `onnxruntime-directml` em ambiente dedicado e execute:

```bat
python _IA_Python/benchmark_io_binding.py --allow-untrained
```

O benchmark não inclui os shaders nem a integração ao jogo. Não é uma medição por timestamps D3D12.

## Pastas

| Pasta | Estado |
|---|---|
| `_IA_Python` | Modelo e ferramentas experimentais; os ONNX antigos têm pesos externos ausentes |
| `_Shaders` | Esboços com correções pontuais; ainda incompatíveis com o layout NCHW do runtime |
| `_OptiScaler_Source` | Fork com bypass TSR removido; engine antiga preservada para reimplementação, excluída do build normal |
| `_Build_Final_Jogo` | Conteúdo legado incompleto; não é uma distribuição validada |

Não há DLL nova validada nesta revisão. O antigo `build.bat` aponta para um arquivo movido e não constitui um build reproduzível. Consulte a auditoria antes de tentar distribuir o projeto.

## Baseline nativo e atualização local

O novo [baseline espacial DX12](native/baseline/README.md) compila separadamente do fork e compara saída bilinear GPU/CPU, com modo WARP e execução em hardware. Ele ainda não implementa TSR temporal.

Para atualizar um clone Git limpo no Windows, execute `powershell -File .\tools\Update-Local.ps1`. O script verifica o origin, recusa alterações locais e atualiza somente por fast-forward. Não executa reset, clean ou stash. Se sua pasta veio de ZIP, faça um clone novo em outra pasta, preservando os arquivos anteriores:

```powershell
git clone --branch fix/tsr-validation-and-safe-fallback https://github.com/Lucaos-sdk/ProjetoTSR_Organizado.git ProjetoTSR_Atualizado
```
