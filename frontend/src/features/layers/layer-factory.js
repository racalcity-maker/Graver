export function findOrCreateLayerForOperation(state, operationId, namePrefix = "Layer") {
  const existing = state.layers.find(
    (item) => item.operationId === operationId && item.name.startsWith(namePrefix)
  );
  if (existing) {
    return existing.id;
  }

  const id = `layer-${Date.now()}-${Math.floor(Math.random() * 1000)}`;
  state.layers.push({
    id,
    name: `${namePrefix} ${state.layers.length + 1}`,
    visible: true,
    locked: false,
    order: (state.layers.length + 1) * 10,
    operationId
  });
  return id;
}
