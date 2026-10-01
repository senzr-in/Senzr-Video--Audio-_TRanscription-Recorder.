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
  } else if (tabId === "gpio") {
    loadGpioPins();
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
  document.getElementById("galFilterAll").classList.toggle("active", filter === "all");
  document.getElementById("galFilterPlates").classList.toggle("active", filter === "plates");
  document.getElementById("galFilterPersons").classList.toggle("active", filter === "persons");
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
  document.getElementById("modePerson").classList.toggle("active", mode === "person");
  document.getElementById("modePlate").classList.toggle("active", mode === "plate");
  document.getElementById("modeBoth").classList.toggle("active", mode === "both");
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

    if (personCountEl) {
      personCountEl.textContent = data.person_count != null ? data.person_count : "0";
      if (data.person_count > 0 && data.persons && data.persons.length > 0) {
        const topConf = Math.round((data.persons[0].confidence || 0) * 100);
        personConfEl.textContent = `Confidence: ${topConf}%`;
      } else {
        personConfEl.textContent = "Confidence: -";
      }
    }

    if (plateTextEl) {
      if (data.plate_detected && data.plates && data.plates.length > 0) {
        const topPlate = data.plates[0];
        const clean = topPlate.plate_clean || topPlate.plate_number || "";
        const formatted = topPlate.plate_formatted || clean;
        plateTextEl.textContent = formatted || "PLATE DETECTED";
        const topConf = Math.round((topPlate.confidence || 0) * 100);
        plateConfEl.textContent = `Confidence: ${topConf}% • Template OCR`;
      } else {
        plateTextEl.textContent = "-";
        plateConfEl.textContent = "Optical Character Recognition";
      }
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

// ── Init & Auto-refresh ────────────────────────────────────────────────────
window.addEventListener("DOMContentLoaded", () => {
  loadDeviceStatus();
  loadCameraStatus();
  loadVisionTelemetry();
  loadHardwareTelemetry();
  loadStorageHub();
  loadWifiStatus();
  loadEthernetStatus();
  loadBluetoothStatus();
  loadGpioPins();
  loadOfflineStorage();
  loadGallery();

  // Fast loop for live sentinel detections (every 1s)
  setInterval(() => {
    loadVisionTelemetry();
    loadCameraStatus();
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
    }
  }, 3000);
});
