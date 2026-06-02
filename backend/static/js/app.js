async function fetchStatus() {
  const response = await fetch("/api/status");
  const data = await response.json();
  renderStatus(data);
}

function renderStatus(data) {
  document.getElementById("runtimeStatus").textContent = data.runtime.status || "idle";
  document.getElementById("activeMode").textContent = data.runtime.active_mode || "-";

  document.getElementById("apRunning").textContent = data.wifi.ap_running ? "Yes" : "No";
  document.getElementById("uplinkState").textContent = data.wifi.uplink_connected ? "Connected" : "Offline";
  document.getElementById("clientCount").textContent = data.wifi.connected_clients.length;

  document.getElementById("ssidValue").textContent = data.wifi.ssid || "-";
  document.getElementById("channelValue").textContent = data.wifi.channel || "-";
  document.getElementById("ipValue").textContent = data.wifi.ip_address || "-";

  const clientList = document.getElementById("clientList");
  clientList.innerHTML = "";
  const clients = data.wifi.connected_clients || [];
  if (clients.length === 0) {
    clientList.innerHTML = "<li>No clients connected</li>";
  } else {
    clients.forEach(client => {
      const li = document.createElement("li");
      li.textContent = client;
      clientList.appendChild(li);
    });
  }

  document.getElementById("detectionMode").textContent = data.inference.detection_mode || "-";
  document.getElementById("inferenceRunning").textContent = data.inference.inference_running ? "Yes" : "No";
  document.getElementById("lastFrame").textContent = data.inference.last_frame_id || "-";
  document.getElementById("inferenceResults").textContent =
    JSON.stringify(data.inference.last_inference || [], null, 2);
}

async function postJSON(url, body = {}) {
  const response = await fetch(url, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(body)
  });

  const data = await response.json();

  if (!response.ok) {
    throw new Error(data.error || "Request failed");
  }

  return data;
}

document.getElementById("startApBtn").addEventListener("click", async () => {
  await postJSON("/api/wifi/start-ap");
  await fetchStatus();
});

document.getElementById("stopApBtn").addEventListener("click", async () => {
  await postJSON("/api/wifi/stop-ap");
  await fetchStatus();
});

document.getElementById("uplinkBtn").addEventListener("click", async () => {
  const current = document.getElementById("uplinkState").textContent === "Connected";
  await postJSON("/api/wifi/uplink", { connected: !current });
  await fetchStatus();
});

document.getElementById("faceModeBtn").addEventListener("click", async () => {
  await postJSON("/api/inference/mode", { mode: "face" });
  await fetchStatus();
});

document.getElementById("objectModeBtn").addEventListener("click", async () => {
  await postJSON("/api/inference/mode", { mode: "object" });
  await fetchStatus();
});

document.getElementById("runInferenceBtn").addEventListener("click", async () => {
  const result = await postJSON("/api/inference/run");
  document.getElementById("inferenceResults").textContent =
    JSON.stringify(result.detections || [], null, 2);
  document.getElementById("lastFrame").textContent = result.frame_id || "-";
  await fetchStatus();
});

fetchStatus();
setInterval(fetchStatus, 5000);