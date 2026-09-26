#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
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
#define MAX_FAMILIARES 2

LiquidCrystal_I2C lcd(0x27, 16, 2);
Servo portaServo;
WebServer server(80);
DNSServer dnsServer;
Preferences preferences;
Preferences prefsCadastro;

const byte DNS_PORT = 53;
IPAddress apIP(192, 168, 4, 1);

const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = -10800; // Brasília (GMT-3)
const int daylightOffset_sec = 0;

bool modoConfig = false;
String configErro = "";

// Contato que recebe avisos por WhatsApp (via CallMeBot). Cada numero precisa
// da sua propria apikey, obtida pelo proprio dono do numero.
struct Contato {
  String nome;
  String telefone;
  String apikey;
};

String nomePaciente = "";
String nomeRemedio = "";
Contato cuidador;
Contato familiares[MAX_FAMILIARES];
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
const unsigned long CICLO_ALARME = 60UL * 1000UL;
const unsigned long TEMPO_PRIMEIRO_AVISO = 6UL * 60UL * 1000UL;
const unsigned long TEMPO_ALERTA_FINAL = 12UL * 60UL * 1000UL;

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

// ---------- ENVIO DE MENSAGENS (WhatsApp via CallMeBot) ----------

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

// Converte o que o cuidador digitou ("(11) 98765-4321", "+55 11 ...") para o
// formato internacional exigido pela API: +5511987654321.
String telefoneInternacional(const String& telefone) {
  String digitos;
  for (unsigned int i = 0; i < telefone.length(); i++) {
    if (isdigit((uint8_t)telefone[i])) digitos += telefone[i];
  }
  if (digitos.startsWith("00")) digitos = digitos.substring(2);
  if (digitos.length() == 10 || digitos.length() == 11) digitos = "55" + digitos; // DDD + numero, sem pais
  return "+" + digitos;
}

bool contatoPodeReceber(const Contato& contato) {
  return contato.telefone != "" && contato.apikey != "";
}

bool enviarWhatsApp(const Contato& contato, const String& texto) {
  if (!contatoPodeReceber(contato)) return false;
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WhatsApp: sem Wi-Fi, mensagem nao enviada");
    return false;
  }

  String url = "https://api.callmebot.com/whatsapp.php?phone=";
  url += codificarURL(telefoneInternacional(contato.telefone));
  url += "&text=" + codificarURL(texto);
  url += "&apikey=" + codificarURL(contato.apikey);

  WiFiClientSecure cliente;
  cliente.setInsecure(); // prototipo: nao valida o certificado do servidor
  HTTPClient http;
  http.setTimeout(10000);
  if (!http.begin(cliente, url)) {
    Serial.println("WhatsApp: falha ao iniciar conexao");
    return false;
  }
  int codigo = http.GET();
  http.end();

  Serial.print("WhatsApp para ");
  Serial.print(contato.nome);
  Serial.print(": HTTP ");
  Serial.println(codigo);
  return codigo == 200;
}

void enviarParaCuidador(const String& texto) {
  enviarWhatsApp(cuidador, texto);
}

void enviarParaTodos(const String& texto) {
  enviarWhatsApp(cuidador, texto);
  for (int i = 0; i < MAX_FAMILIARES; i++) {
    enviarWhatsApp(familiares[i], texto);
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
  enviarParaCuidador("Zelo+: " + nomePaciente + " ainda não acessou o dispenser para tomar " +
                     nomeRemedio + " (horário das " + horarioAlarmeAtual() +
                     "). O alarme continua tocando.");
}

void alertarSemAcesso() {
  enviarParaTodos("Zelo+ ALERTA: " + nomePaciente + " não acessou a caixa de remédios após 12 minutos do horário das " +
                  horarioAlarmeAtual() + " (" + nomeRemedio +
                  "). Algum problema pode ter ocorrido. Por favor, verifique o paciente.");
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
  prefsCadastro.putString((p + "_key").c_str(), contato.apikey);
}

void carregarContato(const char* prefixo, Contato& contato) {
  String p = prefixo;
  contato.nome = prefsCadastro.getString((p + "_nome").c_str(), "");
  contato.telefone = prefsCadastro.getString((p + "_tel").c_str(), "");
  contato.apikey = prefsCadastro.getString((p + "_key").c_str(), "");
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

  salvarContato("cuid", cuidador);
  for (int i = 0; i < MAX_FAMILIARES; i++) {
    salvarContato(prefixoFamiliar(i).c_str(), familiares[i]);
  }
}

void carregarCadastro() {
  nomePaciente = prefsCadastro.getString("nome", "");
  nomeRemedio = prefsCadastro.getString("remedio", "");

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

String camposContato(const char* titulo, const char* prefixo, const Contato& contato, bool obrigatorio) {
  String p = prefixo;
  String html = "<fieldset style='border:1px solid #d1d5db;border-radius:6px;margin:12px 0;padding:8px 12px'>";
  html += "<legend>" + String(titulo) + "</legend>";
  html += campoTexto("Nome:", (p + "_nome").c_str(), contato.nome, "text", "", obrigatorio);
  html += campoTexto("Celular (WhatsApp):", (p + "_tel").c_str(), contato.telefone, "tel", "(11) 98765-4321", obrigatorio);
  html += campoTexto("Chave CallMeBot (apikey):", (p + "_key").c_str(), contato.apikey, "text", "ex.: 123456", false);
  if (contato.telefone != "" && contato.apikey == "") {
    html += "<p style='color:#b45309;font-size:13px;margin:0 0 6px'>Sem a chave este numero nao recebe avisos.</p>";
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

  html += camposContato("Cuidador", "cuid", cuidador, true);
  for (int i = 0; i < MAX_FAMILIARES; i++) {
    String titulo = "Familiar " + String(i + 1) + " (opcional)";
    html += camposContato(titulo.c_str(), prefixoFamiliar(i).c_str(), familiares[i], false);
  }

  html += "<details style='margin-bottom:12px;font-size:14px'><summary>Como obter a chave do WhatsApp</summary>";
  html += "<p>Os avisos sao enviados pelo servico gratuito CallMeBot. Cada pessoa que vai receber avisos deve, no proprio celular:</p>";
  html += "<ol><li>Abrir <a href='https://www.callmebot.com/blog/free-api-whatsapp-messages/' target='_blank'>callmebot.com</a> e salvar nos contatos o numero indicado la.</li>";
  html += "<li>Enviar pelo WhatsApp para esse contato: <i>I allow callmebot to send me messages</i></li>";
  html += "<li>Copiar a chave (apikey) que chegar na resposta e colar aqui.</li></ol></details>";

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

    html += "<button type='button' onclick='testarMensagens()' style='width:100%;padding:10px;background:#16a34a;color:white;border:none;border-radius:6px;font-size:15px'>Enviar mensagem de teste no WhatsApp</button>";
  }

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
    if (c.telefone == "") continue;
    resultado += rotulos[i] + " (" + c.nome + "): ";
    if (c.apikey == "") {
      resultado += "sem chave CallMeBot\n";
    } else if (enviarWhatsApp(c, texto)) {
      resultado += "enviado\n";
    } else {
      resultado += "falhou (confira numero e chave)\n";
    }
  }

  server.send(200, "text/plain; charset=utf-8", resultado);
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
  contato.apikey = server.arg(p + "_key");
  contato.nome.trim();
  contato.telefone.trim();
  contato.apikey.trim();
}

void handleSalvar() {
  if (server.hasArg("paciente") && server.hasArg("remedio")) {
    bool jaTinhaCadastro = alarmeConfigurado;

    nomePaciente = server.arg("paciente");
    nomeRemedio = server.arg("remedio");
    lerContatoDoFormulario("cuid", cuidador);
    for (int i = 0; i < MAX_FAMILIARES; i++) {
      lerContatoDoFormulario(prefixoFamiliar(i).c_str(), familiares[i]);
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
  server.on("/trocar-wifi", handleTrocarWifi);
  server.begin();
}

void setup() {
  Serial.begin(115200);

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
