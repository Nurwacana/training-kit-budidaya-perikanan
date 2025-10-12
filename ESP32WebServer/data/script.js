// === Variabel Global untuk Fetching ===
// Variabel _last dan _fetching sudah tidak diperlukan lagi

// FUNGSI INI TELAH DIPERBAIKI
// Selalu mengambil data terbaru tanpa logika caching yang salah.
async function fetchLast() {
  try {
    const response = await fetch('/last', { cache: 'no-store' });
    if (!response.ok) {
      console.error("Gagal mengambil data sensor:", response.statusText);
      return null; // Kembalikan null jika ada error
    }
    return await response.json();
  } catch (e) {
    console.error("Terjadi exception saat fetch:", e);
    return null; // Kembalikan null jika ada exception
  }
}

// === Fungsi Pembuat Chart Real-time ===
function makeRealtimeChart(canvasId, key, color, yMin, yMax) {
  const ctx = document.getElementById(canvasId).getContext('2d');
  return new Chart(ctx, {
    type: 'line',
    data: {
      datasets: [{
        label: key,
        data: [],
        borderColor: color,
        borderWidth: 2,
        pointRadius: 0,
        cubicInterpolationMode: 'monotone',
        fill: false
      }]
    },
    options: {
      responsive: true,
      maintainAspectRatio: false,
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
              const j = await fetchLast(); // Panggil fungsi yang sudah diperbaiki
              if (!j || typeof j[key] !== 'number') return; // Cek jika data valid

              chart.data.datasets[0].data.push({ x: Date.now(), y: j[key] });

              // Panggil updateTable dari sini agar sinkron dengan chart
              const tableIdMap = {
                  ph: 'tablePH',
                  turb: 'tableTurbidity',
                  oks: 'tableOksigen',
                  suhu: 'tableSuhu'
              };
              if (tableIdMap[key]) {
                  updateTable(tableIdMap[key], key, j[key]);
              }
            }
          }
        },
        y: { min: yMin, max: yMax }
      }
    }
  });
}

// === Fungsi Update Tabel ===
// Diperbarui untuk menerima nilai langsung agar tidak perlu fetch lagi
function updateTable(tableId, key, value) {
  const tableBody = document.querySelector(`#${tableId} tbody`);
  const nowStr = new Date().toLocaleTimeString();
  
  // Buat baris baru dan masukkan ke awal tabel
  tableBody.insertAdjacentHTML('afterbegin', `<tr><td>${nowStr}</td><td>${value.toFixed(2)}</td></tr>`);
  
  // Jaga agar jumlah baris tidak lebih dari 20
  if (tableBody.children.length > 20) {
    tableBody.removeChild(tableBody.lastElementChild);
  }
}

// === Inisialisasi Chart ===
const chartPH = makeRealtimeChart('chartPH', 'ph', '#4CAF50', 0, 14);
const chartTurb = makeRealtimeChart('chartTurbidity', 'turb', '#2196F3', 0, 100);
const chartOks = makeRealtimeChart('chartOksigen', 'oks', '#FF9800', 0, 20);
const chartSuhu = makeRealtimeChart('chartSuhu', 'suhu', '#E91E63', 0, 50);

// === Memuat Data Historis Saat Halaman Dibuka ===
async function loadInitialData() {
  try {
    console.log("Memuat data historis...");
    const response = await fetch('/data');
    const historicalData = await response.json();
    const now = Date.now();

    const items = {
      ph: { chart: chartPH, tableId: 'tablePH' },
      turb: { chart: chartTurb, tableId: 'tableTurbidity' },
      oks: { chart: chartOks, tableId: 'tableOksigen' },
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
          const timestamp = now - (historicalData[key].length - 1 - index) * 1000; // Asumsi interval 1 detik
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

// === WebSocket Logic ===
let ws;
function connectWebSocket() {
    ws = new WebSocket(`ws://${window.location.hostname}:81`);
    
    ws.onopen = () => {
        console.log('WebSocket Connected');
    };
    
    ws.onclose = () => {
        console.log('WebSocket Disconnected');
        setTimeout(connectWebSocket, 1000);
    };
    
    ws.onmessage = (event) => {
        try {
            const data = JSON.parse(event.data);
            console.log('Received WebSocket data:', data); // Debug log
            updateRelayStatusUI(data);
        } catch(e) {
            console.error('WebSocket message error:', e);
        }
    };
}

// ganti fungsi sendButton
async function sendButton(relay) {
    if(!ws || ws.readyState !== WebSocket.OPEN) return;
    
    const idx = relay.replace('relay', '');
    const statusEl = document.getElementById('status-relay' + idx);
    const current = statusEl ? statusEl.textContent.trim() : 'OFF';
    const desired = (current === 'ON') ? 'off' : 'on';
    
    ws.send(relay + '_' + desired);
}

// ganti fungsi toggleMode
function toggleMode() {
    if(!ws || ws.readyState !== WebSocket.OPEN) return;
    
    const modeBtn = document.getElementById('mode-toggle');
    const currentMode = modeBtn.textContent.trim().toLowerCase();
    ws.send('mode_' + (currentMode === 'auto' ? 'manual' : 'auto'));
}

// update event listener
window.addEventListener('DOMContentLoaded', () => {
    loadInitialData();
    connectWebSocket();
});

// === Missing functions for the buttons and mode toggle ===
// Update UI untuk status relay dan mode (fungsi dipanggil dari beberapa tempat)
function updateRelayStatusUI(status) {
  try {
    // Update relay status
    for (let i = 1; i <= 5; i++) {
      const key = 'relay' + i;
      if (status.hasOwnProperty(key)) {
        const isOn = status[key];
        const statusEl = document.getElementById('status-' + key);
        if (statusEl) {
          statusEl.textContent = isOn ? 'ON' : 'OFF';
          // Update button state/appearance
          const btn = statusEl.previousElementSibling;
          if (btn && btn.classList.contains('relay-btn')) {
            btn.classList.toggle('active', isOn);
          }
        }
      }
    }

    // Update mode if present
    if (status.hasOwnProperty('mode')) {
      const modeBtn = document.getElementById('mode-toggle');
      if (modeBtn) {
        modeBtn.textContent = status.mode.charAt(0).toUpperCase() + status.mode.slice(1);
        // Update button states based on mode
        const buttons = document.querySelectorAll('.relay-btn');
        buttons.forEach(btn => {
          btn.disabled = status.mode === 'auto';
        });
      }
    }
  } catch (e) {
    console.error('Error updating UI:', e);
  }
}

// Add these after your existing code

// Function to load current thresholds
async function loadThresholds() {
    try {
        const response = await fetch('/thresholds');
        const thresholds = await response.json();
        
        // Update input fields
        for (const sensor of ['ph', 'turb', 'oks', 'suhu']) {
            document.getElementById(`${sensor}-min`).value = thresholds[sensor].min;
            document.getElementById(`${sensor}-max`).value = thresholds[sensor].max;
        }
    } catch (error) {
        console.error('Failed to load thresholds:', error);
    }
}

// Function to update threshold
async function updateThreshold(sensor) {
    const minVal = parseFloat(document.getElementById(`${sensor}-min`).value);
    const maxVal = parseFloat(document.getElementById(`${sensor}-max`).value);
    
    if (isNaN(minVal) || isNaN(maxVal)) {
        alert('Please enter valid numbers');
        return;
    }
    
    if (minVal >= maxVal) {
        alert('Minimum value must be less than maximum value');
        return;
    }
    
    try {
        const response = await fetch('/thresholds', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json',
            },
            body: JSON.stringify({
                sensor,
                min: minVal,
                max: maxVal
            })
        });
        
        if (!response.ok) throw new Error('Failed to update threshold');
        
        alert('Threshold updated successfully');
    } catch (error) {
        console.error('Error updating threshold:', error);
        alert('Failed to update threshold');
    }
}

// Load thresholds when page loads
document.addEventListener('DOMContentLoaded', () => {
    loadThresholds();
    // ... existing DOMContentLoaded handlers ...
});