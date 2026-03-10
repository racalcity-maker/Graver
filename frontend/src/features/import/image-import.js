import { findOrCreateLayerForOperation } from "../layers/layer-factory.js";
import { findOrCreateOperation } from "../operations/operation-factory.js";

export function appendImportedImage(state, importResult, options) {
  const operationType = "image_raster";
  const operationId = findOrCreateOperation(state, operationType);
  const layerId = findOrCreateLayerForOperation(state, operationId, "IMG");

  const paddingMm = 4;
  const x = Math.max(0, Math.min(state.scene.widthMm - importResult.widthMm, paddingMm));
  const y = Math.max(0, Math.min(state.scene.heightMm - importResult.heightMm, paddingMm));

  state.objects.push({
    id: `img-${Date.now()}-${Math.floor(Math.random() * 1000)}`,
    kind: "image",
    layerId,
    name: options.name || "Raster Image",
    bbox: {
      x: Number(x.toFixed(3)),
      y: Number(y.toFixed(3)),
      w: Number(importResult.widthMm.toFixed(3)),
      h: Number(importResult.heightMm.toFixed(3))
    },
    image: {
      sourceDataUrl: options.sourceDataUrl || "",
      mode: importResult.mode,
      threshold: importResult.threshold,
      widthPx: importResult.widthPx,
      heightPx: importResult.heightPx,
      dpi: importResult.dpi,
      darkRatio: Number(importResult.darkRatio.toFixed(4)),
      previewDataUrl: importResult.previewDataUrl
    }
  });
}
