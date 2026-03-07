function pxFromMm(mm, outputMm, outputPx, fallback = 1) {
  const value = (Number(mm) / Number(outputMm)) * Number(outputPx);
  if (!Number.isFinite(value) || value <= 0) {
    return fallback;
  }
  return value;
}

function regularPolygonPath(context, centerX, centerY, radiusX, radiusY, sides, rotationRad) {
  for (let index = 0; index < sides; index += 1) {
    const angle = rotationRad + ((Math.PI * 2 * index) / sides);
    const x = centerX + (Math.cos(angle) * radiusX);
    const y = centerY + (Math.sin(angle) * radiusY);
    if (index === 0) {
      context.moveTo(x, y);
    } else {
      context.lineTo(x, y);
    }
  }
  context.closePath();
}

function starPath(context, centerX, centerY, outerRadiusX, outerRadiusY, innerRatio = 0.45) {
  for (let index = 0; index < 10; index += 1) {
    const angle = (-Math.PI / 2) + ((Math.PI * index) / 5);
    const useOuter = (index % 2) === 0;
    const radiusX = useOuter ? outerRadiusX : outerRadiusX * innerRatio;
    const radiusY = useOuter ? outerRadiusY : outerRadiusY * innerRatio;
    const x = centerX + (Math.cos(angle) * radiusX);
    const y = centerY + (Math.sin(angle) * radiusY);
    if (index === 0) {
      context.moveTo(x, y);
    } else {
      context.lineTo(x, y);
    }
  }
  context.closePath();
}

function drawPrimitiveShape(context, shape, x, y, width, height) {
  const centerX = x + (width / 2);
  const centerY = y + (height / 2);
  const radiusX = width / 2;
  const radiusY = height / 2;

  context.beginPath();
  switch (shape) {
    case 'square':
      context.rect(x, y, width, height);
      break;
    case 'circle':
    case 'oval':
      context.ellipse(centerX, centerY, radiusX, radiusY, 0, 0, Math.PI * 2);
      break;
    case 'triangle':
      regularPolygonPath(context, centerX, centerY, radiusX, radiusY, 3, -Math.PI / 2);
      break;
    case 'octagon':
      regularPolygonPath(context, centerX, centerY, radiusX, radiusY, 8, Math.PI / 8);
      break;
    case 'diamond':
      regularPolygonPath(context, centerX, centerY, radiusX, radiusY, 4, 0);
      break;
    case 'star':
      starPath(context, centerX, centerY, radiusX, radiusY);
      break;
    default:
      context.rect(x, y, width, height);
      break;
  }
  context.stroke();
}

export function renderPrimitiveCanvas(options) {
  const widthPx = Number(options.widthPx);
  const heightPx = Number(options.heightPx);
  const outputWidthMm = Number(options.outputWidthMm);
  const outputHeightMm = Number(options.outputHeightMm);
  const shapeWidthPx = Math.max(1, Math.round(pxFromMm(options.shapeWidthMm, outputWidthMm, widthPx, widthPx * 0.8)));
  const shapeHeightPx = Math.max(1, Math.round(pxFromMm(options.shapeHeightMm, outputHeightMm, heightPx, heightPx * 0.8)));
  const strokePx = Math.max(1, Math.round(pxFromMm(options.strokeMm, outputWidthMm, widthPx, 1)));

  const canvas = document.createElement('canvas');
  canvas.width = widthPx;
  canvas.height = heightPx;
  const context = canvas.getContext('2d', { willReadFrequently: true });
  if (!context) {
    throw new Error('Canvas 2D context is unavailable.');
  }

  context.fillStyle = '#ffffff';
  context.fillRect(0, 0, widthPx, heightPx);
  context.strokeStyle = '#000000';
  context.lineWidth = strokePx;
  context.lineJoin = 'round';
  context.lineCap = 'round';

  const marginX = Math.max(strokePx, Math.round((widthPx - shapeWidthPx) / 2));
  const marginY = Math.max(strokePx, Math.round((heightPx - shapeHeightPx) / 2));
  const drawWidth = Math.max(1, widthPx - (marginX * 2));
  const drawHeight = Math.max(1, heightPx - (marginY * 2));

  drawPrimitiveShape(context, options.shape, marginX, marginY, drawWidth, drawHeight);
  return canvas;
}
