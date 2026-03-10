import { buildOperation } from "./catalog.js";

export function findOrCreateOperation(state, operationType) {
  let operation = state.operations.find((item) => item.type === operationType);
  if (operation) {
    return operation.id;
  }

  operation = buildOperation(operationType, state.operations.length + 1);
  state.operations.push(operation);
  return operation.id;
}
