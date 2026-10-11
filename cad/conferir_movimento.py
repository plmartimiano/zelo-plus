# Confere o movimento de abertura (0 a 90 graus): tampa + alavanca + braco do servo
# contra as pecas fixas do gabinete e os componentes.
import math, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gabinete import *
fixas = {"corpo": corpo(), "bandeja": bandeja(), "tampa tecnica": tampa_tecnica(),
         "trava": trava_botao() + trava_lingueta(), **componentes()}
PHI0, R = -35.0, 20.0
Sy, Sz = H_Y + S_DY, H_Z + S_DZ
pior = []
for i in range(3):
    a = tampa_x(i)
    t0 = tampa(i)
    al, _ = acionamento(i)
    braco0 = carregar_stl("braco-servo.stl")
    for graus in range(0, 91, 10):
        th = math.radians(graus)
        def girar(m):   # gira em torno do eixo da dobradica (abre levantando a frente)
            return m.translate((0, -H_Y, -H_Z)).rotate((-graus, 0, 0)).translate((0, H_Y, H_Z))
        movel = {"tampa": girar(t0), "alavanca": girar(al)}
        # posicao do pino: circulo do braco x linha do rasgo
        u = (math.cos(math.radians(PHI0) - th), math.sin(math.radians(PHI0) - th))
        dy, dz = H_Y - Sy, H_Z - Sz
        b = 2 * (u[0] * dy + u[1] * dz); c = dy * dy + dz * dz - R * R
        t = max((-b - math.sqrt(b*b - 4*c)) / 2, (-b + math.sqrt(b*b - 4*c)) / 2)
        if not (5.9 <= t <= 12.4):
            t = min((-b - math.sqrt(b*b - 4*c)) / 2, (-b + math.sqrt(b*b - 4*c)) / 2)
        py, pz = H_Y + t * u[0], H_Z + t * u[1]
        ang = math.atan2(pz - Sz, py - Sy); cy, sz = math.cos(ang), math.sin(ang)
        movel["braco"] = braco0.transform(np.array([[0, 0, 1, a + 15.5], [cy, -sz, 0, Sy], [sz, cy, 0, Sz]], dtype=float))
        for nm, mm in movel.items():
            for nf, mfix in fixas.items():
                v = (mm ^ mfix).volume()
                if v > 1.0: pior.append((i + 1, graus, nm, nf, round(v, 1)))
print("colisoes no movimento:", "nenhuma" if not pior else pior[:20])
