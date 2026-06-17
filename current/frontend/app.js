const statusEl = document.getElementById("status");
const hostnameEl = document.getElementById("hostname");
const ipAddressEl = document.getElementById("ip_address");
const messageEl = document.getElementById("message");
const configForm = document.getElementById("configForm");

async function loadStatus() {
  try {
    const response = await fetch("/api/status");

    if (!response.ok) {
      throw new Error("Failed to fetch status");
    }

    const data = await response.json();

    statusEl.textContent = data.status ?? "Unknown";
    hostnameEl.textContent = data.hostname ?? "Unknown";
    ipAddressEl.textContent = data.ip_address ?? "Unknown";
  } catch (error) {
    statusEl.textContent = "Error";
    hostnameEl.textContent = "Unavailable";
    ipAddressEl.textContent = "Unavailable";
    messageEl.textContent = "Failed to load device status.";
  }
}

async function loadConfig() {
  try {
    const response = await fetch("/api/config");

    if (!response.ok) {
      throw new Error("Failed to fetch config");
    }

    const config = await response.json();

    document.getElementById("device_name").value = config.device_name ?? "";
    document.getElementById("mode").value = config.mode ?? "face";
    document.getElementById("wifi_ssid").value = config.wifi_ssid ?? "";
    document.getElementById("wifi_password").value = config.wifi_password ?? "";
    document.getElementById("provisioning_enabled").checked = config.provisioning_enabled ?? false;
    document.getElementById("camera_connected").checked = config.camera_connected ?? false;

    messageEl.textContent = "";
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
    const response = await fetch("/api/config", {
      method: "POST",
      headers: {
        "Content-Type": "application/json"
      },
      body: JSON.stringify(payload)
    });

    if (!response.ok) {
      throw new Error("Save failed");
    }

    const savedConfig = await response.json();

    document.getElementById("device_name").value = savedConfig.device_name ?? "";
    document.getElementById("mode").value = savedConfig.mode ?? "face";
    document.getElementById("wifi_ssid").value = savedConfig.wifi_ssid ?? "";
    document.getElementById("wifi_password").value = savedConfig.wifi_password ?? "";
    document.getElementById("provisioning_enabled").checked = savedConfig.provisioning_enabled ?? false;
    document.getElementById("camera_connected").checked = savedConfig.camera_connected ?? false;

    messageEl.textContent = "Configuration saved successfully.";
  } catch (error) {
    messageEl.textContent = "Failed to save configuration.";
  }
});

loadStatus();
loadConfig();