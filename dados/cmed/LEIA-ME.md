# Base de medicamentos por código de barras (CMED/Anvisa)

Esta pasta guarda a lista de preços de medicamentos da CMED convertida para a consulta
do Zelo+ pelo código de barras (EAN-13). O dispenser baixa daqui apenas a parte do
código lido (ver o Passo 20 do guia do produto).

## Atualização mensal

1. Baixe a planilha "Preço Fábrica" (XLS) mais recente em
   <https://www.gov.br/anvisa/pt-br/assuntos/medicamentos/cmed/precos>.
2. Na raiz do projeto, execute:

   ```bash
   python3 -m pip install pandas openpyxl xlrd
   python3 tools/cmed_para_json.py caminho/da/planilha.xls
   ```

3. Publique a pasta `dados/cmed/` no repositório (commit e push na branch `main`).

## Arquivos

| Arquivo | Conteúdo |
|---|---|
| `000.json` a `999.json` | Partes da base, agrupadas pelos dígitos 10 a 12 do código EAN. |
| `info.json` | Arquivo de origem, data da conversão e totais. |

Cada código aponta para: nome sugerido, produto, apresentação, substância, laboratório,
tarja, registro, tipo de produto e unidades na caixa.
