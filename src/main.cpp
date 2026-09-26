#include <Arduino.h>
#include <WiFi.h>
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

String nomeIdoso = "";
String nomeRemedio = "";
int horaAlarmes[MAX_ALARMES];
int minutoAlarmes[MAX_ALARMES];
bool jaDisparadoHoje[MAX_ALARMES];
int totalAlarmes = 0;
bool alarmeConfigurado = false;
int ultimoMinutoChecado = -1;

const unsigned long TEMPO_PORTA_ABERTA = 3UL * 60UL * 1000UL;
const unsigned long TEMPO_ABASTECIMENTO = 3UL * 60UL * 1000UL;
const unsigned long TRAVA_BOTAO = 7UL * 1000UL; // 7 segundos de trava apos abrir

enum Estado { AGUARDANDO, TOCANDO, PORTA_ABERTA_ESTADO, ABASTECENDO };
Estado estadoAtual = AGUARDANDO;

unsigned long portaAbertaEm = 0;
unsigned long abastecimentoAbertoEm = 0;
unsigned long ultimoToggleAlarme = 0;
bool ledBuzzerLigado = false;

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

// ---------- PERSISTENCIA DO CADASTRO ----------
// Namespace "cadastro" em Preferences (separado do namespace "wifi"),
// para o cadastro sobreviver a reinicios e quedas de energia.

void salvarCadastro() {
  uint8_t horas[MAX_ALARMES];
  uint8_t minutos[MAX_ALARMES];
  for (int i = 0; i < totalAlarmes; i++) {
    horas[i] = (uint8_t)horaAlarmes[i];
    minutos[i] = (uint8_t)minutoAlarmes[i];
  }

  prefsCadastro.putString("nome", nomeIdoso);
  prefsCadastro.putString("remedio", nomeRemedio);
  prefsCadastro.putUChar("total", (uint8_t)totalAlarmes);
  prefsCadastro.putBytes("horas", horas, totalAlarmes);
  prefsCadastro.putBytes("minutos", minutos, totalAlarmes);
}

void carregarCadastro() {
  nomeIdoso = prefsCadastro.getString("nome", "");
  nomeRemedio = prefsCadastro.getString("remedio", "");

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
    html += "<option value='" + WiFi.SSID(i) + "'>" + WiFi.SSID(i) + "</option>";
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
  lcd.print("Configurar Wi-Fi:");
  lcd.setCursor(0, 1);
  lcd.print("Rede: ZeloPlus-Config");

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

void handleRoot() {
  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>Zelo+ Dispenser</title></head><body style='font-family:sans-serif;max-width:400px;margin:20px auto;padding:0 16px'>";
  html += "<h2>Zelo+ - Cadastro de alarme</h2>";
  html += "<form action='/salvar' method='POST'>";
  html += "<label>Nome do idoso:</label><br>";
  html += "<input type='text' name='nome' value='" + nomeIdoso + "' style='width:100%;padding:8px;margin:6px 0' required><br>";
  html += "<label>Nome do remedio:</label><br>";
  html += "<input type='text' name='remedio' value='" + nomeRemedio + "' style='width:100%;padding:8px;margin:6px 0' required><br>";
  html += "<label>Horarios do remedio:</label><br>";
  html += "<div id='horarios-container'>";

  if (totalAlarmes > 0) {
    for (int i = 0; i < totalAlarmes; i++) {
      char valor[6];
      snprintf(valor, sizeof(valor), "%02d:%02d", horaAlarmes[i], minutoAlarmes[i]);
      html += "<div style='margin-bottom:6px'><input type='time' name='horario' value='" + String(valor) + "' style='width:100%;padding:8px' required></div>";
    }
  } else {
    html += "<div style='margin-bottom:6px'><input type='time' name='horario' style='width:100%;padding:8px' required></div>";
  }

  html += "</div>";
  html += "<button type='button' onclick='adicionarHorario()' style='width:100%;padding:10px;background:#e5e7eb;border:none;border-radius:6px;font-size:15px;margin-bottom:12px'>+ Adicionar horario</button><br>";
  html += "<button type='submit' style='width:100%;padding:12px;background:#2563eb;color:white;border:none;border-radius:6px;font-size:16px'>Salvar alarme</button>";
  html += "</form>";

  if (alarmeConfigurado) {
    html += "<p style='margin-top:20px;color:green'>Remedio: " + nomeRemedio + " - " + nomeIdoso + "</p>";
    html += "<p style='color:green'>Horarios cadastrados: ";
    for (int i = 0; i < totalAlarmes; i++) {
      if (horaAlarmes[i] < 10) html += "0";
      html += String(horaAlarmes[i]) + ":";
      if (minutoAlarmes[i] < 10) html += "0";
      html += String(minutoAlarmes[i]);
      if (i < totalAlarmes - 1) html += ", ";
    }
    html += "</p>";
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

void handleTrocarWifi() {
  preferences.remove("ssid");
  preferences.remove("pass");
  server.send(200, "text/plain", "Wi-Fi esquecido. Reiniciando...");
  delay(1500);
  ESP.restart();
}

void handleSalvar() {
  if (server.hasArg("nome") && server.hasArg("remedio")) {
    bool jaTinhaCadastro = alarmeConfigurado;

    nomeIdoso = server.arg("nome");
    nomeRemedio = server.arg("remedio");

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

    if (!jaTinhaCadastro) {
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
    if (alarmeConfigurado) {
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
          estadoAtual = TOCANDO;
          jaDisparadoHoje[i] = true;
          lcd.clear();
          lcd.setCursor(0, 0);
          lcd.print("Hora do remedio!");
          lcd.setCursor(0, 1);
          lcd.print(nomeRemedio.substring(0, 16));
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
      atualizarLCDRelogio();
      break;

    case TOCANDO:
      if (millis() - ultimoToggleAlarme >= 150) {
        ultimoToggleAlarme = millis();
        ledBuzzerLigado = !ledBuzzerLigado;
        digitalWrite(LED_PIN, ledBuzzerLigado ? HIGH : LOW);
        digitalWrite(BUZZER_PIN, ledBuzzerLigado ? HIGH : LOW);
      }

      if (digitalRead(BUTTON_PIN) == LOW) {
        digitalWrite(LED_PIN, LOW);
        digitalWrite(BUZZER_PIN, LOW);

        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Abrindo porta...");
        abrirPortaLentamente();

        estadoAtual = PORTA_ABERTA_ESTADO;
        portaAbertaEm = millis();
      }
      break;

    case PORTA_ABERTA_ESTADO: {
      static unsigned long ultimaAtualizacaoContagem = 0;

      bool travaLiberada = (millis() - portaAbertaEm >= TRAVA_BOTAO);

      if (travaLiberada && digitalRead(BUTTON_PIN) == LOW) {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Fechando porta...");
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
        lcd.print("Fechando porta...");
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
