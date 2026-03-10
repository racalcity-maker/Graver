import { loadImageFromDataUrl, readDataUrlFile } from "../../shared/lib/file-readers.js";
import { processImageTone } from "./image-tone.js";
import { appendImportedImage } from "./image-import.js";

export async function importRasterImageFile(state, file, options) {
  const sourceDataUrl = await readDataUrlFile(file);
  const image = await loadImageFromDataUrl(sourceDataUrl);
  const result = processImageTone(image, {
    widthMm: options.widthMm,
    heightMm: options.heightMm,
    dpi: options.dpi,
    mode: options.mode,
    threshold: options.threshold
  });
  appendImportedImage(state, result, { name: file.name, sourceDataUrl });
}
