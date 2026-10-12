"use strict";

// ── Notification feedback ──────────────────────────────────────────────────
function showMsg(elId, text, isError = false) {
  const el = document.getElementById(elId);
  if (!el) return;
  el.textContent = text;
  el.className = isError ? "feedback-msg error active" : "feedback-msg success active";
  clearTimeout(el._timer);
  el._timer = setTimeout(() => {
    el.textContent = "";
    el.className = "feedback-msg";
  }, 4500);
}

function escapeHtml(str) {
  if (typeof str !== "string") return str == null ? "" : String(str);
  return str.replace(/[&<>"']/g, m => ({
    "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;"
  })[m]);
}

// ── SenzrOS Tab Switching ──────────────────────────────────────────────────
let activeTab = "sentinel";

const TAB_TITLES = {
  sentinel: { title: "Sentinel & Vision", breadcrumb: "Overview / Real-Time Vision & ANPR" },
  storage:  { title: "Storage Hub", breadcrumb: "System / Flash Storage & SQLite Database" },
  gallery:  { title: "Media Gallery", breadcrumb: "Events / License Plates & Person Captures" },
  hardware: { title: "Hardware & NPU", breadcrumb: "Hardware / RK3588 Tri-Core NPU & Octa-Core Telemetry" },
  loadlab:  { title: "AI Models & NPU Load Lab", breadcrumb: "Benchmark / Tri-Core 6.0 TOPS Multi-Model Stress Engine" },
  gpio:     { title: "Access Control", breadcrumb: "Hardware / 40-Pin GPIO & Door Strike Relays" },
  network:  { title: "Network & System", breadcrumb: "Connectivity / Wi-Fi, Ethernet & Clock Management" }
};

function switchTab(tabId) {
  if (!TAB_TITLES[tabId]) return;
  activeTab = tabId;

  // Update navigation items
  document.querySelectorAll(".nav-item").forEach(item => {
    item.classList.remove("active");
  });
  const activeNav = document.getElementById(`nav-${tabId}`);
  if (activeNav) activeNav.classList.add("active");

  // Update tab panes
  document.querySelectorAll(".tab-pane").forEach(pane => {
    pane.classList.remove("active");
  });
  const activePane = document.getElementById(`pane-${tabId}`);
  if (activePane) activePane.classList.add("active");

  // Update Topbar
  const meta = TAB_TITLES[tabId];
  const titleEl = document.getElementById("pageTitle");
  const crumbEl = document.getElementById("pageBreadcrumb");
  if (titleEl) titleEl.textContent = meta.title;
  if (crumbEl) crumbEl.textContent = meta.breadcrumb;

  // Trigger tab-specific refresh
  if (tabId === "storage") {
    loadStorageHub();
    loadOfflineStorage();
  } else if (tabId === "gallery") {
    loadGallery();
  } else if (tabId === "hardware") {
    loadHardwareTelemetry();
    loadTelemetryLog();
  } else if (tabId === "loadlab") {
    loadModelsCatalog();
    updateLoadTestStatus();
  } else if (tabId === "gpio") {
    loadGpioPins();
    loadBleStatusAndConfig();
    loadBleCredentials();
  }
}

// ── Lightbox Modal ─────────────────────────────────────────────────────────
function openLightbox(url, title, meta) {
  const modal = document.getElementById("lightboxModal");
  const img   = document.getElementById("lightboxImg");
  const titleEl = document.getElementById("lightboxTitle");
  const metaEl  = document.getElementById("lightboxMeta");
  const dlBtn   = document.getElementById("lightboxDownloadBtn");

  if (!modal || !img) return;

  img.src = url;
  if (titleEl) titleEl.textContent = title || "Snapshot View";
  if (metaEl)  metaEl.textContent = meta || "";
  if (dlBtn) {
    dlBtn.href = url;
    dlBtn.setAttribute("download", title || "snapshot.jpg");
  }

  modal.classList.add("active");
}

function closeLightbox(event) {
  const modal = document.getElementById("lightboxModal");
  if (modal) {
    modal.classList.remove("active");
    const img = document.getElementById("lightboxImg");
    if (img) img.src = "";
  }
}

// ── Camera & Vision Control ────────────────────────────────────────────────
async function cameraOn() {
  try {
    const res = await fetch("/api/camera/on", { method: "POST" });
    const data = await res.json();
    showMsg("cameraMessage", data.message || "Camera pipeline started!");
    reloadStream();
  } catch (err) {
    showMsg("cameraMessage", "Failed to start camera pipeline", true);
  }
}

async function cameraOff() {
  try {
    const res = await fetch("/api/camera/off", { method: "POST" });
    const data = await res.json();
    showMsg("cameraMessage", data.message || "Camera pipeline stopped.");
  } catch (err) {
    showMsg("cameraMessage", "Failed to stop camera", true);
  }
}

function reloadStream() {
  const img1 = document.getElementById("visionLiveFeed");
  if (img1) img1.src = "/api/vision/stream?t=" + Date.now();
  const img2 = document.querySelector("#pane-loadlab .stream-live-element");
  if (img2) img2.src = "/api/vision/stream?t=" + Date.now();
}

async function captureSnapshot() {
  window.open("/api/vision/snapshot?t=" + Date.now(), "_blank");
}

async function setVisionMode(mode) {
  try {
    const res = await fetch("/api/vision/mode", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ mode: mode })
    });
    const data = await res.json();
    if (data.ok) {
      document.querySelectorAll(".mode-segmented-bar .seg-pill-btn").forEach(btn => btn.classList.remove("active"));
      if (mode === "both" || mode === "all") document.getElementById("modeBoth")?.classList.add("active");
      else if (mode === "vehicle") document.getElementById("modeVehicle")?.classList.add("active");
      else if (mode === "plate") document.getElementById("modePlate")?.classList.add("active");
      else if (mode === "person") document.getElementById("modePerson")?.classList.add("active");
      showMsg("cameraMessage", `AI Mode switched to: ${mode.toUpperCase()}`);
    }
  } catch (err) {
    console.error("Error setting vision mode:", err);
  }
}

// ── Quick Door Unlock Pulse with Countdown ─────────────────────────────────
let doorUnlockTimer = null;

async function unlockDoor(pulseMs = 3000) {
  const btn = document.getElementById("btnTopQuickUnlock");
  const textEl = document.getElementById("quickUnlockText");
  const prevHtml = textEl ? textEl.innerHTML : "";

  try {
    const res = await fetch("/api/gpio/pulse", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ pin: 7, alias: "DOOR_RELAY", duration_ms: pulseMs, active_low: true })
    });
    const data = await res.json();
    showMsg("gpioMessage", data.message || "Door unlocked!");

    if (btn && textEl) {
      btn.disabled = true;
      let remain = Math.ceil(pulseMs / 1000);
      textEl.textContent = `Door Unlocked (${remain}s)...`;

      clearInterval(doorUnlockTimer);
      doorUnlockTimer = setInterval(() => {
        remain--;
        if (remain > 0) {
          textEl.textContent = `Door Unlocked (${remain}s)...`;
        } else {
          clearInterval(doorUnlockTimer);
          textEl.textContent = "Door Locked ✓";
          setTimeout(() => {
            textEl.innerHTML = prevHtml || "Unlock Door (3s)";
            btn.disabled = false;
          }, 1200);
        }
      }, 1000);
    }
  } catch (err) {
    showMsg("gpioMessage", "Failed to send door unlock command", true);
  }
}

// ── Real-time Hardware Telemetry (/api/system/hardware) ────────────────────
async function loadHardwareTelemetry() {
  try {
    const res = await fetch("/api/system/hardware", { cache: "no-store" });
    if (!res.ok) return;
    const hw = await res.json();

    // Top chips
    const chipNpu = document.getElementById("topChipNpu");
    const chipCpu = document.getElementById("topChipCpu");
    const chipRam = document.getElementById("topChipRam");
    const chipDisk= document.getElementById("topChipDisk");

    if (chipNpu && hw.npu_temp_c != null) chipNpu.textContent = `${hw.npu_temp_c.toFixed(1)}°C`;
    if (chipCpu && hw.cpu_temp_c != null) chipCpu.textContent = `${hw.cpu_temp_c.toFixed(1)}°C`;
    if (chipRam && hw.ram && hw.ram.used_pct != null) chipRam.textContent = `${Math.round(hw.ram.used_pct)}%`;

    // Sidebar
    const sideUptime = document.getElementById("sidebarUptime");
    const sideNpu = document.getElementById("sidebarNpuBadge");
    if (sideUptime && hw.uptime_human) sideUptime.textContent = hw.uptime_human;
    if (sideNpu && hw.npu_temp_c != null) sideNpu.textContent = `${hw.npu_temp_c.toFixed(0)}°C`;

    // Tab 4: Hardware View
    const hwNpu = document.getElementById("hwNpuTemp");
    const hwCpu = document.getElementById("hwCpuTemp");
    const hwGpu = document.getElementById("hwGpuTemp");
    const hwLoad = document.getElementById("hwCpuLoad");
    const hwCores = document.getElementById("hwCpuCores");
    const hwUptimeHuman = document.getElementById("hwUptimeHuman");

    if (hwNpu) hwNpu.textContent = `${hw.npu_temp_c.toFixed(1)}°C`;
    if (hwCpu) hwCpu.textContent = `${hw.cpu_temp_c.toFixed(1)}°C`;
    if (hwGpu) hwGpu.textContent = `${hw.gpu_temp_c.toFixed(1)}°C`;

    if (hw.cpu_load && hwLoad) {
      hwLoad.textContent = `Load: ${hw.cpu_load.load_1m.toFixed(2)}, ${hw.cpu_load.load_5m.toFixed(2)}, ${hw.cpu_load.load_15m.toFixed(2)}`;
    }
    if (hw.cpu_load && hwCores) {
      hwCores.textContent = `${hw.cpu_load.cores || 8} Cores`;
    }
    if (hwUptimeHuman) hwUptimeHuman.textContent = hw.uptime_human || "-";

    // RAM meter
    if (hw.ram) {
      const ramProg = document.getElementById("hwRamProgress");
      const ramText = document.getElementById("hwRamUsedText");
      const ramUsed = document.getElementById("hwRamUsedVal");
      const ramFree = document.getElementById("hwRamFreeVal");
      const ramTotal= document.getElementById("hwRamTotalVal");

      if (ramProg) ramProg.style.width = `${Math.min(100, hw.ram.used_pct)}%`;
      if (ramText) ramText.textContent = `${hw.ram.used_mb} / ${hw.ram.total_mb} MB (${Math.round(hw.ram.used_pct)}%)`;
      if (ramUsed) ramUsed.textContent = `${hw.ram.used_mb} MB`;
      if (ramFree) ramFree.textContent = `${hw.ram.free_mb} MB`;
      if (ramTotal) ramTotal.textContent = `${hw.ram.total_mb} MB`;
    }
  } catch (err) {}
}

// ── Storage Hub (/api/system/storage) ──────────────────────────────────────
async function loadStorageHub() {
  try {
    const res = await fetch("/api/system/storage", { cache: "no-store" });
    if (!res.ok) return;
    const data = await res.json();

    // Root disk
    if (data.root_disk) {
      const rd = data.root_disk;
      const prog = document.getElementById("rootDiskProgress");
      const usedEl = document.getElementById("rootDiskUsed");
      const freeEl = document.getElementById("rootDiskFree");
      const totalEl= document.getElementById("rootDiskTotal");
      const pctEl  = document.getElementById("rootDiskPct");
      const topDisk= document.getElementById("topChipDisk");
      const sideDisk= document.getElementById("sidebarDiskBadge");

      const pct = Math.round(rd.used_pct || 0);
      if (prog) {
        prog.style.width = `${Math.min(100, pct)}%`;
        if (pct > 85) prog.classList.add("warn");
        else prog.classList.remove("warn");
      }
      if (usedEl)  usedEl.textContent = `${rd.used_gb.toFixed(1)} GB`;
      if (freeEl)  freeEl.textContent = `${rd.free_gb.toFixed(1)} GB`;
      if (totalEl) totalEl.textContent = `${rd.total_gb.toFixed(1)} GB`;
      if (pctEl)   pctEl.textContent = `${pct}%`;
      if (topDisk) topDisk.textContent = `${pct}%`;
      if (sideDisk) sideDisk.textContent = `${pct}%`;
    }

    // Breakdown
    if (data.recordings) {
      const rec = data.recordings;
      
      const vSize = document.getElementById("storageVideosSize");
      const vCount = document.getElementById("storageVideosCount");
      if (rec.video_sessions) {
        if (vSize) vSize.textContent = `${rec.video_sessions.size_mb.toFixed(1)} MB`;
        if (vCount) vCount.textContent = rec.video_sessions.count;
      }

      const pSize = document.getElementById("storagePlatesSize");
      const pCount = document.getElementById("storagePlatesCount");
      if (rec.plates) {
        if (pSize) pSize.textContent = `${rec.plates.size_mb.toFixed(1)} MB`;
        if (pCount) pCount.textContent = rec.plates.count;
      }

      const perSize = document.getElementById("storagePersonsSize");
      const perCount = document.getElementById("storagePersonsCount");
      if (rec.persons) {
        if (perSize) perSize.textContent = `${rec.persons.size_mb.toFixed(1)} MB`;
        if (perCount) perCount.textContent = rec.persons.count;
      }
    }

    // Database
    if (data.database) {
      const db = data.database;
      const dbSize = document.getElementById("storageDbSize");
      const dbRows = document.getElementById("storageDbRows");
      if (dbSize) dbSize.textContent = `${db.size_mb.toFixed(2)} MB`;
      if (dbRows) {
        const totalRows = (db.total_sessions || 0) + (db.total_detection_events || 0) + (db.total_system_events || 0) + (db.total_telemetry_rows || 0);
        dbRows.textContent = totalRows;
      }
    }
  } catch (err) {}
}

async function cleanStorage(target) {
  const confirmMsg = target === "all" 
    ? "Are you sure you want to purge all stored plate and person snapshots?" 
    : `Clean ${target} storage?`;
  if (!confirm(confirmMsg)) return;

  try {
    const res = await fetch("/api/system/storage/clean", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ target: target })
    });
    const data = await res.json();
    if (data.ok) {
      showMsg("storageCleanMsg", `Cleaned ${data.deleted_files} files, reclaimed ${data.freed_mb.toFixed(1)} MB!`);
      loadStorageHub();
      loadGallery();
    } else {
      showMsg("storageCleanMsg", "Storage cleaning failed", true);
    }
  } catch (err) {
    showMsg("storageCleanMsg", "Network error executing clean", true);
  }
}

// ── Media & Gallery Tab ────────────────────────────────────────────────────
let currentGalleryFilter = "all";

function setGalleryFilter(filter) {
  currentGalleryFilter = filter;
  document.getElementById("galFilterAll")?.classList.toggle("active", filter === "all");
  document.getElementById("galFilterPlates")?.classList.toggle("active", filter === "plates");
  document.getElementById("galFilterPersons")?.classList.toggle("active", filter === "persons");
  loadGallery();
}

async function loadGallery() {
  const grid = document.getElementById("galleryGrid");
  if (!grid) return;

  try {
    let items = [];

    if (currentGalleryFilter === "all" || currentGalleryFilter === "plates") {
      const res = await fetch("/api/plates/gallery?limit=40", { cache: "no-store" });
      if (res.ok) {
        const plates = await res.json();
        plates.forEach(p => items.push({ ...p, type: "plate" }));
      }
    }

    if (currentGalleryFilter === "all" || currentGalleryFilter === "persons") {
      const res = await fetch("/api/persons/gallery?limit=40", { cache: "no-store" });
      if (res.ok) {
        const persons = await res.json();
        persons.forEach(p => items.push({ ...p, type: "person" }));
      }
    }

    const sideBadge = document.getElementById("sidebarGalleryBadge");
    if (sideBadge) sideBadge.textContent = items.length;

    if (items.length === 0) {
      grid.innerHTML = `
        <div style="grid-column: 1 / -1; text-align: center; padding: 48px 16px; color: var(--text-muted);">
          <div style="font-size: 2.5rem; margin-bottom: 8px;">📷</div>
          <h4 style="color: var(--text-secondary); margin-bottom: 4px;">No Media Snapshots Found</h4>
          <p style="font-size: 0.85rem;">Captured license plates and detected persons will automatically appear here.</p>
        </div>
      `;
      return;
    }

    grid.innerHTML = "";
    items.forEach(item => {
      const card = document.createElement("div");
      card.className = "media-card";

      const isPlate = item.type === "plate";
      const tagText = isPlate ? (item.plate_text || "PLATE DETECTED") : "PERSON DETECTED";
      const badgeClass = isPlate ? "badge-plate" : "badge-person";
      const kbSize = item.size_bytes ? (item.size_bytes / 1024).toFixed(0) + " KB" : "";

      card.innerHTML = `
        <div class="media-thumb-box">
          <img class="media-thumb-img" src="${escapeHtml(item.url)}" alt="${escapeHtml(tagText)}" loading="lazy" />
          <div class="media-overlay-actions">
            <button class="media-action-circle" title="Zoom in Lightbox" onclick="openLightbox('${escapeHtml(item.url)}', '${escapeHtml(tagText)}', '${escapeHtml(item.filename)} • ${kbSize}')">
              🔍
            </button>
            <a class="media-action-circle" title="Download Image" href="${escapeHtml(item.url)}" download="${escapeHtml(item.filename)}" target="_blank">
              ⬇️
            </a>
          </div>
        </div>
        <div class="media-meta">
          <div style="display: flex; justify-content: space-between; align-items: center;">
            <span class="badge ${badgeClass}">${escapeHtml(tagText)}</span>
            <span style="font-size: 0.7rem; color: var(--text-muted); font-family: var(--font-mono);">${kbSize}</span>
          </div>
          <span class="media-time" title="${escapeHtml(item.filename)}">${escapeHtml(item.filename)}</span>
        </div>
      `;
      grid.appendChild(card);
    });

  } catch (err) {
    grid.innerHTML = `<p style="grid-column: 1 / -1; text-align: center; color: var(--accent-rose);">Error loading gallery.</p>`;
  }
}

// ── Camera Pipeline & Live Vision ──────────────────────────────────────────
let currentVisionMode = "both";

async function loadCameraStatus() {
  try {
    const res = await fetch("/api/camera/status", { cache: "no-store" });
    if (!res.ok) return;
    const data = await res.json();
    
    const recText  = document.getElementById("recordingStatus");
    const fileEl   = document.getElementById("recordedFile");
    const btnOn    = document.getElementById("btnCameraOn");
    const btnOff   = document.getElementById("btnCameraOff");
    const topPulse = document.getElementById("topPulse");
    const topStatus= document.getElementById("topStatusPill");
    const streamBlink = document.getElementById("streamRecBlink");

    if (btnOn)  btnOn.disabled = data.camera_enabled;
    if (btnOff) btnOff.disabled = !data.camera_enabled;

    if (data.is_recording) {
      if (recText) {
        recText.textContent = "RECORDING (Active Event)";
        recText.style.color = "var(--accent-rose)";
      }
      if (streamBlink) streamBlink.style.display = "inline-block";
      if (topStatus) {
        topStatus.textContent = "RECORDING";
        topStatus.className = "status-pill status-pill-offline";
      }
    } else if (data.camera_enabled) {
      if (recText) {
        recText.textContent = "MONITORING (AI Sentinel)";
        recText.style.color = "var(--accent-emerald)";
      }
      if (streamBlink) streamBlink.style.display = "none";
      if (topStatus) {
        topStatus.textContent = "ONLINE";
        topStatus.className = "status-pill status-pill-online";
      }
    } else {
      if (recText) {
        recText.textContent = "STANDBY (Pipeline Off)";
        recText.style.color = "var(--text-muted)";
      }
      if (streamBlink) streamBlink.style.display = "none";
      if (topStatus) {
        topStatus.textContent = "STANDBY";
        topStatus.className = "status-pill status-pill-amber";
      }
    }

    if (fileEl) {
      fileEl.textContent = data.current_recording_file || data.last_recorded_file || "No active recording";
    }
  } catch (err) {}
}

async function setVisionMode(mode) {
  try {
    const res = await fetch("/api/vision/mode", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ mode: mode })
    });
    const data = await res.json();
    if (data.ok) {
      updateModeButtons(mode);
      showMsg("cameraMessage", `Detection mode switched to: ${mode.toUpperCase()}`);
    }
  } catch (err) {
    showMsg("cameraMessage", "Failed to switch mode", true);
  }
}

function updateModeButtons(mode) {
  currentVisionMode = mode;
  document.getElementById("modeEmergency")?.classList.toggle("active", mode === "emergency");
  document.getElementById("modePlate")?.classList.toggle("active", mode === "plate");
  document.getElementById("modeVehicle")?.classList.toggle("active", mode === "vehicle");
  document.getElementById("modeBoth")?.classList.toggle("active", mode === "both" || mode === "all" || mode === "traffic");
}

async function loadVisionTelemetry() {
  try {
    const res = await fetch("/api/vision/detections", { cache: "no-store" });
    if (!res.ok) return;
    const data = await res.json();

    const personCountEl = document.getElementById("telPersonCount");
    const personConfEl  = document.getElementById("telPersonConf");
    const plateTextEl   = document.getElementById("telPlateText");
    const plateConfEl   = document.getElementById("telPlateConf");
    const emergencyEl   = document.getElementById("telEmergencyStatus");
    const emergencySubEl = document.getElementById("telEmergencySub");

    if (personCountEl) {
      const vehicles = data.vehicles || data.persons || [];
      personCountEl.textContent = vehicles.length;
      if (vehicles.length > 0) {
        const topConf = Math.round((vehicles[0].score || vehicles[0].confidence || 0) * 100);
        personConfEl.textContent = `Confidence: ${topConf}% • Tracking`;
      } else {
        personConfEl.textContent = "Confidence: -";
      }
    }

    if (emergencyEl) {
      const emergencyVehicles = (data.vehicles || []).filter(v => v.is_emergency || v.label === "ambulance" || v.label === "police" || v.label === "fire_engine");
      if (emergencyVehicles.length > 0) {
        emergencyEl.textContent = `🚨 ${emergencyVehicles[0].label.toUpperCase()} DETECTED`;
        emergencyEl.style.color = "var(--accent-red, #ff4444)";
        if (emergencySubEl) emergencySubEl.textContent = "PRIORITY ALERT ACTIVE";
      } else {
        emergencyEl.textContent = "Normal (Clear)";
        emergencyEl.style.color = "var(--accent-emerald, #00e676)";
        if (emergencySubEl) emergencySubEl.textContent = "Priority Siren / Strobe Monitor";
      }
    }

    if (plateTextEl) {
      const plates = data.plates || [];
      if (plates.length > 0) {
        const topPlate = plates[0];
        const clean = topPlate.clean || topPlate.plate_clean || topPlate.formatted || "";
        const formatted = topPlate.formatted || topPlate.plate_formatted || clean;
        plateTextEl.textContent = formatted || "PLATE DETECTED";
        const topConf = Math.round((topPlate.confidence || 0) * 100);
        plateConfEl.textContent = `Confidence: ${topConf}% • ANPR OCR`;
      } else {
        plateTextEl.textContent = "-";
        plateConfEl.textContent = "Optical Character Recognition";
      }
    }

    // Live FPS display update
    const fpsTagEl = document.getElementById("streamFpsTag");
    if (fpsTagEl) {
      const liveFps = (typeof data.fps === "number" && data.fps > 0) ? data.fps.toFixed(1) : "25.0";
      fpsTagEl.textContent = `${liveFps} FPS • RK3588 NPU`;
    }

    if (data.mode && data.mode !== currentVisionMode) {
      updateModeButtons(data.mode);
    }
  } catch (err) {}
}



function reloadStream() {
  const img = document.getElementById("visionLiveFeed");
  const placeholder = document.getElementById("streamPlaceholder");
  if (!img) return;
  img.style.display = "block";
  if (placeholder) placeholder.style.display = "none";
  img.src = "/api/vision/stream?t=" + Date.now();
}

function handleStreamError(img) {
  img.style.display = "none";
  const placeholder = document.getElementById("streamPlaceholder");
  if (placeholder) placeholder.style.display = "flex";
  const tag = document.getElementById("streamTagLive");
  if (tag) tag.className = "stream-tag";
  const text = document.getElementById("streamStatusText");
  if (text) text.textContent = "STREAM OFFLINE";
}

function handleStreamLoaded(img) {
  img.style.display = "block";
  const placeholder = document.getElementById("streamPlaceholder");
  if (placeholder) placeholder.style.display = "none";
  const tag = document.getElementById("streamTagLive");
  if (tag) tag.className = "stream-tag live";
  const text = document.getElementById("streamStatusText");
  if (text) text.textContent = "STREAM ACTIVE";
}

async function captureSnapshot() {
  try {
    const res = await fetch("/api/vision/snapshot", { cache: "no-store" });
    if (!res.ok) {
      showMsg("cameraMessage", "Could not capture snapshot. Is the camera pipeline running?", true);
      return;
    }
    const blob = await res.blob();
    const url = URL.createObjectURL(blob);
    openLightbox(url, "Live Capture Snapshot", "Captured from active preview frame");
  } catch (err) {
    showMsg("cameraMessage", "Network error requesting snapshot", true);
  }
}

async function cameraOn() {
  try {
    const res = await fetch("/api/camera/on", { method: "POST" });
    const data = await res.json();
    showMsg("cameraMessage", data.message || "Camera turned ON", !data.ok);
    loadCameraStatus();
    setTimeout(reloadStream, 600);
  } catch (err) {
    showMsg("cameraMessage", "Failed to start camera", true);
  }
}

async function cameraOff() {
  try {
    const res = await fetch("/api/camera/off", { method: "POST" });
    const data = await res.json();
    showMsg("cameraMessage", data.message || "Camera turned OFF", !data.ok);
    loadCameraStatus();
    handleStreamError(document.getElementById("visionLiveFeed"));
  } catch (err) {
    showMsg("cameraMessage", "Failed to stop camera", true);
  }
}

// ── IP Camera / Stream Source Configuration ────────────────────────────────
async function loadCameraConfig() {
  try {
    const res = await fetch("/api/camera/config", { cache: "no-store" });
    if (!res.ok) return;
    const data = await res.json();
    const input = document.getElementById("cameraSourceInput");
    const badge = document.getElementById("camSourceTypeBadge");
    if (input && data.source) {
      input.value = data.source;
    }
    if (badge) {
      badge.textContent = data.type === "rtsp" ? "RTSP STREAM" : "LOCAL V4L2";
      badge.className = data.type === "rtsp" ? "badge badge-plate" : "badge badge-emerald";
    }
  } catch (err) {
    console.error("Failed to load camera config:", err);
  }
}

async function saveCameraSource() {
  const input = document.getElementById("cameraSourceInput");
  if (!input || !input.value.trim()) {
    showMsg("cameraMessage", "Please enter a valid RTSP stream URL or device index", true);
    return;
  }
  const source = input.value.trim();
  try {
    showMsg("cameraMessage", "Applying new camera stream source...");
    const res = await fetch("/api/camera/config", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ source: source, restart: true })
    });
    const data = await res.json();
    if (data.ok) {
      showMsg("cameraMessage", "Camera source updated: " + source, false);
      loadCameraConfig();
      loadCameraStatus();
      setTimeout(reloadStream, 1000);
    } else {
      showMsg("cameraMessage", data.error || "Failed to update camera source", true);
    }
  } catch (err) {
    showMsg("cameraMessage", "Network error updating camera source", true);
  }
}

function setPresetCameraSource(presetUrl) {
  const input = document.getElementById("cameraSourceInput");
  if (input) {
    input.value = presetUrl;
    saveCameraSource();
  }
}

function toggleCameraSourceBox() {
  const box = document.getElementById("cameraSourceBox");
  if (box) {
    box.style.display = (box.style.display === "none") ? "flex" : "none";
  }
}

// ── Offline Database Sessions & Events ─────────────────────────────────────
async function loadOfflineStorage() {
  try {
    // 1. Sessions
    const sessionsRes = await fetch("/api/sessions?limit=15", { cache: "no-store" });
    if (sessionsRes.ok) {
      const sessions = await sessionsRes.json();
      const tbody = document.getElementById("sessionsTableBody");
      if (tbody && Array.isArray(sessions)) {
        if (sessions.length === 0) {
          tbody.innerHTML = "<tr><td colspan='5' style='padding: 12px; text-align: center; color: var(--text-muted);'>No recorded sessions in offline database.</td></tr>";
        } else {
          tbody.innerHTML = "";
          sessions.forEach(s => {
            const tr = document.createElement("tr");
            const isDone = s.upload_status === "done";
            const isFailed = s.upload_status === "failed";
            const statusClass = isDone ? "text-emerald" : (isFailed ? "text-rose" : "text-amber");

            tr.innerHTML = `
              <td style="font-family: var(--font-mono); font-size: 0.8rem;">${escapeHtml(s.session_id)}</td>
              <td style="color: var(--text-muted);">${escapeHtml(s.start_time ? s.start_time.replace('T', ' ') : '-')}</td>
              <td>${s.duration_sec ? s.duration_sec.toFixed(1) + 's' : '-'}</td>
              <td><span class="${statusClass}" style="font-weight: 600;">${escapeHtml(s.upload_status || 'pending')}</span></td>
              <td style="text-align: right;">
                ${!isDone ? `<button class="btn btn-secondary btn-sm" onclick="retrySessionUpload('${escapeHtml(s.session_id)}')">Retry Upload</button>` : `<span style="color: var(--text-muted); font-size: 0.8rem;">Synced ✓</span>`}
              </td>
            `;
            tbody.appendChild(tr);
          });
        }
      }
    }

    // 2. Detection Events for Live Sentinel feed
    const eventsRes = await fetch("/api/events/detections?limit=10", { cache: "no-store" });
    if (eventsRes.ok) {
      const events = await eventsRes.json();
      const tbody = document.getElementById("quickEventsBody");
      if (tbody && Array.isArray(events)) {
        if (events.length === 0) {
          tbody.innerHTML = "<tr><td colspan='4' style='padding: 12px; text-align: center; color: var(--text-muted);'>No detections recorded yet.</td></tr>";
        } else {
          tbody.innerHTML = "";
          events.forEach(e => {
            const tr = document.createElement("tr");
            let detail = "";
            let snapUrl = "";
            const tsSec = Math.floor(e.timestamp || 0);

            if (e.event_type === "PLATE_DETECTED") {
              const clean = e.plate_clean || "";
              const formatted = e.plate_formatted || clean || "-";
              detail = `<strong style="color: var(--accent-cyan);">${escapeHtml(formatted)}</strong>`;
              if (clean && tsSec > 0) {
                snapUrl = `/api/plates/snapshot/plate_${encodeURIComponent(clean)}_${tsSec}.jpg`;
              }
            } else if (e.event_type === "PERSON_DETECTED") {
              detail = `Person Detected`;
              if (tsSec > 0) {
                snapUrl = `/api/persons/snapshot/person_${tsSec}.jpg`;
              }
            } else {
              detail = escapeHtml(e.event_type);
            }

            const timeStr = e.created_at ? e.created_at.split('T')[1] || e.created_at : '-';

            tr.innerHTML = `
              <td style="font-family: var(--font-mono); font-size: 0.78rem; color: var(--text-muted);">${escapeHtml(timeStr)}</td>
              <td><span class="badge ${e.event_type === 'PLATE_DETECTED' ? 'badge-plate' : 'badge-person'}">${escapeHtml(e.event_type)}</span></td>
              <td>${detail}</td>
              <td>
                ${snapUrl ? `<button class="btn btn-secondary btn-sm" style="padding: 2px 8px; font-size: 0.72rem;" onclick="openLightbox('${escapeHtml(snapUrl)}', '${escapeHtml(detail.replace(/<[^>]*>/g, ''))}', '${escapeHtml(timeStr)}')">Snap 📷</button>` : `<span style="color:var(--text-muted);">-</span>`}
              </td>
            `;
            tbody.appendChild(tr);
          });
        }
      }
    }
  } catch (err) {}
}

async function retrySessionUpload(sessionId) {
  try {
    await fetch(`/api/sessions/${encodeURIComponent(sessionId)}/retry`, { method: "POST" });
    loadOfflineStorage();
  } catch (err) {}
}

// ── Historical Telemetry Log Table ─────────────────────────────────────────
async function loadTelemetryLog() {
  try {
    const res = await fetch("/api/telemetry?n=15", { cache: "no-store" });
    if (!res.ok) return;
    const list = await res.json();
    const tbody = document.getElementById("telemetryTableBody");
    if (!tbody || !Array.isArray(list)) return;

    if (list.length === 0) {
      tbody.innerHTML = "<tr><td colspan='7' style='padding: 12px; text-align: center; color: var(--text-muted);'>No telemetry records logged yet.</td></tr>";
      return;
    }

    tbody.innerHTML = "";
    list.forEach(r => {
      const tr = document.createElement("tr");
      const timeStr = r.timestamp ? r.timestamp.replace('T', ' ') : '-';
      tr.innerHTML = `
        <td style="font-family: var(--font-mono); font-size: 0.78rem; color: var(--text-muted);">${escapeHtml(timeStr)}</td>
        <td class="font-mono">${r.cpu_temp_c != null ? r.cpu_temp_c.toFixed(1) + '°C' : '-'}</td>
        <td class="font-mono" style="color: var(--accent-cyan);">${r.npu_temp_c != null ? r.npu_temp_c.toFixed(1) + '°C' : '-'}</td>
        <td class="font-mono">${r.gpu_temp_c != null ? r.gpu_temp_c.toFixed(1) + '°C' : '-'}</td>
        <td class="font-mono">${r.ram_used_mb} / ${r.ram_total_mb} MB</td>
        <td class="font-mono">${r.disk_used_gb != null ? r.disk_used_gb.toFixed(1) + ' GB' : '-'}</td>
        <td><span class="status-pill ${r.camera_active ? 'status-pill-online' : 'status-pill-offline'}" style="font-size:0.68rem;">${r.camera_active ? 'ACTIVE' : 'OFF'}</span></td>
      `;
      tbody.appendChild(tr);
    });
  } catch (err) {}
}

// ── Access Control & GPIO Header ───────────────────────────────────────────
async function loadGpioPins() {
  try {
    const res = await fetch("/api/gpio/pins", { cache: "no-store" });
    if (!res.ok) return;
    const data = await res.json();
    const tbody = document.getElementById("gpioTableBody");
    if (!tbody || !data.pins) return;

    tbody.innerHTML = "";
    data.pins.forEach(pin => {
      const tr = document.createElement("tr");
      const isHigh = pin.level === 1;
      const isOut  = pin.direction === "out";

      tr.innerHTML = `
        <td class="font-mono" style="font-weight: 700; color: #fff;">Pin ${pin.header_pin}</td>
        <td class="font-mono" style="color: var(--text-muted);">${pin.kernel_gpio != null ? pin.kernel_gpio : '-'}</td>
        <td><strong style="color: var(--accent-cyan);">${escapeHtml(pin.role || pin.alias || "-")}</strong></td>
        <td><span class="badge" style="background: rgba(255,255,255,0.06);">${escapeHtml(pin.direction || 'in')}</span></td>
        <td>
          <span class="status-pill ${isHigh ? 'status-pill-online' : 'status-pill-offline'}" style="font-size: 0.72rem;">
            ${isHigh ? 'HIGH (1)' : 'LOW (0)'}
          </span>
        </td>
        <td style="text-align: right;">
          ${isOut ? `
            <button class="btn btn-secondary btn-sm" onclick="setPinLevel(${pin.header_pin}, ${isHigh ? 0 : 1})">
              ${isHigh ? 'Set LOW' : 'Set HIGH'}
            </button>
          ` : `<span style="color: var(--text-muted); font-size: 0.75rem;">Input Only</span>`}
        </td>
      `;
      tbody.appendChild(tr);
    });
  } catch (err) {}
}

async function setPinLevel(pin, level) {
  try {
    const res = await fetch("/api/gpio/pin/level", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ pin: pin, level: level })
    });
    const data = await res.json();
    showMsg("gpioMessage", data.message || `Pin ${pin} set to ${level}`);
    loadGpioPins();
  } catch (err) {
    showMsg("gpioMessage", "Error changing pin state", true);
  }
}

async function togglePin(alias) {
  try {
    const res = await fetch("/api/gpio/pin/toggle", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ alias: alias })
    });
    const data = await res.json();
    showMsg("gpioMessage", data.message || `Toggled ${alias}`);
    loadGpioPins();
  } catch (err) {
    showMsg("gpioMessage", `Error toggling ${alias}`, true);
  }
}

async function triggerBuzzer(beeps = 2, durationMs = 100) {
  try {
    const res = await fetch("/api/gpio/buzzer", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ beeps: beeps, duration_ms: durationMs })
    });
    const data = await res.json();
    showMsg("gpioMessage", data.message || "Buzzer beep triggered!");
  } catch (err) {
    showMsg("gpioMessage", "Failed to trigger buzzer", true);
  }
}

// ── Wi-Fi Management ───────────────────────────────────────────────────────
async function loadWifiStatus() {
  try {
    const res = await fetch("/api/wifi/status", { cache: "no-store" });
    if (!res.ok) return;
    const data = await res.json();
    const statusText = document.getElementById("wifiStatusText");
    const signalBadge = document.getElementById("wifiSignalBadge");

    if (data.connected && data.ssid) {
      statusText.textContent = data.ssid;
      signalBadge.textContent = `${data.signal_strength}% Signal`;
      signalBadge.style.display = "inline-flex";
    } else {
      statusText.textContent = "Not Connected";
      signalBadge.style.display = "none";
    }
  } catch (err) {}
}

async function scanWifi() {
  const select = document.getElementById("wifiSelect");
  const scanBtn = document.getElementById("btnScanWifi");
  scanBtn.disabled = true;
  select.innerHTML = "<option>Scanning available Wi-Fi networks...</option>";
  showMsg("wifiMessage", "Scanning for 2.4GHz & 5GHz Wi-Fi networks...");

  try {
    const res = await fetch("/api/wifi/scan", { cache: "no-store" });
    const data = await res.json();
    select.innerHTML = '<option value="">-- Select a Network --</option><option value="__manual__">[Enter SSID Manually]</option>';

    if (data.networks && data.networks.length > 0) {
      data.networks.forEach(net => {
        const opt = document.createElement("option");
        opt.value = net.ssid;
        opt.textContent = `${net.ssid} (${net.signal_strength}% Signal • ${net.security})`;
        select.appendChild(opt);
      });
      showMsg("wifiMessage", `Found ${data.networks.length} Wi-Fi networks!`);
    } else {
      showMsg("wifiMessage", "No networks discovered. Try moving closer to router.", true);
    }
  } catch (err) {
    showMsg("wifiMessage", "Error scanning Wi-Fi", true);
  } finally {
    scanBtn.disabled = false;
  }
}

function onWifiSelectChange() {
  const select = document.getElementById("wifiSelect");
  const manualGroup = document.getElementById("wifiManualGroup");
  manualGroup.style.display = (select.value === "__manual__") ? "flex" : "none";
}

async function connectWifi() {
  const select = document.getElementById("wifiSelect");
  const manual = document.getElementById("wifiManualSSID");
  const pass = document.getElementById("wifiPassword").value;

  let ssid = select.value === "__manual__" ? manual.value.trim() : select.value;
  if (!ssid) {
    showMsg("wifiMessage", "Please choose or type a Wi-Fi network name", true);
    return;
  }

  showMsg("wifiMessage", `Connecting to '${ssid}'...`);
  try {
    const res = await fetch("/api/wifi/connect", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ ssid: ssid, password: pass })
    });
    const data = await res.json();
    if (data.ok) {
      showMsg("wifiMessage", `Connected to ${ssid}!`);
      loadWifiStatus();
      loadDeviceStatus();
    } else {
      showMsg("wifiMessage", `Connection failed: ${data.message || "Incorrect password or unreachable network"}`, true);
    }
  } catch (err) {
    showMsg("wifiMessage", "Failed to send connect command", true);
  }
}

async function disconnectWifi() {
  try {
    const res = await fetch("/api/wifi/disconnect", { method: "POST" });
    const data = await res.json();
    showMsg("wifiMessage", data.ok ? "Wi-Fi disconnected." : "Disconnect failed", !data.ok);
    loadWifiStatus();
  } catch (err) {
    showMsg("wifiMessage", "Error disconnecting", true);
  }
}

// ── Bluetooth Management ───────────────────────────────────────────────────
async function loadBluetoothStatus() {
  try {
    const res = await fetch("/api/bluetooth/status", { cache: "no-store" });
    if (!res.ok) return;
    const data = await res.json();

    const pill = document.getElementById("btStatusText");
    const adapterEl = document.getElementById("btAdapterName");
    const btnOn  = document.getElementById("btnBtOn");
    const btnOff = document.getElementById("btnBtOff");

    if (data.enabled) {
      pill.textContent = "Enabled";
      pill.className = "status-pill status-pill-online";
    } else {
      pill.textContent = "Disabled";
      pill.className = "status-pill status-pill-offline";
    }

    if (data.adapter_name && adapterEl) adapterEl.textContent = data.adapter_name;
    if (btnOn)  btnOn.disabled = data.enabled;
    if (btnOff) btnOff.disabled = !data.enabled;
  } catch (err) {}
}

async function setBluetoothPower(enable) {
  try {
    const res = await fetch("/api/bluetooth/power", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ enabled: enable })
    });
    const data = await res.json();
    showMsg("btMessage", `Bluetooth ${enable ? "Enabled" : "Disabled"}`);
    loadBluetoothStatus();
  } catch (err) {
    showMsg("btMessage", "Error toggling Bluetooth", true);
  }
}

async function scanBluetooth() {
  const container = document.getElementById("btDeviceList");
  container.innerHTML = "<p style='color:var(--text-muted); font-size:0.8rem;'>Scanning for Bluetooth & BLE devices (3 seconds)...</p>";

  try {
    const res = await fetch("/api/bluetooth/scan", { cache: "no-store" });
    const data = await res.json();
    container.innerHTML = "";

    if (data.devices && data.devices.length > 0) {
      data.devices.forEach(dev => {
        const item = document.createElement("div");
        item.style.display = "flex";
        item.style.justifyContent = "space-between";
        item.style.alignItems = "center";
        item.style.padding = "6px 0";
        item.style.borderBottom = "1px solid rgba(255,255,255,0.04)";
        item.innerHTML = `
          <div>
            <div style="font-weight:600; color:#fff; font-size:0.85rem;">${escapeHtml(dev.name)}</div>
            <div class="font-mono" style="font-size:0.75rem; color:var(--text-muted);">${escapeHtml(dev.address)}</div>
          </div>
          <button class="btn btn-secondary btn-sm" onclick="pairBluetooth('${escapeHtml(dev.address)}')">Pair</button>
        `;
        container.appendChild(item);
      });
      showMsg("btMessage", `Found ${data.devices.length} Bluetooth devices!`);
    } else {
      container.innerHTML = "<p style='color:var(--text-muted); font-size:0.8rem;'>No Bluetooth devices discovered nearby.</p>";
    }
  } catch (err) {
    showMsg("btMessage", "Error scanning Bluetooth", true);
  }
}

async function pairBluetooth(address) {
  showMsg("btMessage", `Pairing with ${address}...`);
  try {
    const res = await fetch("/api/bluetooth/pair", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ address: address })
    });
    const data = await res.json();
    showMsg("btMessage", data.ok ? `Paired successfully with ${address}!` : `Pairing failed: ${data.message}`, !data.ok);
    loadBluetoothStatus();
  } catch (err) {
    showMsg("btMessage", "Failed to pair", true);
  }
}

// ── Ethernet Management ────────────────────────────────────────────────────
let ethFieldsInitialized = false;

async function loadEthernetStatus() {
  try {
    const res = await fetch("/api/ethernet/status", { cache: "no-store" });
    if (!res.ok) return;
    const data = await res.json();

    const pill = document.getElementById("ethStatusPill");
    const ifaceEl = document.getElementById("ethInterface");
    const ipEl = document.getElementById("ethIpAddress");

    if (ipEl) ipEl.textContent = data.ip_address || "-";
    if (ifaceEl) ifaceEl.textContent = `Interface: ${data.interface_name || '-'} • Link: ${data.connected ? data.speed_mbps + ' Mbps' : 'Disconnected'}`;

    if (pill) {
      if (data.connected) {
        pill.textContent = "Cable Connected";
        pill.className = "status-pill status-pill-online";
      } else {
        pill.textContent = "Cable Disconnected";
        pill.className = "status-pill status-pill-offline";
      }
    }

    if (!ethFieldsInitialized) {
      const modeSelect = document.getElementById("ethModeSelect");
      const staticIp = document.getElementById("ethStaticIp");
      const staticPrefix = document.getElementById("ethStaticPrefix");
      const staticGw = document.getElementById("ethStaticGateway");
      const staticDns = document.getElementById("ethStaticDns");

      if (modeSelect && data.mode) modeSelect.value = data.mode;
      if (staticIp && data.ip_address && data.ip_address !== "-") staticIp.value = data.ip_address;
      if (staticPrefix && data.prefix_cidr) staticPrefix.value = data.prefix_cidr;
      if (staticGw && data.gateway && data.gateway !== "-") staticGw.value = data.gateway;
      if (staticDns && data.dns_servers && data.dns_servers.length > 0) staticDns.value = data.dns_servers.join(", ");

      onEthModeChange();
      ethFieldsInitialized = true;
    }
  } catch (err) {}
}

function onEthModeChange() {
  const modeSelect = document.getElementById("ethModeSelect");
  const staticFields = document.getElementById("ethStaticFields");
  if (modeSelect && staticFields) {
    staticFields.style.display = (modeSelect.value === "static") ? "flex" : "none";
  }
}

async function applyEthernetConfig() {
  const mode = document.getElementById("ethModeSelect").value;
  const ip = document.getElementById("ethStaticIp").value.trim();
  const prefix = parseInt(document.getElementById("ethStaticPrefix").value) || 24;
  const gateway = document.getElementById("ethStaticGateway").value.trim();
  const dnsRaw = document.getElementById("ethStaticDns").value.trim();
  const dnsServers = dnsRaw ? dnsRaw.split(",").map(s => s.trim()).filter(Boolean) : [];

  const body = { mode: mode };
  if (mode === "static") {
    if (!ip) {
      showMsg("ethMessage", "Please enter a valid static IPv4 address", true);
      return;
    }
    body.ip_address = ip;
    body.prefix_cidr = prefix;
    body.gateway = gateway;
    body.dns_servers = dnsServers;
  }

  showMsg("ethMessage", "Applying Ethernet settings...");
  try {
    const res = await fetch("/api/ethernet/config", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(body)
    });
    const data = await res.json();
    if (data.ok) {
      showMsg("ethMessage", "Ethernet configuration applied successfully!");
      loadEthernetStatus();
      loadDeviceStatus();
    } else {
      showMsg("ethMessage", `Failed: ${data.message || "Unknown error"}`, true);
    }
  } catch (err) {
    showMsg("ethMessage", "Network error updating Ethernet", true);
  }
}

// ── Device & Clock Status ──────────────────────────────────────────────────
let tzInitialized = false;

async function loadDeviceStatus() {
  try {
    const res = await fetch("/api/status", { cache: "no-store" });
    if (!res.ok) return;
    const data = await res.json();

    const hostSide = document.getElementById("sidebarHost");
    const ipSide   = document.getElementById("sidebarIp");
    const timeEl   = document.getElementById("systemTime");

    if (hostSide && data.hostname) hostSide.textContent = data.hostname;
    if (ipSide && data.ip_address) ipSide.textContent = data.ip_address;
    if (timeEl && data.system_time) timeEl.textContent = data.system_time;

    if (!tzInitialized && data.timezone) {
      const tzSelect = document.getElementById("timezoneSelect");
      if (tzSelect) tzSelect.value = data.timezone;
      tzInitialized = true;
    }
  } catch (err) {}
}

async function saveTimezone() {
  const select = document.getElementById("timezoneSelect");
  const tz = select.value;
  const btn = document.getElementById("btnSaveTz");

  btn.disabled = true;
  showMsg("timeMessage", `Configuring timezone to '${tz}'...`);

  try {
    const res = await fetch("/api/system/timezone", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ timezone: tz })
    });
    const data = await res.json();
    if (data.ok) {
      showMsg("timeMessage", `Timezone updated to ${tz}! RTC updated.`);
      loadDeviceStatus();
    } else {
      showMsg("timeMessage", `Error updating timezone: ${data.message || "Failed"}`, true);
    }
  } catch (err) {
    showMsg("timeMessage", "Network error updating timezone", true);
  } finally {
    btn.disabled = false;
  }
}

async function syncNtpTime() {
  const btn = document.getElementById("btnSyncNtp");
  btn.disabled = true;
  showMsg("timeMessage", "Synchronizing system clock with NTP pool...");

  try {
    const res = await fetch("/api/system/time/sync", { method: "POST" });
    const data = await res.json();
    if (data.ok) {
      showMsg("timeMessage", "NTP time synchronized & backed to RTC battery!");
      loadDeviceStatus();
    } else {
      showMsg("timeMessage", "NTP sync failed. Check internet connection.", true);
    }
  } catch (err) {
    showMsg("timeMessage", "Error triggering NTP sync", true);
  } finally {
    btn.disabled = false;
  }
}

// ── AI Models & NPU Load Lab ───────────────────────────────────────────────
let isLoadTestActive = false;

async function loadModelsCatalog() {
  try {
    const res = await fetch("/api/models/list");
    if (!res.ok) return;
    const data = await res.json();
    const tbody = document.getElementById("modelsTableBody");
    if (!tbody || !data.models) return;

    if (data.models.length === 0) {
      tbody.innerHTML = `<tr><td colspan="7" style="text-align:center; padding:12px; color:var(--text-muted);">No models discovered.</td></tr>`;
      return;
    }

    tbody.innerHTML = data.models.map(m => `
      <tr>
        <td><strong>${escapeHtml(m.name)}</strong></td>
        <td><span class="badge badge-accent">${escapeHtml(m.task)}</span></td>
        <td class="font-mono" style="color:var(--text-secondary); font-size:0.8rem;">${escapeHtml(m.architecture)}</td>
        <td class="font-mono" style="color:var(--accent-cyan); font-size:0.8rem;">${escapeHtml(m.input_shape)}</td>
        <td class="font-mono" style="color:var(--accent-emerald); font-size:0.85rem;">${m.default_latency_ms} ms</td>
        <td><span class="badge badge-plate">${escapeHtml(m.target_core)}</span></td>
        <td><span class="badge" style="background:rgba(16,185,129,0.15); color:var(--accent-emerald);">Installed</span></td>
      </tr>
    `).join("");
  } catch (err) {
    console.error("Error loading model catalog:", err);
  }
}

async function startLoadTest() {
  const m1 = document.getElementById("selModelCore0")?.value || "yolov8_vehicle";
  const m2 = document.getElementById("selModelCore1")?.value || "vehicle_color_net";
  const m3 = document.getElementById("selModelCore2")?.value || "yolov8n_pose";
  const duration = parseInt(document.getElementById("selDurationSec")?.value || "60", 10);
  const concurrency = parseInt(document.getElementById("selConcurrency")?.value || "3", 10);
  const target_fps = parseInt(document.getElementById("selTargetFps")?.value || "60", 10);

  const btnStart = document.getElementById("btnStartLoadTest");
  const btnStop = document.getElementById("btnStopLoadTest");
  if (btnStart) btnStart.disabled = true;

  showMsg("loadTestMsg", "Initializing multi-core NPU load test...");

  try {
    const res = await fetch("/api/loadtest/start", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({
        model_core0: m1,
        model_core1: m2,
        model_core2: m3,
        duration_sec: duration,
        concurrency: concurrency,
        target_fps: target_fps
      })
    });
    const data = await res.json();
    if (data.ok) {
      isLoadTestActive = true;
      if (btnStop) btnStop.disabled = false;
      showMsg("loadTestMsg", "NPU stress test running across 3 cores!");
      updateLoadTestUI(data.status);
    } else {
      showMsg("loadTestMsg", data.message || "Failed to start test", true);
      if (btnStart) btnStart.disabled = false;
    }
  } catch (err) {
    showMsg("loadTestMsg", "Network error starting load test", true);
    if (btnStart) btnStart.disabled = false;
  }
}

async function stopLoadTest() {
  const btnStop = document.getElementById("btnStopLoadTest");
  if (btnStop) btnStop.disabled = true;

  try {
    const res = await fetch("/api/loadtest/stop", { method: "POST" });
    const data = await res.json();
    isLoadTestActive = false;
    showMsg("loadTestMsg", "Load test stopped.");
    const btnStart = document.getElementById("btnStartLoadTest");
    if (btnStart) btnStart.disabled = false;
    updateLoadTestUI(data.status);
  } catch (err) {
    showMsg("loadTestMsg", "Error stopping load test", true);
  }
}

async function updateLoadTestStatus() {
  try {
    const res = await fetch("/api/loadtest/status");
    if (!res.ok) return;
    const st = await res.json();
    updateLoadTestUI(st);
  } catch (err) {
    console.error("Error polling load test status:", err);
  }
}

function updateLoadTestUI(st) {
  if (!st) return;

  isLoadTestActive = !!st.running;

  // Buttons & badges
  const btnStart = document.getElementById("btnStartLoadTest");
  const btnStop = document.getElementById("btnStopLoadTest");
  const badge = document.getElementById("loadTestBadge");
  const sidebarStress = document.getElementById("sidebarNpuStressBadge");

  if (btnStart) btnStart.disabled = isLoadTestActive;
  if (btnStop) btnStop.disabled = !isLoadTestActive;

  if (badge) {
    if (isLoadTestActive) {
      badge.textContent = "STRESSING NPU (3 CORES)";
      badge.style.background = "rgba(244,63,94,0.2)";
      badge.style.color = "var(--accent-rose)";
      badge.style.border = "1px solid rgba(244,63,94,0.4)";
    } else {
      badge.textContent = "IDLE";
      badge.style.background = "rgba(255,255,255,0.06)";
      badge.style.color = "#fff";
      badge.style.border = "none";
    }
  }

  if (sidebarStress) {
    sidebarStress.style.display = isLoadTestActive ? "inline-block" : "none";
  }

  // Radial Meters
  const c0 = st.npu?.core0_load_pct || 0;
  const c1 = st.npu?.core1_load_pct || 0;
  const c2 = st.npu?.core2_load_pct || 0;

  const circle0 = document.getElementById("npuCore0Circle");
  const val0 = document.getElementById("npuCore0Val");
  if (circle0) circle0.setAttribute("stroke-dasharray", `${Math.min(100, Math.round(c0))}, 100`);
  if (val0) val0.textContent = `${Math.round(c0)}%`;

  const circle1 = document.getElementById("npuCore1Circle");
  const val1 = document.getElementById("npuCore1Val");
  if (circle1) circle1.setAttribute("stroke-dasharray", `${Math.min(100, Math.round(c1))}, 100`);
  if (val1) val1.textContent = `${Math.round(c1)}%`;

  const circle2 = document.getElementById("npuCore2Circle");
  const val2 = document.getElementById("npuCore2Val");
  if (circle2) circle2.setAttribute("stroke-dasharray", `${Math.min(100, Math.round(c2))}, 100`);
  if (val2) val2.textContent = `${Math.round(c2)}%`;

  // Freq & Temp
  const freqBadge = document.getElementById("npuFreqBadge");
  if (freqBadge && st.npu?.frequency_mhz) {
    freqBadge.textContent = `${st.npu.frequency_mhz} MHz Turbo`;
  }
  const tempBadge = document.getElementById("npuTempBadge");
  if (tempBadge && st.npu_temp_c != null) {
    tempBadge.textContent = `${st.npu_temp_c.toFixed(1)}°C`;
  }

  // Performance metrics
  const tpEl = document.getElementById("benchThroughputVal");
  if (tpEl) tpEl.textContent = `${(st.current_fps || 0).toFixed(1)} FPS`;

  const latEl = document.getElementById("benchNpuLatencyVal");
  if (latEl) latEl.textContent = `${(st.latency_npu_ms || 0).toFixed(1)} ms`;

  const infEl = document.getElementById("benchTotalInferencesVal");
  if (infEl) infEl.textContent = (st.total_inferences || 0).toLocaleString();

  const dropEl = document.getElementById("benchDroppedVal");
  if (dropEl) dropEl.textContent = `${st.dropped_frames || 0} (0.0%)`;

  // Progress box
  const progBox = document.getElementById("loadTestProgressBox");
  if (progBox) {
    if (isLoadTestActive && st.duration_sec > 0) {
      progBox.style.display = "block";
      const pct = Math.min(100, Math.round((st.elapsed_sec / st.duration_sec) * 100));
      const progBar = document.getElementById("loadTestProgressBar");
      if (progBar) progBar.style.width = `${pct}%`;
      const elText = document.getElementById("loadTestElapsedText");
      if (elText) elText.textContent = `${st.elapsed_sec}s / ${st.duration_sec}s (${pct}%)`;
      const remText = document.getElementById("loadTestRemainingText");
      if (remText) remText.textContent = `${Math.max(0, st.duration_sec - st.elapsed_sec)}s remaining`;
    } else {
      progBox.style.display = "none";
    }
  }
}

function exportLoadTestReport() {
  window.open("/api/loadtest/export", "_blank");
}

// ── FOTA Management Functions ─────────────────────────────────────────────
async function loadFotaStatus() {
  try {
    const res = await fetch("/api/fota/status");
    if (!res.ok) return;
    const data = await res.json();
    
    const verEl = document.getElementById("fotaCurrentVer");
    if (verEl && data.current_version) {
      verEl.textContent = data.current_version.startsWith("v") ? data.current_version : `v${data.current_version}`;
    }

    const pillEl = document.getElementById("fotaStatusPill");
    if (pillEl) {
      if (data.is_updating) {
        pillEl.textContent = "UPDATING...";
        pillEl.className = "status-pill status-pill-busy font-mono";
      } else {
        pillEl.textContent = "IDLE / READY";
        pillEl.className = "status-pill status-pill-online font-mono";
      }
    }

    const infoEl = document.getElementById("fotaDeviceInfo");
    if (infoEl) {
      infoEl.textContent = `Device: ${data.device_id || "Auto"} • S3: ${data.s3_bucket || "ready"}`;
    }
  } catch (e) {
    console.debug("Failed to fetch FOTA status", e);
  }
}

async function uploadFotaPackage() {
  const fileInput = document.getElementById("fotaFileInput");
  const msgEl = document.getElementById("fotaMessage");
  const progContainer = document.getElementById("fotaProgressContainer");
  const progBar = document.getElementById("fotaProgressBar");
  const progStep = document.getElementById("fotaProgressStep");
  const progPct = document.getElementById("fotaProgressPercent");
  const btnUpload = document.getElementById("btnUploadFota");
  const rebootCheck = document.getElementById("fotaRebootCheckbox");

  if (!fileInput || !fileInput.files || fileInput.files.length === 0) {
    if (msgEl) {
      msgEl.textContent = "⚠️ Please select a firmware release (.zip / .tar.gz) package to upload.";
      msgEl.className = "feedback-msg error";
    }
    return;
  }

  const file = fileInput.files[0];
  const formData = new FormData();
  formData.append("file", file);

  const rebootVal = rebootCheck && rebootCheck.checked ? "1" : "0";
  const uploadUrl = `/api/fota/upload?reboot=${rebootVal}&version=${encodeURIComponent(file.name.replace(/\.[^/.]+$/, ""))}`;

  if (btnUpload) btnUpload.disabled = true;
  if (progContainer) progContainer.style.display = "block";
  if (progBar) progBar.style.width = "10%";
  if (progPct) progPct.textContent = "10%";
  if (progStep) progStep.textContent = `Uploading ${file.name} (${(file.size / (1024 * 1024)).toFixed(1)} MB)...`;
  if (msgEl) {
    msgEl.textContent = "Uploading package to gateway and executing automated flash...";
    msgEl.className = "feedback-msg info";
  }

  try {
    const xhr = new XMLHttpRequest();
    xhr.open("POST", uploadUrl, true);

    xhr.upload.onprogress = (e) => {
      if (e.lengthComputable) {
        const pct = Math.round((e.loaded / e.total) * 75);
        if (progBar) progBar.style.width = `${pct}%`;
        if (progPct) progPct.textContent = `${pct}%`;
      }
    };

    xhr.onload = () => {
      if (btnUpload) btnUpload.disabled = false;
      if (xhr.status >= 200 && xhr.status < 300) {
        if (progBar) progBar.style.width = "100%";
        if (progPct) progPct.textContent = "100%";
        if (progStep) progStep.textContent = "Installation running & service restarting...";
        if (msgEl) {
          msgEl.textContent = `✅ Firmware package uploaded successfully! Installing release. ${rebootCheck?.checked ? "System will reboot in a few seconds..." : ""}`;
          msgEl.className = "feedback-msg success";
        }
        setTimeout(() => { loadFotaStatus(); }, 3000);
      } else {
        if (msgEl) {
          msgEl.textContent = `❌ Upload failed (HTTP ${xhr.status}): ${xhr.responseText}`;
          msgEl.className = "feedback-msg error";
        }
      }
    };

    xhr.onerror = () => {
      if (btnUpload) btnUpload.disabled = false;
      if (msgEl) {
        msgEl.textContent = "❌ Network error during firmware upload.";
        msgEl.className = "feedback-msg error";
      }
    };

    xhr.send(formData);
  } catch (err) {
    if (btnUpload) btnUpload.disabled = false;
    if (msgEl) {
      msgEl.textContent = `❌ Error: ${err.message}`;
      msgEl.className = "feedback-msg error";
    }
  }
}

async function checkFotaCloudUpdate() {
  const msgEl = document.getElementById("fotaMessage");
  if (msgEl) {
    msgEl.textContent = "Checking S3 / Cloud repository for new releases...";
    msgEl.className = "feedback-msg info";
  }
  try {
    const res = await fetch("/api/fota/check", { method: "POST" });
    const data = await res.json();
    if (res.ok && data.success) {
      if (data.update_available) {
        if (msgEl) {
          msgEl.textContent = `🎉 New version available: ${data.latest_version} (File: ${data.file}). Trigger via Cloud/MQTT or flash file.`;
          msgEl.className = "feedback-msg success";
        }
      } else {
        if (msgEl) {
          msgEl.textContent = `✓ Device is running the latest firmware (${data.current_version}).`;
          msgEl.className = "feedback-msg success";
        }
      }
    } else {
      if (msgEl) {
        msgEl.textContent = `⚠️ ${data.error || "Unable to reach S3 update bucket"}`;
        msgEl.className = "feedback-msg warn";
      }
    }
  } catch (err) {
    if (msgEl) {
      msgEl.textContent = `❌ Cloud check failed: ${err.message}`;
      msgEl.className = "feedback-msg error";
    }
  }
}

async function triggerFotaRollback() {
  if (!confirm("Are you sure you want to rollback to the previous firmware backup snapshot?")) {
    return;
  }
  const msgEl = document.getElementById("fotaMessage");
  if (msgEl) {
    msgEl.textContent = "Restoring previous firmware snapshot...";
    msgEl.className = "feedback-msg info";
  }
  try {
    const res = await fetch("/api/fota/rollback", { method: "POST" });
    const data = await res.json();
    if (res.ok && data.success) {
      if (msgEl) {
        msgEl.textContent = `✅ ${data.message} (Version: ${data.current_version})`;
        msgEl.className = "feedback-msg success";
      }
      setTimeout(() => { loadFotaStatus(); }, 2000);
    } else {
      if (msgEl) {
        msgEl.textContent = `❌ ${data.message || "Rollback failed"}`;
        msgEl.className = "feedback-msg error";
      }
    }
  } catch (err) {
    if (msgEl) {
      msgEl.textContent = `❌ Rollback error: ${err.message}`;
      msgEl.className = "feedback-msg error";
    }
  }
}

// ── Init & Auto-refresh ────────────────────────────────────────────────────
window.addEventListener("DOMContentLoaded", () => {
  loadDeviceStatus();
  loadCameraStatus();
  loadCameraConfig();
  loadVisionTelemetry();
  loadHardwareTelemetry();
  loadStorageHub();
  loadWifiStatus();
  loadEthernetStatus();
  loadBluetoothStatus();
  loadGpioPins();
  loadOfflineStorage();
  loadGallery();
  loadModelsCatalog();
  loadFotaStatus();

  // Fast loop for live sentinel detections and active load testing (every 1s)
  setInterval(() => {
    loadVisionTelemetry();
    loadCameraStatus();
    if (activeTab === "loadlab" || isLoadTestActive) {
      updateLoadTestStatus();
    }
  }, 1000);

  // Moderate loop for hardware telemetry and storage (every 3s)
  setInterval(() => {
    loadHardwareTelemetry();
    loadDeviceStatus();
    if (activeTab === "storage") {
      loadStorageHub();
      loadOfflineStorage();
    } else if (activeTab === "gallery") {
      loadGallery();
    } else if (activeTab === "network") {
      loadFotaStatus();
    } else if (activeTab === "gpio") {
      loadBleStatusAndConfig();
    }
  }, 3000);

  // Initialize BLE status and credentials
  loadBleStatusAndConfig();
  loadBleCredentials();
});

// ── BLE Mobile Access, Proximity Range & Device Whitelist ──────────────────

function estimateBleDistanceMeters(rssi) {
  // Path loss model: d = 10 ^ ((-59 - rssi) / (10 * 2))
  const d = Math.pow(10, (-59 - rssi) / 20.0);
  return Math.max(0.1, Math.round(d * 10) / 10);
}

function onBleSliderInput(val) {
  const num = parseInt(val, 10);
  const sliderEl = document.getElementById("bleSliderVal");
  const rangeValEl = document.getElementById("bleRangeValue");
  const presetEl = document.getElementById("bleRangePreset");
  const dist = estimateBleDistanceMeters(num);

  if (sliderEl) sliderEl.textContent = `${num} dBm`;
  if (rangeValEl) rangeValEl.textContent = `${num} dBm (~${dist}m)`;

  if (presetEl) {
    const match = ["-55", "-65", "-72", "-82"].includes(String(num));
    presetEl.value = match ? String(num) : "custom";
  }
}

function onBlePresetChange() {
  const presetEl = document.getElementById("bleRangePreset");
  const slider = document.getElementById("bleRssiSlider");
  if (!presetEl || !slider) return;

  if (presetEl.value !== "custom") {
    slider.value = presetEl.value;
    onBleSliderInput(presetEl.value);
  }
}

async function loadBleStatusAndConfig() {
  try {
    const [cfgRes, stRes] = await Promise.all([
      fetch("/api/ble/config", { cache: "no-store" }),
      fetch("/api/ble/status", { cache: "no-store" })
    ]);

    if (cfgRes.ok) {
      const cfg = await cfgRes.json();
      const slider = document.getElementById("bleRssiSlider");
      const autoUnlock = document.getElementById("bleAutoUnlockCheck");

      if (slider && cfg.rssi_threshold !== undefined) {
        slider.value = cfg.rssi_threshold;
        onBleSliderInput(cfg.rssi_threshold);
      }
      if (autoUnlock && cfg.auto_unlock !== undefined) {
        autoUnlock.checked = cfg.auto_unlock;
      }
    }

    if (stRes.ok) {
      const st = await stRes.json();
      const pill = document.getElementById("bleServicePill");
      if (pill) {
        pill.textContent = st.running ? "GATT Active" : "GATT Inactive";
        pill.className = st.running ? "status-pill status-pill-online" : "status-pill status-pill-offline";
      }

      const sessionsContainer = document.getElementById("bleActiveSessionsList");
      if (sessionsContainer) {
        if (st.active_clients && st.active_clients.length > 0) {
          sessionsContainer.innerHTML = st.active_clients.map(c => {
            const inZone = c.in_actuation_zone;
            const dist = c.estimated_distance_m !== undefined ? c.estimated_distance_m : estimateBleDistanceMeters(c.median_rssi);
            return `
              <div style="display:flex; justify-content:space-between; align-items:center; padding:4px 0; border-bottom:1px solid rgba(255,255,255,0.05); font-size:0.8rem;">
                <div style="display:flex; align-items:center; gap:8px;">
                  <span class="font-mono" style="color:#fff; font-weight:600;">${escapeHtml(c.mac)}</span>
                  <span style="font-size:0.75rem; color:var(--text-muted);">${c.median_rssi} dBm (~${dist}m)</span>
                </div>
                <div style="display:flex; align-items:center; gap:6px;">
                  <span class="badge ${inZone ? 'badge-plate' : ''}" style="font-size:0.65rem; background:${inZone ? 'rgba(52,211,153,0.2)' : 'rgba(239,68,68,0.2)'}; color:${inZone ? '#34d399' : '#f87171'};">
                    ${inZone ? 'IN ACTUATION ZONE' : 'OUT OF RANGE'}
                  </span>
                  <button class="btn btn-secondary btn-sm" style="padding:2px 6px; font-size:0.7rem;" onclick="prefillBleDevice('${escapeHtml(c.mac)}')">+ Whitelist</button>
                </div>
              </div>
            `;
          }).join("");
        } else {
          sessionsContainer.innerHTML = "<p style='color:var(--text-muted); font-size:0.8rem; margin:0;'>No BLE mobile devices connected currently.</p>";
        }
      }
    }
  } catch (err) {}
}

async function saveBleRangeConfig() {
  const slider = document.getElementById("bleRssiSlider");
  const autoUnlock = document.getElementById("bleAutoUnlockCheck");
  const threshold = slider ? parseInt(slider.value, 10) : -70;
  const enableAuto = autoUnlock ? autoUnlock.checked : true;

  try {
    const res = await fetch("/api/ble/config", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({
        rssi_threshold: threshold,
        auto_unlock: enableAuto
      })
    });
    const data = await res.json();
    if (data.ok) {
      showMsg("bleSimMessage", `Range saved: Threshold = ${threshold} dBm, Auto-Unlock = ${enableAuto ? 'ON' : 'OFF'}`);
    } else {
      showMsg("bleSimMessage", "Failed to save range config", true);
    }
  } catch (err) {
    showMsg("bleSimMessage", "Error communicating with BLE API", true);
  }
}

function generateRandomBleSecret() {
  const arr = new Uint8Array(32);
  window.crypto.getRandomValues(arr);
  const hex = Array.from(arr).map(b => b.toString(16).padStart(2, "0")).join("");
  const input = document.getElementById("bleSecretKey");
  if (input) input.value = hex;
}

function prefillBleDevice(mac) {
  const macInput = document.getElementById("bleDeviceMac");
  if (macInput) macInput.value = mac;
  if (!document.getElementById("bleSecretKey").value) {
    generateRandomBleSecret();
  }
  showMsg("bleAddMessage", `Selected MAC ${mac}. Secret key generated!`);
}

async function addBleDevice() {
  const holder = document.getElementById("bleHolderName").value.trim();
  const role = document.getElementById("bleDeviceRole").value;
  const mac = document.getElementById("bleDeviceMac").value.trim();
  let secret = document.getElementById("bleSecretKey").value.trim();

  if (!holder) {
    showMsg("bleAddMessage", "Holder or Device Name is required", true);
    return;
  }

  if (!secret) {
    generateRandomBleSecret();
    secret = document.getElementById("bleSecretKey").value.trim();
  }

  try {
    const res = await fetch("/api/credentials", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({
        holder_name: holder,
        role: role,
        credential_hash: secret,
        plate_number: mac || ""
      })
    });
    const data = await res.json();
    if (data.ok) {
      showMsg("bleAddMessage", `Device '${holder}' added to whitelist successfully!`);
      document.getElementById("bleHolderName").value = "";
      document.getElementById("bleDeviceMac").value = "";
      document.getElementById("bleSecretKey").value = "";
      loadBleCredentials();
    } else {
      showMsg("bleAddMessage", `Error: ${data.message || 'Failed to add credential'}`, true);
    }
  } catch (err) {
    showMsg("bleAddMessage", "Error saving credential", true);
  }
}

async function loadBleCredentials() {
  const tbody = document.getElementById("bleWhitelistTableBody");
  if (!tbody) return;

  try {
    const res = await fetch("/api/credentials?limit=50", { cache: "no-store" });
    if (!res.ok) return;
    const creds = await res.json();

    if (!Array.isArray(creds) || creds.length === 0) {
      tbody.innerHTML = `<tr><td colspan="5" style="text-align:center; padding:12px; color:var(--text-muted);">No authorized devices registered yet. Add one above.</td></tr>`;
      return;
    }

    tbody.innerHTML = creds.map(c => {
      const isRevoked = !!c.is_revoked;
      const hashShort = c.credential_hash ? `${c.credential_hash.slice(0, 8)}...${c.credential_hash.slice(-6)}` : "-";
      return `
        <tr>
          <td>
            <div style="font-weight:600; color:#fff;">${escapeHtml(c.holder_name || 'Unnamed')}</div>
            <div class="font-mono" style="font-size:0.75rem; color:var(--text-muted);">${escapeHtml(c.plate_number || 'Any MAC')}</div>
          </td>
          <td>
            <span class="badge" style="text-transform:uppercase; font-size:0.7rem; background:rgba(6,182,212,0.15); color:var(--accent-cyan); border:1px solid rgba(6,182,212,0.3);">
              ${escapeHtml(c.role || 'user')}
            </span>
          </td>
          <td>
            <span class="font-mono" style="font-size:0.8rem; color:var(--text-muted); cursor:pointer;" title="Click to copy full key" onclick="navigator.clipboard.writeText('${escapeHtml(c.credential_hash)}'); alert('Secret key copied to clipboard!');">
              ${hashShort} 📋
            </span>
          </td>
          <td>
            <span class="status-pill ${isRevoked ? 'status-pill-offline' : 'status-pill-online'}">
              ${isRevoked ? 'Revoked' : 'Active'}
            </span>
          </td>
          <td style="text-align:right;">
            ${!isRevoked ? `
              <button class="btn btn-danger btn-sm" onclick="revokeBleCredential('${escapeHtml(c.credential_hash)}')">Revoke</button>
            ` : '<span style="color:var(--text-muted); font-size:0.75rem;">Revoked</span>'}
          </td>
        </tr>
      `;
    }).join("");
  } catch (err) {
    tbody.innerHTML = `<tr><td colspan="5" style="text-align:center; padding:12px; color:var(--accent-rose);">Failed to load credentials list.</td></tr>`;
  }
}

async function revokeBleCredential(hash) {
  if (!confirm("Are you sure you want to revoke this mobile key?")) return;
  try {
    const res = await fetch("/api/credentials/revoke", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ credential_hash: hash })
    });
    const data = await res.json();
    if (data.ok) {
      showMsg("bleAddMessage", "Device key revoked.");
      loadBleCredentials();
    } else {
      showMsg("bleAddMessage", "Failed to revoke key", true);
    }
  } catch (err) {
    showMsg("bleAddMessage", "Error revoking key", true);
  }
}

async function simulateMobileUnlock() {
  showMsg("bleSimMessage", "Running simulated cryptographic BLE handshake...");
  try {
    const res = await fetch("/api/ble/simulate-test", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ mac: "mobile_test_sim", rssi: -62 })
    });
    const data = await res.json();
    if (data.authorized) {
      showMsg("bleSimMessage", `✓ ${data.message} (RSSI: ${data.rssi} dBm, Nonce: ${data.nonce.slice(0,8)}...)`);
      loadBleStatusAndConfig();
    } else {
      showMsg("bleSimMessage", `✕ ${data.message}`, true);
    }
  } catch (err) {
    showMsg("bleSimMessage", "Failed to run simulation test", true);
  }
}

