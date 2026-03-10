import { exportSceneToRasterJob } from "./raster-scene-exporter.js";

function noop() {}

export async function uploadSceneJobOverWifi(apiClient, state, config, options = {}) {
  const onProgress = typeof options.onProgress === "function" ? options.onProgress : noop;
  const runAfterUpload = Boolean(options.runAfterUpload);

  onProgress("Preparing raster job from scene...");
  const exported = await exportSceneToRasterJob(state, config);
  const { manifest, rasterData } = exported;

  onProgress(`Uploading manifest (${manifest.raster.widthPx}x${manifest.raster.heightPx})...`);
  await apiClient.uploadJobManifest(manifest.jobId, manifest);

  onProgress(`Uploading raster data (${rasterData.byteLength} bytes)...`);
  await apiClient.uploadJobRaster(manifest.jobId, rasterData);

  if (runAfterUpload) {
    onProgress("Starting job...");
    await apiClient.startJob(manifest.jobId);
  }

  onProgress("Done.");
  return {
    jobId: manifest.jobId,
    widthPx: manifest.raster.widthPx,
    heightPx: manifest.raster.heightPx,
    bytes: rasterData.byteLength
  };
}
