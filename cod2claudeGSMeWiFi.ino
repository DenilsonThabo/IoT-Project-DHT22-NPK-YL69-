/*
 * Sistema de Monitoramento de Solo para Cultivo de Arroz
 * Hardware: TTGO T-Call ESP32 (SIM800L integrado)
 * Sensores: YL-69, NPK, DHT22
 * Plataforma: Blynk IoT
 * 
 * Autor: Denilson Matusse
 * Data: 2025
 * Versão: 2.0 - TTGO T-Call
 */

#define BLYNK_TEMPLATE_ID "#########"
#define BLYNK_TEMPLATE_NAME "Monitoramento das Características do Solo"
#define BLYNK_PRINT Serial

// ==================== SELEÇÃO DE MODO ====================
// Descomente APENAS UMA das linhas abaixo:
#define USE_WIFI_MODE      // Usar WiFi
#define USE_GPRS_MODE      // Usar GPRS/GSM

// ==================== BIBLIOTECAS ====================
#ifdef USE_WIFI_MODE
  #include <WiFi.h>
  #include <BlynkSimpleEsp32.h>
#endif

#ifdef USE_GPRS_MODE
  #define TINY_GSM_MODEM_SIM800
  #define TINY_GSM_RX_BUFFER 1024
  #include <TinyGsmClient.h>
  #include <BlynkSimpleTinyGSM.h>
#endif

#include <DHT.h>
#include <HardwareSerial.h>

// ==================== PINAGEM TTGO T-CALL ESP32 ====================
// LEDs
#define LED_WIFI 2          // LED WiFi externo (opcional)
#define LED_STATUS 13       // LED integrado TTGO

// SIM800L (pinos nativos TTGO T-Call)
#define MODEM_RST 5         // Reset do modem
#define MODEM_PWRKEY 4      // Power key
#define MODEM_POWER 23      // Controle de energia
#define MODEM_TX 27         // TX do modem
#define MODEM_RX 26         // RX do modem

// DHT22
#define DHTPIN 15
#define DHTTYPE DHT22

// Sensor de Humidade YL-69
#define SOIL_MOISTURE_PIN 35  // ADC1_CH7 (input only)
#define SOIL_POWER_PIN 32     // Controle de alimentação

// Sensor NPK (RS485)
#define NPK_RX 16             // Serial2 RX
#define NPK_TX 17             // Serial2 TX
#define NPK_RE_DE 25          // Controle DE/RE do MAX485

// ==================== CONFIGURAÇÕES ====================
// Credenciais WiFi
char ssid[] = "#########";
char pass[] = "################";

// Credenciais Blynk
char auth[] = "#############";

// Configurações GPRS
const char apn[] = "internet";           // Movitel/Vodacom
const char gprsUser[] = "";
const char gprsPass[] = "";

// Número para SMS (formato internacional)
const char phoneNumber[] = "++258 123456789";

// ==================== CALIBRAÇÃO SENSOR DE HUMIDADE ====================
int Vseco = 3984;      // Valor ADC em solo seco
int Vhumido = 1632;    // Valor ADC em solo saturado
bool soilCalibrated = true;

// ==================== ESTRUTURAS DE DADOS ====================
struct SensorData {
  float temperature;
  float humidity;
  int soilMoisture;
  int nitrogen;
  int phosphorus;
  int potassium;
  bool sensorOK[3];  // DHT, Soil, NPK
};

struct RiceGrowthStage {
  const char* nome;
  int humiMinima;
  int humiMaxima;
  const char* descricao;
};

RiceGrowthStage riceStages[] = {
  {"GERMINACAO", 80, 100, "Solo saturado"},
  {"VEGETATIVA", 70, 90, "Solo húmido constante"},
  {"FLORAÇÃO", 75, 95, "Alta disponibilidade hídrica"},
  {"MATURACAO", 50, 70, "Redução gradual da água"}
};

int currentStage;  

// ==================== OBJECTOS GLOBAIS ====================
DHT dht(DHTPIN, DHTTYPE);
HardwareSerial npkSerial(2);
SensorData sensorData;
BlynkTimer timer;

#ifdef USE_GPRS_MODE
  HardwareSerial SerialAT(1);
  TinyGsm modem(SerialAT);
  TinyGsmClient client(modem);
#endif

// Variáveis de controle
bool connectionReady = false;
unsigned long lastAlertTime = 0;
const unsigned long ALERT_INTERVAL = 60000;  // 1 minuto entre alertas repetidos
bool lastAlertSent = false;

// ==================== FUNÇÕES DE INICIALIZAÇÃO ====================

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n\n╔════════════════════════════════════════════════╗");
  Serial.println("║   Sistema de Monitoramento de Solo v2.0       ║");
  Serial.println("║   Hardware: TTGO T-Call ESP32                  ║");
  Serial.println("╚════════════════════════════════════════════════╝\n");
  
  // Inicializar pinos
  pinMode(LED_WIFI, OUTPUT);
  pinMode(LED_STATUS, OUTPUT);
  pinMode(SOIL_POWER_PIN, OUTPUT);
  pinMode(NPK_RE_DE, OUTPUT);
  pinMode(MODEM_RST, OUTPUT);
  pinMode(MODEM_PWRKEY, OUTPUT);
  pinMode(MODEM_POWER, OUTPUT);
  
  digitalWrite(LED_WIFI, LOW);
  digitalWrite(LED_STATUS, LOW);
  digitalWrite(SOIL_POWER_PIN, LOW);
  digitalWrite(NPK_RE_DE, LOW);
  
  // Inicializar sensores
  Serial.println("[INIT] Inicializando sensores...");
  dht.begin();
  delay(2000);
  
  npkSerial.begin(4800, SERIAL_8N1, NPK_RX, NPK_TX);
  
  // Testar sensores
  testSensors();
  
  // Calibração
  Serial.println("\n[CALIBRACAO] Status: " + String(soilCalibrated ? "OK" : "NECESSÁRIA"));
  if (!soilCalibrated) {
    Serial.println("  Digite 'C' no monitor serial para calibrar");
  }
  
  // Inicializar conexão
  initializeConnection();
  
  // Configurar timers
  timer.setInterval(5000L, readAndSendData);
  
  Serial.println("\n[SISTEMA] Iniciado com sucesso!");
  Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
  
  // Piscar LED para indicar inicialização
  for (int i = 0; i < 3; i++) {
    digitalWrite(LED_STATUS, HIGH);
    delay(200);
    digitalWrite(LED_STATUS, LOW);
    delay(200);
  }
}

// ==================== TESTE DE SENSORES ====================

void testSensors() {
  Serial.println("\n┌─────────────────────────────────────┐");
  Serial.println("│       TESTE DE SENSORES             │");
  Serial.println("└─────────────────────────────────────┘");
  
  // Teste DHT22
  Serial.print("  [DHT22] ");
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  if (!isnan(t) && !isnan(h)) {
    sensorData.sensorOK[0] = true;
    Serial.println("✓ OK");
    Serial.printf("    └─ Temp: %.1f°C | Hum: %.1f%%\n", t, h);
  } else {
    sensorData.sensorOK[0] = false;
    Serial.println("✗ FALHA");
  }
  
  // Teste YL-69
  Serial.print("  [YL-69] ");
  digitalWrite(SOIL_POWER_PIN, HIGH);
  delay(100);
  int soilValue = analogRead(SOIL_MOISTURE_PIN);
  digitalWrite(SOIL_POWER_PIN, LOW);
  if (soilValue > 0 && soilValue < 4096) {
    sensorData.sensorOK[1] = true;
    Serial.println("✓ OK");
    Serial.printf("    └─ ADC: %d\n", soilValue);
  } else {
    sensorData.sensorOK[1] = false;
    Serial.println("✗ FALHA");
  }
  
  // Teste NPK
  Serial.print("  [NPK]   ");
  if (readNPK()) {
    sensorData.sensorOK[2] = true;
    Serial.println("✓ OK");
    Serial.printf("    └─ N:%d P:%d K:%d mg/kg\n", 
                  sensorData.nitrogen, sensorData.phosphorus, sensorData.potassium);
  } else {
    sensorData.sensorOK[2] = false;
    Serial.println("✗ TIMEOUT");
  }
  
  Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
}

// ==================== CALIBRAÇÃO DO SENSOR ====================

void calibrateSoilSensor() {
  Serial.println("\n╔════════════════════════════════════════════════╗");
  Serial.println("║        CALIBRAÇÃO SENSOR YL-69                 ║");
  Serial.println("╚════════════════════════════════════════════════╝\n");
  
  // Solo seco
  Serial.println("ETAPA 1: SOLO SECO");
  Serial.println("  • Remova e limpe o sensor");
  Serial.println("  • Pressione ENTER...");
  while (!Serial.available()) delay(100);
  while (Serial.available()) Serial.read();
  
  digitalWrite(SOIL_POWER_PIN, HIGH);
  delay(500);
  Vseco = 0;
  for (int i = 0; i < 10; i++) {
    Vseco += analogRead(SOIL_MOISTURE_PIN);
    delay(100);
  }
  Vseco /= 10;
  digitalWrite(SOIL_POWER_PIN, LOW);
  Serial.printf("  ✓ Valor seco: %d\n\n", Vseco);
  
  // Solo saturado
  Serial.println("ETAPA 2: SOLO SATURADO");
  Serial.println("  • Coloque em água/solo úmido");
  Serial.println("  • Pressione ENTER...");
  while (!Serial.available()) delay(100);
  while (Serial.available()) Serial.read();
  
  digitalWrite(SOIL_POWER_PIN, HIGH);
  delay(500);
  Vhumido = 0;
  for (int i = 0; i < 10; i++) {
    Vhumido += analogRead(SOIL_MOISTURE_PIN);
    delay(100);
  }
  Vhumido /= 10;
  digitalWrite(SOIL_POWER_PIN, LOW);
  Serial.printf("  ✓ Valor molhado: %d\n\n", Vhumido);
  
  // Validação
  if (Vseco > Vhumido && (Vseco - Vhumido) > 500) {
    soilCalibrated = true;
    Serial.println("╔════════════════════════════════════════════════╗");
    Serial.println("║           ✓ CALIBRAÇÃO COMPLETA               ║");
    Serial.println("╚════════════════════════════════════════════════╝");
    Serial.printf("\n  Seco: %d | Molhado: %d\n", Vseco, Vhumido);
    Serial.println("\n  Atualize no código:");
    Serial.printf("    int Vseco = %d;\n", Vseco);
    Serial.printf("    int Vhumido = %d;\n\n", Vhumido);
  } else {
    Serial.println("✗ ERRO: Valores inválidos!");
  }
}

// ==================== CONEXÃO ====================

void initializeConnection() {
#ifdef USE_WIFI_MODE
  Serial.println("\n[MODO] WiFi selecionado");
  Serial.print("[WiFi] Conectando");
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
    digitalWrite(LED_STATUS, !digitalRead(LED_STATUS));
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    digitalWrite(LED_WIFI, HIGH);
    digitalWrite(LED_STATUS, HIGH);
    Serial.println(" ✓");
    Serial.print("[WiFi] IP: ");
    Serial.println(WiFi.localIP());
    Serial.print("[WiFi] RSSI: ");
    Serial.println(WiFi.RSSI());
    
    Blynk.config(auth);
    if (Blynk.connect(3000)) {
      connectionReady = true;
      Serial.println("[Blynk] ✓ Conectado via WiFi");
    } else {
      Serial.println("[Blynk] ✗ Falha na conexão");
    }
  } else {
    digitalWrite(LED_WIFI, LOW);
    digitalWrite(LED_STATUS, LOW);
    Serial.println(" ✗ FALHA");
  }
#endif

#ifdef USE_GPRS_MODE
  Serial.println("\n[MODO] GPRS selecionado");
  
  // Configurar modem
  pinMode(MODEM_PWRKEY, OUTPUT);
  pinMode(MODEM_RST, OUTPUT);
  pinMode(MODEM_POWER, OUTPUT);
  
  digitalWrite(MODEM_PWRKEY, LOW);
  digitalWrite(MODEM_RST, HIGH);
  digitalWrite(MODEM_POWER, HIGH);
  
  Serial.println("[Modem] Inicializando SIM800L...");
  SerialAT.begin(9600, SERIAL_8N1, MODEM_RX, MODEM_TX);
  delay(3000);
  
  // Reset modem
  Serial.println("[Modem] Reset...");
  digitalWrite(MODEM_RST, LOW);
  delay(100);
  digitalWrite(MODEM_RST, HIGH);
  delay(3000);
  
  Serial.println("[Modem] Inicializando...");
  if (!modem.restart()) {
    Serial.println("[Modem] ✗ Falha no restart");
    return;
  }
  
  String modemInfo = modem.getModemInfo();
  Serial.println("[Modem] Info: " + modemInfo);
  
  // Aguardar rede
  Serial.print("[Rede] Aguardando registro");
  int netAttempts = 0;
  while (!modem.waitForNetwork(10000) && netAttempts < 3) {
    Serial.print(".");
    netAttempts++;
  }
  
  if (modem.isNetworkConnected()) {
    Serial.println(" ✓");
    Serial.print("[Rede] Operadora: ");
    Serial.println(modem.getOperator());
    Serial.print("[Rede] Sinal: ");
    Serial.println(modem.getSignalQuality());
  } else {
    Serial.println(" ✗ FALHA");
    return;
  }
  
  // Conectar GPRS
  Serial.print("[GPRS] Conectando");
  if (!modem.gprsConnect(apn, gprsUser, gprsPass)) {
    Serial.println(" ✗ FALHA");
    return;
  }
  Serial.println(" ✓");
  
  digitalWrite(LED_WIFI, HIGH);
  digitalWrite(LED_STATUS, HIGH);
  
  // Conectar Blynk
  Serial.print("[Blynk] Conectando via GPRS");
  Blynk.config(modem, auth, "blynk.cloud", 80);
  if (Blynk.connect(5000)) {
    connectionReady = true;
    Serial.println(" ✓");
  } else {
    Serial.println(" ✗ FALHA");
  }
#endif
}

// ==================== LEITURA DE SENSORES ====================

void readAndSendData() {
  digitalWrite(LED_STATUS, HIGH);
  
  // Ler DHT22
  if (sensorData.sensorOK[0]) {
    sensorData.temperature = dht.readTemperature();
    sensorData.humidity = dht.readHumidity();
    
    if (!isnan(sensorData.temperature) && !isnan(sensorData.humidity)) {
      Serial.printf("[DHT22] %.1f°C | %.1f%%\n", 
                    sensorData.temperature, sensorData.humidity);
    } else {
      sensorData.sensorOK[0] = false;
    }
  }
  
  // Ler Humidade do Solo
  if (sensorData.sensorOK[1]) {
    digitalWrite(SOIL_POWER_PIN, HIGH);
    delay(100);
    int soilRaw = analogRead(SOIL_MOISTURE_PIN);
    digitalWrite(SOIL_POWER_PIN, LOW);
    
    sensorData.soilMoisture = map(soilRaw, Vseco, Vhumido, 0, 100);
    sensorData.soilMoisture = constrain(sensorData.soilMoisture, 0, 100);
    
    Serial.printf("[YL-69] %d%% (ADC:%d)\n", sensorData.soilMoisture, soilRaw);
  }
  
  // Ler NPK
  if (sensorData.sensorOK[2]) {
    if (readNPK()) {
      Serial.printf("[NPK] N:%d P:%d K:%d\n", 
                    sensorData.nitrogen, sensorData.phosphorus, sensorData.potassium);
    }
  }
  
  // Enviar ao Blynk
  if (connectionReady) {
    sendToBlynk();
  }
  
  // Verificar alertas
  checkAlerts();
  
  digitalWrite(LED_STATUS, LOW);
}

bool readNPK() {
  byte command[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x07, 0x04, 0x08};
  
  // Limpar buffer
  while (npkSerial.available()) npkSerial.read();
  
  digitalWrite(NPK_RE_DE, HIGH);
  delay(10);
  npkSerial.write(command, sizeof(command));
  npkSerial.flush();
  digitalWrite(NPK_RE_DE, LOW);
  
  unsigned long timeout = millis() + 1000;
  while (npkSerial.available() < 19 && millis() < timeout) {
    delay(10);
  }
  
  if (npkSerial.available() >= 19) {
    byte response[19];
    for (int i = 0; i < 19; i++) {
      response[i] = npkSerial.read();
    }
    
    sensorData.nitrogen = (response[3] << 8) | response[4];
    sensorData.phosphorus = (response[5] << 8) | response[6];
    sensorData.potassium = (response[7] << 8) | response[8];
    
    return true;
  }
  
  return false;
}

// ==================== ENVIO DE DADOS ====================

void sendToBlynk() {
  Blynk.virtualWrite(V0, sensorData.temperature);
  Blynk.virtualWrite(V1, sensorData.humidity);
  Blynk.virtualWrite(V2, sensorData.soilMoisture);
  Blynk.virtualWrite(V3, sensorData.nitrogen);
  Blynk.virtualWrite(V4, sensorData.phosphorus);
  Blynk.virtualWrite(V5, sensorData.potassium);
}

// ==================== SISTEMA DE ALERTAS ====================

void checkAlerts() {
  unsigned long currentTime = millis();
  String alertMessage = "";
  bool criticalAlert = false;
  
  // Temperatura
  if (sensorData.sensorOK[0]) {
    if (sensorData.temperature < 20) {
      alertMessage += "TEMP BAIXA:" + String(sensorData.temperature, 1) + "C\n";
      alertMessage += "Accao: Irrigacao/protecao\n\n";
      criticalAlert = true;
    } else if (sensorData.temperature > 35) {
      alertMessage += "TEMP ALTA:" + String(sensorData.temperature, 1) + "C\n";
      alertMessage += "Accao: Mais agua\n\n";
      criticalAlert = true;
    }
  }
  
  // Humidade do Solo
  if (sensorData.sensorOK[1]) {
    RiceGrowthStage stage = riceStages[currentStage];
    
    if (sensorData.soilMoisture < stage.humiMinima) {
      alertMessage += "SOLO SECO:" + String(sensorData.soilMoisture) + "%\n";
      alertMessage += "Fase:" + String(stage.name) + "\n";
      alertMessage += "Ideal:" + String(stage.humiMinima) + "-" + String(stage.humiMaxima) + "%\n";
      alertMessage += "IRRIGACAO URGENTE!\n\n";
      criticalAlert = true;
    } else if (sensorData.soilMoisture > stage.humiMaxima) {
      alertMessage += "SOLO UMIDO:" + String(sensorData.soilMoisture) + "%\n";
      alertMessage += "Acao: Drenar\n\n";
      criticalAlert = true;
    }
  }
  
  // Nutrientes
  if (sensorData.sensorOK[2]) {
    if (sensorData.nitrogen < 150) {
      alertMessage += "N BAIXO:" + String(sensorData.nitrogen) + "mg/kg\n";
      alertMessage += "Aplicar ureia 40-60kg/ha\n\n";
      criticalAlert = true;
    }
    
    if (sensorData.phosphorus < 20) {
      alertMessage += "P BAIXO:" + String(sensorData.phosphorus) + "mg/kg\n";
      alertMessage += "Aplicar superfosfato\n\n";
      criticalAlert = true;
    }
    
    if (sensorData.potassium < 100) {
      alertMessage += " K BAIXO:" + String(sensorData.potassium) + "mg/kg\n";
      alertMessage += "Aplicar KCl 40-60kg/ha\n\n";
      criticalAlert = true;
    }
  }
  
  // Enviar alertas
  if (criticalAlert && (currentTime - lastAlertTime > ALERT_INTERVAL || !lastAlertSent)) {
    lastAlertTime = currentTime;
    lastAlertSent = true;
    
    Serial.println("\n[ALERTA] Condição crítica detectada!");
    
    // Enviar via Blynk
    if (connectionReady) {
      Blynk.logEvent("critical_alert", alertMessage);
      Serial.println("[ALERTA] Enviado via Blynk");
    }
    
    // Enviar SMS IMEDIATAMENTE
    sendSMS(alertMessage);
  } else if (!criticalAlert) {
    lastAlertSent = false;
  }
}

void sendSMS(String message) {
  Serial.println("[SMS] Preparando envio...");
  
  // Limitar mensagem
  if (message.length() > 160) {
    message = message.substring(0, 157) + "...";
  }
  
#ifdef USE_GPRS_MODE
  Serial.println("[SMS] Enviando via modem TinyGSM...");
  
  if (modem.sendSMS(phoneNumber, message)) {
    Serial.println("[SMS] ✓ Enviado para " + String(phoneNumber));
  } else {
    Serial.println("[SMS] ✗ Falha no envio");
  }
#endif

#ifdef USE_WIFI_MODE
  // Em modo WiFi, usar SIM800L direto via Serial
  Serial.println("[SMS] Inicializando SIM800L para SMS...");
  
  HardwareSerial sim800(1);
  sim800.begin(9600, SERIAL_8N1, MODEM_RX, MODEM_TX);
  delay(1000);
  
  // Enviar comandos AT
  sim800.println("AT");
  delay(500);
  while(sim800.available()) sim800.read();
  
  sim800.println("AT+CMGF=1");
  delay(500);
  while(sim800.available()) sim800.read();
  
  sim800.print("AT+CMGS=\"");
  sim800.print(phoneNumber);
  sim800.println("\"");
  delay(500);
  
  sim800.print(message);
  delay(200);
  sim800.write(26);  // Ctrl+Z
  delay(3000);
  
  Serial.println("[SMS] ✓ Enviado para " + String(phoneNumber));
#endif
}

// ==================== BLYNK CALLBACKS ====================

BLYNK_WRITE(V10) {
  currentStage = param.asInt();

   if (currentStage < 0 || currentStage >= 4) {
    currentStage = 0;
  }
  Serial.println("[Blynk] Fase: " + String(riceStages[currentStage].name));
  Blynk.virtualWrite(V11, riceStages[currentStage].descricao);
}

BLYNK_WRITE(V20) {
  if (param.asInt() == 1) {
    Serial.println("[Blynk] Requisição de teste recebida");
    Blynk.virtualWrite(V20, 0);
    testSensors();
  }
}

// Terminal Blynk (V30)
BLYNK_WRITE(V30) {
  String cmd = param.asStr();
  cmd.toUpperCase();
  
  Serial.println("[Terminal] Comando: " + cmd);
  
  if (cmd == "STATUS" || cmd == "S") {
    String status = "Sistema OK\n";
    status += "DHT:" + String(sensorData.sensorOK[0] ? "OK" : "FALHA") + "\n";
    status += "YL69:" + String(sensorData.sensorOK[1] ? "OK" : "FALHA") + "\n";
    status += "NPK:" + String(sensorData.sensorOK[2] ? "OK" : "FALHA") + "\n";
    Blynk.virtualWrite(V30, status);
  }
  else if (cmd == "TESTE" || cmd == "T") {
    Blynk.virtualWrite(V30, "Testando sensores...\n");
    testSensors();
    Blynk.virtualWrite(V30, "Teste concluido\n");
  }
  else if (cmd == "RESET" || cmd == "R") {
    Blynk.virtualWrite(V30, "Reiniciando...\n");
    delay(1000);
    ESP.restart();
  }
  else if (cmd == "HELP" || cmd == "H") {
    String help = "Comandos:\n";
    help += "STATUS/S - Status\n";
    help += "TESTE/T - Testar\n";
    help += "RESET/R - Reiniciar\n";
    help += "HELP/H - Ajuda\n";
    Blynk.virtualWrite(V30, help);
  }
  else {
    Blynk.virtualWrite(V30, "Comando invalido. Digite HELP\n");
  }
}

// ==================== LOOP PRINCIPAL ====================

void loop() {
  // Comandos serial
  if (Serial.available()) {
    char cmd = Serial.read();
    while(Serial.available()) Serial.read();  // Limpar buffer
    
    if (cmd == 'C' || cmd == 'c') {
      calibrateSoilSensor();
    } else if (cmd == 'T' || cmd == 't') {
      testSensors();
    } else if (cmd == 'R' || cmd == 'r') {
      Serial.println("\n[Sistema] Reiniciando...\n");
      delay(1000);
      ESP.restart();
    } else if (cmd == 'H' || cmd == 'h') {
      Serial.println("\nComandos: C=Calibrar | T=Testar | R=Reiniciar | H=Ajuda\n");
    }
  }
  
  // Manter conexões
#ifdef USE_WIFI_MODE
  if (WiFi.status() != WL_CONNECTED) {
    connectionReady = false;
    digitalWrite(LED_WIFI, LOW);
    Serial.println("[WiFi] Reconectando...");
    initializeConnection();
  } else if (connectionReady) {
    Blynk.run();
  }
#endif

#ifdef USE_GPRS_MODE
  if (connectionReady) {
    Blynk.run();
  }
#endif
  
  timer.run();
  delay(10);
}
