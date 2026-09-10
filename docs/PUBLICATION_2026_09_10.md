# Publicação consolidada — 10/09/2026

Publicação solicitada pelo proprietário para continuar com outra IA e com o
colaborador atual. O README anterior estava obsoleto; foi substituído por
estado consolidado, histórico, conceitos, limites, reprodução e continuidade.
O índice documental e `AI_HANDOFF.md` são os pontos de entrada compartilháveis.

## Conteúdo

Código Python/native/fork acumulado desde o último commit, testes, relatórios,
checkpoints próprios e evidências dos experimentos. Mantidos também resultados
negativos e dados de referência `.npz`/`.bin`, necessários para preservar o
histórico. Arquivos antigos já versionados não foram apagados nesta consolidação.

Excluídos via `.gitignore`: ambientes/builds locais, instaladores e pacotes
externos recebidos, caches externos, executáveis/DLLs/ZIPs sob `artifacts/` e
texto bruto de contexto pessoal. Nada disso foi apagado do computador.
Os relatórios técnicos registram as análises desses materiais.

Não houve alteração de algoritmo, treinamento ou instalação no jogo nesta
publicação. A CI Python recebeu Pillow, dependência necessária dos módulos de
pesquisa agora publicados. Licenças e proveniência de cabeçalhos externos foram
preservadas; não se declarou uma licença nova para o conjunto de terceiros.

## Verificações locais

- Suíte Python completa: **55 testes passaram**, incluindo 42 testes neurais.
- CTest no build local Release existente: **42 testes nativos passaram**, incluindo
  referências CPU e caminhos WARP. Não foi uma recompilação integral do fork.
- Nenhum arquivo candidato ultrapassou 100 MiB. Dois dados sintéticos históricos
  ultrapassam 50 MiB; isso aumenta o clone, mas preserva evidências solicitadas.
- Varredura inicial de padrões explícitos de chaves privadas/tokens e nomes de
  arquivos de credencial sem achados. Não é certificação de auditoria de segurança.
- `.gitattributes` preserva bytes de `artifacts/`, evitando invalidar hashes
  de evidências pela normalização de quebras de linha.

Os manifests de fontes são históricos: outras revisões ou normalização de
arquivos de código podem alterar hashes de fontes. Os hashes dos checkpoints
e evidências binárias continuam conferíveis. Não reescrever manifests antigos
para fazer parecer que foram gerados pela revisão atual.

A publicação não certifica a CI remota, o build completo do OptiScaler, qualidade
em jogos novos, execução da CNN em GPU ou equivalência a DLSS 5. Esses limites
continuam registrados no README e nos protocolos.
