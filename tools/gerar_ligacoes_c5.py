# Gera o diagrama de ligacoes da montagem com ESP32-C5 (mesmo estilo de docs/ligacoes-autonomo.svg).
# Uso: python3 tools/gerar_ligacoes_c5.py docs/ligacoes-c5.svg  (o PNG e gerado a partir do SVG)
import sys

W, H = 1800, 1820
FONT = "DejaVu Sans, sans-serif"
out = []

RED = "#dc2626"      # +5 V
BLACK = "#111111"    # GND
BROWN = "#92400e"    # GND dos servos (depois do MOSFET)
ORANGE = "#f97316"   # 3,3 V
GREY = "#6b7280"


def t(x, y, s, size=17, fill="#111", bold=False, anchor="start", italic=False):
    w = "bold" if bold else "normal"
    st = ' font-style="italic"' if italic else ""
    s = s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
    out.append(f'<text x="{x}" y="{y}" font-size="{size}" fill="{fill}" font-weight="{w}" '
               f'text-anchor="{anchor}" font-family="{FONT}"{st}>{s}</text>')


def rect(x, y, w, h, fill, stroke="#111827", sw=2, rx=10, extra=""):
    out.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{rx}" fill="{fill}" stroke="{stroke}" stroke-width="{sw}" {extra}/>')


def line(pts, color, w=4, dash=None):
    d = " ".join(f"{x},{y}" for x, y in pts)
    da = f' stroke-dasharray="{dash}"' if dash else ""
    out.append(f'<polyline points="{d}" fill="none" stroke="{color}" stroke-width="{w}" stroke-linejoin="round" stroke-linecap="round"{da}/>')


def dot(x, y, color=BLACK, r=7):
    out.append(f'<circle cx="{x}" cy="{y}" r="{r}" fill="{color}"/>')


def pill(x, y, s, color, anchor="middle"):
    w = 14 * len(s) + 22
    x0 = x - w / 2 if anchor == "middle" else (x if anchor == "start" else x - w)
    rect(x0, y - 15, w, 30, "#ffffff", color, 3, 15)
    t(x0 + w / 2, y + 6, s, 17, color, True, "middle")
    return x0, x0 + w


def resistor(x, y, label, vertical=False, below=True):
    if vertical:
        rect(x - 11, y - 30, 22, 60, "#fde68a", "#92400e", 2, 5)
        for k in (-14, 0, 14):
            out.append(f'<rect x="{x-11}" y="{y+k-3}" width="22" height="5" fill="#b45309"/>')
        t(x + 20, y + 6, label, 15, "#92400e", True)
    else:
        rect(x - 30, y - 11, 60, 22, "#fde68a", "#92400e", 2, 5)
        for k in (-14, 0, 14):
            out.append(f'<rect x="{x+k-3}" y="{y-11}" width="5" height="22" fill="#b45309"/>')
        t(x, y + (32 if below else -18), label, 15, "#92400e", True, "middle")


def cap_v(x, y, label, sub, electrolytic=True):
    # capacitor no meio de uma linha vertical (placas horizontais)
    out.append(f'<rect x="{x-24}" y="{y-14}" width="48" height="28" fill="#ffffff"/>')
    out.append(f'<line x1="{x-22}" y1="{y-7}" x2="{x+22}" y2="{y-7}" stroke="#111" stroke-width="5"/>')
    if electrolytic:
        out.append(f'<path d="M{x-22},{y+9} Q{x},{y+1} {x+22},{y+9}" fill="none" stroke="#111" stroke-width="5"/>')
        t(x - 34, y - 6, "+", 18, RED, True, "middle")
    else:
        out.append(f'<line x1="{x-22}" y1="{y+7}" x2="{x+22}" y2="{y+7}" stroke="#111" stroke-width="5"/>')
    t(x + 32, y - 2, label, 16, "#111", True)
    if sub:
        t(x + 32, y + 18, sub, 13, "#374151")


# ---------------------------------------------------------------- titulo
out.append(f'<rect width="{W}" height="{H}" fill="#ffffff"/>')
t(40, 46, "Zelo+ — Ligações com a placa ESP32-C5 (Wi-Fi 2,4 e 5 GHz) + corte de energia dos servos", 30, "#111", True)
t(40, 78, "Placa GOOUUU ESP32-C5 (módulo ESPC5-32 N16R8) · servos ligados só durante o movimento (MOSFET IRLZ44N no G10) · "
          "versão do programa 18/11/2026", 17, "#374151")

# ---------------------------------------------------------------- placa C5
BX0, BX1, BY0, BY1 = 760, 1060, 150, 930
rect(BX0, BY0, BX1 - BX0, BY1 - BY0, "#1f2937", "#111827", 3, 14)
t((BX0 + BX1) / 2, BY0 + 40, "ESP32-C5", 26, "#ffffff", True, "middle")
t((BX0 + BX1) / 2, BY0 + 64, "GOOUUU · ESPC5-32 N16R8", 14, "#9ca3af", False, "middle")
rect(BX0 + 95, BY0 + 82, 110, 70, "#9ca3af", "#d1d5db", 2, 6)
t((BX0 + BX1) / 2, BY0 + 122, "Wi-Fi 6", 15, "#111827", True, "middle")
t((BX0 + BX1) / 2, BY0 + 140, "2,4 + 5 GHz", 13, "#111827", False, "middle")

LEFT = ["3V3", "EN", "G2", "G3", "G0", "G1", "G6", "G7", "G8", "G9", "G10", "G26", "G25", "5V", "GND", "NC"]
RIGHT = ["GND", "TX0", "RX0", "G24", "G23", "G15", "G27", "G4", "G5", "NC", "G28", "GND", "G14", "G13", "GND", "NC"]
PY0, PSTEP = 250, 40
PROIBIDO = {"G27", "G28", "G13", "G14", "TX0", "RX0"}
USADO_COR = {
    "3V3": ORANGE, "G2": "#4338ca", "G3": "#65a30d", "G0": "#b45309", "G1": "#b45309", "G6": "#7c2d12",
    "G7": "#0f766e", "G8": "#16a34a", "G9": "#2563eb", "G10": "#be185d", "G26": "#db2777", "G25": "#0891b2",
    "5V": RED, "GND": BLACK, "G24": "#7c3aed", "G23": "#ea580c", "G4": "#ca8a04", "G5": "#0d9488",
}


def pin_y(i):
    return PY0 + i * PSTEP


def LP(name):
    return BX0, pin_y(LEFT.index(name))


def RP(name, n=0):
    idx = [i for i, p in enumerate(RIGHT) if p == name][n]
    return BX1, pin_y(idx)


for side, names, x in (("L", LEFT, BX0), ("R", RIGHT, BX1)):
    for i, n in enumerate(names):
        y = pin_y(i)
        if n in PROIBIDO:
            fill, tc, b = "#7f1d1d", "#fca5a5", True
        elif n in USADO_COR and not (n == "GND" and side == "L") and not (n == "GND" and side == "R" and i != 14):
            fill, tc, b = USADO_COR[n], "#ffffff", True
        else:
            fill, tc, b = GREY, "#9ca3af", False
        out.append(f'<circle cx="{x}" cy="{y}" r="8" fill="{fill}" stroke="#fff" stroke-width="2"/>')
        if side == "L":
            t(x + 16, y + 6, n, 17, tc, b)
        else:
            t(x - 16, y + 6, n, 17, tc, b, "end")
        if n in PROIBIDO:
            t(x + 16, y + 6, "✕ não usar", 14, "#dc2626", True)

# USB-C da placa
for k, (lab, sub) in enumerate((("USB", "nativo"), ("TTL", "gravar"))):
    cx = BX0 + 85 + k * 130
    rect(cx - 45, BY1 - 8, 90, 34, "#d1d5db", "#6b7280", 2, 8)
    t(cx, BY1 + 15, lab, 15, "#111827", True, "middle")
t((BX0 + BX1) / 2 + 65, BY1 + 48, "gravar e Serial Monitor: USB-C “TTL”", 14, "#b91c1c", True, "middle")

# ---------------------------------------------------------------- tela TFT (esquerda, topo)
TX0_, TY0 = 110, 150
rect(TX0_, TY0, 320, 330, "#dbeafe", "#1e3a8a", 3, 12)
t(TX0_ + 160, TY0 + 32, "Tela TFT 2,25\"", 20, "#1e3a8a", True, "middle")
t(TX0_ + 160, TY0 + 52, "ST7789P3 · SPI", 13, "#1e3a8a", False, "middle")
rect(TX0_ + 30, TY0 + 70, 170, 240, "#0f172a", "#111827", 2, 10)
t(TX0_ + 115, TY0 + 150, "08:30", 36, "#ffffff", True, "middle")
rect(TX0_ + 50, TY0 + 175, 130, 50, "#16a34a", "#16a34a", 1, 8)
t(TX0_ + 115, TY0 + 197, "Medicação", 14, "#fff", True, "middle")
t(TX0_ + 115, TY0 + 215, "em dia", 14, "#fff", True, "middle")
TPINS = ["GND", "VCC", "SCL", "SDA", "RST", "DC", "CS", "BL"]
TPX = TX0_ + 320
tp = {}
for i, n in enumerate(TPINS):
    y = TY0 + 80 + i * 32
    tp[n] = (TPX, y)
    out.append(f'<circle cx="{TPX}" cy="{y}" r="7" fill="#1e3a8a" stroke="#fff" stroke-width="2"/>')
    t(TPX - 14, y + 6, n, 15, "#1e3a8a", True, "end")

# canais verticais das ligacoes da tela
canais = {"CS": ("G2", 712, "#4338ca"), "DC": ("G3", 692, "#65a30d"), "BL": ("G6", 672, "#7c2d12"),
          "RST": ("G7", 652, "#0f766e"), "SCL": ("G8", 632, "#16a34a"), "SDA": ("G9", 612, "#2563eb")}
for tn, (pn, cx, col) in canais.items():
    x0, y0 = tp[tn]
    x1, y1 = LP(pn)
    line([(x0, y0), (cx, y0), (cx, y1), (x1, y1)], col, 4)
    t((x0 + cx) / 2 + 30 if tn in ("RST", "SCL", "SDA") else x0 + 70, y0 - 6, f"{tn} → {pn}", 13, col, True, "middle")
line([tp["VCC"], (tp["VCC"][0] + 40, tp["VCC"][1])], ORANGE, 4)
pill(tp["VCC"][0] + 75, tp["VCC"][1], "3V3", ORANGE)
line([tp["GND"], (tp["GND"][0] + 40, tp["GND"][1])], BLACK, 4)
pill(tp["GND"][0] + 75, tp["GND"][1], "GND", BLACK)
# 3V3 da placa
x, y = LP("3V3")
line([(x, y), (x - 26, y)], ORANGE, 4)
pill(x - 60, y, "3V3", ORANGE)

# ---------------------------------------------------------------- relogio DS3231 (opcional)
RX0_, RY0 = 110, 500
rect(RX0_, RY0, 320, 132, "#fef3c7", "#92400e", 3, 12)
t(RX0_ + 20, RY0 + 32, "Relógio DS3231", 20, "#92400e", True)
t(RX0_ + 20, RY0 + 52, "RTC · I2C · opcional", 13, "#92400e")
out.append(f'<circle cx="{RX0_+70}" cy="{RY0+92}" r="24" fill="#e5e7eb" stroke="#6b7280" stroke-width="2"/>')
t(RX0_ + 70, RY0 + 97, "pilha", 12, "#374151", True, "middle")
RPX = RX0_ + 320
rp = {}
for i, n in enumerate(["SCL", "SDA", "VCC", "GND"]):
    y = RY0 + 25 + i * 28
    rp[n] = (RPX, y)
    out.append(f'<circle cx="{RPX}" cy="{y}" r="7" fill="#92400e" stroke="#fff" stroke-width="2"/>')
    t(RPX - 14, y + 6, n, 15, "#92400e", True, "end")
for rn, pn, cx in (("SDA", "G0", 572), ("SCL", "G1", 552)):
    x0, y0 = rp[rn]
    x1, y1 = LP(pn)
    line([(x0, y0), (cx, y0), (cx, y1), (x1, y1)], "#b45309", 4)
t(RPX + 6, rp["SCL"][1] - 10, "SCL → G1 · SDA → G0", 13, "#b45309", True)
line([rp["VCC"], (rp["VCC"][0] + 30, rp["VCC"][1])], ORANGE, 4)
pill(rp["VCC"][0] + 64, rp["VCC"][1], "3V3", ORANGE)
line([rp["GND"], (rp["GND"][0] + 30, rp["GND"][1])], BLACK, 4)
pill(rp["GND"][0] + 64, rp["GND"][1], "GND", BLACK)

# ---------------------------------------------------------------- buzzer (G26) e LED (G25)
x, y = LP("G26")
line([(x, y), (354, y)], "#db2777", 4)
out.append(f'<circle cx="330" cy="{y}" r="24" fill="#1f2937"/><circle cx="330" cy="{y}" r="7" fill="#9ca3af"/>')
t(330, y - 32, "Buzzer ativo", 15, "#111", True, "middle")
t(600, y - 8, "G26 → buzzer (+)", 13, "#db2777", True, "middle")
line([(306, y), (260, y)], BLACK, 4)
pill(226, y, "GND", BLACK)
x, y = LP("G25")
line([(x, y), (550, y)], "#0891b2", 4)
resistor(650, y, "", below=True)
t(650, y + 4, "", 1)
out.append(f'<polygon points="550,{y-16} 550,{y+16} 520,{y}" fill="#fca5a5" stroke="#b91c1c" stroke-width="3"/>')
out.append(f'<line x1="516" y1="{y-16}" x2="516" y2="{y+16}" stroke="#b91c1c" stroke-width="4"/>')
line([(516, y), (490, y)], BLACK, 4)
pill(456, y, "GND", BLACK)
t(600, y - 16, "G25 → 220 Ω → LED (perna longa)", 13, "#0891b2", True, "middle")

# ---------------------------------------------------------------- botao (G24)
x, y = RP("G24")
line([(x, y), (1300, y)], "#7c3aed", 4)
rect(1300, y - 24, 48, 48, "#ede9fe", "#7c3aed", 3, 8)
out.append(f'<circle cx="1324" cy="{y}" r="13" fill="#7c3aed"/>')
t(1324, y - 34, "Botão", 15, "#7c3aed", True, "middle")
line([(1348, y), (1390, y)], BLACK, 4)
pill(1425, y, "GND", BLACK)
t(1180, y - 8, "G24 → botão", 13, "#7c3aed", True, "middle")

# ---------------------------------------------------------------- trilhos
Y5V, YGNDS, YGND = 1000, 1200, 1510
line([(80, Y5V), (1780, Y5V)], RED, 6)
t(90, Y5V - 12, "trilho +5 V (saída do módulo UPS)", 15, RED, True)
line([(80, YGND), (1780, YGND)], BLACK, 6)
t(90, YGND - 12, "trilho GND (comum a tudo)", 15, BLACK, True)

# ---------------------------------------------------------------- C5 GND
x, y = RP("GND", 2)
line([(x, y), (x + 26, y)], BLACK, 4)
pill(x + 60, y, "GND", BLACK)

# ---------------------------------------------------------------- alimentacao do C5: diodo + 470 uF + 100 nF
x5, y5 = LP("5V")
XD = 470
line([(XD, Y5V), (XD, y5), (x5, y5)], RED, 5)
dot(XD, Y5V, RED)
# diodo no trecho horizontal (catodo para o lado da placa)
DX = 530
out.append(f'<rect x="{DX-32}" y="{y5-13}" width="64" height="26" rx="6" fill="#111827"/>')
out.append(f'<rect x="{DX+16}" y="{y5-13}" width="9" height="26" fill="#d1d5db"/>')
t(XD - 12, y5 + 30, "diodo Schottky (1N5819, 1N5817 ou SR160)", 13, "#111", True, "end")
t(XD - 12, y5 + 48, "faixa do diodo → lado do C5", 12, "#b91c1c", True, "end")
for cx, lab, sub, ele in ((640, "470 µF", "10 V+", True), (712, "100 nF", "", False)):
    dot(cx, y5, RED)
    line([(cx, y5), (cx, y5 + 95)], RED, 4)
    cap_v(cx, y5 + 60, "", "", ele)
    line([(cx, y5 + 74), (cx, y5 + 110)], BLACK, 4)
    pill(cx, y5 + 128, "GND", BLACK)
t(640, y5 + 172, "470 µF + 100 nF junto ao pino 5V", 13, "#111", True, "middle")
t(640, y5 + 190, "(“−” do 470 µF no GND)", 12, "#374151", False, "middle")


# ---------------------------------------------------------------- fonte + UPS
rect(80, 1060, 260, 120, "#fee2e2", "#b91c1c", 3, 10)
t(210, 1092, "Módulo UPS LX-2BUPS", 18, "#b91c1c", True, "middle")
t(210, 1114, "+ baterias 18650", 14, "#7f1d1d", False, "middle")
t(210, 1134, "saída 5 V sem interrupção", 13, "#7f1d1d", False, "middle")
t(210, 1164, "entrada 5V+/5V− · saída 5 V", 12, "#7f1d1d", True, "middle")
line([(120, 1060), (120, Y5V)], RED, 5)
dot(120, Y5V, RED)
t(128, 1045, "5V+", 13, RED, True)
line([(340, 1150), (370, 1150), (370, YGND)], BLACK, 5)
dot(370, YGND, BLACK)
t(378, 1145, "GND", 13, BLACK, True)
rect(80, 1260, 260, 80, "#f3f4f6", "#374151", 3, 10)
t(210, 1292, "Fonte 5 V / 3 A", 18, "#111", True, "middle")
t(210, 1316, "tomada · saída USB-C", 13, "#374151", False, "middle")
line([(150, 1260), (150, 1180)], "#6b7280", 4, "8,6")
t(158, 1232, "USB-C", 12, "#374151", True)

# ---------------------------------------------------------------- MOSFET IRLZ44N
MX = 980  # centro do corpo
MYB = 1250  # topo do corpo
# aba (dreno) com furo
rect(MX - 46, MYB - 34, 92, 40, "#d1d5db", "#6b7280", 2, 4)
out.append(f'<circle cx="{MX}" cy="{MYB-16}" r="9" fill="#ffffff" stroke="#6b7280" stroke-width="2"/>')
rect(MX - 46, MYB, 92, 76, "#111827", "#111827", 2, 4)
t(MX, MYB + 32, "IRLZ44N", 15, "#ffffff", True, "middle")
t(MX, MYB + 54, "MOSFET", 12, "#9ca3af", False, "middle")
LG, LD, LS = MX - 28, MX, MX + 28
for lx, ln in ((LG, "G"), (LD, "D"), (LS, "S")):
    out.append(f'<rect x="{lx-4}" y="{MYB+76}" width="8" height="40" fill="#9ca3af"/>')
    t(lx, MYB + 134, ln, 16, "#111", True, "middle")
t(MX + 62, MYB + 30, "letras de frente:", 12, "#374151")
t(MX + 62, MYB + 47, "G · D · S", 13, "#111", True)
t(MX + 62, MYB + 64, "(gate · drain · source)", 12, "#374151")
# drain (aba) -> trilho GND dos servos
line([(LD, MYB - 34), (LD, YGNDS)], BROWN, 5)
dot(LD, YGNDS, BROWN)
line([(LD, YGNDS), (1690, YGNDS)], BROWN, 6)
t(1040, YGNDS + 30, "trilho GND dos servos (liga ao GND só quando o MOSFET conduz)", 15, BROWN, True)
# source -> GND
line([(LS, MYB + 116), (LS, MYB + 150), (LS + 0, YGND)], BLACK, 5)
dot(LS, YGND, BLACK)
# gate -> 220 ohm -> G10 ; 10k gate -> GND
GY = MYB + 160
line([(LG, MYB + 116), (LG, GY), (760, GY)], "#be185d", 4)
dot(LG, GY, "#be185d")
resistor(860, GY, "220 Ω", below=False)
pill(720, GY, "G10", "#be185d")
line([(LG, GY), (LG - 70, GY), (LG - 70, YGND)], "#be185d", 4)
resistor(LG - 70, (GY + YGND) / 2, "10 kΩ", vertical=True)
dot(LG - 70, YGND, BLACK)
t(LG - 60, GY + 120, "", 12)
# G10 da placa
x, y = LP("G10")
line([(x, y), (x - 26, y)], "#be185d", 4)
pill(x - 62, y, "G10", "#be185d")
t(x - 104, y + 6, "→ MOSFET (gate)", 13, "#be185d", True, "end")

# ---------------------------------------------------------------- servos
# (sinal na ordem que evita cruzamentos: C2 <- G5, C3 <- G4, C1 <- G23)
SERVOS = [("C2", "Compartimento 2", "G5", 1240, "#0d9488"),
          ("C3", "Compartimento 3", "G4", 1440, "#ca8a04"),
          ("C1", "Compartimento 1", "G23", 1640, "#ea580c")]
SY0, SY1 = 1060, 1160
for nome, comp, pn, sx, col in SERVOS:
    cx = sx + 30
    rect(cx - 82, SY0, 164, SY1 - SY0, "#dbeafe", "#1e3a8a", 3, 10)
    t(cx, SY0 + 34, f"Servo {nome}", 19, "#1e3a8a", True, "middle")
    t(cx, SY0 + 56, comp, 13, "#1e3a8a", False, "middle")
    t(cx, SY0 + 76, "SG90 · 5 V", 13, "#1e3a8a", False, "middle")
    # sinal
    x, y = RP(pn)
    line([(x, y), (sx, y), (sx, SY0)], col, 4)
    resistor(x + 70, y, "1 kΩ", below=False)
    t(sx - 8, SY0 - 8, "sinal", 12, col, True, "end")
    t(x + 150, y - 8, f"{pn} → servo {nome}", 13, col, True, "start")
    # +5 V
    line([(cx + 40, SY0), (cx + 40, Y5V)], RED, 4)
    dot(cx + 40, Y5V, RED)
    t(cx + 48, SY0 - 8, "+5V", 12, RED, True)
    # GND (marrom) -> trilho GND dos servos
    line([(cx, SY1), (cx, YGNDS)], BROWN, 4)
    dot(cx, YGNDS, BROWN)
t(1440, 1182, "fio marrom/preto de cada servo → GND dos servos (nunca direto no GND)", 13, BROWN, True, "middle")

# capacitor 1000 uF entre +5 V e GND (extremo direito)
CXC = 1770
line([(CXC, Y5V), (CXC, 1336)], RED, 4)
line([(CXC, 1364), (CXC, YGND)], BLACK, 4)
dot(CXC, Y5V, RED)
dot(CXC, YGND, BLACK)
cap_v(CXC, 1350, "", "", True)
t(CXC - 44, 1330, "1000 µF", 15, "#111", True, "end")
t(CXC - 44, 1350, "entre +5 V e GND,", 12, "#374151", False, "end")
t(CXC - 44, 1366, "perto dos servos", 12, "#374151", False, "end")

# ---------------------------------------------------------------- resumo e legenda
YR = 1590
t(40, YR, "Resumo das ligações", 22, "#111", True)
rows = [
    ("Tela TFT — SCL / SDA / CS", "G8 / G9 / G2", "VCC no 3V3 (nunca no 5 V)"),
    ("Tela TFT — DC / RST / BL", "G3 / G7 / G6", "GND no GND"),
    ("Relógio DS3231 — SDA / SCL", "G0 / G1", "VCC 3V3 · opcional"),
    ("Botão / LED / Buzzer", "G24 / G25 / G26", "botão e LED/buzzer ao GND"),
    ("Servos C1 / C2 / C3 — sinal", "G23 / G5 / G4", "cada um com resistor de 1 kΩ"),
    ("MOSFET IRLZ44N — gate", "G10 (220 Ω)", "10 kΩ do gate ao GND"),
    ("MOSFET — drain / source", "GND dos servos / GND", "corta a energia dos servos"),
    ("Alimentação do C5", "+5 V → diodo → 5V", "470 µF + 100 nF no pino 5V"),
    ("Não ligar nada em", "G27 · G28 · G13 · G14", "TX0 · RX0 (gravação)"),
]
for i, (a, b, c) in enumerate(rows):
    y = YR + 30 + i * 22
    t(40, y, a, 15, "#111")
    t(350, y, b, 15, "#111", True)
    t(560, y, c, 15, "#374151")
XL = 1180
t(XL, YR, "Legenda", 22, "#111", True)
leg = [(RED, "+5 V (saída do módulo UPS)"), (BLACK, "GND (terra comum)"),
       (BROWN, "GND dos servos (via MOSFET)"), (ORANGE, "3,3 V do C5")]
for i, (c, s) in enumerate(leg):
    y = YR + 30 + i * 26
    line([(XL, y - 5), (XL + 50, y - 5)], c, 6)
    t(XL + 62, y, s, 15, "#111")
dot(XL + 25, YR + 30 + 4 * 26 - 5, BLACK)
t(XL + 62, YR + 30 + 4 * 26, "= ligação (emenda)", 15, "#111")
pill(XL + 25, YR + 30 + 5 * 26 - 5, "G10", "#be185d")
t(XL + 62, YR + 30 + 5 * 26, "etiqueta: ligue ao pino de mesmo nome", 15, "#111")
t(XL, YR + 30 + 6 * 26, "Fios que se cruzam SEM ● não estão ligados.", 15, "#b91c1c", True)
t(XL, YR + 30 + 7 * 26, "✕ = pino da placa que não deve ser usado.", 14, "#374151")

svg = (f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}">'
       + "".join(out) + "</svg>")
open(sys.argv[1], "w").write(svg)
print("ok", len(svg))
