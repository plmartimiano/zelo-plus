# Exporta a montagem colorida (uma malha por peca, com nome) em GLB e OBJ, para
# visualizadores e programas como o Meshy. Nao altera as pecas de impressao.
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gabinete import *
CORES = {"corpo": (29, 78, 216), "bandeja": (147, 197, 253), "fundo": (100, 116, 139), "tampa": (31, 41, 55),
         "tampa tecnica": (75, 85, 99), "trava": (209, 213, 219), "alavanca": (71, 85, 105), "braco": (167, 139, 250)}
def malha(m, cor):
    mesh = m.to_mesh()
    t = trimesh.Trimesh(vertices=np.array(mesh.vert_properties)[:, :3], faces=np.array(mesh.tri_verts))
    t.visual = trimesh.visual.TextureVisuals(material=trimesh.visual.material.PBRMaterial(baseColorFactor=[*cor, 255], metallicFactor=0.0, roughnessFactor=0.8))
    return t
cena = trimesh.Scene()
cena.add_geometry(malha(corpo(), CORES["corpo"]), node_name="corpo")
cena.add_geometry(malha(bandeja(), CORES["bandeja"]), node_name="bandeja")
cena.add_geometry(malha(fundo(), CORES["fundo"]), node_name="fundo")
cena.add_geometry(malha(tampa_tecnica(), CORES["tampa tecnica"]), node_name="tampa_tecnica")
cena.add_geometry(malha(trava_botao() + trava_lingueta(), CORES["trava"]), node_name="trava_moeda")
for i in range(3):
    cena.add_geometry(malha(tampa(i), CORES["tampa"]), node_name=f"tampa_{i+1}")
    cena.add_geometry(malha(pinos_dobradica(i), (148, 163, 184)), node_name=f"pinos_dobradica_{i+1}")
    for k, (o0, o1) in enumerate(olhais_x(i)):
        cena.add_geometry(malha(presilha(o0, o1), CORES["trava"]), node_name=f"presilha_{i+1}{'ab'[k]}")
    al, br = acionamento(i)
    cena.add_geometry(malha(al, CORES["alavanca"]), node_name=f"alavanca_{i+1}")
    cena.add_geometry(malha(br, CORES["braco"]), node_name=f"braco_servo_{i+1}")
for nome, m in componentes().items():
    cena.add_geometry(malha(m, (234, 179, 8)), node_name="componente_" + nome.replace(" ", "_").replace("+", "e"))
cena.export(os.path.join(SAIDA, "montagem.glb"))
cena.export(os.path.join(SAIDA, "montagem.obj"))
print("ok")
