# Zelo+ — Modelo 3D do gabinete para impressao (protótipo com protoboard).
# Gera os STL em cad/gabinete/, confere colisoes entre pecas e componentes e
# grava a montagem completa (montagem.stl) para visualizacao.
# Requer: pip install manifold3d trimesh numpy
#
# Eixos: X = largura (da esquerda para a direita, olhando de frente), Y = profundidade
# (da frente para tras), Z = altura (da base para cima). Medidas em mm.
import math, os, sys, itertools
import numpy as np, manifold3d as mf, trimesh

AQUI = os.path.dirname(os.path.abspath(__file__))
SAIDA = os.path.join(AQUI, "gabinete")
os.makedirs(SAIDA, exist_ok=True)
mf.set_circular_segments(48)
M, CS = mf.Manifold, mf.CrossSection

# ---------------- medidas gerais (Parte B do guia) ----------------
C, P, A = 210.0, 130.0, 100.0          # comprimento, profundidade, altura
PAREDE = 3.0
X_COMP = [3.0, 72.15, 141.3]           # inicio de cada compartimento (abertura de 65,7)
LARG_COMP = 65.7
Y_DIV = 78.7                           # divisoria compartimento / area tecnica (78,7 a 80,7)
Z_PISO = 45.0                          # piso dos compartimentos (45 a 47,5)
H_Y, H_Z = 94.5, 99.0                  # eixo das dobradicas
S_DY, S_DZ = 4.0, -26.0                # eixo do servo em relacao ao eixo da dobradica
FOLGA = 0.3

def caixa(x0, y0, z0, x1, y1, z1):
    return M.cube((x1 - x0, y1 - y0, z1 - z0)).translate((x0, y0, z0))
def cil_x(x0, x1, y, z, r):            # cilindro ao longo de X
    return M.cylinder(x1 - x0, r).rotate((0, 90, 0)).translate((x0, y, z))
def cil_y(y0, y1, x, z, r):            # cilindro ao longo de Y
    return M.cylinder(y1 - y0, r).rotate((-90, 0, 0)).translate((x, y0, z))
def cil_z(z0, z1, x, y, r):
    return M.cylinder(z1 - z0, r).translate((x, y, z0))
def uniao(lista):
    return M.batch_boolean(lista, mf.OpType.Add)

def tampa_x(i):                        # inicio (X) da tampa do compartimento i
    return X_COMP[i] + 0.35

# =================== CORPO (casca com suportes da frente e das laterais) ===================
def corpo():
    pecas = [caixa(0, 0, 0, C, P, A) - caixa(PAREDE, PAREDE, -1, C - PAREDE, P - PAREDE, A + 1)]
    # apoios da bandeja (Z 43 a 45) e das tampas (Z 95 a 98), com chanfro de 45 graus por baixo
    for (x0, y0, x1, y1) in [(3, 3, 207, 5.3), (3, 3, 5.3, 78.6), (204.7, 3, 207, 78.6)]:
        for z0, z1 in [(43, 45), (95, 98)]:
            pecas.append(caixa(x0, y0, z0, x1, y1, z1))
    # apoios da tampa tecnica: laterais e parede de tras (com os imas)
    pecas += [caixa(3, 100.3, 94, 9.5, 121, 98), caixa(200.5, 100.3, 94, 207, 121, 98),
              caixa(3, 121, 94, 207, 127, 98)]
    # colunas dos 4 parafusos do fundo (M3 autoatarraxante)
    for (x, y) in [(8, 8), (202, 8), (8, 122), (202, 122)]:
        pecas.append(cil_z(2.5, 12, x, y, 4.0))
    # tela TFT: apoio de baixo, batente esquerdo e trava do lado direito
    pecas += [caixa(68.5, 3, 10, 141.5, 9, 12), caixa(64.5, 3, 10, 68.5, 10, 35),
              caixa(141.5, 3, 10, 145.5, 10, 35), caixa(140, 7.6, 12, 141.5, 9.6, 35)]
    # berco do botao (placa de apoio a 11 mm da parede)
    pecas += [caixa(25.5, 14, 11.5, 46.5, 16, 33.5), caixa(25.5, 3, 11.5, 27, 16, 33.5),
              caixa(45, 3, 11.5, 46.5, 16, 33.5)]
    # suporte do LED atras do acrilico
    pecas += [caixa(158, 8, 12.5, 190, 10, 32.5), caixa(158, 3, 12.5, 160, 10, 32.5),
              caixa(188, 3, 12.5, 190, 10, 32.5)]
    # porta-buzzer na parede lateral esquerda
    pecas.append(cil_x(3, 11, 100, 20, 7.6))
    # prateleira do conector USB-C na parede lateral direita
    pecas.append(caixa(200, 46, 9.5, 207, 64, 12.3))
    c = uniao(pecas)
    furos = [
        caixa(27, -1, 13.5, 45, 4, 31.5),            # botao 18 x 18
        caixa(76.5, -1, 14, 133.5, 4, 31),           # janela da tela 57 x 17
        caixa(154, -1, 13.5, 194, 4, 31.5),          # janela do acrilico 40 x 18
        caixa(153, 1, 12.5, 195, 4, 32.5),           # rebaixo do acrilico (2 mm)
        cil_y(7.9, 10.1, 174, 22.5, 2.6),            # furo do LED de 5 mm
        cil_x(2.9, 11.1, 100, 20, 6.15),             # alojamento do buzzer (12 mm)
        caixa(206, 47.9, 12.3, 211, 62.1, 17.7),     # recorte do USB-C 14,2 x 5,4
    ]
    for y in (96, 100, 104):                          # furos de som (3 x 3)
        for z in (16, 20, 24):
            furos.append(cil_x(-1, 4, y, z, 1.2))
    for (x, y) in [(8, 8), (202, 8), (8, 122), (202, 122)]:
        furos.append(cil_z(1, 12.5, x, y, 1.25))
    for (x, y) in [(6.25, 104.5), (203.75, 104.5), (6.25, 124), (203.75, 124)]:   # imas (6 x 2)
        furos.append(cil_z(95.6, 98.1, x, y, 3.1))
    return c - uniao(furos)

# =================== BANDEJA (compartimentos, suportes dos servos e dobradicas) ===================
def bandeja():
    pecas = [caixa(3.3, 3.3, 45, 206.7, Y_DIV, 47.5),                   # piso
             caixa(3.3, Y_DIV, 45, 206.7, 80.7, 98)]                     # divisoria da area tecnica
    for k in (1, 2):                                                     # divisorias entre compartimentos
        x = X_COMP[k] - 3.45
        pecas += [caixa(x + 0.725, 3.3, 47.5, x + 2.725, Y_DIV, 93), caixa(x, 5.4, 93, x + 3.45, Y_DIV, 98)]
    furos = []
    for i in range(3):
        a = tampa_x(i)
        # placa do servo (o corpo do SG90 atravessa a janela; abas presas com 2 parafusos M2)
        pecas.append(caixa(a + 29.5, 80.7, 45, a + 32, 108, 87))   # nasce na mesa de impressao
        furos += [caixa(a + 29, 92.3, 56.0, a + 32.5, 104.7, 79.3),
                  cil_x(a + 29, a + 32.5, 98.5, 81.35, 0.8), cil_x(a + 29, a + 32.5, 98.5, 53.95, 0.8)]
        # olhais das dobradicas (dos dois lados da tampa) e apoio da tampa tecnica
        for (x0, x1) in [(a, a + 4), (a + 61, a + 65)]:
            pecas += [caixa(x0, 80.7, 45, x1, 100, 98), cil_x(x0, x1, H_Y, H_Z, 2.5)]
            furos.append(cil_x(x0 - 0.1, x1 + 0.1, H_Y, H_Z, 0.95))     # pino: filamento de 1,75 mm
    return uniao(pecas) - uniao(furos)

# =================== TAMPAS DOS COMPARTIMENTOS ===================
def tampa(i):
    a = tampa_x(i)
    t = caixa(a, 3.35, 98, a + 65, 96.95, 100)
    t = t - caixa(a - 0.1, 91.5, 96, a + 4.3, 98, 102) - caixa(a + 60.7, 91.5, 96, a + 65.1, 98, 102)
    for (x0, x1) in [(a + 4.5, a + 9.5), (a + 55.5, a + 60.5)]:          # articulacoes
        t = t + cil_x(x0, x1, H_Y, H_Z, 2.5)
        t = t - cil_x(x0 - 0.1, x1 + 0.1, H_Y, H_Z, 0.95)
    return t

# =================== TAMPA TECNICA E TRAVA DE MOEDA ===================
TRAVA_X, TRAVA_Y = 192.0, 113.0
def tampa_tecnica():
    t = caixa(3.35, 97.5, 98, 206.65, 126.65, 100)
    furos = [caixa(95, 97.4, 97, 115, 100.5, 101),                       # rebaixo para o dedo
             cil_z(97, 101, TRAVA_X, TRAVA_Y, 3.2), cil_z(99, 101, TRAVA_X, TRAVA_Y, 5.2)]
    for (x, y) in [(6.25, 104.5), (203.75, 104.5), (6.25, 124), (203.75, 124)]:
        furos.append(cil_z(97.9, 99.7, x, y, 3.1))
    return t - uniao(furos)
def trava_botao():                                                       # cabeca com fenda + eixo
    b = cil_z(99, 100, TRAVA_X, TRAVA_Y, 5.0) + cil_z(93.6, 99, TRAVA_X, TRAVA_Y, 3.0)
    b = b + caixa(TRAVA_X - 1.5, TRAVA_Y - 1.5, 91.8, TRAVA_X + 1.5, TRAVA_Y + 1.5, 93.6)
    return b - caixa(TRAVA_X - 4, TRAVA_Y - 0.6, 99.3, TRAVA_X + 4, TRAVA_Y + 0.6, 100.1)
def trava_lingueta():                                                    # gira por baixo do apoio de tras
    l = caixa(TRAVA_X - 3, TRAVA_Y - 3, 91.8, TRAVA_X + 3, TRAVA_Y + 11, 93.6)
    return l - caixa(TRAVA_X - 1.7, TRAVA_Y - 1.7, 91.7, TRAVA_X + 1.7, TRAVA_Y + 1.7, 93.7)

# =================== FUNDO REMOVIVEL (berços dos componentes) ===================
PROTO = (12, 34, 95, 89)          # protoboard de 400 pontos (83 x 55 mm)
UPS = (106, 34, 196, 76)          # modulo UPS LX-2BUPS (90 x 42 x 33 mm)
RTC = (108, 90, 146, 112)         # relogio DS3231 (38 x 22 mm)
def moldura(x0, y0, x1, y1, h, folga=0.3, parede=1.5):
    ext = caixa(x0 - folga - parede, y0 - folga - parede, 2.5, x1 + folga + parede, y1 + folga + parede, 2.5 + h)
    return ext - caixa(x0 - folga, y0 - folga, 2, x1 + folga, y1 + folga, 3 + h)
def fundo():
    f = caixa(3.3, 3.3, 0, 206.7, 126.7, 2.5)
    f = f + moldura(*PROTO, 3) + moldura(*UPS, 6) + moldura(*RTC, 3)
    f = f + (caixa(160, 100, 2.5, 180, 112, 8.5) - cil_x(159, 181, 106, 8.5, 5.2))   # berço do capacitor
    furos = []
    for (x, y) in [(8, 8), (202, 8), (8, 122), (202, 122)]:
        furos += [cil_z(-1, 3.5, x, y, 1.7), cil_z(-1, 1.2, x, y, 3.2)]
    for (x, y) in [(18, 18), (192, 18), (18, 112), (192, 112)]:          # pes de borracha (10 mm)
        furos.append(cil_z(-1, 1.0, x, y, 5.2))
    for k in range(6):                                                    # ventilacao sob o modulo UPS
        x = 116 + k * 13
        furos.append(caixa(x, 42, -1, x + 2.5, 68, 3))
    return f - uniao(furos)

# =================== ACIONAMENTO (pecas de cad/gerar_stl.py, na posicao fechada) ===================
def carregar_stl(nome):
    m = trimesh.load(os.path.join(AQUI, nome))
    return M(mf.Mesh(vert_properties=np.array(m.vertices, dtype=np.float32), tri_verts=np.array(m.faces, dtype=np.uint32)))
def rot_x(graus):   # rotacao em torno de X no plano YZ, sentido de Y para Z
    return graus
def acionamento(i):
    a = tampa_x(i)
    alav = carregar_stl("alavanca-tampa.stl")        # local: x=Y-H, y=Z-H, z=X
    alav = alav.transform(np.array([[0, 0, 1, a + 12], [1, 0, 0, H_Y], [0, 1, 0, H_Z]], dtype=float))
    braco = carregar_stl("braco-servo.stl")          # local: braco ao longo de +x, espessura em z
    ang = math.radians(73.16)                        # posicao fechada (graus a partir de +Y)
    cy, sz = math.cos(ang), math.sin(ang)
    Sy, Sz = H_Y + S_DY, H_Z + S_DZ
    # x_local -> (Y, Z) na direcao do pino; y_local -> perpendicular; z_local -> X (lado do servo)
    braco = braco.transform(np.array([[0, 0, 1, a + 15.5], [cy, -sz, 0, Sy], [sz, cy, 0, Sz]], dtype=float))
    return alav, braco

# =================== componentes (volumes simplificados, so para conferir encaixes) ===================
def componentes():
    comp = {}
    comp["protoboard + ESP32"] = caixa(PROTO[0], PROTO[1], 2.5, PROTO[2], PROTO[3], 12) + caixa(27, 47, 12, 78, 75, 22)
    comp["modulo UPS + baterias"] = caixa(UPS[0], UPS[1], 2.5, UPS[2], UPS[3], 35.5)
    comp["relogio DS3231"] = caixa(RTC[0], RTC[1], 2.5, RTC[2], RTC[3], 12)
    comp["tela TFT"] = caixa(68.5, 3, 12, 141.5, 7.2, 33) + caixa(68.6, 7.2, 13, 72, 21, 32)
    comp["botao"] = caixa(30, 3.5, 16.5, 42, 14, 28.5)
    comp["LED"] = cil_y(8, 18, 174, 22.5, 2.5)
    comp["buzzer"] = cil_x(3.2, 12.5, 100, 20, 6.0)
    comp["capacitor"] = cil_x(161, 179, 106, 8.6, 5.0)
    comp["conector USB-C"] = caixa(198, 48.5, 12.4, 207.2, 61.5, 17.5)
    for i in range(3):
        a = tampa_x(i)
        comp[f"servo {i+1}"] = caixa(a + 22.7, 92.6, 56.4, a + 45.4, 104.4, 78.9) + \
                               caixa(a + 27, 92.6, 51.65, a + 29.5, 104.4, 83.65)
    return comp

def salvar(m, nome):
    mesh = m.to_mesh()
    t = trimesh.Trimesh(vertices=np.array(mesh.vert_properties)[:, :3], faces=np.array(mesh.tri_verts))
    t.export(os.path.join(SAIDA, nome))
    return t

if __name__ == "__main__":
    pecas = {"corpo": corpo(), "bandeja": bandeja(), "fundo": fundo(), "tampa tecnica": tampa_tecnica(),
             "trava (botao)": trava_botao(), "trava (lingueta)": trava_lingueta()}
    for i in range(3):
        pecas[f"tampa {i+1}"] = tampa(i)
        al, br = acionamento(i)
        pecas[f"alavanca {i+1}"] = al
        pecas[f"braco {i+1}"] = br
    comp = componentes()
    # ---- conferencia de colisoes (volume de intersecao acima de 1 mm3) ----
    todos = {**pecas, **comp}
    ignorar = {frozenset(p) for p in [("tampa 1", "alavanca 1"), ("tampa 2", "alavanca 2"), ("tampa 3", "alavanca 3"),
                                       ("trava (botao)", "trava (lingueta)")]}
    problemas = []
    for (n1, m1), (n2, m2) in itertools.combinations(todos.items(), 2):
        if frozenset((n1, n2)) in ignorar: continue
        v = (m1 ^ m2).volume()
        if v > 1.0: problemas.append((n1, n2, v))
    print("colisoes:", "nenhuma" if not problemas else "")
    for p in problemas: print(f"  {p[0]} x {p[1]}: {p[2]:.1f} mm3")
    # ---- arquivos para impressao ----
    arquivos = {"corpo": "corpo.stl", "bandeja": "bandeja.stl", "fundo": "fundo.stl",
                "tampa tecnica": "tampa-tecnica.stl", "trava (botao)": "trava-botao.stl",
                "trava (lingueta)": "trava-lingueta.stl", "tampa 1": "tampa-compartimento.stl"}
    for nome, arq in arquivos.items():
        t = salvar(pecas[nome], arq)
        b = t.bounds[1] - t.bounds[0]
        print(f"{arq}: {b[0]:.1f} x {b[1]:.1f} x {b[2]:.1f} mm, {t.volume/1000:.1f} cm3, estanque={t.is_watertight}")
    salvar(uniao(list(pecas.values())), "montagem.stl")
    salvar(uniao(list(comp.values())), "componentes.stl")
