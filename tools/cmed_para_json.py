#!/usr/bin/env python3
"""Converte a lista de precos de medicamentos da CMED (Anvisa) na base de consulta
do Zelo+ por codigo de barras (EAN-13).

Uso:
    python3 tools/cmed_para_json.py <lista_cmed.xls|.xlsx|.csv> [pasta_saida]

A lista oficial e publicada todo mes pela CMED no portal da Anvisa:
https://www.gov.br/anvisa/pt-br/assuntos/medicamentos/cmed/precos
(arquivo "Lista de precos de medicamentos - Preco Fabrica", em XLS).

Saida (padrao: dados/cmed/):
    000.json ... 999.json   uma parte da base para cada grupo de codigos
    info.json               data da conversao, arquivo de origem e totais

Cada parte e um objeto JSON: chave = EAN-13; valor = lista com
    [0] nome sugerido para o cadastro (ex.: "Losartana Potássica 50 mg")
    [1] produto (como na CMED)
    [2] apresentacao (como na CMED)
    [3] substancia (principio ativo)
    [4] laboratorio
    [5] tarja
    [6] registro na Anvisa
    [7] tipo de produto (generico, similar, novo...)
    [8] quantidade de unidades na embalagem (0 se nao identificada)

A parte de um codigo e escolhida pelos digitos 10 a 12 do EAN (posicoes 9 a 11,
contando do zero), que variam de forma uniforme entre os produtos: cada parte fica
pequena (alguns KB) e o ESP32 baixa so a parte do codigo lido.
"""
import json
import os
import re
import sys
import unicodedata
from datetime import datetime, timezone

import pandas as pd

COLUNAS = {
    'substancia': ['SUBSTANCIA'],
    'laboratorio': ['LABORATORIO'],
    'registro': ['REGISTRO'],
    'ean1': ['EAN 1', 'EAN1'],
    'ean2': ['EAN 2', 'EAN2'],
    'ean3': ['EAN 3', 'EAN3'],
    'produto': ['PRODUTO'],
    'apresentacao': ['APRESENTACAO'],
    'tipo': ['TIPO DE PRODUTO'],
    'tarja': ['TARJA'],
}

PALAVRAS_MINUSCULAS = {'de', 'da', 'do', 'das', 'dos', 'e', 'com', 'sem', 'em', 'para'}
# Siglas que ficam em maiusculas no nome (liberacao prolongada, associacoes etc.)
SIGLAS = {'XR', 'SR', 'CR', 'LP', 'AP', 'MR', 'ER', 'DR', 'ODT', 'HCT', 'XL', 'LA', 'CD', 'D3', 'B12', 'B6', 'B1', 'UI', 'DUO', 'XRO'}
UNIDADES = r'(?:MG|MCG|µG|UG|G|UI|U|MEQ|MMOL|%)(?:\s*/\s*(?:ML|G|DOSE|GOTA|H|24H|L))?'
RE_CONCENTRACAO = re.compile(r'(\d+(?:[.,]\d+)?\s*' + UNIDADES + r'(?:\s*\+\s*\d+(?:[.,]\d+)?\s*' + UNIDADES + r')*)', re.I)
RE_QUANTIDADE = re.compile(r'\bX\s*(\d+)\b(?!\s*(?:ML|L|G|MG|KG)\b)', re.I)
FORMAS_CONTAVEIS = re.compile(r'\b(COM|CPR|CAP|CAPS|DRG|COMP|PAST|SACHE|ENV|AMP|SUP|OVL|ADES)\b', re.I)


def normalizar(texto):
    texto = unicodedata.normalize('NFKD', str(texto)).encode('ascii', 'ignore').decode()
    return re.sub(r'\s+', ' ', texto).strip().upper()


def ler_planilha(caminho):
    """Le a lista da CMED. As primeiras linhas do arquivo oficial sao titulos:
    o cabecalho e a primeira linha que contem SUBSTANCIA, PRODUTO e EAN 1."""
    ext = os.path.splitext(caminho)[1].lower()
    if ext == '.csv':
        bruto = pd.read_csv(caminho, sep=None, engine='python', header=None, dtype=str, encoding_errors='replace')
    else:
        bruto = pd.read_excel(caminho, header=None, dtype=str)
    for i in range(min(len(bruto), 100)):
        celulas = [normalizar(v) for v in bruto.iloc[i].tolist() if isinstance(v, str)]
        if 'SUBSTANCIA' in celulas and 'PRODUTO' in celulas and any(c.startswith('EAN 1') or c == 'EAN1' for c in celulas):
            tabela = bruto.iloc[i + 1:].copy()
            tabela.columns = [normalizar(v) if isinstance(v, str) else f'_{k}' for k, v in enumerate(bruto.iloc[i].tolist())]
            return tabela
    raise SystemExit('Cabeçalho da lista da CMED não encontrado (colunas SUBSTÂNCIA, PRODUTO e EAN 1).')


def coluna(tabela, chave):
    for nome in COLUNAS[chave]:
        for c in tabela.columns:
            if c == nome or c.startswith(nome + ' ') or c.startswith(nome + '('):
                return c
    return None


def ean_valido(codigo):
    if not re.fullmatch(r'\d{13}', codigo):
        return False
    soma = sum(int(d) * (3 if i % 2 else 1) for i, d in enumerate(codigo[:12]))
    return (10 - soma % 10) % 10 == int(codigo[12])


def limpar(valor):
    if valor is None or (isinstance(valor, float) and pd.isna(valor)):
        return ''
    texto = re.sub(r'\s+', ' ', str(valor)).strip()
    return '' if texto.upper() in ('NAN', '-', '- (*)') else texto


def capitalizar(texto):
    palavras = texto.lower().split(' ')
    saida = []
    for i, p in enumerate(palavras):
        if p.upper() in SIGLAS or (len(p) <= 3 and not re.search('[aeiouáéíóúâêôãõ]', p)):
            saida.append(p.upper())
        elif i > 0 and p in PALAVRAS_MINUSCULAS:
            saida.append(p)
        else:
            saida.append(p[:1].upper() + p[1:])
    return ' '.join(saida)


def concentracao(apresentacao):
    m = RE_CONCENTRACAO.search(apresentacao)
    if not m:
        return ''
    texto = re.sub(r'\s+', ' ', m.group(1)).strip()
    texto = re.sub(r'(\d)\s*(?=[A-Za-zµ%])', r'\1 ', texto)
    return re.sub(r'[A-Za-zµ/]+', lambda u: u.group(0).lower().replace('ui', 'UI').replace('meq', 'mEq'), texto)


def quantidade(apresentacao):
    if not FORMAS_CONTAVEIS.search(apresentacao):
        return 0
    numeros = RE_QUANTIDADE.findall(apresentacao)
    return int(numeros[-1]) if numeros else 0


def nome_sugerido(produto, apresentacao):
    nome = capitalizar(produto)
    conc = concentracao(apresentacao)
    if conc and conc.lower() not in nome.lower():
        # o campo do nome na pagina aceita 40 caracteres: encurta o produto, nunca a dose
        limite = 39 - len(conc)
        if len(nome) > limite:
            nome = nome[:limite].rsplit(' ', 1)[0] if ' ' in nome[:limite] else nome[:limite]
        nome = f'{nome.rstrip()} {conc}'
    return nome[:40]


def converter(caminho, saida):
    tabela = ler_planilha(caminho)
    cols = {k: coluna(tabela, k) for k in COLUNAS}
    faltando = [k for k in ('produto', 'apresentacao', 'ean1') if cols[k] is None]
    if faltando:
        raise SystemExit(f'Colunas obrigatórias não encontradas: {faltando}')

    partes = {}
    total_linhas = total_eans = 0
    for _, linha in tabela.iterrows():
        produto = limpar(linha[cols['produto']])
        apresentacao = limpar(linha[cols['apresentacao']])
        if not produto:
            continue
        total_linhas += 1
        valor = [
            nome_sugerido(produto, apresentacao),
            produto,
            apresentacao,
            limpar(linha[cols['substancia']]) if cols['substancia'] else '',
            limpar(linha[cols['laboratorio']]) if cols['laboratorio'] else '',
            limpar(linha[cols['tarja']]) if cols['tarja'] else '',
            limpar(linha[cols['registro']]) if cols['registro'] else '',
            limpar(linha[cols['tipo']]) if cols['tipo'] else '',
            quantidade(apresentacao),
        ]
        for k in ('ean1', 'ean2', 'ean3'):
            if not cols[k]:
                continue
            codigo = re.sub(r'\D', '', limpar(linha[cols[k]]).split('.')[0])
            if not ean_valido(codigo):
                continue
            partes.setdefault(codigo[9:12], {})[codigo] = valor
            total_eans += 1

    os.makedirs(saida, exist_ok=True)
    for arquivo in os.listdir(saida):
        if re.fullmatch(r'\d{3}\.json', arquivo):
            os.remove(os.path.join(saida, arquivo))
    maior = 0
    for grupo, itens in partes.items():
        texto = json.dumps(dict(sorted(itens.items())), ensure_ascii=False, separators=(',', ':'))
        with open(os.path.join(saida, f'{grupo}.json'), 'w', encoding='utf-8') as f:
            f.write(texto)
        maior = max(maior, len(texto.encode('utf-8')))
    info = {
        'fonte': 'CMED/Anvisa — Lista de preços de medicamentos',
        'arquivo_origem': os.path.basename(caminho),
        'convertido_em': datetime.now(timezone.utc).strftime('%Y-%m-%d'),
        'apresentacoes': total_linhas,
        'codigos_ean': total_eans,
        'partes': len(partes),
        'maior_parte_bytes': maior,
    }
    with open(os.path.join(saida, 'info.json'), 'w', encoding='utf-8') as f:
        json.dump(info, f, ensure_ascii=False, indent=2)
    return info


if __name__ == '__main__':
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    destino = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.path.dirname(__file__), '..', 'dados', 'cmed')
    print(json.dumps(converter(sys.argv[1], destino), ensure_ascii=False, indent=2))
