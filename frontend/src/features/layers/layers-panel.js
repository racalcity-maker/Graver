import { deepClone } from "../../shared/lib/clone.js";

export function createLayersPanel(container, store) {
  if (!container) {
    return;
  }

  container.innerHTML = `
    <h2>Layers</h2>
    <div id="layers-list" class="stack"></div>
    <button id="add-layer-btn" class="btn" type="button">+ Layer</button>
  `;

  const listNode = container.querySelector("#layers-list");
  const addButton = container.querySelector("#add-layer-btn");

  addButton.addEventListener("click", () => {
    store.setState((state) => {
      const next = deepClone(state);
      const id = `layer-${Date.now()}`;
      next.layers.push({
        id,
        name: `Layer ${next.layers.length + 1}`,
        visible: true,
        locked: false,
        order: (next.layers.length + 1) * 10,
        operationId: next.operations[0]?.id ?? ""
      });
      return next;
    });
  });

  function render(state) {
    listNode.innerHTML = "";
    [...state.layers]
      .sort((a, b) => a.order - b.order)
      .forEach((layer) => {
        const row = document.createElement("label");
        row.className = "row";
        row.innerHTML = `
          <span>${layer.name}</span>
          <select data-layer-id="${layer.id}">
            ${state.operations
              .map((operation) => `<option value="${operation.id}" ${operation.id === layer.operationId ? "selected" : ""}>${operation.name}</option>`)
              .join("")}
          </select>
        `;
        listNode.appendChild(row);
      });

    listNode.querySelectorAll("select[data-layer-id]").forEach((select) => {
      select.addEventListener("change", (event) => {
        const layerId = event.currentTarget.dataset.layerId;
        const operationId = event.currentTarget.value;
        store.setState((prev) => {
          const next = deepClone(prev);
          const layer = next.layers.find((item) => item.id === layerId);
          if (layer) {
            layer.operationId = operationId;
          }
          return next;
        });
      });
    });
  }

  store.subscribe(render);
}
