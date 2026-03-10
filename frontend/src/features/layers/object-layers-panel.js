import { deepClone } from "../../shared/lib/clone.js";
import { deleteObjectFromState, updateObjectInState } from "../editor/object-actions.js";
import { reprocessImageObject } from "../import/image-object-update.js";
import { importRasterImageFile } from "../import/raster-import.js";
import { OPERATION_TYPES, getOperationPreset } from "../operations/catalog.js";
import {
  NEW_OBJECT_TYPES,
  PRIMITIVE_SHAPES,
  TEXT_FILL_MODES,
  TEXT_FONT_OPTIONS,
  buildObjectByType,
  defaultTextStyle,
  getObjectTypeConfig
} from "../objects/object-factory.js";
import {
  createObjectLayerAndOperation,
  ensureDedicatedLayerForObject,
  ensureDedicatedOperationForLayer,
  getLayerById,
  getOperationById
} from "./layer-object-utils.js";

function escapeHtml(value) {
  return String(value ?? "")
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;")
    .replaceAll("\"", "&quot;")
    .replaceAll("'", "&#39;");
}

function toNumber(value, fallback) {
  const parsed = Number(value);
  return Number.isFinite(parsed) ? parsed : fallback;
}

function clamp(value, minValue, maxValue) {
  return Math.max(minValue, Math.min(maxValue, value));
}

function ensureTextStyle(object) {
  if (!object.textStyle) {
    object.textStyle = defaultTextStyle();
  }
  return object.textStyle;
}

function ensurePrimitiveStyle(object) {
  if (!object.primitive) {
    object.primitive = { shape: "rect", strokeMm: 0.5, fillMode: "solid" };
  }
  if (!object.primitive.shape) {
    object.primitive.shape = "rect";
  }
  if (!object.primitive.fillMode) {
    object.primitive.fillMode = "solid";
  }
  if (!Number.isFinite(Number(object.primitive.strokeMm))) {
    object.primitive.strokeMm = 0.5;
  }
  return object.primitive;
}

function ensureLineStyle(object) {
  if (!object.line) {
    object.line = { strokeMm: 0.4, angleDeg: 0 };
  }
  if (!Number.isFinite(Number(object.line.strokeMm))) {
    object.line.strokeMm = 0.4;
  }
  if (!Number.isFinite(Number(object.line.angleDeg))) {
    object.line.angleDeg = 0;
  }
  return object.line;
}

function withObjectOperationState(prev, objectId, mutate) {
  const next = deepClone(prev);
  const layer = ensureDedicatedLayerForObject(next, objectId);
  if (!layer) {
    return prev;
  }
  const operation = ensureDedicatedOperationForLayer(next, layer.id);
  if (!operation) {
    return prev;
  }
  mutate({ next, layer, operation });
  return next;
}

function isTextObject(object) {
  return object.kind === "text" || object.source?.tag === "text";
}

function isLineObject(object) {
  return object.kind === "line";
}

function isPrimitiveObject(object) {
  return object.kind === "primitive" || object.kind === "shape";
}

function objectTypeLabel(object) {
  if (isTextObject(object)) {
    return "Text";
  }
  if (object.kind === "image") {
    return "Image";
  }
  if (isLineObject(object)) {
    return "Line";
  }
  if (isPrimitiveObject(object)) {
    return "Primitive";
  }
  return object.kind;
}

export function createObjectLayersPanel(container, store) {
  if (!container) {
    return;
  }

  container.innerHTML = `
    <div class="row panel-head">
      <h2>Objects</h2>
      <div class="row add-object-row">
        <select id="new-object-type">
          ${NEW_OBJECT_TYPES.map((item) => `<option value="${item.value}">${item.label}</option>`).join("")}
        </select>
        <button id="add-object-btn" class="btn btn-xs" type="button">+ Add</button>
      </div>
    </div>
    <p class="muted">Choose object type first. The editor shows only fields relevant to that object.</p>
    <input id="add-object-image-input" type="file" accept=".png,.jpg,.jpeg,.bmp,.webp,image/*" class="hidden" />
    <div id="object-layers-list" class="object-layers-list"></div>
  `;

  const listNode = container.querySelector("#object-layers-list");
  const addButton = container.querySelector("#add-object-btn");
  const addTypeNode = container.querySelector("#new-object-type");
  const addImageInput = container.querySelector("#add-object-image-input");
  const expandedObjectIds = new Set();

  function addTypedObject(type) {
    store.setState((prev) => {
      const next = deepClone(prev);
      const config = getObjectTypeConfig(type);
      const { layer } = createObjectLayerAndOperation(next, config.operationType);
      const object = buildObjectByType(type, next.objects.length + 1);
      object.layerId = layer.id;
      next.objects.push(object);
      if (!next.ui) {
        next.ui = {};
      }
      next.ui.selectedObjectId = object.id;
      expandedObjectIds.add(object.id);
      return next;
    });
  }

  addButton.addEventListener("click", () => {
    const type = addTypeNode.value || "text";
    if (type === "image") {
      addImageInput.click();
      return;
    }
    addTypedObject(type);
  });

  addImageInput.addEventListener("change", async (event) => {
    const file = event.currentTarget.files?.[0];
    if (!file) {
      return;
    }
    try {
      const current = store.getState();
      const next = deepClone(current);
      await importRasterImageFile(next, file, {
        widthMm: 30,
        heightMm: 30,
        dpi: 254,
        mode: "binary",
        threshold: 128
      });
      const created = next.objects.at(-1);
      if (created) {
        if (!next.ui) {
          next.ui = {};
        }
        next.ui.selectedObjectId = created.id;
        expandedObjectIds.add(created.id);
      }
      store.setState(next);
    } catch (error) {
      console.error(error);
      window.alert(error.message || "Image import failed.");
    } finally {
      addImageInput.value = "";
    }
  });

  async function applyImagePatch(objectId, patch) {
    try {
      const current = store.getState();
      const next = deepClone(current);
      const object = next.objects.find((item) => item.id === objectId);
      if (!object) {
        return;
      }
      await reprocessImageObject(object, patch);
      store.setState(next);
    } catch (error) {
      console.error(error);
      window.alert(error.message || "Failed to update image settings.");
    }
  }

  function render(state) {
    listNode.innerHTML = "";
    if (!state.objects.length) {
      listNode.innerHTML = `<div class="empty-panel">No objects yet. Add one or import SVG/image.</div>`;
      return;
    }

    const objects = state.objects.slice().reverse();
    for (const object of objects) {
      const selected = state.ui?.selectedObjectId === object.id;
      const layer = getLayerById(state, object.layerId);
      const operation = layer ? getOperationById(state, layer.operationId) : null;
      const opened = expandedObjectIds.has(object.id) || selected;
      const textObject = isTextObject(object);
      const lineObject = isLineObject(object);
      const primitiveObject = isPrimitiveObject(object);

      const textStyle = textObject ? ensureTextStyle(object) : defaultTextStyle();
      const primitive = primitiveObject ? ensurePrimitiveStyle(object) : { shape: "rect", strokeMm: 0.5, fillMode: "solid" };
      const lineStyle = lineObject ? ensureLineStyle(object) : { strokeMm: 0.4, angleDeg: 0 };

      const imageMode = object.image?.mode ?? "binary";
      const imageThreshold = object.image?.threshold ?? 128;
      const imageDpi = object.image?.dpi ?? 254;
      const operationType = operation?.type ?? "engrave_fill";
      const textContent = object.text ?? object.source?.text ?? "";

      const card = document.createElement("details");
      card.className = `object-layer-card${selected ? " selected" : ""}`;
      card.dataset.objectId = object.id;
      if (opened) {
        card.open = true;
      }

      card.innerHTML = `
        <summary class="object-layer-summary">
          <span class="object-layer-title">${escapeHtml(object.name || object.kind)}</span>
          <span class="object-layer-meta">${escapeHtml(objectTypeLabel(object))}</span>
        </summary>
        <div class="object-layer-body stack">
          <label class="row">
            <span>Name</span>
            <input data-object-field="name" data-object-id="${object.id}" type="text" value="${escapeHtml(object.name || "")}" />
          </label>
          <label class="row ${textObject ? "" : "hidden"} text-content-row">
            <span>Text</span>
            <textarea data-text-content="${object.id}" rows="3">${escapeHtml(textContent)}</textarea>
          </label>
          <label class="row ${textObject ? "" : "hidden"}">
            <span>Font</span>
            <select data-text-style="fontFamily" data-object-id="${object.id}">
              ${TEXT_FONT_OPTIONS.map((font) => `<option value="${font}" ${font === textStyle.fontFamily ? "selected" : ""}>${font}</option>`).join("")}
            </select>
          </label>
          <label class="row ${textObject ? "" : "hidden"}">
            <span>Size mm</span>
            <input data-text-style="fontSizeMm" data-object-id="${object.id}" type="number" min="1" max="40" step="0.1" value="${textStyle.fontSizeMm}" />
          </label>
          <label class="row ${textObject ? "" : "hidden"}">
            <span>Align</span>
            <select data-text-style="align" data-object-id="${object.id}">
              <option value="left" ${textStyle.align === "left" ? "selected" : ""}>Left</option>
              <option value="center" ${textStyle.align === "center" ? "selected" : ""}>Center</option>
              <option value="right" ${textStyle.align === "right" ? "selected" : ""}>Right</option>
            </select>
          </label>
          <label class="row ${textObject ? "" : "hidden"}">
            <span>Fill mode</span>
            <select data-text-style="fillMode" data-object-id="${object.id}">
              ${TEXT_FILL_MODES.map((item) => `<option value="${item.value}" ${item.value === textStyle.fillMode ? "selected" : ""}>${item.label}</option>`).join("")}
            </select>
          </label>
          <label class="row ${textObject ? "" : "hidden"}">
            <span>Line height</span>
            <input data-text-style="lineHeight" data-object-id="${object.id}" type="number" min="0.8" max="3" step="0.05" value="${textStyle.lineHeight}" />
          </label>
          <label class="row ${textObject ? "" : "hidden"}">
            <span>Bold</span>
            <input data-text-style-check="bold" data-object-id="${object.id}" type="checkbox" ${textStyle.bold ? "checked" : ""} />
          </label>
          <label class="row ${textObject ? "" : "hidden"}">
            <span>Italic</span>
            <input data-text-style-check="italic" data-object-id="${object.id}" type="checkbox" ${textStyle.italic ? "checked" : ""} />
          </label>
          <label class="row ${textObject && textStyle.fillMode === "hatch" ? "" : "hidden"}">
            <span>Hatch mm</span>
            <input data-text-style="hatchSpacingMm" data-object-id="${object.id}" type="number" min="0.2" max="5" step="0.1" value="${textStyle.hatchSpacingMm}" />
          </label>
          <label class="row ${textObject && textStyle.fillMode === "hatch" ? "" : "hidden"}">
            <span>Hatch angle</span>
            <input data-text-style="hatchAngleDeg" data-object-id="${object.id}" type="number" min="-180" max="180" step="1" value="${textStyle.hatchAngleDeg}" />
          </label>
          <label class="row ${lineObject ? "" : "hidden"}">
            <span>Line mm</span>
            <input data-line-style="strokeMm" data-object-id="${object.id}" type="number" min="0.1" max="8" step="0.1" value="${lineStyle.strokeMm}" />
          </label>
          <label class="row ${lineObject ? "" : "hidden"}">
            <span>Angle</span>
            <input data-line-style="angleDeg" data-object-id="${object.id}" type="number" min="-180" max="180" step="1" value="${lineStyle.angleDeg}" />
          </label>
          <label class="row ${primitiveObject ? "" : "hidden"}">
            <span>Shape</span>
            <select data-primitive-style="shape" data-object-id="${object.id}">
              ${PRIMITIVE_SHAPES.map((item) => `<option value="${item.value}" ${item.value === primitive.shape ? "selected" : ""}>${item.label}</option>`).join("")}
            </select>
          </label>
          <label class="row ${primitiveObject ? "" : "hidden"}">
            <span>Stroke mm</span>
            <input data-primitive-style="strokeMm" data-object-id="${object.id}" type="number" min="0.1" max="8" step="0.1" value="${primitive.strokeMm}" />
          </label>
          <label class="row ${primitiveObject ? "" : "hidden"}">
            <span>Fill mode</span>
            <select data-primitive-style="fillMode" data-object-id="${object.id}">
              ${TEXT_FILL_MODES.map((item) => `<option value="${item.value}" ${item.value === primitive.fillMode ? "selected" : ""}>${item.label}</option>`).join("")}
            </select>
          </label>
          <label class="row">
            <span>X mm</span>
            <input data-object-field="x" data-object-id="${object.id}" type="number" step="0.1" value="${object.bbox.x}" />
          </label>
          <label class="row">
            <span>Y mm</span>
            <input data-object-field="y" data-object-id="${object.id}" type="number" step="0.1" value="${object.bbox.y}" />
          </label>
          <label class="row">
            <span>W mm</span>
            <input data-object-field="w" data-object-id="${object.id}" type="number" min="0.1" step="0.1" value="${object.bbox.w}" />
          </label>
          <label class="row">
            <span>H mm</span>
            <input data-object-field="h" data-object-id="${object.id}" type="number" min="0.1" step="0.1" value="${object.bbox.h}" />
          </label>
          <label class="row">
            <span>Visible</span>
            <input data-layer-visible="${object.id}" type="checkbox" ${layer?.visible !== false ? "checked" : ""} />
          </label>
          <label class="row">
            <span>Operation</span>
            <select data-op-type="${object.id}">
              ${OPERATION_TYPES.map((item) => `<option value="${item.value}" ${item.value === operationType ? "selected" : ""}>${item.label}</option>`).join("")}
            </select>
          </label>
          <label class="row">
            <span>Speed mm/min</span>
            <input data-op-field="speedMmMin" data-object-id="${object.id}" type="number" min="1" value="${operation?.speedMmMin ?? 1200}" />
          </label>
          <label class="row">
            <span>Power (0..255)</span>
            <input data-op-field="power" data-object-id="${object.id}" type="number" min="0" max="255" value="${operation?.power ?? 100}" />
          </label>
          <label class="row">
            <span>Passes</span>
            <input data-op-field="passes" data-object-id="${object.id}" type="number" min="1" max="20" value="${operation?.passes ?? 1}" />
          </label>
          <label class="row">
            <span>Order</span>
            <input data-op-field="order" data-object-id="${object.id}" type="number" min="1" value="${operation?.order ?? 10}" />
          </label>
          <div class="stack ${object.kind === "image" && object.image ? "" : "hidden"}">
            <label class="row">
              <span>Image mode</span>
              <select data-image-field="mode" data-object-id="${object.id}">
                <option value="binary" ${imageMode === "binary" ? "selected" : ""}>Binary</option>
                <option value="grayscale" ${imageMode === "grayscale" ? "selected" : ""}>Grayscale</option>
                <option value="dither" ${imageMode === "dither" ? "selected" : ""}>Dither</option>
              </select>
            </label>
            <label class="row">
              <span>Threshold</span>
              <input data-image-field="threshold" data-object-id="${object.id}" type="number" min="1" max="254" step="1" value="${imageThreshold}" />
            </label>
            <label class="row">
              <span>DPI</span>
              <input data-image-field="dpi" data-object-id="${object.id}" type="number" min="25" step="1" value="${imageDpi}" />
            </label>
          </div>
          <div class="row">
            <span></span>
            <button data-delete-object="${object.id}" class="btn danger btn-xs" type="button">Delete</button>
          </div>
        </div>
      `;

      listNode.appendChild(card);
    }

    listNode.querySelectorAll(".object-layer-card").forEach((detailsNode) => {
      detailsNode.addEventListener("toggle", () => {
        const objectId = detailsNode.dataset.objectId;
        if (!objectId) {
          return;
        }
        if (detailsNode.open) {
          expandedObjectIds.add(objectId);
        } else {
          expandedObjectIds.delete(objectId);
        }
      });

      const summaryNode = detailsNode.querySelector(".object-layer-summary");
      summaryNode?.addEventListener("click", () => {
        const objectId = detailsNode.dataset.objectId;
        if (!objectId) {
          return;
        }
        store.setState((prev) => {
          const next = deepClone(prev);
          if (!next.ui) {
            next.ui = {};
          }
          next.ui.selectedObjectId = objectId;
          return next;
        });
      });
    });

    listNode.querySelectorAll("[data-object-field]").forEach((node) => {
      node.addEventListener("change", (event) => {
        const objectId = event.currentTarget.dataset.objectId;
        const field = event.currentTarget.dataset.objectField;
        const value = event.currentTarget.value;
        store.setState((prev) => {
          const next = deepClone(prev);
          const changed = updateObjectInState(next, objectId, { [field]: value });
          return changed ? next : prev;
        });
      });
    });

    listNode.querySelectorAll("[data-text-content]").forEach((node) => {
      node.addEventListener("change", (event) => {
        const objectId = event.currentTarget.dataset.textContent;
        const value = event.currentTarget.value ?? "";
        store.setState((prev) => {
          const next = deepClone(prev);
          const object = next.objects.find((item) => item.id === objectId);
          if (!object) {
            return prev;
          }
          object.text = value;
          if (object.source && typeof object.source === "object") {
            object.source.text = value;
          }
          if (isTextObject(object)) {
            const compact = value.replaceAll("\n", " ").trim();
            object.name = compact ? compact.slice(0, 48) : "Text";
          }
          return next;
        });
      });
    });

    listNode.querySelectorAll("[data-text-style]").forEach((node) => {
      node.addEventListener("change", (event) => {
        const objectId = event.currentTarget.dataset.objectId;
        const field = event.currentTarget.dataset.textStyle;
        const value = event.currentTarget.value;
        store.setState((prev) => {
          const next = deepClone(prev);
          const object = next.objects.find((item) => item.id === objectId);
          if (!object) {
            return prev;
          }
          const style = ensureTextStyle(object);
          if (field === "fontSizeMm") {
            style.fontSizeMm = clamp(toNumber(value, style.fontSizeMm), 1, 40);
          } else if (field === "lineHeight") {
            style.lineHeight = clamp(toNumber(value, style.lineHeight), 0.8, 3);
          } else if (field === "hatchSpacingMm") {
            style.hatchSpacingMm = clamp(toNumber(value, style.hatchSpacingMm), 0.2, 5);
          } else if (field === "hatchAngleDeg") {
            style.hatchAngleDeg = clamp(toNumber(value, style.hatchAngleDeg), -180, 180);
          } else {
            style[field] = value;
          }
          return next;
        });
      });
    });

    listNode.querySelectorAll("[data-text-style-check]").forEach((node) => {
      node.addEventListener("change", (event) => {
        const objectId = event.currentTarget.dataset.objectId;
        const field = event.currentTarget.dataset.textStyleCheck;
        const checked = Boolean(event.currentTarget.checked);
        store.setState((prev) => {
          const next = deepClone(prev);
          const object = next.objects.find((item) => item.id === objectId);
          if (!object) {
            return prev;
          }
          const style = ensureTextStyle(object);
          style[field] = checked;
          return next;
        });
      });
    });

    listNode.querySelectorAll("[data-line-style]").forEach((node) => {
      node.addEventListener("change", (event) => {
        const objectId = event.currentTarget.dataset.objectId;
        const field = event.currentTarget.dataset.lineStyle;
        const value = event.currentTarget.value;
        store.setState((prev) => {
          const next = deepClone(prev);
          const object = next.objects.find((item) => item.id === objectId);
          if (!object) {
            return prev;
          }
          const line = ensureLineStyle(object);
          if (field === "strokeMm") {
            line.strokeMm = clamp(toNumber(value, line.strokeMm), 0.1, 8);
          } else if (field === "angleDeg") {
            line.angleDeg = clamp(toNumber(value, line.angleDeg), -180, 180);
          }
          return next;
        });
      });
    });

    listNode.querySelectorAll("[data-primitive-style]").forEach((node) => {
      node.addEventListener("change", (event) => {
        const objectId = event.currentTarget.dataset.objectId;
        const field = event.currentTarget.dataset.primitiveStyle;
        const value = event.currentTarget.value;
        store.setState((prev) => {
          const next = deepClone(prev);
          const object = next.objects.find((item) => item.id === objectId);
          if (!object) {
            return prev;
          }
          const primitive = ensurePrimitiveStyle(object);
          if (field === "strokeMm") {
            primitive.strokeMm = clamp(toNumber(value, primitive.strokeMm), 0.1, 8);
          } else {
            primitive[field] = value;
          }
          return next;
        });
      });
    });

    listNode.querySelectorAll("[data-layer-visible]").forEach((node) => {
      node.addEventListener("change", (event) => {
        const objectId = event.currentTarget.dataset.layerVisible;
        const visible = Boolean(event.currentTarget.checked);
        store.setState((prev) => {
          const next = deepClone(prev);
          const layer = ensureDedicatedLayerForObject(next, objectId);
          if (!layer) {
            return prev;
          }
          layer.visible = visible;
          return next;
        });
      });
    });

    listNode.querySelectorAll("[data-op-type]").forEach((node) => {
      node.addEventListener("change", (event) => {
        const objectId = event.currentTarget.dataset.opType;
        const opType = event.currentTarget.value;
        store.setState((prev) => withObjectOperationState(prev, objectId, ({ operation }) => {
          const preset = getOperationPreset(opType);
          operation.type = preset.value;
          operation.name = preset.label;
          operation.speedMmMin = preset.speedMmMin;
          operation.power = preset.power;
          operation.passes = preset.passes;
          operation.order = preset.defaultOrder;
        }));
      });
    });

    listNode.querySelectorAll("[data-op-field]").forEach((node) => {
      node.addEventListener("change", (event) => {
        const objectId = event.currentTarget.dataset.objectId;
        const field = event.currentTarget.dataset.opField;
        const rawValue = event.currentTarget.value;
        store.setState((prev) => withObjectOperationState(prev, objectId, ({ operation }) => {
          if (field === "speedMmMin") {
            operation.speedMmMin = clamp(Math.round(toNumber(rawValue, operation.speedMmMin)), 1, 40000);
            return;
          }
          if (field === "power") {
            operation.power = clamp(Math.round(toNumber(rawValue, operation.power)), 0, 255);
            return;
          }
          if (field === "passes") {
            operation.passes = clamp(Math.round(toNumber(rawValue, operation.passes)), 1, 20);
            return;
          }
          if (field === "order") {
            operation.order = clamp(Math.round(toNumber(rawValue, operation.order)), 1, 999);
          }
        }));
      });
    });

    listNode.querySelectorAll("[data-image-field]").forEach((node) => {
      node.addEventListener("change", (event) => {
        const objectId = event.currentTarget.dataset.objectId;
        const field = event.currentTarget.dataset.imageField;
        const value = event.currentTarget.value;
        if (field === "mode") {
          applyImagePatch(objectId, { mode: value });
          return;
        }
        if (field === "threshold") {
          applyImagePatch(objectId, { threshold: clamp(Math.round(toNumber(value, 128)), 1, 254) });
          return;
        }
        if (field === "dpi") {
          applyImagePatch(objectId, { dpi: clamp(Math.round(toNumber(value, 254)), 25, 1200) });
        }
      });
    });

    listNode.querySelectorAll("[data-delete-object]").forEach((node) => {
      node.addEventListener("click", (event) => {
        const objectId = event.currentTarget.dataset.deleteObject;
        expandedObjectIds.delete(objectId);
        store.setState((prev) => {
          const next = deepClone(prev);
          const deleted = deleteObjectFromState(next, objectId);
          return deleted ? next : prev;
        });
      });
    });
  }

  store.subscribe(render);
}
