// Zelo+ — Acionamento da tampa do compartimento (impressao 3D)
// Pecas: alavanca da tampa, braco do servo e pino com argola.
// Abra no OpenSCAD, escolha a peca em PECA e exporte o STL (F6, depois F7).
//
// Montagem (corte lateral; Y para tras e Z para cima, em mm a partir da frente e da base):
//   eixo da dobradica da tampa ........ Y 94,5  Z 99   (13,8 mm atras da divisoria)
//   eixo do servo SG90 ................. Y 98,5  Z 73   (eixo paralelo ao da dobradica)
//   furo do pino no braco do servo ..... a 20 mm do eixo do servo
//   rasgo da alavanca .................. de 6,0 a 12,3 mm do eixo da dobradica, a -35 graus
//   curso do servo ..................... 47 graus para a tampa ir de 0 a 90 graus

PECA = "alavanca";   // "alavanca", "braco" ou "pino"

PHI0 = -35;            // angulo da alavanca com a tampa fechada
R_BRACO = 20;          // raio do furo do pino no braco do servo
RASGO_INI = 6.0;
RASGO_FIM = 12.3;
FURO = 3.4;            // folga para o pino de 3,0 mm
PAREDE = 1.6;
ESP_ALAVANCA = 3.0;
ESP_BRACO = 3.2;
PINO_D = 3.0;
// braco simples original do SG90 (encaixe)
HORN_L = 15.5;  HORN_CUBO = 3.8;  HORN_PONTA = 2.3;

$fn = 96;
u = [cos(PHI0), sin(PHI0)];
function ao_longo(r) = r * u;
rb = FURO / 2 + PAREDE;

module bossa()  { hull() { translate(ao_longo(RASGO_INI)) circle(rb); translate(ao_longo(RASGO_FIM)) circle(rb); } }
module rasgo()  { hull() { translate(ao_longo(RASGO_INI)) circle(FURO / 2); translate(ao_longo(RASGO_FIM)) circle(FURO / 2); } }
module sapata() { translate([-12, -3]) square([10.2, 2]); }   // face de colagem em Z = -1

module alavanca() {
  linear_extrude(ESP_ALAVANCA)
    difference() {
      intersection() {
        union() { sapata(); hull() { sapata(); translate(ao_longo(RASGO_INI)) circle(rb); } bossa(); }
        translate([-20, -30]) square([50, 29]);            // nada acima de Z = -1
      }
      rasgo();
    }
  translate([0, 0, -6]) linear_extrude(9) sapata();           // sapata de 9 mm para a cola
}

module braco() {
  difference() {
    linear_extrude(ESP_BRACO) hull() { circle(6); translate([R_BRACO, 0]) circle(4); }
    translate([0, 0, -1]) cylinder(h = ESP_BRACO + 2, r = 2.9);              // acesso ao parafuso central
    translate([R_BRACO, 0, -1]) cylinder(h = ESP_BRACO + 2, d = FURO);       // furo do pino
    translate([0, 0, ESP_BRACO - 1.6]) linear_extrude(2)                     // encaixe do braco original
      hull() { circle(HORN_CUBO + 0.2); translate([HORN_L, 0]) circle(HORN_PONTA + 0.2); }
    for (r = [8, 12]) translate([r, 0, -1]) cylinder(h = ESP_BRACO + 2, r = 0.65);  // parafusos M2
  }
}

module pino() {
  L = ESP_ALAVANCA + 0.5 + ESP_BRACO + 2.6;   // alavanca + folga + braco + trava
  ez = PINO_D / 2 - 0.3;                       // base plana de 0,3 mm na mesa
  intersection() {
    union() {
      difference() {
        union() {
          translate([0, 0, ez]) rotate([0, 90, 0]) cylinder(h = L, d = PINO_D);
          translate([L - 1.2, 0, ez]) rotate([0, 90, 0]) cylinder(h = 1.2, r1 = PINO_D / 2 + 0.35, r2 = PINO_D / 2 - 0.4);
        }
        translate([L - 4.2, -0.4, -2]) cube([4.3, 0.8, 10]);   // fenda: as metades flexionam ao encaixar
      }
      translate([-1, 0, ez]) rotate([0, 90, 0]) cylinder(h = 1, r = PINO_D / 2 + 1);
      translate([-1 - 6 + 0.6, 0, 0]) linear_extrude(2.4) difference() { circle(6); circle(3.6); }
    }
    translate([-50, -50, 0]) cube([100, 100, 50]);
  }
}

if (PECA == "alavanca") alavanca();
if (PECA == "braco") braco();
if (PECA == "pino") pino();
