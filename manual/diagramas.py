"""Icones e diagramas (SVG) do manual do usuario do Zelo+."""

AZUL, VERDE, ROXO = '#2563eb', '#16a34a', '#9333ea'
LARANJA, VERMELHO, ESCURO, CINZA = '#ea580c', '#dc2626', '#1e3a8a', '#4b5563'

ICONES = {
    'sino': '<path d="M6 8a6 6 0 0 1 12 0c0 7 3 9 3 9H3s3-2 3-9"/><path d="M10.3 21a1.94 1.94 0 0 0 3.4 0"/>',
    'celular': '<rect x="6" y="2" width="12" height="20" rx="2.5"/><path d="M11 18h2"/>',
    'relogio': '<circle cx="12" cy="12" r="9.5"/><path d="M12 6.5V12l3.5 2"/>',
    'wifi': '<path d="M5 12.5a11 11 0 0 1 14 0"/><path d="M1.5 9a16 16 0 0 1 21 0"/><path d="M8.5 16a6 6 0 0 1 7 0"/><circle cx="12" cy="19.5" r=".9" fill="currentColor"/>',
    'enviar': '<path d="M22 2 11 13"/><path d="M22 2 15 22l-4-9-9-4 20-7z"/>',
    'pessoas': '<circle cx="9" cy="7" r="4"/><path d="M2 21v-2a4 4 0 0 1 4-4h6a4 4 0 0 1 4 4v2"/><path d="M16 3.1a4 4 0 0 1 0 7.8"/><path d="M22 21v-2a4 4 0 0 0-3-3.9"/>',
    'pilula': '<path d="m10.5 20.5 10-10a5 5 0 1 0-7-7l-10 10a5 5 0 1 0 7 7z"/><path d="m8.5 8.5 7 7"/>',
    'botao': '<circle cx="12" cy="12" r="9.5"/><circle cx="12" cy="12" r="5" fill="currentColor" fill-opacity=".25"/>',
    'grafico': '<path d="M3 3v18h18"/><path d="m7 15 4-4 3 3 6-7"/>',
    'camera': '<path d="M14.5 4h-5L7 7H4a2 2 0 0 0-2 2v9a2 2 0 0 0 2 2h16a2 2 0 0 0 2-2V9a2 2 0 0 0-2-2h-3z"/><circle cx="12" cy="13" r="3.5"/>',
    'alerta': '<path d="M10.3 3.9 1.8 18a2 2 0 0 0 1.7 3h17a2 2 0 0 0 1.7-3L13.7 3.9a2 2 0 0 0-3.4 0z"/><path d="M12 9v4"/><path d="M12 17h.01"/>',
    'bateria': '<rect x="2" y="7" width="17" height="10" rx="2"/><path d="M22 11v2"/><path d="M6 10v4M10 10v4"/>',
    'tomada': '<path d="M12 22v-5"/><path d="M9 8V2M15 8V2"/><path d="M18 8v5a4 4 0 0 1-4 4h-4a4 4 0 0 1-4-4V8z"/>',
    'moeda': '<circle cx="12" cy="12" r="9.5"/><path d="M8 12h8"/>',
    'caixa': '<path d="M21 8 12 3 3 8v8l9 5 9-5z"/><path d="m3 8 9 5 9-5"/><path d="M12 13v8"/>',
    'ok': '<path d="M20 6 9 17l-5-5"/>',
    'chave': '<circle cx="7.5" cy="15.5" r="5.5"/><path d="m11.5 11.5 9.5-9.5"/><path d="m15.5 7.5 3 3L22 7l-3-3"/>',
    'porta': '<path d="M4 21V4a1 1 0 0 1 1-1h11a1 1 0 0 1 1 1v17"/><path d="M2 21h20"/><circle cx="13.5" cy="12" r=".9" fill="currentColor"/>',
    'escudo': '<path d="M12 22s8-4 8-10V5l-8-3-8 3v7c0 6 8 10 8 10z"/>',
    'lista': '<path d="M9 6h12M9 12h12M9 18h12"/><circle cx="4" cy="6" r="1" fill="currentColor"/><circle cx="4" cy="12" r="1" fill="currentColor"/><circle cx="4" cy="18" r="1" fill="currentColor"/>',
    'engrenagem': '<circle cx="12" cy="12" r="3"/><path d="M19.4 15a1.7 1.7 0 0 0 .3 1.8l.1.1a2 2 0 1 1-2.8 2.8l-.1-.1a1.7 1.7 0 0 0-1.8-.3 1.7 1.7 0 0 0-1 1.5V21a2 2 0 1 1-4 0v-.1a1.7 1.7 0 0 0-1.1-1.5 1.7 1.7 0 0 0-1.8.3l-.1.1a2 2 0 1 1-2.8-2.8l.1-.1a1.7 1.7 0 0 0 .3-1.8 1.7 1.7 0 0 0-1.5-1H3a2 2 0 1 1 0-4h.1a1.7 1.7 0 0 0 1.5-1.1 1.7 1.7 0 0 0-.3-1.8l-.1-.1a2 2 0 1 1 2.8-2.8l.1.1a1.7 1.7 0 0 0 1.8.3H9a1.7 1.7 0 0 0 1-1.5V3a2 2 0 1 1 4 0v.1a1.7 1.7 0 0 0 1 1.5 1.7 1.7 0 0 0 1.8-.3l.1-.1a2 2 0 1 1 2.8 2.8l-.1.1a1.7 1.7 0 0 0-.3 1.8V9a1.7 1.7 0 0 0 1.5 1H21a2 2 0 1 1 0 4h-.1a1.7 1.7 0 0 0-1.5 1z"/>',
    'limpar': '<path d="M3 6h18"/><path d="M19 6v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6"/><path d="M8 6V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2"/>',
    'info': '<circle cx="12" cy="12" r="9.5"/><path d="M12 16v-4"/><path d="M12 8h.01"/>',
}


def icone(nome, cor='currentColor', tam=18, larg=2):
    return (f'<svg class="ic" viewBox="0 0 24 24" width="{tam}" height="{tam}" fill="none" stroke="{cor}" '
            f'stroke-width="{larg}" stroke-linecap="round" stroke-linejoin="round" style="color:{cor}">{ICONES[nome]}</svg>')


def icone_svg(nome, x, y, tam, cor):
    """Icone posicionado dentro de outro SVG."""
    e = tam / 24
    return (f'<g transform="translate({x},{y}) scale({e})" fill="none" stroke="{cor}" stroke-width="2" '
            f'stroke-linecap="round" stroke-linejoin="round" style="color:{cor}">{ICONES[nome]}</g>')


def _texto(x, y, linhas, tam=15, cor='#1f2937', peso='normal', ancora='start', dy=None):
    dy = dy or tam * 1.25
    out = []
    for i, l in enumerate(linhas):
        out.append(f'<text x="{x}" y="{y + i * dy:.1f}" font-size="{tam}" fill="{cor}" font-weight="{peso}" text-anchor="{ancora}">{l}</text>')
    return ''.join(out)


def mapa_mental():
    cx, cy = 460, 318
    ramos = [
        ('Lembrar', 'sino', AZUL, (150, 92), ['Alarme com som e luz', 'Visor na cor do compartimento', 'Nome do remédio no visor']),
        ('Liberar', 'porta', VERDE, (770, 92), ['Abre só o compartimento da dose', 'Paciente abre com o botão', 'Fecha sozinho em 3 minutos']),
        ('Avisar', 'enviar', LARANJA, (782, 318), ['Cuidador aos 6 minutos', 'Família aos 12 minutos', 'Confirma a chegada do paciente']),
        ('Registrar', 'grafico', ROXO, (770, 545), ['Histórico de cada dose', 'Adesão dos últimos 7 dias', 'Planilha para o médico']),
        ('Configurar', 'celular', ESCURO, (150, 545), ['Remédios e horários', 'Contatos e Telegram', 'Leitura da caixa por foto']),
        ('Proteger', 'escudo', VERMELHO, (138, 318), ['Modo do cuidador (botão)', 'Funciona sem internet', 'Bateria e abertura manual']),
    ]
    g = []
    for nome, ic, cor, (x, y), folhas in ramos:
        mx = (cx + x) / 2
        g.append(f'<path d="M{cx},{cy} C{mx},{cy} {mx},{y} {x},{y}" fill="none" stroke="{cor}" stroke-width="7" stroke-linecap="round" opacity=".85"/>')
    for nome, ic, cor, (x, y), folhas in ramos:
        w, h = 264, 124
        x0, y0 = x - w / 2, y - h / 2
        g.append(f'<rect x="{x0}" y="{y0}" width="{w}" height="{h}" rx="16" fill="#fff" stroke="{cor}" stroke-width="3"/>')
        g.append(f'<rect x="{x0}" y="{y0}" width="{w}" height="38" rx="16" fill="{cor}"/><rect x="{x0}" y="{y0 + 22}" width="{w}" height="16" fill="{cor}"/>')
        g.append(icone_svg(ic, x0 + 12, y0 + 7, 24, '#fff'))
        g.append(f'<text x="{x0 + 44}" y="{y0 + 27}" font-size="19" font-weight="bold" fill="#fff">{nome}</text>')
        for i, f in enumerate(folhas):
            yy = y0 + 62 + i * 23
            g.append(f'<circle cx="{x0 + 16}" cy="{yy - 5}" r="3.5" fill="{cor}"/>')
            g.append(f'<text x="{x0 + 27}" y="{yy}" font-size="14" fill="#1f2937">{f}</text>')
    g.append(f'<circle cx="{cx}" cy="{cy}" r="92" fill="{ESCURO}"/><circle cx="{cx}" cy="{cy}" r="82" fill="none" stroke="#fff" stroke-width="2" opacity=".5"/>')
    g.append(f'<text x="{cx}" y="{cy + 2}" font-size="40" font-weight="bold" fill="#fff" text-anchor="middle">Zelo+</text>')
    g.append(f'<text x="{cx}" y="{cy + 30}" font-size="15" fill="#c7d2fe" text-anchor="middle">o remédio certo,</text>')
    g.append(f'<text x="{cx}" y="{cy + 49}" font-size="15" fill="#c7d2fe" text-anchor="middle">na hora certa</text>')
    return f'<svg viewBox="0 0 920 625" width="100%">{"".join(g)}</svg>'


def _caixa(x, y, w, h, cor, titulo, linhas=(), ic=None, fundo=None, tam=15):
    fundo = fundo or '#fff'
    g = [f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="12" fill="{fundo}" stroke="{cor}" stroke-width="2.5"/>']
    tx = x + (40 if ic else 14)
    if ic:
        g.append(icone_svg(ic, x + 10, y + h / 2 - 12, 24, cor))
    total = 1 + len(linhas)
    y1 = y + h / 2 - (total - 1) * 9.5 + 5
    g.append(f'<text x="{tx}" y="{y1}" font-size="{tam}" font-weight="bold" fill="#111827">{titulo}</text>')
    for i, l in enumerate(linhas):
        g.append(f'<text x="{tx}" y="{y1 + 19 * (i + 1)}" font-size="{tam - 1.5}" fill="#374151">{l}</text>')
    return ''.join(g)


def _seta(x1, y1, x2, y2, cor='#64748b', rotulo=None, lado='d'):
    g = [f'<line x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}" stroke="{cor}" stroke-width="2.5" marker-end="url(#ponta)"/>']
    if rotulo:
        mx, my = (x1 + x2) / 2, (y1 + y2) / 2
        if y1 == y2:
            g.append(f'<text x="{mx}" y="{my - 7}" font-size="13.5" font-weight="bold" fill="{cor}" text-anchor="middle">{rotulo}</text>')
        else:
            dx = 8 if lado == 'd' else -8
            anc = 'start' if lado == 'd' else 'end'
            g.append(f'<text x="{mx + dx}" y="{my + 5}" font-size="13.5" font-weight="bold" fill="{cor}" text-anchor="{anc}">{rotulo}</text>')
    return ''.join(g)


def fluxo_dose():
    g = ['<defs><marker id="ponta" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="#64748b"/></marker></defs>']
    # coluna principal
    g.append(_caixa(250, 6, 300, 54, ESCURO, 'Chega o horário cadastrado', ['para um ou mais remédios'], 'relogio'))
    g.append(_seta(400, 60, 400, 84))
    g.append(_caixa(220, 86, 360, 74, AZUL, 'Alarme: som, luz e visor na cor', ['do compartimento, com o nome do remédio', '(1 minuto tocando, 1 minuto em silêncio)'], 'sino', '#eff6ff'))
    g.append(_seta(400, 160, 400, 186))
    # losango
    g.append('<polygon points="400,188 520,236 400,284 280,236" fill="#fefce8" stroke="#ca8a04" stroke-width="2.5"/>')
    g.append(_texto(400, 231, ['O paciente', 'apertou o botão?'], 14.5, '#111827', 'bold', 'middle', 18))
    # sim -> esquerda
    g.append(_seta(280, 236, 196, 236, VERDE, 'Sim', 'e'))
    g.append(_caixa(10, 196, 184, 82, VERDE, 'Abre a porta', ['só do compartimento', 'da dose'], 'porta', '#f0fdf4'))
    g.append(_seta(102, 278, 102, 304))
    g.append(_caixa(10, 306, 184, 82, VERDE, 'Paciente retira', ['o remédio; a porta', 'fecha em 3 minutos'], 'pilula', '#f0fdf4'))
    g.append(_seta(102, 388, 102, 414))
    g.append(_caixa(10, 416, 184, 82, VERDE, 'Registro', ['no horário (até 6 min)', 'ou com atraso'], 'ok', '#f0fdf4'))
    # nao -> direita
    g.append(_seta(520, 236, 604, 236, LARANJA, 'Não', 'd'))
    g.append(_caixa(606, 186, 184, 100, LARANJA, 'Aos 6 minutos', ['o cuidador recebe', 'mensagem pelo', 'Telegram'], 'enviar', '#fff7ed'))
    g.append(_seta(698, 286, 698, 312, LARANJA, 'sem acesso', 'e'))
    g.append(_caixa(606, 314, 184, 118, VERMELHO, 'Aos 12 minutos', ['o alarme para; o', 'cuidador e a família', 'recebem alerta; visor:', '"Dose pendente!"'], 'alerta', '#fef2f2'))
    g.append(_seta(606, 410, 196, 470, VERDE, None))
    g.append(f'<text x="400" y="416" font-size="13.5" fill="{VERDE}" font-weight="bold" text-anchor="middle">o botão continua abrindo a dose</text>')
    g.append(f'<text x="445" y="492" font-size="13.5" fill="{VERDE}" text-anchor="middle">(registro com atraso e confirmação a quem foi avisado)</text>')
    return f'<svg viewBox="0 0 800 500" width="100%" font-family="Helvetica,Arial,sans-serif">{"".join(g)}</svg>'


def linha_tempo():
    g = []
    x0, x1, y = 40, 860, 70
    esc = (x1 - x0) / 12
    for m in range(12):
        som = m % 2 == 0
        cor = AZUL if som else '#cbd5e1'
        g.append(f'<rect x="{x0 + m * esc:.1f}" y="{y - 14}" width="{esc:.1f}" height="28" fill="{cor}" stroke="#fff" stroke-width="2"/>')
        if som:
            g.append(icone_svg('sino', x0 + m * esc + esc / 2 - 9, y - 9, 18, '#fff'))
    for m in range(13):
        g.append(f'<text x="{x0 + m * esc:.1f}" y="{y + 34}" font-size="13" fill="#475569" text-anchor="middle">{m} min</text>')
    for m, cor, t1, t2 in [(0, AZUL, 'Começa o alarme', ''), (6, LARANJA, 'Aviso ao cuidador', '(Telegram)'), (12, VERMELHO, 'Alarme para:', 'alerta a todos')]:
        x = x0 + m * esc
        g.append(f'<line x1="{x:.1f}" y1="{y - 18}" x2="{x:.1f}" y2="{y - 40}" stroke="{cor}" stroke-width="2.5"/><circle cx="{x:.1f}" cy="{y - 44}" r="5" fill="{cor}"/>')
        anc = 'start' if m == 0 else ('end' if m == 12 else 'middle')
        g.append(f'<text x="{x:.1f}" y="{y - 54}" font-size="14" font-weight="bold" fill="{cor}" text-anchor="{anc}">{t1} {t2}</text>')
    g.append(f'<rect x="{x0}" y="{y + 50}" width="16" height="12" fill="{AZUL}"/><text x="{x0 + 22}" y="{y + 61}" font-size="13" fill="#374151">som e luz</text>')
    g.append(f'<rect x="{x0 + 110}" y="{y + 50}" width="16" height="12" fill="#cbd5e1"/><text x="{x0 + 132}" y="{y + 61}" font-size="13" fill="#374151">silêncio (o visor continua mostrando o remédio)</text>')
    g.append(f'<text x="{x0}" y="{y + 88}" font-size="13.5" fill="#374151">Aberto até 6 minutos: registro <tspan fill="{VERDE}" font-weight="bold">no horário</tspan>. Depois disso: <tspan fill="#b45309" font-weight="bold">com atraso</tspan>.</text>')
    g.append(f'<text x="{x0}" y="{y + 107}" font-size="13.5" fill="#374151">Sem abrir até 12 minutos: <tspan fill="{VERMELHO}" font-weight="bold">sem acesso</tspan> (passa a "com atraso" se o paciente abrir depois).</text>')
    return f'<svg viewBox="0 0 900 182" width="100%" font-family="Helvetica,Arial,sans-serif">{"".join(g)}</svg>'


def fluxo_horizontal(passos, cor=AZUL):
    """Passos em cartoes ligados por setas (HTML)."""
    out = ['<div class="fluxo">']
    for i, (ic, titulo, texto) in enumerate(passos):
        if i:
            out.append(f'<div class="fluxo-seta" style="color:{cor}">&#10140;</div>')
        out.append(f'<div class="fluxo-passo" style="border-color:{cor}"><div class="fluxo-num" style="background:{cor}">{i + 1}</div>'
                   f'{icone(ic, cor, 26)}<b>{titulo}</b><span>{texto}</span></div>')
    out.append('</div>')
    return ''.join(out)


def arvore_emergencia():
    g = ['<defs><marker id="ponta2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="#64748b"/></marker></defs>']

    def seta(x1, y1, x2, y2, rot=None, cor='#64748b', dx=8):
        s = f'<line x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}" stroke="{cor}" stroke-width="2.5" marker-end="url(#ponta2)"/>'
        if rot:
            s += f'<text x="{(x1 + x2) / 2 + dx}" y="{(y1 + y2) / 2 + 5}" font-size="13.5" font-weight="bold" fill="{cor}" text-anchor="{"start" if dx > 0 else "end"}">{rot}</text>'
        return s

    def losango(x, y, l1, l2):
        return (f'<polygon points="{x},{y - 40} {x + 120},{y} {x},{y + 40} {x - 120},{y}" fill="#fefce8" stroke="#ca8a04" stroke-width="2.5"/>'
                + _texto(x, y - 4, [l1, l2], 14, '#111827', 'bold', 'middle', 18))

    g.append(_caixa(270, 4, 360, 50, VERMELHO, 'Preciso abrir um compartimento agora', [], 'alerta', '#fef2f2'))
    g.append(seta(450, 54, 450, 78))
    g.append(losango(450, 120, 'O visor está', 'aceso?'))
    g.append(seta(330, 120, 216, 120, 'Sim', VERDE, -8))
    g.append(_caixa(10, 80, 204, 112, VERDE, 'Modo do cuidador', ['segure o botão 10 s,', 'escolha o compartimento', 'e confirme'], 'botao', '#f0fdf4'))
    g.append(seta(450, 160, 450, 192, 'Não', VERMELHO))
    g.append(losango(450, 234, 'Há um power bank', 'ou outra fonte 5 V?'))
    g.append(seta(570, 234, 686, 234, 'Sim', VERDE))
    g.append(_caixa(688, 180, 204, 112, VERDE, 'Ligue no USB-C', ['da lateral direita: o', 'dispenser religa e', 'funciona normalmente'], 'bateria', '#f0fdf4'))
    g.append(seta(450, 274, 450, 306, 'Não', VERMELHO))
    g.append(_caixa(250, 308, 400, 66, ESCURO, 'Abertura manual pela tampa técnica', ['moeda + pino com argola'], 'moeda', '#eef2ff'))
    return f'<svg viewBox="0 0 900 380" width="100%" font-family="Helvetica,Arial,sans-serif">{"".join(g)}</svg>'


def gestos_botao():
    """Tres gestos do botao no modo do cuidador."""
    def cart(x, titulo, sub, barra, cor):
        s = f'<rect x="{x}" y="0" width="270" height="118" rx="14" fill="#fff" stroke="{cor}" stroke-width="2.5"/>'
        s += f'<circle cx="{x + 40}" cy="48" r="22" fill="#111827" stroke="#64748b" stroke-width="3"/><circle cx="{x + 40}" cy="48" r="12" fill="#1f2937"/>'
        s += f'<text x="{x + 76}" y="40" font-size="16" font-weight="bold" fill="#111827">{titulo}</text>'
        s += f'<text x="{x + 76}" y="61" font-size="13.5" fill="#374151">{sub}</text>'
        s += f'<rect x="{x + 18}" y="88" width="234" height="12" rx="6" fill="#e5e7eb"/><rect x="{x + 18}" y="88" width="{barra}" height="12" rx="6" fill="{cor}"/>'
        return s
    g = [cart(0, 'Segurar 10 s', 'entra no modo', 234, ESCURO),
         cart(295, 'Toque curto', 'próxima opção', 22, AZUL),
         cart(590, 'Segurar 1,5 s', 'confirma a opção', 70, VERDE)]
    return f'<svg viewBox="0 0 862 120" width="100%" font-family="Helvetica,Arial,sans-serif">{"".join(g)}</svg>'
