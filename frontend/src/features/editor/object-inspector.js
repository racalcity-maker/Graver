import { deepClone } from "../../shared/lib/clone.js";
import { deleteObjectFromState, updateObjectInState } from "./object-actions.js";
import { reprocessImageObject } from "../import/image-object-update.js";

function setDisabled(node, disabled) {
  if (!node) {
    return;
  }
  node.disabled = disabled;
}

export function createObjectInspector(container, store) {
  if (!container) {
    return;
  }

  container.innerHTML = `
    <div class="row inspector-head">
      <h3>Object Inspector</h3>
      <button id="inspector-delete" type="button" class="btn danger">Delete</button>
    </div>
    <p id="inspector-hint" class="muted">Select an object to edit.</p>
    <div class="stack">
      <label class="row">
        <span>Name</span>
        <input id="inspector-name" type="text" />
      </label>
      <label class="row">
        <span>Layer</span>
        <select id="inspector-layer"></select>
      </label>
      <label class="row">
        <span>X mm</span>
        <input id="inspector-x" type="number" step="0.1" />
      </label>
      <label class="row">
        <span>Y mm</span>
        <input id="inspector-y" type="number" step="0.1" />
      </label>
      <label class="row">
        <span>W mm</span>
        <input id="inspector-w" type="number" step="0.1" min="0.1" />
      </label>
      <label class="row">
        <span>H mm</span>
        <input id="inspector-h" type="number" step="0.1" min="0.1" />
      </label>
      <div id="inspector-image-group" class="stack">
        <label class="row">
          <span>Image mode</span>
          <select id="inspector-image-mode">
            <option value="binary">Binary</option>
            <option value="grayscale">Grayscale</option>
            <option value="dither">Dither</option>
          </select>
        </label>
        <label class="row">
          <span>Threshold</span>
          <input id="inspector-image-threshold" type="number" min="1" max="254" step="1" />
        </label>
        <label class="row">
          <span>DPI</span>
          <input id="inspector-image-dpi" type="number" min="25" step="1" />
        </label>
      </div>
    </div>
  `;

  const hintNode = container.querySelector("#inspector-hint");
  const nameNode = container.querySelector("#inspector-name");
  const layerNode = container.querySelector("#inspector-layer");
  const xNode = container.querySelector("#inspector-x");
  const yNode = container.querySelector("#inspector-y");
  const wNode = container.querySelector("#inspector-w");
  const hNode = container.querySelector("#inspector-h");
  const imageGroupNode = container.querySelector("#inspector-image-group");
  const imageModeNode = container.querySelector("#inspector-image-mode");
  const imageThresholdNode = container.querySelector("#inspector-image-threshold");
  const imageDpiNode = container.querySelector("#inspector-image-dpi");
  const deleteButton = container.querySelector("#inspector-delete");

  let selectedObjectId = null;

  function applyPatch(patch) {
    if (!selectedObjectId) {
      return;
    }
    store.setState((prev) => {
      const next = deepClone(prev);
      const changed = updateObjectInState(next, selectedObjectId, patch);
      return changed ? next : prev;
    });
  }

  nameNode.addEventListener("change", () => applyPatch({ name: nameNode.value }));
  layerNode.addEventListener("change", () => applyPatch({ layerId: layerNode.value }));
  xNode.addEventListener("change", () => applyPatch({ x: xNode.value }));
  yNode.addEventListener("change", () => applyPatch({ y: yNode.value }));
  wNode.addEventListener("change", () => applyPatch({ w: wNode.value }));
  hNode.addEventListener("change", () => applyPatch({ h: hNode.value }));

  async function applyImagePatch(patch) {
    if (!selectedObjectId) {
      return;
    }
    try {
      const current = store.getState();
      const next = deepClone(current);
      const object = next.objects.find((item) => item.id === selectedObjectId);
      if (!object) {
        return;
      }
      await reprocessImageObject(object, patch);
      store.setState(next);
    } catch (error) {
      console.error(error);
      window.alert(error.message || "Failed to update image mode.");
    }
  }

  imageModeNode.addEventListener("change", () => applyImagePatch({ mode: imageModeNode.value }));
  imageThresholdNode.addEventListener("change", () => applyImagePatch({ threshold: Number(imageThresholdNode.value) }));
  imageDpiNode.addEventListener("change", () => applyImagePatch({ dpi: Number(imageDpiNode.value) }));

  deleteButton.addEventListener("click", () => {
    if (!selectedObjectId) {
      return;
    }
    store.setState((prev) => {
      const next = deepClone(prev);
      const deleted = deleteObjectFromState(next, selectedObjectId);
      return deleted ? next : prev;
    });
  });

  function render(state) {
    selectedObjectId = state.ui?.selectedObjectId || null;
    const selectedObject = state.objects.find((item) => item.id === selectedObjectId);
    const disabled = !selectedObject;

    setDisabled(nameNode, disabled);
    setDisabled(layerNode, disabled);
    setDisabled(xNode, disabled);
    setDisabled(yNode, disabled);
    setDisabled(wNode, disabled);
    setDisabled(hNode, disabled);
    setDisabled(imageModeNode, disabled);
    setDisabled(imageThresholdNode, disabled);
    setDisabled(imageDpiNode, disabled);
    setDisabled(deleteButton, disabled);

    layerNode.innerHTML = state.layers
      .map((layer) => `<option value="${layer.id}">${layer.name}</option>`)
      .join("");

    if (!selectedObject) {
      hintNode.textContent = "Select an object to edit.";
      nameNode.value = "";
      xNode.value = "";
      yNode.value = "";
      wNode.value = "";
      hNode.value = "";
      imageModeNode.value = "binary";
      imageThresholdNode.value = "128";
      imageDpiNode.value = "254";
      imageGroupNode.classList.add("hidden");
      return;
    }

    hintNode.textContent = `${selectedObject.kind} | id: ${selectedObject.id}`;
    nameNode.value = selectedObject.name || "";
    layerNode.value = selectedObject.layerId;
    xNode.value = String(selectedObject.bbox.x);
    yNode.value = String(selectedObject.bbox.y);
    wNode.value = String(selectedObject.bbox.w);
    hNode.value = String(selectedObject.bbox.h);

    if (selectedObject.kind === "image" && selectedObject.image) {
      imageGroupNode.classList.remove("hidden");
      imageModeNode.value = selectedObject.image.mode || "binary";
      imageThresholdNode.value = String(selectedObject.image.threshold ?? 128);
      imageDpiNode.value = String(selectedObject.image.dpi ?? 254);
    } else {
      imageGroupNode.classList.add("hidden");
    }
  }

  store.subscribe(render);
}
