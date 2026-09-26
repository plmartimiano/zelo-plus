# Zelo+ — Dispenser Inteligente de Medicamentos (firmware ESP32)

Protótipo de hardware/firmware do dispenser automático de medicamentos para idosos
do projeto acadêmico Zelo+. Projeto PlatformIO (framework Arduino, placa `esp32dev`).

```
zelo-plus/
├── platformio.ini   # ambiente esp32dev + dependências
└── src/main.cpp     # firmware completo (máquina de estados + web + captive portal)
```

## Como compilar e gravar

1. Abra a pasta `zelo-plus/` no VSCode com a extensão PlatformIO.
2. Ajuste `upload_port` no `platformio.ini` (no Windows: Gerenciador de Dispositivos →
   "Silicon Labs CP210x USB to UART Bridge"), ou remova a linha para autodetecção.
3. `PlatformIO: Upload` e depois `PlatformIO: Monitor` (115200 baud).

## Fluxo de funcionamento

1. Cuidador acessa a página web servida pelo ESP32 e cadastra nome do idoso, remédio e
   um ou mais horários.
2. No **primeiro cadastro** o compartimento abre para abastecimento.
3. O compartimento fecha pelo botão físico (trava de 7 s contra toque duplo) ou
   sozinho após 3 minutos.
4. No horário: buzzer + LED piscando e nome do remédio no LCD.
5. Idoso aperta o botão → alarme para, compartimento abre suavemente (servo).
6. Idoso retira o remédio e fecha pelo botão (mesma trava de 7 s) ou timeout de 3 min.
7. Repete todo dia nos mesmos horários.
8. "Abrir compartimento para reposição" na página (dupla confirmação) abre sem refazer o
   cadastro.
9. Adicionar horários a um cadastro existente **não** abre o compartimento.

## Hardware e pinagem

| Função | GPIO |
|---|---|
| Buzzer (ativo) | 15 |
| LED (+ resistor 220–330 Ω) | 4 |
| Botão (INPUT_PULLUP, outra perna no GND) | 5 |
| Servo (sinal) | 13 |
| LCD I2C 16x2 (PCF8574, `0x27`) — SDA | 21 |
| LCD I2C — SCL | 22 |

- Servo: 0° = fechado, 90° = aberto. **Alimentação externa de 5 V**, com GND
  compartilhado com o ESP32 (o 3V3 da placa causa reinícios por queda de tensão).
- LCD: GND→GND, VCC→3V3, SDA→21, SCL→22.

## Wi-Fi (captive portal)

Não há credenciais fixas no código. Sem rede salva (ou se a conexão falhar), o ESP32
cria a rede aberta **ZeloPlus-Config**; ao conectar, o celular abre a página em
`192.168.4.1`, onde o cuidador escolhe a rede e digita a senha. A rede fica salva em
`Preferences` (namespace `wifi`). O link "Trocar rede Wi-Fi" apaga a rede e reinicia no
modo de configuração.

## Persistência do cadastro

Nome do idoso, remédio e horários ficam salvos em `Preferences` (namespace
`cadastro`, chaves `nome`, `remedio`, `total`, `horas`, `minutos`) a cada
"Salvar alarme" e são recarregados no `setup()`. Após um reinício ou queda de energia
o dispenser volta a tocar nos horários cadastrados sem precisar recadastrar, e o
próximo salvamento não reabre o compartimento (já conta como cadastro existente).

## Máquina de estados (`loop()`)

- `AGUARDANDO` — relógio no LCD, checa horários a cada minuto.
- `TOCANDO` — buzzer + LED a cada 150 ms, aguarda o botão.
- `PORTA_ABERTA_ESTADO` — aberto após o alarme; botão (após 7 s) ou timeout de 3 min.
- `ABASTECENDO` — aberto para o cuidador (primeiro cadastro ou reposição manual).

`modoConfig` separa o modo de configuração de Wi-Fi do modo normal.

## Limitações conhecidas

1. Um idoso/remédio por vez (exigiria refatorar para array de estruturas).
2. Máximo de 6 horários por dia (`MAX_ALARMES`).
3. O captive portal pode não abrir sozinho em alguns celulares (abrir o navegador
   manualmente funciona).
4. Sem HTTPS/autenticação: qualquer pessoa na mesma rede pode alterar o cadastro.
5. Se a placa reiniciar dentro do minuto de um alarme que já tocou, ele pode tocar de
   novo (o controle "já disparado" fica só em RAM).

## Decisões de projeto

- Página web no próprio ESP32, para o cuidador não mexer no firmware.
- Trava de 7 s no botão contra fechamento acidental por toque duplo.
- Abertura automática só no primeiro cadastro.
- Dupla confirmação no botão de reposição.
- Wi-Fi configurável por portal, para replicar o dispositivo em outros locais.

## Próximos passos sugeridos

- Suporte a múltiplos idosos/compartimentos, se o escopo exigir.
- Autenticação simples na página web.
- Integração com backend/app do cuidador para monitoramento remoto.
- Testar o captive portal em mais modelos de celular.
- Validar fiação/protoboard para uso contínuo (certificação, testes com ILPIs).
