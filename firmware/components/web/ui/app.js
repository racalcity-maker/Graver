import { connectWiFi, listJobs, loadSettings, loadStatus, postJson } from '/assets/api.js?v=20260304e';
import { initUpload } from '/assets/upload.js?v=20260304e';

const tabs = document.querySelectorAll('[data-tab]');
const panels = {
  status: document.getElementById('tab-status'),
  control: document.getElementById('tab-control'),
  upload: document.getElementById('tab-upload'),
  jobs: document.getElementById('tab-jobs'),
  settings: document.getElementById('tab-settings'),
};
const logNode = document.getElementById('command-log');
const homeAction = document.body.dataset.homeAction || document.getElementById('home-btn').textContent.trim();
let statusPollHandle = null;

function setTab(name) {
  tabs.forEach((button) => button.classList.toggle('active', button.dataset.tab === name));
  Object.entries(panels).forEach(([key, panel]) => panel.classList.toggle('hidden', key !== name));
}

function logLine(message) {
  const stamp = new Date().toLocaleTimeString();
  logNode.textContent = `[${stamp}] ${message}\n${logNode.textContent}`;
}

async function refreshStatus() {
  const status = await loadStatus();
  document.getElementById('status-state').textContent = status.state;
  document.getElementById('status-motion').textContent = `${status.motionBusy} / ${status.motionOp}`;
  document.getElementById('status-x').textContent = Number(status.x).toFixed(3);
  document.getElementById('status-y').textContent = Number(status.y).toFixed(3);
  document.getElementById('status-homed').textContent = String(status.homed);
  document.getElementById('status-motors').textContent = String(status.motorsHeld);
  document.getElementById('status-laser').textContent = String(status.laserArmed);
  document.getElementById('status-network').textContent = status.networkMode || '-';
  document.getElementById('status-ip').textContent = status.staIp || (status.apActive ? '192.168.4.1' : '-');
  document.getElementById('status-message').textContent = status.message || '-';
  document.getElementById('status-job').textContent = status.activeJobId || '-';
  const percent = Number(status.jobProgressPercent || 0);
  const rowsDone = Number(status.jobRowsDone || 0);
  const rowsTotal = Number(status.jobRowsTotal || 0);
  const progressText = `${percent}% (${rowsDone} / ${rowsTotal} units)`;
  document.getElementById('job-progress-fill').style.width = `${percent}%`;
  document.getElementById('job-progress-text').textContent = progressText;
  document.getElementById('jobs-progress-fill').style.width = `${percent}%`;
  document.getElementById('jobs-progress-text').textContent = progressText;
}

function ensureStatusPolling() {
  if (statusPollHandle !== null) {
    return;
  }
  statusPollHandle = window.setInterval(() => {
    refreshStatus().catch(() => {});
  }, 1000);
}

function pauseStatusPolling() {
  if (statusPollHandle !== null) {
    window.clearInterval(statusPollHandle);
    statusPollHandle = null;
  }
}

async function refreshJobs() {
  const jobs = await listJobs();
  document.getElementById('jobs-list').textContent = JSON.stringify(jobs, null, 2);
  const items = Array.isArray(jobs?.items) ? jobs.items : [];
  if (items.length > 0 && !document.getElementById('run-job-id').value) {
    document.getElementById('run-job-id').value = items[0];
  }
}

async function loadSettingsIntoUi() {
  const settings = await loadSettings();
  document.getElementById('settings-json').value = JSON.stringify(settings, null, 2);
  document.getElementById('wifi-ssid').value = settings?.network?.staSsid || '';
  document.getElementById('wifi-password').value = settings?.network?.staPassword || '';
}

async function runMoveTo() {
  const xMm = Number(document.getElementById('move-to-x').value);
  const yMm = Number(document.getElementById('move-to-y').value);
  const feedMmMin = Number(document.getElementById('move-to-feed').value);
  await postJson('/api/control/move-to', { xMm, yMm, feedMmMin });
  logLine(`Move To x=${xMm} y=${yMm} feed=${feedMmMin}.`);
}

async function runMoveToWithLog() {
  try {
    await runMoveTo();
  } catch (error) {
    logLine(`Move To failed: ${error.message}`);
  }
}

tabs.forEach((button) => button.addEventListener('click', () => setTab(button.dataset.tab)));

document.getElementById('refresh-status').addEventListener('click', async () => {
  try {
    await refreshStatus();
    logLine('Status refreshed.');
  } catch (error) {
    logLine(`Status failed: ${error.message}`);
  }
});

document.getElementById('refresh-jobs').addEventListener('click', async () => {
  try {
    await refreshJobs();
    logLine('Jobs refreshed.');
  } catch (error) {
    logLine(`Jobs failed: ${error.message}`);
  }
});

document.getElementById('run-job-btn').addEventListener('click', async () => {
  const jobId = document.getElementById('run-job-id').value.trim();
  try {
    await postJson('/api/control/jobs/run', { jobId });
    logLine(`Job run requested: ${jobId}.`);
  } catch (error) {
    logLine(`Run job failed: ${error.message}`);
  }
});

document.getElementById('pause-job-btn').addEventListener('click', async () => {
  try {
    await postJson('/api/control/jobs/pause', {});
    logLine('Job pause requested.');
  } catch (error) {
    logLine(`Pause job failed: ${error.message}`);
  }
});

document.getElementById('resume-job-btn').addEventListener('click', async () => {
  try {
    await postJson('/api/control/jobs/resume', {});
    logLine('Job resume requested.');
  } catch (error) {
    logLine(`Resume job failed: ${error.message}`);
  }
});

document.getElementById('abort-job-btn').addEventListener('click', async () => {
  try {
    await postJson('/api/control/jobs/abort', {});
    logLine('Job abort requested.');
  } catch (error) {
    logLine(`Abort job failed: ${error.message}`);
  }
});

document.getElementById('load-settings').addEventListener('click', async () => {
  try {
    await loadSettingsIntoUi();
    logLine('Settings loaded.');
  } catch (error) {
    logLine(`Settings failed: ${error.message}`);
  }
});

document.getElementById('wifi-connect-btn').addEventListener('click', async () => {
  const ssid = document.getElementById('wifi-ssid').value.trim();
  const password = document.getElementById('wifi-password').value;
  const statusNode = document.getElementById('wifi-connect-status');

  if (!ssid) {
    statusNode.textContent = 'Enter SSID first.';
    return;
  }

  try {
    statusNode.textContent = 'Connecting to Wi-Fi...';
    const response = await connectWiFi(ssid, password);
    const ip = response?.ip || '-';
    const delaySeconds = Math.round(Number(response?.apShutdownDelayMs || 0) / 1000);
    statusNode.textContent = `Connected. New IP: ${ip}. SoftAP will turn off in ${delaySeconds}s.`;
    logLine(`Wi-Fi connected: ${ip}`);
    window.alert(`Connected to Wi-Fi.\nNew IP: ${ip}\nSoftAP will turn off in ${delaySeconds} seconds.`);
    await refreshStatus();
    await loadSettingsIntoUi();
  } catch (error) {
    statusNode.textContent = `Wi-Fi connect failed: ${error.message}`;
    logLine(`Wi-Fi connect failed: ${error.message}`);
  }
});

document.getElementById('home-btn').addEventListener('click', async () => {
  try {
    await postJson('/api/control/home', {});
    logLine(`${homeAction} requested.`);
  } catch (error) {
    logLine(`${homeAction} failed: ${error.message}`);
  }
});

document.getElementById('go-zero-btn').addEventListener('click', async () => {
  try {
    await postJson('/api/control/go-to-zero', {});
    logLine('Go To Zero requested.');
  } catch (error) {
    logLine(`Go To Zero failed: ${error.message}`);
  }
});

document.getElementById('move-to-btn').addEventListener('click', runMoveToWithLog);
document.getElementById('move-to-run-btn').addEventListener('click', runMoveToWithLog);

document.getElementById('frame-btn').addEventListener('click', async () => {
  try {
    await postJson('/api/control/frame', {});
    logLine('Frame requested.');
  } catch (error) {
    logLine(`Frame failed: ${error.message}`);
  }
});

document.getElementById('release-motors-btn').addEventListener('click', async () => {
  try {
    await postJson('/api/control/motors/release', {});
    logLine('Motors released.');
  } catch (error) {
    logLine(`Release failed: ${error.message}`);
  }
});

document.getElementById('hold-motors-btn').addEventListener('click', async () => {
  try {
    await postJson('/api/control/motors/hold', {});
    logLine('Motors held.');
  } catch (error) {
    logLine(`Hold failed: ${error.message}`);
  }
});

document.getElementById('stop-btn').addEventListener('click', async () => {
  try {
    await postJson('/api/control/stop', {});
    logLine('Stop requested.');
  } catch (error) {
    logLine(`Stop failed: ${error.message}`);
  }
});

document.getElementById('laser-pulse').addEventListener('click', async () => {
  const power = Number(document.getElementById('laser-power').value);
  const durationMs = Number(document.getElementById('laser-duration').value);
  try {
    await postJson('/api/diag/laser-pulse', { power, durationMs });
    logLine(`Laser pulse power=${power} duration=${durationMs}ms.`);
  } catch (error) {
    logLine(`Laser pulse failed: ${error.message}`);
  }
});

initUpload(logLine, {
  onUploadStart: pauseStatusPolling,
  onUploadDone: ensureStatusPolling,
  onJobUploaded: async (jobId) => {
    document.getElementById('run-job-id').value = jobId;
    try {
      await refreshJobs();
      logLine(`Job ready to run: ${jobId}.`);
    } catch (error) {
      logLine(`Jobs refresh after upload failed: ${error.message}`);
    }
  },
});
setTab('status');
ensureStatusPolling();
refreshStatus().catch((error) => logLine(`Status failed: ${error.message}`));
refreshJobs().catch((error) => logLine(`Jobs failed: ${error.message}`));
loadSettingsIntoUi().catch((error) => logLine(`Settings failed: ${error.message}`));
