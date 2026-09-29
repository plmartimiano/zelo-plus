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
#include <LiquidCrystal_I2C.h>
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

LiquidCrystal_I2C lcd(0x27, 16, 2);
Servo servos[NUM_COMPARTIMENTOS];
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
bool modoLocal = false;  // sem internet: o dispenser usa a propria rede (ZeloPlus-Config)
String configErro = "";

// ---------- RELOGIO DS3231 (opcional) ----------
// Modulo de relogio ligado no I2C, junto com o LCD (endereco 0x68). Guarda a hora
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

// Motivo da abertura no estado ABASTECENDO (so muda o texto do LCD).
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
const unsigned long TEMPO_MOVIMENTO_PORTA = 2500;

// Move a porta devagar e sem trancos: comeca lento, acelera no meio e freia no
// fim (curva de cosseno). Um passo a cada 20 ms, o intervalo do sinal do servo.
void moverPorta(int c, int de, int para) {
  const int passos = TEMPO_MOVIMENTO_PORTA / 20;
  for (int i = 1; i <= passos; i++) {
    float t = (float)i / passos;          // 0 -> 1 ao longo do movimento
    float suave = (1 - cos(t * PI)) / 2;  // 0 -> 1, lento no inicio e no fim
    servos[c].write(de + (int)round((para - de) * suave));
    delay(20);
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

// O LCD 16x2 nao tem acentos. Dois caracteres especiais sao desenhados nele ao
// ligar (setup): "ç" no codigo 1 e "ã" no codigo 2 (ex.: "Medicação em dia").
const uint8_t LCD_CEDILHA = 1;
const uint8_t LCD_A_TIL = 2;
uint8_t DESENHO_CEDILHA[8] = {0b00000, 0b01110, 0b10000, 0b10000, 0b10001, 0b01110, 0b00100, 0b01100};
uint8_t DESENHO_A_TIL[8] = {0b01101, 0b10010, 0b01110, 0b00001, 0b01111, 0b10001, 0b01111, 0b00000};

// Prepara o texto para o LCD e corta em 'largura' caracteres. "ç" e "ã" usam os
// caracteres especiais; as outras letras acentuadas perdem o acento. Com
// maiusculas = true (nomes dos remedios), tudo vira maiusculas sem acento
// (ex.: "Losartana Potássica" -> "LOSARTANA POTASSICA").
String textoLCD(const String& texto, unsigned int largura = 16, bool maiusculas = false) {
  String saida;
  for (unsigned int i = 0; i < texto.length() && saida.length() < largura; i++) {
    uint8_t c = (uint8_t)texto[i];
    if (c == 0xC3 && i + 1 < texto.length()) {
      uint8_t original = (uint8_t)texto[++i];
      if (!maiusculas && original == 0xA7) { saida += (char)LCD_CEDILHA; continue; }
      if (!maiusculas && original == 0xA3) { saida += (char)LCD_A_TIL; continue; }
      uint8_t d = original | 0x20; // 0x80-0x9F (maiusculas) -> minusculas
      char base = '?';
      if (d >= 0xA0 && d <= 0xA5) base = 'A';
      else if (d == 0xA7) base = 'C';
      else if (d >= 0xA8 && d <= 0xAB) base = 'E';
      else if (d >= 0xAC && d <= 0xAF) base = 'I';
      else if (d == 0xB1) base = 'N';
      else if (d >= 0xB2 && d <= 0xB6) base = 'O';
      else if (d >= 0xB9 && d <= 0xBC) base = 'U';
      if (!maiusculas && original >= 0xA0 && base != '?') base = (char)tolower(base);
      saida += base;
    } else if (c >= 0x80) {
      // outro caractere especial: pula os bytes de continuacao
      while (i + 1 < texto.length() && ((uint8_t)texto[i + 1] & 0xC0) == 0x80) i++;
      saida += '?';
    } else {
      saida += maiusculas ? (char)toupper(c) : (char)c;
    }
  }
  return saida;
}

void escreverLinhaLCD(int linha, const String& texto) {
  String t = textoLCD(texto);
  while (t.length() < 16) t += ' ';
  lcd.setCursor(0, linha);
  lcd.print(t);
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

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Testando rede...");

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

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Conecte no Wi-Fi");
  lcd.setCursor(0, 1);
  lcd.print("ZeloPlus-Config");

  server.on("/", handleInicio);
  server.on("/conectar", HTTP_POST, handleConectar);
  server.on("/modo-local", HTTP_POST, handleModoLocal);

  server.on("/generate_204", handleCaptivePortal);
  server.on("/gen_204", handleCaptivePortal);
  server.on("/hotspot-detect.html", handleCaptivePortal);
  server.on("/library/test/success.html", handleCaptivePortal);
  server.on("/ncsi.txt", handleCaptivePortal);
  server.onNotFound(handleCaptivePortal);

  server.begin();
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

void handleRoot() {
  int ativos = totalMedicamentosAtivos();
  String faltando;
  bool pendente = telegramPendente(&faltando);

  String html = "<!DOCTYPE html><html lang='pt-BR'><head><meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>Zelo+</title><style>" + String(CSS_PAGINA) + "</style></head><body>";
  html += "<h1>Zelo+</h1>";

  if (modoLocal) {
    html += "<div class='aviso'>&#128246; Modo sem internet: o dispenser funciona na rede ZeloPlus-Config e os avisos pelo Telegram ficam desligados.</div>";
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

  html += "<script>";
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
  if (modo == ABAST_REPOR) titulo = "Repor compart." + String(c + 1);
  else if (modo == ABAST_ESVAZIAR) titulo = "Esvaziar comp." + String(c + 1);
  else titulo = "Abast. compart." + String(c + 1);

  lcd.clear();
  escreverLinhaLCD(0, titulo);
  escreverLinhaLCD(1, modo == ABAST_ESVAZIAR ? String("Retire tudo") : textoLCD(medicamentos[c].nome, 16, true));
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

void mostrarAlarmeLCD() {
  lcd.clear();
  escreverLinhaLCD(0, "Hora do remedio!");
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
  mostrarAlarmeLCD();
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

  lcd.clear();
  escreverLinhaLCD(0, "Retire: " + listaCompartimentos(doseMascara));
  escreverLinhaLCD(1, "Abrindo...");
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

void atualizarLCDRelogio() {
  static unsigned long ultimaAtualizacao = 0;
  if (millis() - ultimaAtualizacao < 1000) return;
  ultimaAtualizacao = millis();

  // Hora desconhecida (sem internet e sem DS3231): os alarmes nao disparam.
  // O LCD pede o acerto e o LED pisca, para ninguem achar que esta tudo normal.
  static bool avisoHoraAtivo = false;
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 10)) {
    avisoHoraAtivo = true;
    escreverLinhaLCD(0, "Acerte a hora");
    escreverLinhaLCD(1, "pelo celular");
    digitalWrite(LED_PIN, digitalRead(LED_PIN) == HIGH ? LOW : HIGH);
    return;
  }
  if (avisoHoraAtivo) {
    avisoHoraAtivo = false;
    digitalWrite(LED_PIN, LOW);
  }

  // Linha 1: hora e minuto, centralizados.
  char horaBuffer[6];
  strftime(horaBuffer, sizeof(horaBuffer), "%H:%M", &timeinfo);
  escreverLinhaLCD(0, String("     ") + horaBuffer);

  int ativos = totalMedicamentosAtivos();
  if (dosePendente) {
    escreverLinhaLCD(1, "Dose pendente!");
  } else if (ativos > 0) {
    escreverLinhaLCD(1, "Medicação em dia");
  } else {
    escreverLinhaLCD(1, "Sem remédio");
  }
}

// Linha 2 do LCD enquanto a porta esta aberta: "Aguarde..." durante a trava e
// depois a contagem para o fechamento automatico.
void mostrarContagemLCD(unsigned long abertoEm, unsigned long tempoTotal) {
  unsigned long decorrido = millis() - abertoEm;
  if (decorrido < TRAVA_BOTAO) {
    escreverLinhaLCD(1, "Aguarde...");
  } else {
    unsigned long restante = (tempoTotal - decorrido) / 1000;
    escreverLinhaLCD(1, "Fecha em: " + String(restante) + "s");
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
// loop() para nao disputar os fios do I2C com o LCD.
void gravarHoraNoRTC() {
  if (!ntpSincronizou) return;
  ntpSincronizou = false;
  if (!rtcPresente) return;
  time_t agora = time(nullptr);
  if ((uint32_t)agora < HORA_MINIMA_VALIDA) return;
  rtc.adjust(DateTime((uint32_t)agora));
  Serial.println("DS3231: hora gravada (vinda da internet)");
}

// Sem internet, tenta a rede salva de novo a cada 30 s. Quando conecta, a pagina
// volta a abrir no mesmo IP e o NTP acerta a hora (e o DS3231).
void manterWiFi() {
  static unsigned long ultimaTentativa = 0;
  static bool estavaConectado = true;
  bool conectado = (WiFi.status() == WL_CONNECTED);
  if (conectado && !estavaConectado) {
    Serial.print("Wi-Fi reconectado. Acesse: http://");
    Serial.println(WiFi.localIP());
  }
  estavaConectado = conectado;
  if (modoLocal) return;  // na rede propria, nao ha rede externa para tentar
  if (conectado || millis() - ultimaTentativa < 30000) return;
  ultimaTentativa = millis();
  WiFi.reconnect();
}

void iniciarModoNormal() {
  modoConfig = false;

  sntp_set_time_sync_notification_cb(aoSincronizarNTP);
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  lcd.clear();
  if (modoLocal) {
    lcd.print("ZeloPlus-Config");
    lcd.setCursor(0, 1);
    lcd.print(apIP);
    Serial.println("Sem internet: rede ZeloPlus-Config, acesse http://192.168.4.1");
  } else if (WiFi.status() == WL_CONNECTED) {
    lcd.print("IP do sistema:");
    lcd.setCursor(0, 1);
    lcd.print(WiFi.localIP());
    Serial.print("Acesse: http://");
    Serial.println(WiFi.localIP());
  } else {
    lcd.print("Sem internet");
    lcd.setCursor(0, 1);
    lcd.print("Hora do relogio");
    Serial.println("Sem internet: alarmes pela hora do DS3231");
  }
  delay(6000);
  lcd.clear();

  server.on("/", handleRoot);
  server.on("/salvar", HTTP_POST, handleSalvar);
  server.on("/abrir-manual", HTTP_POST, handleAbrirManual);
  server.on("/esvaziar", HTTP_POST, handleEsvaziar);
  server.on("/testar-mensagens", HTTP_POST, handleTestarMensagens);
  server.on("/telegram-ids", HTTP_POST, handleTelegramIds);
  server.on("/historico.csv", handleHistoricoCSV);
  server.on("/limpar-historico", HTTP_POST, handleLimparHistorico);
  server.on("/trocar-wifi", handleTrocarWifi);
  if (!modoLocal) {
    server.begin();  // no modo sem internet o servidor ja esta no ar
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
    servos[c].attach(PINOS_SERVO[c]);
    servos[c].write(ANGULO_FECHADO[c]);
  }

  lcd.init();
  lcd.backlight();
  lcd.createChar(LCD_CEDILHA, DESENHO_CEDILHA);
  lcd.createChar(LCD_A_TIL, DESENHO_A_TIL);
  lcd.print("Iniciando...");

  iniciarRelogioRTC();

  prefsCadastro.begin("cadastro", false);
  carregarCadastro();
  prefsHistorico.begin("historico", false);
  carregarHistorico();

  preferences.begin("wifi", false);
  String ssidSalvo = preferences.getString("ssid", "");
  String senhaSalva = preferences.getString("pass", "");

  // Botao apertado ao ligar: vai direto para a configuracao (rede ZeloPlus-Config)
  bool forcarConfig = (digitalRead(BUTTON_PIN) == LOW);

  if (forcarConfig) {
    iniciarModoConfig();
    return;
  }

  if (ssidSalvo != "") {
    lcd.clear();
    lcd.print("Conectando...");
    WiFi.begin(ssidSalvo.c_str(), senhaSalva.c_str());

    unsigned long inicio = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - inicio < 15000) {
      delay(300);
    }
  }

  if (WiFi.status() == WL_CONNECTED) {
    iniciarModoNormal();
  } else if (ssidSalvo != "" && horaDoRtc) {
    // Rede conhecida fora do ar, mas o DS3231 deu a hora: os alarmes funcionam
    // e o Wi-Fi continua sendo tentado em segundo plano (manterWiFi).
    iniciarModoNormal();
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
      atualizarLCDRelogio();
      break;

    case TOCANDO: {
      unsigned long decorrido = millis() - alarmeIniciadoEm;

      if (digitalRead(BUTTON_PIN) == LOW) {
        abrirParaPaciente();
        break;
      }

      if (decorrido >= TEMPO_ALERTA_FINAL) {
        desligarLedBuzzer();
        lcd.clear();
        escreverLinhaLCD(0, "Avisando familia");
        atualizarDose(DOSE_SEM_ACESSO, decorrido);
        alertarSemAcesso();
        nivelAvisoEnviado = 2;

        // Para de tocar, mas o botao continua abrindo os compartimentos da dose.
        // Os registros continuam ligados a dose para virar "com atraso" se o
        // paciente ainda aparecer.
        dosePendente = true;
        lcd.clear();
        estadoAtual = AGUARDANDO;
        break;
      }

      if (decorrido >= TEMPO_PRIMEIRO_AVISO && nivelAvisoEnviado == 0) {
        desligarLedBuzzer();
        escreverLinhaLCD(1, "Avisando cuidad.");
        avisarPrimeiroAtraso();
        nivelAvisoEnviado = 1;
      }

      // Linha 2: nome e compartimento de cada remedio da dose, alternando a cada 2 s.
      static unsigned long ultimaTrocaNome = 0;
      static int nomeMostrado = -1;
      if (millis() - ultimaTrocaNome >= 2000 || nomeMostrado < 0 || !temBit(doseMascara, nomeMostrado)) {
        ultimaTrocaNome = millis();
        for (int passo = 1; passo <= NUM_COMPARTIMENTOS; passo++) {
          int c = (nomeMostrado + passo + NUM_COMPARTIMENTOS) % NUM_COMPARTIMENTOS;
          if (!temBit(doseMascara, c)) continue;
          nomeMostrado = c;
          String sufixo = " (C" + String(c + 1) + ")";
          String nomeCurto = textoLCD(medicamentos[c].nome, 16 - sufixo.length(), true);
          nomeCurto.trim();
          escreverLinhaLCD(1, nomeCurto + sufixo);
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
        lcd.clear();
        escreverLinhaLCD(0, "Fechando porta..");
        fecharCompartimentos(mascaraAberta);
        mascaraAberta = 0;

        lcd.clear();
        estadoAtual = AGUARDANDO;
        break;
      }

      if (millis() - ultimaAtualizacaoContagem >= 500) {
        ultimaAtualizacaoContagem = millis();
        escreverLinhaLCD(0, "Retire: " + listaCompartimentos(mascaraAberta));
        mostrarContagemLCD(portaAbertaEm, TEMPO_PORTA_ABERTA);
      }
      break;
    }

    case ABASTECENDO: {
      static unsigned long ultimaAtualizacaoAbastecimento = 0;

      bool travaLiberada = (millis() - abastecimentoAbertoEm >= TRAVA_BOTAO);
      bool fechar = (travaLiberada && digitalRead(BUTTON_PIN) == LOW) ||
                    (millis() - abastecimentoAbertoEm >= TEMPO_ABASTECIMENTO);

      if (fechar) {
        lcd.clear();
        escreverLinhaLCD(0, "Fechando...");
        fecharCompartimentos(mascaraAberta);
        mascaraAberta = 0;
        compartimentoAbastecendo = -1;

        lcd.clear();
        estadoAtual = AGUARDANDO; // o proximo da fila (se houver) abre pelo loop()
        break;
      }

      // Linha 2 alterna entre o nome do remedio e a contagem, a cada 2 s,
      // depois da trava.
      if (millis() - ultimaAtualizacaoAbastecimento >= 1000) {
        ultimaAtualizacaoAbastecimento = millis();
        unsigned long decorrido = millis() - abastecimentoAbertoEm;
        bool mostrarNome = !travaLiberada || (decorrido / 2000) % 2 == 0;
        if (mostrarNome) {
          escreverLinhaLCD(1, modoAbastecimento == ABAST_ESVAZIAR ? String("Retire tudo")
                                                                  : textoLCD(medicamentos[compartimentoAbastecendo].nome, 16, true));
        } else {
          mostrarContagemLCD(abastecimentoAbertoEm, TEMPO_ABASTECIMENTO);
        }
      }
      break;
    }
  }
}
