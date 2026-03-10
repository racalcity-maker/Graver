function toNumber(value, fallback) {
  const number = Number(value);
  return Number.isFinite(number) ? number : fallback;
}

function clamp(value, minValue, maxValue) {
  if (value < minValue) {
    return minValue;
  }
  if (value > maxValue) {
    return maxValue;
  }
  return value;
}

function round3(value) {
  return Number(value.toFixed(3));
}

export function updateObjectInState(state, objectId, patch) {
  const object = state.objects.find((item) => item.id === objectId);
  if (!object) {
    return false;
  }

  if (typeof patch.name === "string") {
    const trimmed = patch.name.trim();
    object.name = trimmed.length ? trimmed.slice(0, 80) : object.name;
  }

  if (typeof patch.layerId === "string" && patch.layerId) {
    const layerExists = state.layers.some((layer) => layer.id === patch.layerId);
    if (layerExists) {
      object.layerId = patch.layerId;
    }
  }

  const sceneWidth = Math.max(1, Number(state.scene.widthMm));
  const sceneHeight = Math.max(1, Number(state.scene.heightMm));

  const nextWidth = clamp(toNumber(patch.w, object.bbox.w), 0.1, sceneWidth);
  const nextHeight = clamp(toNumber(patch.h, object.bbox.h), 0.1, sceneHeight);

  const maxX = Math.max(0, sceneWidth - nextWidth);
  const maxY = Math.max(0, sceneHeight - nextHeight);
  const nextX = clamp(toNumber(patch.x, object.bbox.x), 0, maxX);
  const nextY = clamp(toNumber(patch.y, object.bbox.y), 0, maxY);

  object.bbox.x = round3(nextX);
  object.bbox.y = round3(nextY);
  object.bbox.w = round3(nextWidth);
  object.bbox.h = round3(nextHeight);
  return true;
}

export function deleteObjectFromState(state, objectId) {
  const index = state.objects.findIndex((item) => item.id === objectId);
  if (index < 0) {
    return false;
  }
  state.objects.splice(index, 1);

  const usedLayerIds = new Set(state.objects.map((item) => item.layerId));
  state.layers = state.layers.filter((layer) => usedLayerIds.has(layer.id));

  const usedOperationIds = new Set(state.layers.map((layer) => layer.operationId));
  state.operations = state.operations.filter((operation) => usedOperationIds.has(operation.id));

  if (!state.ui) {
    state.ui = {};
  }
  if (state.ui.selectedObjectId === objectId) {
    state.ui.selectedObjectId = state.objects.at(-1)?.id ?? null;
  }
  return true;
}
