# Zelo+ — Próximas etapas (registro de decisões)

Registro de trabalho interno. Não faz parte do guia do produto.

## 1. Tela OLED 2,42" no lugar do LCD 16x2 — aguardando a peça

**Motivo:** no ensaio prático do protótipo, o LCD 16x2 mostrou leitura difícil para o
público idoso. O contraste é baixo (fundo azul iluminado) e os caracteres têm só cerca
de 5 mm de altura.

**Decisão:** substituir pela tela **OLED 2,42" 128×64, controlador SSD1309, ligação
I2C** (endereço 0x3C).

**Compra:**
- Modelo: OLED 2,42" 128×64 SSD1309, 7 pinos, SPI/I2C.
- Cor: branca ou amarela. A amarela é a sugerida, por cansar menos a vista.
- Preferir anúncio **já configurado para I2C**. Se vier em SPI, a passagem para I2C
  exige mudar resistores de posição com solda.
- Preço de referência: cerca de R$ 25–31 (Mercado Livre, set. 2026), contra R$ 24,90
  do LCD atual.

**Ganhos esperados:**
- Contraste máximo e boa leitura de lado.
- Hora com cerca de 14–17 mm de altura, contra 5 mm no LCD.
- Acentos exibidos direto, sem os caracteres especiais do LCD.

**Quando a peça chegar:**
1. **Código:**
   - Trocar a biblioteca LiquidCrystal_I2C pela U8g2 e adaptar
     `escreverLinhaLCD` e `atualizarLCDRelogio`.
   - Hora em fonte grande.
   - Deslocar a imagem 1–2 pontos a cada minuto e reduzir o brilho à noite, para
     evitar o desgaste da imagem fixa.
   - Remover os caracteres especiais de ç e ã.
2. **Ligação:** a mesma do LCD (VCC, GND, SDA → GPIO 21, SCL → GPIO 22).
3. **Gabinete:**
   - A janela passa de 66 × 18 mm para cerca de 57 × 29 mm, centrada a 22,5 mm da
     base e no eixo central.
   - Conferir as medidas reais da placa recebida.
4. **Guia:**
   - Partes C (seção do LCD), B (desenho técnico, B4), E, F (lista e preço) e G
     (custos).
   - Diagramas de ligação.
   - Referências.
5. **Ensaios:** leitura a 1 m e a 2 m, de frente e de lado, com as telas de espera,
   alarme, retirada e dose pendente.

## 2. Alternância automática de Wi-Fi — implementada, falta validar no hardware

- Ao ligar, o dispenser tenta a rede salva. Se ela não conectar, abre sozinho a
  rede ZeloPlus (senha `zelo1234`, 192.168.4.1).
- A página mostra um aviso com o motivo e os botões "Tentar conectar novamente" e
  "Trocar rede Wi-Fi"; a hora é acertada pelo celular ao abrir a página.
- Nova tentativa automática a cada 10 minutos, só quando ninguém está conectado à
  ZeloPlus.
- Não citar placa com Wi-Fi de 5 GHz no guia. Para a apresentação, usar o
  roteamento do celular em 2,4 GHz, se quiser mostrar o Telegram.

## 3. Relógio DS3231 — próxima etapa, necessária ao produto

- Já suportado pelo programa e registrado no guia como próxima etapa (E8, E9, F4).
- Sem o módulo e sem internet, depois de um desligamento total, o LCD mostra `Acerte
  a hora` / `pelo celular` e o LED pisca.

## 4. Já no código, falta validar no hardware

- Leitura da caixa do remédio por foto (IA, Gemini, camada gratuita), Passo 20 do
  guia. Exige a chave criada no Google AI Studio, cadastrada no quadro "Leitura da
  caixa por foto". O modelo padrão é `gemini-3.5-flash` e pode ser trocado na página.
  A consulta do código de barras à CMED fica para depois.

- Correção da lista de redes vazia fora de casa.
- Botão "Usar sem internet".
- Botão apertado ao ligar leva à configuração.
- Aviso "Acerte a hora".
- Pendentes de ensaio: abastecimento guiado, ç/ã no visor, DS3231 e `MODO_TESTE 0`.

## 5. Modelo próprio de IA — etapa futura (cerca de 12 meses)

- O curso é de Inteligência Artificial: a meta é um modelo simples e próprio, para o
  Zelo+ não depender de modelos comerciais.
- Registrado no guia como H7: localização do texto, OCR e comparação com a lista da
  CMED; roda no celular ou num servidor local; avaliado contra o Gemini como linha de
  base.
