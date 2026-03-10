import { computeSceneViewport, getResizeHandlePoints, mmRectToCanvasRect } from "./scene-geometry.js";

function resolveLayerColor(state, layerId) {
  const layer = state.layers.find((item) => item.id === layerId);
  if (!layer) {
    return "#2f6a56";
  }
  const operation = state.operations.find((item) => item.id === layer.operationId);
  if (!operation) {
    return "#2f6a56";
  }
  if (operation.type === "cut_line") {
    return "#a73e2b";
  }
  if (operation.type === "engrave_fill") {
    return "#1f4e8d";
  }
  if (operation.type === "image_raster") {
    return "#6748a8";
  }
  return "#2f6a56";
}

const imageCache = new Map();

function getImagePreview(dataUrl) {
  if (!dataUrl) {
    return null;
  }
  const cached = imageCache.get(dataUrl);
  if (cached) {
    return cached;
  }
  const image = new Image();
  image.src = dataUrl;
  imageCache.set(dataUrl, image);
  return image;
}

function drawHatch(context, rect, color, spacingPx, angleDeg) {
  const angle = (angleDeg * Math.PI) / 180;
  const { x, y, w, h } = rect;
  const cx = x + (w / 2);
  const cy = y + (h / 2);
  const radius = Math.sqrt((w * w) + (h * h));
  const step = Math.max(4, spacingPx);

  context.save();
  context.translate(cx, cy);
  context.rotate(angle);
  context.strokeStyle = color;
  context.lineWidth = 1;
  for (let v = -radius; v <= radius; v += step) {
    context.beginPath();
    context.moveTo(-radius, v);
    context.lineTo(radius, v);
    context.stroke();
  }
  context.restore();
}

function drawTextObject(context, object, rect, viewportScale, color) {
  const text = String(object.text ?? object.source?.text ?? object.name ?? "Text");
  const style = object.textStyle || {};
  const fontMm = Number(style.fontSizeMm ?? 6);
  const fontPx = Math.max(8, fontMm * viewportScale);
  const lineHeight = Math.max(0.8, Number(style.lineHeight ?? 1.2));
  const fontFamily = style.fontFamily || "Arial";
  const fontWeight = style.bold ? "700 " : "";
  const fontStyle = style.italic ? "italic " : "";
  const align = style.align || "left";
  const fillMode = style.fillMode || "solid";
  const hatchMm = Number(style.hatchSpacingMm ?? 0.8);
  const hatchSpacingPx = Math.max(4, hatchMm * viewportScale);
  const hatchAngleDeg = Number(style.hatchAngleDeg ?? 45);
  const lines = text.split(/\r?\n/);

  context.save();
  context.beginPath();
  context.rect(rect.x, rect.y, rect.w, rect.h);
  context.clip();

  context.font = `${fontStyle}${fontWeight}${fontPx}px ${fontFamily}`;
  context.textAlign = align;
  context.textBaseline = "top";
  context.fillStyle = color;
  context.strokeStyle = color;
  context.lineWidth = Math.max(1, 0.15 * viewportScale);

  const x =
    align === "center" ? rect.x + (rect.w / 2) : align === "right" ? rect.x + rect.w - 2 : rect.x + 2;
  let y = rect.y + 2;
  const dy = fontPx * lineHeight;

  if (fillMode === "hatch") {
    context.save();
    context.globalAlpha = 0.25;
    for (const line of lines) {
      context.fillText(line, x, y);
      y += dy;
    }
    context.restore();
    y = rect.y + 2;
    for (const line of lines) {
      context.strokeText(line, x, y);
      y += dy;
    }
    drawHatch(context, rect, color, hatchSpacingPx, hatchAngleDeg);
  } else if (fillMode === "outline") {
    for (const line of lines) {
      context.strokeText(line, x, y);
      y += dy;
    }
  } else {
    for (const line of lines) {
      context.fillText(line, x, y);
      y += dy;
    }
  }

  context.restore();
}

function drawPrimitiveObject(context, object, rect, viewportScale, color) {
  const primitive = object.primitive || {};
  const shape = primitive.shape || "rect";
  const fillMode = primitive.fillMode || "solid";
  const strokeMm = Number(primitive.strokeMm ?? 0.5);
  const strokePx = Math.max(1, strokeMm * viewportScale);
  const { x, y, w, h } = rect;

  context.save();
  context.beginPath();
  if (shape === "circle") {
    context.ellipse(x + (w / 2), y + (h / 2), w / 2, h / 2, 0, 0, Math.PI * 2);
  } else if (shape === "triangle") {
    context.moveTo(x + (w / 2), y);
    context.lineTo(x + w, y + h);
    context.lineTo(x, y + h);
    context.closePath();
  } else if (shape === "diamond") {
    context.moveTo(x + (w / 2), y);
    context.lineTo(x + w, y + (h / 2));
    context.lineTo(x + (w / 2), y + h);
    context.lineTo(x, y + (h / 2));
    context.closePath();
  } else if (shape === "star") {
    const cx = x + (w / 2);
    const cy = y + (h / 2);
    const outer = Math.min(w, h) / 2;
    const inner = outer * 0.45;
    for (let i = 0; i < 10; i += 1) {
      const radius = i % 2 === 0 ? outer : inner;
      const angle = (-Math.PI / 2) + ((i * Math.PI) / 5);
      const px = cx + (Math.cos(angle) * radius);
      const py = cy + (Math.sin(angle) * radius);
      if (i === 0) {
        context.moveTo(px, py);
      } else {
        context.lineTo(px, py);
      }
    }
    context.closePath();
  } else {
    context.rect(x, y, w, h);
  }

  context.strokeStyle = color;
  context.fillStyle = `${color}33`;
  context.lineWidth = strokePx;
  if (fillMode === "solid") {
    context.fill();
    context.stroke();
  } else if (fillMode === "hatch") {
    context.save();
    context.clip();
    drawHatch(context, rect, color, Math.max(4, 0.7 * viewportScale), 45);
    context.restore();
    context.stroke();
  } else {
    context.stroke();
  }
  context.restore();
}

function drawLineObject(context, object, rect, viewportScale, color) {
  const line = object.line || {};
  const strokeMm = Number(line.strokeMm ?? 0.4);
  const angleDeg = Number(line.angleDeg ?? 0);
  const angle = (angleDeg * Math.PI) / 180;
  const cx = rect.x + (rect.w / 2);
  const cy = rect.y + (rect.h / 2);
  const len = Math.max(1, Math.max(rect.w, rect.h));
  const dx = Math.cos(angle) * (len / 2);
  const dy = Math.sin(angle) * (len / 2);

  context.save();
  context.strokeStyle = color;
  context.lineWidth = Math.max(1, strokeMm * viewportScale);
  context.beginPath();
  context.moveTo(cx - dx, cy - dy);
  context.lineTo(cx + dx, cy + dy);
  context.stroke();
  context.restore();
}

function drawWorkspaceGrid(context, viewport, scene) {
  const x0 = viewport.offsetX;
  const y0 = viewport.offsetY;
  const width = viewport.drawWidth;
  const height = viewport.drawHeight;
  const mmToPx = viewport.scale;

  let minorStepMm = 5;
  let majorStepMm = 10;
  if (minorStepMm * mmToPx < 7) {
    minorStepMm = 10;
    majorStepMm = 20;
  }

  context.save();
  context.beginPath();
  context.rect(x0, y0, width, height);
  context.clip();

  context.lineWidth = 1;
  for (let xMm = 0; xMm <= scene.widthMm + 0.001; xMm += minorStepMm) {
    const x = x0 + (xMm * mmToPx);
    const isMajor = Math.abs((xMm % majorStepMm)) < 0.001;
    context.strokeStyle = isMajor ? "#cfd8cc" : "#e7eee5";
    context.beginPath();
    context.moveTo(x, y0);
    context.lineTo(x, y0 + height);
    context.stroke();
  }

  for (let yMm = 0; yMm <= scene.heightMm + 0.001; yMm += minorStepMm) {
    const y = y0 + (yMm * mmToPx);
    const isMajor = Math.abs((yMm % majorStepMm)) < 0.001;
    context.strokeStyle = isMajor ? "#cfd8cc" : "#e7eee5";
    context.beginPath();
    context.moveTo(x0, y);
    context.lineTo(x0 + width, y);
    context.stroke();
  }

  context.restore();
}

export function renderSceneCanvas(canvas, state, options = {}) {
  if (!canvas) {
    return;
  }
  const context = canvas.getContext("2d");
  if (!context) {
    return;
  }

  const width = canvas.width;
  const height = canvas.height;
  context.clearRect(0, 0, width, height);
  context.fillStyle = "#f9fcf6";
  context.fillRect(0, 0, width, height);

  const viewport = computeSceneViewport(canvas, state.scene);
  const { drawWidth, drawHeight, offsetX, offsetY } = viewport;
  const selectedObjectId = options.selectedObjectId || state.ui?.selectedObjectId || null;
  const visibleLayerIds = new Set(
    state.layers.filter((layer) => layer.visible !== false).map((layer) => layer.id)
  );
  const showGrid = state.ui?.showGrid !== false;

  if (showGrid) {
    drawWorkspaceGrid(context, viewport, state.scene);
  }

  context.strokeStyle = "#9cad9a";
  context.lineWidth = 1;
  context.strokeRect(offsetX, offsetY, drawWidth, drawHeight);

  for (const object of state.objects) {
    if (!visibleLayerIds.has(object.layerId)) {
      continue;
    }
    const color = resolveLayerColor(state, object.layerId);
    const rect = mmRectToCanvasRect(viewport, object.bbox);
    const x = rect.x;
    const y = rect.y;
    const w = rect.w;
    const h = rect.h;

    if (object.kind === "image" && object.image?.previewDataUrl) {
      const previewImage = getImagePreview(object.image.previewDataUrl);
      if (previewImage?.complete && previewImage.naturalWidth > 0) {
        context.drawImage(previewImage, x, y, w, h);
      } else {
        context.fillStyle = "#ececec";
        context.fillRect(x, y, w, h);
      }
      context.strokeStyle = color;
      context.lineWidth = 1;
      context.strokeRect(x, y, w, h);
    } else if (object.kind === "text" || object.source?.tag === "text") {
      drawTextObject(context, object, rect, viewport.scale, color);
      context.strokeStyle = `${color}66`;
      context.lineWidth = 1;
      context.strokeRect(x, y, w, h);
    } else if (object.kind === "line") {
      drawLineObject(context, object, rect, viewport.scale, color);
      context.strokeStyle = `${color}55`;
      context.lineWidth = 1;
      context.strokeRect(x, y, w, h);
    } else if (object.kind === "primitive" || object.kind === "shape") {
      drawPrimitiveObject(context, object, rect, viewport.scale, color);
      context.strokeStyle = `${color}55`;
      context.lineWidth = 1;
      context.strokeRect(x, y, w, h);
    } else {
      context.fillStyle = `${color}22`;
      context.strokeStyle = color;
      context.lineWidth = 1.2;
      context.fillRect(x, y, w, h);
      context.strokeRect(x, y, w, h);
    }

    if (object.id === selectedObjectId) {
      context.strokeStyle = "#f3a41a";
      context.lineWidth = 2.2;
      context.strokeRect(x - 2, y - 2, w + 4, h + 4);

      const handles = getResizeHandlePoints(rect);
      context.fillStyle = "#f3a41a";
      context.strokeStyle = "#9a6a0d";
      context.lineWidth = 1;
      for (const handle of handles) {
        context.beginPath();
        context.arc(handle.x, handle.y, 4.5, 0, Math.PI * 2);
        context.fill();
        context.stroke();
      }
    }
  }
}
