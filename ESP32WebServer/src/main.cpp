#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <esp_system.h>
#include <stdlib.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <LiquidCrystal_I2C.h>
#include <Wire.h>
#include <math.h>
#include <Arduino.h>
#include <FS.h>
#include <SPIFFS.h>
#include <WebSocketsServer.h>

// ===== User defined constants =====
// sudah terdefinisi di header esp32-hal-gpio.h
// #define GPIO_OUT_W1TS_REG 0x3FF44008
// #define GPIO_OUT_W1TC_REG 0x3FF4400C

#define MODE_STA_OR_AP "ap" // "sta" atau "ap"
#define TWO_POINT_CALIBRATION 0 // 0 = single point, 1 = two point
// Single point calibration needs to be filled CAL1_V and CAL1_T
#define CAL1_V (1600) // mv
#define CAL1_T (25)   // ℃
// Two-point calibration needs to be filled CAL2_V and CAL2_T
// CAL1 High temperature point, CAL2 Low temperature point
#define CAL2_V (1300) // mv
#define CAL2_T (15)   // ℃

// Pin definitions
#define PIN_PH 32
#define PIN_TURBIDITY 33
#define PIN_OKSIGEN 34
#define PIN_POTENSIO 35
#define PIN_SUHU 23
#define PIN_RELAY_1 14
#define PIN_RELAY_2 27
#define PIN_RELAY_3 26
#define PIN_RELAY_4 25
#define PIN_RELAY_5 19

// Lookup table for Dissolved Oxygen (DO) in mg/L at different temperatures (0-40°C)
const uint16_t DO_Table[41] = {
    14460, 14220, 13820, 13440, 13090, 12740, 12420, 12110, 11810, 11530,
    11260, 11010, 10770, 10530, 10300, 10080, 9860, 9660, 9460, 9270,
    9080, 8900, 8730, 8570, 8410, 8250, 8110, 7960, 7820, 7690,
    7560, 7430, 7300, 7180, 7070, 6950, 6840, 6730, 6630, 6530, 6410};

const long tempRequestInterval = 750; // Minta suhu setiap 750 ms

// jika mode STA
const char *ssid = "Pertanian IPB utama";
const char *password = "pertanian dan pangan";

// jika mode AP
const char *ap_ssid = "Trainer_Akuaponik";
const char *ap_password = "12345678";

const int maxDataPoints = 30;

// ===== User defined classes =====
class Analog
{
public:
  enum VarType
  {
    VOLTAGE,
    FINAL,
    PERCENT
  };
  /// @brief inisialisasi pin analog dengan filter alpha
  /// @param p pin analog
  /// @param a variabel alpha (0 < a < 1), semakin kecil semakin halus
  Analog(int p, float a) : pin(p), alpha(a), voltage(0.0), final(0.0), percent(0.0), smoothedRaw(0.0)
  {
    pinMode(pin, INPUT);
  }
  /// @brief panggil di loop utama
  void update()
  {
    if (firstRead)
    {
      smoothedRaw = (float)analogRead(pin);
      firstRead = false;
    }
    else
    {
      smoothedRaw = (alpha * (float)analogRead(pin)) + ((1 - alpha) * smoothedRaw);
    }
    voltage = (round(smoothedRaw) / 4095.0f) * 3.3f;
    percent = (round(smoothedRaw) / 4095.0f) * 100.0f;
  }
  /// @brief mendapatkan pin analog
  uint8_t getPin() { return pin; }

  /// @brief mengambil nilai variabel (alpha, voltage, final)
  float getVar(VarType var) const
  { // Tambahkan 'const' karena fungsi ini tidak mengubah state objek
    switch (var)
    {
    case VOLTAGE:
      return voltage;
    case FINAL:
      return final;
    case PERCENT:
      return percent;
    default:
      return 0.0; // Nilai default jika ada case yang tidak terduga
    }
  }

  void setFinal(float v)
  {
    final = v;
  }

private:
  const uint8_t pin;
  float alpha;
  float voltage;
  float final;
  float percent;
  float smoothedRaw;
  bool firstRead = true;
};

// Struktur data untuk menyimpan data sensor
struct DataNode
{
  float ph;
  float turbidity;
  float oksigen;
  float suhu;
  DataNode *next;
};

class DataList
{
public:
  // Konstruktor: Inisialisasi linked list dan batas data
  DataList(int maxPoints) : head(nullptr), tail(nullptr), count(0), maxDataPoints(maxPoints) {}

  // Destruktor: Menghapus semua node di list untuk mencegah memory leak
  ~DataList()
  {
    DataNode *current = head;
    while (current)
    {
      DataNode *next = current->next;
      delete current;
      current = next;
    }
  }

  void addData(float ph, float turb, float oks, float suhu)
  {
    // 1. Buat node baru
    DataNode *newNode = new DataNode{ph, turb, oks, suhu, nullptr};

    // 2. Tambahkan node ke akhir list
    if (tail)
    {
      tail->next = newNode;
      tail = newNode;
    }
    else
    {
      // Jika list kosong, head dan tail menunjuk ke node baru
      head = tail = newNode;
    }

    // 3. Perbarui jumlah node
    count++;

    // 4. Pangkas list jika melebihi batas
    if (count > maxDataPoints)
    {
      DataNode *oldHead = head;
      head = head->next;
      delete oldHead;
      count--;
    }
  }

  // Metode untuk mendapatkan jumlah data saat ini (opsional)
  int getCount() const
  {
    return count;
  }

  // Metode baru untuk mengambil data sebagai string berformat
  String getChartData(const String &sensor) const
  {
    String data = "[";
    DataNode *current = head;

    while (current)
    {
      if (sensor == "ph")
        data += String(current->ph, 2);
      else if (sensor == "turb")
        data += String(current->turbidity, 2);
      else if (sensor == "oks")
        data += String(current->oksigen, 2);
      else if (sensor == "suhu")
        data += String(current->suhu, 2);

      if (current->next)
        data += ",";

      current = current->next;
    }

    data += "]";
    return data;
  }
  // Metode baru untuk mendapatkan node terakhir
  DataNode *getLastNode() const
  {
    return tail;
  }

private:
  DataNode *head;
  DataNode *tail;
  int maxDataPoints;
  int count; // Menghitung node secara efisien
};

// ===== User Global variables =====
Analog phSensor = Analog(PIN_PH, 0.1f);
Analog turbiditySensor = Analog(PIN_TURBIDITY, 0.1f);
Analog oksigenSensor = Analog(PIN_OKSIGEN, 0.1f);
Analog potensiometer = Analog(PIN_POTENSIO, 0.1f);
static float suhuValue = 0;

unsigned long lastTempRequest = 0;

OneWire oneWire(PIN_SUHU);
DallasTemperature sensorSuhu(&oneWire);

unsigned long lastUpdate_sensor = 0;

LiquidCrystal_I2C lcd(0x27, 16, 2);

WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81); // WebSocket di port 81

DataList sensorData(maxDataPoints);

bool relayState[5] = {false, false, false, false, false};
bool autoMode = true; // true = otomatis, false = manual

// new globals for client connection tracking
unsigned long lastClientPing = 0;
const unsigned long clientTimeoutMs = 6000; // jika tidak ada ping dalam 6s -> dianggap tidak connected


// ===== User defined functions =====
// ===== Web =====
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length);

void handleRoot();
void handleData();
void handleLast();
void handleRelayStatus();
// Handler untuk mengirim file script.js
void handleScriptJs();
void handleChartJs();
void handleLuxonJs();
void handleAdapterLuxonJs();
void handleStreamingPluginJs();
void handleSbAdmin2Css();
// ===== LCD I2C =====
void timerLcdI2c();

void fastWrite(uint8_t pin, uint8_t value)
{
  // Buat bitmask: 1 digeser ke kiri sebanyak 'pin'
  const uint32_t bitmask = (1UL << pin);

  // Deklarasi pointer ke register GPIO
  // Keyword 'volatile' penting agar kompilator tidak mengoptimasi penulisan
  volatile uint32_t *set_reg = (volatile uint32_t *)GPIO_OUT_W1TS_REG;
  volatile uint32_t *clear_reg = (volatile uint32_t *)GPIO_OUT_W1TC_REG;

  if (value == 1)
  {
    // Tulis bitmask ke register set untuk menyalakan pin
    *set_reg = bitmask;
  }
  else
  {
    // Tulis bitmask ke register clear untuk mematikan pin
    *clear_reg = bitmask;
  }
}

void handlePhSensor()
{
  const float calibration_value = 21.34 + 0.0f;
  float voltage = phSensor.getVar(Analog::VOLTAGE);

  float phValue = -5.70 * voltage + calibration_value;
  phSensor.setFinal(phValue);
}

float map_float(float x, float in_min, float in_max, float out_min, float out_max) {
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

void handleTurbiditySensor()
{
  float voltage = turbiditySensor.getVar(Analog::VOLTAGE);

  const float clear_point = 1.53;
  const float dirty_point = 0.0;
  const float NTU_MAX = 3000.0f;

  float a = -NTU_MAX / ((clear_point - dirty_point) *
                        (clear_point - dirty_point));

  // float ntu = a * (voltage - dirty_point) * (voltage - dirty_point) + NTU_MAX;
  // float voltage_adjusted = voltage * (5.0 / 3.3);
  // float ntu = -1120.4*sq(voltage)+5742.3*voltage-4353.8;

  if (voltage > clear_point)
    voltage = clear_point;
  else if (voltage < dirty_point)
    voltage = dirty_point;

  float ntu = map_float(voltage, 0, clear_point, 3000, 0);

  if (ntu < 0)
    ntu = 0;
  if (ntu > NTU_MAX)
    ntu = NTU_MAX;

  // Serial.println(voltage);

  turbiditySensor.setFinal(ntu);
}

int16_t readDO(uint32_t voltage_mv, uint8_t temperature_c)
{

#if TWO_POINT_CALIBRATION == 0
  uint16_t V_saturation = (uint32_t)CAL1_V + (uint32_t)35 * temperature_c - (uint32_t)CAL1_T * 35;
  return (voltage_mv * DO_Table[temperature_c] / V_saturation);
#else
  uint16_t V_saturation = (int16_t)((int8_t)temperature_c - CAL2_T) * ((uint16_t)CAL1_V - CAL2_V) / ((uint8_t)CAL1_T - CAL2_T) + CAL2_V;
  return (voltage_mv * DO_Table[temperature_c] / V_saturation);
#endif
}

void handleOksigenSensor()
{
  float voltage_v = oksigenSensor.getVar(Analog::VOLTAGE);

  uint16_t voltage_mv = (uint16_t)(voltage_v * 1000.0);

  uint8_t currentTemperature = (uint8_t)round(suhuValue);
  // Serial.println(analogRead(PIN_OKSIGEN));
  int tes = analogRead(PIN_OKSIGEN);
  static int anjay = 0;
  if (tes > 0)
  {
    // Serial.println(tes);
  }

  // 4. Panggil fungsi readDO() untuk menghitung nilai Dissolved Oxygen (mg/L).
  float o2Value = readDO(voltage_mv, currentTemperature);

  oksigenSensor.setFinal(o2Value);
}

// helper: set relay state and immediately update pin (uses fastWrite)
void setRelay(int idx, bool state) {
  if (idx < 0 || idx > 4) return; // Ubah dari 3 ke 4 untuk indeks maksimum
  relayState[idx] = state;
  int pin;
  // Tentukan pin berdasarkan indeks
  switch(idx) {
    case 0: pin = PIN_RELAY_1; break;
    case 1: pin = PIN_RELAY_2; break;
    case 2: pin = PIN_RELAY_3; break;
    case 3: pin = PIN_RELAY_4; break;
    case 4: pin = PIN_RELAY_5; break;
    default: return;
  }
  fastWrite(pin, relayState[idx] ? LOW : HIGH);
}

void handleButton() {
  if (!server.hasArg("relay") || !server.hasArg("state")) {
    server.send(400, "application/json", "{\"error\":\"Missing relay or state parameter\"}");
    return;
  }
  // Jika mode otomatis aktif, tolak perubahan manual
  if (autoMode) {
    server.send(403, "application/json", "{\"error\":\"Device in automatic mode\"}");
    return;
  }

  String relay = server.arg("relay");
  String state = server.arg("state"); // expected "on" or "off"
  int idx = -1;
  if (relay == "relay1") idx = 0;
  else if (relay == "relay2") idx = 1;
  else if (relay == "relay3") idx = 2;
  else if (relay == "relay4") idx = 3;
  else if (relay == "relay5") idx = 4; // Tambahkan ini
  
  if (idx == -1) {
    server.send(400, "application/json", "{\"error\":\"Invalid relay\"}");
    return;
  }

  // server is authoritative: apply requested state
  if (state == "on") setRelay(idx, true);
  else if (state == "off") setRelay(idx, false);
  else {
    server.send(400, "application/json", "{\"error\":\"Invalid state\"}");
    return;
  }

  // Kirim status semua relay sebagai JSON (tambahkan mode + koneksi nanti via handleRelayStatus)
  String json = "{";
  for (int i = 0; i < 5; i++) { // Ubah dari 4 ke 5
    json += "\"relay" + String(i+1) + "\":" + (relayState[i] ? "true" : "false");
    if (i < 4) json += ","; // Ubah dari 3 ke 4
  }
  json += "}";
  server.send(200, "application/json", json);
}


// contoh otomatis: ubah relay berdasarkan kondisi sensor / jadwal
void autoRelayLogic() {
  static unsigned long lastAutoMillis = 0;
  const unsigned long interval = 10; // cek tiap 10 detik
  if (millis() - lastAutoMillis < interval) return;
  lastAutoMillis = millis();

  bool stateChanged = false;
  bool oldStates[4];
  // Simpan status relay sebelumnya
  for(int i = 0; i < 4; i++) {
    oldStates[i] = relayState[i];
  }

  // Contoh 1: jika suhu > 30C -> nyalakan relay1, else matikan
  if (suhuValue < 20.0) setRelay(4, true);
  else setRelay(0, false);

  // Contoh 2: jika potensiometer (percent) > 50 -> nyalakan relay2
  float potPercent = potensiometer.getVar(Analog::PERCENT);
  if (potPercent > 50.0) setRelay(1, true);
  else setRelay(1, false);

  // Cek apakah ada perubahan status
  for(int i = 0; i < 4; i++) {
    if(oldStates[i] != relayState[i]) {
      stateChanged = true;
      break;
    }
  }

  // Jika ada perubahan, kirim update ke semua client
  if(stateChanged) {
    String json = "{";
    for(int i = 0; i < 4; i++) {
      json += "\"relay" + String(i+1) + "\":" + (relayState[i] ? "true" : "false");
      if(i < 3) json += ",";
    }
    json += "}";
    webSocket.broadcastTXT(json);
  }

  // Tambahkan logika lain sesuai kebutuhan...
}

void modeSTA()
{
  Serial.println("Inisialisasi STA...");
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Inisialisasi");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
    lcd.setCursor(0, 1);
    lcd.print("Menghubungkan..");
  }
  Serial.println("\nWiFi connected");
  Serial.println(WiFi.localIP());
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("WiFi Terhubung");
}

void modeAP()
{
  Serial.println("Mengatur mode Access Point...");
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Mode AP");
  delay(500);
  WiFi.mode(WIFI_AP);
  bool result = WiFi.softAP(ap_ssid, ap_password);
  if (!result)
  {
    Serial.println("Gagal mengatur AP!");
    lcd.setCursor(0, 1);
    lcd.print("Gagal AP");
    delay(2000);
    return;
  }
  IPAddress IP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(IP);
  lcd.setCursor(0, 1);
  lcd.print("AP IP:");
  lcd.setCursor(0, 1);
  lcd.print(IP);
  delay(2000);
}

void hostingStart()
{
  if (MODE_STA_OR_AP == "sta")
  {
    modeSTA();
  }
  else
  {
    modeAP();
  }
}

void handleModeGet();
void handleModePost();

void setup()
{
  // ===== User Initialization =====
  Serial.println(analogRead(PIN_PH));
  Serial.begin(115200);
  lcd.init();
  lcd.backlight();

  hostingStart();

  if (!SPIFFS.begin(true))
  {
    Serial.println("SPIFFS Mount Failed");
    return;
  }

  if (MDNS.begin("esp32"))
    Serial.println("mDNS: http://esp32.local");
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("mDNS:");
  lcd.setCursor(0, 1);
  lcd.print("esp32.local");
  delay(2000);

  server.on("/sb-admin-2.css", handleSbAdmin2Css);
  server.on("/chart.js", HTTP_GET, handleChartJs);
  server.on("/luxon.js", HTTP_GET, handleLuxonJs);
  server.on("/chartjs-adapter-luxon.js", HTTP_GET, handleAdapterLuxonJs);
  server.on("/chartjs-plugin-streaming.js", HTTP_GET, handleStreamingPluginJs);
  server.on("/script.js", handleScriptJs);
  server.on("/button", HTTP_POST, handleButton);
  server.on("/relay-status", HTTP_GET, handleRelayStatus);
  server.on("/mode", HTTP_GET, handleModeGet);
  server.on("/mode", HTTP_POST, handleModePost);
  server.on("/", handleRoot);
  server.on("/data", handleData); // historis (opsional)
  server.on("/last", handleLast); // realtime
  server.begin();

  webSocket.begin();
  webSocket.onEvent(webSocketEvent);

  Wire.begin();
  sensorSuhu.begin();

  phSensor.update();
  turbiditySensor.update();
  oksigenSensor.update();
  potensiometer.update();

  pinMode(PIN_RELAY_1, OUTPUT);
  pinMode(PIN_RELAY_2, OUTPUT);
  pinMode(PIN_RELAY_3, OUTPUT);
  pinMode(PIN_RELAY_4, OUTPUT);
  pinMode(PIN_RELAY_5, OUTPUT); // Tambahkan ini

  fastWrite(PIN_RELAY_1, HIGH);
  fastWrite(PIN_RELAY_2, HIGH);
  fastWrite(PIN_RELAY_3, HIGH);
  fastWrite(PIN_RELAY_4, HIGH);
  fastWrite(PIN_RELAY_5, HIGH); // Tambahkan ini

  sensorSuhu.requestTemperatures();
  suhuValue = sensorSuhu.getTempCByIndex(0);
}

void loop()
{
  // Serial.println(phSensor.getVar(Analog::VOLTAGE));
  static bool tempReady = false;
  static bool firstRun = false;
  if (!firstRun)
  {
    lcd.clear();
    firstRun = true;
  }

  fastWrite(PIN_RELAY_1, relayState[0] ? LOW : HIGH);
  fastWrite(PIN_RELAY_2, relayState[1] ? LOW : HIGH);
  fastWrite(PIN_RELAY_3, relayState[2] ? LOW : HIGH);
  fastWrite(PIN_RELAY_4, relayState[3] ? LOW : HIGH);
  fastWrite(PIN_RELAY_5, relayState[4] ? LOW : HIGH); // Tambahkan ini

  // printf(relayState[4] ? "0" : "1");
  // printf("\n");
  
  // Panggil auto logic hanya saat autoMode = true
  if (autoMode) {
    autoRelayLogic();
  }

  server.handleClient();
  webSocket.loop();
  phSensor.update();
  turbiditySensor.update();
  oksigenSensor.update();
  potensiometer.update();

  handlePhSensor();
  handleTurbiditySensor();
  handleOksigenSensor();

  if (millis() - lastTempRequest >= tempRequestInterval)
  {
    lastTempRequest = millis();
    sensorSuhu.requestTemperatures(); // Langkah 1: Minta sensor mulai mengukur (tidak menunggu)
    // Serial.println("Meminta pembacaan suhu baru...");
    tempReady = true; // Tandai bahwa kita sudah boleh mengambil data nanti
  }

  if (millis() - lastUpdate_sensor >= 50)
  {
    lastUpdate_sensor = millis();

    if (tempReady)
    {
      tempReady = false;
      suhuValue = sensorSuhu.getTempCByIndex(0);

      if (suhuValue == DEVICE_DISCONNECTED_C)
      {
        suhuValue = 0;
        // Serial.println("Sensor suhu tidak terhubung!");
      }
      else
      {
        // Print nilai suhu (sekarang tidak akan memblokir lagi)
        // Serial.printf("Suhu: %.2f °C\n", suhuValue);
      }
    }

    sensorData.addData(
        phSensor.getVar(Analog::FINAL),
        turbiditySensor.getVar(Analog::FINAL),
        oksigenSensor.getVar(Analog::FINAL),
        suhuValue);
  }
  timerLcdI2c();
}

void timerLcdI2c()
{
  DataNode *lastNode = sensorData.getLastNode();
  static unsigned long lastUpdateLcd = 0;

  if (millis() - lastUpdateLcd >= 500)
  {
    lastUpdateLcd = millis();

    // Siapkan buffer untuk menampung teks per baris (16 karakter + 1 null terminator)
    char line1[17];
    char line2[17];

    float ph_val = phSensor.getVar(Analog::FINAL);
    float turb_val = turbiditySensor.getVar(Analog::FINAL);
    float oks_val = oksigenSensor.getVar(Analog::FINAL);
    int pot_val = (int)potensiometer.getVar(Analog::PERCENT);

    // %-5.2f artinya: format float, lebar 5 karakter, 2 angka di belakang koma, rata kiri
    sprintf(line1, "pH:%-5.2f C:%-5.2f", ph_val, suhuValue);

    // %-4.0f artinya: format float, lebar 4 karakter, 0 angka di belakang koma, rata kiri
    // %-3d%% artinya: format integer, lebar 3 karakter, rata kiri, diakhiri tanda %
    sprintf(line2, "Tb:%-4.0f  P:%-3d%%", turb_val, pot_val);

    // Cetak ke LCD
    lcd.setCursor(0, 0);
    lcd.print(line1);

    lcd.setCursor(0, 1);
    lcd.print(line2);
  }
}

// ===== Fungsi Penanganan HTTP =====

// Fungsi untuk mengirim data sampel terakhir
void handleLast()
{
  DataNode *lastNode = sensorData.getLastNode();

  if (!lastNode)
  {
    server.send(200, "application/json", "{\"ph\":null,\"turb\":null,\"oks\":null,\"suhu\":null}");
    return;
  }

  String json = "{";
  json += "\"ph\":" + String(lastNode->ph, 2) + ",";
  json += "\"turb\":" + String(lastNode->turbidity, 2) + ",";
  json += "\"oks\":" + String(lastNode->oksigen, 2) + ",";
  json += "\"suhu\":" + String(lastNode->suhu, 2);
  json += "}";
  server.send(200, "application/json", json);
}

// Fungsi untuk mengirim data historis sebagai array
void handleData()
{
  String json = "{";
  json += "\"ph\":" + sensorData.getChartData("ph") + ",";
  json += "\"turb\":" + sensorData.getChartData("turb") + ",";
  json += "\"oks\":" + sensorData.getChartData("oks") + ",";
  json += "\"suhu\":" + sensorData.getChartData("suhu");
  json += "}";
  server.send(200, "application/json", json);
}

void handleSbAdmin2Css()
{
  File file = SPIFFS.open("/sb-admin-2.css", "r");
  if (!file)
  {
    server.send(404, "text/plain", "CSS not found");
    return;
  }
  server.streamFile(file, "text/css");
  file.close();
}

// Tambahkan fungsi baru ini di main.cpp
void handleScriptJs()
{
  File file = SPIFFS.open("/script.js", "r");
  if (!file)
  {
    server.send(404, "text/plain", "JS not found");
    return;
  }
  // Penting: Gunakan tipe konten "application/javascript"
  server.streamFile(file, "application/javascript");
  file.close();
}

// Handler untuk file chart.js
void handleChartJs()
{
  File file = SPIFFS.open("/chart.js", "r");
  if (!file)
  {
    server.send(404, "text/plain", "chart.js not found");
    return;
  }
  server.streamFile(file, "application/javascript");
  file.close();
}

// Handler untuk file luxon.js
void handleLuxonJs()
{
  File file = SPIFFS.open("/luxon.js", "r");
  if (!file)
  {
    server.send(404, "text/plain", "luxon.js not found");
    return;
  }
  server.streamFile(file, "application/javascript");
  file.close();
}

// Handler untuk file chartjs-adapter-luxon.js
void handleAdapterLuxonJs()
{
  File file = SPIFFS.open("/chartjs-adapter-luxon.js", "r");
  if (!file)
  {
    server.send(404, "text/plain", "adapter-luxon.js not found");
    return;
  }
  server.streamFile(file, "application/javascript");
  file.close();
}

// Handler untuk file chartjs-plugin-streaming.js
void handleStreamingPluginJs()
{
  File file = SPIFFS.open("/chartjs-plugin-streaming.js", "r");
  if (!file)
  {
    server.send(404, "text/plain", "streaming-plugin.js not found");
    return;
  }
  server.streamFile(file, "application/javascript");
  file.close();
}


/// Handler untuk memberikan status semua relay dalam JSON (tambahkan mode + koneksi)
void handleRelayStatus() {
  String json = "{";
  for (int i = 0; i < 4; ++i) {
    json += "\"relay" + String(i + 1) + "\":" + (relayState[i] ? "true" : "false");
    if (i < 3) json += ",";
  }
  json += ",\"mode\":\"";
  json += (autoMode ? "auto" : "manual");
  json += "\"}";
  server.send(200, "application/json", json);
}

// Handler untuk mendapatkan mode
void handleModeGet() {
  String json = "{";
  json += "\"mode\":\"";
  json += (autoMode ? "auto" : "manual");
  json += "\"}";
  server.send(200, "application/json", json);
}

// Handler untuk mengubah mode (POST ?mode=auto|manual)
void handleModePost() {
  if (!server.hasArg("mode")) {
    server.send(400, "application/json", "{\"error\":\"Missing mode parameter\"}");
    return;
  }
  String mode = server.arg("mode");
  if (mode == "auto") autoMode = true;
  else if (mode == "manual") autoMode = false;
  else {
    server.send(400, "application/json", "{\"error\":\"Invalid mode\"}");
    return;
  }
  // kembalikan state saat ini
  String json = "{";
  json += "\"mode\":\"";
  json += (autoMode ? "auto" : "manual");
  json += "\"}";
  server.send(200, "application/json", json);
}

void handleRoot()
{
  // Baca file /index.html dari SPIFFS dan stream ke client
  File file = SPIFFS.open("/index.html", "r");
  if (!file)
  {
    server.send(500, "text/plain", "index.html not found");
    return;
  }
  server.streamFile(file, "text/html; charset=utf-8");
  file.close();
}

// Tambahkan setelah deklarasi WebServer
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
    switch(type) {
        case WStype_CONNECTED:
            {
                Serial.printf("[%u] Connected!\n", num);
                // Kirim status awal ke client yang baru terkoneksi
                String status = "{";
                for(int i = 0; i < 5; i++) { // Ubah dari 4 ke 5
                    status += "\"relay" + String(i+1) + "\":" + (relayState[i] ? "true" : "false");
                    if(i < 4) status += ","; // Ubah dari 3 ke 4
                }
                status += ",\"mode\":\"" + String(autoMode ? "auto" : "manual") + "\"}";
                webSocket.sendTXT(num, status);
            }
            break;
        case WStype_TEXT:
            {
                String text = String((char*)payload);
                if(text.startsWith("relay")) {
                    int relay = text[5] - '1'; // relay1 -> 0, relay2 -> 1, etc.
                    bool state = text.endsWith("on");
                    if(relay >= 0 && relay < 5 && !autoMode) { // Ubah dari 4 ke 5
                        setRelay(relay, state);
                        String status = "{\"relay" + String(relay+1) + "\":" + (state ? "true" : "false") + "}";
                        webSocket.broadcastTXT(status);
                    }
                }
                else if(text == "mode_auto") {
                    autoMode = true;
                    webSocket.broadcastTXT("{\"mode\":\"auto\"}");
                }
                else if(text == "mode_manual") {
                    autoMode = false;
                    webSocket.broadcastTXT("{\"mode\":\"manual\"}");
                }
            }
            break;
    }
}
