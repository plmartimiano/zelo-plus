#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <time.h>
#include <Wire.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <U8g2_for_Adafruit_GFX.h>
#include <ESP32Servo.h>
#include <RTClib.h>
#include <esp_sntp.h>

#define BUZZER_PIN 15
#define LED_PIN 4
#define BUTTON_PIN 5
#define NUM_COMPARTIMENTOS 3
#define MAX_HORARIOS 6       // horarios por medicamento
#define MAX_FAMILIARES 5
#define MAX_HISTORICO 60

// Um servo por compartimento. O GPIO 12 foi evitado de proposito: ele interfere
// na inicializacao do ESP32.
const int PINOS_SERVO[NUM_COMPARTIMENTOS] = {13, 14, 27};
// Angulos de cada porta (ajuste aqui se algum mecanismo precisar de outro valor).
const int ANGULO_FECHADO[NUM_COMPARTIMENTOS] = {0, 0, 0};
const int ANGULO_ABERTO[NUM_COMPARTIMENTOS] = {90, 90, 90};

// Cor de cada compartimento na pagina (use etiquetas das mesmas cores na caixa).
const char* COR_COMPARTIMENTO[NUM_COMPARTIMENTOS] = {"#2563eb", "#16a34a", "#9333ea"};
const char* FUNDO_COMPARTIMENTO[NUM_COMPARTIMENTOS] = {"#eff6ff", "#f0fdf4", "#faf5ff"};
const char* NOME_COR[NUM_COMPARTIMENTOS] = {"azul", "verde", "roxo"};

// Visor: tela TFT 2,25" colorida (controlador ST7789P3, 76 x 284 pontos), ligada
// por SPI (SCL -> GPIO 18, SDA -> GPIO 23). A luz de fundo deste modulo acende
// com o pino BL em nivel BAIXO.
#define TELA_CS 26
#define TELA_DC 16
#define TELA_RST 17
#define TELA_BL 25
Adafruit_ST7789 tela(TELA_CS, TELA_DC, TELA_RST);
U8G2_FOR_ADAFRUIT_GFX texto;
Servo servos[NUM_COMPARTIMENTOS];
// Sinal de brilho da tela. Fica com a biblioteca dos servos, que escolhe um canal
// e um temporizador livres: os servos usam 50 Hz e a luz de fundo 5.000 Hz, e os
// dois nao podem dividir o mesmo temporizador do ESP32.
ESP32PWM luzVisor;
WebServer server(80);
DNSServer dnsServer;
Preferences preferences;
Preferences prefsCadastro;
Preferences prefsHistorico;

const byte DNS_PORT = 53;
IPAddress apIP(192, 168, 4, 1);

const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = -10800; // Brasília (GMT-3)
const int daylightOffset_sec = 0;

bool modoConfig = false;
bool modoLocal = false;  // sem internet: o dispenser usa a propria rede
bool servidorNoAr = false;

// Rede propria aberta automaticamente quando a rede salva nao conecta ao ligar.
const char* NOME_REDE_LOCAL = "ZeloPlus";
const char* SENHA_REDE_LOCAL = "zelo1234";
const unsigned long INTERVALO_NOVA_TENTATIVA = 10UL * 60UL * 1000UL; // 10 min
const unsigned long DURACAO_TENTATIVA = 20000;
String nomeRedeLocal = "";      // ZeloPlus (automatica) ou ZeloPlus-Config (pela configuracao)
String motivoSemInternet = "";  // mostrado no aviso da pagina
String redeSalva = "";
String senhaRedeSalva = "";
bool tentandoRede = false;
unsigned long tentativaIniciadaEm = 0;
String configErro = "";

// ---------- RELOGIO DS3231 (opcional) ----------
// Modulo de relogio ligado no I2C (endereco 0x68). Guarda a hora
// certa mesmo sem internet e sem energia. Sem o modulo, tudo funciona como antes,
// com a hora vinda da internet (NTP).
RTC_DS3231 rtc;
bool rtcPresente = false;             // o modulo respondeu no I2C ao ligar
bool horaDoRtc = false;               // a hora atual veio do DS3231 ao ligar
volatile bool ntpSincronizou = false; // avisado pelo NTP a cada acerto de hora
const uint32_t HORA_MINIMA_VALIDA = 1704067200UL; // 01/01/2024: antes disso, hora invalida

// Contato que recebe avisos pelo Telegram. O chatId e o numero que o Telegram
// usa para identificar a conversa da pessoa com o bot do Zelo+ (a pessoa precisa
// abrir o bot e tocar em "Iniciar" uma vez).
struct Contato {
  String nome;
  String telefone;
  String chatId;
};

// Medicamento guardado num compartimento fixo (indice 0..2 = compartimento 1..3).
struct Medicamento {
  String nome;
  int totalHorarios = 0;
  int hora[MAX_HORARIOS];
  int minuto[MAX_HORARIOS];
  bool jaDisparado[MAX_HORARIOS];
};

String nomePaciente = "";
Medicamento medicamentos[NUM_COMPARTIMENTOS];
Contato cuidador;
Contato familiares[MAX_FAMILIARES];
String tokenTelegram = ""; // token do bot, criado pelo cuidador no @BotFather
String chaveIA = "";       // chave da API do Gemini (leitura da caixa por foto)
const char* MODELO_IA_PADRAO = "gemini-3.5-flash";
String modeloIA = MODELO_IA_PADRAO; // modelo do Gemini usado na leitura
int ultimoMinutoChecado = -1;

const unsigned long TEMPO_PORTA_ABERTA = 3UL * 60UL * 1000UL;
const unsigned long TEMPO_ABASTECIMENTO = 3UL * 60UL * 1000UL;
const unsigned long TRAVA_BOTAO = 7UL * 1000UL; // 7 segundos de trava apos abrir

// Ciclo do alarme: 1 min tocando, 1 min em silencio, repetindo.
// Aos 6 min sem acesso avisa o cuidador; aos 12 min para de tocar e alerta
// cuidador + familiares.
// MODO_TESTE encurta os tempos para testar em bancada: aviso aos 30 s, alerta
// final aos 60 s e ciclo de 5 s tocando / 5 s em silencio (mesma proporcao do
// modo real). Mude para 0 antes de usar de verdade.
#define MODO_TESTE 1

#if MODO_TESTE
const unsigned long CICLO_ALARME = 5UL * 1000UL;
const unsigned long TEMPO_PRIMEIRO_AVISO = 30UL * 1000UL;
const unsigned long TEMPO_ALERTA_FINAL = 60UL * 1000UL;
#else
const unsigned long CICLO_ALARME = 60UL * 1000UL;
const unsigned long TEMPO_PRIMEIRO_AVISO = 6UL * 60UL * 1000UL;
const unsigned long TEMPO_ALERTA_FINAL = 12UL * 60UL * 1000UL;
#endif

enum Estado { AGUARDANDO, TOCANDO, PORTA_ABERTA_ESTADO, ABASTECENDO };
Estado estadoAtual = AGUARDANDO;

// Motivo da abertura no estado ABASTECENDO (so muda o texto do visor).
enum ModoAbastecimento { ABAST_NOVO, ABAST_REPOR, ABAST_ESVAZIAR };
ModoAbastecimento modoAbastecimento = ABAST_NOVO;
int compartimentoAbastecendo = -1;
uint8_t filaAbastecimento = 0; // compartimentos novos esperando para abrir (bits)

uint8_t mascaraAberta = 0;     // compartimentos com a porta aberta agora (bits)
unsigned long portaAbertaEm = 0;
unsigned long abastecimentoAbertoEm = 0;
unsigned long ultimoToggleAlarme = 0;
bool ledBuzzerLigado = false;

// Dose atual: compartimentos que tocaram juntos no mesmo horario.
uint8_t doseMascara = 0;
String horarioDose = "";
String nomesDose = "";
unsigned long alarmeIniciadoEm = 0;
int nivelAvisoEnviado = 0;   // 0 = nenhum, 1 = cuidador avisado, 2 = todos alertados
bool dosePendente = false;   // alarme terminou sem acesso; botao ainda abre os compartimentos

// Horarios que chegaram enquanto o dispenser estava ocupado: tocam assim que
// ele voltar a ficar livre.
uint8_t mascaraEmEspera = 0;
String horarioEmEspera = "";

bool temBit(uint8_t mascara, int i) {
  return (mascara >> i) & 1;
}

bool medicamentoAtivo(int c) {
  return medicamentos[c].nome != "" && medicamentos[c].totalHorarios > 0;
}

int totalMedicamentosAtivos() {
  int total = 0;
  for (int c = 0; c < NUM_COMPARTIMENTOS; c++) {
    if (medicamentoAtivo(c)) total++;
  }
  return total;
}

// ---------- PORTAS (servos) ----------
// Nunca move dois servos ao mesmo tempo, para nao sobrecarregar a fonte.

// Duracao de cada movimento de porta (abrir ou fechar), em milissegundos.
// Aumente para deixar as portas mais lentas.
const unsigned long TEMPO_MOVIMENTO_PORTA = 7000;

// Tamanho de cada passo do movimento, em graus. Passos de fracao de grau nao
// vencem o atrito da porta: o servo fica zumbindo parado e depois salta de uma
// vez. Com 3 graus por passo ele sempre responde ao comando.
const int GRAUS_POR_PASSO = 3;

// Largura do pulso do servo em 0 e em 180 graus (padrao do SG90/MG90).
const int PULSO_0_GRAU = 544;
const int PULSO_180_GRAUS = 2400;

// Resolucao do sinal dos servos: 16 bits dao cerca de 0,3 us por unidade (o
// padrao, 10 bits, so tem cerca de 20 us, quase 2 graus).
const int RESOLUCAO_SERVO = 16;

int pulsoDoAngulo(int angulo) {
  return PULSO_0_GRAU + (long)angulo * (PULSO_180_GRAUS - PULSO_0_GRAU) / 180;
}

// Move a porta em velocidade constante, um passo de GRAUS_POR_PASSO por vez,
// com uma pausa igual entre os passos para completar TEMPO_MOVIMENTO_PORTA.
void moverPorta(int c, int de, int para) {
  int passos = max(1, abs(para - de) / GRAUS_POR_PASSO);
  unsigned long pausa = TEMPO_MOVIMENTO_PORTA / passos;
  for (int i = 1; i <= passos; i++) {
    int angulo = de + (para - de) * i / passos;
    servos[c].writeMicroseconds(pulsoDoAngulo(angulo));
    delay(pausa);
  }
}

void abrirCompartimento(int c) {
  moverPorta(c, ANGULO_FECHADO[c], ANGULO_ABERTO[c]);
}

void fecharCompartimento(int c) {
  moverPorta(c, ANGULO_ABERTO[c], ANGULO_FECHADO[c]);
}

// Abre os compartimentos marcados, um logo apos o outro.
void abrirCompartimentos(uint8_t mascara) {
  for (int c = 0; c < NUM_COMPARTIMENTOS; c++) {
    if (temBit(mascara, c)) abrirCompartimento(c);
  }
}

void fecharCompartimentos(uint8_t mascara) {
  for (int c = 0; c < NUM_COMPARTIMENTOS; c++) {
    if (temBit(mascara, c)) fecharCompartimento(c);
  }
}

// ---------- TEXTO ----------

// Escapa caracteres especiais antes de inserir texto do usuario (ou nomes de
// redes Wi-Fi) no HTML, para que aspas ou tags nao quebrem a pagina.
String escaparHTML(const String& texto) {
  String saida;
  saida.reserve(texto.length() + 8);
  for (unsigned int i = 0; i < texto.length(); i++) {
    char c = texto[i];
    switch (c) {
      case '&': saida += "&amp;"; break;
      case '<': saida += "&lt;"; break;
      case '>': saida += "&gt;"; break;
      case '"': saida += "&quot;"; break;
      case '\'': saida += "&#39;"; break;
      default: saida += c;
    }
  }
  return saida;
}

// ---------- VISOR ----------

const int VISOR_LARGURA = 284;
const int VISOR_ALTURA = 76;

// Cores do visor (RGB). Os compartimentos usam as mesmas cores da pagina.
const uint32_t COR_PRETO = 0x000000;
const uint32_t COR_BRANCO = 0xFFFFFF;
const uint32_t COR_AMARELO = 0xFACC15;
const uint32_t COR_VERDE = 0x16A34A;
const uint32_t COR_VERMELHO = 0xDC2626;
const uint32_t COR_LARANJA = 0xEA580C;
const uint32_t COR_CINZA = 0x4B5563;
const uint32_t COR_AZUL_ESCURO = 0x1E3A8A;
const uint32_t COR_VISOR_COMPARTIMENTO[NUM_COMPARTIMENTOS] = {0x2563EB, 0x16A34A, 0x9333EA};

uint32_t corFundo = COR_PRETO;
uint32_t corLinha[2] = {COR_BRANCO, COR_BRANCO};
String linhaMostrada[2];
bool telaEsperaDesenhada = false;

uint16_t cor565(uint32_t rgb) {
  return tela.color565((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
}

// Brilho de 0 a 100 %. O pino BL acende em nivel baixo: 100 % = pino em 0.
void brilhoVisor(int porcento) {
  luzVisor.write((100 - porcento) * 255 / 100);
}

// Troca a fonte mantendo o texto transparente (a biblioteca volta a pintar o
// fundo das letras a cada troca de fonte).
void fonteVisor(const uint8_t* fonte) {
  texto.setFont(fonte);
  texto.setFontMode(1);
}

// Cores da proxima tela: fundo e cor de cada linha. Vale a partir do
// proximo limparVisor().
void temaVisor(uint32_t fundo, uint32_t linha0 = COR_BRANCO, uint32_t linha1 = COR_BRANCO) {
  corFundo = fundo;
  corLinha[0] = linha0;
  corLinha[1] = linha1;
}

void limparVisor() {
  tela.fillScreen(cor565(corFundo));
  linhaMostrada[0] = "\x01";
  linhaMostrada[1] = "\x01";
  telaEsperaDesenhada = false;
}

void iniciarVisor() {
  luzVisor.attachPin(TELA_BL, 5000, 8);
  brilhoVisor(100);
  tela.init(VISOR_ALTURA, VISOR_LARGURA); // o painel e "em pe": 76 x 284
  tela.setRotation(1);                    // deitado: 284 x 76
  tela.invertDisplay(false);              // este painel nao usa inversao de cores
  texto.begin(tela);
  temaVisor(COR_PRETO);
  limparVisor();
}

// Prepara o texto para o visor: corta em 'maxLetras' letras (sem partir os
// acentos) e, com maiusculas = true (nomes dos remedios), passa tudo para
// maiusculas, inclusive as letras acentuadas ("Losartana Potássica" ->
// "LOSARTANA POTÁSSICA").
String textoVisor(const String& original, unsigned int maxLetras = 40, bool maiusculas = false) {
  String saida;
  unsigned int letras = 0;
  for (unsigned int i = 0; i < original.length() && letras < maxLetras; i++) {
    uint8_t c = (uint8_t)original[i];
    if (c == 0xC3 && i + 1 < original.length()) {
      uint8_t segundo = (uint8_t)original[++i];
      if (maiusculas && segundo >= 0xA0 && segundo <= 0xBE && segundo != 0xB7) segundo -= 0x20;
      saida += (char)c;
      saida += (char)segundo;
    } else if (c >= 0x80) {
      saida += (char)c;
      while (i + 1 < original.length() && ((uint8_t)original[i + 1] & 0xC0) == 0x80) saida += original[++i];
    } else {
      saida += maiusculas ? (char)toupper(c) : (char)c;
    }
    letras++;
  }
  return saida;
}

// Escreve uma das duas linhas do visor, centralizada: linha 0 (em cima, letra
// menor) ou linha 1 (embaixo, letra maior, para o nome do remedio). Se o texto
// nao couber, a letra diminui. So redesenha quando o texto muda.
void escreverLinha(int linha, const String& conteudo) {
  if (conteudo == linhaMostrada[linha]) return;
  linhaMostrada[linha] = conteudo;

  int topo = (linha == 0) ? 0 : 34;
  int altura = (linha == 0) ? 34 : 42;
  tela.fillRect(0, topo, VISOR_LARGURA, altura, cor565(corFundo));

  const uint8_t* fontes[3] = {u8g2_font_helvB24_tf, u8g2_font_helvB18_tf, u8g2_font_helvB14_tf};
  int primeira = (linha == 0) ? 1 : 0;
  for (int f = primeira; f < 3; f++) {
    fonteVisor(fontes[f]);
    if (texto.getUTF8Width(conteudo.c_str()) <= VISOR_LARGURA - 8 || f == 2) break;
  }
  // Ainda grande demais na menor letra: corta o final e poe "..."
  String mostrar = conteudo;
  while (texto.getUTF8Width(mostrar.c_str()) > VISOR_LARGURA - 8 && mostrar.length() > 4) {
    if (mostrar.endsWith("...")) mostrar.remove(mostrar.length() - 3);
    int fim = mostrar.length() - 1;
    while (fim > 0 && ((uint8_t)mostrar[fim] & 0xC0) == 0x80) fim--; // nao parte letras acentuadas
    mostrar.remove(fim);
    mostrar.trim();
    mostrar += "...";
  }
  int largura = texto.getUTF8Width(mostrar.c_str());
  int base = topo + (altura + texto.getFontAscent()) / 2;
  texto.setForegroundColor(cor565(corLinha[linha]));
  texto.drawUTF8((VISOR_LARGURA - largura) / 2, base, mostrar.c_str());
}

// Tela de espera: hora grande a esquerda e caixa colorida com a situacao a
// direita. So redesenha a parte que mudou.
void desenharTelaEspera(const char* hora, const char* situacao1, const char* situacao2, uint32_t corCaixa) {
  static String horaMostrada, situacaoMostrada;
  String situacao = String(situacao1) + "|" + situacao2;
  if (!telaEsperaDesenhada) {
    tela.fillScreen(cor565(COR_PRETO));
    horaMostrada = "";
    situacaoMostrada = "";
    telaEsperaDesenhada = true;
  }
  if (horaMostrada != hora) {
    horaMostrada = hora;
    tela.fillRect(0, 0, 142, VISOR_ALTURA, cor565(COR_PRETO));
    fonteVisor(u8g2_font_logisoso46_tn);
    texto.setForegroundColor(cor565(COR_BRANCO));
    texto.drawUTF8(4, 61, hora);
  }
  if (situacaoMostrada != situacao) {
    situacaoMostrada = situacao;
    tela.fillRect(142, 0, VISOR_LARGURA - 142, VISOR_ALTURA, cor565(COR_PRETO));
    tela.fillRoundRect(144, 6, 136, 64, 10, cor565(corCaixa));
    fonteVisor(u8g2_font_helvB18_tf);
    texto.setForegroundColor(cor565(COR_BRANCO));
    texto.drawUTF8(212 - texto.getUTF8Width(situacao1) / 2, 33, situacao1);
    texto.drawUTF8(212 - texto.getUTF8Width(situacao2) / 2, 59, situacao2);
  }
}

String formatarHorario(int hora, int minuto) {
  char valor[6];
  snprintf(valor, sizeof(valor), "%02d:%02d", hora, minuto);
  return String(valor);
}

// "A", "A e B", "A, B e C"
String juntarNomes(uint8_t mascara) {
  String nomes[NUM_COMPARTIMENTOS];
  int total = 0;
  for (int c = 0; c < NUM_COMPARTIMENTOS; c++) {
    if (temBit(mascara, c) && medicamentos[c].nome != "") nomes[total++] = medicamentos[c].nome;
  }
  String texto;
  for (int i = 0; i < total; i++) {
    if (i > 0) texto += (i == total - 1) ? " e " : ", ";
    texto += nomes[i];
  }
  return texto;
}

// "C1 C2"
String listaCompartimentos(uint8_t mascara) {
  String texto;
  for (int c = 0; c < NUM_COMPARTIMENTOS; c++) {
    if (!temBit(mascara, c)) continue;
    if (texto != "") texto += " ";
    texto += "C" + String(c + 1);
  }
  return texto;
}

// ---------- ENVIO DE MENSAGENS (Telegram) ----------

String codificarURL(const String& texto) {
  const char* hex = "0123456789ABCDEF";
  String saida;
  saida.reserve(texto.length() * 3);
  for (unsigned int i = 0; i < texto.length(); i++) {
    uint8_t c = (uint8_t)texto[i];
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      saida += (char)c;
    } else {
      saida += '%';
      saida += hex[c >> 4];
      saida += hex[c & 0x0F];
    }
  }
  return saida;
}

bool contatoPodeReceber(const Contato& contato) {
  return tokenTelegram != "" && contato.chatId != "";
}

// Chama um metodo da API de bots do Telegram. Devolve o codigo HTTP (ou <= 0 em
// falha de conexao) e, se pedido, o corpo da resposta.
int chamarTelegram(const String& token, const char* metodo, const String& corpo, String* resposta = nullptr) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Telegram: sem Wi-Fi");
    return -1;
  }

  WiFiClientSecure cliente;
  cliente.setInsecure(); // prototipo: nao valida o certificado do servidor
  HTTPClient http;
  http.setTimeout(10000);
  String url = "https://api.telegram.org/bot" + token + "/" + metodo;
  if (!http.begin(cliente, url)) {
    Serial.println("Telegram: falha ao iniciar conexao");
    return -1;
  }
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  int codigo = http.POST(corpo);
  if (resposta != nullptr && codigo > 0) *resposta = http.getString();
  http.end();
  return codigo;
}

bool enviarTelegram(const Contato& contato, const String& texto) {
  if (!contatoPodeReceber(contato)) return false;

  String corpo = "chat_id=" + codificarURL(contato.chatId) + "&text=" + codificarURL(texto);
  int codigo = chamarTelegram(tokenTelegram, "sendMessage", corpo);

  Serial.print("Telegram para ");
  Serial.print(contato.nome);
  Serial.print(": HTTP ");
  Serial.println(codigo);
  return codigo == 200;
}

void enviarParaCuidador(const String& texto) {
  enviarTelegram(cuidador, texto);
}

void enviarParaTodos(const String& texto) {
  enviarTelegram(cuidador, texto);
  for (int i = 0; i < MAX_FAMILIARES; i++) {
    enviarTelegram(familiares[i], texto);
  }
}

String horarioAgora() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 100)) return "";
  return formatarHorario(timeinfo.tm_hour, timeinfo.tm_min);
}

void avisarPrimeiroAtraso() {
  enviarParaCuidador("Zelo+: Paciente “" + nomePaciente + "” não acessou o medicamento das “" +
                     horarioDose + "” horas (" + nomesDose + ").");
}

void alertarSemAcesso() {
  enviarParaTodos("Zelo+: Paciente “" + nomePaciente + "” não foi até o dispenser no horário das “" +
                  horarioDose + "” (" + nomesDose + ").");
}

// Depois de um aviso, confirma para quem foi avisado que o paciente chegou.
void avisarAcessoAposAviso() {
  String texto = "Zelo+: " + nomePaciente + " acessou o dispenser às " + horarioAgora() +
                 " (dose de " + nomesDose + " das " + horarioDose + ").";
  if (nivelAvisoEnviado >= 2) {
    enviarParaTodos(texto);
  } else if (nivelAvisoEnviado == 1) {
    enviarParaCuidador(texto);
  }
  nivelAvisoEnviado = 0;
}

// ---------- PERSISTENCIA DO CADASTRO ----------
// Namespace "cadastro" em Preferences (separado do namespace "wifi"),
// para o cadastro sobreviver a reinicios e quedas de energia.

void salvarContato(const char* prefixo, const Contato& contato) {
  String p = prefixo;
  prefsCadastro.putString((p + "_nome").c_str(), contato.nome);
  prefsCadastro.putString((p + "_tel").c_str(), contato.telefone);
  prefsCadastro.putString((p + "_chat").c_str(), contato.chatId);
}

void carregarContato(const char* prefixo, Contato& contato) {
  String p = prefixo;
  contato.nome = prefsCadastro.getString((p + "_nome").c_str(), "");
  contato.telefone = prefsCadastro.getString((p + "_tel").c_str(), "");
  contato.chatId = prefsCadastro.getString((p + "_chat").c_str(), "");
  contato.nome.trim();
  contato.telefone.trim();
  contato.chatId.trim();
}

String prefixoFamiliar(int i) {
  return "fam" + String(i);
}

String prefixoMedicamento(int c) {
  return "m" + String(c);
}

void salvarMedicamento(int c) {
  const Medicamento& m = medicamentos[c];
  String p = prefixoMedicamento(c);
  uint8_t horas[MAX_HORARIOS];
  uint8_t minutos[MAX_HORARIOS];
  for (int i = 0; i < m.totalHorarios; i++) {
    horas[i] = (uint8_t)m.hora[i];
    minutos[i] = (uint8_t)m.minuto[i];
  }
  prefsCadastro.putString((p + "_nome").c_str(), m.nome);
  prefsCadastro.putUChar((p + "_tot").c_str(), (uint8_t)m.totalHorarios);
  if (m.totalHorarios > 0) {
    prefsCadastro.putBytes((p + "_hr").c_str(), horas, m.totalHorarios);
    prefsCadastro.putBytes((p + "_mn").c_str(), minutos, m.totalHorarios);
  }
}

// Le horarios guardados como dois vetores de bytes (horas e minutos).
int lerHorarios(const char* chaveTotal, const char* chaveHoras, const char* chaveMinutos,
                int* horaDestino, int* minutoDestino, bool* disparadoDestino) {
  int total = prefsCadastro.getUChar(chaveTotal, 0);
  if (total > MAX_HORARIOS) total = MAX_HORARIOS;

  uint8_t horas[MAX_HORARIOS];
  uint8_t minutos[MAX_HORARIOS];
  if (total == 0 ||
      prefsCadastro.getBytes(chaveHoras, horas, total) != (size_t)total ||
      prefsCadastro.getBytes(chaveMinutos, minutos, total) != (size_t)total) {
    return 0;
  }

  int validos = 0;
  for (int i = 0; i < total; i++) {
    if (horas[i] > 23 || minutos[i] > 59) continue;
    horaDestino[validos] = horas[i];
    minutoDestino[validos] = minutos[i];
    disparadoDestino[validos] = false;
    validos++;
  }
  return validos;
}

void carregarMedicamento(int c) {
  Medicamento& m = medicamentos[c];
  String p = prefixoMedicamento(c);
  m.nome = prefsCadastro.getString((p + "_nome").c_str(), "");
  m.nome.trim(); // versoes antigas podiam gravar espacos no fim do nome
  m.totalHorarios = lerHorarios((p + "_tot").c_str(), (p + "_hr").c_str(), (p + "_mn").c_str(),
                                m.hora, m.minuto, m.jaDisparado);
}

void salvarCadastro() {
  prefsCadastro.putString("nome", nomePaciente); // chave mantida da versao anterior
  for (int c = 0; c < NUM_COMPARTIMENTOS; c++) {
    salvarMedicamento(c);
  }

  prefsCadastro.putString("tg_token", tokenTelegram);
  prefsCadastro.putString("ia_chave", chaveIA);
  prefsCadastro.putString("ia_modelo", modeloIA);
  salvarContato("cuid", cuidador);
  for (int i = 0; i < MAX_FAMILIARES; i++) {
    salvarContato(prefixoFamiliar(i).c_str(), familiares[i]);
  }
}

// A versao de um compartimento so guardava "remedio" + "total"/"horas"/"minutos".
// Na primeira vez que esta versao liga, esse remedio passa a ser o compartimento 1.
void migrarCadastroAntigo() {
  if (prefsCadastro.isKey("m0_nome") || !prefsCadastro.isKey("remedio")) return;

  Medicamento& m = medicamentos[0];
  m.nome = prefsCadastro.getString("remedio", "");
  m.nome.trim();
  m.totalHorarios = lerHorarios("total", "horas", "minutos", m.hora, m.minuto, m.jaDisparado);
  salvarMedicamento(0);

  prefsCadastro.remove("remedio");
  prefsCadastro.remove("total");
  prefsCadastro.remove("horas");
  prefsCadastro.remove("minutos");
  Serial.println("Cadastro antigo migrado para o compartimento 1");
}

void carregarCadastro() {
  nomePaciente = prefsCadastro.getString("nome", "");
  nomePaciente.trim();

  migrarCadastroAntigo();
  for (int c = 0; c < NUM_COMPARTIMENTOS; c++) {
    carregarMedicamento(c);
  }

  tokenTelegram = prefsCadastro.getString("tg_token", "");
  chaveIA = prefsCadastro.getString("ia_chave", "");
  modeloIA = prefsCadastro.getString("ia_modelo", MODELO_IA_PADRAO);
  carregarContato("cuid", cuidador);
  for (int i = 0; i < MAX_FAMILIARES; i++) {
    carregarContato(prefixoFamiliar(i).c_str(), familiares[i]);
  }

  for (int c = 0; c < NUM_COMPARTIMENTOS; c++) {
    Serial.print("Compartimento ");
    Serial.print(c + 1);
    Serial.print(": ");
    Serial.print(medicamentoAtivo(c) ? medicamentos[c].nome : String("(vazio)"));
    Serial.print(" - ");
    Serial.print(medicamentos[c].totalHorarios);
    Serial.println(" horario(s)");
  }
}

// ---------- HISTORICO DE DOSES ----------
// Cada medicamento de cada disparo de alarme vira um registro, guardado em
// Preferences (namespace "historico") num buffer circular com as ultimas
// MAX_HISTORICO doses.

enum StatusDose : uint8_t {
  DOSE_PENDENTE = 0,    // alarme tocando (ou placa reiniciou antes de concluir)
  DOSE_NO_HORARIO = 1,  // acesso antes do primeiro aviso ao cuidador
  DOSE_ATRASADA = 2,    // acesso depois do primeiro aviso (inclusive apos o alerta final)
  DOSE_SEM_ACESSO = 3   // alerta final enviado e ninguem acessou
};

struct RegistroDose {
  uint32_t inicio;         // horario do disparo (segundos desde 1970)
  uint16_t atrasoSeg;      // tempo ate o acesso
  uint8_t status;
  uint8_t compartimento;   // 0..2
  char remedio[20];        // nome do medicamento no momento da dose
};

// Formato da versao anterior (um compartimento, sem nome do remedio).
struct RegistroDoseV1 {
  uint32_t inicio;
  uint16_t atrasoSeg;
  uint8_t status;
  uint8_t reservado;
};

RegistroDose historico[MAX_HISTORICO];
int totalHistorico = 0;    // registros validos (ate MAX_HISTORICO)
int proximoHistorico = 0;  // posicao onde entra o proximo registro
int registroDose[NUM_COMPARTIMENTOS] = {-1, -1, -1}; // registro de cada compartimento da dose atual

void salvarHistorico() {
  prefsHistorico.putBytes("regs2", historico, sizeof(historico));
  prefsHistorico.putUChar("total", (uint8_t)totalHistorico);
  prefsHistorico.putUChar("prox", (uint8_t)proximoHistorico);
}

void carregarHistorico() {
  totalHistorico = prefsHistorico.getUChar("total", 0);
  proximoHistorico = prefsHistorico.getUChar("prox", 0);
  if (totalHistorico > MAX_HISTORICO || proximoHistorico >= MAX_HISTORICO) {
    totalHistorico = 0;
    proximoHistorico = 0;
  }

  if (prefsHistorico.getBytes("regs2", historico, sizeof(historico)) == sizeof(historico)) return;

  // Sem historico no formato novo: converte o da versao anterior, se existir.
  memset(historico, 0, sizeof(historico));
  static RegistroDoseV1 antigos[MAX_HISTORICO];
  if (prefsHistorico.getBytes("regs", antigos, sizeof(antigos)) == sizeof(antigos)) {
    for (int i = 0; i < MAX_HISTORICO; i++) {
      historico[i].inicio = antigos[i].inicio;
      historico[i].atrasoSeg = antigos[i].atrasoSeg;
      historico[i].status = antigos[i].status;
      historico[i].compartimento = 0;
    }
    prefsHistorico.remove("regs");
    salvarHistorico();
    Serial.println("Historico antigo convertido");
  } else {
    totalHistorico = 0;
    proximoHistorico = 0;
  }
}

// Indice do k-esimo registro mais recente (k = 0 e o ultimo).
int indiceHistorico(int k) {
  return (proximoHistorico - 1 - k + 2 * MAX_HISTORICO) % MAX_HISTORICO;
}

int registrarInicioDose(int compartimento) {
  int idx = proximoHistorico;
  RegistroDose& r = historico[idx];
  r.inicio = (uint32_t)time(nullptr);
  r.atrasoSeg = 0;
  r.status = DOSE_PENDENTE;
  r.compartimento = (uint8_t)compartimento;
  strncpy(r.remedio, medicamentos[compartimento].nome.c_str(), sizeof(r.remedio) - 1);
  r.remedio[sizeof(r.remedio) - 1] = '\0';
  proximoHistorico = (proximoHistorico + 1) % MAX_HISTORICO;
  if (totalHistorico < MAX_HISTORICO) totalHistorico++;
  return idx;
}

// Atualiza todos os registros da dose atual com a mesma situacao.
void atualizarDose(StatusDose status, unsigned long decorridoMs) {
  unsigned long segundos = decorridoMs / 1000UL;
  bool mudou = false;
  for (int c = 0; c < NUM_COMPARTIMENTOS; c++) {
    if (registroDose[c] < 0) continue;
    historico[registroDose[c]].status = status;
    historico[registroDose[c]].atrasoSeg = segundos > 65535UL ? 65535 : (uint16_t)segundos;
    mudou = true;
  }
  if (mudou) salvarHistorico();
}

void esquecerRegistrosDose() {
  for (int c = 0; c < NUM_COMPARTIMENTOS; c++) registroDose[c] = -1;
}

bool registroEmAndamento(int idx) {
  if (estadoAtual != TOCANDO) return false;
  for (int c = 0; c < NUM_COMPARTIMENTOS; c++) {
    if (registroDose[c] == idx) return true;
  }
  return false;
}

const char* textoStatusDose(uint8_t status, bool emAndamento) {
  switch (status) {
    case DOSE_NO_HORARIO: return "Tomada no horario";
    case DOSE_ATRASADA: return "Tomada com atraso";
    case DOSE_SEM_ACESSO: return "Sem acesso";
    default: return emAndamento ? "Tocando agora" : "Sem acesso";
  }
}

const char* corStatusDose(uint8_t status, bool emAndamento) {
  switch (status) {
    case DOSE_NO_HORARIO: return "#16a34a";
    case DOSE_ATRASADA: return "#d97706";
    case DOSE_PENDENTE: return emAndamento ? "#2563eb" : "#dc2626";
    default: return "#dc2626";
  }
}

String nomeRegistro(const RegistroDose& r) {
  String nome = String(r.remedio);
  if (nome == "") return "Compart. " + String(r.compartimento + 1);
  return nome + " (C" + String(r.compartimento + 1) + ")";
}

String formatarDataHora(uint32_t segundos, const char* formato) {
  time_t t = (time_t)segundos;
  struct tm info;
  localtime_r(&t, &info);
  char buffer[24];
  strftime(buffer, sizeof(buffer), formato, &info);
  return String(buffer);
}

String formatarAtraso(uint16_t segundos) {
  if (segundos < 60) return String(segundos) + " s";
  return String(segundos / 60) + " min";
}

int percentual(int parte, int total) {
  return (parte * 100 + total / 2) / total;
}

// Bolinha colorida com o numero do compartimento.
String marcaCompartimento(int c, int tamanho = 26) {
  return "<span class='num' style='background:" + String(COR_COMPARTIMENTO[c]) + ";width:" + String(tamanho) +
         "px;height:" + String(tamanho) + "px;font-size:" + String(tamanho * 55 / 100) + "px'>" + String(c + 1) + "</span>";
}

String htmlHistorico() {
  String html = "<section class='bloco'><h2>&#128202; Histórico de doses</h2>";
  if (totalHistorico == 0) {
    html += "<p class='suave'>Nenhuma dose registrada ainda.</p></section>";
    return html;
  }

  // Resumo dos ultimos 7 dias (sem contar o alarme que esta tocando agora),
  // geral e por medicamento.
  uint32_t limite = (uint32_t)time(nullptr) - 7UL * 24UL * 3600UL;
  int noHorario = 0, atrasadas = 0, semAcesso = 0;
  const int MAX_GRUPOS = 8; // medicamentos diferentes no resumo
  String nomesGrupo[MAX_GRUPOS];
  int compartimentoGrupo[MAX_GRUPOS];
  int tomadasGrupo[MAX_GRUPOS];
  int totalGrupo[MAX_GRUPOS];
  int grupos = 0;
  for (int k = 0; k < totalHistorico; k++) {
    int idx = indiceHistorico(k);
    const RegistroDose& r = historico[idx];
    if (r.inicio < limite || registroEmAndamento(idx)) continue;
    bool tomada = (r.status == DOSE_NO_HORARIO || r.status == DOSE_ATRASADA);
    if (r.status == DOSE_NO_HORARIO) noHorario++;
    else if (r.status == DOSE_ATRASADA) atrasadas++;
    else semAcesso++;

    String nome = nomeRegistro(r);
    int g = 0;
    while (g < grupos && nomesGrupo[g] != nome) g++;
    if (g == grupos) {
      if (grupos == MAX_GRUPOS) continue;
      nomesGrupo[g] = nome;
      compartimentoGrupo[g] = r.compartimento;
      tomadasGrupo[g] = 0;
      totalGrupo[g] = 0;
      grupos++;
    }
    totalGrupo[g]++;
    if (tomada) tomadasGrupo[g]++;
  }
  int concluidas = noHorario + atrasadas + semAcesso;
  if (concluidas == 0) {
    html += "<p class='suave'>Sem doses concluídas nos últimos 7 dias.</p>";
  } else {
    html += "<div class='adesao'><b>" + String(percentual(noHorario + atrasadas, concluidas)) +
            "%</b> das doses foram tomadas<br><small>nos últimos 7 dias</small></div>";
    html += "<p class='contagem'><span style='color:#15803d'>&#9679; " + String(noHorario) + " no horário</span>";
    html += "<span style='color:#b45309'>&#9679; " + String(atrasadas) + " com atraso</span>";
    html += "<span style='color:#b91c1c'>&#9679; " + String(semAcesso) + " sem acesso</span></p>";
    if (grupos > 1) {
      // ordena pelo numero do compartimento (1, 2, 3)
      for (int a = 1; a < grupos; a++) {
        for (int b = a; b > 0 && compartimentoGrupo[b] < compartimentoGrupo[b - 1]; b--) {
          String n = nomesGrupo[b]; nomesGrupo[b] = nomesGrupo[b - 1]; nomesGrupo[b - 1] = n;
          int t = compartimentoGrupo[b]; compartimentoGrupo[b] = compartimentoGrupo[b - 1]; compartimentoGrupo[b - 1] = t;
          t = tomadasGrupo[b]; tomadasGrupo[b] = tomadasGrupo[b - 1]; tomadasGrupo[b - 1] = t;
          t = totalGrupo[b]; totalGrupo[b] = totalGrupo[b - 1]; totalGrupo[b - 1] = t;
        }
      }
      for (int g = 0; g < grupos; g++) {
        int c = compartimentoGrupo[g] < NUM_COMPARTIMENTOS ? compartimentoGrupo[g] : 0;
        String nome = nomesGrupo[g];
        int parentese = nome.indexOf(" (C");
        if (parentese > 0) nome = nome.substring(0, parentese);
        html += "<div class='linha-remedio'>" + marcaCompartimento(c, 22) + " " + escaparHTML(nome) +
                ": <b>" + String(percentual(tomadasGrupo[g], totalGrupo[g])) + "%</b></div>";
      }
    }
  }

  int mostrar = totalHistorico < 20 ? totalHistorico : 20;
  html += "<details class='caixa'><summary>&#128203; Ver últimas doses (" + String(mostrar) + ")<span class='seta'></span></summary><div class='conteudo'>";
  html += "<table><tr><th>Dia</th><th>Hora</th><th>Remédio</th><th>Situação</th></tr>";
  for (int k = 0; k < mostrar; k++) {
    int idx = indiceHistorico(k);
    const RegistroDose& r = historico[idx];
    bool emAndamento = registroEmAndamento(idx);
    int c = r.compartimento < NUM_COMPARTIMENTOS ? r.compartimento : 0;
    String nome = String(r.remedio);
    if (nome == "") nome = "Compart. " + String(c + 1);
    html += "<tr><td>" + formatarDataHora(r.inicio, "%d/%m") + "</td>";
    html += "<td>" + formatarDataHora(r.inicio, "%H:%M") + "</td>";
    html += "<td>" + marcaCompartimento(c, 20) + " " + escaparHTML(nome) + "</td>";
    html += "<td style='color:" + String(corStatusDose(r.status, emAndamento)) + "'>" + textoStatusDose(r.status, emAndamento);
    if (r.status == DOSE_ATRASADA) html += " (" + formatarAtraso(r.atrasoSeg) + ")";
    html += "</td></tr>";
  }
  html += "</table>";
  html += "<p><a href='/historico.csv'>&#11015;&#65039; Baixar histórico completo (planilha)</a></p>";
  html += "<button type='button' class='link-perigo' onclick='limparHistorico()'>Apagar histórico</button>";
  html += "</div></details></section>";
  return html;
}

// ---------- MODO DE CONFIGURACAO DE WI-FI ----------

void handleConfigRoot() {
  int redesEncontradas = WiFi.scanNetworks();
  if (redesEncontradas < 0) {
    // A busca falha enquanto a placa ainda tenta a rede antiga: interrompe e repete
    WiFi.disconnect();
    delay(200);
    redesEncontradas = WiFi.scanNetworks();
  }
  if (redesEncontradas < 0) {
    redesEncontradas = 0;
  }

  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>Zelo+ Configuracao</title></head><body style='font-family:sans-serif;max-width:400px;margin:20px auto;padding:0 16px'>";
  html += "<h2>Zelo+ - Conectar ao Wi-Fi</h2>";

  if (configErro != "") {
    html += "<p style='color:red;font-weight:bold'>" + configErro + "</p>";
  }

  html += "<form action='/conectar' method='POST'>";
  html += "<label>Rede Wi-Fi:</label><br>";
  if (redesEncontradas == 0) {
    html += "<p>Nenhuma rede encontrada. O dispenser so enxerga redes de 2,4 GHz.</p>";
  }
  html += "<select name='ssid' style='width:100%;padding:8px;margin:6px 0'>";
  for (int i = 0; i < redesEncontradas; i++) {
    String ssidEscapado = escaparHTML(WiFi.SSID(i));
    html += "<option value='" + ssidEscapado + "'>" + ssidEscapado + "</option>";
  }
  html += "</select><br>";
  html += "<p style='margin:4px 0'><a href='/'>Atualizar lista</a></p>";
  html += "<label>Ou digite o nome da rede:</label><br>";
  html += "<input type='text' name='ssid_manual' autocapitalize='off' style='width:100%;padding:8px;margin:6px 0'><br>";
  html += "<label>Senha da rede:</label><br>";
  html += "<input type='password' name='senha' style='width:100%;padding:8px;margin:6px 0'><br><br>";
  html += "<button type='submit' style='width:100%;padding:12px;background:#2563eb;color:white;border:none;border-radius:6px;font-size:16px'>Conectar</button>";
  html += "</form>";
  // Sem internet: o dispenser funciona na propria rede, com a hora deste aparelho
  html += "<hr style='margin:24px 0'><h3>Sem internet</h3>";
  html += "<p>Use o dispenser na rede dele mesmo (ZeloPlus-Config). A hora vem deste aparelho. Os avisos do Telegram ficam desligados.</p>";
  html += "<form action='/modo-local' method='POST' onsubmit=\"this.epoch.value=Math.floor(Date.now()/1000)\">";
  html += "<input type='hidden' name='epoch' value=''>";
  html += "<button type='submit' style='width:100%;padding:12px;background:#16a34a;color:white;border:none;border-radius:6px;font-size:16px'>Usar sem internet</button>";
  html += "</form>";
  html += "</body></html>";

  server.send(200, "text/html", html);
}

void handleConectar() {
  String ssid = server.arg("ssid_manual");
  ssid.trim();
  if (ssid == "") {
    ssid = server.arg("ssid");
  }
  if (ssid == "") {
    configErro = "Escolha uma rede da lista ou digite o nome dela.";
    server.sendHeader("Location", "/");
    server.send(303);
    return;
  }

  String senha = server.arg("senha");

  temaVisor(COR_AZUL_ESCURO);
  limparVisor();
  escreverLinha(0, "Testando rede...");

  WiFi.begin(ssid.c_str(), senha.c_str());
  unsigned long inicio = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - inicio < 15000) {
    delay(300);
  }

  if (WiFi.status() == WL_CONNECTED) {
    preferences.putString("ssid", ssid);
    preferences.putString("pass", senha);

    String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'></head>";
    html += "<body style='font-family:sans-serif;max-width:400px;margin:20px auto;padding:0 16px'>";
    html += "<h2 style='color:green'>Conectado com sucesso!</h2>";
    html += "<p>O sistema vai reiniciar. Conecte seu celular de volta na rede normal e acesse o IP mostrado no visor do dispenser.</p>";
    html += "</body></html>";
    server.send(200, "text/html", html);

    delay(3000);
    ESP.restart();
  } else {
    configErro = "Nao foi possivel conectar. Verifique a senha e tente novamente.";
    WiFi.disconnect();
    server.sendHeader("Location", "/");
    server.send(303);
  }
}

void handleCaptivePortal() {
  server.sendHeader("Location", "http://192.168.4.1/", true);
  server.send(302, "text/plain", "");
}

void handleRoot();
void iniciarModoNormal();

// Enderecos que o celular consulta para saber se ha internet: todos levam a
// pagina do dispenser, que abre sozinha ao conectar na rede propria.
void registrarPortalCativo() {
  server.on("/generate_204", handleCaptivePortal);
  server.on("/gen_204", handleCaptivePortal);
  server.on("/hotspot-detect.html", handleCaptivePortal);
  server.on("/library/test/success.html", handleCaptivePortal);
  server.on("/ncsi.txt", handleCaptivePortal);
  server.onNotFound(handleCaptivePortal);
}

// Explica por que a rede salva nao conectou (aparece no aviso da pagina).
String descreverFalhaWiFi(wl_status_t situacao) {
  if (situacao == WL_NO_SSID_AVAIL) {
    return "a rede " + redeSalva + " não foi encontrada (o dispenser só enxerga redes de 2,4 GHz)";
  }
  if (situacao == WL_CONNECT_FAILED) {
    return "a rede " + redeSalva + " recusou a conexão (confira a senha)";
  }
  return "não foi possível conectar à rede " + redeSalva;
}

// Sem a rede salva: abre a rede propria ZeloPlus (com senha) e segue no modo
// normal. Alarmes, portas e historico funcionam; o Telegram espera a internet.
void iniciarModoLocal(const String& motivo) {
  modoLocal = true;
  nomeRedeLocal = NOME_REDE_LOCAL;
  motivoSemInternet = motivo;
  WiFi.setAutoReconnect(false); // novas tentativas so no horario certo (manterRedeLocal)
  WiFi.disconnect();
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(NOME_REDE_LOCAL, SENHA_REDE_LOCAL);
  dnsServer.start(DNS_PORT, "*", apIP);
  registrarPortalCativo();
  Serial.println("Sem internet: " + motivo);
  iniciarModoNormal();
}

// O endereco "/" mostra a configuracao ou, depois de "Usar sem internet", a
// pagina do dispenser.
void handleInicio() {
  if (modoConfig) {
    handleConfigRoot();
  } else {
    handleRoot();
  }
}

// "Usar sem internet": acerta o relogio com a hora do celular e liga o modo
// normal, mantendo a rede ZeloPlus-Config no ar.
void handleModoLocal() {
  uint32_t segundos = strtoul(server.arg("epoch").c_str(), nullptr, 10);
  if (segundos >= HORA_MINIMA_VALIDA) {
    struct timeval agora = { (time_t)segundos, 0 };
    settimeofday(&agora, nullptr);
    if (rtcPresente) rtc.adjust(DateTime(segundos));
  } else if (!horaDoRtc) {
    configErro = "Nao foi possivel ler a hora deste aparelho. Tente novamente.";
    server.sendHeader("Location", "/");
    server.send(303);
    return;
  }

  modoLocal = true;
  nomeRedeLocal = "ZeloPlus-Config";
  server.sendHeader("Location", "/");
  server.send(303);
  iniciarModoNormal();
}

void iniciarModoConfig() {
  modoConfig = true;

  WiFi.disconnect();  // para de tentar a rede salva, senao a busca de redes falha
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP("ZeloPlus-Config");

  dnsServer.start(DNS_PORT, "*", apIP);

  temaVisor(COR_AZUL_ESCURO);
  limparVisor();
  escreverLinha(0, "Conecte no Wi-Fi");
  escreverLinha(1, "ZeloPlus-Config");

  server.on("/", handleInicio);
  server.on("/conectar", HTTP_POST, handleConectar);
  server.on("/modo-local", HTTP_POST, handleModoLocal);
  registrarPortalCativo();

  server.begin();
  servidorNoAr = true;
}

// ---------- MODO NORMAL (rotina do dispenser) ----------

// Visual pensado para pessoas com pouca familiaridade com tecnologia: letras e
// botoes grandes, uma cor por compartimento e blocos recolhidos (so o nome
// aparece; um toque abre os detalhes).
const char* CSS_PAGINA =
  "body{font-family:sans-serif;font-size:18px;line-height:1.4;max-width:480px;margin:0 auto;padding:10px 14px 40px;color:#1f2937;background:#f3f4f6}"
  "h1{font-size:30px;margin:8px 0 0;color:#1e3a8a}"
  "h2{font-size:21px;margin:0 0 10px}"
  ".bloco{background:#fff;border-radius:14px;padding:14px;margin:14px 0;box-shadow:0 1px 3px rgba(0,0,0,.15)}"
  "label{display:block;font-weight:bold;margin:12px 0 4px}"
  "input[type=text],input[type=tel],input[type=password],input[type=time]{width:100%;box-sizing:border-box;font-size:18px;padding:12px;border:2px solid #9ca3af;border-radius:10px;background:#fff;margin-bottom:6px}"
  "button{font-size:18px;min-height:52px;border-radius:12px;border:none;width:100%;margin-top:10px;cursor:pointer}"
  ".principal{background:#2563eb;color:#fff;font-weight:bold}"
  ".secundario{background:#e5e7eb;color:#111827}"
  ".reposicao{background:#f59e0b;color:#fff;font-weight:bold}"
  ".perigo{background:#dc2626;color:#fff;font-weight:bold}"
  ".link-perigo{background:none;color:#b91c1c;min-height:0;width:auto;padding:8px 0;text-decoration:underline;font-size:16px}"
  "details{border-radius:12px;margin:10px 0}"
  "summary{list-style:none;cursor:pointer;padding:12px;border-radius:12px;display:flex;align-items:center;gap:12px}"
  "summary::-webkit-details-marker{display:none}"
  ".seta{margin-left:auto;font-size:14px;color:#4b5563;white-space:nowrap}"
  ".seta::after{content:'abrir \\25BE'}"
  "details[open]>summary .seta::after{content:'fechar \\25B4'}"
  ".conteudo{padding:0 14px 14px}"
  ".num{border-radius:50%;color:#fff;font-weight:bold;display:inline-flex;align-items:center;justify-content:center;flex:none;vertical-align:middle}"
  ".comp{border:3px solid var(--cor);background:var(--fundo)}"
  "summary small{display:block;color:#4b5563;font-size:15px}"
  ".dica{background:#fff;border:2px dashed var(--cor);border-radius:10px;padding:10px;margin-top:12px}"
  ".pessoa,.caixa{border:2px solid #d1d5db;background:#fff}"
  ".aviso{background:#fef3c7;border:2px solid #f59e0b;border-radius:12px;padding:12px;margin:12px 0}"
  ".aviso a{color:#92400e;font-weight:bold}"
  ".suave{color:#6b7280;font-size:15px}"
  ".adesao{font-size:20px;text-align:center;background:#f0fdf4;border-radius:12px;padding:10px}"
  ".adesao b{font-size:34px;color:#15803d}"
  ".contagem{display:flex;flex-wrap:wrap;gap:4px 14px;font-size:16px}"
  ".linha-remedio{margin:6px 0}"
  "table{width:100%;border-collapse:collapse;font-size:15px}"
  "th{text-align:left;border-bottom:2px solid #d1d5db;padding:4px 2px}"
  "td{border-bottom:1px solid #e5e7eb;padding:6px 2px}"
  ".opcao{display:flex;align-items:center;gap:10px;padding:12px;margin:8px 0;border:3px solid var(--cor);background:var(--fundo);border-radius:12px;font-size:19px}"
  ".opcao input{width:24px;height:24px}"
  ".rodape{text-align:center;margin:24px 0 8px}"
  ".rodape a{color:#374151}"
  ".ok{color:#15803d;font-weight:bold}"
  ".falta{color:#b45309;font-weight:bold}";

String campoTexto(const char* rotulo, const String& nome, const String& valor, const char* tipo,
                  const char* placeholder, bool obrigatorio, const String& extra = "") {
  String html = "<label>" + String(rotulo) + "</label>";
  html += "<input type='" + String(tipo) + "' name='" + nome + "' value='" + escaparHTML(valor) + "'";
  html += " placeholder='" + String(placeholder) + "'" + extra;
  if (obrigatorio) html += " required";
  html += ">";
  return html;
}

String campoHorario(int c, const String& valor) {
  return "<input type='time' name='m" + String(c) + "_h' value='" + valor + "'>";
}

String horariosTexto(const Medicamento& m) {
  String texto;
  for (int i = 0; i < m.totalHorarios; i++) {
    if (i > 0) texto += " &middot; ";
    texto += formatarHorario(m.hora[i], m.minuto[i]);
  }
  return texto;
}

// Cartao recolhido de um compartimento: na faixa colorida aparecem numero,
// remedio e horarios; tocando, abre para editar.
String cartaoCompartimento(int c, bool aberto) {
  const Medicamento& m = medicamentos[c];
  String n = String(c + 1);
  String html = "<details class='comp' style='--cor:" + String(COR_COMPARTIMENTO[c]) + ";--fundo:" +
                String(FUNDO_COMPARTIMENTO[c]) + "'" + (aberto ? " open" : "") + "><summary>";
  html += marcaCompartimento(c, 44);
  if (medicamentoAtivo(c)) {
    html += "<span><b>" + escaparHTML(m.nome) + "</b><small>Compartimento " + n + " (" + NOME_COR[c] + ") &middot; " +
            horariosTexto(m) + "</small></span>";
  } else {
    html += "<span><b>Compartimento " + n + "</b><small>" + NOME_COR[c] + " &middot; vazio &middot; toque para cadastrar</small></span>";
  }
  html += "<span class='seta'></span></summary><div class='conteudo'>";

  html += "<label>Nome do remédio:</label>";
  html += "<input type='text' name='m" + String(c) + "_nome' id='m" + String(c) + "_nome' value='" + escaparHTML(m.nome) +
          "' data-original='" + escaparHTML(m.nome) + "' oninput='atualizarDica(" + String(c) + ")' maxlength='40'>";
  html += "<button type='button' class='secundario' onclick='tirarFoto(" + String(c) + ")'>&#128247; Foto da caixa do remédio</button>";
  html += "<input type='file' accept='image/*' capture='environment' id='foto-" + String(c) + "' style='display:none' onchange='enviarFoto(" + String(c) + ", this)'>";
  html += "<p class='suave' id='foto-status-" + String(c) + "' style='display:none'></p>";
  html += "<label>Horários:</label>";
  html += "<div id='horarios-" + String(c) + "'>";
  for (int i = 0; i < m.totalHorarios; i++) {
    html += campoHorario(c, formatarHorario(m.hora[i], m.minuto[i]));
  }
  if (m.totalHorarios == 0) html += campoHorario(c, "");
  html += "</div>";
  html += "<p class='suave' style='margin:2px 0'>Para tirar um horário, apague-o.</p>";
  html += "<button type='button' class='secundario' onclick='adicionarHorario(" + String(c) + ")'>+ Adicionar horário</button>";
  html += "<div class='dica' id='dica-" + String(c) + "'" + (m.nome == "" ? " style='display:none'" : "") + ">";
  html += "&#128230; Coloque <b>" + escaparHTML(m.nome) + "</b> no <b>compartimento " + n + " (" + NOME_COR[c] + ")</b></div>";
  if (medicamentoAtivo(c)) {
    html += "<button type='button' class='link-perigo' onclick='esvaziarCompartimento(" + String(c) + ")'>Esvaziar o compartimento " + n + "</button>";
  }
  html += "</div></details>";
  return html;
}

// Pessoa recolhida: so o nome aparece; tocando, abre nome e celular.
// O ID do Telegram fica no bloco do Telegram, no fim da pagina.
String cartaoPessoa(const char* icone, const char* papel, const String& prefixo, const Contato& contato,
                    bool obrigatorio, bool familiar, bool aberto) {
  String html = "<details class='pessoa'";
  if (familiar) html += " data-idx='" + prefixo + "'";
  html += aberto ? " open>" : ">";
  html += "<summary><span style='font-size:28px'>" + String(icone) + "</span><span><b>";
  html += contato.nome != "" ? escaparHTML(contato.nome) : String(familiar ? "Novo familiar" : "Cuidador(a)");
  html += "</b><small>" + String(papel) + "</small></span><span class='seta'></span></summary><div class='conteudo'>";
  html += campoTexto("Nome:", prefixo + "_nome", contato.nome, "text", "", obrigatorio);
  html += campoTexto("Celular:", prefixo + "_tel", contato.telefone, "tel", "(11) 98765-4321", obrigatorio);
  if (familiar) {
    html += "<button type='button' class='link-perigo' onclick='removerFamiliar(this)'>" +
            String(contato.nome == "" ? "Cancelar" : "Remover este familiar") + "</button>";
  }
  html += "</div></details>";
  return html;
}

// Telegram so precisa de atencao se faltar o token ou o ID de alguem cadastrado.
bool telegramPendente(String* faltando) {
  bool pendente = (tokenTelegram == "");
  if (cuidador.nome != "" && cuidador.chatId == "") {
    pendente = true;
    if (faltando) *faltando += cuidador.nome;
  }
  for (int i = 0; i < MAX_FAMILIARES; i++) {
    if (familiares[i].nome == "" || familiares[i].chatId != "") continue;
    pendente = true;
    if (faltando) {
      if (*faltando != "") *faltando += ", ";
      *faltando += familiares[i].nome;
    }
  }
  return pendente;
}

String campoIdTelegram(const String& prefixo, const Contato& contato) {
  String html = "<label>" + escaparHTML(contato.nome) + " ";
  html += contato.chatId != "" ? "<span class='ok'>&#10003;</span>" : "<span class='falta'>(falta)</span>";
  html += "</label><input type='text' inputmode='numeric' form='cadastro' name='" + prefixo + "_chat' id='chat-" + prefixo +
          "' value='" + escaparHTML(contato.chatId) + "' placeholder='ID do Telegram, ex.: 123456789'>";
  return html;
}

// Bloco do Telegram (fim da pagina). Aberto so enquanto falta configurar algo.
String blocoTelegram(bool pendente) {
  String html = "<details class='caixa' id='telegram'";
  html += pendente ? " open style='border-color:#f59e0b'>" : ">";
  html += "<summary><span style='font-size:26px'>&#128241;</span><span><b>Avisos pelo Telegram</b><small>";
  html += pendente ? "<span class='falta'>falta configurar</span>" : "<span class='ok'>configurado &#10003;</span>";
  html += "</small></span><span class='seta'></span></summary><div class='conteudo'>";

  html += "<p class='suave'>O Zelo+ avisa o cuidador e os familiares pelo Telegram quando o paciente não pega o remédio.</p>";
  html += "<label>Token do bot:</label>";
  html += "<input type='password' form='cadastro' name='tg_token' id='tg_token' autocomplete='off' placeholder='";
  html += tokenTelegram != "" ? "(salvo &mdash; deixe em branco para manter)" : "cole aqui o token do @BotFather";
  html += "'>";
  html += "<button type='button' class='secundario' onclick='buscarIdsTelegram()'>&#128269; Buscar IDs do Telegram</button>";
  html += "<pre id='ids-telegram' style='white-space:pre-wrap;font-size:15px;background:#f3f4f6;padding:10px;border-radius:10px;display:none'></pre>";

  bool alguem = false;
  if (cuidador.nome != "") {
    html += campoIdTelegram("cuid", cuidador);
    alguem = true;
  }
  for (int i = 0; i < MAX_FAMILIARES; i++) {
    if (familiares[i].nome == "") continue;
    html += campoIdTelegram(prefixoFamiliar(i), familiares[i]);
    alguem = true;
  }
  if (!alguem) html += "<p class='suave'>Cadastre o cuidador e salve; depois volte aqui para ligar os avisos.</p>";

  html += "<button type='submit' form='cadastro' class='principal'>&#128190; Salvar</button>";
  if (!pendente) {
    html += "<button type='button' class='secundario' onclick='testarMensagens()'>&#9993;&#65039; Enviar mensagem de teste</button>";
  }
  html += "<details class='caixa'><summary>&#10067; Como configurar (passo a passo)<span class='seta'></span></summary><div class='conteudo'><ol>";
  html += "<li><b>Uma vez só:</b> no Telegram, abra o <b>@BotFather</b>, envie <i>/newbot</i>, escolha um nome e um usuário terminado em <i>bot</i>. Copie o <b>token</b> e cole acima.</li>";
  html += "<li><b>Cada pessoa</b> procura esse bot no Telegram e toca em <b>Iniciar</b>.</li>";
  html += "<li>Toque em <b>Buscar IDs do Telegram</b> e copie o ID de cada pessoa para o campo dela.</li>";
  html += "<li>Toque em <b>Salvar</b>.</li></ol></div></details>";
  html += "</div></details>";
  return html;
}

// Bloco da leitura da caixa por foto (fim da pagina): so pede a chave da IA.
String blocoIA() {
  String html = "<details class='caixa' id='leitura-foto'>";
  html += "<summary><span style='font-size:26px'>&#128247;</span><span><b>Leitura da caixa por foto</b><small>";
  html += chaveIA != "" ? "<span class='ok'>configurada &#10003;</span>" : "<span class='falta'>falta a chave da IA</span>";
  html += "</small></span><span class='seta'></span></summary><div class='conteudo'>";
  html += "<p class='suave'>No cadastro, o botão <b>Foto da caixa do remédio</b> lê o nome e a concentração impressos na embalagem ";
  html += "usando inteligência artificial (Gemini, do Google). O nome só é preenchido depois da sua confirmação. Precisa de internet.</p>";
  html += "<label>Chave da API do Gemini:</label>";
  html += "<input type='password' form='cadastro' name='ia_chave' autocomplete='off' placeholder='";
  html += chaveIA != "" ? "(salva &mdash; deixe em branco para manter)" : "cole aqui a chave criada no Google AI Studio";
  html += "'>";
  html += "<label>Modelo:</label>";
  html += "<input type='text' form='cadastro' name='ia_modelo' autocomplete='off' autocapitalize='off' value='" + escaparHTML(modeloIA) + "'>";
  html += "<button type='submit' form='cadastro' class='principal'>&#128190; Salvar</button>";
  html += "<details class='caixa'><summary>&#10067; Como obter a chave<span class='seta'></span></summary><div class='conteudo'><ol>";
  html += "<li>Acesse <b>aistudio.google.com</b> com uma conta Google.</li>";
  html += "<li>Toque em <b>Get API key</b> e crie uma chave (a camada gratuita não pede cartão).</li>";
  html += "<li>Cole a chave acima e toque em <b>Salvar</b>. O campo Modelo já vem preenchido.</li></ol></div></details>";
  html += "</div></details>";
  return html;
}

void handleRoot() {
  int ativos = totalMedicamentosAtivos();
  String faltando;
  bool pendente = telegramPendente(&faltando);

  String html = "<!DOCTYPE html><html lang='pt-BR'><head><meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>Zelo+</title><style>" + String(CSS_PAGINA) + "</style></head><body>";
  html += "<h1>Zelo+</h1>";

  if (modoLocal) {
    html += "<div class='aviso'>&#128246; <b>Sem internet</b>";
    if (motivoSemInternet != "") html += ": " + escaparHTML(motivoSemInternet);
    html += ". O dispenser está funcionando na rede própria <b>" + nomeRedeLocal + "</b>";
    if (nomeRedeLocal == NOME_REDE_LOCAL) html += " (senha <b>" + String(SENHA_REDE_LOCAL) + "</b>)";
    html += ". Alarmes, portas e histórico funcionam normalmente; os avisos pelo Telegram ficam desligados até a internet voltar.";
    if (redeSalva != "") {
      html += " O dispenser tenta a rede <b>" + escaparHTML(redeSalva) + "</b> de novo a cada 10 minutos.";
      html += "<button type='button' class='secundario' onclick='tentarWifi()'>&#128260; Tentar conectar novamente</button>";
    }
    html += "<a href='/trocar-wifi' onclick=\"return confirm('O dispenser vai esquecer a rede Wi-Fi e reiniciar no modo de configuração. Continuar?')\">&#128246; Trocar rede Wi-Fi</a>";
    html += "</div>";
  }

  if (pendente && cuidador.nome != "") {
    html += "<div class='aviso'>&#9888;&#65039; Os avisos pelo Telegram ainda não estão prontos";
    if (faltando != "") html += " (falta: " + escaparHTML(faltando) + ")";
    html += ". <a href='#telegram'>Configurar agora</a></div>";
  }

  html += "<form id='cadastro' action='/salvar' method='POST' onsubmit='return enviarCadastro(event)'>";

  html += "<details class='pessoa' style='margin:14px 0'" + String(nomePaciente == "" ? " open" : "") + "><summary><span style='font-size:28px'>&#129491;</span><span><b>";
  html += nomePaciente != "" ? escaparHTML(nomePaciente) : String("Paciente");
  html += "</b><small>Paciente</small></span><span class='seta'></span></summary><div class='conteudo'>";
  html += campoTexto("Nome do paciente:", "paciente", nomePaciente, "text", "", true);
  html += "</div></details>";

  html += "<section class='bloco'><h2>&#128138; Remédios</h2>";
  html += "<p class='suave' style='margin-top:0'>Cada remédio fica sempre no mesmo compartimento, identificado pela cor. Toque em um compartimento para ver ou mudar.</p>";
  for (int c = 0; c < NUM_COMPARTIMENTOS; c++) {
    html += cartaoCompartimento(c, ativos == 0 && c == 0);
  }
  html += "</section>";

  html += "<section class='bloco'><h2>&#128101; Contatos</h2>";
  html += cartaoPessoa("&#128100;", cuidador.nome == "" ? "obrigatório &middot; toque para cadastrar" : "Cuidador(a)", "cuid", cuidador, true, false, cuidador.nome == "");
  html += "<div id='familiares-container'>";
  int totalFamiliares = 0;
  for (int i = 0; i < MAX_FAMILIARES; i++) {
    if (familiares[i].nome == "" && familiares[i].telefone == "") continue;
    html += cartaoPessoa("&#128106;", "Familiar", prefixoFamiliar(i), familiares[i], false, true, false);
    totalFamiliares++;
  }
  html += "</div>";
  html += "<template id='modelo-familiar'>" +
          cartaoPessoa("&#128106;", "Familiar &middot; opcional", "famX", Contato(), false, true, true) + "</template>";
  html += "<button type='button' class='secundario' id='btn-familiar' onclick='adicionarFamiliar()'" +
          String(totalFamiliares >= MAX_FAMILIARES ? " style='display:none'" : "") + ">+ Cadastrar familiar</button>";
  html += "</section>";

  html += "<button type='submit' class='principal' style='min-height:60px;font-size:20px'>&#128190; Salvar alterações</button>";
  html += "</form>";

  // Reposicao: escolhe o remedio, confirma e abre so o compartimento dele.
  if (ativos > 0) {
    html += "<section class='bloco'><h2>&#128260; Repor remédio</h2>";
    html += "<div id='repor1'><button type='button' class='reposicao' onclick='mostrarRepor(2)'>Abrir compartimento para reposição</button></div>";
    html += "<div id='repor2' style='display:none'><p style='margin:0'><b>Qual remédio você vai repor?</b></p>";
    for (int c = 0; c < NUM_COMPARTIMENTOS; c++) {
      if (!medicamentoAtivo(c)) continue;
      html += "<label class='opcao' style='--cor:" + String(COR_COMPARTIMENTO[c]) + ";--fundo:" + String(FUNDO_COMPARTIMENTO[c]) + "'>";
      html += "<input type='radio' name='repor' value='" + String(c) + "' data-nome='" + escaparHTML(medicamentos[c].nome) + "'>";
      html += marcaCompartimento(c, 32) + " " + escaparHTML(medicamentos[c].nome) + "</label>";
    }
    html += "<button type='button' class='reposicao' onclick='continuarRepor()'>Continuar</button>";
    html += "<button type='button' class='secundario' onclick='mostrarRepor(1)'>Cancelar</button></div>";
    html += "<div id='repor3' style='display:none'><p id='repor-pergunta' style='margin:0;font-size:20px'></p>";
    html += "<button type='button' class='perigo' onclick='confirmarRepor()'>Sim, abrir agora</button>";
    html += "<button type='button' class='secundario' onclick='mostrarRepor(1)'>Cancelar</button></div>";
    html += "</section>";
  }

  html += htmlHistorico();

  html += "<p class='rodape'><a href='/trocar-wifi' onclick=\"return confirm('O dispenser vai esquecer a rede Wi-Fi e reiniciar no modo de configuração. Continuar?')\">&#128246; Trocar rede Wi-Fi</a></p>";
  html += "<p class='rodape' style='font-size:15px;color:#6b7280;margin-top:0'>&#128339; Relógio DS3231: ";
  html += rtcPresente ? "conectado" : "não instalado (hora pela internet)";
  html += "</p>";
  html += blocoTelegram(pendente);
  html += blocoIA();

  html += "<script>";
  html += "var TEM_CHAVE_IA = " + String(chaveIA != "" ? "true" : "false") + ";";
  // Sem hora (sem internet e sem DS3231): usa a hora deste aparelho ao abrir a pagina.
  html += "var HORA_DESCONHECIDA = " + String(time(nullptr) < (time_t)HORA_MINIMA_VALIDA ? "true" : "false") + ";";
  html += "if (HORA_DESCONHECIDA) {";
  html += "  var corpoHora = new URLSearchParams(); corpoHora.append('epoch', Math.floor(Date.now() / 1000));";
  html += "  fetch('/acertar-hora', {method:'POST', body: corpoHora}).then(function(r){ return r.text(); })";
  html += "    .then(function(t){ if (t == 'OK') location.reload(); });";
  html += "}";
  html += "function tentarWifi() {";
  html += "  fetch('/tentar-wifi', {method:'POST'}).then(function(r){ return r.text(); })";
  html += "    .then(function(t){ alert(t); setTimeout(function(){ location.reload(); }, 25000); })";
  html += "    .catch(function(){ alert('Não foi possível falar com o dispenser.'); });";
  html += "}";
  html += "var NUM_COMPARTIMENTOS = " + String(NUM_COMPARTIMENTOS) + ";";
  html += "var MAX_HORARIOS = " + String(MAX_HORARIOS) + ";";
  html += "var MAX_FAMILIARES = " + String(MAX_FAMILIARES) + ";";
  html += "var CORES = ['" + String(NOME_COR[0]) + "', '" + String(NOME_COR[1]) + "', '" + String(NOME_COR[2]) + "'];";
  html += "function adicionarHorario(c) {";
  html += "  var container = document.getElementById('horarios-' + c);";
  html += "  if (container.querySelectorAll('input').length >= MAX_HORARIOS) { alert('Máximo de ' + MAX_HORARIOS + ' horários por remédio.'); return; }";
  html += "  var input = document.createElement('input');";
  html += "  input.type = 'time'; input.name = 'm' + c + '_h';";
  html += "  container.appendChild(input); input.focus();";
  html += "}";
  html += "function atualizarDica(c) {";
  html += "  var nome = document.getElementById('m' + c + '_nome').value.trim();";
  html += "  var dica = document.getElementById('dica-' + c);";
  html += "  dica.style.display = nome ? 'block' : 'none';";
  html += "  dica.innerHTML = '';";
  html += "  dica.appendChild(document.createTextNode('\\uD83D\\uDCE6 Coloque '));";
  html += "  var b1 = document.createElement('b'); b1.textContent = nome; dica.appendChild(b1);";
  html += "  dica.appendChild(document.createTextNode(' no '));";
  html += "  var b2 = document.createElement('b'); b2.textContent = 'compartimento ' + (c + 1) + ' (' + CORES[c] + ')'; dica.appendChild(b2);";
  html += "}";
  // Troca de remedio num compartimento ja usado: pede para retirar os antigos.
  html += "function confirmarTrocas() {";
  html += "  for (var c = 0; c < NUM_COMPARTIMENTOS; c++) {";
  html += "    var campo = document.getElementById('m' + c + '_nome');";
  html += "    var antigo = campo.dataset.original.trim(); var novo = campo.value.trim();";
  html += "    if (antigo && novo && antigo.toLowerCase() != novo.toLowerCase()) {";
  html += "      if (!confirm('O compartimento ' + (c + 1) + ' (' + CORES[c] + ') tinha ' + antigo + '. Retire todos os comprimidos antigos antes de colocar ' + novo + '. Confirmar troca?')) return false;";
  html += "    }";
  html += "  }";
  html += "  return true;";
  html += "}";
  // Envia o cadastro com fetch (como os outros comandos): o envio classico de
  // formulario por http faz o navegador mostrar o aviso "informacoes nao protegidas".
  // Foto da caixa: o celular reduz a imagem (ate 800 px) e envia ao dispenser,
  // que consulta a IA. O nome so e preenchido depois da confirmacao.
  html += "function tirarFoto(c) {";
  html += "  if (!TEM_CHAVE_IA) { alert('Para ler a caixa por foto, cole antes a chave da IA no quadro \\u0022Leitura da caixa por foto\\u0022, no fim da página, e salve.'); return; }";
  html += "  document.getElementById('foto-' + c).click();";
  html += "}";
  html += "function enviarFoto(c, entrada) {";
  html += "  var arquivo = entrada.files[0]; if (!arquivo) return;";
  html += "  var status = document.getElementById('foto-status-' + c);";
  html += "  status.style.display = 'block'; status.textContent = 'Lendo a caixa... isso pode levar alguns segundos.';";
  html += "  var endereco = URL.createObjectURL(arquivo); var img = new Image();";
  html += "  img.onerror = function(){ status.textContent = 'Não foi possível abrir a foto.'; entrada.value = ''; };";
  html += "  img.onload = function() {";
  html += "    URL.revokeObjectURL(endereco); entrada.value = '';";
  html += "    comprimirFoto(img, 900, 0.7, function(foto) {";
  html += "      var dados = new FormData(); dados.append('foto', foto, 'caixa.jpg');";
  html += "      fetch('/ler-caixa', {method:'POST', body: dados})";
  html += "      .then(function(r){ return r.json(); })";
  html += "      .then(function(d){";
  html += "        if (!d.ok) { status.textContent = d.erro; return; }";
  html += "        var sugestao = (d.nome + ' ' + d.concentracao).trim().substring(0, 40);";
  html += "        var texto = 'Lido na foto:\\n\\n' + sugestao + (d.codigo ? '\\nCódigo de barras: ' + d.codigo : '') + '\\n\\nConfira com a caixa. Preencher o nome do remédio?';";
  html += "        if (confirm(texto)) {";
  html += "          document.getElementById('m' + c + '_nome').value = sugestao; atualizarDica(c);";
  html += "          status.textContent = 'Nome preenchido pela foto. Confira e toque em Salvar.';";
  html += "        } else { status.textContent = 'Leitura descartada.'; }";
  html += "      })";
  html += "      .catch(function(){ status.textContent = 'Não foi possível falar com o dispenser. Tente de novo.'; });";
  html += "    });";
  html += "  };";
  html += "  img.src = endereco;";
  html += "}";
  // Reduz a foto ate caber no limite do dispenser (cerca de 100 KB).
  html += "function comprimirFoto(img, lado, qualidade, pronto) {";
  html += "  var escala = Math.min(1, lado / Math.max(img.width, img.height));";
  html += "  var tela = document.createElement('canvas');";
  html += "  tela.width = Math.round(img.width * escala); tela.height = Math.round(img.height * escala);";
  html += "  tela.getContext('2d').drawImage(img, 0, 0, tela.width, tela.height);";
  html += "  tela.toBlob(function(foto) {";
  html += "    if (foto.size > 100000 && lado > 500) { comprimirFoto(img, Math.round(lado * 0.8), 0.6, pronto); return; }";
  html += "    pronto(foto);";
  html += "  }, 'image/jpeg', qualidade);";
  html += "}";
  html += "function enviarCadastro(e) {";
  html += "  e.preventDefault();";
  html += "  if (!confirmarTrocas()) return false;";
  html += "  var form = document.getElementById('cadastro');";
  html += "  fetch('/salvar', {method:'POST', body: new URLSearchParams(new FormData(form))})";
  html += "    .then(function(r){ if (r.ok) { location.href = '/'; return; } return r.text().then(function(t){ alert(t); }); })";
  html += "    .catch(function(){ alert('Não foi possível salvar. Verifique a conexão com o dispenser.'); });";
  html += "  return false;";
  html += "}";
  html += "function esvaziarCompartimento(c) {";
  html += "  var nome = document.getElementById('m' + c + '_nome').dataset.original;";
  html += "  if (!confirm('Esvaziar o compartimento ' + (c + 1) + ' (' + CORES[c] + ', ' + nome + ')? O cadastro deste remédio será apagado.')) return;";
  html += "  var abrir = confirm('Abrir o compartimento ' + (c + 1) + ' agora para retirar os comprimidos que sobraram?');";
  html += "  var corpo = new URLSearchParams(); corpo.append('c', c); corpo.append('abrir', abrir ? '1' : '0');";
  html += "  fetch('/esvaziar', {method:'POST', body: corpo}).then(function(r){ return r.text(); })";
  html += "    .then(function(t){ if (t != 'OK') alert(t); location.reload(); });";
  html += "}";
  html += "function mostrarRepor(passo) {";
  html += "  for (var i = 1; i <= 3; i++) { var el = document.getElementById('repor' + i); if (el) el.style.display = (i == passo) ? 'block' : 'none'; }";
  html += "}";
  html += "var reporEscolhido = -1;";
  html += "function continuarRepor() {";
  html += "  var escolha = document.querySelector('input[name=repor]:checked');";
  html += "  if (!escolha) { alert('Toque no remédio que será reposto.'); return; }";
  html += "  reporEscolhido = escolha.value;";
  html += "  var pergunta = document.getElementById('repor-pergunta'); pergunta.innerHTML = '';";
  html += "  pergunta.appendChild(document.createTextNode('Abrir o '));";
  html += "  var b1 = document.createElement('b'); b1.textContent = 'compartimento ' + (Number(reporEscolhido) + 1) + ' (' + CORES[reporEscolhido] + ')'; pergunta.appendChild(b1);";
  html += "  pergunta.appendChild(document.createTextNode(' para repor '));";
  html += "  var b2 = document.createElement('b'); b2.textContent = escolha.dataset.nome; pergunta.appendChild(b2);";
  html += "  pergunta.appendChild(document.createTextNode('?'));";
  html += "  mostrarRepor(3);";
  html += "}";
  html += "function confirmarRepor() {";
  html += "  var corpo = new URLSearchParams(); corpo.append('c', reporEscolhido);";
  html += "  fetch('/abrir-manual', {method:'POST', body: corpo}).then(function(r){";
  html += "    if (r.ok) { alert('Compartimento ' + (Number(reporEscolhido) + 1) + ' aberto! Reponha o remédio e aperte o botão do dispenser para fechar.'); }";
  html += "    else { alert('Não foi possível abrir agora. Tente novamente em instantes.'); }";
  html += "    mostrarRepor(1);";
  html += "  });";
  html += "}";
  // Familiares: cada caixa usa um indice livre (fam0..fam4). Ao remover, o ID do
  // Telegram daquela posicao e apagado para nao passar para um familiar novo.
  html += "function atualizarBotaoFamiliar() {";
  html += "  var total = document.querySelectorAll('#familiares-container .pessoa').length;";
  html += "  document.getElementById('btn-familiar').style.display = total >= MAX_FAMILIARES ? 'none' : 'block';";
  html += "}";
  html += "function limparIdTelegram(idx) { var campo = document.getElementById('chat-' + idx); if (campo) campo.value = ''; }";
  html += "function adicionarFamiliar() {";
  html += "  var usados = [].map.call(document.querySelectorAll('#familiares-container .pessoa'), function(e){ return e.dataset.idx; });";
  html += "  for (var i = 0; i < MAX_FAMILIARES; i++) {";
  html += "    if (usados.indexOf('fam' + i) >= 0) continue;";
  html += "    var modelo = document.getElementById('modelo-familiar').innerHTML.split('famX').join('fam' + i);";
  html += "    document.getElementById('familiares-container').insertAdjacentHTML('beforeend', modelo);";
  html += "    limparIdTelegram('fam' + i);";
  html += "    var campos = document.querySelectorAll('#familiares-container .pessoa:last-child input');";
  html += "    if (campos.length) campos[0].focus();";
  html += "    break;";
  html += "  }";
  html += "  atualizarBotaoFamiliar();";
  html += "}";
  html += "function removerFamiliar(botao) {";
  html += "  var caixa = botao.closest('.pessoa');";
  html += "  var nome = caixa.querySelector('input').value.trim();";
  html += "  if (nome && !confirm('Remover ' + nome + ' dos contatos? (a mudança vale depois de salvar)')) return;";
  html += "  limparIdTelegram(caixa.dataset.idx);";
  html += "  caixa.remove();";
  html += "  atualizarBotaoFamiliar();";
  html += "}";
  html += "function buscarIdsTelegram() {";
  html += "  var saida = document.getElementById('ids-telegram');";
  html += "  saida.style.display = 'block'; saida.textContent = 'Buscando...';";
  html += "  var corpo = new URLSearchParams(); corpo.append('tg_token', document.getElementById('tg_token').value);";
  html += "  fetch('/telegram-ids', {method:'POST', body: corpo}).then(function(r){ return r.text(); })";
  html += "    .then(function(t){ saida.textContent = t; });";
  html += "}";
  html += "function limparHistorico() {";
  html += "  if (!confirm('Apagar todo o histórico de doses?')) return;";
  html += "  fetch('/limpar-historico', {method:'POST'}).then(function(){ location.reload(); });";
  html += "}";
  html += "function testarMensagens() {";
  html += "  alert('Enviando... isso pode levar alguns segundos.');";
  html += "  fetch('/testar-mensagens', {method:'POST'}).then(function(r){ return r.text(); })";
  html += "    .then(function(t){ alert(t); });";
  html += "}";
  html += "</script>";
  html += "</body></html>";
  server.send(200, "text/html", html);
}

// Abre um compartimento para o cuidador (abastecer, repor ou esvaziar).
void iniciarAbastecimento(int c, ModoAbastecimento modo) {
  compartimentoAbastecendo = c;
  modoAbastecimento = modo;

  String titulo;
  if (modo == ABAST_REPOR) titulo = "Repor compartimento " + String(c + 1);
  else if (modo == ABAST_ESVAZIAR) titulo = "Esvaziar compartimento " + String(c + 1);
  else titulo = "Abastecer compartimento " + String(c + 1);

  temaVisor(COR_VISOR_COMPARTIMENTO[c]);
  limparVisor();
  escreverLinha(0, titulo);
  escreverLinha(1, modo == ABAST_ESVAZIAR ? String("Retire tudo") : textoVisor(medicamentos[c].nome, 22, true));
  abrirCompartimento(c);
  mascaraAberta = (uint8_t)(1 << c);
  estadoAtual = ABASTECENDO;
  abastecimentoAbertoEm = millis();
}

bool dispenserLivre() {
  return estadoAtual == AGUARDANDO && mascaraEmEspera == 0;
}

void handleAbrirManual() {
  int c = server.arg("c").toInt();
  if (!server.hasArg("c") || c < 0 || c >= NUM_COMPARTIMENTOS || !medicamentoAtivo(c)) {
    server.send(400, "text/plain", "Compartimento invalido");
    return;
  }
  if (!dispenserLivre()) {
    server.send(409, "text/plain", "Sistema ocupado no momento");
    return;
  }

  server.send(200, "text/plain", "OK");
  iniciarAbastecimento(c, ABAST_REPOR);
}

void handleEsvaziar() {
  int c = server.arg("c").toInt();
  if (!server.hasArg("c") || c < 0 || c >= NUM_COMPARTIMENTOS) {
    server.send(400, "text/plain", "Compartimento invalido");
    return;
  }

  medicamentos[c].nome = "";
  medicamentos[c].totalHorarios = 0;
  filaAbastecimento &= (uint8_t)~(1 << c);
  salvarMedicamento(c);

  if (server.arg("abrir") == "1") {
    if (!dispenserLivre()) {
      server.send(200, "text/plain", "Compartimento esvaziado no cadastro, mas o dispenser esta ocupado e nao abriu agora. Use a reposicao depois, se precisar.");
      return;
    }
    server.send(200, "text/plain", "OK");
    iniciarAbastecimento(c, ABAST_ESVAZIAR);
    return;
  }
  server.send(200, "text/plain", "OK");
}

void handleTestarMensagens() {
  String texto = "Zelo+: mensagem de teste. Você receberá avisos se " + nomePaciente +
                 " não acessar o dispenser no horário do remédio.";
  String resultado = "Resultado do teste:\n";

  Contato* contatos[1 + MAX_FAMILIARES];
  String rotulos[1 + MAX_FAMILIARES];
  contatos[0] = &cuidador;
  rotulos[0] = "Cuidador";
  for (int i = 0; i < MAX_FAMILIARES; i++) {
    contatos[i + 1] = &familiares[i];
    rotulos[i + 1] = "Familiar " + String(i + 1);
  }

  for (int i = 0; i < 1 + MAX_FAMILIARES; i++) {
    const Contato& c = *contatos[i];
    if (c.nome == "") continue;
    resultado += rotulos[i] + " (" + c.nome + "): ";
    if (tokenTelegram == "") {
      resultado += "falta o token do bot\n";
    } else if (c.chatId == "") {
      resultado += "sem ID do Telegram\n";
    } else if (enviarTelegram(c, texto)) {
      resultado += "enviado\n";
    } else {
      resultado += "falhou (confira o ID e se a pessoa tocou em Iniciar no bot)\n";
    }
  }

  server.send(200, "text/plain; charset=utf-8", resultado);
}

// Lista quem mandou mensagem ao bot recentemente (ex.: tocou em "Iniciar"),
// para o cuidador copiar o ID de cada pessoa. O Telegram guarda essas
// mensagens por 24 horas.
void handleTelegramIds() {
  String token = server.arg("tg_token");
  token.trim();
  if (token == "") token = tokenTelegram;
  if (token == "") {
    server.send(200, "text/plain; charset=utf-8", "Cole primeiro o token do bot.");
    return;
  }

  String resposta;
  int codigo = chamarTelegram(token, "getUpdates", "limit=50&allowed_updates=%5B%22message%22%5D", &resposta);
  if (codigo == 401 || codigo == 404) {
    server.send(200, "text/plain; charset=utf-8", "Token invalido. Confira o token enviado pelo @BotFather.");
    return;
  }
  if (codigo != 200) {
    server.send(200, "text/plain; charset=utf-8", "Nao foi possivel falar com o Telegram (HTTP " + String(codigo) + "). Tente de novo.");
    return;
  }

  JsonDocument filtro;
  JsonObject chatFiltro = filtro["result"][0]["message"]["chat"].to<JsonObject>();
  chatFiltro["id"] = true;
  chatFiltro["first_name"] = true;
  chatFiltro["last_name"] = true;
  chatFiltro["username"] = true;

  JsonDocument doc;
  if (deserializeJson(doc, resposta, DeserializationOption::Filter(filtro))) {
    server.send(200, "text/plain; charset=utf-8", "Resposta do Telegram nao reconhecida.");
    return;
  }

  String lista = "";
  String idsVistos = ",";
  for (JsonObject atualizacao : doc["result"].as<JsonArray>()) {
    JsonObject chat = atualizacao["message"]["chat"];
    if (chat.isNull()) continue;
    String id = chat["id"].as<String>();
    if (idsVistos.indexOf("," + id + ",") >= 0) continue;
    idsVistos += id + ",";

    String nome = chat["first_name"] | "";
    String sobrenome = chat["last_name"] | "";
    String usuario = chat["username"] | "";
    if (sobrenome != "") nome += " " + sobrenome;
    if (usuario != "") nome += " (@" + usuario + ")";
    lista += nome + "\nID: " + id + "\n\n";
  }

  if (lista == "") {
    lista = "Ninguem iniciou o bot nas ultimas 24 horas.\nPeca para cada pessoa abrir o bot no Telegram, tocar em Iniciar, e busque de novo.";
  } else {
    lista = "Copie o ID de cada pessoa para o campo dela:\n\n" + lista;
  }
  server.send(200, "text/plain; charset=utf-8", lista);
}

// ---------- LEITURA DA CAIXA POR FOTO (IA) ----------

// Foto recebida da pagina (JPEG). Fica numa unica area de memoria: o ESP32 nao
// comporta duas copias de uma foto de ~80 KB.
const size_t FOTO_MAX = 110000;
uint8_t* fotoRecebida = nullptr;
size_t fotoTamanho = 0;
bool fotoRecusada = false;

void descartarFoto() {
  free(fotoRecebida);
  fotoRecebida = nullptr;
  fotoTamanho = 0;
}

// Recebe a foto em pedacos, conforme ela chega pela rede.
void receberFoto() {
  HTTPUpload& envio = server.upload();
  if (envio.status == UPLOAD_FILE_START) {
    descartarFoto();
    fotoRecusada = false;
  } else if (envio.status == UPLOAD_FILE_WRITE) {
    if (fotoRecusada) return;
    if (fotoTamanho + envio.currentSize > FOTO_MAX) {
      fotoRecusada = true;
      descartarFoto();
      return;
    }
    uint8_t* maior = (uint8_t*)realloc(fotoRecebida, fotoTamanho + envio.currentSize);
    if (maior == nullptr) {
      fotoRecusada = true;
      descartarFoto();
      return;
    }
    fotoRecebida = maior;
    memcpy(fotoRecebida + fotoTamanho, envio.buf, envio.currentSize);
    fotoTamanho += envio.currentSize;
  }
}

// Corpo do pedido a IA: inicio do JSON, foto convertida para base64 no momento
// do envio (sem uma segunda copia na memoria) e fim do JSON.
class CorpoPedidoIA : public Stream {
 public:
  CorpoPedidoIA(const char* inicio, const uint8_t* dados, size_t tamanhoDados, const char* fim)
      : inicio(inicio), fim(fim), dados(dados), tamanhoDados(tamanhoDados),
        tamanhoInicio(strlen(inicio)), tamanhoBase64((tamanhoDados + 2) / 3 * 4), tamanhoFim(strlen(fim)) {}
  size_t tamanho() const { return tamanhoInicio + tamanhoBase64 + tamanhoFim; }
  int available() override { return tamanho() - pos; }
  int read() override {
    int c = peek();
    if (c >= 0) pos++;
    return c;
  }
  int peek() override { return pos < tamanho() ? (uint8_t)caractere(pos) : -1; }
  size_t readBytes(char* buffer, size_t tamanhoMax) {
    size_t lidos = 0;
    while (lidos < tamanhoMax && pos < tamanho()) buffer[lidos++] = caractere(pos++);
    return lidos;
  }
  size_t write(uint8_t) override { return 0; }

 private:
  char caractere(size_t i) const {
    if (i < tamanhoInicio) return inicio[i];
    i -= tamanhoInicio;
    if (i < tamanhoBase64) return base64(i);
    return fim[i - tamanhoBase64];
  }
  char base64(size_t i) const {
    static const char* ALFABETO = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t g = i / 4 * 3;
    int p = i % 4;
    if (p == 2 && g + 1 >= tamanhoDados) return '=';
    if (p == 3 && g + 2 >= tamanhoDados) return '=';
    uint32_t v = (uint32_t)dados[g] << 16;
    if (g + 1 < tamanhoDados) v |= (uint32_t)dados[g + 1] << 8;
    if (g + 2 < tamanhoDados) v |= dados[g + 2];
    return ALFABETO[(v >> (18 - 6 * p)) & 63];
  }
  const char* inicio;
  const char* fim;
  const uint8_t* dados;
  size_t tamanhoDados, tamanhoInicio, tamanhoBase64, tamanhoFim;
  size_t pos = 0;
};

// Instrucoes e formato da resposta pedidos a IA (Gemini). A foto vai entre o
// inicio e o fim; a resposta vem em JSON com os campos do esquema (saida estruturada).
const char* PEDIDO_IA_INICIO =
    "{\"contents\":[{\"parts\":[{\"inline_data\":{\"mime_type\":\"image/jpeg\",\"data\":\"";
const char* PEDIDO_IA_FIM =
    "\"}},{\"text\":\""
    "A imagem mostra a embalagem de um medicamento, fotografada por um cuidador para cadastrar o remédio "
    "num dispenser. Transcreva exatamente como impresso: em nome, o nome do medicamento (nome comercial ou "
    "princípio ativo, o que estiver em destaque); em concentracao, a dosagem (ex.: 50 mg); em codigo_barras, "
    "os dígitos impressos sob o código de barras, se estiverem visíveis. Não deduza o nome a partir do código "
    "de barras nem de outras informações: se o nome não estiver legível na foto, deixe nome vazio e legivel "
    "como false. Campos que não aparecem na foto ficam vazios."
    "\"}]}],\"generationConfig\":{\"responseMimeType\":\"application/json\",\"responseSchema\":{"
    "\"type\":\"OBJECT\",\"properties\":{"
    "\"nome\":{\"type\":\"STRING\"},\"concentracao\":{\"type\":\"STRING\"},"
    "\"codigo_barras\":{\"type\":\"STRING\"},\"legivel\":{\"type\":\"BOOLEAN\"}},"
    "\"required\":[\"nome\",\"concentracao\",\"codigo_barras\",\"legivel\"]}}}";

void responderLeitura(const String& erro, const String& nome = "", const String& concentracao = "",
                      const String& codigo = "") {
  JsonDocument doc;
  doc["ok"] = (erro == "");
  if (erro != "") {
    doc["erro"] = erro;
  } else {
    doc["nome"] = nome;
    doc["concentracao"] = concentracao;
    doc["codigo"] = codigo;
  }
  String saida;
  serializeJson(doc, saida);
  server.send(200, "application/json; charset=utf-8", saida);
}

// Com a foto ja recebida (receberFoto), pergunta a IA o nome do
// remedio e devolve o que foi lido. Quem confirma e preenche e a pagina.
void handleLerCaixa() {
  if (chaveIA == "") {
    descartarFoto();
    responderLeitura("Cadastre antes a chave da IA no quadro Leitura da caixa por foto.");
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    descartarFoto();
    responderLeitura("Sem internet: a leitura por foto precisa de conexão com a internet.");
    return;
  }
  if (fotoRecusada || fotoRecebida == nullptr) {
    descartarFoto();
    responderLeitura("Foto grande demais ou não recebida. Tente de novo.");
    return;
  }
  if (fotoTamanho < 1000 || fotoRecebida[0] != 0xFF || fotoRecebida[1] != 0xD8) {
    descartarFoto();
    responderLeitura("A foto não chegou em formato JPEG. Tente de novo.");
    return;
  }

  if (estadoAtual == AGUARDANDO) {
    temaVisor(COR_AZUL_ESCURO);
    limparVisor();
    escreverLinha(0, "Lendo a caixa...");
  }

  CorpoPedidoIA corpo(PEDIDO_IA_INICIO, fotoRecebida, fotoTamanho, PEDIDO_IA_FIM);

  WiFiClientSecure cliente;
  cliente.setInsecure(); // prototipo: nao valida o certificado do servidor
  HTTPClient http;
  http.setTimeout(40000);
  String resposta;
  int codigo = -1;
  String endereco = "https://generativelanguage.googleapis.com/v1beta/models/" + modeloIA + ":generateContent";
  if (http.begin(cliente, endereco)) {
    http.addHeader("Content-Type", "application/json");
    http.addHeader("x-goog-api-key", chaveIA);
    codigo = http.sendRequest("POST", &corpo, corpo.tamanho());
    if (codigo > 0) resposta = http.getString();
    http.end();
  }
  descartarFoto();
  if (estadoAtual == AGUARDANDO) limparVisor();
  Serial.print("IA (leitura da caixa): HTTP ");
  Serial.println(codigo);

  // Em caso de erro, o Google explica o motivo em error.message; ele aparece na
  // pagina e no Serial Monitor para facilitar o diagnostico.
  String motivo = "";
  if (codigo != 200 && codigo > 0) {
    JsonDocument erro;
    JsonDocument filtroErro;
    filtroErro["error"]["message"] = true;
    filtroErro["error"]["status"] = true;
    if (!deserializeJson(erro, resposta, DeserializationOption::Filter(filtroErro))) {
      motivo = erro["error"]["message"] | "";
      String situacao = erro["error"]["status"] | "";
      if (situacao != "") motivo = situacao + ": " + motivo;
    }
    if (motivo.length() > 220) motivo = motivo.substring(0, 220) + "...";
    Serial.println("IA (leitura da caixa): " + motivo);
  }
  String detalhe = motivo != "" ? " Resposta do Google (" + String(codigo) + "): " + motivo : "";

  if (codigo == 400 && resposta.indexOf("API_KEY_INVALID") >= 0) {
    responderLeitura("Chave da IA recusada: o Google não reconheceu a chave. Confira se foi copiada inteira." + detalhe);
    return;
  }
  if (codigo == 401 || codigo == 403) {
    responderLeitura("Chave da IA recusada: a chave existe, mas não tem permissão para usar o Gemini." + detalhe);
    return;
  }
  if (codigo == 404) {
    responderLeitura("Modelo de IA não encontrado (" + modeloIA + "). Confira o nome do modelo no Google AI Studio.");
    return;
  }
  if (codigo == 429) {
    responderLeitura("Limite de uso da IA atingido por agora. Tente de novo mais tarde.");
    return;
  }
  if (codigo >= 500) {
    responderLeitura("O serviço de IA está ocupado. Tente de novo em instantes.");
    return;
  }
  if (codigo != 200) {
    responderLeitura("Não foi possível falar com o serviço de IA (código " + String(codigo) + "). Tente de novo." + detalhe);
    return;
  }

  JsonDocument filtro;
  filtro["candidates"][0]["finishReason"] = true;
  filtro["candidates"][0]["content"]["parts"][0]["text"] = true;
  filtro["candidates"][0]["content"]["parts"][0]["thought"] = true;
  JsonDocument doc;
  if (deserializeJson(doc, resposta, DeserializationOption::Filter(filtro))) {
    responderLeitura("Resposta da IA não reconhecida. Tente de novo.");
    return;
  }
  JsonObject candidato = doc["candidates"][0];
  String texto = "";
  for (JsonObject parte : candidato["content"]["parts"].as<JsonArray>()) {
    if (parte["thought"] | false) continue; // resumo do raciocinio, nao e a resposta
    texto += parte["text"] | "";
  }
  JsonDocument leitura;
  if (candidato["finishReason"] != "STOP" || texto == "" || deserializeJson(leitura, texto)) {
    responderLeitura("Não foi possível ler a caixa. Tente outra foto.");
    return;
  }

  String nome = leitura["nome"] | "";
  String concentracao = leitura["concentracao"] | "";
  String codigoBarras = leitura["codigo_barras"] | "";
  nome.trim();
  concentracao.trim();
  codigoBarras.trim();
  if (!(leitura["legivel"] | false) || nome == "") {
    if (codigoBarras != "") {
      responderLeitura("Código de barras lido (" + codigoBarras + "), mas o nome do remédio não aparece na foto. "
                       "Fotografe a frente da caixa, onde o nome está escrito.");
    } else {
      responderLeitura("Não foi possível ler o nome do remédio. Tente uma foto mais próxima, com boa luz.");
    }
    return;
  }
  responderLeitura("", nome, concentracao, codigoBarras);
}

void handleHistoricoCSV() {
  String csv = "data;horario;medicamento;compartimento;situacao;atraso_minutos\r\n";
  for (int k = totalHistorico - 1; k >= 0; k--) { // do mais antigo para o mais recente
    int idx = indiceHistorico(k);
    const RegistroDose& r = historico[idx];
    csv += formatarDataHora(r.inicio, "%d/%m/%Y") + ";" + formatarDataHora(r.inicio, "%H:%M") + ";";
    csv += String(r.remedio) + ";" + String(r.compartimento + 1) + ";";
    csv += textoStatusDose(r.status, registroEmAndamento(idx));
    csv += ";";
    if (r.status == DOSE_NO_HORARIO || r.status == DOSE_ATRASADA) csv += String((r.atrasoSeg + 30) / 60);
    csv += "\r\n";
  }
  server.sendHeader("Content-Disposition", "attachment; filename=zelo-historico.csv");
  server.send(200, "text/csv; charset=utf-8", csv);
}

void handleLimparHistorico() {
  totalHistorico = 0;
  proximoHistorico = 0;
  esquecerRegistrosDose();
  memset(historico, 0, sizeof(historico));
  salvarHistorico();
  server.send(200, "text/plain", "OK");
}

void handleTrocarWifi() {
  preferences.remove("ssid");
  preferences.remove("pass");
  server.send(200, "text/plain", "Wi-Fi esquecido. Reiniciando...");
  delay(1500);
  ESP.restart();
}

void lerContatoDoFormulario(const char* prefixo, Contato& contato) {
  String p = prefixo;
  contato.nome = server.arg(p + "_nome");
  contato.telefone = server.arg(p + "_tel");
  contato.chatId = server.arg(p + "_chat");
  contato.nome.trim();
  contato.telefone.trim();
  contato.chatId.trim();
}

// Le do formulario o medicamento de um compartimento. Horarios em branco ou
// repetidos sao ignorados.
void lerMedicamentoDoFormulario(int c, Medicamento& m) {
  String p = prefixoMedicamento(c);
  m.nome = server.arg(p + "_nome");
  m.nome.trim();
  m.totalHorarios = 0;
  String nomeCampo = p + "_h";
  for (int i = 0; i < server.args() && m.totalHorarios < MAX_HORARIOS; i++) {
    if (server.argName(i) != nomeCampo) continue;
    String horario = server.arg(i);
    int separador = horario.indexOf(':');
    if (separador <= 0) continue;
    int hora = horario.substring(0, separador).toInt();
    int minuto = horario.substring(separador + 1).toInt();
    if (hora < 0 || hora > 23 || minuto < 0 || minuto > 59) continue;
    bool repetido = false;
    for (int j = 0; j < m.totalHorarios; j++) {
      if (m.hora[j] == hora && m.minuto[j] == minuto) repetido = true;
    }
    if (repetido) continue;
    m.hora[m.totalHorarios] = hora;
    m.minuto[m.totalHorarios] = minuto;
    m.jaDisparado[m.totalHorarios] = false;
    m.totalHorarios++;
  }
  if (m.nome == "") m.totalHorarios = 0; // compartimento vazio nao toca
}

void handleSalvar() {
  if (!server.hasArg("paciente")) {
    server.send(400, "text/plain", "Dados incompletos");
    return;
  }

  Contato novoCuidador;
  lerContatoDoFormulario("cuid", novoCuidador);
  if (novoCuidador.nome == "" || novoCuidador.telefone == "") {
    server.send(400, "text/plain; charset=utf-8", "O cuidador é obrigatório: preencha nome e celular.");
    return;
  }

  Medicamento novos[NUM_COMPARTIMENTOS];
  int ativos = 0;
  for (int c = 0; c < NUM_COMPARTIMENTOS; c++) {
    lerMedicamentoDoFormulario(c, novos[c]);
    if (novos[c].nome != "" && novos[c].totalHorarios == 0) {
      server.send(400, "text/plain; charset=utf-8",
                  "Informe ao menos um horário para " + novos[c].nome + " (compartimento " + String(c + 1) + "). Volte e corrija.");
      return;
    }
    if (novos[c].nome != "") ativos++;
  }
  if (ativos == 0) {
    server.send(400, "text/plain; charset=utf-8", "Cadastre ao menos um medicamento. Volte e corrija.");
    return;
  }

  // Compartimentos que recebem um medicamento novo (ou trocado) abrem para
  // abastecimento, um de cada vez. So mudar horarios nao abre nada.
  for (int c = 0; c < NUM_COMPARTIMENTOS; c++) {
    // Compara sem diferenciar maiusculas/minusculas nem espacos nas pontas:
    // so um remedio realmente diferente abre o compartimento.
    String antigo = medicamentos[c].nome;
    String novo = novos[c].nome;
    antigo.trim();
    novo.trim();
    antigo.toLowerCase();
    novo.toLowerCase();
    if (novo != "" && novo != antigo) filaAbastecimento |= (uint8_t)(1 << c);
    if (novo == "") filaAbastecimento &= (uint8_t)~(1 << c);
    medicamentos[c] = novos[c];
  }

  cuidador = novoCuidador;

  String novoToken = server.arg("tg_token");
  novoToken.trim();
  if (novoToken != "") tokenTelegram = novoToken; // em branco = mantem o token salvo

  String novaChave = server.arg("ia_chave");
  novaChave.trim();
  if (novaChave != "") chaveIA = novaChave; // em branco = mantem a chave salva

  // Nome do modelo: so letras minusculas, numeros, ponto e hifen (vai na URL).
  String novoModelo = server.arg("ia_modelo");
  novoModelo.trim();
  bool modeloValido = novoModelo.length() > 0 && novoModelo.length() <= 60;
  for (unsigned int i = 0; i < novoModelo.length() && modeloValido; i++) {
    char ch = novoModelo[i];
    modeloValido = (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '.' || ch == '-';
  }
  if (modeloValido) modeloIA = novoModelo;

  nomePaciente = server.arg("paciente");
  nomePaciente.trim();

  // Caixas de familiar deixadas em branco sao ignoradas; os preenchidos sao
  // guardados em sequencia e o restante da lista e limpo.
  int totalFamiliares = 0;
  for (int i = 0; i < MAX_FAMILIARES; i++) {
    Contato familiar;
    lerContatoDoFormulario(prefixoFamiliar(i).c_str(), familiar);
    if (familiar.nome == "" && familiar.telefone == "") continue;
    familiares[totalFamiliares++] = familiar;
  }
  for (int i = totalFamiliares; i < MAX_FAMILIARES; i++) {
    familiares[i] = Contato();
  }

  salvarCadastro();

  server.sendHeader("Location", "/");
  server.send(303);
  // O abastecimento comeca pelo loop(), assim que o dispenser estiver livre.
}

// ---------- ALARME ----------

// Tela do alarme: fundo na cor do compartimento do remedio mostrado, com o
// numero do compartimento em cima e o nome do remedio embaixo.
void mostrarAlarme(int c) {
  temaVisor(COR_VISOR_COMPARTIMENTO[c]);
  limparVisor();
  escreverLinha(0, "Hora do remédio! (C" + String(c + 1) + ")");
}

void iniciarAlarme(uint8_t mascara, const String& horario) {
  estadoAtual = TOCANDO;
  doseMascara = mascara;
  horarioDose = horario;
  nomesDose = juntarNomes(mascara);
  alarmeIniciadoEm = millis();
  nivelAvisoEnviado = 0;
  dosePendente = false;

  esquecerRegistrosDose();
  for (int c = 0; c < NUM_COMPARTIMENTOS; c++) {
    if (temBit(mascara, c)) registroDose[c] = registrarInicioDose(c);
  }
  salvarHistorico();

  Serial.print("Alarme ");
  Serial.print(horario);
  Serial.print(": ");
  Serial.println(nomesDose);
  for (int c = 0; c < NUM_COMPARTIMENTOS; c++) {
    if (temBit(mascara, c)) {
      mostrarAlarme(c);
      break;
    }
  }
}

void desligarLedBuzzer() {
  ledBuzzerLigado = false;
  digitalWrite(LED_PIN, LOW);
  digitalWrite(BUZZER_PIN, LOW);
}

// Chamado quando o paciente aperta o botao (durante o alarme ou com dose
// pendente): abre todos os compartimentos da dose, um logo apos o outro.
void abrirParaPaciente() {
  unsigned long decorrido = millis() - alarmeIniciadoEm;
  atualizarDose(decorrido < TEMPO_PRIMEIRO_AVISO ? DOSE_NO_HORARIO : DOSE_ATRASADA, decorrido);
  esquecerRegistrosDose();

  desligarLedBuzzer();
  dosePendente = false;

  temaVisor(COR_PRETO, COR_BRANCO, COR_AMARELO);
  limparVisor();
  escreverLinha(0, "Retire: " + listaCompartimentos(doseMascara));
  escreverLinha(1, "Abrindo...");
  abrirCompartimentos(doseMascara);

  mascaraAberta = doseMascara;
  estadoAtual = PORTA_ABERTA_ESTADO;
  portaAbertaEm = millis();

  avisarAcessoAposAviso();
}

// Confere a cada segundo se chegou o horario de algum medicamento. Funciona em
// qualquer estado: se o dispenser estiver ocupado, o horario fica em espera e
// toca assim que ele ficar livre.
void verificarHorarios() {
  static unsigned long ultimaVerificacao = 0;
  if (millis() - ultimaVerificacao < 1000) return;
  ultimaVerificacao = millis();

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 10)) return;

  if (timeinfo.tm_min != ultimoMinutoChecado) {
    ultimoMinutoChecado = timeinfo.tm_min;
    for (int c = 0; c < NUM_COMPARTIMENTOS; c++) {
      for (int i = 0; i < medicamentos[c].totalHorarios; i++) {
        if (timeinfo.tm_min != medicamentos[c].minuto[i]) medicamentos[c].jaDisparado[i] = false;
      }
    }
  }

  for (int c = 0; c < NUM_COMPARTIMENTOS; c++) {
    if (!medicamentoAtivo(c)) continue;
    Medicamento& m = medicamentos[c];
    for (int i = 0; i < m.totalHorarios; i++) {
      if (m.jaDisparado[i] || timeinfo.tm_hour != m.hora[i] || timeinfo.tm_min != m.minuto[i]) continue;
      m.jaDisparado[i] = true;
      if (mascaraEmEspera == 0) horarioEmEspera = formatarHorario(m.hora[i], m.minuto[i]);
      mascaraEmEspera |= (uint8_t)(1 << c);
    }
  }
}

void atualizarTelaEspera() {
  static unsigned long ultimaAtualizacao = 0;
  if (millis() - ultimaAtualizacao < 1000) return;
  ultimaAtualizacao = millis();

  // Hora desconhecida (sem internet e sem DS3231): os alarmes nao disparam.
  // O visor pede o acerto e o LED pisca, para ninguem achar que esta tudo normal.
  static bool avisoHoraAtivo = false;
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 10)) {
    if (!avisoHoraAtivo || telaEsperaDesenhada) {
      temaVisor(COR_LARANJA);
      limparVisor();
    }
    avisoHoraAtivo = true;
    escreverLinha(0, "Acerte a hora");
    escreverLinha(1, "pelo celular");
    digitalWrite(LED_PIN, digitalRead(LED_PIN) == HIGH ? LOW : HIGH);
    return;
  }
  if (avisoHoraAtivo) {
    avisoHoraAtivo = false;
    digitalWrite(LED_PIN, LOW);
    telaEsperaDesenhada = false;
  }

  // Hora grande a esquerda; a direita, a situacao das doses numa caixa colorida.
  char horaBuffer[6];
  strftime(horaBuffer, sizeof(horaBuffer), "%H:%M", &timeinfo);

  int ativos = totalMedicamentosAtivos();
  if (dosePendente) {
    desenharTelaEspera(horaBuffer, "Dose", "pendente!", COR_VERMELHO);
  } else if (ativos > 0) {
    desenharTelaEspera(horaBuffer, "Medicação", "em dia", COR_VERDE);
  } else {
    desenharTelaEspera(horaBuffer, "Sem", "remédio", COR_CINZA);
  }
}

// Linha de baixo do visor enquanto a porta esta aberta: "Aguarde..." durante a
// trava e depois a contagem para o fechamento automatico.
void mostrarContagem(unsigned long abertoEm, unsigned long tempoTotal) {
  unsigned long decorrido = millis() - abertoEm;
  if (decorrido < TRAVA_BOTAO) {
    escreverLinha(1, "Aguarde...");
  } else {
    unsigned long restante = (tempoTotal - decorrido) / 1000;
    escreverLinha(1, "Fecha em: " + String(restante) + "s");
  }
}

// ---------- RELOGIO DS3231 ----------

// Chamada pelo NTP (fora do loop) sempre que a hora e acertada pela internet.
void aoSincronizarNTP(struct timeval* tv) {
  ntpSincronizou = true;
}

// Ao ligar: procura o DS3231. Se ele tiver uma hora valida, acerta o relogio do
// ESP32 com ela na hora, sem esperar a internet.
void iniciarRelogioRTC() {
  rtcPresente = rtc.begin();
  if (!rtcPresente) {
    Serial.println("DS3231 nao encontrado - usando so a internet");
    return;
  }
  if (rtc.lostPower()) {
    Serial.println("DS3231 sem hora valida - aguardando a internet");
    return;
  }
  uint32_t segundos = rtc.now().unixtime();
  if (segundos < HORA_MINIMA_VALIDA) {
    Serial.println("DS3231 com data antiga - aguardando a internet");
    return;
  }
  struct timeval agora = { (time_t)segundos, 0 };
  settimeofday(&agora, nullptr);
  horaDoRtc = true;
  Serial.println("DS3231: hora carregada do relogio");
}

// Depois de cada acerto pela internet, grava a hora certa no DS3231. Roda no
// loop(), junto com as demais tarefas do programa.
void gravarHoraNoRTC() {
  if (!ntpSincronizou) return;
  ntpSincronizou = false;
  if (!rtcPresente) return;
  time_t agora = time(nullptr);
  if ((uint32_t)agora < HORA_MINIMA_VALIDA) return;
  rtc.adjust(DateTime((uint32_t)agora));
  Serial.println("DS3231: hora gravada (vinda da internet)");
}

// Comeca uma tentativa na rede salva sem travar o programa; manterRedeLocal()
// acompanha o resultado. Com a rede propria no ar, o ESP32 pode trocar de canal
// durante a tentativa, e quem estiver conectado a ela perde a conexao.
void iniciarTentativaRede() {
  if (redeSalva == "" || tentandoRede) return;
  tentandoRede = true;
  tentativaIniciadaEm = millis();
  WiFi.begin(redeSalva.c_str(), senhaRedeSalva.c_str());
  Serial.println("Tentando a rede " + redeSalva + "...");
}

// A rede salva voltou: desliga a rede propria e segue com internet.
void sairModoLocal() {
  modoLocal = false;
  tentandoRede = false;
  motivoSemInternet = "";
  dnsServer.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  Serial.print("Rede salva conectada. Acesse: http://");
  Serial.println(WiFi.localIP());
  if (estadoAtual == AGUARDANDO) {
    temaVisor(COR_AZUL_ESCURO);
    limparVisor();
    escreverLinha(0, "Endereço da página:");
    escreverLinha(1, WiFi.localIP().toString());
    delay(5000);
    limparVisor();
  }
}

// Na rede propria: tenta a rede salva a cada 10 minutos, so quando ninguem esta
// conectado a rede propria (a tentativa derrubaria essa conexao).
void manterRedeLocal() {
  static unsigned long ultimaTentativaLocal = millis();
  if (tentandoRede) {
    wl_status_t situacao = WiFi.status();
    if (situacao == WL_CONNECTED) {
      sairModoLocal();
      return;
    }
    if (millis() - tentativaIniciadaEm < DURACAO_TENTATIVA) return;
    tentandoRede = false;
    motivoSemInternet = descreverFalhaWiFi(situacao);
    WiFi.disconnect(); // so a conexao com a rede salva; a rede propria continua
    ultimaTentativaLocal = millis();
    Serial.println("Ainda sem internet: " + motivoSemInternet);
    return;
  }
  if (redeSalva == "") return;
  if (millis() - ultimaTentativaLocal < INTERVALO_NOVA_TENTATIVA) return;
  if (WiFi.softAPgetStationNum() > 0) return;
  iniciarTentativaRede();
}

// Botao "Tentar conectar novamente" da pagina.
void handleTentarWifi() {
  if (!modoLocal || redeSalva == "") {
    server.send(200, "text/plain; charset=utf-8", "Não há rede salva para tentar. Use Trocar rede Wi-Fi.");
    return;
  }
  server.send(200, "text/plain; charset=utf-8",
              "Tentando conectar à rede " + redeSalva + ". Se conseguir, a rede " + String(NOME_REDE_LOCAL) +
                  " será desligada: volte o celular para a rede de casa e abra o endereço que aparecer no visor.");
  iniciarTentativaRede();
}

// Sem hora conhecida, a pagina envia a hora do celular ao abrir.
void handleAcertarHora() {
  uint32_t segundos = strtoul(server.arg("epoch").c_str(), nullptr, 10);
  if (time(nullptr) >= (time_t)HORA_MINIMA_VALIDA || segundos < HORA_MINIMA_VALIDA) {
    server.send(200, "text/plain", "MANTIDA");
    return;
  }
  struct timeval agora = { (time_t)segundos, 0 };
  settimeofday(&agora, nullptr);
  if (rtcPresente) rtc.adjust(DateTime(segundos));
  Serial.println("Hora acertada pelo celular");
  server.send(200, "text/plain", "OK");
}

// Com internet, se a conexao cair, tenta a rede salva de novo a cada 30 s. Quando
// conecta, a pagina volta a abrir no mesmo IP e o NTP acerta a hora (e o DS3231).
void manterWiFi() {
  static unsigned long ultimaTentativa = 0;
  static bool estavaConectado = true;
  if (modoLocal) {
    manterRedeLocal();
    return;
  }
  bool conectado = (WiFi.status() == WL_CONNECTED);
  if (conectado && !estavaConectado) {
    Serial.print("Wi-Fi reconectado. Acesse: http://");
    Serial.println(WiFi.localIP());
  }
  estavaConectado = conectado;
  if (conectado || millis() - ultimaTentativa < 30000) return;
  ultimaTentativa = millis();
  WiFi.reconnect();
}

void iniciarModoNormal() {
  modoConfig = false;

  sntp_set_time_sync_notification_cb(aoSincronizarNTP);
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  temaVisor(COR_AZUL_ESCURO);
  limparVisor();
  if (modoLocal) {
    // Visor: nome e senha da rede propria, depois o endereco da pagina.
    escreverLinha(0, "Rede " + nomeRedeLocal);
    escreverLinha(1, nomeRedeLocal == NOME_REDE_LOCAL ? "senha " + String(SENHA_REDE_LOCAL) : "sem senha");
    delay(4000);
    escreverLinha(0, "Endereço da página:");
    escreverLinha(1, "192.168.4.1");
    Serial.println("Sem internet: rede " + nomeRedeLocal + ", acesse http://192.168.4.1");
  } else {
    escreverLinha(0, "Endereço da página:");
    escreverLinha(1, WiFi.localIP().toString());
    Serial.print("Acesse: http://");
    Serial.println(WiFi.localIP());
  }
  delay(6000);
  limparVisor();

  server.on("/", handleRoot);
  server.on("/salvar", HTTP_POST, handleSalvar);
  server.on("/abrir-manual", HTTP_POST, handleAbrirManual);
  server.on("/esvaziar", HTTP_POST, handleEsvaziar);
  server.on("/testar-mensagens", HTTP_POST, handleTestarMensagens);
  server.on("/telegram-ids", HTTP_POST, handleTelegramIds);
  server.on("/ler-caixa", HTTP_POST, handleLerCaixa, receberFoto);
  server.on("/historico.csv", handleHistoricoCSV);
  server.on("/limpar-historico", HTTP_POST, handleLimparHistorico);
  server.on("/trocar-wifi", handleTrocarWifi);
  server.on("/tentar-wifi", HTTP_POST, handleTentarWifi);
  server.on("/acertar-hora", HTTP_POST, handleAcertarHora);
  if (!servidorNoAr) {
    server.begin();
    servidorNoAr = true;
  }
}

void setup() {
  Serial.begin(115200);
#if MODO_TESTE
  Serial.println("ATENCAO: MODO_TESTE ativo (avisos aos 30 s e 60 s)");
#endif

  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  digitalWrite(BUZZER_PIN, LOW);
  digitalWrite(LED_PIN, LOW);

  for (int c = 0; c < NUM_COMPARTIMENTOS; c++) {
    servos[c].setTimerWidth(RESOLUCAO_SERVO);  // precisa vir antes do attach
    servos[c].attach(PINOS_SERVO[c], PULSO_0_GRAU, PULSO_180_GRAUS);
    servos[c].write(ANGULO_FECHADO[c]);
  }

  iniciarVisor();
  escreverLinha(0, "Zelo+");
  escreverLinha(1, "Iniciando...");

  iniciarRelogioRTC();

  prefsCadastro.begin("cadastro", false);
  carregarCadastro();
  prefsHistorico.begin("historico", false);
  carregarHistorico();

  preferences.begin("wifi", false);
  String ssidSalvo = preferences.getString("ssid", "");
  String senhaSalva = preferences.getString("pass", "");
  redeSalva = ssidSalvo;
  senhaRedeSalva = senhaSalva;

  // Botao apertado ao ligar: vai direto para a configuracao (rede ZeloPlus-Config)
  bool forcarConfig = (digitalRead(BUTTON_PIN) == LOW);

  if (forcarConfig) {
    iniciarModoConfig();
    return;
  }

  if (ssidSalvo != "") {
    limparVisor();
    escreverLinha(0, "Zelo+");
    escreverLinha(1, "Conectando...");
    WiFi.begin(ssidSalvo.c_str(), senhaSalva.c_str());

    unsigned long inicio = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - inicio < 15000) {
      delay(300);
    }
  }

  if (WiFi.status() == WL_CONNECTED) {
    iniciarModoNormal();
  } else if (ssidSalvo != "") {
    // Rede salva fora do ar, fora de alcance ou com senha recusada: o dispenser
    // abre a rede propria e continua funcionando (hora pelo DS3231 ou pelo celular).
    iniciarModoLocal(descreverFalhaWiFi(WiFi.status()));
  } else {
    iniciarModoConfig();
  }
}

void loop() {
  if (modoConfig || modoLocal) {
    dnsServer.processNextRequest();
  }
  server.handleClient();

  if (modoConfig) {
    return;
  }

  manterWiFi();
  gravarHoraNoRTC();
  verificarHorarios();

  switch (estadoAtual) {
    case AGUARDANDO:
      if (dosePendente && digitalRead(BUTTON_PIN) == LOW) {
        abrirParaPaciente();
        break;
      }
      // Prioridade: alarme em espera; depois abastecimento de compartimento novo.
      if (mascaraEmEspera != 0) {
        uint8_t mascara = mascaraEmEspera;
        mascaraEmEspera = 0;
        iniciarAlarme(mascara, horarioEmEspera);
        break;
      }
      if (filaAbastecimento != 0) {
        for (int c = 0; c < NUM_COMPARTIMENTOS; c++) {
          if (!temBit(filaAbastecimento, c)) continue;
          filaAbastecimento &= (uint8_t)~(1 << c);
          iniciarAbastecimento(c, ABAST_NOVO);
          break;
        }
        break;
      }
      atualizarTelaEspera();
      break;

    case TOCANDO: {
      unsigned long decorrido = millis() - alarmeIniciadoEm;

      if (digitalRead(BUTTON_PIN) == LOW) {
        abrirParaPaciente();
        break;
      }

      if (decorrido >= TEMPO_ALERTA_FINAL) {
        desligarLedBuzzer();
        temaVisor(COR_VERMELHO);
        limparVisor();
        escreverLinha(0, "Avisando a família");
        atualizarDose(DOSE_SEM_ACESSO, decorrido);
        alertarSemAcesso();
        nivelAvisoEnviado = 2;

        // Para de tocar, mas o botao continua abrindo os compartimentos da dose.
        // Os registros continuam ligados a dose para virar "com atraso" se o
        // paciente ainda aparecer.
        dosePendente = true;
        limparVisor();
        estadoAtual = AGUARDANDO;
        break;
      }

      if (decorrido >= TEMPO_PRIMEIRO_AVISO && nivelAvisoEnviado == 0) {
        desligarLedBuzzer();
        escreverLinha(1, "Avisando o cuidador");
        avisarPrimeiroAtraso();
        nivelAvisoEnviado = 1;
      }

      // Nome e compartimento de cada remedio da dose, alternando a cada 2 s; o
      // fundo do visor fica na cor do compartimento mostrado.
      static unsigned long ultimaTrocaNome = 0;
      static int nomeMostrado = -1;
      if (millis() - ultimaTrocaNome >= 2000 || nomeMostrado < 0 || !temBit(doseMascara, nomeMostrado)) {
        ultimaTrocaNome = millis();
        for (int passo = 1; passo <= NUM_COMPARTIMENTOS; passo++) {
          int c = (nomeMostrado + passo + NUM_COMPARTIMENTOS) % NUM_COMPARTIMENTOS;
          if (!temBit(doseMascara, c)) continue;
          if (c != nomeMostrado) mostrarAlarme(c);
          nomeMostrado = c;
          escreverLinha(1, textoVisor(medicamentos[c].nome, 40, true));
          break;
        }
      }

      bool minutoDeSom = ((decorrido / CICLO_ALARME) % 2) == 0;
      if (!minutoDeSom) {
        if (ledBuzzerLigado) desligarLedBuzzer();
        break;
      }

      if (millis() - ultimoToggleAlarme >= 150) {
        ultimoToggleAlarme = millis();
        ledBuzzerLigado = !ledBuzzerLigado;
        digitalWrite(LED_PIN, ledBuzzerLigado ? HIGH : LOW);
        digitalWrite(BUZZER_PIN, ledBuzzerLigado ? HIGH : LOW);
      }
      break;
    }

    case PORTA_ABERTA_ESTADO: {
      static unsigned long ultimaAtualizacaoContagem = 0;

      bool travaLiberada = (millis() - portaAbertaEm >= TRAVA_BOTAO);
      bool fechar = (travaLiberada && digitalRead(BUTTON_PIN) == LOW) ||
                    (millis() - portaAbertaEm >= TEMPO_PORTA_ABERTA);

      if (fechar) {
        temaVisor(COR_PRETO);
        limparVisor();
        escreverLinha(0, "Fechando a porta...");
        fecharCompartimentos(mascaraAberta);
        mascaraAberta = 0;

        limparVisor();
        estadoAtual = AGUARDANDO;
        break;
      }

      if (millis() - ultimaAtualizacaoContagem >= 500) {
        ultimaAtualizacaoContagem = millis();
        escreverLinha(0, "Retire: " + listaCompartimentos(mascaraAberta));
        mostrarContagem(portaAbertaEm, TEMPO_PORTA_ABERTA);
      }
      break;
    }

    case ABASTECENDO: {
      static unsigned long ultimaAtualizacaoAbastecimento = 0;

      bool travaLiberada = (millis() - abastecimentoAbertoEm >= TRAVA_BOTAO);
      bool fechar = (travaLiberada && digitalRead(BUTTON_PIN) == LOW) ||
                    (millis() - abastecimentoAbertoEm >= TEMPO_ABASTECIMENTO);

      if (fechar) {
        temaVisor(COR_PRETO);
        limparVisor();
        escreverLinha(0, "Fechando a porta...");
        fecharCompartimentos(mascaraAberta);
        mascaraAberta = 0;
        compartimentoAbastecendo = -1;

        limparVisor();
        estadoAtual = AGUARDANDO; // o proximo da fila (se houver) abre pelo loop()
        break;
      }

      // A linha de baixo alterna entre o nome do remedio e a contagem, a cada 2 s,
      // depois da trava.
      if (millis() - ultimaAtualizacaoAbastecimento >= 1000) {
        ultimaAtualizacaoAbastecimento = millis();
        unsigned long decorrido = millis() - abastecimentoAbertoEm;
        bool mostrarNome = !travaLiberada || (decorrido / 2000) % 2 == 0;
        if (mostrarNome) {
          escreverLinha(1, modoAbastecimento == ABAST_ESVAZIAR ? String("Retire tudo")
                                                               : textoVisor(medicamentos[compartimentoAbastecendo].nome, 22, true));
        } else {
          mostrarContagem(abastecimentoAbertoEm, TEMPO_ABASTECIMENTO);
        }
      }
      break;
    }
  }
}
