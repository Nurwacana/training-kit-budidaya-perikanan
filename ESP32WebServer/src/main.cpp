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

#define PIN_PH 32
#define PIN_TURBIDITY 33
#define PIN_OKSIGEN 34
#define PIN_POTENSIO 35
#define PIN_SUHU 23
#define PIN_RELAY_1 14
#define PIN_RELAY_2 27
#define PIN_RELAY_3 26
#define PIN_RELAY_4 25

const char *ssid = "Pertanian IPB utama";
const char *password = "pertanian dan pangan";

WebServer server(80);

const int maxDataPoints = 30;

class Analog
{
public:
  /// @brief inisialisasi pin analog dengan filter alpha
  /// @param p pin analog
  /// @param a variabel alpha (0 < a < 1), semakin kecil semakin halus
  Analog(int p, float a) : pin(p), alpha(a), voltage(0.0), final(0.0)
  {
    pinMode(pin, INPUT);
  }
  /// @brief panggil di loop utama
  void update()
  {
    uint16_t raw = analogRead(pin);
    // Hitung nilai rata-rata bergerak eksponensial
    voltage = (alpha * ((float)raw / 4095.0f) * 3.3f) + ((1 - alpha) * voltage);
    percent = ((float)raw / 4095.0f) * 100.0f;
  }
  /// @brief mendapatkan pin analog
  uint8_t getPin() { return pin; }

  /// @brief mengambil nilai variabel (alpha, voltage, final)
  float getVar(String var)
  {
    for (unsigned int i = 0; i < var.length(); i++)
    {
      char c = var[i];
      if (c >= 'A' && c <= 'Z')
      {
        var[i] = c + ('a' - 'A');
      }
    }
    if (var == "alpha")
      return alpha;
    else if (var == "voltage")
      return voltage;
    else if (var == "final")
      return final;
    else
      return 0.0;
  }

  float getPercent()
  {
    return (voltage / 3.3f) * 100.0f;
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
};

Analog phSensor = Analog(PIN_PH, 0.1f);
Analog turbiditySensor = Analog(PIN_TURBIDITY, 0.1f);
Analog oksigenSensor = Analog(PIN_OKSIGEN, 0.1f);
Analog potensiometer = Analog(PIN_POTENSIO, 0.1f);

unsigned long lastTempRequest = 0;
const long tempRequestInterval = 750; // Minta suhu setiap 1 detik
bool tempReady = false;

OneWire oneWire(PIN_SUHU);

DallasTemperature sensorSuhu(&oneWire);

unsigned long lastUpdate_sensor = 0;

/*
  Inisialisasi objek LCD
  Parameter:
  - 0x27: Alamat I2C. Ini adalah alamat yang paling umum.
          Jika tidak berhasil, alamat Anda mungkin 0x3F. (Lihat bagian troubleshooting di bawah)
  - 16: Jumlah kolom karakter pada LCD
  - 2: Jumlah baris pada LCD
*/
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Struktur node sekarang menjadi bagian privat dari kelas
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

  // Metode untuk menambahkan data ke linked list
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

DataList sensorData(maxDataPoints);

// ===== Web =====
void handleRoot();
void handleData();
void handleLast();
// ===== LCD I2C =====
void timerLcdI2c();

// void handleSensorTurbidity() {
//   calculateExponentialMovingAverage(sensor.turbidity);
//   sensor.turbidity.final = -1120.4 * sq(sensor.turbidity.voltage) + 5742.3 * sensor.turbidity.voltage - 4353.8;
// }

void setup()
{
  Serial.begin(115200);
  Serial.println("Inisialisasi WiFi...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");
  Serial.println(WiFi.localIP());

  if (MDNS.begin("esp32"))
    Serial.println("mDNS: http://esp32.local");


  server.on("/", handleRoot);
  server.on("/data", handleData); // historis (opsional)
  server.on("/last", handleLast); // realtime
  server.begin();

  // Mulai komunikasi I2C. Untuk ESP32, pin defaultnya sudah D21 (SDA) dan D22 (SCL).
  Wire.begin();
  // Inisialisasi LCD
  lcd.init();
  // Nyalakan lampu latar (backlight) LCD
  lcd.backlight();
  // Kosongkan layar untuk memastikan tidak ada sisa teks
  lcd.clear();
  // Atur kursor ke kolom 0, baris 0 (baris pertama)
  lcd.setCursor(0, 0);
  lcd.print("Halo, Dunia!");
  // Atur kursor ke kolom 0, baris 1 (baris kedua)
  lcd.setCursor(0, 1);
  lcd.print("ESP32 & LCD I2C");

  sensorSuhu.begin();

  phSensor.update();
  turbiditySensor.update();
  oksigenSensor.update();
  potensiometer.update();

  pinMode(PIN_RELAY_1, OUTPUT);
  pinMode(PIN_RELAY_2, OUTPUT);
  pinMode(PIN_RELAY_3, OUTPUT);
  pinMode(PIN_RELAY_4, OUTPUT);
}

void loop()
{
  server.handleClient();
  phSensor.update();
  turbiditySensor.update();
  oksigenSensor.update();
  potensiometer.update();

  // Serial.println((uint8_t)potensiometer.getPercent());

  if (millis() - lastTempRequest >= tempRequestInterval)
  {
    lastTempRequest = millis();
    sensorSuhu.requestTemperatures(); // Langkah 1: Minta sensor mulai mengukur (tidak menunggu)
    Serial.println("Meminta pembacaan suhu baru...");
    tempReady = true; // Tandai bahwa kita sudah boleh mengambil data nanti
  }

  if (millis() - lastUpdate_sensor >= 50)
  {
    lastUpdate_sensor = millis();
    static float suhuValue = 0;

    if (tempReady)
    {
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
        phSensor.getVar("final"),
        turbiditySensor.getVar("final"),
        oksigenSensor.getVar("final"),
        suhuValue);
  }
}

void timerLcdI2c()
{
  DataNode *lastNode = sensorData.getLastNode();
  static unsigned long lastUpdateLcd = 0;
  if (millis() - lastUpdateLcd >= 500)
  {
    lastUpdateLcd = millis();
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("pH: " + String(lastNode ? lastNode->ph : 0.0, 2));
    lcd.setCursor(0, 1);
    lcd.print("Suhu: " + String(lastNode ? lastNode->suhu : 0.0, 2) + " C");
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

void handleRoot()
{
  String htmlString = R"rawliteral(
<!DOCTYPE html>
<html lang="id">
<head>
<meta charset="UTF-8" />
<meta name="viewport" content="width=device-width, initial-scale=1.0" />
<title>IoT Perairan Dashboard</title>

<!-- Chart.js + Streaming plugin -->
<script src="https://cdn.jsdelivr.net/npm/chart.js@4"></script>
<script src="https://cdn.jsdelivr.net/npm/luxon@3"></script>
<script src="https://cdn.jsdelivr.net/npm/chartjs-adapter-luxon@1"></script>
<script src="https://cdn.jsdelivr.net/npm/chartjs-plugin-streaming@2"></script>

<style>
  body { font-family: Arial, sans-serif; background:#f5f5f5; margin:0; padding:20px; }
  h2 { text-align:center; margin-bottom:20px; }
  .grid { display:grid; grid-template-columns:repeat(auto-fit,minmax(350px,1fr)); gap:20px; }
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
  <h2>📊 IoT Perairan Dashboard</h2>
  <div class="grid">
    <div class="card">
      <h3>pH</h3>
      <div class="wrap"><canvas id="chartPH"></canvas></div>
      <table id="tablePH">
        <thead><tr><th>Timestamp</th><th>pH</th></tr></thead>
        <tbody></tbody>
      </table>
    </div>
    <div class="card">
      <h3>Kekeruhan (NTU)</h3>
      <div class="wrap"><canvas id="chartTurbidity"></canvas></div>
      <table id="tableTurbidity">
        <thead><tr><th>Timestamp</th><th>NTU</th></tr></thead>
        <tbody></tbody>
      </table>
    </div>
    <div class="card">
      <h3>Kadar Oksigen (mg/L)</h3>
      <div class="wrap"><canvas id="chartOksigen"></canvas></div>
      <table id="tableOksigen">
        <thead><tr><th>Timestamp</th><th>mg/L</th></tr></thead>
        <tbody></tbody>
      </table>
    </div>
    <div class="card">
      <h3>Suhu (°C)</h3>
      <div class="wrap"><canvas id="chartSuhu"></canvas></div>
      <table id="tableSuhu">
        <thead><tr><th>Timestamp</th><th>°C</th></tr></thead>
        <tbody></tbody>
      </table>
    </div>
  </div>

<script>
let _last = null, _fetching = false;
async function fetchLast(){
  if (_fetching) return _last;
  _fetching = true;
  try {
    const r = await fetch('/last', { cache:'no-store' });
    _last = await r.json();
    return _last;
  } catch(e){ console.error(e); return _last; }
  finally { _fetching = false; }
}

function makeRealtimeChart(canvasId, key, color, yMin, yMax){
  const ctx = document.getElementById(canvasId).getContext('2d');
  return new Chart(ctx, {
    type: 'line',
    data: { datasets: [{
      label: key, data: [],
      borderColor: color, borderWidth: 2,
      pointRadius: 0, cubicInterpolationMode: 'monotone', fill: false
    }]},
    options: {
      responsive: true, maintainAspectRatio: false,
      animation: false,
      plugins: { legend: { display: false } },
      scales: {
        x: {
          type: 'realtime',
          realtime: {
            duration: 60000,
            refresh: 100,  // Chart refresh cepat
            delay: 100,
            frameRate: 30,
            onRefresh: async (chart) => {
              const j = await fetchLast();
              if (!j || typeof j[key] !== 'number') return;
              chart.data.datasets[0].data.push({ x: Date.now(), y: j[key] });
            }
          }
        },
        y: { min: yMin, max: yMax }
      }
    }
  });
}

function updateTable(tableId, key){
  const tableBody = document.querySelector(`#${tableId} tbody`);
  const nowStr = new Date().toLocaleTimeString();
  if (!_last || typeof _last[key] !== 'number') return;
  tableBody.insertAdjacentHTML('afterbegin', `<tr><td>${nowStr}</td><td>${_last[key].toFixed(2)}</td></tr>`);
  if (tableBody.children.length > 20) {
    tableBody.removeChild(tableBody.lastElementChild);
  }
}

// === Inisialisasi Chart ===
const chartPH   = makeRealtimeChart('chartPH', 'ph',   '#4CAF50', 0, 14);
const chartTurb = makeRealtimeChart('chartTurbidity', 'turb', '#2196F3', 0, 1000);
const chartOks  = makeRealtimeChart('chartOksigen', 'oks', '#FF9800', 0, 20);
const chartSuhu = makeRealtimeChart('chartSuhu', 'suhu', '#E91E63', 0, 100);

// === Jadwal Update Tabel setiap 1 detik ===
setInterval(() => {
  updateTable('tablePH', 'ph');
  updateTable('tableTurbidity', 'turb');
  updateTable('tableOksigen', 'oks');
  updateTable('tableSuhu', 'suhu');
}, 1000);
</script>

</body>
</html>
  )rawliteral";

  server.send(200, "text/html; charset=utf-8", htmlString);
}