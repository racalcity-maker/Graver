import { findOrCreateLayerForOperation } from "../layers/layer-factory.js";
import { findOrCreateOperation } from "../operations/operation-factory.js";
import { parseSvgObjects } from "./svg-parser.js";

export function appendImportedSvg(state, svgText, options = {}) {
  const parsed = parseSvgObjects(svgText, state.scene);
  if (!parsed.objects.length) {
    throw new Error("SVG has no drawable vector elements.");
  }

  const mode = options.mode || "auto";
  const layerByOperation = new Map();

  for (const parsedObject of parsed.objects) {
    const operationType = mode === "auto" ? parsedObject.operationHint : mode;
    const operationId = findOrCreateOperation(state, operationType);
    let layerId = layerByOperation.get(operationType);
    if (!layerId) {
      layerId = findOrCreateLayerForOperation(state, operationId, "SVG");
      layerByOperation.set(operationType, layerId);
    }

    state.objects.push({
      id: parsedObject.id,
      kind: parsedObject.kind,
      layerId,
      name: parsedObject.name,
      bbox: parsedObject.bbox,
      source: parsedObject.source,
      appearance: parsedObject.appearance
    });
  }
}
