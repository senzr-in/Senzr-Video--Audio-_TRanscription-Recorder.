async function loadConfig() {
  const res = await fetch('/api/config', { cache: 'no-store' });
  if (!res.ok) {
    throw new Error(`HTTP ${res.status}`);
  }

  const data = await res.json();

  document.getElementById('config-loaded').textContent = data.config_loaded ? 'Yes' : 'No';
  document.getElementById('camera-connected').textContent = data.camera_connected ? 'Yes' : 'No';
  document.getElementById('hostname').textContent = data.hostname || '-';
  document.getElementById('ip-address').textContent = data.ip_address || '-';
}

window.addEventListener('DOMContentLoaded', () => {
  loadConfig().catch(console.error);
  setInterval(() => loadConfig().catch(console.error), 5000);
});
