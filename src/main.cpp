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
String configErro = "";

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

void abrirCompartimento(int c) {
  for (int angulo = ANGULO_FECHADO[c]; angulo <= ANGULO_ABERTO[c]; angulo++) {
    servos[c].write(angulo);
    delay(15);
  }
}

void fecharCompartimento(int c) {
  for (int angulo = ANGULO_ABERTO[c]; angulo >= ANGULO_FECHADO[c]; angulo--) {
    servos[c].write(angulo);
    delay(15);
  }
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

// O LCD nao mostra acentos: converte para maiusculas sem acento (ex.: "Losartana
// Potássica" -> "LOSARTANA POTASSICA") e corta em 'largura' caracteres.
String textoLCD(const String& texto, unsigned int largura = 16) {
  String saida;
  for (unsigned int i = 0; i < texto.length() && saida.length() < largura; i++) {
    uint8_t c = (uint8_t)texto[i];
    if (c == 0xC3 && i + 1 < texto.length()) {
      uint8_t d = (uint8_t)texto[++i] | 0x20; // 0x80-0x9F (maiusculas) -> minusculas
      char base = '?';
      if (d >= 0xA0 && d <= 0xA5) base = 'A';
      else if (d == 0xA7) base = 'C';
      else if (d >= 0xA8 && d <= 0xAB) base = 'E';
      else if (d >= 0xAC && d <= 0xAF) base = 'I';
      else if (d == 0xB1) base = 'N';
      else if (d >= 0xB2 && d <= 0xB6) base = 'O';
      else if (d >= 0xB9 && d <= 0xBC) base = 'U';
      saida += base;
    } else if (c >= 0x80) {
      // outro caractere especial: pula os bytes de continuacao
      while (i + 1 < texto.length() && ((uint8_t)texto[i + 1] & 0xC0) == 0x80) i++;
      saida += '?';
    } else {
      saida += (char)toupper(c);
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

String htmlHistorico() {
  String html = "<hr style='margin:24px 0'><h3 style='margin-bottom:6px'>Historico de doses</h3>";
  if (totalHistorico == 0) {
    html += "<p style='color:#6b7280'>Nenhuma dose registrada ainda.</p>";
    return html;
  }

  // Resumo dos ultimos 7 dias (sem contar o alarme que esta tocando agora),
  // geral e por medicamento.
  uint32_t limite = (uint32_t)time(nullptr) - 7UL * 24UL * 3600UL;
  int noHorario = 0, atrasadas = 0, semAcesso = 0;
  const int MAX_GRUPOS = 8; // medicamentos diferentes no resumo
  String nomesGrupo[MAX_GRUPOS];
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
      tomadasGrupo[g] = 0;
      totalGrupo[g] = 0;
      grupos++;
    }
    totalGrupo[g]++;
    if (tomada) tomadasGrupo[g]++;
  }
  int concluidas = noHorario + atrasadas + semAcesso;
  html += "<p style='margin:4px 0 10px;font-size:15px'>Ultimos 7 dias: ";
  if (concluidas == 0) {
    html += "sem doses concluidas.";
  } else {
    html += "<b>" + String(percentual(noHorario + atrasadas, concluidas)) + "% de adesao</b><br>";
    html += "<span style='color:#16a34a'>" + String(noHorario) + " no horario</span> &middot; ";
    html += "<span style='color:#d97706'>" + String(atrasadas) + " com atraso</span> &middot; ";
    html += "<span style='color:#dc2626'>" + String(semAcesso) + " sem acesso</span>";
    if (grupos > 1) {
      for (int g = 0; g < grupos; g++) {
        html += "<br>" + escaparHTML(nomesGrupo[g]) + ": " + String(percentual(tomadasGrupo[g], totalGrupo[g])) + "%";
      }
    }
  }
  html += "</p>";

  html += "<table style='width:100%;border-collapse:collapse;font-size:14px'>";
  html += "<tr style='text-align:left;border-bottom:1px solid #d1d5db'><th>Data</th><th>Hora</th><th>Remedio</th><th>Situacao</th></tr>";
  int mostrar = totalHistorico < 20 ? totalHistorico : 20;
  for (int k = 0; k < mostrar; k++) {
    int idx = indiceHistorico(k);
    const RegistroDose& r = historico[idx];
    bool emAndamento = registroEmAndamento(idx);
    html += "<tr style='border-bottom:1px solid #f3f4f6'>";
    html += "<td style='padding:4px 0'>" + formatarDataHora(r.inicio, "%d/%m") + "</td>";
    html += "<td>" + formatarDataHora(r.inicio, "%H:%M") + "</td>";
    html += "<td>" + escaparHTML(nomeRegistro(r)) + "</td>";
    html += "<td style='color:" + String(corStatusDose(r.status, emAndamento)) + "'>" + textoStatusDose(r.status, emAndamento);
    if (r.status == DOSE_ATRASADA) html += " (" + formatarAtraso(r.atrasoSeg) + ")";
    html += "</td></tr>";
  }
  html += "</table>";

  html += "<p style='font-size:14px'><a href='/historico.csv'>Baixar historico completo (planilha CSV)</a></p>";
  html += "<button type='button' onclick='limparHistorico()' style='background:none;border:none;color:#dc2626;font-size:13px;padding:0;cursor:pointer'>Apagar historico</button>";
  return html;
}

// ---------- MODO DE CONFIGURACAO DE WI-FI ----------

void handleConfigRoot() {
  int redesEncontradas = WiFi.scanNetworks();

  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>Zelo+ Configuracao</title></head><body style='font-family:sans-serif;max-width:400px;margin:20px auto;padding:0 16px'>";
  html += "<h2>Zelo+ - Conectar ao Wi-Fi</h2>";

  if (configErro != "") {
    html += "<p style='color:red;font-weight:bold'>" + configErro + "</p>";
  }

  html += "<form action='/conectar' method='POST'>";
  html += "<label>Rede Wi-Fi:</label><br>";
  html += "<select name='ssid' style='width:100%;padding:8px;margin:6px 0' required>";
  for (int i = 0; i < redesEncontradas; i++) {
    String ssidEscapado = escaparHTML(WiFi.SSID(i));
    html += "<option value='" + ssidEscapado + "'>" + ssidEscapado + "</option>";
  }
  html += "</select><br>";
  html += "<label>Senha da rede:</label><br>";
  html += "<input type='password' name='senha' style='width:100%;padding:8px;margin:6px 0'><br><br>";
  html += "<button type='submit' style='width:100%;padding:12px;background:#2563eb;color:white;border:none;border-radius:6px;font-size:16px'>Conectar</button>";
  html += "</form>";
  html += "</body></html>";

  server.send(200, "text/html", html);
}

void handleConectar() {
  if (!server.hasArg("ssid")) {
    server.send(400, "text/plain", "Dados incompletos");
    return;
  }

  String ssid = server.arg("ssid");
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

void iniciarModoConfig() {
  modoConfig = true;

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP("ZeloPlus-Config");

  dnsServer.start(DNS_PORT, "*", apIP);

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Conecte no Wi-Fi");
  lcd.setCursor(0, 1);
  lcd.print("ZeloPlus-Config");

  server.on("/", handleConfigRoot);
  server.on("/conectar", HTTP_POST, handleConectar);

  server.on("/generate_204", handleCaptivePortal);
  server.on("/gen_204", handleCaptivePortal);
  server.on("/hotspot-detect.html", handleCaptivePortal);
  server.on("/library/test/success.html", handleCaptivePortal);
  server.on("/ncsi.txt", handleCaptivePortal);
  server.onNotFound(handleCaptivePortal);

  server.begin();
}

// ---------- MODO NORMAL (rotina do dispenser) ----------

const char* ESTILO_CAMPO = "width:100%;padding:8px;margin:6px 0;box-sizing:border-box";
const char* ESTILO_CAIXA = "border:1px solid #d1d5db;border-radius:6px;margin:12px 0;padding:8px 12px";

String campoTexto(const char* rotulo, const char* nome, const String& valor, const char* tipo,
                  const char* placeholder, bool obrigatorio) {
  String html = "<label>" + String(rotulo) + "</label><br>";
  html += "<input type='" + String(tipo) + "' name='" + String(nome) + "' value='" + escaparHTML(valor) + "'";
  html += " placeholder='" + String(placeholder) + "' style='" + String(ESTILO_CAMPO) + "'";
  if (obrigatorio) html += " required";
  html += "><br>";
  return html;
}

String camposContato(const char* titulo, const char* prefixo, const Contato& contato, bool obrigatorio,
                     bool familiar = false) {
  String p = prefixo;
  String html = "<fieldset";
  if (familiar) html += " class='familiar'";
  html += " style='" + String(ESTILO_CAIXA) + "'>";
  html += "<legend>" + String(titulo) + "</legend>";
  if (familiar) {
    html += "<button type='button' onclick='removerFamiliar(this)' style='float:right;background:none;border:none;color:#dc2626;font-size:13px;cursor:pointer'>Remover</button>";
  }
  html += campoTexto("Nome:", (p + "_nome").c_str(), contato.nome, "text", "", obrigatorio);
  html += campoTexto("Celular:", (p + "_tel").c_str(), contato.telefone, "tel", "(11) 98765-4321", obrigatorio);
  html += campoTexto("ID do Telegram:", (p + "_chat").c_str(), contato.chatId, "text", "ex.: 123456789", false);
  if (contato.nome != "" && contato.chatId == "") {
    html += "<p style='color:#b45309;font-size:13px;margin:0 0 6px'>Sem o ID do Telegram esta pessoa nao recebe avisos.</p>";
  }
  html += "</fieldset>";
  return html;
}

String campoHorario(int c, const String& valor) {
  return "<input type='time' name='m" + String(c) + "_h' value='" + valor +
         "' style='width:100%;padding:8px;margin-bottom:6px;box-sizing:border-box'>";
}

// Cartao de um compartimento: medicamento, horarios e onde colocar o remedio.
String cartaoCompartimento(int c) {
  const Medicamento& m = medicamentos[c];
  String n = String(c + 1);
  String html = "<fieldset style='" + String(ESTILO_CAIXA) + "'>";
  html += "<legend><b>Compartimento " + n + "</b>" + (medicamentoAtivo(c) ? "" : " (vazio)") + "</legend>";
  html += "<label>Medicamento:</label><br>";
  html += "<input type='text' name='m" + String(c) + "_nome' id='m" + String(c) + "_nome' value='" + escaparHTML(m.nome) +
          "' data-original='" + escaparHTML(m.nome) + "' oninput='atualizarDica(" + String(c) + ")'" +
          " maxlength='40' style='" + String(ESTILO_CAMPO) + "'><br>";
  html += "<label>Horarios <span style='font-size:12px;color:#6b7280'>(apague um horario para remove-lo)</span>:</label><br>";
  html += "<div id='horarios-" + String(c) + "'>";
  for (int i = 0; i < m.totalHorarios; i++) {
    html += campoHorario(c, formatarHorario(m.hora[i], m.minuto[i]));
  }
  if (m.totalHorarios == 0) html += campoHorario(c, "");
  html += "</div>";
  html += "<button type='button' onclick='adicionarHorario(" + String(c) + ")' style='width:100%;padding:8px;background:#e5e7eb;border:none;border-radius:6px;font-size:14px;margin-bottom:6px'>+ Adicionar horario</button>";
  html += "<p id='dica-" + String(c) + "' style='margin:4px 0;padding:8px;background:#ecfdf5;border-radius:6px;font-size:14px;";
  html += m.nome == "" ? "display:none'>" : "'>";
  html += "&#128230; Coloque <b>" + escaparHTML(m.nome) + "</b> no <b>compartimento " + n + "</b></p>";
  if (medicamentoAtivo(c)) {
    html += "<button type='button' onclick=\"esvaziarCompartimento(" + String(c) + ")\" style='background:none;border:none;color:#dc2626;font-size:13px;padding:4px 0;cursor:pointer'>Esvaziar compartimento " + n + "</button>";
  }
  html += "</fieldset>";
  return html;
}

void handleRoot() {
  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>Zelo+ Dispenser</title></head><body style='font-family:sans-serif;max-width:400px;margin:20px auto;padding:0 16px'>";
  html += "<h2>Zelo+ - Cadastro</h2>";
  html += "<form action='/salvar' method='POST' onsubmit='return confirmarTrocas()'>";
  html += campoTexto("Nome do paciente:", "paciente", nomePaciente, "text", "", true);

  html += "<h3 style='margin:18px 0 0'>Medicamentos</h3>";
  html += "<p style='font-size:13px;color:#6b7280;margin:4px 0'>Cada medicamento fica sempre no mesmo compartimento. Deixe em branco os compartimentos que nao forem usados.</p>";
  for (int c = 0; c < NUM_COMPARTIMENTOS; c++) {
    html += cartaoCompartimento(c);
  }

  html += "<h3 style='margin:18px 0 0'>Contatos</h3>";
  html += camposContato("Cuidador (obrigatorio)", "cuid", cuidador, true);

  // Familiares sao opcionais: mostra os ja cadastrados (ou uma caixa vazia) e
  // o botao "+" cria novas caixas no navegador, ate MAX_FAMILIARES.
  int familiaresMostrados = 0;
  html += "<div id='familiares-container'>";
  for (int i = 0; i < MAX_FAMILIARES; i++) {
    if (familiares[i].nome == "" && familiares[i].telefone == "") continue;
    String titulo = "Familiar " + String(familiaresMostrados + 1) + " (opcional)";
    html += camposContato(titulo.c_str(), prefixoFamiliar(familiaresMostrados).c_str(), familiares[i], false, true);
    familiaresMostrados++;
  }
  if (familiaresMostrados == 0) {
    html += camposContato("Familiar 1 (opcional)", "fam0", Contato(), false, true);
  }
  html += "</div>";
  html += "<template id='modelo-familiar'>" + camposContato("Familiar", "fam0", Contato(), false, true) + "</template>";
  html += "<button type='button' id='btn-familiar' onclick='adicionarFamiliar()' style='width:100%;padding:10px;background:#e5e7eb;border:none;border-radius:6px;font-size:15px;margin-bottom:12px'>+ Adicionar familiar</button>";

  html += "<fieldset style='" + String(ESTILO_CAIXA) + "'>";
  html += "<legend>Avisos pelo Telegram</legend>";
  html += "<label>Token do bot:</label><br>";
  html += "<input type='password' name='tg_token' id='tg_token' autocomplete='off' placeholder='";
  html += tokenTelegram != "" ? "(salvo - deixe em branco para manter)" : "cole aqui o token do @BotFather";
  html += "' style='" + String(ESTILO_CAMPO) + "'><br>";
  html += "<button type='button' onclick='buscarIdsTelegram()' style='width:100%;padding:10px;background:#0ea5e9;color:white;border:none;border-radius:6px;font-size:15px;margin:6px 0'>Buscar IDs do Telegram</button>";
  html += "<pre id='ids-telegram' style='white-space:pre-wrap;font-size:13px;background:#f3f4f6;padding:8px;border-radius:6px;display:none'></pre>";
  html += "<details style='font-size:14px'><summary>Como configurar (passo a passo)</summary><ol>";
  html += "<li><b>Uma vez so:</b> no Telegram, abra o <b>@BotFather</b>, envie <i>/newbot</i>, escolha um nome e um usuario terminado em <i>bot</i> (ex.: <i>zelo_maria_bot</i>). Copie o <b>token</b> que ele mandar e cole acima.</li>";
  html += "<li><b>Cada pessoa</b> (cuidador e familiares) procura esse bot no Telegram e toca em <b>Iniciar</b>.</li>";
  html += "<li>Toque em <b>Buscar IDs do Telegram</b>: aparece o nome e o ID de quem iniciou o bot. Copie cada ID para o campo da pessoa e salve.</li>";
  html += "<li>Use <b>Enviar mensagem de teste</b> para conferir.</li></ol></details>";
  html += "</fieldset>";

  html += "<button type='submit' style='width:100%;padding:12px;background:#2563eb;color:white;border:none;border-radius:6px;font-size:16px'>Salvar cadastro</button>";
  html += "</form>";

  if (totalMedicamentosAtivos() > 0) {
    html += "<div style='margin-top:20px;padding:10px;background:#f0fdf4;border-radius:6px;font-size:14px'>";
    html += "<b>" + escaparHTML(nomePaciente) + "</b><br>";
    for (int c = 0; c < NUM_COMPARTIMENTOS; c++) {
      if (!medicamentoAtivo(c)) continue;
      html += "Compartimento " + String(c + 1) + ": " + escaparHTML(medicamentos[c].nome) + " - ";
      for (int i = 0; i < medicamentos[c].totalHorarios; i++) {
        if (i > 0) html += ", ";
        html += formatarHorario(medicamentos[c].hora[i], medicamentos[c].minuto[i]);
      }
      html += "<br>";
    }
    html += "</div>";
    html += "<button type='button' onclick='testarMensagens()' style='width:100%;padding:10px;margin-top:10px;background:#16a34a;color:white;border:none;border-radius:6px;font-size:15px'>Enviar mensagem de teste no Telegram</button>";
  }

  html += htmlHistorico();

  // Reposicao: escolhe o medicamento, confirma e abre so o compartimento dele.
  html += "<hr style='margin:24px 0'>";
  if (totalMedicamentosAtivos() > 0) {
    html += "<div id='repor1'>";
    html += "<button type='button' onclick='mostrarRepor(2)' style='width:100%;padding:12px;background:#f59e0b;color:white;border:none;border-radius:6px;font-size:15px'>Abrir compartimento para reposicao</button>";
    html += "</div>";
    html += "<div id='repor2' style='display:none;padding:12px;background:#fef3c7;border-radius:6px'>";
    html += "<p style='margin-top:0'><b>Qual medicamento voce vai repor?</b></p>";
    for (int c = 0; c < NUM_COMPARTIMENTOS; c++) {
      if (!medicamentoAtivo(c)) continue;
      html += "<label style='display:block;padding:6px 0'><input type='radio' name='repor' value='" + String(c) + "'";
      html += " data-nome='" + escaparHTML(medicamentos[c].nome) + "'> ";
      html += escaparHTML(medicamentos[c].nome) + " &rarr; compartimento " + String(c + 1) + "</label>";
    }
    html += "<button type='button' onclick='continuarRepor()' style='width:100%;padding:12px;background:#f59e0b;color:white;border:none;border-radius:6px;font-size:15px;margin:8px 0'>Continuar</button>";
    html += "<button type='button' onclick='mostrarRepor(1)' style='width:100%;padding:10px;background:#e5e7eb;border:none;border-radius:6px;font-size:14px'>Cancelar</button>";
    html += "</div>";
    html += "<div id='repor3' style='display:none;padding:12px;background:#fef3c7;border-radius:6px'>";
    html += "<p id='repor-pergunta' style='margin-top:0'></p>";
    html += "<button type='button' onclick='confirmarRepor()' style='width:100%;padding:12px;background:#dc2626;color:white;border:none;border-radius:6px;font-size:15px;margin-bottom:8px'>Sim, abrir compartimento</button>";
    html += "<button type='button' onclick='mostrarRepor(1)' style='width:100%;padding:10px;background:#e5e7eb;border:none;border-radius:6px;font-size:14px'>Cancelar</button>";
    html += "</div>";
  }

  html += "<p style='margin-top:30px'><a href='/trocar-wifi'>Trocar rede Wi-Fi</a></p>";

  html += "<script>";
  html += "var NUM_COMPARTIMENTOS = " + String(NUM_COMPARTIMENTOS) + ";";
  html += "var MAX_HORARIOS = " + String(MAX_HORARIOS) + ";";
  html += "function adicionarHorario(c) {";
  html += "  var container = document.getElementById('horarios-' + c);";
  html += "  if (container.querySelectorAll('input').length >= MAX_HORARIOS) { alert('Maximo de ' + MAX_HORARIOS + ' horarios por medicamento.'); return; }";
  html += "  var input = document.createElement('input');";
  html += "  input.type = 'time'; input.name = 'm' + c + '_h';";
  html += "  input.style.cssText = 'width:100%;padding:8px;margin-bottom:6px;box-sizing:border-box';";
  html += "  container.appendChild(input);";
  html += "}";
  html += "function atualizarDica(c) {";
  html += "  var nome = document.getElementById('m' + c + '_nome').value.trim();";
  html += "  var dica = document.getElementById('dica-' + c);";
  html += "  dica.style.display = nome ? 'block' : 'none';";
  html += "  dica.innerHTML = '';";
  html += "  dica.appendChild(document.createTextNode('\\uD83D\\uDCE6 Coloque '));";
  html += "  var b1 = document.createElement('b'); b1.textContent = nome; dica.appendChild(b1);";
  html += "  dica.appendChild(document.createTextNode(' no '));";
  html += "  var b2 = document.createElement('b'); b2.textContent = 'compartimento ' + (c + 1); dica.appendChild(b2);";
  html += "}";
  // Troca de medicamento num compartimento ja usado: pede para retirar os antigos.
  html += "function confirmarTrocas() {";
  html += "  for (var c = 0; c < NUM_COMPARTIMENTOS; c++) {";
  html += "    var campo = document.getElementById('m' + c + '_nome');";
  html += "    var antigo = campo.dataset.original.trim(); var novo = campo.value.trim();";
  html += "    if (antigo && novo && antigo.toLowerCase() != novo.toLowerCase()) {";
  html += "      if (!confirm('O compartimento ' + (c + 1) + ' tinha ' + antigo + '. Retire todos os comprimidos antigos antes de colocar ' + novo + '. Confirmar troca?')) return false;";
  html += "    }";
  html += "  }";
  html += "  return true;";
  html += "}";
  html += "function esvaziarCompartimento(c) {";
  html += "  var nome = document.getElementById('m' + c + '_nome').dataset.original;";
  html += "  if (!confirm('Esvaziar o compartimento ' + (c + 1) + ' (' + nome + ')? O cadastro deste medicamento sera apagado.')) return;";
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
  html += "  if (!escolha) { alert('Escolha o medicamento que sera reposto.'); return; }";
  html += "  reporEscolhido = escolha.value;";
  html += "  var pergunta = document.getElementById('repor-pergunta'); pergunta.innerHTML = '';";
  html += "  pergunta.appendChild(document.createTextNode('Abrir o '));";
  html += "  var b1 = document.createElement('b'); b1.textContent = 'compartimento ' + (Number(reporEscolhido) + 1); pergunta.appendChild(b1);";
  html += "  pergunta.appendChild(document.createTextNode(' para repor '));";
  html += "  var b2 = document.createElement('b'); b2.textContent = escolha.dataset.nome; pergunta.appendChild(b2);";
  html += "  pergunta.appendChild(document.createTextNode('?'));";
  html += "  mostrarRepor(3);";
  html += "}";
  html += "function confirmarRepor() {";
  html += "  var corpo = new URLSearchParams(); corpo.append('c', reporEscolhido);";
  html += "  fetch('/abrir-manual', {method:'POST', body: corpo}).then(function(r){";
  html += "    if (r.ok) { alert('Compartimento ' + (Number(reporEscolhido) + 1) + ' aberto! Reponha o medicamento.'); }";
  html += "    else { alert('Nao foi possivel abrir agora. Tente novamente em instantes.'); }";
  html += "    mostrarRepor(1);";
  html += "  });";
  html += "}";
  html += "var MAX_FAMILIARES = " + String(MAX_FAMILIARES) + ";";
  html += "function renumerarFamiliares() {";
  html += "  var caixas = document.querySelectorAll('#familiares-container .familiar');";
  html += "  for (var i = 0; i < caixas.length; i++) {";
  html += "    caixas[i].querySelector('legend').textContent = 'Familiar ' + (i + 1) + ' (opcional)';";
  html += "    var campos = caixas[i].querySelectorAll('input');";
  html += "    for (var j = 0; j < campos.length; j++) { campos[j].name = campos[j].name.replace(/^fam[0-9]+/, 'fam' + i); }";
  html += "  }";
  html += "  document.getElementById('btn-familiar').style.display = caixas.length >= MAX_FAMILIARES ? 'none' : 'block';";
  html += "}";
  html += "function adicionarFamiliar() {";
  html += "  var container = document.getElementById('familiares-container');";
  html += "  if (container.querySelectorAll('.familiar').length >= MAX_FAMILIARES) return;";
  html += "  container.appendChild(document.getElementById('modelo-familiar').content.cloneNode(true));";
  html += "  renumerarFamiliares();";
  html += "}";
  html += "function removerFamiliar(botao) {";
  html += "  botao.closest('.familiar').remove();";
  html += "  renumerarFamiliares();";
  html += "}";
  html += "renumerarFamiliares();";
  html += "function buscarIdsTelegram() {";
  html += "  var saida = document.getElementById('ids-telegram');";
  html += "  saida.style.display = 'block'; saida.textContent = 'Buscando...';";
  html += "  var corpo = new URLSearchParams(); corpo.append('tg_token', document.getElementById('tg_token').value);";
  html += "  fetch('/telegram-ids', {method:'POST', body: corpo}).then(function(r){ return r.text(); })";
  html += "    .then(function(t){ saida.textContent = t; });";
  html += "}";
  html += "function limparHistorico() {";
  html += "  if (!confirm('Apagar todo o historico de doses?')) return;";
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
  escreverLinhaLCD(1, modo == ABAST_ESVAZIAR ? String("Retire tudo") : medicamentos[c].nome);
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
    String antigo = medicamentos[c].nome;
    String novo = novos[c].nome;
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

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 10)) return;

  char horaBuffer[17];
  strftime(horaBuffer, sizeof(horaBuffer), "%H:%M:%S", &timeinfo);
  escreverLinhaLCD(0, horaBuffer);

  int ativos = totalMedicamentosAtivos();
  if (dosePendente) {
    escreverLinhaLCD(1, "Dose pendente!");
  } else if (ativos > 0) {
    escreverLinhaLCD(1, String(ativos) + (ativos == 1 ? " remedio" : " remedios"));
  } else {
    escreverLinhaLCD(1, "Sem remedio");
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

void iniciarModoNormal() {
  modoConfig = false;

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  lcd.clear();
  lcd.print("IP do sistema:");
  lcd.setCursor(0, 1);
  lcd.print(WiFi.localIP());
  Serial.print("Acesse: http://");
  Serial.println(WiFi.localIP());
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
  server.begin();
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
  lcd.print("Iniciando...");

  prefsCadastro.begin("cadastro", false);
  carregarCadastro();
  prefsHistorico.begin("historico", false);
  carregarHistorico();

  preferences.begin("wifi", false);
  String ssidSalvo = preferences.getString("ssid", "");
  String senhaSalva = preferences.getString("pass", "");

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
  } else {
    iniciarModoConfig();
  }
}

void loop() {
  if (modoConfig) {
    dnsServer.processNextRequest();
  }
  server.handleClient();

  if (modoConfig) {
    return;
  }

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
          String nomeCurto = textoLCD(medicamentos[c].nome, 16 - sufixo.length());
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
                                                                  : medicamentos[compartimentoAbastecendo].nome);
        } else {
          mostrarContagemLCD(abastecimentoAbertoEm, TEMPO_ABASTECIMENTO);
        }
      }
      break;
    }
  }
}
