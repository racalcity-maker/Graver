import { loadImageFromDataUrl } from "../../shared/lib/file-readers.js";
import { processImageTone } from "./image-tone.js";

export async function reprocessImageObject(object, patch = {}) {
  if (!object || object.kind !== "image" || !object.image?.sourceDataUrl) {
    throw new Error("Selected object is not a reprocessable image.");
  }

  const sourceImage = await loadImageFromDataUrl(object.image.sourceDataUrl);
  const nextMode = patch.mode ?? object.image.mode ?? "binary";
  const nextThreshold = patch.threshold ?? object.image.threshold ?? 128;
  const nextDpi = patch.dpi ?? object.image.dpi ?? 254;
  const widthMm = patch.widthMm ?? object.bbox.w;
  const heightMm = patch.heightMm ?? object.bbox.h;

  const processed = processImageTone(sourceImage, {
    widthMm,
    heightMm,
    dpi: nextDpi,
    mode: nextMode,
    threshold: nextThreshold
  });

  object.bbox.w = Number(processed.widthMm.toFixed(3));
  object.bbox.h = Number(processed.heightMm.toFixed(3));
  object.image.mode = processed.mode;
  object.image.threshold = processed.threshold;
  object.image.dpi = processed.dpi;
  object.image.widthPx = processed.widthPx;
  object.image.heightPx = processed.heightPx;
  object.image.darkRatio = Number(processed.darkRatio.toFixed(4));
  object.image.previewDataUrl = processed.previewDataUrl;
}
