export async function requestJson(url, options) {
  const response = await fetch(url, options);
  const text = await response.text();
  let data = null;
  try {
    data = text ? JSON.parse(text) : null;
  } catch {
    data = text;
  }
  if (!response.ok) {
    throw new Error(typeof data === 'string' ? data : JSON.stringify(data));
  }
  return data;
}

export async function postJson(url, payload) {
  return requestJson(url, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(payload),
  });
}

export async function postBinary(url, body, headers = {}) {
  const response = await fetch(url, {
    method: 'POST',
    headers,
    body,
  });
  const text = await response.text();
  let data = null;
  try {
    data = text ? JSON.parse(text) : null;
  } catch {
    data = text;
  }
  if (!response.ok) {
    throw new Error(typeof data === 'string' ? data : JSON.stringify(data));
  }
  return data;
}

export function listJobs() {
  return requestJson('/api/jobs');
}

export function loadSettings() {
  return requestJson('/api/settings/machine');
}

export function loadStatus() {
  return requestJson('/api/status');
}

export function uploadJobManifest(jobId, manifest) {
  return postJson('/api/jobs/manifest', { jobId, manifest });
}

export function connectWiFi(ssid, password) {
  return postJson('/api/network/connect', { ssid, password });
}

export async function uploadJobRaster(jobId, rasterData) {
  const chunkSize = 16 * 1024;
  const totalSize = rasterData.byteLength;
  let offset = 0;

  while (offset < totalSize) {
    const end = Math.min(offset + chunkSize, totalSize);
    const chunk = rasterData.slice(offset, end);
    await postBinary('/api/jobs/raster', chunk, {
      'X-Job-Id': jobId,
      'X-Chunk-Offset': String(offset),
      'X-Chunk-Total': String(totalSize),
    });
    offset = end;
  }

  return { ok: true };
}
