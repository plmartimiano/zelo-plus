"""Ilustracao do Zelo+ em perspectiva cavaleira (sem escala) para o manual."""
import math
K, A = 0.8, math.radians(40)
CX, SY = K * math.cos(A), K * math.sin(A)
COR = ['#2563eb', '#16a34a', '#9333ea']

def P(x, y, z):
    return (x + y * CX, -z - y * SY)

def poly(pts, fill, stroke='#0f172a', sw=0.6, extra=''):
    s = ' '.join(f'{a:.1f},{b:.1f}' for a, b in (P(*p) for p in pts))
    return f'<polygon points="{s}" fill="{fill}" stroke="{stroke}" stroke-width="{sw}" stroke-linejoin="round" {extra}/>'

def rect_front(x0, x1, z0, z1, y, fill, **kw):
    return poly([(x0, y, z0), (x1, y, z0), (x1, y, z1), (x0, y, z1)], fill, **kw)

def rect_top(x0, x1, y0, y1, z, fill, **kw):
    return poly([(x0, y0, z), (x1, y0, z), (x1, y1, z), (x0, y1, z)], fill, **kw)

def rect_side(y0, y1, z0, z1, x, fill, **kw):
    return poly([(x, y0, z0), (x, y1, z0), (x, y1, z1), (x, y0, z1)], fill, **kw)

LIDS = [(3.75 + i * 68.75, 3.75 + i * 68.75 + 65) for i in range(3)]

def produto(aberta=None, callouts=True, destaque=None, visor='08:30', moeda=False, largura=520, rotulos=None):
    W, D, H, BAND = 210, 130, 100, 44
    g = []
    # sombra
    g.append(f'<ellipse cx="{P(105, 65, 0)[0]:.1f}" cy="{P(105,65,0)[1]+6:.1f}" rx="150" ry="16" fill="#0f172a" opacity=".10"/>')
    # lateral direita
    g.append(rect_side(0, D, 0, BAND, W, '#1f2937'))
    g.append(rect_side(0, D, BAND, H, W, '#1e3a8a'))
    # USB-C na lateral direita
    g.append(rect_side(55, 64, 20, 23.5, W, '#0b1220', stroke='#94a3b8', sw=0.5))
    # frente
    g.append(rect_front(0, W, 0, BAND, 0, '#111827'))
    g.append(rect_front(0, W, BAND, H, 0, '#2348c9'))
    # botao
    g.append(rect_front(27, 45, 13, 31, 0, '#0b0f17', stroke='#64748b', sw=0.8))
    g.append(rect_front(30, 42, 16, 28, 0, '#1f2937', stroke='#334155', sw=0.5))
    # visor (janela 57 x 17)
    x0, z0 = 76.5, 13.5
    g.append(rect_front(x0 - 1.5, x0 + 58.5, z0 - 1.5, z0 + 18.5, 0, '#020617', stroke='#475569', sw=0.6))
    if visor:
        a, b = P(x0, 0, z0 + 17)
        if visor == 'alarme':
            g.append(rect_front(x0, x0 + 57, z0, z0 + 17, 0, COR[0], stroke='none'))
            g.append(f'<text x="{a+28.5:.1f}" y="{b+7:.1f}" font-size="5.2" fill="#fff" text-anchor="middle" font-family="Helvetica,Arial" font-weight="bold">Hora do remédio! (C1)</text>')
            g.append(f'<text x="{a+28.5:.1f}" y="{b+14:.1f}" font-size="5.6" fill="#fff" text-anchor="middle" font-family="Helvetica,Arial" font-weight="bold">LOSARTANA 50 MG</text>')
        else:
            g.append(rect_front(x0, x0 + 57, z0, z0 + 17, 0, '#000', stroke='none'))
            g.append(f'<text x="{a+2:.1f}" y="{b+13:.1f}" font-size="11.5" fill="#fff" font-family="Helvetica,Arial" font-weight="bold">{visor}</text>')
            g.append(f'<rect x="{a+30:.1f}" y="{b+1.5:.1f}" width="25.5" height="14" rx="2" fill="#16a34a"/>')
            g.append(f'<text x="{a+42.75:.1f}" y="{b+7.4:.1f}" font-size="4.6" fill="#fff" text-anchor="middle" font-family="Helvetica,Arial" font-weight="bold">Medicação</text>')
            g.append(f'<text x="{a+42.75:.1f}" y="{b+13:.1f}" font-size="4.6" fill="#fff" text-anchor="middle" font-family="Helvetica,Arial" font-weight="bold">em dia</text>')
    # luz de alerta
    luz = '#fde68a' if visor == 'alarme' else '#e5e7eb'
    g.append(rect_front(154, 194, 12, 32, 0, luz, stroke='#94a3b8', sw=0.8))
    if visor == 'alarme':
        a, b = P(174, 0, 22)
        g.append(f'<circle cx="{a:.1f}" cy="{b:.1f}" r="26" fill="#facc15" opacity=".25"/>')
    # logotipo
    a, b = P(48, 0, 62)
    g.append(f'<g transform="translate({a:.1f},{b:.1f})"><path d="M0,-9 C-4,-15 -13,-12 -11,-5 C-9,1 0,6 0,6 C0,6 9,1 11,-5 C13,-12 4,-15 0,-9Z" fill="none" stroke="#fff" stroke-width="2.4"/>'
             f'<path d="M-3,-4 h6 M0,-7 v6" stroke="#fff" stroke-width="1.8"/></g>')
    g.append(f'<text x="{a+16:.1f}" y="{b+7:.1f}" font-size="27" fill="#fff" font-family="Helvetica,Arial" font-weight="bold">Zelo+</text>')
    # topo
    g.append(rect_top(0, W, 0, D, H, '#1e40af'))
    # tampa tecnica
    g.append(rect_top(3.35, 206.65, 98.5, 127.7, H + 0.3, '#334155'))
    a, b = P(195, 113, H + 0.3)
    g.append(f'<ellipse cx="{a:.1f}" cy="{b:.1f}" rx="4.2" ry="2.4" fill="#cbd5e1" stroke="#0f172a" stroke-width=".5"/>'
             f'<line x1="{a-3:.1f}" y1="{b:.1f}" x2="{a+3:.1f}" y2="{b:.1f}" stroke="#0f172a" stroke-width="1"/>')
    if moeda:
        g.append(f'<g transform="translate({a+9:.1f},{b-14:.1f}) rotate(-25)"><ellipse rx="7" ry="7" fill="#d4a017" stroke="#7c5c00" stroke-width=".8"/><text y="2.5" font-size="6" text-anchor="middle" fill="#7c5c00" font-family="Arial" font-weight="bold">R$</text></g>')
    # tampas
    for i, (l0, l1) in enumerate(LIDS):
        if aberta == i:
            # cavidade
            g.append(rect_top(l0, l1, 3.5, 96.5, H, '#0f172a'))
            g.append(rect_top(l0 + 4, l1 - 4, 10, 90, H, '#cbd5e1', stroke='none', sw=0))
            for k, (px, py) in enumerate([(18, 30), (30, 52), (44, 38), (24, 70), (50, 66)]):
                cx, cy = P(l0 + px, py, H)
                g.append(f'<ellipse cx="{cx:.1f}" cy="{cy:.1f}" rx="4.4" ry="2.6" fill="#fff" stroke="#94a3b8" stroke-width=".5"/>')
            # tampa levantada (90 graus, junto a dobradica)
            g.append(poly([(l0, 96, H), (l1, 96, H), (l1, 96, H + 93.6), (l0, 96, H + 93.6)], '#1f2937'))
            g.append(poly([(l0, 96, H + 93.6), (l1, 96, H + 93.6), (l1, 98, H + 93.6), (l0, 98, H + 93.6)], '#334155'))
            cx, cy = P((l0 + l1) / 2, 96, H + 55)
            g.append(f'<circle cx="{cx:.1f}" cy="{cy:.1f}" r="9" fill="{COR[i]}" stroke="#fff" stroke-width="1.2"/><text x="{cx:.1f}" y="{cy+4.2:.1f}" font-size="12" text-anchor="middle" fill="#fff" font-family="Arial" font-weight="bold">{i+1}</text>')
        else:
            fill = '#1f2937'
            g.append(rect_top(l0, l1, 3.5, 96.6, H + 0.3, fill, sw=0.8 if destaque != i else 2.2, stroke='#0f172a' if destaque != i else '#facc15'))
            cx, cy = P((l0 + l1) / 2, 50, H + 0.3)
            g.append(f'<ellipse cx="{cx:.1f}" cy="{cy:.1f}" rx="11" ry="6.5" fill="{COR[i]}" stroke="#fff" stroke-width="1.2"/><text x="{cx:.1f}" y="{cy+4:.1f}" font-size="11" text-anchor="middle" fill="#fff" font-family="Arial" font-weight="bold">{i+1}</text>')
    # furos de som (indicados na aresta esquerda)
    pts = []
    if callouts:
        rot = rotulos or {}
        alvos = {
            1: (P(LIDS[0][0] + 20, 30, H), 'tl'),
            2: (P(150, 113, H), 'tr'),
            3: (P(36, 0, 22), 'bl'),
            4: (P(105, 0, 13), 'b'),
            5: (P(174, 0, 12), 'br'),
            6: (P(W, 60, 22), 'r'),
            7: (P(0, 40, 70), 'l'),
        }
        pos = {1: (-45, -215), 2: (330, -250), 3: (-45, 40), 4: (105, 45), 5: (235, 45), 6: (345, -20), 7: (-45, -80)}
        for n, ((tx, ty), _) in alvos.items():
            if rot and n not in rot:
                continue
            lx, ly = pos[n]
            g.append(f'<line x1="{lx:.1f}" y1="{ly:.1f}" x2="{tx:.1f}" y2="{ty:.1f}" stroke="#f59e0b" stroke-width="1.3" stroke-dasharray="{"3 2" if n == 7 else "none"}"/>'
                     f'<circle cx="{tx:.1f}" cy="{ty:.1f}" r="2.2" fill="#f59e0b"/>'
                     f'<circle cx="{lx:.1f}" cy="{ly:.1f}" r="11" fill="#f59e0b" stroke="#fff" stroke-width="1.5"/>'
                     f'<text x="{lx:.1f}" y="{ly+4.6:.1f}" font-size="13" text-anchor="middle" fill="#fff" font-family="Arial" font-weight="bold">{n}</text>')
    vb = '-62 -268 430 330' if callouts else '-15 -265 325 290'
    if aberta is not None and not callouts:
        vb = '-15 -300 325 330'
    return f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="{vb}" width="{largura}">' + ''.join(g) + '</svg>'

if __name__ == '__main__':
    import sys
    open(sys.argv[1], 'w').write('<html><body style="margin:0;background:#fff">' + produto() + produto(aberta=0, callouts=False, visor='alarme') + '</body></html>')
