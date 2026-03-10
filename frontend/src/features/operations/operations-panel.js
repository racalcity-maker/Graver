import { deepClone } from "../../shared/lib/clone.js";
import { buildOperation, OPERATION_TYPES } from "./catalog.js";

function deleteOperationInState(state, operationId) {
  if (state.operations.length <= 1) {
    return false;
  }

  const index = state.operations.findIndex((item) => item.id === operationId);
  if (index < 0) {
    return false;
  }

  const fallback = state.operations.find((item) => item.id !== operationId);
  if (!fallback) {
    return false;
  }

  state.operations.splice(index, 1);
  state.layers.forEach((layer) => {
    if (layer.operationId === operationId) {
      layer.operationId = fallback.id;
    }
  });
  return true;
}

export function createOperationsPanel(container, store) {
  if (!container) {
    return;
  }

  container.innerHTML = `
    <h2>Operations</h2>
    <div id="operations-list" class="stack"></div>
    <button id="add-operation-btn" class="btn" type="button">+ Operation</button>
  `;

  const listNode = container.querySelector("#operations-list");
  const addButton = container.querySelector("#add-operation-btn");

  addButton.addEventListener("click", () => {
    store.setState((state) => {
      const next = deepClone(state);
      next.operations.push(buildOperation("engrave_fill", next.operations.length + 1));
      return next;
    });
  });

  function render(state) {
    listNode.innerHTML = "";
    state.operations
      .slice()
      .sort((a, b) => a.order - b.order)
      .forEach((operation) => {
        const canDelete = state.operations.length > 1;
        const card = document.createElement("div");
        card.className = "operation-card";
        card.innerHTML = `
          <div class="row operation-head">
            <strong>${operation.name}</strong>
            <button class="btn danger btn-xs" type="button" data-delete-op-id="${operation.id}" ${canDelete ? "" : "disabled"} title="${canDelete ? "Delete operation" : "At least one operation must remain"}">Delete</button>
          </div>
          <label class="row">
            <span>Name</span>
            <input data-field="name" data-op-id="${operation.id}" value="${operation.name}" />
          </label>
          <label class="row">
            <span>Type</span>
            <select data-field="type" data-op-id="${operation.id}">
              ${OPERATION_TYPES
                .map((item) => `<option value="${item.value}" ${item.value === operation.type ? "selected" : ""}>${item.label}</option>`)
                .join("")}
            </select>
          </label>
          <label class="row">
            <span>Speed mm/min</span>
            <input type="number" min="1" data-field="speedMmMin" data-op-id="${operation.id}" value="${operation.speedMmMin}" />
          </label>
          <label class="row">
            <span>Power (0..255)</span>
            <input type="number" min="0" max="255" data-field="power" data-op-id="${operation.id}" value="${operation.power}" />
          </label>
          <label class="row">
            <span>Passes</span>
            <input type="number" min="1" max="20" data-field="passes" data-op-id="${operation.id}" value="${operation.passes}" />
          </label>
          <label class="row">
            <span>Order</span>
            <input type="number" min="1" data-field="order" data-op-id="${operation.id}" value="${operation.order}" />
          </label>
        `;
        listNode.appendChild(card);
      });

    listNode.querySelectorAll("[data-delete-op-id]").forEach((button) => {
      button.addEventListener("click", (event) => {
        const operationId = event.currentTarget.dataset.deleteOpId;
        store.setState((prev) => {
          const next = deepClone(prev);
          const deleted = deleteOperationInState(next, operationId);
          return deleted ? next : prev;
        });
      });
    });

    listNode.querySelectorAll("[data-op-id]").forEach((node) => {
      node.addEventListener("change", (event) => {
        const target = event.currentTarget;
        const opId = target.dataset.opId;
        const field = target.dataset.field;
        const rawValue = target.value;
        store.setState((prev) => {
          const next = deepClone(prev);
          const operation = next.operations.find((item) => item.id === opId);
          if (!operation) {
            return prev;
          }
          if (field === "speedMmMin" || field === "power" || field === "passes" || field === "order") {
            operation[field] = Number(rawValue);
          } else {
            operation[field] = rawValue;
          }
          return next;
        });
      });
    });
  }

  store.subscribe(render);
}
