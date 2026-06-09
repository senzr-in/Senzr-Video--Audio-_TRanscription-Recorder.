const API_BASE = "http://127.0.0.1:8000";

const statusEl = document.getElementById("status");
const hostnameEl = document.getElementById("hostname");
const ipAddressEl = document.getElementById("ip_address");
const messageEl = document.getElementById("message");
const configForm = document.getElementById("configForm");

async function loadStatus() {
  try {
    const response = await fetch(`${API_BASE}/api/status`);
    const data = await response.json();

    statusEl.textContent = data.status;
    hostnameEl.textContent = data.hostname;
    ipAddressEl.textContent = data.ip_address;
  } catch (error) {
    statusEl.textContent = "Error";
    hostnameEl.textContent = "Unavailable";
    ipAddressEl.textContent = "Unavailable";
  }
}

async function loadConfig() {
  try {
    const response = await fetch(`${API_BASE}/api/config`);
    const config = await response.json();

    document.getElementById("device_name").value = config.device_name;
    document.getElementById("mode").value = config.mode;
    document.getElementById("wifi_ssid").value = config.wifi_ssid;
    document.getElementById("wifi_password").value = config.wifi_password;
    document.getElementById("provisioning_enabled").checked = config.provisioning_enabled;
    document.getElementById("camera_connected").checked = config.camera_connected;
  } catch (error) {
    messageEl.textContent = "Failed to load configuration.";
  }
}

configForm.addEventListener("submit", async (event) => {
  event.preventDefault();

  const payload = {
    device_name: document.getElementById("device_name").value,
    mode: document.getElementById("mode").value,
    wifi_ssid: document.getElementById("wifi_ssid").value,
    wifi_password: document.getElementById("wifi_password").value,
    provisioning_enabled: document.getElementById("provisioning_enabled").checked,
    camera_connected: document.getElementById("camera_connected").checked
  };

  try {
    const response = await fetch(`${API_BASE}/api/config`, {
      method: "POST",
      headers: {
        "Content-Type": "application/json"
      },
      body: JSON.stringify(payload)
    });

    if (!response.ok) {
      throw new Error("Save failed");
    }

    messageEl.textContent = "Configuration saved successfully.";
  } catch (error) {
    messageEl.textContent = "Failed to save configuration.";
  }
});

loadStatus();
loadConfig();