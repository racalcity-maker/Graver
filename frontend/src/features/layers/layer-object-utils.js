import { buildOperation } from "../operations/catalog.js";

function makeId(prefix) {
  return `${prefix}-${Date.now()}-${Math.floor(Math.random() * 1000)}`;
}

export function getObjectById(state, objectId) {
  return state.objects.find((item) => item.id === objectId) || null;
}

export function getLayerById(state, layerId) {
  return state.layers.find((item) => item.id === layerId) || null;
}

export function getOperationById(state, operationId) {
  return state.operations.find((item) => item.id === operationId) || null;
}

export function getLayerUsageCount(state, layerId) {
  return state.objects.filter((item) => item.layerId === layerId).length;
}

export function getOperationUsageCount(state, operationId) {
  return state.layers.filter((item) => item.operationId === operationId).length;
}

export function ensureDedicatedLayerForObject(state, objectId) {
  const object = getObjectById(state, objectId);
  if (!object) {
    return null;
  }

  const currentLayer = getLayerById(state, object.layerId);
  if (!currentLayer) {
    return null;
  }

  if (getLayerUsageCount(state, currentLayer.id) <= 1) {
    return currentLayer;
  }

  const clonedLayer = {
    ...currentLayer,
    id: makeId("layer"),
    name: `${currentLayer.name} copy`,
    order: (state.layers.length + 1) * 10
  };
  state.layers.push(clonedLayer);
  object.layerId = clonedLayer.id;
  return clonedLayer;
}

export function ensureDedicatedOperationForLayer(state, layerId) {
  const layer = getLayerById(state, layerId);
  if (!layer) {
    return null;
  }

  let operation = getOperationById(state, layer.operationId);
  if (!operation) {
    operation = buildOperation("engrave_fill", state.operations.length + 1);
    state.operations.push(operation);
    layer.operationId = operation.id;
    return operation;
  }

  if (getOperationUsageCount(state, operation.id) <= 1) {
    return operation;
  }

  const clone = {
    ...operation,
    id: makeId("op"),
    name: `${operation.name} copy`
  };
  state.operations.push(clone);
  layer.operationId = clone.id;
  return clone;
}

export function createObjectLayerAndOperation(state, operationType = "engrave_fill") {
  const operation = buildOperation(operationType, state.operations.length + 1);
  const layer = {
    id: makeId("layer"),
    name: `Layer ${state.layers.length + 1}`,
    visible: true,
    locked: false,
    order: (state.layers.length + 1) * 10,
    operationId: operation.id
  };

  state.operations.push(operation);
  state.layers.push(layer);

  return { layer, operation };
}
