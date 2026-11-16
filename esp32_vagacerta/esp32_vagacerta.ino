#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <ESP32Servo.h>

const char* REDE  = "roteador";
const char* SENHA = "12345678";

const char* REDE_AP  = "VagaCerta";
const char* SENHA_AP = "12345678";

int TRIG1 = 18;
int ECHO1 = 27;
int VERDE1 = 12;
int VERMELHO1 = 14;

int TRIG2 = 23;
int ECHO2 = 19;
int VERDE2 = 16;
int VERMELHO2 = 4;

int LIMITE = 10;

LiquidCrystal_I2C lcd(0x27, 16, 2);

Servo cancela;
int PINO_SERVO = 13;
int ABERTA = 90;
int FECHADA = 0;

int vaga1Livre = 1;
int vaga2Livre = 1;
int modo = 0;
int cancelaAberta = -1;
int lcdLivres = -1;
int lcdV1 = -1;
int lcdV2 = -1;
int lcdModo = -1;
unsigned long ultimaLeitura = 0;

int LEITURAS_IGUAIS = 5;
int contaMudanca1 = 0;
int contaMudanca2 = 0;

WebServer server(80);

int medirDistancia(int trig, int echo) {
  digitalWrite(trig, LOW);
  delayMicroseconds(2);
  digitalWrite(trig, HIGH);
  delayMicroseconds(10);
  digitalWrite(trig, LOW);

  long duracao = pulseIn(echo, HIGH, 30000);

  if (duracao == 0) {
    return 999;
  }
  return duracao * 0.034 / 2;
}

int lerLivre(int distancia) {
  if (distancia < LIMITE) {
    return 0;
  }
  return 1;
}

int filtrarVaga(int leitura, int estadoAtual, int &contador) {
  if (leitura == estadoAtual) {
    contador = 0;
    return estadoAtual;
  }
  contador++;
  if (contador >= LEITURAS_IGUAIS) {
    contador = 0;
    return leitura;
  }
  return estadoAtual;
}

void acenderLeds(int livre, int verde, int vermelho) {
  if (livre == 1) {
    digitalWrite(verde, HIGH);
    digitalWrite(vermelho, LOW);
  } else {
    digitalWrite(verde, LOW);
    digitalWrite(vermelho, HIGH);
  }
}

int totalLivres() {
  return vaga1Livre + vaga2Livre;
}

String nomeModo() {
  if (modo == 1) return "Aberta manualmente";
  if (modo == 2) return "Entrada bloqueada";
  return "Automatico";
}

void atualizarCancela() {
  int abrir = 0;

  if (modo == 1) {
    abrir = 1;
  } else if (modo == 2) {
    abrir = 0;
  } else if (totalLivres() > 0) {
    abrir = 1;
  }

  if (abrir != cancelaAberta) {
    cancelaAberta = abrir;
    if (abrir == 1) {
      cancela.write(ABERTA);
    } else {
      cancela.write(FECHADA);
    }
  }
}

void atualizarLCD() {
  if (lcdLivres == totalLivres() && lcdV1 == vaga1Livre && lcdV2 == vaga2Livre && lcdModo == modo) {
    return;
  }
  lcdLivres = totalLivres();
  lcdV1 = vaga1Livre;
  lcdV2 = vaga2Livre;
  lcdModo = modo;

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Vagas livres: ");
  lcd.print(totalLivres());

  lcd.setCursor(0, 1);
  if (modo == 2) {
    lcd.print("ENTRADA BLOQ.");
  } else if (totalLivres() == 0) {
    lcd.print("ESTAC. LOTADO!");
  } else if (vaga1Livre == 1 && vaga2Livre == 1) {
    lcd.print("Livre: vagas 1 2");
  } else if (vaga1Livre == 1) {
    lcd.print("Livre: vaga 1");
  } else {
    lcd.print("Livre: vaga 2");
  }
}

const char PAGINA[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="pt-BR">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>VagaCerta</title>
<style>
  body { font-family: Arial, sans-serif; background: #f2f2f2; margin: 0; padding: 16px; }
  h1 { margin: 0 0 12px; }
  .card { background: #fff; border-radius: 8px; padding: 16px; margin-bottom: 12px; }
  .vagas { display: flex; gap: 12px; }
  .vaga { flex: 1; border-radius: 8px; padding: 16px; text-align: center; color: #fff; font-weight: bold; background: #9e9e9e; }
  .livre { background: #2e7d32; }
  .ocupada { background: #c62828; }
  button { width: 100%; padding: 14px; margin-top: 8px; font-size: 16px; border: none; border-radius: 8px; color: #fff; }
  .abrir { background: #1565c0; }
  .bloquear { background: #c62828; }
  .auto { background: #555; }
</style>
</head>
<body>
  <h1>VagaCerta</h1>

  <div class="card">
    <div class="vagas">
      <div id="v1" class="vaga">Vaga 1<br><span id="t1">-</span></div>
      <div id="v2" class="vaga">Vaga 2<br><span id="t2">-</span></div>
    </div>
    <p>Vagas livres: <b id="livres">-</b></p>
    <p>Cancela: <b id="cancela">-</b></p>
    <p>Modo: <b id="modo">-</b></p>
    <p id="aviso"></p>
  </div>

  <div class="card">
    <button class="abrir" onclick="enviar('/abrir')">Abrir cancela</button>
    <button class="bloquear" onclick="enviar('/bloquear')">Bloquear entrada</button>
    <button class="auto" onclick="enviar('/auto')">Modo automatico</button>
  </div>

<script>
function pintar(caixa, texto, livre) {
  document.getElementById(caixa).className = 'vaga ' + (livre ? 'livre' : 'ocupada');
  document.getElementById(texto).innerText = livre ? 'LIVRE' : 'OCUPADA';
}

function atualizar() {
  fetch('/status').then(function (r) { return r.json(); }).then(function (d) {
    pintar('v1', 't1', d.vaga1);
    pintar('v2', 't2', d.vaga2);
    document.getElementById('livres').innerText = d.livres;
    document.getElementById('cancela').innerText = d.cancela ? 'ABERTA' : 'FECHADA';
    document.getElementById('modo').innerText = d.modo;
    document.getElementById('aviso').innerText = '';
  }).catch(function () {
    document.getElementById('aviso').innerText = 'Sem conexao com o ESP32';
  });
}

function enviar(rota) {
  fetch(rota, { method: 'POST' }).then(atualizar);
}

setInterval(atualizar, 1000);
atualizar();
</script>
</body>
</html>
)HTML";

void paginaInicial() {
  server.send_P(200, "text/html", PAGINA);
}

void enviarStatus() {
  String json = "{";
  json += "\"vaga1\":" + String(vaga1Livre ? "true" : "false") + ",";
  json += "\"vaga2\":" + String(vaga2Livre ? "true" : "false") + ",";
  json += "\"livres\":" + String(totalLivres()) + ",";
  json += "\"cancela\":" + String(cancelaAberta == 1 ? "true" : "false") + ",";
  json += "\"modo\":\"" + nomeModo() + "\"";
  json += "}";
  server.send(200, "application/json", json);
}

void acaoAbrir() {
  modo = 1;
  atualizarCancela();
  atualizarLCD();
  server.send(200, "text/plain", "ok");
}

void acaoBloquear() {
  modo = 2;
  atualizarCancela();
  atualizarLCD();
  server.send(200, "text/plain", "ok");
}

void acaoAuto() {
  modo = 0;
  atualizarCancela();
  atualizarLCD();
  server.send(200, "text/plain", "ok");
}

void conectarWiFi() {
  lcd.clear();
  lcd.print("Conectando WiFi");

  WiFi.mode(WIFI_STA);
  WiFi.begin(REDE, SENHA);

  int tentativas = 0;
  while (WiFi.status() != WL_CONNECTED && tentativas < 20) {
    delay(500);
    Serial.print(".");
    tentativas++;
  }

  String ip;
  if (WiFi.status() == WL_CONNECTED) {
    ip = WiFi.localIP().toString();
    Serial.println("\nConectado! IP: " + ip);
  } else {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(REDE_AP, SENHA_AP);
    ip = WiFi.softAPIP().toString();
    Serial.println("\nRede propria: " + String(REDE_AP) + " | IP: " + ip);
  }

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Acesse:");
  lcd.setCursor(0, 1);
  lcd.print(ip);
  delay(4000);
}

void setup() {
  Serial.begin(115200);

  pinMode(TRIG1, OUTPUT);
  pinMode(ECHO1, INPUT);
  pinMode(VERDE1, OUTPUT);
  pinMode(VERMELHO1, OUTPUT);

  pinMode(TRIG2, OUTPUT);
  pinMode(ECHO2, INPUT);
  pinMode(VERDE2, OUTPUT);
  pinMode(VERMELHO2, OUTPUT);

  lcd.init();
  lcd.backlight();

  cancela.setPeriodHertz(50);
  cancela.attach(PINO_SERVO, 500, 2400);
  cancela.write(FECHADA);

  conectarWiFi();

  server.on("/", HTTP_GET, paginaInicial);
  server.on("/status", HTTP_GET, enviarStatus);
  server.on("/abrir", HTTP_POST, acaoAbrir);
  server.on("/bloquear", HTTP_POST, acaoBloquear);
  server.on("/auto", HTTP_POST, acaoAuto);
  server.begin();
}

void loop() {
  server.handleClient();

  if (millis() - ultimaLeitura >= 200) {
    ultimaLeitura = millis();

    int dist1 = medirDistancia(TRIG1, ECHO1);
    delay(60);
    int dist2 = medirDistancia(TRIG2, ECHO2);

    vaga1Livre = filtrarVaga(lerLivre(dist1), vaga1Livre, contaMudanca1);
    vaga2Livre = filtrarVaga(lerLivre(dist2), vaga2Livre, contaMudanca2);

    acenderLeds(vaga1Livre, VERDE1, VERMELHO1);
    acenderLeds(vaga2Livre, VERDE2, VERMELHO2);

    atualizarCancela();
    atualizarLCD();

    Serial.print("Vaga1: ");
    Serial.print(dist1);
    Serial.print(" cm | Vaga2: ");
    Serial.print(dist2);
    Serial.print(" cm | Livres: ");
    Serial.println(totalLivres());
  }
}
