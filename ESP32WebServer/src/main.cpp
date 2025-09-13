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

// ===== User defined constants =====
// sudah terdefinisi di header esp32-hal-gpio.h
// #define GPIO_OUT_W1TS_REG 0x3FF44008
// #define GPIO_OUT_W1TC_REG 0x3FF4400C

#define TWO_POINT_CALIBRATION 0
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

// Lookup table for Dissolved Oxygen (DO) in mg/L at different temperatures (0-40°C)
const uint16_t DO_Table[41] = {
    14460, 14220, 13820, 13440, 13090, 12740, 12420, 12110, 11810, 11530,
    11260, 11010, 10770, 10530, 10300, 10080, 9860, 9660, 9460, 9270,
    9080, 8900, 8730, 8570, 8410, 8250, 8110, 7960, 7820, 7690,
    7560, 7430, 7300, 7180, 7070, 6950, 6840, 6730, 6630, 6530, 6410};

const long tempRequestInterval = 750; // Minta suhu setiap 750 ms

const char *ssid = "Pertanian IPB utama";
const char *password = "pertanian dan pangan";

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

DataList sensorData(maxDataPoints);

bool relayState[4] = {false, false, false, false};

// ===== User defined functions =====
// ===== Web =====
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
  Serial.println(voltage);

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

void handleButton()
{
  if (!server.hasArg("relay"))
  {
    server.send(400, "application/json", "{\"error\":\"Missing relay parameter\"}");
    return;
  }
  String relay = server.arg("relay");
  int idx = -1;
  if (relay == "relay1")
    idx = 0;
  else if (relay == "relay2")
    idx = 1;
  else if (relay == "relay3")
    idx = 2;
  else if (relay == "relay4")
    idx = 3;

  if (idx == -1)
  {
    server.send(400, "application/json", "{\"error\":\"Invalid relay\"}");
    return;
  }

  relayState[idx] = !relayState[idx]; // toggle state

  int pin = PIN_RELAY_1 + idx; // PIN_RELAY_1,2,3,4 harus urut
  // digitalWrite(pin, relayState[idx] ? HIGH : LOW);
  // fastWrite(pin, relayState[idx] ? LOW : HIGH);

  // Kirim status semua relay dalam JSON
  String json = "{";
  for (int i = 0; i < 4; i++)
  {
    json += "\"relay" + String(i + 1) + "\":" + (relayState[i] ? "true" : "false");
    if (i < 3)
      json += ",";
  }
  json += "}";
  server.send(200, "application/json", json);
}

void handleRelayStatus()
{
  String json = "{";
  for (int i = 0; i < 4; i++)
  {
    json += "\"relay" + String(i + 1) + "\":" + (relayState[i] ? "true" : "false");
    if (i < 3)
      json += ",";
  }
  json += "}";
  server.send(200, "application/json", json);
}

void modeWifi()
{
  Serial.println("Inisialisasi WiFi...");
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

void setup()
{
  Serial.println(analogRead(PIN_PH));
  Serial.begin(115200);
  lcd.init();
  lcd.backlight();

  // modeWifi();
  modeAP();

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
  server.on("/", handleRoot);
  server.on("/data", handleData); // historis (opsional)
  server.on("/last", handleLast); // realtime
  server.begin();

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

  fastWrite(PIN_RELAY_1, HIGH);
  fastWrite(PIN_RELAY_2, HIGH);
  fastWrite(PIN_RELAY_3, HIGH);
  fastWrite(PIN_RELAY_4, HIGH);

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

  server.handleClient();
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

void handleRoot()
{
  String htmlString = R"rawliteral(
<!DOCTYPE html>
<html lang="id">
<head>
<meta charset="UTF-8" />
<meta name="viewport" content="width=device-width, initial-scale=1, shrink-to-fit=no">
<title>Dashboard Perairan IoT</title>
<link rel="stylesheet" href="/sb-admin-2.css">

<!-- Chart.js + Streaming plugin -->
<script src="/chart.js"></script>
<script src="/luxon.js"></script>
<script src="/chartjs-adapter-luxon.js"></script>
<script src="/chartjs-plugin-streaming.js"></script>

<style>
  body { font-family: Arial, sans-serif; background:#f5f5f5; margin:0; padding:20px; }
  h2 { text-align:center; margin-bottom:20px; }
  .grid { display:grid; grid-template-columns:repeat(auto-fit,minmax(300px,1fr)); gap:15px; }
  .card { background:#fff; border-radius:12px; padding:15px; box-shadow:0 4px 12px rgba(0,0,0,.1); display:flex; flex-direction:column; }
  .card h3 { margin:0 0 10px; font-size:1.05em; color:#333; }
  .wrap { position:relative; width:100%; height:220px; }
  canvas { position:absolute; inset:0; }
  table { width:100%; border-collapse:collapse; font-size:0.85em; margin-top:10px; }
  table th, table td { border:1px solid #ddd; padding:4px 6px; text-align:center; }
  table th { background:#f0f0f0; }
  tbody { display:block; max-height:160px; overflow-y:auto; } /* Scroll tabel */
  thead, tbody tr { display:table; width:100%; table-layout:fixed; }
</style>
</head>
<body>
    <div class="container">
        <div class="page-header">
            <h2>Dashboard Perairan IoT</h2>
        </div>
        <div class="main-content">
            <hr>
            <div class="card">
                <div class="card-body" style="display: flex; justify-content: space-around; flex-wrap: wrap; gap: 20px;">
                    <div style="text-align: center;">
                        <button class="btn-primary relay-btn" onclick="sendButton('relay1')">Relay 1</button>
                        <div id="status-relay1" class="relay-status">OFF</div>
                    </div>
                    <div style="text-align: center;">
                        <button class="btn-primary relay-btn" onclick="sendButton('relay2')">Relay 2</button>
                        <div id="status-relay2" class="relay-status">OFF</div>
                    </div>
                    <div style="text-align: center;">
                        <button class="btn-primary relay-btn" onclick="sendButton('relay3')">Relay 3</button>
                        <div id="status-relay3" class="relay-status">OFF</div>
                    </div>
                    <div style="text-align: center;">
                        <button class="btn-primary relay-btn" onclick="sendButton('relay4')">Relay 4</button>
                        <div id="status-relay4" class="relay-status">OFF</div>
                    </div>
                </div>
            </div>
            <div class="grid-container" style="margin-top: 20px;">
                <div class="card">
                    <div class="card-body">
                        <h3>pH</h3>
                        <div class="wrap"><canvas id="chartPH"></canvas></div>
                        <table id="tablePH" class="table">
                            <thead><tr><th>Timestamp</th><th>pH</th></tr></thead>
                            <tbody></tbody>
                        </table>
                    </div>
                </div>
                <div class="card">
                    <div class="card-body">
                        <h3>Kekeruhan (NTU)</h3>
                        <div class="wrap"><canvas id="chartTurbidity"></canvas></div>
                        <table id="tableTurbidity" class="table">
                            <thead><tr><th>Timestamp</th><th>NTU</th></tr></thead>
                            <tbody></tbody>
                        </table>
                    </div>
                </div>
                <div class="card">
                    <div class="card-body">
                        <h3>Kadar Oksigen (mg/L)</h3>
                        <div class="wrap"><canvas id="chartOksigen"></canvas></div>
                        <table id="tableOksigen" class="table">
                            <thead><tr><th>Timestamp</th><th>mg/L</th></tr></thead>
                            <tbody></tbody>
                        </table>
                    </div>
                </div>
                <div class="card">
                    <div class="card-body">
                        <h3>Suhu (°C)</h3>
                        <div class="wrap"><canvas id="chartSuhu"></canvas></div>
                        <table id="tableSuhu" class="table">
                            <thead><tr><th>Timestamp</th><th>°C</th></tr></thead>
                            <tbody></tbody>
                        </table>
                    </div>
                </div>
            </div>
        </div>
    </div> 
    <script src="/script.js"></script> 
</body>
</html>
  )rawliteral";

  server.send(200, "text/html; charset=utf-8", htmlString);
}