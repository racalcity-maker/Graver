export function computeSceneViewport(canvas, scene) {
  const width = canvas.width;
  const height = canvas.height;
  const sceneWidth = Math.max(1, Number(scene.widthMm));
  const sceneHeight = Math.max(1, Number(scene.heightMm));
  const pad = 16;
  const scale = Math.min((width - (pad * 2)) / sceneWidth, (height - (pad * 2)) / sceneHeight);
  const drawWidth = sceneWidth * scale;
  const drawHeight = sceneHeight * scale;
  const offsetX = (width - drawWidth) / 2;
  const offsetY = (height - drawHeight) / 2;

  return {
    width,
    height,
    sceneWidth,
    sceneHeight,
    scale,
    drawWidth,
    drawHeight,
    offsetX,
    offsetY
  };
}

export function mmRectToCanvasRect(viewport, bbox) {
  return {
    x: viewport.offsetX + (bbox.x * viewport.scale),
    y: viewport.offsetY + (bbox.y * viewport.scale),
    w: Math.max(1, bbox.w * viewport.scale),
    h: Math.max(1, bbox.h * viewport.scale)
  };
}

export function canvasPointToMm(viewport, point) {
  return {
    x: (point.x - viewport.offsetX) / viewport.scale,
    y: (point.y - viewport.offsetY) / viewport.scale
  };
}

export function getResizeHandlePoints(rect) {
  return [
    { id: "nw", x: rect.x, y: rect.y },
    { id: "ne", x: rect.x + rect.w, y: rect.y },
    { id: "sw", x: rect.x, y: rect.y + rect.h },
    { id: "se", x: rect.x + rect.w, y: rect.y + rect.h }
  ];
}

export function handleCursor(handleId) {
  if (handleId === "nw" || handleId === "se") {
    return "nwse-resize";
  }
  if (handleId === "ne" || handleId === "sw") {
    return "nesw-resize";
  }
  return "default";
}

export function hitTestResizeHandle(state, canvas, objectId, point, radiusPx = 8) {
  const object = state.objects.find((item) => item.id === objectId);
  if (!object) {
    return null;
  }
  const viewport = computeSceneViewport(canvas, state.scene);
  const rect = mmRectToCanvasRect(viewport, object.bbox);
  const handles = getResizeHandlePoints(rect);
  const maxDistSq = radiusPx * radiusPx;

  for (const handle of handles) {
    const dx = point.x - handle.x;
    const dy = point.y - handle.y;
    if ((dx * dx) + (dy * dy) <= maxDistSq) {
      return handle.id;
    }
  }

  return null;
}

export function clientToCanvasPoint(canvas, event) {
  const rect = canvas.getBoundingClientRect();
  const x = ((event.clientX - rect.left) / rect.width) * canvas.width;
  const y = ((event.clientY - rect.top) / rect.height) * canvas.height;
  return { x, y };
}

export function hitTestObjectId(state, canvas, point) {
  const viewport = computeSceneViewport(canvas, state.scene);
  const visibleLayerIds = new Set(
    state.layers.filter((layer) => layer.visible !== false).map((layer) => layer.id)
  );

  for (let i = state.objects.length - 1; i >= 0; i -= 1) {
    const object = state.objects[i];
    if (!visibleLayerIds.has(object.layerId)) {
      continue;
    }
    const rect = mmRectToCanvasRect(viewport, object.bbox);
    if (point.x >= rect.x && point.x <= rect.x + rect.w && point.y >= rect.y && point.y <= rect.y + rect.h) {
      return object.id;
    }
  }
  return null;
}
