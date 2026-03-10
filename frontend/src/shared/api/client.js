const DEFAULT_BASE_URL = "";

function normalizeBaseUrl(baseUrl) {
  return String(baseUrl ?? "").trim().replace(/\/+$/, "");
}

function buildUrl(path, baseUrl = DEFAULT_BASE_URL) {
  if (!path.startsWith("/")) {
    throw new Error(`Path must start with "/" but got "${path}"`);
  }
  return `${normalizeBaseUrl(baseUrl)}${path}`;
}

async function parseResponse(response) {
  const text = await response.text();
  if (!text) {
    return null;
  }
  try {
    return JSON.parse(text);
  } catch {
    return text;
  }
}

async function requestJson(path, options = {}, baseUrl) {
  const response = await fetch(buildUrl(path, baseUrl), options);
  const payload = await parseResponse(response);
  if (!response.ok) {
    const details = typeof payload === "string" ? payload : JSON.stringify(payload);
    throw new Error(`Request failed (${response.status}): ${details}`);
  }
  return payload;
}

async function postBinary(path, body, headers = {}, baseUrl) {
  const response = await fetch(buildUrl(path, baseUrl), {
    method: "POST",
    headers,
    body
  });
  const payload = await parseResponse(response);
  if (!response.ok) {
    const details = typeof payload === "string" ? payload : JSON.stringify(payload);
    throw new Error(`Request failed (${response.status}): ${details}`);
  }
  return payload;
}

export function createApiClient(baseUrl = DEFAULT_BASE_URL) {
  const resolvedBase = normalizeBaseUrl(baseUrl);

  return {
    getStatus() {
      return requestJson("/api/status", { method: "GET" }, resolvedBase);
    },
    listJobs() {
      return requestJson("/api/jobs", { method: "GET" }, resolvedBase);
    },
    uploadJobManifest(jobId, manifest) {
      return requestJson("/api/jobs/manifest", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ jobId, manifest })
      }, resolvedBase);
    },
    async uploadJobRaster(jobId, rasterData, chunkSize = 16 * 1024) {
      const totalSize = rasterData.byteLength;
      let offset = 0;
      while (offset < totalSize) {
        const end = Math.min(offset + chunkSize, totalSize);
        const chunk = rasterData.slice(offset, end);
        await postBinary("/api/jobs/raster", chunk, {
          "X-Job-Id": String(jobId),
          "X-Chunk-Offset": String(offset),
          "X-Chunk-Total": String(totalSize)
        }, resolvedBase);
        offset = end;
      }
      return { ok: true };
    },
    startJob(jobId) {
      return requestJson("/api/control/jobs/run", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ jobId })
      }, resolvedBase);
    },
    pause() {
      return requestJson("/api/control/jobs/pause", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: "{}"
      }, resolvedBase);
    },
    resume() {
      return requestJson("/api/control/jobs/resume", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: "{}"
      }, resolvedBase);
    },
    abort() {
      return requestJson("/api/control/jobs/abort", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: "{}"
      }, resolvedBase);
    }
  };
}
