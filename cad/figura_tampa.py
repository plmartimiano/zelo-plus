# Gera docs/tampa-dobradica.png: cortes do modelo com as folgas da tampa, os apoios em tres
# lados e a dobradica com pinos de aco. Requer: pip install manifold3d trimesh numpy matplotlib
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gabinete import *
import matplotlib; matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.patches import Polygon
plt.rcParams['font.family'] = 'DejaVu Sans'
AZUL, AZUL_CL, TAMPA, CINZA, VERDE, VERM = '#1d4ed8', '#93c5fd', '#1f2937', '#6b7280', '#16a34a', '#dc2626'
i = 1; a = X_COMP[i]
def desenhar(ax, itens, plano, pos):
    for m, cor in itens:
        if plano == 'x':
            sec = m.translate((-pos, 0, 0)).rotate((0, -90, 0)).slice(0.0); f = lambda p: (p[1], -p[0])
        else:
            sec = m.translate((0, -pos, 0)).rotate((90, 0, 0)).slice(0.0); f = lambda p: (p[0], -p[1])
        for poly in sec.to_polygons():
            ax.add_patch(Polygon([f(p) for p in poly], closed=True, facecolor=cor, edgecolor='#111827', lw=0.5))
def estilo(ax, lim, titulo, xl):
    ax.set_xlim(*lim[0]); ax.set_ylim(*lim[1]); ax.set_aspect('equal')
    ax.set_title(titulo, color='#1e3a8a', fontsize=11, fontweight='bold')
    ax.set_xlabel(xl, fontsize=8); ax.set_ylabel('Z (mm)', fontsize=8); ax.tick_params(labelsize=7); ax.grid(alpha=0.25)
def nota(ax, txt, xy, xytext):
    ax.annotate(txt, xy=xy, xytext=xytext, fontsize=7.5, color='#111827',
                arrowprops=dict(arrowstyle='->', color='#374151', lw=0.7))
fig, axs = plt.subplots(2, 2, figsize=(11, 8.2))
# 1. apoios laterais (corte frontal na divisoria)
ax = axs[0][0]
desenhar(ax, [(corpo(), AZUL), (bandeja(), AZUL_CL), (tampa(0), TAMPA), (tampa(1), TAMPA)], 'y', 40)
estilo(ax, ((60, 80), (86, 101.5)), '1. Apoios laterais (corte frontal)', 'X (mm)')
nota(ax, 'folga 0,8', (68.3, 99.2), (61, 100.7)); nota(ax, 'ressalto 3 mm\n(apoio 2,2 mm)', (74.5, 96.2), (75.5, 91))
nota(ax, 'chanfro 45°', (67, 92.6), (60.6, 89)); nota(ax, 'tampa 3 mm', (78, 98.5), (75.4, 100.9))
# 2. apoio frontal
ax = axs[0][1]
desenhar(ax, [(corpo(), AZUL), (tampa(1), TAMPA)], 'x', a + 40)
estilo(ax, ((0, 20), (86, 101.5)), '2. Apoio frontal (corte lateral)', 'Y (mm)')
nota(ax, 'folga 0,8', (3.4, 99), (7, 100.9)); nota(ax, 'ressalto 3 mm', (5, 95.5), (9, 92)); nota(ax, 'chanfro 45°', (4.5, 92.6), (8, 88.5))
# 3. barril da dobradica
ax = axs[1][0]
desenhar(ax, [(bandeja(), AZUL_CL), (tampa(1), TAMPA), (tampa_tecnica(), CINZA)], 'x', a + 50)
estilo(ax, ((76, 106), (86, 101.5)), '3. Barril da dobradiça (corte lateral)', 'Y (mm)')
nota(ax, 'barril Ø 8\nfuro Ø 3,1', (97.5, 93.5), (99.5, 88.5)); nota(ax, 'eixo 4 mm\nabaixo do topo', (94.5, 96), (84, 89))
nota(ax, 'tampa técnica', (102, 99), (100, 94.5))
# 4. olhal, pino e presilha
ax = axs[1][1]
o0, o1 = olhais_x(i)[0]
desenhar(ax, [(bandeja(), AZUL_CL), (tampa(1), TAMPA), (tampa_tecnica(), CINZA),
              (presilha(o0, o1), VERDE), (pinos_dobradica(i), VERM)], 'x', (o0 + o1) / 2)
estilo(ax, ((78, 106), (82, 101.5)), '4. Olhal com pino de aço e presilha', 'Y (mm)')
nota(ax, 'pino de aço Ø 3', (94.5, 96), (99.5, 90.5)); nota(ax, 'presilha', (96, 99), (100.5, 95.5))
nota(ax, 'parafuso M2 × 8', (89.5, 93), (80, 86)); nota(ax, 'rasgo em U', (94.5, 94.5), (99.5, 87))
fig.suptitle('Zelo+ — Tampa do compartimento: folgas, apoios em três lados e dobradiça com pinos de aço',
             color='#1e3a8a', fontsize=13, fontweight='bold')
fig.text(0.5, 0.01, 'Cortes do modelo 3D (cad/gabinete.py). Azul escuro: corpo. Azul claro: bandeja. Preto: tampa. Cinza: tampa técnica. Verde: presilha. Vermelho: pino.',
         ha='center', fontsize=8, color='#374151')
fig.tight_layout(rect=(0, 0.03, 1, 0.96))
fig.savefig(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'docs', 'tampa-dobradica.png'), dpi=150)
print('ok')
