import { uploadJobManifest, uploadJobRaster } from '/assets/api.js?v=20260307a';
import { loadImageFromFile, rasterizeToGray8 } from '/assets/image/rasterize.js?v=20260307a';
import { renderPrimitiveCanvas } from '/assets/image/primitives.js?v=20260307a';

const TEXT_FONT_STACKS = {
  arial: 'Arial, "Helvetica Neue", Helvetica, sans-serif',
  calibri: 'Calibri, Carlito, "Segoe UI", sans-serif',
  verdana: 'Verdana, Geneva, sans-serif',
  tahoma: 'Tahoma, "Segoe UI", sans-serif',
  trebuchet: '"Trebuchet MS", Verdana, sans-serif',
  georgia: 'Georgia, serif',
  times: '"Times New Roman", Times, serif',
  sans: 'system-ui, -apple-system, "Segoe UI", sans-serif',
  mono: '"Consolas", "Courier New", monospace',
};

const UPLOAD_STATE_KEY = 'lasergraver.upload.v1';
const SAVE_DEBOUNCE_MS = 450;

function mmPerPixel(sizeMm, sizePx) {
  return sizePx > 0 ? sizeMm / sizePx : 0;
}

function effectiveJobSizeMm(options) {
  if (options.sourceType === 'primitive') {
    return {
      widthMm: Number(options.primitiveWidthMm),
      heightMm: Number(options.primitiveHeightMm),
    };
  }
  if (options.sourceType === 'text') {
    return {
      widthMm: Number(options.textWidthMm),
      heightMm: Number(options.textHeightMm),
    };
  }

  return {
    widthMm: Number(options.widthMm),
    heightMm: Number(options.heightMm),
  };
}

function effectiveOutputSizeMm(options, rasterInfo) {
  const jobSize = effectiveJobSizeMm(options);
  if (options.sourceType !== 'image' || !rasterInfo?.crop) {
    return jobSize;
  }

  const sourceWidthPx = Math.max(1, Number(rasterInfo.crop.sourceWidthPx || rasterInfo.widthPx));
  const sourceHeightPx = Math.max(1, Number(rasterInfo.crop.sourceHeightPx || rasterInfo.heightPx));
  return {
    widthMm: jobSize.widthMm * (Number(rasterInfo.widthPx) / sourceWidthPx),
    heightMm: jobSize.heightMm * (Number(rasterInfo.heightPx) / sourceHeightPx),
  };
}

function effectiveRasterOptions(options) {
  const jobSize = effectiveJobSizeMm(options);
  return {
    ...options,
    widthMm: jobSize.widthMm,
    heightMm: jobSize.heightMm,
  };
}

function isBinaryRasterText(options) {
  return options.sourceType === 'text' && options.textRenderMode === 'raster';
}

function measureLetterspacedTextPx(context, text, letterSpacingPx) {
  let width = 0;
  const chars = Array.from(String(text || ''));
  chars.forEach((char, index) => {
    width += context.measureText(char).width;
    if (index < chars.length - 1) {
      width += letterSpacingPx;
    }
  });
  return width;
}

function drawLetterspacedTextPx(context, text, startX, startY, letterSpacingPx) {
  let cursorX = startX;
  const chars = Array.from(String(text || ''));
  chars.forEach((char, index) => {
    context.fillText(char, cursorX, startY);
    cursorX += context.measureText(char).width;
    if (index < chars.length - 1) {
      cursorX += letterSpacingPx;
    }
  });
}

function wrapParagraphPx(context, paragraph, maxWidthPx, letterSpacingPx) {
  const words = String(paragraph).split(/\s+/).filter(Boolean);
  if (words.length === 0) {
    return [''];
  }

  const wrapped = [];
  let current = '';
  for (const word of words) {
    const candidate = current ? `${current} ${word}` : word;
    const candidateWidth = measureLetterspacedTextPx(context, candidate, letterSpacingPx);
    if (candidateWidth <= maxWidthPx || !current) {
      current = candidate;
    } else {
      wrapped.push(current);
      current = word;
    }
  }

  if (current || wrapped.length === 0) {
    wrapped.push(current);
  }

  return wrapped;
}

function buildRasterTextCanvas(options) {
  const widthMm = Number(options.textWidthMm);
  const heightMm = Number(options.textHeightMm);
  const dpi = Number(options.dpi);
  const fontSizeMm = Number(options.textFontSizeMm);
  const lineSpacing = Math.max(0.8, Number(options.textLineSpacing));
  const letterSpacingMm = Math.max(0, Number(options.textLetterSpacing));
  const align = options.textAlign;
  const text = String(options.textValue || '').replace(/\r\n/g, '\n');
  const fontFamily = TEXT_FONT_STACKS[options.textFontFamily] || TEXT_FONT_STACKS.arial;

  const widthPx = Math.max(1, Math.round((widthMm / 25.4) * dpi));
  const heightPx = Math.max(1, Math.round((heightMm / 25.4) * dpi));
  const fontSizePx = Math.max(1, (fontSizeMm / 25.4) * dpi);
  const lineHeightPx = fontSizePx * lineSpacing;
  const letterSpacingPx = (letterSpacingMm / 25.4) * dpi;

  const canvas = document.createElement('canvas');
  canvas.width = widthPx;
  canvas.height = heightPx;
  const context = canvas.getContext('2d', { willReadFrequently: true });
  if (!context) {
    throw new Error('Canvas 2D context is unavailable.');
  }

  context.fillStyle = '#fff';
  context.fillRect(0, 0, widthPx, heightPx);
  context.strokeStyle = '#d9ded2';
  context.lineWidth = 1;
  context.strokeRect(0.5, 0.5, Math.max(0, widthPx - 1), Math.max(0, heightPx - 1));
  context.fillStyle = '#000';
  context.textBaseline = 'top';
  context.font = `${fontSizePx}px ${fontFamily}`;

  const paddingTopPx = fontSizePx * 0.1;
  const paragraphs = text.split('\n');
  const wrappedLines = paragraphs.flatMap((paragraph) => wrapParagraphPx(context, paragraph, widthPx, letterSpacingPx));

  wrappedLines.forEach((lineText, lineIndex) => {
    const lineWidthPx = measureLetterspacedTextPx(context, lineText, letterSpacingPx);
    let offsetX = 0;
    if (align === 'center') {
      offsetX = Math.max(0, (widthPx - lineWidthPx) / 2);
    } else if (align === 'right') {
      offsetX = Math.max(0, widthPx - lineWidthPx);
    }
    drawLetterspacedTextPx(context, lineText, offsetX, paddingTopPx + (lineIndex * lineHeightPx), letterSpacingPx);
  });

  return {
    canvas,
    widthPx,
    heightPx,
    lineCount: wrappedLines.length,
    overflowY: (paddingTopPx + (wrappedLines.length * lineHeightPx)) > heightPx,
  };
}

function buildBinaryTextCanvas(options) {
  const widthMm = Number(options.textWidthMm);
  const heightMm = Number(options.textHeightMm);
  const dpi = Number(options.dpi);
  const fontSizeMm = Number(options.textFontSizeMm);
  const lineSpacing = Math.max(0.8, Number(options.textLineSpacing));
  const letterSpacingMm = Math.max(0, Number(options.textLetterSpacing));
  const align = options.textAlign;
  const text = String(options.textValue || '').replace(/\r\n/g, '\n');
  const fontFamily = TEXT_FONT_STACKS.arial;

  const widthPx = Math.max(1, Math.round((widthMm / 25.4) * dpi));
  const heightPx = Math.max(1, Math.round((heightMm / 25.4) * dpi));
  const fontSizePx = Math.max(1, (fontSizeMm / 25.4) * dpi);
  const lineHeightPx = fontSizePx * lineSpacing;
  const letterSpacingPx = (letterSpacingMm / 25.4) * dpi;

  const canvas = document.createElement('canvas');
  canvas.width = widthPx;
  canvas.height = heightPx;
  const context = canvas.getContext('2d', { willReadFrequently: true });
  if (!context) {
    throw new Error('Canvas 2D context is unavailable.');
  }

  context.fillStyle = '#fff';
  context.fillRect(0, 0, widthPx, heightPx);
  context.strokeStyle = '#d9ded2';
  context.lineWidth = 1;
  context.strokeRect(0.5, 0.5, Math.max(0, widthPx - 1), Math.max(0, heightPx - 1));
  context.fillStyle = '#000';
  context.textBaseline = 'top';
  context.font = `${fontSizePx}px ${fontFamily}`;

  const paddingTopPx = fontSizePx * 0.1;
  const paragraphs = text.split('\n');
  const wrappedLines = paragraphs.flatMap((paragraph) => wrapParagraphPx(context, paragraph, widthPx, letterSpacingPx));

  wrappedLines.forEach((lineText, lineIndex) => {
    const lineWidthPx = measureLetterspacedTextPx(context, lineText, letterSpacingPx);
    let offsetX = 0;
    if (align === 'center') {
      offsetX = Math.max(0, (widthPx - lineWidthPx) / 2);
    } else if (align === 'right') {
      offsetX = Math.max(0, widthPx - lineWidthPx);
    }
    drawLetterspacedTextPx(context, lineText, offsetX, paddingTopPx + (lineIndex * lineHeightPx), letterSpacingPx);
  });

  const imageData = context.getImageData(0, 0, widthPx, heightPx);
  const mask = new Uint8Array(widthPx * heightPx);
  for (let i = 0, p = 0; i < imageData.data.length; i += 4, p += 1) {
    const r = imageData.data[i];
    const g = imageData.data[i + 1];
    const b = imageData.data[i + 2];
    const gray = Math.round((0.299 * r) + (0.587 * g) + (0.114 * b));
    const on = gray < 220 ? 1 : 0;
    mask[p] = on;
    const preview = on ? 0 : 255;
    imageData.data[i] = preview;
    imageData.data[i + 1] = preview;
    imageData.data[i + 2] = preview;
    imageData.data[i + 3] = 255;
  }
  context.putImageData(imageData, 0, 0);

  return {
    canvas,
    widthPx,
    heightPx,
    lineCount: wrappedLines.length,
    overflowY: (paddingTopPx + (wrappedLines.length * lineHeightPx)) > heightPx,
    mask,
  };
}

function pointKey(x, y) {
  return `${x},${y}`;
}

function simplifyClosedPath(points) {
  if (points.length < 4) {
    return points;
  }

  const simplified = [];
  for (let i = 0; i < points.length; i += 1) {
    const prev = points[(i - 1 + points.length) % points.length];
    const current = points[i];
    const next = points[(i + 1) % points.length];
    const sameX = prev[0] === current[0] && current[0] === next[0];
    const sameY = prev[1] === current[1] && current[1] === next[1];
    if (!sameX && !sameY) {
      simplified.push(current);
    }
  }
  return simplified.length >= 3 ? simplified : points;
}

function perpendicularDistance(point, start, end) {
  const dx = end[0] - start[0];
  const dy = end[1] - start[1];
  if (dx === 0 && dy === 0) {
    return Math.hypot(point[0] - start[0], point[1] - start[1]);
  }
  const numerator = Math.abs((dy * point[0]) - (dx * point[1]) + (end[0] * start[1]) - (end[1] * start[0]));
  const denominator = Math.hypot(dx, dy);
  return numerator / denominator;
}

function simplifyPathRdp(points, epsilon) {
  if (points.length <= 2) {
    return points;
  }

  let maxDistance = 0;
  let splitIndex = 0;
  for (let i = 1; i < points.length - 1; i += 1) {
    const distance = perpendicularDistance(points[i], points[0], points[points.length - 1]);
    if (distance > maxDistance) {
      maxDistance = distance;
      splitIndex = i;
    }
  }

  if (maxDistance <= epsilon) {
    return [points[0], points[points.length - 1]];
  }

  const left = simplifyPathRdp(points.slice(0, splitIndex + 1), epsilon);
  const right = simplifyPathRdp(points.slice(splitIndex), epsilon);
  return left.slice(0, -1).concat(right);
}

function quantizePoint(point, quantum) {
  return [
    Math.round(point[0] / quantum) * quantum,
    Math.round(point[1] / quantum) * quantum,
  ];
}

function traceMaskContours(mask, widthPx, heightPx, scaleX, scaleY) {
  const segments = [];
  function isOn(x, y) {
    if (x < 0 || y < 0 || x >= widthPx || y >= heightPx) {
      return false;
    }
    return mask[(y * widthPx) + x] === 1;
  }

  for (let y = 0; y < heightPx; y += 1) {
    for (let x = 0; x < widthPx; x += 1) {
      if (!isOn(x, y)) {
        continue;
      }
      if (!isOn(x, y - 1)) {
        segments.push([[x, y], [x + 1, y]]);
      }
      if (!isOn(x + 1, y)) {
        segments.push([[x + 1, y], [x + 1, y + 1]]);
      }
      if (!isOn(x, y + 1)) {
        segments.push([[x + 1, y + 1], [x, y + 1]]);
      }
      if (!isOn(x - 1, y)) {
        segments.push([[x, y + 1], [x, y]]);
      }
    }
  }

  const byStart = new Map();
  segments.forEach((segment, index) => {
    const key = pointKey(segment[0][0], segment[0][1]);
    if (!byStart.has(key)) {
      byStart.set(key, []);
    }
    byStart.get(key).push(index);
  });

  const used = new Uint8Array(segments.length);
  const contours = [];
  for (let i = 0; i < segments.length; i += 1) {
    if (used[i]) {
      continue;
    }
    const path = [];
    let currentIndex = i;
    while (!used[currentIndex]) {
      used[currentIndex] = 1;
      const segment = segments[currentIndex];
      if (path.length === 0) {
        path.push(segment[0]);
      }
      path.push(segment[1]);
      const nextKey = pointKey(segment[1][0], segment[1][1]);
      const candidates = byStart.get(nextKey) || [];
      const nextIndex = candidates.find((candidate) => !used[candidate]);
      if (nextIndex === undefined) {
        break;
      }
      currentIndex = nextIndex;
    }

    if (path.length >= 4) {
      const closed = path[path.length - 1][0] === path[0][0] && path[path.length - 1][1] === path[0][1];
      const core = closed ? path.slice(0, -1) : path;
      const simplified = simplifyClosedPath(core);
      if (simplified.length >= 3) {
        const mmPath = simplified.map(([x, y]) => [x * scaleX, y * scaleY]);
        mmPath.push(mmPath[0]);
        contours.push(mmPath);
      }
    }
  }

  return contours;
}

function traceMaskHorizontalFill(mask, widthPx, heightPx, scaleX, scaleY, spacingPx) {
  const strokes = [];
  const step = Math.max(1, Math.round(spacingPx));
  for (let y = 0; y < heightPx; y += step) {
    let x = 0;
    while (x < widthPx) {
      while (x < widthPx && mask[(y * widthPx) + x] !== 1) {
        x += 1;
      }
      if (x >= widthPx) {
        break;
      }
      const startX = x;
      while (x < widthPx && mask[(y * widthPx) + x] === 1) {
        x += 1;
      }
      const endX = x;
      if ((endX - startX) >= 1) {
        const yMm = (y + 0.5) * scaleY;
        strokes.push([
          [startX * scaleX, yMm],
          [endX * scaleX, yMm],
        ]);
      }
    }
  }
  return strokes;
}

function buildVectorTextLayout(options) {
  const widthMm = Number(options.textWidthMm);
  const heightMm = Number(options.textHeightMm);
  const strokeMm = Math.max(0.1, Number(options.textStrokeMm));
  const traceOptions = {
    ...options,
    dpi: Math.min(Number(options.dpi), 96),
  };
  const rendered = buildBinaryTextCanvas(traceOptions);
  const scaleX = widthMm / rendered.widthPx;
  const scaleY = heightMm / rendered.heightPx;
  let strokes;
  if (options.textVectorMode === 'filled') {
    const spacingPx = Math.max(1, (strokeMm / Math.max(scaleY, 0.001)));
    strokes = traceMaskHorizontalFill(rendered.mask, rendered.widthPx, rendered.heightPx, scaleX, scaleY, spacingPx);
  } else {
    const rawContours = traceMaskContours(rendered.mask, rendered.widthPx, rendered.heightPx, scaleX, scaleY);
    const epsilonMm = Math.max(0.12, strokeMm * 0.35);
    const quantumMm = 0.05;
    strokes = rawContours
      .map((contour) => {
        const closed = contour.length > 2 && contour[0][0] === contour[contour.length - 1][0]
          && contour[0][1] === contour[contour.length - 1][1];
        const core = closed ? contour.slice(0, -1) : contour.slice();
        const simplified = simplifyPathRdp(core, epsilonMm).map((point) => quantizePoint(point, quantumMm));
        const deduped = simplified.filter((point, index, array) => {
          if (index === 0) {
            return true;
          }
          return point[0] !== array[index - 1][0] || point[1] !== array[index - 1][1];
        });
        if (deduped.length < 3) {
          return null;
        }
        deduped.push(deduped[0]);
        return deduped;
      })
      .filter(Boolean);
  }

  const previewCanvas = document.createElement('canvas');
  previewCanvas.width = rendered.widthPx;
  previewCanvas.height = rendered.heightPx;
  const context = previewCanvas.getContext('2d');
  if (!context) {
    throw new Error('Canvas 2D context is unavailable.');
  }
  context.fillStyle = '#fff';
  context.fillRect(0, 0, previewCanvas.width, previewCanvas.height);
  context.strokeStyle = '#d9ded2';
  context.lineWidth = 1;
  context.strokeRect(0.5, 0.5, Math.max(0, previewCanvas.width - 1), Math.max(0, previewCanvas.height - 1));
  context.strokeStyle = '#111';
  context.lineCap = 'round';
  context.lineJoin = 'round';
  context.lineWidth = Math.max(1, (rendered.widthPx / Math.max(widthMm, 1)) * strokeMm);
  strokes.forEach((stroke) => {
    if (stroke.length < 2) {
      return;
    }
    context.beginPath();
    context.moveTo(stroke[0][0] / scaleX, stroke[0][1] / scaleY);
    for (let i = 1; i < stroke.length; i += 1) {
      context.lineTo(stroke[i][0] / scaleX, stroke[i][1] / scaleY);
    }
    context.stroke();
  });

  return {
    canvas: previewCanvas,
    widthPx: rendered.widthPx,
    heightPx: rendered.heightPx,
    strokes,
    lineCount: rendered.lineCount,
    overflowY: rendered.overflowY,
  };
}

export function initUpload(logLine, hooks = {}) {
  const fileInput = document.getElementById('upload-image');
  const sourceSwitch = document.getElementById('upload-source-switch');
  const sourceButtons = Array.from(document.querySelectorAll('[data-source-type]'));
  const previewButton = document.getElementById('upload-preview-btn');
  const uploadButton = document.getElementById('upload-send-btn');
  const resetCurrentButton = document.getElementById('upload-reset-current-btn');
  const resetAllButton = document.getElementById('upload-reset-all-btn');
  const previewCanvas = document.getElementById('upload-preview');
  const metaNode = document.getElementById('upload-meta');
  const imageControls = document.getElementById('image-controls');
  const primitiveControls = document.getElementById('primitive-controls');
  const textControls = document.getElementById('text-controls');
  const textRenderModeSelect = document.getElementById('text-render-mode');
  const textRasterFontWrap = document.getElementById('text-raster-font-family-wrap');
  const widthInput = document.getElementById('upload-width-mm');
  const heightInput = document.getElementById('upload-height-mm');

  const state = {
    image: null,
    raster: null,
  };
  let saveTimer = null;

  const defaults = {
    sourceType: 'image',
    common: {
      jobId: 'demo-001',
      dpi: 254,
      printSpeedMmMin: 1200,
      travelSpeedMmMin: 2400,
      overscanMm: 0,
      minPower: 0,
      maxPower: 255,
    },
      image: {
        toneMode: 'binary',
        widthMm: 30,
        heightMm: 30,
      },
    primitive: {
      shape: 'star',
      renderMode: 'vector-outline',
      widthMm: 20,
      heightMm: 20,
      strokeMm: 1,
    },
    text: {
      value: 'HELLO',
      renderMode: 'vector',
      vectorMode: 'outline',
      widthMm: 40,
      heightMm: 20,
      vectorFontFamily: 'sans',
      fontFamily: 'arial',
      fontSizeMm: 3,
      strokeMm: 0.6,
      align: 'center',
      lineSpacing: 1.4,
      letterSpacing: 0.2,
    },
  };

  function cloneDefaults() {
    return JSON.parse(JSON.stringify(defaults));
  }

  function loadSavedState() {
    try {
      const raw = window.localStorage.getItem(UPLOAD_STATE_KEY);
      if (!raw) {
        return cloneDefaults();
      }
      return {
        ...cloneDefaults(),
        ...JSON.parse(raw),
      };
    } catch {
      return cloneDefaults();
    }
  }

  function writeSavedState(nextState) {
    try {
      window.localStorage.setItem(UPLOAD_STATE_KEY, JSON.stringify(nextState));
    } catch {
      // Ignore storage quota errors in UI.
    }
  }

  function collectPersistentState() {
    const options = readOptions();
    return {
      sourceType: options.sourceType,
      common: {
        jobId: options.jobId,
        dpi: options.dpi,
        printSpeedMmMin: options.printSpeedMmMin,
        travelSpeedMmMin: options.travelSpeedMmMin,
        overscanMm: options.overscanMm,
        minPower: options.minPower,
        maxPower: options.maxPower,
      },
      image: {
        toneMode: options.imageToneMode,
        widthMm: options.widthMm,
        heightMm: options.heightMm,
      },
      primitive: {
        shape: options.primitiveShape,
        renderMode: options.primitiveRenderMode,
        widthMm: options.primitiveWidthMm,
        heightMm: options.primitiveHeightMm,
        strokeMm: options.primitiveStrokeMm,
      },
      text: {
        value: options.textValue,
        renderMode: options.textRenderMode,
        vectorMode: options.textVectorMode,
        widthMm: options.textWidthMm,
        heightMm: options.textHeightMm,
        vectorFontFamily: options.textVectorFontFamily,
        fontFamily: options.textFontFamily,
        fontSizeMm: options.textFontSizeMm,
        strokeMm: options.textStrokeMm,
        align: options.textAlign,
        lineSpacing: options.textLineSpacing,
        letterSpacing: options.textLetterSpacing,
      },
    };
  }

  function scheduleStateSave() {
    if (saveTimer !== null) {
      window.clearTimeout(saveTimer);
    }
    saveTimer = window.setTimeout(() => {
      writeSavedState(collectPersistentState());
      saveTimer = null;
    }, SAVE_DEBOUNCE_MS);
  }

  function invalidatePreparedJob(message = 'Parameters changed. Generate preview or upload to rebuild the job.') {
    state.raster = null;
    if (message) {
      metaNode.textContent = message;
    }
  }

  function setValue(id, value) {
    const node = document.getElementById(id);
    if (node) {
      node.value = value;
    }
  }

  function applySavedState(savedState) {
    const stateToApply = savedState || cloneDefaults();
    setValue('upload-job-id', stateToApply.common?.jobId ?? defaults.common.jobId);
    setValue('upload-dpi', stateToApply.common?.dpi ?? defaults.common.dpi);
    setValue('upload-print-speed', stateToApply.common?.printSpeedMmMin ?? defaults.common.printSpeedMmMin);
    setValue('upload-travel-speed', stateToApply.common?.travelSpeedMmMin ?? defaults.common.travelSpeedMmMin);
    setValue('upload-overscan', stateToApply.common?.overscanMm ?? defaults.common.overscanMm);
    setValue('upload-min-power', stateToApply.common?.minPower ?? defaults.common.minPower);
    setValue('upload-max-power', stateToApply.common?.maxPower ?? defaults.common.maxPower);

    setValue('upload-width-mm', stateToApply.image?.widthMm ?? defaults.image.widthMm);
    setValue('upload-height-mm', stateToApply.image?.heightMm ?? defaults.image.heightMm);
    setValue('image-tone-mode', stateToApply.image?.toneMode ?? defaults.image.toneMode);

    setValue('primitive-shape', stateToApply.primitive?.shape ?? defaults.primitive.shape);
    setValue('primitive-render-mode', stateToApply.primitive?.renderMode ?? defaults.primitive.renderMode);
    setValue('primitive-width-mm', stateToApply.primitive?.widthMm ?? defaults.primitive.widthMm);
    setValue('primitive-height-mm', stateToApply.primitive?.heightMm ?? defaults.primitive.heightMm);
    setValue('primitive-stroke-mm', stateToApply.primitive?.strokeMm ?? defaults.primitive.strokeMm);

    setValue('text-value', stateToApply.text?.value ?? defaults.text.value);
    setValue('text-render-mode', stateToApply.text?.renderMode ?? defaults.text.renderMode);
    setValue('text-vector-mode', stateToApply.text?.vectorMode ?? defaults.text.vectorMode);
    setValue('text-width-mm', stateToApply.text?.widthMm ?? defaults.text.widthMm);
    setValue('text-height-mm', stateToApply.text?.heightMm ?? defaults.text.heightMm);
    setValue('text-vector-font-family', stateToApply.text?.vectorFontFamily ?? defaults.text.vectorFontFamily);
    setValue('text-font-family', stateToApply.text?.fontFamily ?? defaults.text.fontFamily);
    setValue('text-font-size-mm', stateToApply.text?.fontSizeMm ?? defaults.text.fontSizeMm);
    setValue('text-stroke-mm', stateToApply.text?.strokeMm ?? defaults.text.strokeMm);
    setValue('text-align', stateToApply.text?.align ?? defaults.text.align);
    setValue('text-line-spacing', stateToApply.text?.lineSpacing ?? defaults.text.lineSpacing);
    setValue('text-letter-spacing', stateToApply.text?.letterSpacing ?? defaults.text.letterSpacing);

    setSourceType(stateToApply.sourceType ?? defaults.sourceType, false);
    syncTextModeUi();
  }

  async function ensureImageLoaded() {
    const [file] = fileInput.files || [];
    if (!file) {
      throw new Error('Select an image first.');
    }
    if (state.image && state.image.__fileName === file.name && state.image.__fileSize === file.size) {
      return state.image;
    }

    const image = await loadImageFromFile(file);
    image.__fileName = file.name;
    image.__fileSize = file.size;
    state.image = image;
    state.raster = null;
    return image;
  }

  function readOptions() {
    return {
      sourceType: sourceButtons.find((button) => button.classList.contains('active'))?.dataset.sourceType || 'image',
      jobId: document.getElementById('upload-job-id').value.trim(),
      widthMm: Number(document.getElementById('upload-width-mm').value),
      heightMm: Number(document.getElementById('upload-height-mm').value),
      imageToneMode: document.getElementById('image-tone-mode').value,
      dpi: Number(document.getElementById('upload-dpi').value),
      printSpeedMmMin: Number(document.getElementById('upload-print-speed').value),
      travelSpeedMmMin: Number(document.getElementById('upload-travel-speed').value),
      overscanMm: Number(document.getElementById('upload-overscan').value),
      minPower: Number(document.getElementById('upload-min-power').value),
      maxPower: Number(document.getElementById('upload-max-power').value),
      primitiveShape: document.getElementById('primitive-shape').value,
      primitiveRenderMode: document.getElementById('primitive-render-mode').value,
      primitiveWidthMm: Number(document.getElementById('primitive-width-mm').value),
      primitiveHeightMm: Number(document.getElementById('primitive-height-mm').value),
      primitiveStrokeMm: Number(document.getElementById('primitive-stroke-mm').value),
      textValue: document.getElementById('text-value').value,
      textRenderMode: document.getElementById('text-render-mode').value,
      textVectorMode: document.getElementById('text-vector-mode').value,
      textWidthMm: Number(document.getElementById('text-width-mm').value),
      textHeightMm: Number(document.getElementById('text-height-mm').value),
      textVectorFontFamily: document.getElementById('text-vector-font-family').value,
      textFontFamily: document.getElementById('text-font-family').value,
      textFontSizeMm: Number(document.getElementById('text-font-size-mm').value),
      textStrokeMm: Number(document.getElementById('text-stroke-mm').value),
      textAlign: document.getElementById('text-align').value,
      textLineSpacing: Number(document.getElementById('text-line-spacing').value),
      textLetterSpacing: Number(document.getElementById('text-letter-spacing').value),
    };
  }

  function setSourceType(sourceType, shouldSave = true) {
    const usePrimitive = sourceType === 'primitive';
    const useText = sourceType === 'text';
    const useImage = sourceType === 'image';

    sourceButtons.forEach((button) => {
      button.classList.toggle('active', button.dataset.sourceType === sourceType);
    });
    imageControls.classList.toggle('hidden', !useImage);
    primitiveControls.classList.toggle('hidden', !usePrimitive);
    textControls.classList.toggle('hidden', !useText);
    fileInput.disabled = !useImage;
    widthInput.disabled = !useImage;
    heightInput.disabled = !useImage;
    invalidatePreparedJob(
      usePrimitive
        ? 'Primitive selected. Shape settings are restored automatically.'
        : useText
          ? 'Text selected. Text layout settings are restored automatically.'
          : 'Image selected. Pick a file and generate preview.'
    );
    if (shouldSave) {
      scheduleStateSave();
    }
  }

  function syncTextModeUi() {
    const isRaster = textRenderModeSelect.value === 'raster';
    textRasterFontWrap.classList.toggle('hidden', !isRaster);
  }

  function renderPreview(canvasSource) {
    const context = previewCanvas.getContext('2d');
    if (!context) {
      return;
    }
    context.imageSmoothingEnabled = false;
    previewCanvas.width = canvasSource.width;
    previewCanvas.height = canvasSource.height;
    context.clearRect(0, 0, previewCanvas.width, previewCanvas.height);
    context.drawImage(canvasSource, 0, 0);
  }

  function buildManifest(options, rasterInfo) {
    const jobSize = effectiveOutputSizeMm(options, rasterInfo);
    if (options.sourceType === 'primitive' && options.primitiveRenderMode === 'vector-outline') {
      return {
        version: 1,
        jobId: options.jobId,
        jobType: 'vector-primitive',
        source: {
          name: `${options.primitiveShape}.primitive`,
        },
        primitive: {
          shape: options.primitiveShape,
          widthMm: jobSize.widthMm,
          heightMm: jobSize.heightMm,
          strokeMm: options.primitiveStrokeMm,
          segmentsPerCircle: 72,
        },
        motion: {
          travelSpeedMmMin: options.travelSpeedMmMin,
          printSpeedMmMin: options.printSpeedMmMin,
        },
        laser: {
          power: options.maxPower,
        },
        origin: {
          mode: 'top-left',
        },
      };
    }

    if (options.sourceType === 'text' && options.textRenderMode === 'vector') {
      return {
        version: 1,
        jobId: options.jobId,
        jobType: 'vector-text',
        text: {
          value: options.textValue,
          widthMm: jobSize.widthMm,
          heightMm: jobSize.heightMm,
          strokeMm: options.textStrokeMm,
          strokes: rasterInfo.vectorStrokes,
        },
        motion: {
          travelSpeedMmMin: options.travelSpeedMmMin,
          printSpeedMmMin: options.printSpeedMmMin,
        },
        laser: {
          power: options.maxPower,
        },
        origin: {
          mode: 'top-left',
        },
      };
    }

    const binaryRasterText = isBinaryRasterText(options);
    const binaryRasterImage = options.sourceType === 'image' && options.imageToneMode === 'binary';
    const pixelWidthMm = mmPerPixel(jobSize.widthMm, rasterInfo.widthPx);
    const pixelHeightMm = mmPerPixel(jobSize.heightMm, rasterInfo.heightPx);
    return {
      version: 1,
      jobId: options.jobId,
      jobType: 'raster',
      source: {
        name: options.sourceType === 'primitive'
          ? `${options.primitiveShape}.primitive`
          : options.sourceType === 'text'
            ? `${options.textFontFamily || 'text'}.txt`
            : (state.image?.__fileName || 'upload'),
        widthPx: state.image?.naturalWidth || rasterInfo.widthPx,
        heightPx: state.image?.naturalHeight || rasterInfo.heightPx,
      },
      output: {
        widthMm: jobSize.widthMm,
        heightMm: jobSize.heightMm,
        dpi: options.dpi,
        pixelSizeUm: pixelWidthMm * 1000.0,
      },
      raster: {
        widthPx: rasterInfo.widthPx,
        heightPx: rasterInfo.heightPx,
        bytesPerPixel: 1,
        encoding: 'gray8-row-major',
        bidirectional: true,
        serpentine: true,
      },
      motion: {
        travelSpeedMmMin: options.travelSpeedMmMin,
        printSpeedMmMin: options.printSpeedMmMin,
        overscanMm: options.overscanMm,
        lineStepMm: pixelHeightMm,
      },
      laser: {
        minPower: (binaryRasterText || binaryRasterImage) ? 0 : options.minPower,
        maxPower: options.maxPower,
        pwmMode: (binaryRasterText || binaryRasterImage) ? 'binary' : 'dynamic',
      },
      origin: {
        mode: 'top-left',
      },
    };
  }

  async function generatePreview() {
    const options = readOptions();
    if (!options.jobId) {
      throw new Error('Job ID is required.');
    }

    let rasterInfo;
    const jobSize = effectiveOutputSizeMm(options, rasterInfo);
    const rasterOptions = effectiveRasterOptions(options);
    if (options.sourceType === 'primitive') {
      const widthPx = Math.max(1, Math.round((jobSize.widthMm / 25.4) * options.dpi));
      const heightPx = Math.max(1, Math.round((jobSize.heightMm / 25.4) * options.dpi));
      const primitiveCanvas = renderPrimitiveCanvas({
        shape: options.primitiveShape,
        widthPx,
        heightPx,
        outputWidthMm: jobSize.widthMm,
        outputHeightMm: jobSize.heightMm,
        shapeWidthMm: options.primitiveWidthMm,
        shapeHeightMm: options.primitiveHeightMm,
        strokeMm: options.primitiveStrokeMm,
      });
      if (options.primitiveRenderMode === 'vector-outline') {
        rasterInfo = {
          widthPx,
          heightPx,
          raster: new Uint8Array(0),
          previewCanvas: primitiveCanvas,
        };
      } else {
        rasterInfo = rasterizeToGray8(primitiveCanvas, rasterOptions);
      }
      state.image = null;
    } else if (options.sourceType === 'text') {
      if (options.textRenderMode === 'vector') {
        const renderedText = buildVectorTextLayout(options);
        rasterInfo = {
          widthPx: renderedText.widthPx,
          heightPx: renderedText.heightPx,
          raster: new Uint8Array(0),
          previewCanvas: renderedText.canvas,
          vectorStrokes: renderedText.strokes,
          textLayout: {
            lineCount: renderedText.lineCount,
            overflowY: renderedText.overflowY,
          },
        };
      } else {
        const renderedText = buildRasterTextCanvas(options);
        rasterInfo = rasterizeToGray8(renderedText.canvas, {
          ...rasterOptions,
          sourceType: 'text',
          textBinary: true,
        });
        rasterInfo.textLayout = {
          lineCount: renderedText.lineCount,
          overflowY: renderedText.overflowY,
        };
      }
      state.image = null;
    } else {
      const image = await ensureImageLoaded();
      rasterInfo = rasterizeToGray8(image, rasterOptions);
    }

    state.raster = rasterInfo;
    renderPreview(rasterInfo.previewCanvas);
    if (options.sourceType === 'primitive' && options.primitiveRenderMode === 'vector-outline') {
      metaNode.textContent =
        `Vector primitive ${options.primitiveShape}, width ${jobSize.widthMm} mm, height ${jobSize.heightMm} mm, stroke ${options.primitiveStrokeMm} mm, power ${options.maxPower}.`;
    } else if (options.sourceType === 'text') {
      metaNode.textContent =
        `${options.textRenderMode === 'vector' ? 'Vector' : 'Raster'} text box ${jobSize.widthMm} x ${jobSize.heightMm} mm, font ${options.textRenderMode === 'vector' ? options.textVectorFontFamily : options.textFontFamily}, size ${options.textFontSizeMm} mm, align ${options.textAlign}, line ${options.textLineSpacing}, letter ${options.textLetterSpacing}, power ${options.maxPower}${options.textRenderMode === 'vector' ? `, mode ${options.textVectorMode}, ${rasterInfo.vectorStrokes.length} strokes` : `, ${rasterInfo.widthPx} x ${rasterInfo.heightPx}, binary on/off laser`}.${rasterInfo.textLayout?.overflowY ? ' Warning: text exceeds box height.' : ''}`;
    } else {
      metaNode.textContent =
        `Raster ${rasterInfo.widthPx} x ${rasterInfo.heightPx}, width ${jobSize.widthMm.toFixed(2)} mm, height ${jobSize.heightMm.toFixed(2)} mm, DPI ${options.dpi}, tone ${options.imageToneMode}, crop ${rasterInfo.crop?.widthPx || rasterInfo.widthPx} x ${rasterInfo.crop?.heightPx || rasterInfo.heightPx}, ${rasterInfo.raster.byteLength} bytes.`;
    }
    logLine(`Preview generated for ${options.jobId}: ${rasterInfo.widthPx}x${rasterInfo.heightPx}.`);
  }

  async function uploadJob() {
    if (!state.raster) {
      await generatePreview();
    }

    const options = readOptions();
    if (!options.jobId) {
      throw new Error('Job ID is required.');
    }

    hooks.onUploadStart?.();
    const manifest = buildManifest(options, state.raster);
    try {
      await uploadJobManifest(options.jobId, manifest);
      if (!((options.sourceType === 'primitive' && options.primitiveRenderMode === 'vector-outline')
        || (options.sourceType === 'text' && options.textRenderMode === 'vector'))) {
        await uploadJobRaster(options.jobId, state.raster.raster);
      }
    } finally {
      hooks.onUploadDone?.();
    }
    hooks.onJobUploaded?.(options.jobId);
    logLine(`Job uploaded: ${options.jobId}.`);
  }

  previewButton.addEventListener('click', async () => {
    try {
      await generatePreview();
    } catch (error) {
      logLine(`Preview failed: ${error.message}`);
    }
  });

  uploadButton.addEventListener('click', async () => {
    try {
      await uploadJob();
    } catch (error) {
      logLine(`Upload failed: ${error.message}`);
    }
  });

  fileInput.addEventListener('change', () => {
    state.image = null;
    invalidatePreparedJob('Image selected. Generate preview to prepare raster.');
    scheduleStateSave();
  });

  sourceSwitch.addEventListener('click', (event) => {
    const button = event.target.closest('[data-source-type]');
    if (!button) {
      return;
    }
    setSourceType(button.dataset.sourceType);
  });

  const trackedFieldIds = [
    'upload-job-id',
    'upload-width-mm',
    'upload-height-mm',
    'image-tone-mode',
    'upload-dpi',
    'upload-print-speed',
    'upload-travel-speed',
    'upload-overscan',
    'upload-min-power',
    'upload-max-power',
    'primitive-shape',
    'primitive-render-mode',
    'primitive-width-mm',
    'primitive-height-mm',
    'primitive-stroke-mm',
    'text-value',
    'text-render-mode',
    'text-vector-mode',
    'text-width-mm',
    'text-height-mm',
    'text-vector-font-family',
    'text-font-family',
    'text-font-size-mm',
    'text-stroke-mm',
    'text-align',
    'text-line-spacing',
    'text-letter-spacing',
  ];

  trackedFieldIds.forEach((id) => {
    const node = document.getElementById(id);
    if (!node) {
      return;
    }
    const eventName = node.tagName === 'TEXTAREA' || node.tagName === 'INPUT' ? 'input' : 'change';
    node.addEventListener(eventName, () => {
      invalidatePreparedJob();
      scheduleStateSave();
    });
    if (eventName !== 'change') {
      node.addEventListener('change', () => {
        invalidatePreparedJob();
        scheduleStateSave();
      });
    }
  });

  textRenderModeSelect.addEventListener('change', () => {
    syncTextModeUi();
    invalidatePreparedJob();
    scheduleStateSave();
  });

  resetCurrentButton.addEventListener('click', () => {
    const saved = loadSavedState();
    const sourceType = readOptions().sourceType;
    if (sourceType === 'image') {
      saved.image = cloneDefaults().image;
    } else if (sourceType === 'primitive') {
      saved.primitive = cloneDefaults().primitive;
    } else {
      saved.text = cloneDefaults().text;
    }
    writeSavedState(saved);
    applySavedState(saved);
    invalidatePreparedJob();
    logLine(`Reset ${sourceType} settings.`);
  });

  resetAllButton.addEventListener('click', () => {
    const restored = cloneDefaults();
    writeSavedState(restored);
    applySavedState(restored);
    state.image = null;
    invalidatePreparedJob('Upload settings reset to defaults.');
    fileInput.value = '';
    logLine('Upload settings reset to defaults.');
  });

  applySavedState(loadSavedState());
}
