#!/usr/bin/env python3
"""Gera src/leitor_js.h: o leitor de codigo de barras da pagina do Zelo+, compactado
(gzip) e gravado no ESP32, que o entrega em /leitor.js.

Uso (na pasta do projeto):
    cd tools/leitor && npm install && cd ../..
    python3 tools/leitor/gerar.py

O leitor usa so a parte de EAN-13 da biblioteca ZXing (Apache-2.0, ver LICENSE-ZXING).
So e preciso gerar de novo se leitor.js ou a versao da biblioteca mudarem.
"""
import gzip, os, subprocess

AQUI = os.path.dirname(os.path.abspath(__file__))
RAIZ = os.path.abspath(os.path.join(AQUI, '..', '..'))
saida_js = os.path.join(AQUI, 'zelo-leitor.min.js')
subprocess.run(['npx', 'esbuild', 'leitor.js', '--bundle', '--minify', '--format=iife',
                '--legal-comments=eof', f'--outfile={saida_js}'], cwd=AQUI, check=True)
dados = gzip.compress(open(saida_js, 'rb').read(), compresslevel=9, mtime=0)
os.remove(saida_js)
linhas = []
for i in range(0, len(dados), 20):
    linhas.append('  ' + ', '.join(f'0x{b:02x}' for b in dados[i:i + 20]) + ',')
cabecalho = f'''// Gerado por tools/leitor/gerar.py: nao editar a mao.
// Leitor de codigo de barras (EAN-13) da pagina, compactado em gzip ({len(dados)} bytes).
// Contem codigo da biblioteca ZXing (https://github.com/zxing-js/library),
// licenca Apache-2.0 (tools/leitor/LICENSE-ZXING).
#pragma once
#include <Arduino.h>

const uint8_t LEITOR_JS_GZ[] PROGMEM = {{
{chr(10).join(linhas)}
}};
const size_t LEITOR_JS_GZ_TAM = sizeof(LEITOR_JS_GZ);
'''
open(os.path.join(RAIZ, 'src', 'leitor_js.h'), 'w').write(cabecalho)
print(f'src/leitor_js.h: {len(dados)} bytes (gzip)')
