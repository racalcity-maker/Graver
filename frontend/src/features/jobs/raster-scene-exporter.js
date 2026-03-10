function clamp(value, minValue, maxValue) {
  return Math.max(minValue, Math.min(maxValue, value));
}

function toNumber(value, fallback) {
  const parsed = Number(value);
  return Number.isFinite(parsed) ? parsed : fallback;
}

function normalizeBbox(bbox) {
  return {
    x: toNumber(bbox?.x, 0),
    y: toNumber(bbox?.y, 0),
    w: Math.max(0.1, toNumber(bbox?.w, 1)),
    h: Math.max(0.1, toNumber(bbox?.h, 1))
  };
}

function mmRectToPxRect(bbox, scene, widthPx, heightPx) {
  return {
    x: (bbox.x / scene.widthMm) * widthPx,
    y: (bbox.y / scene.heightMm) * heightPx,
    w: Math.max(1, (bbox.w / scene.widthMm) * widthPx),
    h: Math.max(1, (bbox.h / scene.heightMm) * heightPx)
  };
}

function drawHatch(context, rect, spacingPx, angleDeg) {
  const angle = (angleDeg * Math.PI) / 180;
  const cx = rect.x + (rect.w / 2);
  const cy = rect.y + (rect.h / 2);
  const radius = Math.sqrt((rect.w * rect.w) + (rect.h * rect.h));
  const step = Math.max(2, spacingPx);

  context.save();
  context.translate(cx, cy);
  context.rotate(angle);
  context.strokeStyle = "#ffffff";
  context.lineWidth = 1;
  for (let v = -radius; v <= radius; v += step) {
    context.beginPath();
    context.moveTo(-radius, v);
    context.lineTo(radius, v);
    context.stroke();
  }
  context.restore();
}

function drawTextMask(context, object, rect, scaleX, scaleY) {
  const style = object.textStyle || {};
  const text = String(object.text ?? object.source?.text ?? object.name ?? "");
  const fontSizePx = Math.max(8, toNumber(style.fontSizeMm, 6) * scaleY);
  const lineHeight = clamp(toNumber(style.lineHeight, 1.2), 0.8, 3);
  const fontFamily = style.fontFamily || "Arial";
  const fontWeight = style.bold ? "700 " : "";
  const fontStyle = style.italic ? "italic " : "";
  const align = style.align || "left";
  const fillMode = style.fillMode || "solid";
  const hatchSpacingPx = Math.max(2, toNumber(style.hatchSpacingMm, 0.8) * scaleY);
  const hatchAngleDeg = toNumber(style.hatchAngleDeg, 45);
  const lines = text.split(/\r?\n/);

  context.save();
  context.beginPath();
  context.rect(rect.x, rect.y, rect.w, rect.h);
  context.clip();
  context.fillStyle = "#ffffff";
  context.strokeStyle = "#ffffff";
  context.lineWidth = Math.max(1, 0.18 * ((scaleX + scaleY) * 0.5));
  context.textBaseline = "top";
  context.textAlign = align;
  context.font = `${fontStyle}${fontWeight}${fontSizePx}px ${fontFamily}`;

  const x = align === "center" ? rect.x + (rect.w / 2) : align === "right" ? rect.x + rect.w - 2 : rect.x + 2;
  let y = rect.y + 2;
  const dy = fontSizePx * lineHeight;

  if (fillMode === "outline") {
    for (const line of lines) {
      context.strokeText(line, x, y);
      y += dy;
    }
  } else {
    for (const line of lines) {
      context.fillText(line, x, y);
      y += dy;
    }
    if (fillMode === "hatch") {
      context.save();
      context.globalCompositeOperation = "source-atop";
      drawHatch(context, rect, hatchSpacingPx, hatchAngleDeg);
      context.restore();
    }
  }
  context.restore();
}

function drawPrimitiveMask(context, object, rect, scale) {
  const primitive = object.primitive || {};
  const shape = primitive.shape || "rect";
  const fillMode = primitive.fillMode || "solid";
  const strokePx = Math.max(1, toNumber(primitive.strokeMm, 0.5) * scale);

  context.save();
  context.beginPath();
  if (shape === "circle") {
    context.ellipse(rect.x + (rect.w / 2), rect.y + (rect.h / 2), rect.w / 2, rect.h / 2, 0, 0, Math.PI * 2);
  } else if (shape === "triangle") {
    context.moveTo(rect.x + (rect.w / 2), rect.y);
    context.lineTo(rect.x + rect.w, rect.y + rect.h);
    context.lineTo(rect.x, rect.y + rect.h);
    context.closePath();
  } else if (shape === "diamond") {
    context.moveTo(rect.x + (rect.w / 2), rect.y);
    context.lineTo(rect.x + rect.w, rect.y + (rect.h / 2));
    context.lineTo(rect.x + (rect.w / 2), rect.y + rect.h);
    context.lineTo(rect.x, rect.y + (rect.h / 2));
    context.closePath();
  } else if (shape === "star") {
    const cx = rect.x + (rect.w / 2);
    const cy = rect.y + (rect.h / 2);
    const outer = Math.min(rect.w, rect.h) / 2;
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
    context.rect(rect.x, rect.y, rect.w, rect.h);
  }

  context.strokeStyle = "#ffffff";
  context.fillStyle = "#ffffff";
  context.lineWidth = strokePx;
  if (fillMode === "outline") {
    context.stroke();
  } else {
    context.fill();
    if (fillMode === "hatch") {
      context.save();
      context.globalCompositeOperation = "source-atop";
      drawHatch(context, rect, Math.max(2, 0.8 * scale), 45);
      context.restore();
    }
    context.stroke();
  }
  context.restore();
}

function drawLineMask(context, object, rect, scale) {
  const line = object.line || {};
  const strokePx = Math.max(1, toNumber(line.strokeMm, 0.4) * scale);
  const angleDeg = toNumber(line.angleDeg, 0);
  const angle = (angleDeg * Math.PI) / 180;
  const cx = rect.x + (rect.w / 2);
  const cy = rect.y + (rect.h / 2);
  const len = Math.max(rect.w, rect.h);
  const dx = Math.cos(angle) * (len / 2);
  const dy = Math.sin(angle) * (len / 2);

  context.save();
  context.strokeStyle = "#ffffff";
  context.lineWidth = strokePx;
  context.beginPath();
  context.moveTo(cx - dx, cy - dy);
  context.lineTo(cx + dx, cy + dy);
  context.stroke();
  context.restore();
}

function drawMaskForObject(context, object, rect, scene, widthPx, heightPx) {
  const scaleX = widthPx / scene.widthMm;
  const scaleY = heightPx / scene.heightMm;
  const scale = (scaleX + scaleY) / 2;

  if (object.kind === "text" || object.source?.tag === "text") {
    drawTextMask(context, object, rect, scaleX, scaleY);
    return "regular";
  }
  if (object.kind === "line") {
    drawLineMask(context, object, rect, scale);
    return "regular";
  }
  if (object.kind === "primitive" || object.kind === "shape") {
    drawPrimitiveMask(context, object, rect, scale);
    return "regular";
  }
  context.fillStyle = "#ffffff";
  context.fillRect(rect.x, rect.y, rect.w, rect.h);
  return "regular";
}

function readMaskPower(imageData, mode, power) {
  const rgba = imageData.data;
  const out = new Uint8Array(rgba.length / 4);
  for (let i = 0, p = 0; i < rgba.length; i += 4, p += 1) {
    const gray = Math.round((0.299 * rgba[i]) + (0.587 * rgba[i + 1]) + (0.114 * rgba[i + 2]));
    const mask = mode === "image" ? (255 - gray) / 255 : gray / 255;
    out[p] = Math.max(0, Math.min(255, Math.round(mask * power)));
  }
  return out;
}

const previewImageCache = new Map();

function loadImageFromDataUrl(dataUrl) {
  const cached = previewImageCache.get(dataUrl);
  if (cached) {
    return cached;
  }
  const promise = new Promise((resolve, reject) => {
    const image = new Image();
    image.onload = () => resolve(image);
    image.onerror = () => reject(new Error("Failed to load image preview."));
    image.src = dataUrl;
  });
  previewImageCache.set(dataUrl, promise);
  return promise;
}

export async function exportSceneToRasterJob(state, config) {
  const sceneWidthMm = Math.max(1, toNumber(state.scene?.widthMm, 180));
  const sceneHeightMm = Math.max(1, toNumber(state.scene?.heightMm, 130));
  const dpi = Math.max(25, Math.round(toNumber(config?.dpi, 254)));
  const widthPx = Math.max(1, Math.round((sceneWidthMm / 25.4) * dpi));
  const heightPx = Math.max(1, Math.round((sceneHeightMm / 25.4) * dpi));
  const pixelSizeMm = sceneWidthMm / widthPx;
  const lineStepMm = sceneHeightMm / heightPx;

  const layerById = new Map(state.layers.map((layer) => [layer.id, layer]));
  const operationById = new Map(state.operations.map((operation) => [operation.id, operation]));
  const rasterData = new Uint8Array(widthPx * heightPx);

  const tmpCanvas = document.createElement("canvas");
  tmpCanvas.width = widthPx;
  tmpCanvas.height = heightPx;
  const tmpContext = tmpCanvas.getContext("2d", { willReadFrequently: true });
  if (!tmpContext) {
    throw new Error("Canvas 2D context is unavailable.");
  }

  const visibleObjects = state.objects.filter((object) => {
    const layer = layerById.get(object.layerId);
    return Boolean(layer && layer.visible !== false);
  });

  for (const object of visibleObjects) {
    const layer = layerById.get(object.layerId);
    const operation = layer ? operationById.get(layer.operationId) : null;
    const power = clamp(Math.round(toNumber(operation?.power, 180)), 0, 255);
    if (power <= 0) {
      continue;
    }

    const bbox = normalizeBbox(object.bbox);
    const rect = mmRectToPxRect(bbox, { widthMm: sceneWidthMm, heightMm: sceneHeightMm }, widthPx, heightPx);

    tmpContext.fillStyle = "#000000";
    tmpContext.clearRect(0, 0, widthPx, heightPx);
    tmpContext.fillRect(0, 0, widthPx, heightPx);
    let mode = "regular";
    if (object.kind === "image" && object.image?.previewDataUrl) {
      const preview = await loadImageFromDataUrl(object.image.previewDataUrl);
      tmpContext.drawImage(preview, rect.x, rect.y, rect.w, rect.h);
      mode = "image";
    } else {
      mode = drawMaskForObject(
        tmpContext,
        object,
        rect,
        { widthMm: sceneWidthMm, heightMm: sceneHeightMm },
        widthPx,
        heightPx
      );
    }
    const imageData = tmpContext.getImageData(0, 0, widthPx, heightPx);
    const itemPower = readMaskPower(imageData, mode, power);

    for (let i = 0; i < rasterData.length; i += 1) {
      if (itemPower[i] > rasterData[i]) {
        rasterData[i] = itemPower[i];
      }
    }
  }

  const jobId = String(config?.jobId || "studio-001").trim() || "studio-001";
  const manifest = {
    version: 1,
    jobId,
    jobType: "raster",
    source: {
      name: "studio-scene",
      widthPx,
      heightPx
    },
    output: {
      widthMm: sceneWidthMm,
      heightMm: sceneHeightMm,
      dpi,
      pixelSizeUm: pixelSizeMm * 1000
    },
    raster: {
      widthPx,
      heightPx,
      bytesPerPixel: 1,
      encoding: "gray8-row-major",
      bidirectional: true,
      serpentine: true
    },
    motion: {
      travelSpeedMmMin: Math.max(1, toNumber(config?.travelSpeedMmMin, 2400)),
      printSpeedMmMin: Math.max(1, toNumber(config?.printSpeedMmMin, 1200)),
      overscanMm: Math.max(0, toNumber(config?.overscanMm, 0)),
      lineStepMm
    },
    laser: {
      minPower: 0,
      maxPower: 255,
      pwmMode: "dynamic"
    },
    origin: {
      mode: "top-left"
    }
  };

  return { manifest, rasterData };
}
