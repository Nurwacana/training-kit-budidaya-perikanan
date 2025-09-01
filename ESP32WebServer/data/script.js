// === Variabel Global untuk Fetching ===
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

// === Fungsi Pembuat Chart Real-time ===
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
            refresh: 1000,
            delay: 2000,
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

// === Fungsi Update Tabel ===
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
const chartTurb = makeRealtimeChart('chartTurbidity', 'turb', '#2196F3', 0, 3000);
const chartOks  = makeRealtimeChart('chartOksigen', 'oks', '#FF9800', 0, 20);
const chartSuhu = makeRealtimeChart('chartSuhu', 'suhu', '#E91E63', 0, 50);

// === Memuat Data Historis Saat Halaman Dibuka ===
async function loadInitialData() {
  try {
    console.log("Memuat data historis...");
    const response = await fetch('/data');
    const historicalData = await response.json();
    const now = Date.now();

    const items = {
      ph:   { chart: chartPH,   tableId: 'tablePH' },
      turb: { chart: chartTurb, tableId: 'tableTurbidity' },
      oks:  { chart: chartOks,  tableId: 'tableOksigen' },
      suhu: { chart: chartSuhu, tableId: 'tableSuhu' }
    };

    for (const key in historicalData) {
      if (items[key] && Array.isArray(historicalData[key])) {
        const { chart, tableId } = items[key];
        const dataset = chart.data.datasets[0].data;
        const tableBody = document.querySelector(`#${tableId} tbody`);
        tableBody.innerHTML = '';
        dataset.length = 0;

        historicalData[key].forEach((value, index) => {
          const timestamp = now - (historicalData[key].length - 1 - index) * 2000;
          dataset.push({ x: timestamp, y: value });
          const timeString = new Date(timestamp).toLocaleTimeString();
          tableBody.insertAdjacentHTML('afterbegin', `<tr><td>${timeString}</td><td>${value.toFixed(2)}</td></tr>`);
        });
        
        chart.update('quiet');
      }
    }
    console.log("Data historis berhasil dimuat.");
  } catch (error) {
    console.error("Gagal memuat data historis:", error);
  }
}

document.addEventListener('DOMContentLoaded', loadInitialData);

// === Jadwal Update Tabel setiap 1 detik ===
setInterval(() => {
  updateTable('tablePH', 'ph');
  updateTable('tableTurbidity', 'turb');
  updateTable('tableOksigen', 'oks');
  updateTable('tableSuhu', 'suhu');
}, 1000);

// === Relay Button Logic ===
function updateRelayStatusUI(status) {
  for (let i = 1; i <= 4; i++) {
    const el = document.getElementById('status-relay' + i);
    if (el) el.textContent = status['relay' + i] ? 'ON' : 'OFF';
  }
}

function sendButton(relay) {
  fetch('/button?relay=' + relay, {method: 'POST'})
    .then(r => r.json())
    .then(updateRelayStatusUI)
    .catch(console.error);
}

// Ambil status awal saat halaman dimuat
window.addEventListener('DOMContentLoaded', () => {
  fetch('/relay-status')
    .then(r => r.json())
    .then(updateRelayStatusUI)
    .catch(console.error);
});