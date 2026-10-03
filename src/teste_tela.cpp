// Programa de TESTE da tela TFT 2,25" (controlador ST7789P3, 76 x 284 pontos).
// Fica separado do firmware do Zelo+: no PlatformIO, escolha o ambiente
// "env:teste-tela" para gravar este teste e "env:esp32dev" para voltar ao Zelo+.
//
// Ligacao (SPI):
//   GND -> GND      VCC -> 3V3
//   SCL -> GPIO 18  SDA -> GPIO 23   (relogio e dados do SPI, nao e I2C)
//   RST -> GPIO 17  DC  -> GPIO 16   CS -> GPIO 26   BL -> GPIO 25 (luz de fundo,
//   acende em nivel baixo)
//
// O que o teste mostra, em sequencia:
//   1. Tela inteira VERMELHA, VERDE e AZUL (confere as cores).
//   2. Telas do Zelo+: em dia, dose pendente, alarme, retirada.
//   3. Acentos e brilho reduzido (luz de fundo a 30 %).

#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <U8g2_for_Adafruit_GFX.h>

#define TELA_CS 26
#define TELA_DC 16
#define TELA_RST 17
#define TELA_BL 25
#define CANAL_BRILHO 0

const int LARGURA = 284;
const int ALTURA = 76;

Adafruit_ST7789 tela(TELA_CS, TELA_DC, TELA_RST);
U8G2_FOR_ADAFRUIT_GFX texto;

uint16_t PRETO, BRANCO, VERDE, VERMELHO, AZUL, ROXO, AMARELO;

// Neste modulo a luz de fundo acende com o BL em nivel BAIXO (GND): o
// transistor do modulo inverte o sinal. Por isso 100 % de brilho = pino em 0.
void brilho(int porcento) {
  ledcWrite(CANAL_BRILHO, (100 - porcento) * 255 / 100);
}

// Troca a fonte mantendo o texto transparente: a biblioteca volta a pintar o
// fundo das letras a cada setFont, o que criava "caixinhas" atras do texto.
void fonte(const uint8_t* f) {
  texto.setFont(f);
  texto.setFontMode(1);
}

void escreverCentro(const char* s, int cx, int y) {
  int largura = texto.getUTF8Width(s);
  texto.drawUTF8(cx - largura / 2, y, s);
}

// Hora de exemplo (comeca em 08:30 e anda com o relogio do ESP32).
void horaExemplo(char* saida) {
  unsigned long minutos = 8 * 60 + 30 + millis() / 60000;
  sprintf(saida, "%02lu:%02lu", (minutos / 60) % 24, minutos % 60);
}

void telaCor(uint16_t cor, const char* nome) {
  tela.fillScreen(cor);
  fonte(u8g2_font_helvB24_tf);
  texto.setForegroundColor(BRANCO);
  escreverCentro(nome, LARGURA / 2, 50);
  Serial.printf("Cor: %s\n", nome);
}

// Hora grande a esquerda e caixa colorida com a situacao a direita.
void telaEspera(const char* linha1, const char* linha2, uint16_t corCaixa) {
  char hora[6];
  horaExemplo(hora);
  tela.fillScreen(PRETO);
  fonte(u8g2_font_logisoso50_tn);
  texto.setForegroundColor(BRANCO);
  texto.drawUTF8(6, 63, hora);
  tela.fillRoundRect(150, 6, 128, 64, 10, corCaixa);
  fonte(u8g2_font_helvB18_tf);
  escreverCentro(linha1, 214, 33);
  escreverCentro(linha2, 214, 59);
  Serial.printf("Espera: %s %s %s\n", hora, linha1, linha2);
}

void telaAlarme() {
  tela.fillScreen(AZUL);
  texto.setForegroundColor(BRANCO);
  fonte(u8g2_font_helvB18_tf);
  escreverCentro("Hora do remédio!", LARGURA / 2, 28);
  fonte(u8g2_font_helvB24_tf);
  escreverCentro("LOSARTANA (C1)", LARGURA / 2, 64);
  Serial.println("Alarme");
}

void telaRetirada() {
  tela.fillScreen(PRETO);
  fonte(u8g2_font_helvB24_tf);
  texto.setForegroundColor(BRANCO);
  escreverCentro("Retire: C1 C2", LARGURA / 2, 32);
  fonte(u8g2_font_helvB18_tf);
  texto.setForegroundColor(AMARELO);
  escreverCentro("Fecha em: 170s", LARGURA / 2, 64);
  Serial.println("Retirada");
}

void telaAcentos(int porcentoBrilho) {
  tela.fillScreen(PRETO);
  brilho(porcentoBrilho);
  fonte(u8g2_font_helvB18_tf);
  texto.setForegroundColor(BRANCO);
  escreverCentro("Medicação ç ã é ô í ú", LARGURA / 2, 30);
  char linha[24];
  sprintf(linha, "Brilho %d%%", porcentoBrilho);
  texto.setForegroundColor(ROXO);
  escreverCentro(linha, LARGURA / 2, 62);
  Serial.printf("Acentos, brilho %d%%\n", porcentoBrilho);
}

void setup() {
  Serial.begin(115200);
  Serial.println("Teste da tela TFT 2,25\" (ST7789P3, 76x284)");

  ledcSetup(CANAL_BRILHO, 5000, 8);
  ledcAttachPin(TELA_BL, CANAL_BRILHO);
  brilho(100);

  tela.init(ALTURA, LARGURA);  // a tela e "em pe": 76 de largura por 284 de altura
  tela.setRotation(1);         // deitada: 284 x 76
  tela.invertDisplay(false);   // este painel mostra as cores certas sem inversao
  texto.begin(tela);

  PRETO = tela.color565(0, 0, 0);
  BRANCO = tela.color565(255, 255, 255);
  VERDE = tela.color565(0x16, 0xa3, 0x4a);
  VERMELHO = tela.color565(0xdc, 0x26, 0x26);
  AZUL = tela.color565(0x25, 0x63, 0xeb);
  ROXO = tela.color565(0x93, 0x33, 0xea);
  AMARELO = tela.color565(0xfa, 0xcc, 0x15);
}

void loop() {
  telaCor(tela.color565(255, 0, 0), "VERMELHO");
  delay(2000);
  telaCor(tela.color565(0, 255, 0), "VERDE");
  delay(2000);
  telaCor(tela.color565(0, 0, 255), "AZUL");
  delay(2000);
  telaEspera("Medicação", "em dia", VERDE);
  delay(4000);
  telaEspera("Dose", "pendente!", VERMELHO);
  delay(4000);
  telaAlarme();
  delay(4000);
  telaRetirada();
  delay(4000);
  telaAcentos(100);
  delay(3000);
  telaAcentos(30);
  delay(3000);
  brilho(100);
}
