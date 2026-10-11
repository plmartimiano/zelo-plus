# Gera os STL do acionamento da tampa (mesma geometria de zelo_acionamento.scad).
# Requer: pip install manifold3d trimesh numpy
import math, numpy as np, manifold3d as mf, trimesh
mf.set_circular_segments(96)
CS, M = mf.CrossSection, mf.Manifold
import os
OUT = os.path.dirname(os.path.abspath(__file__)) + "/"

# ---------------- parametros (os mesmos do zelo_acionamento.scad) ----------------
PHI0 = -35.0          # angulo da alavanca com a tampa fechada (graus, a partir da horizontal, para tras)
R_BRACO = 20.0        # raio do furo do pino no braco do servo
RASGO_INI, RASGO_FIM = 6.0, 12.3   # trecho do rasgo ao longo da alavanca (a partir do eixo da dobradica)
FURO = 3.4            # largura do rasgo e furo do braco (pino de 3,0 mm)
PAREDE = 1.6
ESP_ALAVANCA = 3.0
ESP_BRACO = 3.2
PINO_D = 3.0

def circ(r, c=(0, 0)): return CS.circle(r).translate(c)
def rect(x0, y0, x1, y1): return CS.square((x1 - x0, y1 - y0)).translate((x0, y0))

# ---------------- alavanca (colada sob a tampa) ----------------
u = (math.cos(math.radians(PHI0)), math.sin(math.radians(PHI0)))
def ao_longo(r): return (r * u[0], r * u[1])
rb = FURO / 2 + PAREDE
bossa = CS.batch_hull([circ(rb, ao_longo(RASGO_INI)), circ(rb, ao_longo(RASGO_FIM))])
sapata = rect(-12.0, -3.0, -1.8, -1.0)                 # face de colagem em Z = -1 (face de baixo da tampa)
ligacao = CS.batch_hull([sapata, circ(rb, ao_longo(RASGO_INI))])
perfil = sapata + ligacao + bossa
perfil = perfil ^ rect(-20, -30, 30, -1.0)            # nada acima da face de baixo da tampa
rasgo = CS.batch_hull([circ(FURO / 2, ao_longo(RASGO_INI)), circ(FURO / 2, ao_longo(RASGO_FIM))])
perfil = perfil - rasgo
alavanca = perfil.extrude(ESP_ALAVANCA) + sapata.extrude(9.0).translate((0, 0, -6.0))   # sapata de 9 mm para a cola, do lado oposto ao braco
# impressa com a face de colagem (Z = -1) na mesa
alavanca.to_mesh()
def salvar(m, nome):
    mesh = m.to_mesh()
    t = trimesh.Trimesh(vertices=np.array(mesh.vert_properties)[:, :3], faces=np.array(mesh.tri_verts))
    t.export(OUT + nome)
    b = t.bounds
    print(f"{nome}: {b[1][0]-b[0][0]:.1f} x {b[1][1]-b[0][1]:.1f} x {b[1][2]-b[0][2]:.1f} mm, volume {t.volume:.0f} mm3, estanque={t.is_watertight}")
    return t
salvar(alavanca, "alavanca-tampa.stl")

# ---------------- braco do servo (vai sobre o braco simples do SG90) ----------------
CUBO_R, PONTA_R = 6.0, 4.0
braco2d = CS.batch_hull([circ(CUBO_R), circ(PONTA_R, (R_BRACO, 0))])
braco = braco2d.extrude(ESP_BRACO)
braco = braco - CS.circle(2.9).extrude(ESP_BRACO)                       # acesso ao parafuso central
braco = braco - circ(FURO / 2, (R_BRACO, 0)).extrude(ESP_BRACO)         # furo do pino
# encaixe do braco original do SG90 (rebaixo de 1,6 mm na face de cima da impressao)
HORN_L, HORN_CUBO, HORN_PONTA = 15.5, 3.8, 2.3
encaixe = CS.batch_hull([circ(HORN_CUBO + 0.2), circ(HORN_PONTA + 0.2, (HORN_L, 0))])
braco = braco - encaixe.extrude(1.6).translate((0, 0, ESP_BRACO - 1.6))
for r in (8.0, 12.0):                                                     # furos para 2 parafusos M2 (ou cola)
    braco = braco - circ(0.65, (r, 0)).extrude(ESP_BRACO)
salvar(braco, "braco-servo.stl")

# ---------------- pino com argola (impresso deitado) ----------------
L_HASTE = ESP_ALAVANCA + 0.5 + ESP_BRACO + 2.6      # alavanca + folga + braco + trava
ARG_EXT, ARG_INT, ARG_ESP = 6.0, 3.6, 2.4
h = PINO_D - 0.3                                      # base plana de 0,3 mm para aderir a mesa
eixo_z = PINO_D / 2 - 0.3
haste = M.cylinder(L_HASTE, PINO_D / 2).rotate((0, 90, 0)).translate((0, 0, eixo_z))
# ponta com ressalto (trava por pressao) e fenda que deixa as duas metades flexionarem
ressalto = M.cylinder(1.2, PINO_D / 2 + 0.35, PINO_D / 2 - 0.4).rotate((0, 90, 0)).translate((L_HASTE - 1.2, 0, eixo_z))
pino = haste + ressalto
fenda = M.cube((4.2, 0.8, 10)).translate((L_HASTE - 4.2 + 0.01, -0.4, -2))
pino = pino - fenda
cabeca = M.cylinder(1.0, PINO_D / 2 + 1.0).rotate((0, 90, 0)).translate((-1.0, 0, eixo_z))
argola = (CS.circle(ARG_EXT) - CS.circle(ARG_INT)).extrude(ARG_ESP).translate((-1.0 - ARG_EXT + 0.6, 0, 0))
pino = pino + cabeca + argola
pino = pino ^ M.cube((100, 100, 50)).translate((-50, -50, 0))       # corta abaixo da mesa (base plana)
salvar(pino, "pino-argola.stl")
