import { deepClone } from "../../shared/lib/clone.js";
import { readTextFile } from "../../shared/lib/file-readers.js";
import { renderSceneCanvas } from "./scene-renderer.js";
import { appendImportedSvg } from "../import/svg-import.js";
import { importRasterImageFile } from "../import/raster-import.js";
import {
  canvasPointToMm,
  clientToCanvasPoint,
  computeSceneViewport,
  handleCursor,
  hitTestObjectId,
  hitTestResizeHandle
} from "./scene-geometry.js";

function clamp(value, minValue, maxValue) {
  return Math.max(minValue, Math.min(maxValue, value));
}

function round3(value) {
  return Number(value.toFixed(3));
}

function resizeBBoxFromHandle(startBBox, handle, pointerMm, sceneWidthMm, sceneHeightMm) {
  const minSize = 0.5;
  let left = startBBox.x;
  let top = startBBox.y;
  let right = startBBox.x + startBBox.w;
  let bottom = startBBox.y + startBBox.h;

  if (handle === "se") {
    right = clamp(pointerMm.x, left + minSize, sceneWidthMm);
    bottom = clamp(pointerMm.y, top + minSize, sceneHeightMm);
  } else if (handle === "sw") {
    left = clamp(pointerMm.x, 0, right - minSize);
    bottom = clamp(pointerMm.y, top + minSize, sceneHeightMm);
  } else if (handle === "ne") {
    right = clamp(pointerMm.x, left + minSize, sceneWidthMm);
    top = clamp(pointerMm.y, 0, bottom - minSize);
  } else if (handle === "nw") {
    left = clamp(pointerMm.x, 0, right - minSize);
    top = clamp(pointerMm.y, 0, bottom - minSize);
  }

  return {
    x: round3(left),
    y: round3(top),
    w: round3(right - left),
    h: round3(bottom - top)
  };
}

function isSvgFile(file) {
  if (!file) {
    return false;
  }
  const name = String(file.name || "").toLowerCase();
  const type = String(file.type || "").toLowerCase();
  return name.endsWith(".svg") || type.includes("image/svg");
}

export function createEditorView(container, store) {
  if (!container) {
    return;
  }

  container.innerHTML = `
    <h2>Editor</h2>
    <div class="toolbar">
      <button id="import-btn" class="btn" type="button">Import</button>
      <button id="open-upload-btn" class="btn alt" type="button">Upload</button>
      <button id="open-settings-btn" class="btn alt" type="button">Settings</button>
      <input id="import-file-input" type="file" accept=".svg,image/svg+xml,.png,.jpg,.jpeg,.bmp,.webp,image/*" class="hidden" />
    </div>
    <div class="scene-shell">
      <div id="scene-meta" class="scene-meta"></div>
      <canvas id="scene-canvas" width="1560" height="880" class="scene-canvas"></canvas>
    </div>
  `;

  const metaNode = container.querySelector("#scene-meta");
  const importButton = container.querySelector("#import-btn");
  const openUploadButton = container.querySelector("#open-upload-btn");
  const openSettingsButton = container.querySelector("#open-settings-btn");
  const importFileInput = container.querySelector("#import-file-input");
  const sceneCanvas = container.querySelector("#scene-canvas");
  let dragState = null;

  importButton.addEventListener("click", () => importFileInput.click());
  openUploadButton.addEventListener("click", () => {
    window.dispatchEvent(new CustomEvent("lasergraver:open-upload-modal"));
  });
  openSettingsButton.addEventListener("click", () => {
    window.dispatchEvent(new CustomEvent("lasergraver:open-settings-modal"));
  });

  sceneCanvas.addEventListener("mousedown", (event) => {
    const current = store.getState();
    const point = clientToCanvasPoint(sceneCanvas, event);
    const selectedObjectId = current.ui?.selectedObjectId || null;

    if (selectedObjectId) {
      const resizeHandle = hitTestResizeHandle(current, sceneCanvas, selectedObjectId, point);
      if (resizeHandle) {
        const selectedObject = current.objects.find((item) => item.id === selectedObjectId);
        if (!selectedObject) {
          return;
        }
        dragState = {
          mode: "resize",
          handle: resizeHandle,
          objectId: selectedObjectId,
          startBBox: { ...selectedObject.bbox },
          sceneWidthMm: current.scene.widthMm,
          sceneHeightMm: current.scene.heightMm
        };
        sceneCanvas.style.cursor = handleCursor(resizeHandle);
        return;
      }
    }

    const hitId = hitTestObjectId(current, sceneCanvas, point);

    if (!hitId) {
      store.setState((state) => {
        const next = deepClone(state);
        if (!next.ui) {
          next.ui = {};
        }
        next.ui.selectedObjectId = null;
        return next;
      });
      return;
    }

    store.setState((state) => {
      const next = deepClone(state);
      if (!next.ui) {
        next.ui = {};
      }
      next.ui.selectedObjectId = hitId;
      return next;
    });

    const selectedObject = current.objects.find((item) => item.id === hitId);
    if (!selectedObject) {
      return;
    }

    const viewport = computeSceneViewport(sceneCanvas, current.scene);
    dragState = {
      mode: "move",
      objectId: hitId,
      startCanvasX: point.x,
      startCanvasY: point.y,
      startBBox: { ...selectedObject.bbox },
      mmPerCanvasPx: 1 / viewport.scale
    };
  });

  window.addEventListener("mousemove", (event) => {
    if (!dragState) {
      return;
    }

    const current = store.getState();
    const point = clientToCanvasPoint(sceneCanvas, event);

    store.setState((state) => {
      const next = deepClone(state);
      const object = next.objects.find((item) => item.id === dragState.objectId);
      if (!object) {
        return state;
      }

      if (dragState.mode === "resize") {
        const viewport = computeSceneViewport(sceneCanvas, current.scene);
        const pointerMm = canvasPointToMm(viewport, point);
        object.bbox = resizeBBoxFromHandle(
          dragState.startBBox,
          dragState.handle,
          pointerMm,
          dragState.sceneWidthMm,
          dragState.sceneHeightMm
        );
        return next;
      }

      const dxMm = (point.x - dragState.startCanvasX) * dragState.mmPerCanvasPx;
      const dyMm = (point.y - dragState.startCanvasY) * dragState.mmPerCanvasPx;
      const maxX = Math.max(0, current.scene.widthMm - object.bbox.w);
      const maxY = Math.max(0, current.scene.heightMm - object.bbox.h);
      object.bbox.x = round3(Math.max(0, Math.min(maxX, dragState.startBBox.x + dxMm)));
      object.bbox.y = round3(Math.max(0, Math.min(maxY, dragState.startBBox.y + dyMm)));
      return next;
    });
  });

  sceneCanvas.addEventListener("mousemove", (event) => {
    if (dragState) {
      return;
    }
    const current = store.getState();
    const point = clientToCanvasPoint(sceneCanvas, event);
    const selectedObjectId = current.ui?.selectedObjectId || null;
    if (selectedObjectId) {
      const handle = hitTestResizeHandle(current, sceneCanvas, selectedObjectId, point);
      if (handle) {
        sceneCanvas.style.cursor = handleCursor(handle);
        return;
      }
    }
    const hitId = hitTestObjectId(current, sceneCanvas, point);
    sceneCanvas.style.cursor = hitId ? "move" : "default";
  });

  sceneCanvas.addEventListener("mouseleave", () => {
    if (!dragState) {
      sceneCanvas.style.cursor = "default";
    }
  });

  window.addEventListener("mouseup", () => {
    dragState = null;
    sceneCanvas.style.cursor = "default";
  });

  importFileInput.addEventListener("change", async (event) => {
    const file = event.currentTarget.files?.[0];
    if (!file) {
      return;
    }

    try {
      if (isSvgFile(file)) {
        const svgText = await readTextFile(file);
        store.setState((state) => {
          const next = deepClone(state);
          appendImportedSvg(next, svgText, { mode: "auto" });
          return next;
        });
      } else {
        const current = store.getState();
        const next = deepClone(current);
        await importRasterImageFile(next, file, {
          widthMm: 30,
          heightMm: 30,
          dpi: 254,
          mode: "binary",
          threshold: 128
        });
        store.setState(next);
      }
    } catch (error) {
      console.error(error);
      window.alert(error.message || "Import failed.");
    } finally {
      importFileInput.value = "";
    }
  });

  function render(state) {
    metaNode.textContent = `Workspace: ${state.scene.widthMm} x ${state.scene.heightMm} mm`;
    renderSceneCanvas(sceneCanvas, state, { selectedObjectId: state.ui?.selectedObjectId || null });
  }

  store.subscribe(render);
}
