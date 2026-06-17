/*
 * Edge Gateway v2.0.0 — Camera Control Dashboard
 *
 * Rules:
 *  - All fetch() calls use same-origin relative paths only (no 127.0.0.1).
 *  - Camera status is polled every 3 seconds automatically.
 *  - ON/OFF buttons are disabled while a request is in-flight.
 */

"use strict";

// ── Element references ─────────────────────────────────────────────────────
const statusEl          = document.getElementById("status");
const hostnameEl        = document.getElementById("hostname");
const ipAddressEl       = document.getElementById("ip_address");

const cameraStatusText  = document.getElementById("cameraStatusText");
const recordingStatus   = document.getElementById("recordingStatus");
const recordedFileEl    = document.getElementById("recordedFile");
const recordingDot      = document.getElementById("recordingDot");
const cameraMessage     = document.getElementById("cameraMessage");

const btnOn             = document.getElementById("btnCameraOn");
const btnOff            = document.getElementById("btnCameraOff");

// ── Helpers ────────────────────────────────────────────────────────────────

function showMessage(text, isError = false) {
  cameraMessage.textContent = text;
  cameraMessage.className   = isError ? "message message-error" : "message message-ok";
  // Auto-clear after 4 seconds
  clearTimeout(cameraMessage._timer);
  cameraMessage._timer = setTimeout(() => {
    cameraMessage.textContent = "";
    cameraMessage.className   = "message";
  }, 4000);
}

function setButtonsLoading(loading) {
  btnOn.disabled  = loading;
  btnOff.disabled = loading;
}

// ── Device Status ──────────────────────────────────────────────────────────

async function loadDeviceStatus() {
  try {
    const res  = await fetch("/api/status");
    if (!res.ok) throw new Error("HTTP " + res.status);
    const data = await res.json();

    statusEl.textContent    = data.status    ?? "Unknown";
    hostnameEl.textContent  = data.hostname  ?? "Unknown";
    ipAddressEl.textContent = data.ip_address ?? "Unknown";

    // Remove shimmer once data arrives
    [statusEl, hostnameEl, ipAddressEl].forEach(el => el.classList.remove("loading-shimmer"));

    statusEl.className = "value " + (data.status === "online" ? "status-on" : "status-off");
  } catch (err) {
    statusEl.textContent    = "Error";
    hostnameEl.textContent  = "Unavailable";
    ipAddressEl.textContent = "Unavailable";
    [statusEl, hostnameEl, ipAddressEl].forEach(el => el.classList.remove("loading-shimmer"));
  }
}

// ── Camera Status ──────────────────────────────────────────────────────────

async function loadCameraStatus() {
  try {
    const res  = await fetch("/api/camera/status");
    if (!res.ok) throw new Error("HTTP " + res.status);
    const data = await res.json();
    applyCameraStatus(data);
  } catch (err) {
    // Silently fail on poll — do not spam the user on every 3-second tick
  }
}

function applyCameraStatus(data) {
  const { camera_enabled, is_recording, current_recording_file, last_recorded_file } = data;

  // ── Camera Status label ────────────────────────────────────────────────
  cameraStatusText.textContent = camera_enabled ? "ON" : "OFF";
  cameraStatusText.className   = "value " + (camera_enabled ? "status-on" : "status-off");

  // ── Button states ──────────────────────────────────────────────────────
  btnOn.disabled  = camera_enabled;   // already ON → disable ON button
  btnOff.disabled = !camera_enabled;  // already OFF → disable OFF button
  btnOn.classList.toggle("btn-active",  camera_enabled);
  btnOff.classList.toggle("btn-active", !camera_enabled);

  // ── Recording Status ───────────────────────────────────────────────────
  if (is_recording) {
    recordingStatus.textContent = "Recording";
    recordingStatus.className   = "value status-recording";
    recordingDot.classList.add("dot-active");
  } else {
    recordingStatus.textContent = camera_enabled ? "Idle — waiting for person" : "Idle";
    recordingStatus.className   = "value status-idle";
    recordingDot.classList.remove("dot-active");
  }

  // ── Recorded File ──────────────────────────────────────────────────────
  // Priority: current active file > latest completed file > placeholder
  const displayFile = current_recording_file || last_recorded_file || null;
  if (displayFile) {
    recordedFileEl.textContent = displayFile;
    recordedFileEl.className   = "value file-value file-present";
  } else {
    recordedFileEl.textContent = "No recordings yet";
    recordedFileEl.className   = "value file-value";
  }
}

// ── Camera ON ──────────────────────────────────────────────────────────────

async function cameraOn() {
  setButtonsLoading(true);
  try {
    const res  = await fetch("/api/camera/on", { method: "POST" });
    const data = await res.json();
    showMessage(data.message ?? "Camera ON", !data.ok);
    await loadCameraStatus();  // immediate refresh
  } catch (err) {
    showMessage("Failed to turn camera ON.", true);
  } finally {
    setButtonsLoading(false);
  }
}

// ── Camera OFF ─────────────────────────────────────────────────────────────

async function cameraOff() {
  setButtonsLoading(true);
  try {
    const res  = await fetch("/api/camera/off", { method: "POST" });
    const data = await res.json();
    showMessage(data.message ?? "Camera OFF", !data.ok);
    await loadCameraStatus();  // immediate refresh
  } catch (err) {
    showMessage("Failed to turn camera OFF.", true);
  } finally {
    setButtonsLoading(false);
  }
}

// ── Polling ────────────────────────────────────────────────────────────────

// Poll camera status every 3 seconds so filename and recording state update live
setInterval(loadCameraStatus, 3000);

// ── Init ───────────────────────────────────────────────────────────────────

loadDeviceStatus();
loadCameraStatus();
