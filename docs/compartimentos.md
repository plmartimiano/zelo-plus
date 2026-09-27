# Zelo+ v2 — Três compartimentos de medicamentos

Especificação aprovada pelo responsável do projeto antes da implementação.

## 1. Objetivo

Permitir que o cuidador armazene e programe **até 3 medicamentos diferentes** para o
mesmo paciente. Cada medicamento fica **fixo no seu compartimento**, e essa ligação
fica salva na memória da placa. O sistema sempre indica qual compartimento usar (no
cadastro, na reposição e no alarme), para evitar misturar remédios.

## 2. Hardware

| Compartimento | Servo (pino de sinal) |
|---|---|
| 1 | GPIO 13 |
| 2 | GPIO 14 |
| 3 | GPIO 27 |

- O GPIO 12 foi evitado porque interfere na inicialização do ESP32.
- Buzzer (15), LED (4), botão (5) e LCD (21/22) não mudam.
- Os 3 servos usam a fonte externa de 5 V (recomendado ≥ 2 A), com GND comum. O
  firmware **nunca move dois servos ao mesmo tempo**.
- Sem LED por compartimento: a porta aberta já indica qual remédio pegar.
- Etiquetas **1, 2 e 3** na caixa, na mesma ordem da página.
- Ângulos: 0° fechado, 90° aberto, ajustáveis por compartimento
  (`ANGULO_FECHADO` / `ANGULO_ABERTO`).

## 3. Cadastro (página web)

- Três cartões fixos, um por compartimento: nome do medicamento, até 6 horários e a
  indicação **"Coloque LOSARTANA no compartimento 1"** (atualizada enquanto digita).
- Compartimento em branco = vazio (não toca). É preciso ao menos 1 medicamento.
- Medicamento com nome e sem horário é recusado com aviso.
- Para remover um horário, basta apagá-lo; horários repetidos são ignorados.
- **Troca de medicamento** num compartimento já usado pede confirmação: "O
  compartimento 2 tinha Metformina. Retire todos os comprimidos antigos antes de
  colocar Glifage. Confirmar troca?".
- **Esvaziar compartimento**: apaga o cadastro daquele medicamento e pergunta se deve
  abrir o compartimento para retirar as sobras.

## 4. Abastecimento guiado

- Ao salvar um medicamento **novo** (ou trocado) num compartimento, só esse
  compartimento abre. LCD: `Abast. compart.1` / `LOSARTANA`.
- Vários novos no mesmo salvamento abrem **um de cada vez**, na ordem 1 → 2 → 3; o
  próximo abre depois que o anterior fechar.
- Fechamento: botão (após a trava de 7 s) ou automático em 3 min.
- Mudar só horários não abre nada.

## 5. Reposição

1. "Abrir compartimento para reposição".
2. A página pergunta **qual medicamento** será reposto (só os cadastrados, com o
   compartimento de cada um).
3. Confirmação: "Abrir o compartimento 2 para repor Metformina?".
4. Abre **só esse compartimento**. LCD: `Repor compart.2` / `METFORMINA`.
5. Com o dispenser ocupado (alarme ou outra porta aberta): "Sistema ocupado".

## 6. Alarme e retirada pelo paciente

- Ciclo e tempos não mudam (1 min tocando / 1 min silêncio; aviso ao cuidador aos
  6 min; alerta a todos aos 12 min; `MODO_TESTE` com 30 s / 60 s).
- LCD: `Hora do remedio!` / `LOSARTANA (C1)`, alternando a cada 2 s quando há mais de
  um remédio.
- **Mais de um compartimento no mesmo horário:** um único alarme; **um único toque
  abre todos os compartimentos da dose**, um servo logo após o outro, sem precisar de
  outro toque. LCD: `Retire: C1 C2`.
- Um toque (após a trava de 7 s) **fecha todos**; sem toque, fecham em 3 min.
- Dose pendente (após 12 min): o toque abre todos os compartimentos pendentes.
- Um horário que chega com algum compartimento aberto (reposição, retirada) **espera**
  a porta fechar e toca em seguida.

## 7. Mensagens do Telegram

Textos anteriores + nome do medicamento entre parênteses:

- Cuidador (primeiro aviso): *Zelo+: Paciente "Paulo" não acessou o medicamento das
  "15:26" horas (Losartana).*
- Todos (alerta final): *Zelo+: Paciente "Paulo" não foi até o dispenser no horário
  das "15:26" (Losartana e Metformina).*
- Acesso após aviso: *Zelo+: Paulo acessou o dispenser às 15:27 (dose de Losartana
  das 15:26).*

## 8. Histórico de doses

- Um registro **por medicamento** em cada horário, com nome e compartimento.
- Tabela com coluna "Remédio (Cx)"; CSV com colunas `medicamento` e `compartimento`.
- Resumo de 7 dias com adesão geral e **por medicamento**.
- Registros da versão anterior aparecem como "Compart. 1".

## 9. Memória e migração

- Chaves novas: `m0_*`, `m1_*`, `m2_*` (`_nome`, `_tot`, `_hr`, `_mn`).
- O cadastro antigo (um remédio) migra sozinho para o compartimento 1.
- Histórico antigo é convertido para o novo formato.
- Wi-Fi, contatos e token do Telegram não mudam.

## 10. LCD (16 colunas; "ç" e "ã" como caracteres especiais)

| Situação | Linha 1 | Linha 2 |
|---|---|---|
| Aguardando | `15:26` (centralizado) | `Medicação em dia` |
| Alarme | `Hora do remedio!` | `LOSARTANA (C1)` |
| Retirada | `Retire: C1 C2` | `Fecha em: 170s` |
| Abastecer | `Abast. compart.2` | `METFORMINA` |
| Repor | `Repor compart.3` | `SINVASTATINA` |
| Esvaziar | `Esvaziar comp.1` | `Retire tudo` |
| Pendente | `15:40` (centralizado) | `Dose pendente!` |

## 11. Testes pendentes

- [ ] Histórico: no horário, com atraso, sem acesso e download do CSV.
- [ ] Alerta de 60 s (sem acesso) chegando para cuidador e familiares.
- [ ] Abastecimento guiado de 2 ou 3 compartimentos novos.
- [ ] Reposição escolhendo o medicamento.
- [ ] Dois remédios no mesmo horário (um toque abre os dois).
- [ ] Migração do cadastro atual para o compartimento 1.
