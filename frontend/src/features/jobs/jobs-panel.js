import { createApiClient } from "../../shared/api/client.js";
import { loadDeviceConfig, saveDeviceConfig } from "../../shared/settings/device-config.js";
import { buildMultiStageJobPlan } from "./job-planner.js";
import { uploadSceneJobOverWifi } from "./wifi-uploader.js";

function downloadJson(fileName, payload) {
  const blob = new Blob([JSON.stringify(payload, null, 2)], { type: "application/json" });
  const url = URL.createObjectURL(blob);
  const anchor = document.createElement("a");
  anchor.href = url;
  anchor.download = fileName;
  anchor.click();
  URL.revokeObjectURL(url);
}

function nowTimeLabel() {
  return new Date().toLocaleTimeString();
}

function clamp(value, minValue, maxValue) {
  return Math.max(minValue, Math.min(maxValue, value));
}

function renderSummary(target, plan) {
  target.innerHTML = "";

  const stageCount = document.createElement("div");
  stageCount.className = "kv";
  stageCount.textContent = `Stages: ${plan.stages.length}`;
  target.appendChild(stageCount);

  const estimate = document.createElement("div");
  estimate.className = "kv";
  estimate.textContent = `Estimated: ${(plan.estimatedSec / 60).toFixed(1)} min`;
  target.appendChild(estimate);

  const distance = document.createElement("div");
  distance.className = "kv";
  distance.textContent =
    `Distance: ${plan.meta.totalDistanceMm.toFixed(1)} mm (laser ${plan.meta.totalLaserDistanceMm.toFixed(1)} mm)`;
  target.appendChild(distance);

  for (const stage of plan.stages) {
    const row = document.createElement("div");
    row.className = "kv";
    row.textContent =
      `${stage.order}: ${stage.operationType} | ${stage.layerName} | objects: ${stage.items.length} | ` +
      `${stage.metrics.distanceMm.toFixed(1)} mm`;
    target.appendChild(row);
  }
}

export function createJobsPanel(container, store) {
  if (!container) {
    return;
  }

  container.innerHTML = "";

  const existingUploadModal = document.getElementById("upload-modal");
  if (existingUploadModal) {
    existingUploadModal.remove();
  }
  const existingSettingsModal = document.getElementById("settings-modal");
  if (existingSettingsModal) {
    existingSettingsModal.remove();
  }

  const initialConfig = loadDeviceConfig();

  const uploadModal = document.createElement("div");
  uploadModal.id = "upload-modal";
  uploadModal.className = "upload-modal hidden";
  uploadModal.innerHTML = `
    <div class="upload-modal-card">
      <div class="upload-modal-head">
        <h3>Загрузка работы</h3>
        <button id="close-upload-window-btn" class="btn btn-xs" type="button">Закрыть</button>
      </div>
      <p class="muted">В этом окне только загрузка/запуск. Параметры DPI и скоростей находятся в окне Settings.</p>

      <div class="stack device-box">
        <label class="row"><span>Job ID</span><input id="upload-job-id" type="text" value="${initialConfig.jobId}" /></label>
        <div class="row actions-row">
          <button id="upload-btn" class="btn alt btn-xs" type="button">Upload</button>
          <button id="upload-run-btn" class="btn alt btn-xs" type="button">Upload + Run</button>
        </div>
        <div class="row actions-row">
          <button id="pause-btn" class="btn btn-xs" type="button">Pause</button>
          <button id="resume-btn" class="btn btn-xs" type="button">Resume</button>
          <button id="abort-btn" class="btn danger btn-xs" type="button">Abort</button>
          <button id="export-job-btn" class="btn btn-xs" type="button">Export JSON</button>
        </div>
        <pre id="upload-log" class="job-log"></pre>
      </div>

      <div id="upload-job-summary" class="stack"></div>
      <pre id="upload-job-preview" class="job-preview"></pre>
    </div>
  `;
  document.body.appendChild(uploadModal);

  const settingsModal = document.createElement("div");
  settingsModal.id = "settings-modal";
  settingsModal.className = "upload-modal hidden";
  settingsModal.innerHTML = `
    <div class="upload-modal-card">
      <div class="upload-modal-head">
        <h3>Settings</h3>
        <button id="close-settings-window-btn" class="btn btn-xs" type="button">Close</button>
      </div>

      <div class="stack device-box">
        <h4 class="settings-section-title">Network</h4>
        <label class="row"><span>Device URL</span><input id="settings-device-url" type="text" placeholder="http://192.168.4.1" value="${initialConfig.baseUrl}" /></label>
        <div class="row actions-row">
          <button id="ping-btn" class="btn btn-xs" type="button">Ping</button>
        </div>
      </div>

      <div class="stack device-box">
        <h4 class="settings-section-title">General</h4>
        <label class="row"><span>Workspace W mm</span><input id="settings-workspace-width-mm" type="number" min="20" max="2000" step="1" value="${initialConfig.workspaceWidthMm}" /></label>
        <label class="row"><span>Workspace H mm</span><input id="settings-workspace-height-mm" type="number" min="20" max="2000" step="1" value="${initialConfig.workspaceHeightMm}" /></label>
        <label class="row"><span>Show grid</span><input id="settings-show-grid" type="checkbox" ${initialConfig.showGrid ? "checked" : ""} /></label>
      </div>

      <div class="stack device-box">
        <h4 class="settings-section-title">Laser Profile</h4>
        <label class="row"><span>DPI</span><input id="settings-dpi" type="number" min="25" max="1200" step="1" value="${initialConfig.dpi}" /></label>
        <label class="row"><span>Print mm/min</span><input id="settings-print-speed" type="number" min="1" step="1" value="${initialConfig.printSpeedMmMin}" /></label>
        <label class="row"><span>Travel mm/min</span><input id="settings-travel-speed" type="number" min="1" step="1" value="${initialConfig.travelSpeedMmMin}" /></label>
        <label class="row"><span>Overscan mm</span><input id="settings-overscan" type="number" min="0" max="20" step="0.1" value="${initialConfig.overscanMm}" /></label>
      </div>

      <pre id="settings-log" class="job-log"></pre>
    </div>
  `;
  document.body.appendChild(settingsModal);

  const closeUploadButton = uploadModal.querySelector("#close-upload-window-btn");
  const closeSettingsButton = settingsModal.querySelector("#close-settings-window-btn");

  const uploadSummaryNode = uploadModal.querySelector("#upload-job-summary");
  const uploadPreviewNode = uploadModal.querySelector("#upload-job-preview");
  const exportButton = uploadModal.querySelector("#export-job-btn");
  const uploadLogNode = uploadModal.querySelector("#upload-log");
  const settingsLogNode = settingsModal.querySelector("#settings-log");

  const jobIdNode = uploadModal.querySelector("#upload-job-id");
  const uploadButton = uploadModal.querySelector("#upload-btn");
  const uploadRunButton = uploadModal.querySelector("#upload-run-btn");
  const pauseButton = uploadModal.querySelector("#pause-btn");
  const resumeButton = uploadModal.querySelector("#resume-btn");
  const abortButton = uploadModal.querySelector("#abort-btn");

  const baseUrlNode = settingsModal.querySelector("#settings-device-url");
  const pingButton = settingsModal.querySelector("#ping-btn");
  const workspaceWidthNode = settingsModal.querySelector("#settings-workspace-width-mm");
  const workspaceHeightNode = settingsModal.querySelector("#settings-workspace-height-mm");
  const showGridNode = settingsModal.querySelector("#settings-show-grid");
  const dpiNode = settingsModal.querySelector("#settings-dpi");
  const printSpeedNode = settingsModal.querySelector("#settings-print-speed");
  const travelSpeedNode = settingsModal.querySelector("#settings-travel-speed");
  const overscanNode = settingsModal.querySelector("#settings-overscan");

  let saveTimer = null;
  let busy = false;
  let lastPlan = null;
  const logLines = [];

  function openUploadModal() {
    settingsModal.classList.add("hidden");
    uploadModal.classList.remove("hidden");
  }

  function closeUploadModal() {
    uploadModal.classList.add("hidden");
  }

  function openSettingsModal() {
    uploadModal.classList.add("hidden");
    settingsModal.classList.remove("hidden");
  }

  function closeSettingsModal() {
    settingsModal.classList.add("hidden");
  }

  function appendLog(message) {
    logLines.push(`[${nowTimeLabel()}] ${message}`);
    while (logLines.length > 120) {
      logLines.shift();
    }
    const value = logLines.join("\n");
    uploadLogNode.textContent = value;
    settingsLogNode.textContent = value;
    uploadLogNode.scrollTop = uploadLogNode.scrollHeight;
    settingsLogNode.scrollTop = settingsLogNode.scrollHeight;
  }

  function readConfigFromInputs() {
    return {
      baseUrl: baseUrlNode.value.trim(),
      jobId: jobIdNode.value.trim(),
      dpi: Number(dpiNode.value),
      printSpeedMmMin: Number(printSpeedNode.value),
      travelSpeedMmMin: Number(travelSpeedNode.value),
      overscanMm: Number(overscanNode.value),
      workspaceWidthMm: Number(workspaceWidthNode.value),
      workspaceHeightMm: Number(workspaceHeightNode.value),
      showGrid: Boolean(showGridNode.checked)
    };
  }

  function applyConfigToInputs(config) {
    baseUrlNode.value = config.baseUrl;
    jobIdNode.value = config.jobId;
    dpiNode.value = String(config.dpi);
    printSpeedNode.value = String(config.printSpeedMmMin);
    travelSpeedNode.value = String(config.travelSpeedMmMin);
    overscanNode.value = String(config.overscanMm);
    workspaceWidthNode.value = String(config.workspaceWidthMm);
    workspaceHeightNode.value = String(config.workspaceHeightMm);
    showGridNode.checked = Boolean(config.showGrid);
  }

  function applyGeneralSettingsToScene(config) {
    const widthMm = clamp(Number(config.workspaceWidthMm), 20, 2000);
    const heightMm = clamp(Number(config.workspaceHeightMm), 20, 2000);
    const showGrid = Boolean(config.showGrid);

    store.setState((state) => ({
      ...state,
      scene: {
        ...state.scene,
        widthMm,
        heightMm
      },
      ui: {
        ...(state.ui || {}),
        showGrid
      }
    }));
  }

  function persistConfigSoon() {
    if (saveTimer !== null) {
      window.clearTimeout(saveTimer);
    }
    saveTimer = window.setTimeout(() => {
      const saved = saveDeviceConfig(readConfigFromInputs());
      applyConfigToInputs(saved);
      applyGeneralSettingsToScene(saved);
      saveTimer = null;
    }, 300);
  }

  function setBusy(nextBusy) {
    busy = nextBusy;
    pingButton.disabled = busy;
    uploadButton.disabled = busy;
    uploadRunButton.disabled = busy;
    pauseButton.disabled = busy;
    resumeButton.disabled = busy;
    abortButton.disabled = busy;
  }

  async function runWithApi(actionName, handler) {
    try {
      setBusy(true);
      const config = saveDeviceConfig(readConfigFromInputs());
      applyConfigToInputs(config);
      applyGeneralSettingsToScene(config);
      const client = createApiClient(config.baseUrl);
      await handler(client, config);
    } catch (error) {
      appendLog(`${actionName} failed: ${error.message || String(error)}`);
    } finally {
      setBusy(false);
    }
  }

  closeUploadButton.addEventListener("click", closeUploadModal);
  closeSettingsButton.addEventListener("click", closeSettingsModal);

  uploadModal.addEventListener("click", (event) => {
    if (event.target === uploadModal) {
      closeUploadModal();
    }
  });
  settingsModal.addEventListener("click", (event) => {
    if (event.target === settingsModal) {
      closeSettingsModal();
    }
  });

  window.addEventListener("keydown", (event) => {
    if (event.key === "Escape") {
      if (!uploadModal.classList.contains("hidden")) {
        closeUploadModal();
      }
      if (!settingsModal.classList.contains("hidden")) {
        closeSettingsModal();
      }
    }
  });

  window.addEventListener("lasergraver:open-upload-modal", openUploadModal);
  window.addEventListener("lasergraver:open-settings-modal", openSettingsModal);

  pingButton.addEventListener("click", () => runWithApi("Ping", async (client) => {
    const status = await client.getStatus();
    appendLog(`Ping OK: state=${status?.state || "unknown"} x=${status?.x ?? "?"} y=${status?.y ?? "?"}`);
  }));

  uploadButton.addEventListener("click", () => runWithApi("Upload", async (client, config) => {
    const stats = await uploadSceneJobOverWifi(client, store.getState(), config, {
      runAfterUpload: false,
      onProgress: appendLog
    });
    appendLog(`Uploaded job=${stats.jobId} raster=${stats.widthPx}x${stats.heightPx} bytes=${stats.bytes}`);
  }));

  uploadRunButton.addEventListener("click", () => runWithApi("Upload+Run", async (client, config) => {
    const stats = await uploadSceneJobOverWifi(client, store.getState(), config, {
      runAfterUpload: true,
      onProgress: appendLog
    });
    appendLog(`Uploaded and started job=${stats.jobId} (${stats.widthPx}x${stats.heightPx})`);
  }));

  pauseButton.addEventListener("click", () => runWithApi("Pause", async (client) => {
    await client.pause();
    appendLog("Pause sent.");
  }));

  resumeButton.addEventListener("click", () => runWithApi("Resume", async (client) => {
    await client.resume();
    appendLog("Resume sent.");
  }));

  abortButton.addEventListener("click", () => runWithApi("Abort", async (client) => {
    await client.abort();
    appendLog("Abort sent.");
  }));

  [
    jobIdNode,
    baseUrlNode,
    workspaceWidthNode,
    workspaceHeightNode,
    dpiNode,
    printSpeedNode,
    travelSpeedNode,
    overscanNode
  ].forEach((node) => {
    node.addEventListener("input", persistConfigSoon);
    node.addEventListener("change", persistConfigSoon);
  });
  showGridNode.addEventListener("change", persistConfigSoon);

  exportButton.addEventListener("click", () => {
    if (!lastPlan) {
      return;
    }
    downloadJson("native-multi-stage-job.json", lastPlan);
  });

  function render(state) {
    const plan = buildMultiStageJobPlan(state);
    lastPlan = plan;
    uploadPreviewNode.textContent = JSON.stringify(plan, null, 2);
    renderSummary(uploadSummaryNode, plan);
  }

  applyConfigToInputs(initialConfig);
  applyGeneralSettingsToScene(initialConfig);
  store.subscribe(render);
  appendLog("UI ready.");
}
