const STORAGE_KEY = "lasergraver.studio.device.v1";

const DEFAULT_CONFIG = {
  baseUrl: "",
  jobId: "studio-001",
  dpi: 254,
  printSpeedMmMin: 1200,
  travelSpeedMmMin: 2400,
  overscanMm: 0,
  workspaceWidthMm: 180,
  workspaceHeightMm: 130,
  showGrid: true
};

function clamp(value, minValue, maxValue) {
  return Math.max(minValue, Math.min(maxValue, value));
}

function toNumber(value, fallback) {
  const parsed = Number(value);
  return Number.isFinite(parsed) ? parsed : fallback;
}

function toBool(value, fallback) {
  if (typeof value === "boolean") {
    return value;
  }
  if (value === "true") {
    return true;
  }
  if (value === "false") {
    return false;
  }
  return fallback;
}

function normalizeBaseUrl(value) {
  const raw = String(value ?? "").trim();
  if (!raw) {
    return "";
  }
  return raw.replace(/\/+$/, "");
}

function sanitize(config) {
  return {
    baseUrl: normalizeBaseUrl(config?.baseUrl ?? DEFAULT_CONFIG.baseUrl),
    jobId: String(config?.jobId ?? DEFAULT_CONFIG.jobId).trim() || DEFAULT_CONFIG.jobId,
    dpi: clamp(Math.round(toNumber(config?.dpi, DEFAULT_CONFIG.dpi)), 25, 1200),
    printSpeedMmMin: clamp(Math.round(toNumber(config?.printSpeedMmMin, DEFAULT_CONFIG.printSpeedMmMin)), 1, 40000),
    travelSpeedMmMin: clamp(Math.round(toNumber(config?.travelSpeedMmMin, DEFAULT_CONFIG.travelSpeedMmMin)), 1, 40000),
    overscanMm: clamp(toNumber(config?.overscanMm, DEFAULT_CONFIG.overscanMm), 0, 20),
    workspaceWidthMm: clamp(toNumber(config?.workspaceWidthMm, DEFAULT_CONFIG.workspaceWidthMm), 20, 2000),
    workspaceHeightMm: clamp(toNumber(config?.workspaceHeightMm, DEFAULT_CONFIG.workspaceHeightMm), 20, 2000),
    showGrid: toBool(config?.showGrid, DEFAULT_CONFIG.showGrid)
  };
}

export function loadDeviceConfig() {
  try {
    const raw = window.localStorage.getItem(STORAGE_KEY);
    if (!raw) {
      return { ...DEFAULT_CONFIG };
    }
    const parsed = JSON.parse(raw);
    return sanitize({ ...DEFAULT_CONFIG, ...parsed });
  } catch {
    return { ...DEFAULT_CONFIG };
  }
}

export function saveDeviceConfig(config) {
  const sanitized = sanitize(config);
  try {
    window.localStorage.setItem(STORAGE_KEY, JSON.stringify(sanitized));
  } catch {
    // Ignore storage quota errors.
  }
  return sanitized;
}

export function getDefaultDeviceConfig() {
  return { ...DEFAULT_CONFIG };
}
