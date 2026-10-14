"""Gera o manual do usuario do Zelo+ (manual.html) a partir das ilustracoes."""
from diagramas import (icone, mapa_mental, fluxo_dose, linha_tempo, fluxo_horizontal,
                       arvore_emergencia, gestos_botao, AZUL, VERDE, ROXO, LARANJA, VERMELHO, ESCURO)
from produto_svg import produto

CSS = """
@page { size: A4; margin: 0; }
* { box-sizing: border-box; }
html { -webkit-print-color-adjust: exact; print-color-adjust: exact; }
body { margin: 0; font-family: 'Helvetica Neue', Helvetica, Arial, sans-serif; color: #1f2937; font-size: 10.6pt; line-height: 1.42; background: #fff; }
.pagina { width: 210mm; height: 297mm; padding: 15mm 15mm 16mm; position: relative; page-break-after: always; overflow: hidden; }
.pagina:last-child { page-break-after: auto; }
.rodape { position: absolute; bottom: 7mm; left: 15mm; right: 15mm; display: flex; justify-content: space-between; font-size: 8pt; color: #94a3b8; border-top: 1px solid #e2e8f0; padding-top: 2mm; }
.faixa { display: flex; align-items: center; gap: 4mm; margin: 0 0 5mm; }
.faixa .n { background: var(--c, #1e3a8a); color: #fff; font-weight: 700; font-size: 15pt; width: 12mm; height: 12mm; border-radius: 3.5mm; display: flex; align-items: center; justify-content: center; flex: none; }
.faixa h1 { margin: 0; font-size: 20pt; color: var(--c, #1e3a8a); line-height: 1.1; }
.faixa p { margin: 1mm 0 0; color: #64748b; font-size: 10pt; }
h2 { font-size: 13pt; color: #1e3a8a; margin: 5mm 0 2.5mm; display: flex; align-items: center; gap: 2mm; }
h3 { font-size: 11pt; margin: 3mm 0 1.5mm; color: #111827; }
p { margin: 0 0 2.5mm; }
.ic { vertical-align: -3px; flex: none; }
.duas { display: grid; grid-template-columns: 1fr 1fr; gap: 6mm; }
.lado { display: grid; grid-template-columns: 58mm 1fr; gap: 7mm; align-items: start; }
.lado.inv { grid-template-columns: 1fr 58mm; }
.celular { width: 58mm; background: #0f172a; border-radius: 7mm; padding: 3mm 2mm 4mm; box-shadow: 0 2mm 5mm rgba(15,23,42,.25); position: relative; }
.celular::before { content: ''; display: block; width: 14mm; height: 1.4mm; background: #334155; border-radius: 1mm; margin: 0 auto 2mm; }
.celular img { width: 100%; display: block; border-radius: 3.5mm; }
.celular.p { width: 50mm; }
.legenda { font-size: 8.6pt; color: #64748b; text-align: center; margin-top: 2mm; }
.visor { background: #0b0f17; border-radius: 2.4mm; padding: 1.6mm; display: inline-block; box-shadow: 0 1mm 2mm rgba(0,0,0,.2); }
.visor img { display: block; width: 100%; border-radius: 1mm; }
.passos { counter-reset: p; list-style: none; padding: 0; margin: 0; }
.passos > li { counter-increment: p; position: relative; padding: 0 0 3.2mm 10mm; }
.passos > li::before { content: counter(p); position: absolute; left: 0; top: -.3mm; width: 6.6mm; height: 6.6mm; border-radius: 50%; background: var(--c, #2563eb); color: #fff; font-weight: 700; font-size: 9.5pt; display: flex; align-items: center; justify-content: center; }
.passos > li b:first-child { color: #111827; }
.nota { border-radius: 3mm; padding: 3mm 4mm 3mm 12mm; margin: 3mm 0; position: relative; font-size: 9.8pt; }
.nota .ic { position: absolute; left: 3.5mm; top: 3.3mm; }
.nota.dica { background: #eff6ff; border: 1.4px solid #93c5fd; }
.nota.atencao { background: #fff7ed; border: 1.4px solid #fdba74; }
.nota.perigo { background: #fef2f2; border: 1.4px solid #fca5a5; }
.nota.ok { background: #f0fdf4; border: 1.4px solid #86efac; }
table { width: 100%; border-collapse: collapse; font-size: 9.5pt; }
th { background: #1e3a8a; color: #fff; text-align: left; padding: 2mm 2.5mm; font-weight: 600; }
td { border-bottom: 1px solid #e2e8f0; padding: 1.7mm 2.5mm; vertical-align: top; }
tr:nth-child(even) td { background: #f8fafc; }
.fluxo { display: flex; align-items: stretch; gap: 1.5mm; margin: 3mm 0; }
.fluxo-passo { flex: 1; border: 2px solid; border-radius: 3.5mm; padding: 4mm 2.5mm 3mm; text-align: center; position: relative; display: flex; flex-direction: column; align-items: center; gap: 1.2mm; background: #fff; font-size: 9pt; }
.fluxo-passo b { font-size: 9.8pt; }
.fluxo-passo span { color: #475569; }
.fluxo-num { position: absolute; top: -3.2mm; left: 50%; transform: translateX(-50%); color: #fff; width: 6.4mm; height: 6.4mm; border-radius: 50%; font-weight: 700; font-size: 9pt; display: flex; align-items: center; justify-content: center; }
.fluxo-seta { display: flex; align-items: center; font-size: 14pt; }
.chips { display: flex; flex-wrap: wrap; gap: 2mm; margin: 2mm 0; }
.chip { border-radius: 10mm; padding: 1mm 3mm; font-size: 9pt; font-weight: 600; color: #fff; }
.cartao { border: 1.4px solid #e2e8f0; border-radius: 3.5mm; padding: 3.5mm 4mm; background: #fff; }
.grade-visor { display: grid; grid-template-columns: 1fr 1fr; gap: 3.2mm 6mm; }
.grade-visor > div { display: grid; grid-template-columns: 44mm 1fr; gap: 3mm; align-items: center; font-size: 9.2pt; }
.grade-visor b { display: block; font-size: 9.6pt; color: #111827; }
.marca { position: absolute; width: 6.4mm; height: 6.4mm; border-radius: 50%; background: #f59e0b; color: #fff; font-weight: 700; font-size: 9pt; display: flex; align-items: center; justify-content: center; border: .6mm solid #fff; box-shadow: 0 .5mm 1.5mm rgba(0,0,0,.3); }
.chat { background: #dbeafe; border-radius: 4mm; padding: 4mm; display: flex; flex-direction: column; gap: 2.5mm; }
.bolha { background: #fff; border-radius: 3mm 3mm 3mm .8mm; padding: 2.4mm 3mm; font-size: 9.2pt; box-shadow: 0 .4mm 1mm rgba(0,0,0,.12); max-width: 92%; }
.bolha small { display: block; color: #64748b; font-size: 7.8pt; margin-top: .8mm; }
.bolha .q { font-weight: 700; color: #1e3a8a; font-size: 8.4pt; margin-bottom: .5mm; }
.lista-check { list-style: none; padding: 0; margin: 0; }
.lista-check li { padding: 1.4mm 0 1.4mm 8mm; position: relative; border-bottom: 1px dashed #e2e8f0; }
.lista-check li::before { content: ''; position: absolute; left: 0; top: 1.9mm; width: 4mm; height: 4mm; border: 1.6px solid #64748b; border-radius: 1mm; }
.cor { display: inline-block; width: 3.2mm; height: 3.2mm; border-radius: 50%; vertical-align: -.4mm; margin-right: 1mm; }
.sumario { columns: 2; column-gap: 8mm; padding: 0; list-style: none; margin: 0; }
.sumario li { display: flex; gap: 3mm; align-items: center; padding: 1.8mm 0; border-bottom: 1px solid #e2e8f0; break-inside: avoid; font-size: 10.4pt; }
.sumario .n { background: var(--c); color: #fff; font-weight: 700; width: 7mm; height: 7mm; border-radius: 2mm; display: flex; align-items: center; justify-content: center; flex: none; font-size: 9pt; }
"""

PAGINAS = []
SECOES = []


def pagina(conteudo, classe=''):
    PAGINAS.append((conteudo, classe))


def faixa(n, titulo, sub, cor=ESCURO):
    SECOES.append((n, titulo, cor))
    return (f'<div class="faixa" style="--c:{cor}"><div class="n">{n}</div><div><h1>{titulo}</h1>'
            f'<p>{sub}</p></div></div>')


def celular(img, legenda='', classe=''):
    leg = f'<div class="legenda">{legenda}</div>' if legenda else ''
    return f'<div><div class="celular {classe}"><img src="img/tela-{img}.png"></div>{leg}</div>'


def visor(nome, largura='100%'):
    return f'<div class="visor" style="width:{largura}"><img src="img/visor-{nome}.png"></div>'


def nota(tipo, texto):
    ic = {'dica': ('info', '#2563eb'), 'atencao': ('alerta', '#ea580c'), 'perigo': ('alerta', '#dc2626'), 'ok': ('ok', '#16a34a')}[tipo]
    return f'<div class="nota {tipo}">{icone(ic[0], ic[1], 17)}{texto}</div>'


# ---------------------------------------------------------------- capa
pagina(f"""
<div style="position:absolute;inset:0;background:linear-gradient(160deg,#1e3a8a 0%,#2348c9 55%,#3b82f6 100%)"></div>
<div style="position:absolute;inset:0;background:radial-gradient(circle at 80% 15%,rgba(255,255,255,.18),transparent 45%)"></div>
<div style="position:relative;color:#fff;padding:12mm 4mm 0">
  <div style="font-size:12pt;letter-spacing:.3em;opacity:.8">DISPENSER INTELIGENTE DE MEDICAMENTOS</div>
  <div style="font-size:64pt;font-weight:800;line-height:1;margin-top:6mm">Zelo+</div>
  <div style="font-size:26pt;font-weight:300;margin-top:4mm">Manual do Usuário</div>
  <div style="font-size:13pt;opacity:.85;margin-top:2mm">Guia ilustrado para o cuidador</div>
</div>
<div style="position:relative;margin:12mm auto 0;width:140mm;background:#fff;border-radius:8mm;padding:5mm 4mm 1mm;box-shadow:0 4mm 10mm rgba(0,0,0,.25)">
  {produto(callouts=False, largura='100%')}
</div>
<div style="position:relative;display:grid;grid-template-columns:repeat(4,1fr);gap:4mm;margin-top:10mm;color:#fff;text-align:center;font-size:10pt">
  <div>{icone('sino', '#fff', 30)}<br><b>Lembra</b><br>na hora certa</div>
  <div>{icone('porta', '#fff', 30)}<br><b>Libera</b><br>só o remédio da hora</div>
  <div>{icone('enviar', '#fff', 30)}<br><b>Avisa</b><br>o cuidador e a família</div>
  <div>{icone('grafico', '#fff', 30)}<br><b>Registra</b><br>cada dose tomada</div>
</div>
<div style="position:absolute;bottom:14mm;left:15mm;right:15mm;color:#fff;display:flex;justify-content:space-between;font-size:10pt;opacity:.9">
  <span>Leia este manual antes de usar o dispenser.</span><span>Outubro de 2026</span>
</div>
""", 'capa')

# ---------------------------------------------------------------- boas-vindas e sumario
pagina_sumario_idx = len(PAGINAS)
pagina('')  # preenchida no fim, quando todas as secoes forem conhecidas

# ---------------------------------------------------------------- 1 antes de comecar
pagina(f"""
{faixa(1, 'Antes de começar', 'O que acompanha o produto, o que é preciso ter e os cuidados de segurança.')}
<div class="duas">
  <div class="cartao">
    <h3>{icone('caixa', AZUL)} O que acompanha o produto</h3>
    <ul class="lista-check" style="margin-top:2mm">
      <li>Dispenser Zelo+ com bateria interna recarregável</li>
      <li>Fonte de alimentação 5 V com cabo USB-C</li>
      <li>Etiquetas coloridas <b>1</b>, <b>2</b> e <b>3</b> já aplicadas nas tampas</li>
      <li>Este manual do usuário</li>
    </ul>
  </div>
  <div class="cartao">
    <h3>{icone('celular', AZUL)} O que é preciso ter</h3>
    <ul class="lista-check" style="margin-top:2mm">
      <li>Celular com navegador de internet (qualquer marca)</li>
      <li>Rede Wi-Fi de <b>2,4 GHz</b> com a senha em mãos <span style="color:#64748b">(opcional: o dispenser também funciona sem internet)</span></li>
      <li>Aplicativo <b>Telegram</b> no celular de quem vai receber avisos <span style="color:#64748b">(opcional)</span></li>
      <li>Conta Google, para a leitura da caixa por foto <span style="color:#64748b">(opcional)</span></li>
    </ul>
  </div>
</div>

<h2>{icone('pessoas', ESCURO, 20)} Quem faz o quê</h2>
<div class="duas">
  <div class="cartao" style="border-color:#93c5fd;background:#f8fbff">
    <b style="color:#1e3a8a">Paciente</b>
    <p style="margin:1.5mm 0 0">Ao ouvir o alarme, vai até o dispenser e <b>aperta o botão</b>. A porta certa abre sozinha; basta retirar o remédio. Nada mais é exigido do paciente.</p>
  </div>
  <div class="cartao" style="border-color:#86efac;background:#f9fffb">
    <b style="color:#15803d">Cuidador</b>
    <p style="margin:1.5mm 0 0">Instala o dispenser, cadastra remédios, horários e contatos pelo celular, abastece os compartimentos e acompanha o histórico e os avisos.</p>
  </div>
</div>

<h2>{icone('escudo', VERMELHO, 20)} Cuidados de segurança</h2>
<table>
  <tr><th style="width:9mm"></th><th>Cuidado</th><th>Por quê</th></tr>
  <tr><td>{icone('pilula', ESCURO)}</td><td>Coloque <b>somente comprimidos e cápsulas</b> secos, na embalagem original fracionada ou soltos.</td><td>Líquidos, pomadas e sachês abertos podem vazar e danificar o equipamento.</td></tr>
  <tr><td>{icone('lista', ESCURO)}</td><td>Siga sempre a <b>prescrição médica</b>. O dispenser não substitui a orientação do médico ou do farmacêutico.</td><td>O Zelo+ lembra e libera o que foi cadastrado; a conferência da receita é do cuidador.</td></tr>
  <tr><td>{icone('porta', ESCURO)}</td><td><b>Não force</b> as tampas com a mão quando o dispenser estiver ligado.</td><td>Cada tampa é movida por um pequeno motor; forçar pode quebrar as engrenagens.</td></tr>
  <tr><td>{icone('tomada', ESCURO)}</td><td>Use a fonte que acompanha o produto ou outra de <b>5 V e 2 A ou mais</b>, com conector USB-C.</td><td>Fontes fracas fazem o visor apagar e as portas travarem no meio do movimento.</td></tr>
  <tr><td>{icone('alerta', ESCURO)}</td><td>Mantenha em <b>local plano, seco e ventilado</b>, longe do fogão, da pia, do sol direto e do alcance de crianças.</td><td>Calor e umidade alteram os remédios e prejudicam a eletrônica.</td></tr>
  <tr><td>{icone('chave', ESCURO)}</td><td>A tampa técnica (atrás das tampas dos remédios) é de uso <b>exclusivo do cuidador</b>.</td><td>Ela dá acesso aos motores e serve para a abertura de emergência.</td></tr>
</table>
""")

# ---------------------------------------------------------------- 2 conheca
pagina(f"""
{faixa(2, 'Conheça o Zelo+', 'As partes do dispenser e para que serve cada uma.')}
<div style="width:126mm;margin:-4mm auto -2mm">{produto(callouts=True, largura='100%')}</div>
<table style="margin-top:1mm">
  <tr><th style="width:9mm">N.º</th><th style="width:48mm">Parte</th><th>Para que serve</th></tr>
  <tr><td><span class="marca" style="position:static">1</span></td><td><b>Tampas dos compartimentos 1, 2 e 3</b></td><td>Cada compartimento guarda <b>um remédio</b>, sempre o mesmo. As etiquetas <span class="cor" style="background:{AZUL}"></span>azul (1), <span class="cor" style="background:{VERDE}"></span>verde (2) e <span class="cor" style="background:{ROXO}"></span>roxa (3) são as mesmas cores usadas no visor e no celular. As tampas abrem e fecham sozinhas.</td></tr>
  <tr><td><span class="marca" style="position:static">2</span></td><td><b>Tampa técnica</b></td><td>Cobre os motores das tampas. Fica presa por ímãs e por uma trava que se gira com uma moeda. Uso exclusivo do cuidador.</td></tr>
  <tr><td><span class="marca" style="position:static">3</span></td><td><b>Botão</b></td><td>Na hora do remédio, o paciente aperta para abrir. Com a porta aberta, um novo toque fecha. Segurado por 10 s fora do horário, entra no modo do cuidador.</td></tr>
  <tr><td><span class="marca" style="position:static">4</span></td><td><b>Visor colorido</b></td><td>Mostra a hora, a situação do dia, o remédio a tomar e as instruções. A cor de fundo indica o compartimento ou o tipo de aviso.</td></tr>
  <tr><td><span class="marca" style="position:static">5</span></td><td><b>Luz de alerta</b></td><td>Pisca junto com o som do alarme, para chamar a atenção de quem ouve pouco.</td></tr>
  <tr><td><span class="marca" style="position:static">6</span></td><td><b>Entrada USB-C</b> (lateral direita)</td><td>Ligação da fonte de alimentação. Também aceita um power bank de celular em caso de falta de energia.</td></tr>
  <tr><td><span class="marca" style="position:static">7</span></td><td><b>Saída de som</b> (lateral esquerda)</td><td>Furos do alarme sonoro. Não cubra esta lateral nem encoste o dispenser na parede por este lado.</td></tr>
</table>
<div class="duas" style="margin-top:4mm">
  {nota('dica', '<b>Bateria interna:</b> se faltar energia da tomada, o dispenser continua funcionando pela bateria por várias horas, com alarmes, portas e registros. Ela recarrega sozinha quando a energia volta.')}
  {nota('atencao', '<b>Medidas:</b> 210 × 130 × 100 mm (largura × profundidade × altura). Deixe cerca de 10 cm livres acima do dispenser para as tampas abrirem.')}
</div>
""")

# ---------------------------------------------------------------- 3 mapa mental
pagina(f"""
{faixa(3, 'O que o Zelo+ faz', 'Mapa das funções: tudo o que o dispenser faz pelo paciente e pelo cuidador.')}
<div style="margin:4mm -3mm 0">{mapa_mental()}</div>
<h2>{icone('info', ESCURO, 20)} Em uma frase</h2>
<p style="font-size:11.5pt">O cuidador cadastra os remédios pelo celular e abastece os compartimentos; na hora certa o Zelo+ <b style="color:{AZUL}">lembra</b> o paciente, <b style="color:{VERDE}">libera</b> só o remédio daquela hora, <b style="color:{LARANJA}">avisa</b> o cuidador e a família se a dose for esquecida e <b style="color:{ROXO}">registra</b> tudo para consulta.</p>
<div class="duas" style="margin-top:4mm">
  <div class="cartao"><h3 style="margin-top:0">Até 3 remédios</h3><p style="margin:0">Um por compartimento, cada um com até 6 horários por dia. Remédios do mesmo horário abrem juntos.</p></div>
  <div class="cartao"><h3 style="margin-top:0">Até 6 pessoas avisadas</h3><p style="margin:0">O cuidador e até 5 familiares recebem mensagens pelo Telegram.</p></div>
</div>
""")

# ---------------------------------------------------------------- 4 dia da dose
pagina(f"""
{faixa(4, 'Como funciona na hora do remédio', 'O que acontece desde o alarme até o registro da dose.', AZUL)}
<div style="margin:0 -2mm">{fluxo_dose()}</div>
<h2>{icone('relogio', AZUL, 20)} Linha do tempo do alarme</h2>
<div style="margin:0 -2mm">{linha_tempo()}</div>
<h2>{icone('porta', VERDE, 20)} Quando o paciente aperta o botão</h2>
<div style="display:grid;grid-template-columns:1fr 1fr 1fr;gap:4mm;align-items:start">
  <div>{visor('hora-c1')}<p class="legenda">Alarme: o fundo tem a cor do compartimento e mostra o nome do remédio.</p></div>
  <div>{visor('retire')}<p class="legenda">Porta aberta: o visor diz qual compartimento usar e conta o tempo.</p></div>
  <div>{visor('fechando')}<p class="legenda">Depois de 3 minutos, ou com um toque no botão, a porta fecha.</p></div>
</div>
{nota('dica', 'Se dois remédios têm o mesmo horário, as duas portas abrem e o visor alterna entre os nomes e as cores, a cada 2 segundos. Nos primeiros 7 segundos com a porta aberta o botão não fecha a porta, para evitar fechamento por toque duplo.')}
""")

# ---------------------------------------------------------------- 5 visor
pagina(f"""
{faixa(5, 'O que o visor mostra', 'Cada tela, o que significa e o que fazer.', ESCURO)}
<h2>{icone('relogio', ESCURO, 20)} No dia a dia</h2>
<div class="grade-visor">
  <div>{visor('em-dia')}<span><b>Medicação em dia</b>Hora atual. Nenhuma dose esperando.</span></div>
  <div>{visor('pendente')}<span><b>Dose pendente!</b>Uma dose passou sem ser retirada. O botão ainda abre o compartimento.</span></div>
  <div>{visor('sem-remedio')}<span><b>Sem remédio</b>Nenhum remédio cadastrado. Faça o cadastro pelo celular.</span></div>
  <div>{visor('acerte-hora')}<span><b>Acerte a hora</b>O relógio está sem hora: os alarmes não tocam. Abra a página no celular.</span></div>
</div>
<h2>{icone('sino', AZUL, 20)} Hora do remédio</h2>
<div class="grade-visor">
  <div>{visor('hora-c1')}<span><b>Compartimento 1 (azul)</b>Aperte o botão para abrir.</span></div>
  <div>{visor('hora-c2')}<span><b>Compartimento 2 (verde)</b>Aperte o botão para abrir.</span></div>
  <div>{visor('hora-c3')}<span><b>Compartimento 3 (roxo)</b>Aperte o botão para abrir.</span></div>
  <div>{visor('retire')}<span><b>Retire o remédio</b>Porta aberta e tempo até fechar.</span></div>
  <div>{visor('avisando')}<span><b>Avisando</b>A dose foi esquecida e as mensagens estão sendo enviadas.</span></div>
  <div>{visor('fechando')}<span><b>Fechando a porta</b>Aguarde o fim do movimento.</span></div>
</div>
<h2>{icone('wifi', ESCURO, 20)} Instalação, rede e abastecimento</h2>
<div class="grade-visor">
  <div>{visor('iniciando')}<span><b>Iniciando</b>O dispenser acabou de ser ligado.</span></div>
  <div>{visor('config')}<span><b>Conecte no Wi-Fi</b>Primeira instalação: conecte o celular à rede indicada.</span></div>
  <div>{visor('endereco')}<span><b>Endereço da página</b>Digite este número no navegador do celular.</span></div>
  <div>{visor('rede-propria')}<span><b>Rede própria</b>Sem internet: o dispenser criou a própria rede Wi-Fi.</span></div>
  <div>{visor('abastecer')}<span><b>Abastecer</b>Compartimento aberto para colocar o remédio indicado.</span></div>
  <div>{visor('lendo')}<span><b>Lendo a caixa</b>A foto da caixa do remédio está sendo analisada.</span></div>
</div>
""")

# ---------------------------------------------------------------- 6 instalacao 1
pagina(f"""
{faixa(6, 'Primeira instalação', 'Ligar o dispenser e conectá-lo ao Wi-Fi da casa. Feito uma única vez.', VERDE)}
{fluxo_horizontal([('tomada', 'Ligar', 'Fonte na tomada e cabo no USB-C'), ('wifi', 'Conectar', 'Celular na rede ZeloPlus-Config'), ('lista', 'Escolher a rede', 'Rede da casa e senha'), ('celular', 'Abrir a página', 'Endereço mostrado no visor')], VERDE)}
<div class="lado" style="margin-top:5mm">
  {celular('wifi', 'Página de conexão ao Wi-Fi')}
  <div>
    <ol class="passos" style="--c:{VERDE}">
      <li><b>Escolha o local.</b> Superfície plana, perto de uma tomada e onde o paciente passe todos os dias (por exemplo, a mesa da cozinha). Deixe a lateral esquerda livre, por causa do som.</li>
      <li><b>Ligue a fonte</b> na tomada e o cabo na entrada USB-C da lateral direita. O visor mostra:<div style="margin:2mm 0 0">{visor('iniciando', '62mm')}</div></li>
      <li><b>Aguarde o visor pedir a conexão.</b> Na primeira vez ele mostra:<div style="margin:2mm 0 0">{visor('config', '62mm')}</div></li>
      <li><b>No celular</b>, abra <i>Configurações &gt; Wi-Fi</i> e conecte-se à rede <b>ZeloPlus-Config</b>. A página de conexão abre sozinha; se não abrir, digite <b>192.168.4.1</b> no navegador.</li>
      <li><b>Escolha a rede da casa</b> na lista, digite a senha e toque em <b>Conectar</b>. Se a rede não aparecer, use <i>Atualizar lista</i> ou digite o nome dela.</li>
    </ol>
  </div>
</div>
{nota('atencao', '<b>Somente redes de 2,4 GHz.</b> Redes de 5 GHz não aparecem na lista. Muitos roteadores têm as duas: escolha a que não termina em "5G".')}
""")

# ---------------------------------------------------------------- 7 instalacao 2
pagina(f"""
{faixa(7, 'Abrir a página do dispenser', 'O "aplicativo" do Zelo+ é uma página aberta no navegador do celular. Não é preciso instalar nada.', VERDE)}
<div class="lado inv">
  <div>
    <ol class="passos" style="--c:{VERDE}" start="6">
      <li><b>Conexão confirmada.</b> O celular mostra "Conectado com sucesso!" e o dispenser reinicia sozinho.</li>
      <li><b>Volte o celular para a rede da casa.</b> O visor passa a mostrar o endereço da página:<div style="margin:2mm 0 0">{visor('endereco', '62mm')}</div></li>
      <li><b>Digite o endereço no navegador</b> do celular (por exemplo, <b>192.168.0.42</b>), exatamente como aparece no visor.</li>
      <li><b>Salve um atalho.</b> No menu do navegador, toque em <i>Adicionar à tela inicial</i>. A página passa a abrir como um aplicativo.</li>
      <li><b>Comece o cadastro.</b> Na primeira vez, a página já abre no cadastro do paciente e do compartimento 1.</li>
    </ol>
    {nota('dica', 'O endereço aparece no visor sempre que o dispenser é ligado. Anote-o na última página deste manual.')}
    <h2>{icone('wifi', LARANJA, 20)} Sem internet em casa?</h2>
    <p>Na página de conexão, toque em <b>Usar sem internet</b>. O dispenser cria a própria rede, <b>ZeloPlus</b> (senha <b>zelo1234</b>), e acerta o relógio pela hora do celular. Alarmes, portas e histórico funcionam; só os avisos pelo Telegram ficam desligados.</p>
    <div style="width:62mm">{visor('rede-propria')}</div>
  </div>
  {celular('primeiro-uso', 'Primeiro acesso: cadastro do paciente')}
</div>
""")

# ---------------------------------------------------------------- 8 mapa da pagina
pontos = ''.join(f'<span class="marca" style="left:-3mm;top:calc({y}% - 3.2mm)">{n}</span>' for n, y in [(1, 6.9), (2, 16.4), (3, 23.6), (4, 62.4), (5, 95.2)])
pagina(f"""
{faixa(8, 'Conheça a página', 'Tudo fica em uma única página, de cima para baixo. Toque em um item para abrir ou fechar.', AZUL)}
<div class="lado" style="grid-template-columns:50mm 1fr">
  <div><div class="celular" style="width:50mm"><div style="position:relative"><img src="img/tela-mapa.png">{pontos}</div></div><div class="legenda">Parte de cima da página, inteira</div></div>
  <div>
    <table>
      <tr><th style="width:9mm">N.º</th><th>Seção</th><th>O que se faz nela</th></tr>
      <tr><td><span class="marca" style="position:static">1</span></td><td><b>Avisos</b></td><td>Faixas amarelas no topo indicam algo pendente, como um contato sem Telegram ou a falta de internet.</td></tr>
      <tr><td><span class="marca" style="position:static">2</span></td><td><b>Paciente</b></td><td>Nome do paciente, usado nas mensagens.</td></tr>
      <tr><td><span class="marca" style="position:static">3</span></td><td><b>Remédios</b></td><td>Os três compartimentos, com nome do remédio, horários e foto da caixa.</td></tr>
      <tr><td><span class="marca" style="position:static">4</span></td><td><b>Contatos</b></td><td>Cuidador (obrigatório) e até 5 familiares.</td></tr>
      <tr><td><span class="marca" style="position:static">5</span></td><td><b>Salvar alterações</b></td><td>Grava remédios, horários e contatos.</td></tr>
    </table>
    <h3 style="margin-top:5mm">Mais abaixo, na mesma página</h3>
    <div style="display:flex;flex-direction:column;gap:2mm">
      <div class="cartao" style="padding:2.5mm 3.5mm;border-left:4px solid #f59e0b">{icone('caixa', '#f59e0b')} <b>Repor remédio</b> — abre um compartimento para completar.</div>
      <div class="cartao" style="padding:2.5mm 3.5mm;border-left:4px solid {ROXO}">{icone('grafico', ROXO)} <b>Histórico de doses</b> — adesão e lista das últimas doses.</div>
      <div class="cartao" style="padding:2.5mm 3.5mm;border-left:4px solid {ESCURO}">{icone('wifi', ESCURO)} <b>Trocar rede Wi-Fi</b> — volta à página de conexão.</div>
      <div class="cartao" style="padding:2.5mm 3.5mm;border-left:4px solid {AZUL}">{icone('enviar', AZUL)} <b>Avisos pelo Telegram</b> — token do bot e IDs dos contatos.</div>
      <div class="cartao" style="padding:2.5mm 3.5mm;border-left:4px solid {VERDE}">{icone('camera', VERDE)} <b>Leitura da caixa por foto</b> — chave da inteligência artificial.</div>
    </div>
    {nota('atencao', 'Depois de mudar qualquer campo, toque em <b>Salvar alterações</b>. Sair da página sem salvar descarta as mudanças.')}
  </div>
</div>
""")

# ---------------------------------------------------------------- 9 remedios
pagina(f"""
{faixa(9, 'Cadastrar os remédios', 'Um remédio por compartimento, com até 6 horários por dia.', AZUL)}
<div class="lado">
  {celular('remedio', 'Compartimento 1 aberto para edição')}
  <div>
    <ol class="passos">
      <li><b>Toque no compartimento</b> (1 azul, 2 verde ou 3 roxo) para abrir o cartão.</li>
      <li><b>Digite o nome do remédio</b> com a dosagem, como está na caixa (ex.: <i>Losartana 50 mg</i>). Ou toque em <b>Foto da caixa do remédio</b> para preencher pela câmera.</li>
      <li><b>Informe os horários.</b> Use <b>+ Adicionar horário</b> para mais de um por dia. Para tirar um horário, apague o campo.</li>
      <li><b>Confira o quadro tracejado</b>: ele diz em qual compartimento colocar o remédio.</li>
      <li>Toque em <b>Salvar alterações</b>. O compartimento <b>abre sozinho</b> para ser abastecido:<div style="margin:2mm 0">{visor('abastecer', '62mm')}</div></li>
      <li><b>Coloque os comprimidos</b> e aperte o botão do dispenser para fechar (ou aguarde 3 minutos).</li>
    </ol>
  </div>
</div>
<div class="duas" style="margin-top:2mm">
  {nota('atencao', '<b>Trocar o remédio de um compartimento:</b> a página pede confirmação e lembra de retirar todos os comprimidos antigos antes de colocar o novo.')}
  {nota('dica', '<b>Esvaziar:</b> o link <i>Esvaziar o compartimento</i> apaga o cadastro e oferece abrir a porta para retirar as sobras.')}
</div>
<div style="display:grid;grid-template-columns:62mm 1fr;gap:6mm;align-items:center;margin-top:2mm">
  <div>{produto(aberta=0, callouts=False, visor=None, largura='100%')}</div>
  <div class="cartao"><b>Uma cor, um remédio.</b> O compartimento 1 é sempre azul, o 2 verde e o 3 roxo: no visor, na página e na etiqueta da tampa. Antes de abastecer, confira se a cor da tampa aberta é a mesma do cartão na página.</div>
</div>
""")

# ---------------------------------------------------------------- 10 foto da caixa
pagina(f"""
{faixa(10, 'Leitura da caixa por foto', 'A câmera do celular lê o nome e a dosagem impressos na embalagem.', VERDE)}
<div class="lado inv">
  <div>
    {fluxo_horizontal([('camera', 'Fotografar', 'Frente da caixa, bem iluminada'), ('engrenagem', 'Analisar', 'Visor: "Lendo a caixa..."'), ('ok', 'Confirmar', 'O nome só entra com o seu OK')], VERDE)}
    <h2>{icone('chave', VERDE, 20)} Ativar a leitura (uma vez só)</h2>
    <ol class="passos" style="--c:{VERDE}">
      <li>No computador ou no celular, acesse <b>aistudio.google.com</b> com uma conta Google pessoal.</li>
      <li>Toque em <b>Get API key</b> e crie uma chave em um projeto novo. O uso gratuito não pede cartão.</li>
      <li>Na página do dispenser, abra <b>Leitura da caixa por foto</b>, cole a chave e toque em <b>Salvar</b>. O campo <i>Modelo</i> já vem preenchido.</li>
    </ol>
    <h2>{icone('camera', VERDE, 20)} Usar no cadastro</h2>
    <ol class="passos" style="--c:{VERDE}">
      <li>Abra o compartimento na página e toque em <b>Foto da caixa do remédio</b>.</li>
      <li>Fotografe a frente da embalagem, com o nome legível e sem reflexo.</li>
      <li>Confira o nome sugerido e confirme. Depois, toque em <b>Salvar alterações</b>.</li>
    </ol>
    {nota('atencao', 'A leitura é uma ajuda para digitar. <b>Confira sempre</b> o nome e a dosagem com a receita antes de salvar.')}
    {nota('dica', 'Se aparecer "chave recusada", crie a chave com uma conta Google pessoal (Gmail), em um projeto novo do AI Studio, e cole-a de novo.')}
  </div>
  {celular('ia', 'Seção da leitura por foto')}
</div>
""")

# ---------------------------------------------------------------- 11 contatos e telegram
pagina(f"""
{faixa(11, 'Contatos e avisos pelo Telegram', 'Quem recebe as mensagens quando uma dose é esquecida.', LARANJA)}
{fluxo_horizontal([('enviar', 'Criar o bot', 'No Telegram, @BotFather &gt; /newbot'), ('chave', 'Colar o token', 'Seção Avisos pelo Telegram'), ('pessoas', 'Cada pessoa: Iniciar', 'Procura o bot e toca em Iniciar'), ('lista', 'Buscar IDs', 'Copiar o ID de cada pessoa'), ('ok', 'Salvar', 'Enviar mensagem de teste')], LARANJA)}
<div style="display:grid;grid-template-columns:50mm 50mm 1fr;gap:5mm;margin-top:4mm;align-items:start">
  {celular('contatos', 'Contatos', 'p')}
  {celular('telegram', 'Avisos pelo Telegram', 'p')}
  <div>
    <h3 style="margin-top:0">1. Cadastre os contatos</h3>
    <p>Em <b>Contatos</b>, preencha nome e celular do <b>cuidador</b> (obrigatório). Use <b>+ Cadastrar familiar</b> para até 5 familiares. Toque em <b>Salvar alterações</b>.</p>
    <h3>2. Crie o bot (uma vez só)</h3>
    <p>No Telegram, abra o <b>@BotFather</b>, envie <b>/newbot</b>, escolha um nome e um usuário terminado em <i>bot</i>. Copie o <b>token</b> e cole na seção <b>Avisos pelo Telegram</b>.</p>
    <h3>3. Ligue cada pessoa ao bot</h3>
    <p>Cada pessoa procura o bot no Telegram e toca em <b>Iniciar</b>. Depois, toque em <b>Buscar IDs do Telegram</b> e copie o ID de cada uma para o campo com o nome dela. Toque em <b>Salvar</b>.</p>
    {nota('ok', 'Um <b>✓</b> verde ao lado do nome indica que a pessoa já recebe avisos. Enquanto faltar alguém, uma faixa amarela aparece no topo da página.')}
  </div>
</div>
""")

# ---------------------------------------------------------------- 12 mensagens
pagina(f"""
{faixa(12, 'Quem recebe cada aviso', 'As mensagens enviadas pelo Zelo+ e quando cada uma chega.', LARANJA)}
<table>
  <tr><th>Situação</th><th style="width:30mm">Quando</th><th style="width:22mm">Cuidador</th><th style="width:22mm">Familiares</th></tr>
  <tr><td><b>O paciente não abriu o compartimento</b></td><td>6 min após o alarme</td><td style="color:{VERDE};font-weight:700">Recebe</td><td style="color:#94a3b8">—</td></tr>
  <tr><td><b>O paciente não foi até o dispenser</b> (o alarme para)</td><td>12 min após o alarme</td><td style="color:{VERDE};font-weight:700">Recebe</td><td style="color:{VERDE};font-weight:700">Recebem</td></tr>
  <tr><td><b>O paciente chegou depois do aviso</b></td><td>Na hora em que abrir</td><td style="color:{VERDE};font-weight:700">Recebe</td><td>Se foram avisados</td></tr>
  <tr><td><b>Abertura pelo botão no modo do cuidador</b></td><td>Na hora</td><td style="color:{VERDE};font-weight:700">Recebe</td><td style="color:#94a3b8">—</td></tr>
</table>
<div style="display:grid;grid-template-columns:1fr 1fr;gap:6mm;margin-top:6mm">
  <div class="chat">
    <div style="font-size:9pt;color:#1e3a8a;font-weight:700;display:flex;align-items:center;gap:2mm">{icone('enviar', '#1e3a8a', 16)} Exemplo de conversa com o bot</div>
    <div class="bolha"><div class="q">Zelo+</div>Zelo+: Paciente “Paulo Silva” não acessou o medicamento das “12:00” horas (Metformina 850 mg).<small>12:06</small></div>
    <div class="bolha"><div class="q">Zelo+</div>Zelo+: Paulo Silva acessou o dispenser às 12:08 (dose de Metformina 850 mg das 12:00).<small>12:08</small></div>
    <div class="bolha"><div class="q">Zelo+</div>Zelo+: Paciente “Paulo Silva” não foi até o dispenser no horário das “20:00” (Losartana 50 mg).<small>20:12</small></div>
  </div>
  <div>
    <h3 style="margin-top:0">{icone('info', AZUL)} O que fazer ao receber um aviso</h3>
    <ol class="passos">
      <li><b>Ligue para o paciente</b> ou para quem estiver com ele e lembre do remédio.</li>
      <li>O paciente pode <b>apertar o botão</b> mesmo depois do aviso: o compartimento da dose ainda abre.</li>
      <li>Quando ele abrir, chega a <b>confirmação</b> com o horário.</li>
      <li>Sem confirmação, verifique pessoalmente.</li>
    </ol>
    {nota('dica', 'Depois de salvar, toque em <b>Enviar mensagem de teste</b> na seção do Telegram para conferir se todos recebem.')}
  </div>
</div>
{nota('atencao', 'Os avisos dependem de internet. Sem internet, o alarme, a luz e o registro continuam funcionando normalmente; somente as mensagens deixam de ser enviadas.')}
""")

# ---------------------------------------------------------------- 13 repor
pagina(f"""
{faixa(13, 'Repor um remédio', 'Abrir só o compartimento do remédio que está acabando.', '#d97706')}
<div style="display:grid;grid-template-columns:50mm 50mm 1fr;gap:5mm;align-items:start">
  {celular('repor-1', 'Escolha o remédio', 'p')}
  {celular('repor-2', 'Confirme a abertura', 'p')}
  <div>
    <ol class="passos" style="--c:#d97706">
      <li>Na página, em <b>Repor remédio</b>, toque em <b>Abrir compartimento para reposição</b>.</li>
      <li><b>Marque o remédio</b> que vai repor e toque em <b>Continuar</b>.</li>
      <li>Confira a pergunta e toque em <b>Sim, abrir agora</b>.</li>
      <li>Somente aquele compartimento abre; o visor mostra o nome do remédio:<div style="margin:2mm 0">{visor('abastecer')}</div></li>
      <li>Complete os comprimidos e <b>aperte o botão</b> para fechar. Se esquecer, a porta fecha sozinha em 3 minutos.</li>
    </ol>
  </div>
</div>
<h2>{icone('lista', '#d97706', 20)} Rotina de reposição sugerida</h2>
<div class="duas">
  <div class="cartao"><b>Uma vez por semana</b>, no mesmo dia, confira quantos comprimidos restam em cada compartimento e complete para mais 7 dias.</div>
  <div class="cartao"><b>Nunca misture</b> remédios num mesmo compartimento. Se o médico trocar um remédio, use <i>Esvaziar o compartimento</i> antes.</div>
</div>
""")

# ---------------------------------------------------------------- 14 historico
pagina(f"""
{faixa(14, 'Histórico e adesão', 'Acompanhe se as doses estão sendo tomadas no horário.', ROXO)}
<div class="lado">
  {celular('historico', 'Seção Histórico de doses')}
  <div>
    <h3 style="margin-top:0">Como ler</h3>
    <p>O número grande é a <b>adesão</b>: a parte das doses dos últimos 7 dias que foi retirada (no horário ou com atraso). Logo abaixo, a contagem por situação e a adesão de cada remédio.</p>
    <table>
      <tr><th>Situação</th><th>Significado</th></tr>
      <tr><td><span class="cor" style="background:#15803d"></span><b>Tomada no horário</b></td><td>Aberto em até 6 minutos depois do alarme.</td></tr>
      <tr><td><span class="cor" style="background:#b45309"></span><b>Tomada com atraso</b></td><td>Aberto depois de 6 minutos; mostra quanto tempo depois.</td></tr>
      <tr><td><span class="cor" style="background:#dc2626"></span><b>Sem acesso</b></td><td>Ninguém abriu o compartimento da dose.</td></tr>
      <tr><td><span class="cor" style="background:#6b7280"></span><b>Abertura manual</b></td><td>Aberto pelo cuidador no botão. Não entra na conta da adesão.</td></tr>
    </table>
    <h3>Levar ao médico</h3>
    <p>Toque em <b>Ver últimas doses</b> para a lista completa e em <b>Baixar histórico completo (planilha)</b> para salvar um arquivo que abre no Excel ou no Google Planilhas.</p>
    {nota('atencao', 'O dispenser registra a <b>abertura do compartimento</b>, não a ingestão do remédio. Sempre que possível, confirme com o paciente.')}
    {nota('dica', 'Adesão baixa ou muitos atrasos no mesmo horário podem indicar que o horário não combina com a rotina do paciente. Converse com o médico sobre um ajuste.')}
  </div>
</div>
""")

# ---------------------------------------------------------------- 15 modo do cuidador
pagina(f"""
{faixa(15, 'Modo do cuidador', 'Abrir qualquer compartimento pelo botão, sem usar o celular.', ESCURO)}
{gestos_botao()}
<div style="display:grid;grid-template-columns:1fr 1fr;gap:5mm 7mm;margin-top:5mm">
  <div><div style="display:flex;gap:3mm;align-items:center"><span class="marca" style="position:static;background:{ESCURO}">1</span><b>Segure o botão</b></div>
    <p style="margin:1.5mm 0">Fora do horário de dose, segure o botão. Depois de 2 s o visor conta o tempo que falta.</p>{visor('cuid-segure')}</div>
  <div><div style="display:flex;gap:3mm;align-items:center"><span class="marca" style="position:static;background:{ESCURO}">2</span><b>Solte ao entrar no modo</b></div>
    <p style="margin:1.5mm 0">Aos 10 s o visor mostra a primeira opção: abrir o compartimento 1.</p>{visor('cuid-opcao')}</div>
  <div><div style="display:flex;gap:3mm;align-items:center"><span class="marca" style="position:static;background:{AZUL}">3</span><b>Toques curtos trocam a opção</b></div>
    <p style="margin:1.5mm 0">C1, C2, C3 e <b>Sair</b>, nessa ordem, voltando ao início.</p>{visor('cuid-sair')}</div>
  <div><div style="display:flex;gap:3mm;align-items:center"><span class="marca" style="position:static;background:{VERDE}">4</span><b>Segure 1,5 s para confirmar</b></div>
    <p style="margin:1.5mm 0">O visor pede para soltar; ao soltar, a opção é executada.</p>{visor('cuid-solte')}</div>
  <div><div style="display:flex;gap:3mm;align-items:center"><span class="marca" style="position:static;background:{AZUL}">5</span><b>O compartimento abre</b></div>
    <p style="margin:1.5mm 0">Fecha com um toque no botão ou sozinho em 3 minutos. O cuidador recebe mensagem.</p>{visor('cuid-abrindo')}</div>
  <div style="align-self:center">
    {nota('dica', 'Sem nenhum toque por 30 segundos, o modo do cuidador se encerra sozinho e o visor volta à tela normal.')}
    {nota('atencao', 'A abertura fica no histórico como <b>Abertura manual</b>. O modo não funciona durante um alarme: nessa hora, um toque abre a dose.')}
  </div>
</div>
""")

# ---------------------------------------------------------------- 16 sem internet
pagina(f"""
{faixa(16, 'Sem internet', 'O dispenser continua cuidando do paciente quando a internet cai.', LARANJA)}
<div class="lado">
  {celular('sem-internet', 'Aviso de funcionamento sem internet')}
  <div>
    <h3 style="margin-top:0">O que continua funcionando</h3>
    <div class="chips">
      <span class="chip" style="background:{VERDE}">Alarmes</span><span class="chip" style="background:{VERDE}">Portas e botão</span>
      <span class="chip" style="background:{VERDE}">Histórico</span><span class="chip" style="background:{VERDE}">Página no celular</span>
      <span class="chip" style="background:#94a3b8">Telegram: pausado</span>
    </div>
    <h3>Como acessar a página</h3>
    <ol class="passos" style="--c:{LARANJA}">
      <li>O visor mostra a rede própria do dispenser:<div style="margin:2mm 0">{visor('rede-propria', '62mm')}</div></li>
      <li>Conecte o celular à rede <b>ZeloPlus</b>, senha <b>zelo1234</b>. A página abre sozinha (ou digite <b>192.168.4.1</b>).</li>
      <li>A faixa amarela explica o motivo. O dispenser tenta a rede da casa de novo <b>a cada 10 minutos</b>; para tentar na hora, toque em <b>Tentar conectar novamente</b>.</li>
      <li>Se a senha do Wi-Fi da casa mudou, toque em <b>Trocar rede Wi-Fi</b> e refaça a conexão.</li>
    </ol>
    <h3>Visor laranja: "Acerte a hora"</h3>
    <div style="width:62mm;margin-bottom:2mm">{visor('acerte-hora')}</div>
    <p>Sem internet e sem hora certa, os alarmes não tocam. Basta <b>abrir a página no celular</b>: a hora do celular é enviada ao dispenser automaticamente.</p>
  </div>
</div>
""")

# ---------------------------------------------------------------- 17 emergencia
pagina(f"""
{faixa(17, 'Falta de energia e emergência', 'Como abrir um compartimento em qualquer situação.', VERMELHO)}
<div style="margin:0 -2mm">{arvore_emergencia()}</div>
<h2>{icone('moeda', VERMELHO, 20)} Abertura manual pela tampa técnica (sem energia nenhuma)</h2>
<img src="img/emergencia-passos.png" style="width:100%;border:1px solid #e2e8f0;border-radius:3mm">
<ol class="passos" style="--c:{VERMELHO};margin-top:3mm">
  <li><b>Destrave a tampa técnica</b> girando a trava 1/4 de volta com uma moeda e levante-a pelo rebaixo.</li>
  <li><b>Puxe pela argola</b> o pino que liga o motor à tampa do compartimento desejado.</li>
  <li><b>Abra a tampa do compartimento</b> com a mão e retire o remédio.</li>
  <li><b>Remonte:</b> feche a tampa do compartimento, alinhe os furos, recoloque o pino, feche a tampa técnica e trave com a moeda.</li>
</ol>
{nota('perigo', 'Nunca force a tampa de um compartimento com o dispenser ligado ou sem tirar o pino: as engrenagens do motor podem quebrar. Uma abertura feita sem energia não fica registrada no histórico.')}
""")

# ---------------------------------------------------------------- 18 cuidados
pagina(f"""
{faixa(18, 'Cuidados e rotina', 'Pequenas tarefas que mantêm o dispenser confiável.', VERDE)}
<div class="duas">
  <div class="cartao">
    <h3 style="margin-top:0">{icone('lista', VERDE)} Toda semana</h3>
    <ul class="lista-check">
      <li>Completar os compartimentos (seção <i>Repor remédio</i>)</li>
      <li>Olhar o histórico e a adesão dos 7 dias</li>
      <li>Conferir se o visor mostra a hora certa</li>
      <li>Conferir a validade dos remédios guardados</li>
    </ul>
  </div>
  <div class="cartao">
    <h3 style="margin-top:0">{icone('relogio', VERDE)} Todo mês</h3>
    <ul class="lista-check">
      <li>Tocar em <i>Enviar mensagem de teste</i> (Telegram)</li>
      <li>Limpar os compartimentos (veja ao lado)</li>
      <li>Testar o modo do cuidador uma vez</li>
      <li>Baixar a planilha do histórico, se o médico pedir</li>
    </ul>
  </div>
</div>
<h2>{icone('limpar', VERDE, 20)} Limpeza</h2>
<ol class="passos" style="--c:{VERDE}">
  <li>Abra cada compartimento pela seção <i>Repor remédio</i> e retire os comprimidos.</li>
  <li>Passe um pano macio, seco ou levemente úmido. Para pó de comprimido, use um pincel seco.</li>
  <li>Espere secar totalmente antes de recolocar os remédios.</li>
</ol>
{nota('perigo', 'Não lave o dispenser, não use álcool em excesso nem produtos abrasivos e não deixe entrar água pelos furos laterais ou pela tampa técnica.')}
<h2>{icone('bateria', VERDE, 20)} Energia</h2>
<div class="duas">
  <div class="cartao">Mantenha o dispenser <b>sempre ligado na tomada</b>. A bateria interna é para emergências e recarrega sozinha.</div>
  <div class="cartao">Tenha um <b>power bank</b> de celular carregado em casa. Ele religa o dispenser pela entrada USB-C se a falta de energia for longa.</div>
</div>
<h2>{icone('caixa', VERDE, 20)} Mudar o dispenser de lugar</h2>
<p>Feche todas as tampas, desligue o cabo e transporte o dispenser na horizontal, apoiado por baixo. Ao religar, o visor mostra o endereço da página; se a rede Wi-Fi for outra, use <i>Trocar rede Wi-Fi</i>.</p>
""")

# ---------------------------------------------------------------- 19 problemas
pagina(f"""
{faixa(19, 'Solução de problemas', 'O que observar e o que fazer.', VERMELHO)}
<table>
  <tr><th style="width:52mm">O que acontece</th><th style="width:52mm">Causa provável</th><th>O que fazer</th></tr>
  <tr><td><b>O alarme não tocou</b></td><td>Remédio sem horário, hora errada ou visor laranja "Acerte a hora".</td><td>Confira os horários na página. Se o visor estiver laranja, abra a página no celular para acertar a hora.</td></tr>
  <tr><td><b>Visor apagado</b></td><td>Sem energia e bateria descarregada, ou fonte fraca.</td><td>Confira a tomada e o cabo USB-C. Use a fonte original (5 V, 2 A ou mais) ou um power bank.</td></tr>
  <tr><td><b>Uma porta não abre ou para no meio</b></td><td>Fonte fraca, objeto travando a tampa ou pino solto.</td><td>Retire o que estiver sobre a tampa e use a fonte original. Se persistir, confira o pino na tampa técnica.</td></tr>
  <tr><td><b>A página não abre no celular</b></td><td>Celular em outra rede ou endereço digitado errado.</td><td>Conecte o celular ao mesmo Wi-Fi do dispenser e digite o endereço mostrado no visor ao ligar.</td></tr>
  <tr><td><b>A rede da casa não aparece</b></td><td>Rede de 5 GHz ou roteador distante.</td><td>Use a rede de 2,4 GHz ou aproxime o dispenser do roteador. Toque em <i>Atualizar lista</i>.</td></tr>
  <tr><td><b>Faixa "Sem internet"</b></td><td>Roteador desligado, senha alterada ou queda da internet.</td><td>Aguarde a nova tentativa (10 min) ou toque em <i>Tentar conectar novamente</i>. Se a senha mudou, <i>Trocar rede Wi-Fi</i>.</td></tr>
  <tr><td><b>Mensagens do Telegram não chegam</b></td><td>Pessoa sem ID, não tocou em <i>Iniciar</i> no bot ou falta de internet.</td><td>Procure "(falta)" na seção do Telegram, peça para a pessoa tocar em <i>Iniciar</i>, busque os IDs de novo e salve.</td></tr>
  <tr><td><b>"Chave da IA recusada"</b></td><td>Chave criada em conta ou projeto sem permissão.</td><td>Crie uma chave nova no AI Studio com uma conta Google pessoal, em um projeto novo.</td></tr>
  <tr><td><b>Visor "Dose pendente!"</b></td><td>A dose não foi retirada a tempo.</td><td>O paciente ainda pode apertar o botão para abrir. Avise-o ou verifique pessoalmente.</td></tr>
  <tr><td><b>O botão não fecha a porta</b></td><td>Primeiros 7 segundos com a porta aberta.</td><td>Aguarde alguns segundos e toque de novo, ou deixe fechar sozinha.</td></tr>
  <tr><td><b>Esqueceu a senha do Wi-Fi do dispenser</b></td><td>—</td><td>A rede própria é sempre <b>ZeloPlus</b>, senha <b>zelo1234</b>; na instalação, <b>ZeloPlus-Config</b>, sem senha.</td></tr>
</table>
{nota('dica', 'Quando nada resolver: desligue o cabo por 10 segundos e ligue de novo. Cadastro, contatos e histórico ficam guardados na memória do dispenser.')}
""")

# ---------------------------------------------------------------- 20 especificacoes
pagina(f"""
{faixa(20, 'Especificações e anotações', 'Dados técnicos e espaço para as informações da sua instalação.', ESCURO)}
<div class="duas">
  <table>
    <tr><th colspan="2">Especificações</th></tr>
    <tr><td>Dimensões</td><td>210 × 130 × 100 mm</td></tr>
    <tr><td>Compartimentos</td><td>3, de 65,7 × 75,7 × 50,5 mm (cerca de 250 cm³ cada)</td></tr>
    <tr><td>Horários</td><td>Até 6 por remédio</td></tr>
    <tr><td>Contatos</td><td>Cuidador e até 5 familiares</td></tr>
    <tr><td>Histórico</td><td>Últimas 60 doses, com exportação em planilha</td></tr>
    <tr><td>Visor</td><td>Colorido, 320 × 170 pontos</td></tr>
    <tr><td>Alarme</td><td>Som e luz, 1 min tocando e 1 min em silêncio, por até 12 min</td></tr>
    <tr><td>Porta aberta</td><td>Fecha sozinha em 3 min</td></tr>
    <tr><td>Alimentação</td><td>5 V, 2 A ou mais, entrada USB-C</td></tr>
    <tr><td>Bateria interna</td><td>Recarregável, mantém o funcionamento sem energia da tomada</td></tr>
    <tr><td>Conexão</td><td>Wi-Fi 2,4 GHz, ou rede própria sem internet</td></tr>
    <tr><td>Avisos</td><td>Telegram (requer internet)</td></tr>
  </table>
  <div>
    <table>
      <tr><th colspan="2">Minha instalação</th></tr>
      <tr><td style="width:36mm">Endereço da página</td><td style="height:9mm"></td></tr>
      <tr><td>Rede Wi-Fi</td><td style="height:9mm"></td></tr>
      <tr><td>Nome do bot</td><td style="height:9mm"></td></tr>
      <tr><td>Data da instalação</td><td style="height:9mm"></td></tr>
    </table>
    <table style="margin-top:5mm">
      <tr><th style="width:12mm">N.º</th><th>Remédio</th><th style="width:30mm">Horários</th></tr>
      <tr><td><span class="cor" style="background:{AZUL}"></span>1</td><td style="height:10mm"></td><td></td></tr>
      <tr><td><span class="cor" style="background:{VERDE}"></span>2</td><td style="height:10mm"></td><td></td></tr>
      <tr><td><span class="cor" style="background:{ROXO}"></span>3</td><td style="height:10mm"></td><td></td></tr>
    </table>
  </div>
</div>
<h2>{icone('info', ESCURO, 20)} Perguntas frequentes</h2>
<div class="duas" style="font-size:9.8pt">
  <div><b>Posso colocar dois remédios no mesmo compartimento?</b><p>Não. Cada compartimento guarda um único remédio, para evitar trocas. Remédios do mesmo horário ficam em compartimentos diferentes e abrem juntos.</p>
  <b>O que acontece se faltar energia?</b><p>A bateria interna mantém tudo funcionando. Os cadastros e o histórico ficam guardados mesmo com o dispenser desligado.</p></div>
  <div><b>Preciso deixar o celular perto do dispenser?</b><p>Não. O celular só é usado para configurar e consultar. Alarme e abertura funcionam sozinhos.</p>
  <b>Mais de uma pessoa pode configurar?</b><p>Sim. Qualquer celular na mesma rede abre a página. Combine quem faz as alterações para evitar mudanças cruzadas.</p></div>
</div>
""")

# ---------------------------------------------------------------- sumario (preenchido agora)
itens = ''.join(f'<li><span class="n" style="--c:{c}">{n}</span>{t}</li>' for n, t, c in SECOES)
PAGINAS[pagina_sumario_idx] = (f"""
<div class="faixa" style="--c:{ESCURO}"><div><h1>Bem-vindo ao Zelo+</h1><p>Obrigado por escolher o Zelo+ para cuidar de quem você ama.</p></div></div>
<p style="font-size:11.5pt">O Zelo+ é um dispenser de medicamentos para uso em casa. Ele guarda até três remédios, lembra o paciente na hora certa com som, luz e mensagens no visor, abre somente o compartimento daquela dose e avisa o cuidador e a família pelo celular se a dose for esquecida.</p>
<p style="font-size:11.5pt">Este manual foi feito para o <b>cuidador</b>: a pessoa que instala, configura e acompanha o dispenser. As ilustrações mostram o produto, o visor e as telas do celular exatamente como aparecem no uso.</p>
<h2>{icone('lista', ESCURO, 20)} Conteúdo</h2>
<ul class="sumario">{itens}</ul>
{nota('dica', '<b>Como ler este manual:</b> os quadros azuis trazem dicas; os laranja, pontos de atenção; os vermelhos, situações de risco. As telas do celular mostradas são da página do próprio dispenser, com dados de exemplo.')}
<h2>{icone('ok', VERDE, 20)} Comece em 5 passos</h2>
{fluxo_horizontal([('tomada', 'Ligue', 'Fonte no USB-C'), ('wifi', 'Conecte', 'Wi-Fi pelo celular (seção 6)'), ('pilula', 'Cadastre', 'Remédios e horários (seção 9)'), ('caixa', 'Abasteça', 'Cada remédio na sua cor'), ('enviar', 'Avisos', 'Telegram (seção 11)')], VERDE)}
""", '')

# ---------------------------------------------------------------- montagem
html = [f'<!DOCTYPE html><html lang="pt-BR"><head><meta charset="utf-8"><title>Zelo+ — Manual do Usuário</title><style>{CSS}</style></head><body>']
for i, (conteudo, classe) in enumerate(PAGINAS):
    rod = '' if classe == 'capa' else f'<div class="rodape"><span>Zelo+ · Manual do Usuário</span><span>{i + 1}</span></div>'
    html.append(f'<section class="pagina {classe}">{conteudo}{rod}</section>')
html.append('</body></html>')
open('manual.html', 'w').write(''.join(html))
print(len(PAGINAS), 'paginas')
