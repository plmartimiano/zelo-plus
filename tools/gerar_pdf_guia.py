#!/usr/bin/env python3
"""Gera os PDFs dos guias e da especificacao 3D a partir dos arquivos Markdown.

Uso:
    python3 tools/gerar_pdf_guia.py                 # gera todos os documentos
    python3 tools/gerar_pdf_guia.py ste             # gera so a versao STE100
    python3 tools/gerar_pdf_guia.py especificacao   # gera so a especificacao 3D

Etapas: Markdown -> HTML (com capa e sumario numerado) -> PDF pelo Chromium sem
interface (--print-to-pdf) -> marcadores (indice lateral) com PyMuPDF. O sumario e
gerado duas vezes: na primeira, o programa descobre a pagina de cada titulo; na
segunda, escreve os numeros de pagina no sumario.

Requisitos: python3 -m pip install markdown pygments pymupdf
            Chromium ou Google Chrome (variavel CHROMIUM, se nao estiver no PATH).
"""
import glob
import html
import os
import re
import shutil
import subprocess
import sys
import tempfile

import markdown
import pymupdf

RAIZ = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
DOCS = os.path.join(RAIZ, 'docs')

CAPA_GUIA = {
    'subtitulo': 'Guia do produto',
    'imagem': 'referencia-produto.jpg',
    'legenda': 'Imagem de referência — conceito visual do produto final (ilustrativo).',
    'linhas': ['Dispenser inteligente de medicamentos com 3 compartimentos<br>\n  ESP32 · servos · LCD I2C · Telegram · histórico de doses',
               'Desenho técnico · funcionamento · montagem<br>\n  componentes · custo de produção'],
    'data': 'Setembro de 2026',
}

GUIAS = {
    'principal': {
        'md': 'GUIA-DO-PRODUTO.md',
        'pdf': 'docs/Zelo-Guia-do-Produto.pdf',
        'titulo': 'Zelo+ — Guia do produto',
        'selo': '',
        'fonte_sumario': '7.3pt',
        'capa': CAPA_GUIA,
        'marcadores': 'partes',
    },
    'ste': {
        'md': 'GUIA-DO-PRODUTO-STE.md',
        'pdf': 'docs/Zelo-Guia-do-Produto-STE.pdf',
        'titulo': 'Zelo+ — Guia do produto (STE100)',
        'selo': 'Redação controlada conforme as regras da ASD-STE100',
        'fonte_sumario': '7.0pt',
        'capa': CAPA_GUIA,
        'marcadores': 'partes',
    },
    'especificacao': {
        'md': 'ESPECIFICACAO-MODELAGEM-3D.md',
        'pdf': 'docs/Zelo-Especificacao-Modelagem-3D.pdf',
        'titulo': 'Zelo+ — Especificação para modelagem 3D',
        'selo': '',
        'fonte_sumario': '7.3pt',
        'capa': {
            'subtitulo': 'Especificação para modelagem 3D',
            'imagem': 'gabinete-3d.png',
            'legenda': 'Modelo 3D de referência do gabinete.',
            'linhas': ['Dispenser inteligente de medicamentos com 3 compartimentos',
                       'Dimensões · compartimentos e tampas · aberturas e suportes<br>\n  acionamento das tampas · componentes · impressão'],
            'data': 'Outubro de 2026',
        },
        'marcadores': 'secoes',   # cada secao (##) e um marcador de primeiro nivel
    },
}

CAPA = '''
<section class="capa">
  <div class="marca">Zelo+</div>
  <div class="subtitulo">{subtitulo}</div>
  {selo}
  <img src="{imagem}" alt="{legenda}">
  <p class="legenda-capa">{legenda}</p>
  <hr>
  {linhas}
  <p class="data">{data}</p>
</section>
'''

CSS = '''
@page { size: A4; margin: 15mm 15mm 16mm 15mm;
  @bottom-center { content: counter(page); font: 8pt 'DejaVu Sans', sans-serif; color: #6b7280; } }
@page :first { @bottom-center { content: none; } }
html { -webkit-print-color-adjust: exact; print-color-adjust: exact; }
body { font-family: 'DejaVu Sans', Arial, sans-serif; font-size: 10.3pt; line-height: 1.5; color: #1f2937; background: #fff; margin: 0; }
a { color: #1d4ed8; text-decoration: none; }
h1 { font-size: 20pt; color: #1e3a8a; border-bottom: 2.5px solid #1e3a8a; padding-bottom: 4px; margin: 0 0 14px; page-break-before: always; break-before: page; }
h2 { font-size: 14pt; color: #1e3a8a; border-left: 5px solid #3b82f6; padding-left: 8px; margin: 18px 0 8px; break-after: avoid; }
h3 { font-size: 11.5pt; color: #1e3a8a; margin: 14px 0 6px; break-after: avoid; }
h4 { font-size: 10.5pt; margin: 12px 0 4px; break-after: avoid; }
p { margin: 5px 0 7px; }
ul, ol { margin: 4px 0 8px; padding-left: 22px; }
li { margin: 2px 0; }
hr { border: 0; border-top: 1px solid #d1d5db; margin: 14px 0; }
blockquote { margin: 10px 0; padding: 8px 14px; background: #fff7ed; border-left: 5px solid #f59e0b; color: #1f2937; break-inside: avoid; }
blockquote p { margin: 3px 0; }
code { font-family: 'DejaVu Sans Mono', monospace; font-size: 8.4pt; background: #e8edf8; color: #1e3a8a; padding: 1px 4px; border-radius: 3px; }
pre { font-family: 'DejaVu Sans Mono', monospace; font-size: 7.6pt; line-height: 1.38; background: #f3f4f6; border-left: 4px solid #3b82f6; border-radius: 3px; padding: 8px 10px; white-space: pre-wrap; word-break: break-word; margin: 8px 0; }
h1 code, h2 code, h3 code { font-size: 0.9em; }
pre code, .codehilite code { background: none; color: inherit; padding: 0; font-size: inherit; }
.codehilite { margin: 8px 0; }
.codehilite pre { margin: 0; }
table { border-collapse: collapse; width: 100%; margin: 8px 0 12px; font-size: 8.8pt; break-inside: auto; }
thead { display: table-header-group; }
tr { break-inside: avoid; }
th { background: #1e3a8a; color: #fff; text-align: left; padding: 5px 7px; font-weight: bold; }
td { border-bottom: 1px solid #e5e7eb; padding: 4px 7px; vertical-align: top; }
tbody tr:nth-child(even) td { background: #f8fafc; }
img { max-width: 100%; }
p:has(> img:only-child) { text-align: center; margin: 10px 0 4px; }
p:has(> img:only-child) img { max-height: 118mm; border-radius: 6px; }
p:has(> img:only-child) + p:has(> em:only-child) { text-align: center; font-size: 8pt; color: #4b5563; margin: 2px 30px 12px; }
figure { margin: 10px 0 12px; text-align: center; break-inside: avoid; }
figure p { margin: 0; }
figure img { max-width: 88%; max-height: 200mm; border-radius: 6px; }
figcaption { font-size: 8pt; font-style: italic; color: #4b5563; margin: 4px 30px 0; }
.referencias p { padding-left: 0; text-align: left; font-size: 9.2pt; margin: 0 0 9px; }
/* capa */
.capa { text-align: center; padding-top: 32mm; break-after: page; }
.capa .marca { font-size: 46pt; font-weight: bold; color: #1e3a8a; letter-spacing: 1px; }
.capa .subtitulo { font-size: 17pt; color: #1f2937; margin: 6px 0 4px; }
.capa .selo { font-size: 10.5pt; color: #1d4ed8; margin: 6px 0 0; }
.capa img { width: 100%; max-height: 105mm; object-fit: cover; border-radius: 8px; margin-top: 14px; }
.capa .legenda-capa { font-size: 7.5pt; font-style: italic; color: #6b7280; margin: 4px 0 10px; }
.capa hr { width: 70%; margin: 6px auto 14px; border-top: 3px solid #3b82f6; }
.capa .linha { font-size: 10.5pt; color: #374151; margin: 10px 0; }
.capa .data { font-size: 9pt; color: #6b7280; margin-top: 16px; }
/* sumario */
h2#sumário { margin-top: 10px; }
h2#sumário + ul { font-size: FONTE_SUMARIO; line-height: 1.05; padding-left: 14px; }
h2#sumário + ul code { font-size: inherit; padding: 0 2px; line-height: 1; }
h2#sumário + ul ul { padding-left: 16px; margin: 0; }
h2#sumário + ul li { margin: 0; }
h2#sumário + ul a { display: flex; color: #1d4ed8; }
h2#sumário + ul a .t { flex: 0 1 auto; }
h2#sumário + ul a .p { flex: 1 0 auto; text-align: right; color: #374151; }
h2#sumário + ul a .p::before { content: ''; display: inline-block; }
h2#sumário + ul a .pontos { flex: 1 1 auto; border-bottom: 1px dotted #9ca3af; margin: 0 4px 3px; }
h2#sumário + ul a .p { flex: 0 0 auto; }
'''


def slugify(valor, separador='-'):
    return re.sub(r'[^\w\- ]', '', valor.strip().lower()).replace(' ', separador)


def preparar_markdown(texto):
    # trechos marcados como "so-github" aparecem so no repositorio (a capa do PDF os substitui)
    texto = re.sub(r'<!-- so-github -->.*?<!-- /so-github -->\n?', '', texto, flags=re.S)
    # listas aninhadas com 2 espacos (padrao do GitHub) passam a 4, como pede o Python-Markdown
    linhas, em_codigo = [], False
    for linha in texto.split('\n'):
        if linha.lstrip().startswith('```'):
            em_codigo = not em_codigo
        m = None if em_codigo else re.match(r'^((?:  )+)(?=[-*] |\d+\. )', linha)
        if m:
            linha = m.group(1) * 2 + linha[len(m.group(1)):]
        linhas.append(linha)
    return '\n'.join(linhas)


def converter_html(texto):
    md = markdown.Markdown(
        extensions=['fenced_code', 'tables', 'codehilite', 'toc', 'md_in_html'],
        extension_configs={'codehilite': {'guess_lang': False, 'noclasses': True, 'pygments_style': 'friendly'},
                           'toc': {'slugify': slugify}})
    corpo = md.convert(texto)
    # imagens: caminhos relativos a pasta docs/, onde o HTML temporario e gravado
    corpo = corpo.replace('src="docs/', 'src="')
    # a linha horizontal antes de um titulo de primeiro nivel e redundante (o titulo ja abre pagina)
    corpo = re.sub(r'<hr\s*/?>\s*(?=<h1)', '', corpo)
    return corpo


def numerar_sumario(corpo, paginas):
    """Coloca cada item do sumario em linha pontilhada com o numero da pagina."""
    inicio = corpo.find('<h2 id="sumário">')
    if inicio < 0:
        return corpo
    fim = corpo.find('</ul>\n<', inicio)
    fim = corpo.find('<h', fim)
    trecho = corpo[inicio:fim]

    def item(m):
        alvo, texto = m.group(1), m.group(2)
        pagina = paginas.get(html.unescape(alvo).lstrip('#'), '')
        return (f'<a href="{alvo}"><span class="t">{texto}</span><span class="pontos"></span>'
                f'<span class="p">{pagina}</span></a>')

    trecho = re.sub(r'<a href="([^"]+)">(.*?)</a>', item, trecho)
    return corpo[:inicio] + trecho + corpo[fim:]


def montar_html(guia, corpo):
    selo = f'<div class="selo">{guia["selo"]}</div>' if guia['selo'] else ''
    c = guia['capa']
    linhas = '\n  '.join(f'<p class="linha">{l}</p>' for l in c['linhas'])
    capa = CAPA.format(selo=selo, subtitulo=c['subtitulo'], imagem=c['imagem'], legenda=c['legenda'], linhas=linhas, data=c['data'])
    css = CSS.replace('FONTE_SUMARIO', guia['fonte_sumario'])
    # o primeiro titulo (# do arquivo) e substituido pela capa
    corpo = re.sub(r'^<h1[^>]*>.*?</h1>', '', corpo, count=1, flags=re.S)
    return (f'<!DOCTYPE html><html lang="pt-BR"><head><meta charset="utf-8">'
            f'<title>{guia["titulo"]}</title><style>{css}</style></head><body>'
            f'{capa}<div class="intro">{corpo}</div></body></html>')


def chromium():
    if os.environ.get('CHROMIUM'):
        return os.environ['CHROMIUM']
    for nome in ('chromium', 'chromium-browser', 'google-chrome', 'chrome'):
        if shutil.which(nome):
            return shutil.which(nome)
    candidatos = sorted(glob.glob('/opt/pw-browsers/chromium_headless_shell-*/chrome-*/chrome-headless-shell'))
    if candidatos:
        return candidatos[-1]
    raise SystemExit('Chromium não encontrado. Informe o caminho na variável CHROMIUM.')


def imprimir(arquivo_html, arquivo_pdf):
    subprocess.run([chromium(), '--headless', '--no-sandbox', '--disable-gpu', '--no-pdf-header-footer',
                    '--allow-file-access-from-files', f'--print-to-pdf={arquivo_pdf}', 'file://' + arquivo_html],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=300)


def titulos(corpo):
    """Lista (nivel, id, texto) dos titulos h1 e h2 do corpo, na ordem do documento."""
    saida = []
    for m in re.finditer(r'<h([12]) id="([^"]+)">(.*?)</h\1>', corpo, flags=re.S):
        texto = html.unescape(re.sub(r'<[^>]+>', '', m.group(3))).strip()
        saida.append((int(m.group(1)), m.group(2), texto))
    return saida


def localizar_paginas(pdf, lista):
    """Procura cada titulo no PDF, em ordem, a partir da pagina do titulo anterior. So contam
    os trechos em letra de titulo (12 pt ou mais), para nao confundir com o sumario."""
    doc = pymupdf.open(pdf)
    textos = []
    for pagina in doc:
        trechos = [span['text'] for bloco in pagina.get_text('dict')['blocks'] for linha in bloco.get('lines', [])
                   for span in linha['spans'] if span['size'] >= 12]
        textos.append(re.sub(r'\s+', '', ''.join(trechos)))
    paginas, atual = {}, 1  # a capa (pagina 1) nao tem titulos
    for nivel, ident, texto in lista:
        if ident == 'sumário':
            continue
        alvo = re.sub(r'\s+', '', texto)  # sem espacos: o PDF separa trechos de codigo
        for i in range(atual, len(textos)):
            if alvo in textos[i]:
                paginas[ident] = i + 1
                atual = i
                break
    doc.close()
    return paginas


def gerar(chave):
    guia = GUIAS[chave]
    with open(os.path.join(RAIZ, guia['md']), encoding='utf-8') as f:
        corpo = converter_html(preparar_markdown(f.read()))
    lista = titulos(corpo)
    destino = os.path.join(RAIZ, guia['pdf'])
    temporario = tempfile.NamedTemporaryFile('w', suffix='.html', dir=DOCS, delete=False, encoding='utf-8')
    temporario.close()
    try:
        paginas = {}
        for _ in range(2):
            with open(temporario.name, 'w', encoding='utf-8') as f:
                f.write(montar_html(guia, numerar_sumario(corpo, paginas)))
            imprimir(temporario.name, destino)
            paginas = localizar_paginas(destino, lista)
    finally:
        os.remove(temporario.name)

    # marcadores: titulos de primeiro nivel (#) no nivel 1; secoes (##) das Partes no nivel 2
    doc = pymupdf.open(destino)
    marcadores, em_parte = [], False
    for nivel, ident, texto in lista:
        if ident not in paginas:
            continue
        if guia['marcadores'] == 'secoes':
            if nivel == 2:
                marcadores.append([1, texto, paginas[ident]])
        elif nivel == 1:
            em_parte = texto.startswith('Parte ')
            marcadores.append([1, texto, paginas[ident]])
        elif em_parte:
            marcadores.append([2, texto, paginas[ident]])
    doc.set_toc(marcadores)
    doc.set_metadata({'title': guia['titulo']})
    doc.save(destino + '.tmp', garbage=3, deflate=True)
    doc.close()
    os.replace(destino + '.tmp', destino)
    total = pymupdf.open(destino).page_count
    print(f'{guia["pdf"]}: {total} páginas, {len(marcadores)} marcadores')


if __name__ == '__main__':
    escolhidos = sys.argv[1:] or list(GUIAS)
    for chave in escolhidos:
        gerar(chave)
