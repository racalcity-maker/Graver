function safeNumber(value, fallback = 0) {
  const number = Number(value);
  return Number.isFinite(number) ? number : fallback;
}

function clamp(value, minValue, maxValue) {
  return Math.max(minValue, Math.min(maxValue, value));
}

function normalizeBbox(bbox) {
  const x = safeNumber(bbox?.x, 0);
  const y = safeNumber(bbox?.y, 0);
  const w = Math.max(0.1, safeNumber(bbox?.w, 1));
  const h = Math.max(0.1, safeNumber(bbox?.h, 1));
  return { x, y, w, h };
}

function perimeterForPrimitive(shape, w, h) {
  if (shape === "circle") {
    const a = w / 2;
    const b = h / 2;
    return Math.PI * (3 * (a + b) - Math.sqrt((3 * a + b) * (a + 3 * b)));
  }
  if (shape === "triangle") {
    return w + (2 * Math.sqrt(((w / 2) ** 2) + (h ** 2)));
  }
  if (shape === "diamond") {
    return 4 * Math.sqrt(((w / 2) ** 2) + ((h / 2) ** 2));
  }
  if (shape === "star") {
    return Math.max(w, h) * 5.5;
  }
  return 2 * (w + h);
}

function fillDistance(areaMm2, fillMode, hatchSpacingMm, fallbackMm) {
  if (fillMode === "outline") {
    return fallbackMm;
  }
  const spacing = fillMode === "hatch" ? clamp(hatchSpacingMm, 0.2, 5) : Math.max(0.2, hatchSpacingMm * 0.8);
  return Math.max(fallbackMm, areaMm2 / spacing);
}

function buildTextItem(object, bbox) {
  const style = object.textStyle || {};
  const text = String(object.text ?? object.source?.text ?? object.name ?? "");
  const compact = text.trim();
  const chars = Math.max(1, compact.replaceAll(/\s+/g, "").length);
  const fontSizeMm = clamp(safeNumber(style.fontSizeMm, 6), 1, 50);
  const fillMode = style.fillMode || "solid";
  const hatchSpacingMm = clamp(safeNumber(style.hatchSpacingMm, 0.8), 0.2, 5);
  const outlineDistance = Math.max(1, chars * fontSizeMm * 2.2);
  const area = bbox.w * bbox.h * 0.58;
  const distanceMm = fillDistance(area, fillMode, hatchSpacingMm, outlineDistance);

  return {
    item: {
      id: object.id,
      kind: "text",
      name: object.name || "Text",
      bbox,
      payload: {
        text,
        textStyle: {
          fontFamily: style.fontFamily || "Arial",
          fontSizeMm,
          align: style.align || "left",
          lineHeight: clamp(safeNumber(style.lineHeight, 1.2), 0.8, 3),
          bold: Boolean(style.bold),
          italic: Boolean(style.italic),
          fillMode,
          hatchSpacingMm,
          hatchAngleDeg: clamp(safeNumber(style.hatchAngleDeg, 45), -180, 180)
        }
      }
    },
    distanceMm,
    laserDistanceMm: distanceMm
  };
}

function buildPrimitiveItem(object, bbox) {
  const primitive = object.primitive || {};
  const shape = primitive.shape || "rect";
  const fillMode = primitive.fillMode || "solid";
  const strokeMm = clamp(safeNumber(primitive.strokeMm, 0.5), 0.1, 8);
  const perimeter = perimeterForPrimitive(shape, bbox.w, bbox.h);
  const area = bbox.w * bbox.h;
  const distanceMm = fillDistance(area, fillMode, strokeMm * 2.2, perimeter);

  return {
    item: {
      id: object.id,
      kind: "primitive",
      name: object.name || "Primitive",
      bbox,
      payload: {
        shape,
        strokeMm,
        fillMode
      }
    },
    distanceMm,
    laserDistanceMm: distanceMm
  };
}

function buildLineItem(object, bbox) {
  const line = object.line || {};
  const angleDeg = clamp(safeNumber(line.angleDeg, 0), -180, 180);
  const length = Math.max(0.2, Math.max(bbox.w, bbox.h));
  const strokeMm = clamp(safeNumber(line.strokeMm, 0.4), 0.1, 8);
  return {
    item: {
      id: object.id,
      kind: "line",
      name: object.name || "Line",
      bbox,
      payload: {
        strokeMm,
        angleDeg
      }
    },
    distanceMm: length,
    laserDistanceMm: length
  };
}

function buildImageItem(object, bbox) {
  const image = object.image || {};
  const widthPx = Math.max(1, Math.round(safeNumber(image.widthPx, bbox.w * 10)));
  const heightPx = Math.max(1, Math.round(safeNumber(image.heightPx, bbox.h * 10)));
  const dpi = Math.max(25, safeNumber(image.dpi, 254));
  const darkRatio = clamp(safeNumber(image.darkRatio, 0.5), 0, 1);
  const linePitchMm = 25.4 / dpi;
  const distanceMm = (heightPx * (bbox.w + linePitchMm));
  const laserDistanceMm = bbox.w * heightPx * darkRatio;

  return {
    item: {
      id: object.id,
      kind: "image",
      name: object.name || "Image",
      bbox,
      payload: {
        mode: image.mode || "binary",
        threshold: clamp(Math.round(safeNumber(image.threshold, 128)), 1, 254),
        dpi,
        widthPx,
        heightPx,
        darkRatio
      }
    },
    distanceMm,
    laserDistanceMm
  };
}

function buildPathItem(object, bbox) {
  const source = object.source || {};
  const distanceMm = 2 * (bbox.w + bbox.h);
  return {
    item: {
      id: object.id,
      kind: "path",
      name: object.name || "Path",
      bbox,
      payload: {
        source: {
          tag: source.tag || "",
          d: source.d || "",
          points: source.points || "",
          text: source.text || ""
        }
      }
    },
    distanceMm,
    laserDistanceMm: distanceMm
  };
}

export function buildJobItemForObject(object) {
  const bbox = normalizeBbox(object.bbox);
  if (object.kind === "image") {
    return buildImageItem(object, bbox);
  }
  if (object.kind === "text" || object.source?.tag === "text") {
    return buildTextItem(object, bbox);
  }
  if (object.kind === "line") {
    return buildLineItem(object, bbox);
  }
  if (object.kind === "primitive" || object.kind === "shape") {
    return buildPrimitiveItem(object, bbox);
  }
  return buildPathItem(object, bbox);
}
