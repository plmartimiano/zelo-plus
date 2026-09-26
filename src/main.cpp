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
#define SERVO_PIN 13
#define MAX_ALARMES 6
#define MAX_FAMILIARES 5
#define MAX_HISTORICO 60

LiquidCrystal_I2C lcd(0x27, 16, 2);
Servo portaServo;
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

String nomePaciente = "";
String nomeRemedio = "";
Contato cuidador;
Contato familiares[MAX_FAMILIARES];
String tokenTelegram = ""; // token do bot, criado pelo cuidador no @BotFather
int horaAlarmes[MAX_ALARMES];
int minutoAlarmes[MAX_ALARMES];
bool jaDisparadoHoje[MAX_ALARMES];
int totalAlarmes = 0;
bool alarmeConfigurado = false;
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

unsigned long portaAbertaEm = 0;
unsigned long abastecimentoAbertoEm = 0;
unsigned long ultimoToggleAlarme = 0;
bool ledBuzzerLigado = false;

unsigned long alarmeIniciadoEm = 0;
int alarmeAtual = -1;        // indice do horario que disparou
int nivelAvisoEnviado = 0;   // 0 = nenhum, 1 = cuidador avisado, 2 = todos alertados
bool dosePendente = false;   // alarme terminou sem acesso; botao ainda abre o compartimento

void abrirPortaLentamente() {
  for (int angulo = 0; angulo <= 90; angulo++) {
    portaServo.write(angulo);
    delay(15);
  }
}

void fecharPortaLentamente() {
  for (int angulo = 90; angulo >= 0; angulo--) {
    portaServo.write(angulo);
    delay(15);
  }
}

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

String formatarHorario(int hora, int minuto) {
  char valor[6];
  snprintf(valor, sizeof(valor), "%02d:%02d", hora, minuto);
  return String(valor);
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

String horarioAlarmeAtual() {
  if (alarmeAtual < 0) return "";
  return formatarHorario(horaAlarmes[alarmeAtual], minutoAlarmes[alarmeAtual]);
}

String horarioAgora() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 100)) return "";
  return formatarHorario(timeinfo.tm_hour, timeinfo.tm_min);
}

void avisarPrimeiroAtraso() {
  enviarParaCuidador("Zelo+: Paciente “" + nomePaciente + "” não acessou o medicamento das “" +
                     horarioAlarmeAtual() + "” horas.");
}

void alertarSemAcesso() {
  enviarParaTodos("Zelo+: Paciente “" + nomePaciente + "” não foi até o dispenser no horário das “" +
                  horarioAlarmeAtual() + "”.");
}

// Depois de um aviso, confirma para quem foi avisado que o paciente chegou.
void avisarAcessoAposAviso() {
  String texto = "Zelo+: " + nomePaciente + " acessou o dispenser às " + horarioAgora() +
                 " (dose de " + nomeRemedio + " das " + horarioAlarmeAtual() + ").";
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

void salvarCadastro() {
  uint8_t horas[MAX_ALARMES];
  uint8_t minutos[MAX_ALARMES];
  for (int i = 0; i < totalAlarmes; i++) {
    horas[i] = (uint8_t)horaAlarmes[i];
    minutos[i] = (uint8_t)minutoAlarmes[i];
  }

  prefsCadastro.putString("nome", nomePaciente); // chave mantida da versao anterior
  prefsCadastro.putString("remedio", nomeRemedio);
  prefsCadastro.putUChar("total", (uint8_t)totalAlarmes);
  prefsCadastro.putBytes("horas", horas, totalAlarmes);
  prefsCadastro.putBytes("minutos", minutos, totalAlarmes);

  prefsCadastro.putString("tg_token", tokenTelegram);
  salvarContato("cuid", cuidador);
  for (int i = 0; i < MAX_FAMILIARES; i++) {
    salvarContato(prefixoFamiliar(i).c_str(), familiares[i]);
  }
}

void carregarCadastro() {
  nomePaciente = prefsCadastro.getString("nome", "");
  nomeRemedio = prefsCadastro.getString("remedio", "");

  tokenTelegram = prefsCadastro.getString("tg_token", "");
  carregarContato("cuid", cuidador);
  for (int i = 0; i < MAX_FAMILIARES; i++) {
    carregarContato(prefixoFamiliar(i).c_str(), familiares[i]);
  }

  int total = prefsCadastro.getUChar("total", 0);
  if (total > MAX_ALARMES) total = MAX_ALARMES;

  uint8_t horas[MAX_ALARMES];
  uint8_t minutos[MAX_ALARMES];
  if (total == 0 ||
      prefsCadastro.getBytes("horas", horas, total) != (size_t)total ||
      prefsCadastro.getBytes("minutos", minutos, total) != (size_t)total) {
    total = 0;
  }

  totalAlarmes = 0;
  for (int i = 0; i < total; i++) {
    if (horas[i] > 23 || minutos[i] > 59) continue;
    horaAlarmes[totalAlarmes] = horas[i];
    minutoAlarmes[totalAlarmes] = minutos[i];
    jaDisparadoHoje[totalAlarmes] = false;
    totalAlarmes++;
  }

  alarmeConfigurado = (totalAlarmes > 0);

  Serial.print("Cadastro carregado: ");
  Serial.print(totalAlarmes);
  Serial.println(" horario(s)");
}

// ---------- HISTORICO DE DOSES ----------
// Cada disparo de alarme vira um registro, guardado em Preferences (namespace
// "historico") num buffer circular com as ultimas MAX_HISTORICO doses.

enum StatusDose : uint8_t {
  DOSE_PENDENTE = 0,    // alarme tocando (ou placa reiniciou antes de concluir)
  DOSE_NO_HORARIO = 1,  // acesso antes do primeiro aviso ao cuidador
  DOSE_ATRASADA = 2,    // acesso depois do primeiro aviso (inclusive apos o alerta final)
  DOSE_SEM_ACESSO = 3   // alerta final enviado e ninguem acessou
};

struct RegistroDose {
  uint32_t inicio;     // horario do disparo (segundos desde 1970)
  uint16_t atrasoSeg;  // tempo ate o acesso
  uint8_t status;
  uint8_t reservado;
};

RegistroDose historico[MAX_HISTORICO];
int totalHistorico = 0;    // registros validos (ate MAX_HISTORICO)
int proximoHistorico = 0;  // posicao onde entra o proximo registro
int registroAtual = -1;    // registro do alarme em andamento

void salvarHistorico() {
  prefsHistorico.putBytes("regs", historico, sizeof(historico));
  prefsHistorico.putUChar("total", (uint8_t)totalHistorico);
  prefsHistorico.putUChar("prox", (uint8_t)proximoHistorico);
}

void carregarHistorico() {
  totalHistorico = 0;
  proximoHistorico = 0;
  if (prefsHistorico.getBytes("regs", historico, sizeof(historico)) != sizeof(historico)) {
    memset(historico, 0, sizeof(historico));
    return;
  }
  totalHistorico = prefsHistorico.getUChar("total", 0);
  proximoHistorico = prefsHistorico.getUChar("prox", 0);
  if (totalHistorico > MAX_HISTORICO || proximoHistorico >= MAX_HISTORICO) {
    totalHistorico = 0;
    proximoHistorico = 0;
  }
}

// Indice do k-esimo registro mais recente (k = 0 e o ultimo).
int indiceHistorico(int k) {
  return (proximoHistorico - 1 - k + 2 * MAX_HISTORICO) % MAX_HISTORICO;
}

int registrarInicioDose() {
  int idx = proximoHistorico;
  historico[idx].inicio = (uint32_t)time(nullptr);
  historico[idx].atrasoSeg = 0;
  historico[idx].status = DOSE_PENDENTE;
  historico[idx].reservado = 0;
  proximoHistorico = (proximoHistorico + 1) % MAX_HISTORICO;
  if (totalHistorico < MAX_HISTORICO) totalHistorico++;
  salvarHistorico();
  return idx;
}

void atualizarDose(StatusDose status, unsigned long decorridoMs) {
  if (registroAtual < 0) return;
  unsigned long segundos = decorridoMs / 1000UL;
  historico[registroAtual].status = status;
  historico[registroAtual].atrasoSeg = segundos > 65535UL ? 65535 : (uint16_t)segundos;
  salvarHistorico();
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

String htmlHistorico() {
  String html = "<hr style='margin:24px 0'><h3 style='margin-bottom:6px'>Historico de doses</h3>";
  if (totalHistorico == 0) {
    html += "<p style='color:#6b7280'>Nenhuma dose registrada ainda.</p>";
    return html;
  }

  // Resumo dos ultimos 7 dias (sem contar o alarme que esta tocando agora).
  uint32_t limite = (uint32_t)time(nullptr) - 7UL * 24UL * 3600UL;
  int noHorario = 0, atrasadas = 0, semAcesso = 0;
  for (int k = 0; k < totalHistorico; k++) {
    int idx = indiceHistorico(k);
    if (historico[idx].inicio < limite) continue;
    if (idx == registroAtual && estadoAtual == TOCANDO) continue;
    switch (historico[idx].status) {
      case DOSE_NO_HORARIO: noHorario++; break;
      case DOSE_ATRASADA: atrasadas++; break;
      default: semAcesso++; break;
    }
  }
  int concluidas = noHorario + atrasadas + semAcesso;
  html += "<p style='margin:4px 0 10px;font-size:15px'>Ultimos 7 dias: ";
  if (concluidas == 0) {
    html += "sem doses concluidas.";
  } else {
    int adesao = ((noHorario + atrasadas) * 100 + concluidas / 2) / concluidas;
    html += "<b>" + String(adesao) + "% de adesao</b><br>";
    html += "<span style='color:#16a34a'>" + String(noHorario) + " no horario</span> &middot; ";
    html += "<span style='color:#d97706'>" + String(atrasadas) + " com atraso</span> &middot; ";
    html += "<span style='color:#dc2626'>" + String(semAcesso) + " sem acesso</span>";
  }
  html += "</p>";

  html += "<table style='width:100%;border-collapse:collapse;font-size:14px'>";
  html += "<tr style='text-align:left;border-bottom:1px solid #d1d5db'><th>Data</th><th>Horario</th><th>Situacao</th></tr>";
  int mostrar = totalHistorico < 20 ? totalHistorico : 20;
  for (int k = 0; k < mostrar; k++) {
    int idx = indiceHistorico(k);
    const RegistroDose& r = historico[idx];
    bool emAndamento = (idx == registroAtual && estadoAtual == TOCANDO);
    html += "<tr style='border-bottom:1px solid #f3f4f6'>";
    html += "<td style='padding:4px 0'>" + formatarDataHora(r.inicio, "%d/%m") + "</td>";
    html += "<td>" + formatarDataHora(r.inicio, "%H:%M") + "</td>";
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
  html += " style='border:1px solid #d1d5db;border-radius:6px;margin:12px 0;padding:8px 12px'>";
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

void handleRoot() {
  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>Zelo+ Dispenser</title></head><body style='font-family:sans-serif;max-width:400px;margin:20px auto;padding:0 16px'>";
  html += "<h2>Zelo+ - Cadastro</h2>";
  html += "<form action='/salvar' method='POST'>";
  html += campoTexto("Nome do paciente:", "paciente", nomePaciente, "text", "", true);
  html += campoTexto("Nome do remedio:", "remedio", nomeRemedio, "text", "", true);

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

  html += "<fieldset style='border:1px solid #d1d5db;border-radius:6px;margin:12px 0;padding:8px 12px'>";
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

  html += "<label>Horarios do remedio:</label><br>";
  html += "<div id='horarios-container'>";

  if (totalAlarmes > 0) {
    for (int i = 0; i < totalAlarmes; i++) {
      html += "<div style='margin-bottom:6px'><input type='time' name='horario' value='" + formatarHorario(horaAlarmes[i], minutoAlarmes[i]) + "' style='width:100%;padding:8px' required></div>";
    }
  } else {
    html += "<div style='margin-bottom:6px'><input type='time' name='horario' style='width:100%;padding:8px' required></div>";
  }

  html += "</div>";
  html += "<button type='button' onclick='adicionarHorario()' style='width:100%;padding:10px;background:#e5e7eb;border:none;border-radius:6px;font-size:15px;margin-bottom:12px'>+ Adicionar horario</button><br>";
  html += "<button type='submit' style='width:100%;padding:12px;background:#2563eb;color:white;border:none;border-radius:6px;font-size:16px'>Salvar cadastro</button>";
  html += "</form>";

  if (alarmeConfigurado) {
    html += "<p style='margin-top:20px;color:green'>Remedio: " + escaparHTML(nomeRemedio) + " - " + escaparHTML(nomePaciente) + "</p>";
    html += "<p style='color:green'>Horarios cadastrados: ";
    for (int i = 0; i < totalAlarmes; i++) {
      html += formatarHorario(horaAlarmes[i], minutoAlarmes[i]);
      if (i < totalAlarmes - 1) html += ", ";
    }
    html += "</p>";

    html += "<button type='button' onclick='testarMensagens()' style='width:100%;padding:10px;background:#16a34a;color:white;border:none;border-radius:6px;font-size:15px'>Enviar mensagem de teste no Telegram</button>";
  }

  html += htmlHistorico();

  html += "<hr style='margin:24px 0'>";
  html += "<div id='passo1'>";
  html += "<button type='button' onclick='mostrarConfirmacao()' style='width:100%;padding:12px;background:#f59e0b;color:white;border:none;border-radius:6px;font-size:15px'>Abrir compartimento para reposicao</button>";
  html += "</div>";
  html += "<div id='passo2' style='display:none;margin-top:10px;padding:12px;background:#fef3c7;border-radius:6px'>";
  html += "<p style='margin-top:0'>Tem certeza? O compartimento vai abrir agora mesmo.</p>";
  html += "<button type='button' onclick='confirmarAbertura()' style='width:100%;padding:12px;background:#dc2626;color:white;border:none;border-radius:6px;font-size:15px;margin-bottom:8px'>Sim, abrir compartimento</button>";
  html += "<button type='button' onclick='cancelarAbertura()' style='width:100%;padding:10px;background:#e5e7eb;border:none;border-radius:6px;font-size:14px'>Cancelar</button>";
  html += "</div>";

  html += "<p style='margin-top:30px'><a href='/trocar-wifi'>Trocar rede Wi-Fi</a></p>";

  html += "<script>";
  html += "function adicionarHorario() {";
  html += "  var container = document.getElementById('horarios-container');";
  html += "  var div = document.createElement('div');";
  html += "  div.style.marginBottom = '6px';";
  html += "  div.innerHTML = \"<input type='time' name='horario' style='width:100%;padding:8px' required>\";";
  html += "  container.appendChild(div);";
  html += "}";
  html += "function mostrarConfirmacao() {";
  html += "  document.getElementById('passo1').style.display = 'none';";
  html += "  document.getElementById('passo2').style.display = 'block';";
  html += "}";
  html += "function cancelarAbertura() {";
  html += "  document.getElementById('passo2').style.display = 'none';";
  html += "  document.getElementById('passo1').style.display = 'block';";
  html += "}";
  html += "function confirmarAbertura() {";
  html += "  fetch('/abrir-manual', {method:'POST'}).then(function(r){";
  html += "    if (r.ok) { alert('Compartimento aberto! Reponha o medicamento.'); }";
  html += "    else { alert('Nao foi possivel abrir agora. Tente novamente em instantes.'); }";
  html += "    cancelarAbertura();";
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

void handleAbrirManual() {
  if (estadoAtual != AGUARDANDO) {
    server.send(409, "text/plain", "Sistema ocupado no momento");
    return;
  }

  server.send(200, "text/plain", "OK");

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Abastecendo...");
  lcd.setCursor(0, 1);
  lcd.print("Aperte p/ fechar");
  abrirPortaLentamente();
  estadoAtual = ABASTECENDO;
  abastecimentoAbertoEm = millis();
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
  String csv = "data;horario;situacao;atraso_minutos\r\n";
  for (int k = totalHistorico - 1; k >= 0; k--) { // do mais antigo para o mais recente
    int idx = indiceHistorico(k);
    const RegistroDose& r = historico[idx];
    bool emAndamento = (idx == registroAtual && estadoAtual == TOCANDO);
    csv += formatarDataHora(r.inicio, "%d/%m/%Y") + ";" + formatarDataHora(r.inicio, "%H:%M") + ";";
    csv += textoStatusDose(r.status, emAndamento);
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
  registroAtual = -1;
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

void handleSalvar() {
  if (server.hasArg("paciente") && server.hasArg("remedio")) {
    bool jaTinhaCadastro = alarmeConfigurado;

    Contato novoCuidador;
    lerContatoDoFormulario("cuid", novoCuidador);
    if (novoCuidador.nome == "" || novoCuidador.telefone == "") {
      server.send(400, "text/plain; charset=utf-8", "O cuidador é obrigatório: preencha nome e celular.");
      return;
    }
    cuidador = novoCuidador;

    String novoToken = server.arg("tg_token");
    novoToken.trim();
    if (novoToken != "") tokenTelegram = novoToken; // em branco = mantem o token salvo

    nomePaciente = server.arg("paciente");
    nomeRemedio = server.arg("remedio");
    nomePaciente.trim(); // evita espacos sobrando nas mensagens
    nomeRemedio.trim();

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

    totalAlarmes = 0;
    for (int i = 0; i < server.args() && totalAlarmes < MAX_ALARMES; i++) {
      if (server.argName(i) == "horario") {
        String horario = server.arg(i);
        int separador = horario.indexOf(':');
        if (separador > 0) {
          horaAlarmes[totalAlarmes] = horario.substring(0, separador).toInt();
          minutoAlarmes[totalAlarmes] = horario.substring(separador + 1).toInt();
          jaDisparadoHoje[totalAlarmes] = false;
          totalAlarmes++;
        }
      }
    }

    alarmeConfigurado = (totalAlarmes > 0);
    salvarCadastro();

    server.sendHeader("Location", "/");
    server.send(303);

    if (!jaTinhaCadastro && estadoAtual == AGUARDANDO) {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Abastecendo...");
      lcd.setCursor(0, 1);
      lcd.print("Aperte p/ fechar");
      abrirPortaLentamente();
      estadoAtual = ABASTECENDO;
      abastecimentoAbertoEm = millis();
    }
  } else {
    server.send(400, "text/plain", "Dados incompletos");
  }
}

void iniciarAlarme(int indice) {
  estadoAtual = TOCANDO;
  alarmeAtual = indice;
  alarmeIniciadoEm = millis();
  nivelAvisoEnviado = 0;
  dosePendente = false;
  registroAtual = registrarInicioDose();

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Hora do remedio!");
  lcd.setCursor(0, 1);
  lcd.print(nomeRemedio.substring(0, 16));
}

void desligarLedBuzzer() {
  ledBuzzerLigado = false;
  digitalWrite(LED_PIN, LOW);
  digitalWrite(BUZZER_PIN, LOW);
}

// Chamado quando o paciente aperta o botao (durante o alarme ou com dose pendente).
void abrirParaPaciente() {
  unsigned long decorrido = millis() - alarmeIniciadoEm;
  atualizarDose(decorrido < TEMPO_PRIMEIRO_AVISO ? DOSE_NO_HORARIO : DOSE_ATRASADA, decorrido);
  registroAtual = -1;

  desligarLedBuzzer();
  dosePendente = false;

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Abrindo porta...");
  abrirPortaLentamente();

  estadoAtual = PORTA_ABERTA_ESTADO;
  portaAbertaEm = millis();

  avisarAcessoAposAviso();
}

void atualizarLCDRelogio() {
  static unsigned long ultimaAtualizacao = 0;
  if (millis() - ultimaAtualizacao < 1000) return;
  ultimaAtualizacao = millis();

  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    char horaBuffer[17];
    strftime(horaBuffer, sizeof(horaBuffer), "%H:%M:%S", &timeinfo);
    lcd.setCursor(0, 0);
    lcd.print(horaBuffer);

    lcd.setCursor(0, 1);
    if (dosePendente) {
      lcd.print("Dose pendente!  ");
    } else if (alarmeConfigurado) {
      char alarmeBuffer[17];
      snprintf(alarmeBuffer, sizeof(alarmeBuffer), "%d alarme(s) hoje ", totalAlarmes);
      lcd.print(alarmeBuffer);
    } else {
      lcd.print("Sem alarme     ");
    }

    if (timeinfo.tm_min != ultimoMinutoChecado) {
      ultimoMinutoChecado = timeinfo.tm_min;
      for (int i = 0; i < totalAlarmes; i++) {
        if (timeinfo.tm_min != minutoAlarmes[i]) {
          jaDisparadoHoje[i] = false;
        }
      }
    }

    if (alarmeConfigurado) {
      for (int i = 0; i < totalAlarmes; i++) {
        if (!jaDisparadoHoje[i] &&
            timeinfo.tm_hour == horaAlarmes[i] && timeinfo.tm_min == minutoAlarmes[i]) {
          jaDisparadoHoje[i] = true;
          iniciarAlarme(i);
          break;
        }
      }
    }
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

  portaServo.attach(SERVO_PIN);
  portaServo.write(0);

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

  switch (estadoAtual) {
    case AGUARDANDO:
      if (dosePendente && digitalRead(BUTTON_PIN) == LOW) {
        abrirParaPaciente();
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
        lcd.setCursor(0, 0);
        lcd.print("Avisando familia");
        atualizarDose(DOSE_SEM_ACESSO, decorrido);
        alertarSemAcesso();
        nivelAvisoEnviado = 2;

        // Para de tocar, mas o botao continua abrindo o compartimento.
        dosePendente = true;
        lcd.clear();
        estadoAtual = AGUARDANDO;
        break;
      }

      if (decorrido >= TEMPO_PRIMEIRO_AVISO && nivelAvisoEnviado == 0) {
        desligarLedBuzzer();
        lcd.setCursor(0, 1);
        lcd.print("Avisando cuidad.");
        avisarPrimeiroAtraso();
        nivelAvisoEnviado = 1;
        lcd.setCursor(0, 1);
        lcd.print("                ");
        lcd.setCursor(0, 1);
        lcd.print(nomeRemedio.substring(0, 16));
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

      if (travaLiberada && digitalRead(BUTTON_PIN) == LOW) {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Fechando porta..");
        fecharPortaLentamente();

        lcd.clear();
        estadoAtual = AGUARDANDO;
        break;
      }

      if (millis() - ultimaAtualizacaoContagem >= 500) {
        ultimaAtualizacaoContagem = millis();
        unsigned long restante = (TEMPO_PORTA_ABERTA - (millis() - portaAbertaEm)) / 1000;
        lcd.setCursor(0, 1);
        if (!travaLiberada) {
          lcd.print("Aguarde...      ");
        } else {
          lcd.print("Fecha em: ");
          lcd.print(restante);
          lcd.print("s  ");
        }
      }

      if (millis() - portaAbertaEm >= TEMPO_PORTA_ABERTA) {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Fechando porta..");
        fecharPortaLentamente();

        lcd.clear();
        estadoAtual = AGUARDANDO;
      }
      break;
    }

    case ABASTECENDO: {
      static unsigned long ultimaAtualizacaoAbastecimento = 0;

      bool travaLiberada = (millis() - abastecimentoAbertoEm >= TRAVA_BOTAO);

      if (travaLiberada && digitalRead(BUTTON_PIN) == LOW) {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Fechando...");
        fecharPortaLentamente();

        lcd.clear();
        estadoAtual = AGUARDANDO;
        break;
      }

      if (millis() - ultimaAtualizacaoAbastecimento >= 1000) {
        ultimaAtualizacaoAbastecimento = millis();
        unsigned long restante = (TEMPO_ABASTECIMENTO - (millis() - abastecimentoAbertoEm)) / 1000;
        lcd.setCursor(0, 1);
        if (!travaLiberada) {
          lcd.print("Aguarde...      ");
        } else {
          lcd.print("Fecha em: ");
          lcd.print(restante);
          lcd.print("s  ");
        }
      }

      if (millis() - abastecimentoAbertoEm >= TEMPO_ABASTECIMENTO) {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Fechando...");
        fecharPortaLentamente();

        lcd.clear();
        estadoAtual = AGUARDANDO;
      }
      break;
    }
  }
}
