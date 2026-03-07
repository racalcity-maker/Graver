function clampPositiveInteger(value, fallback) {
  const parsed = Number(value);
  if (!Number.isFinite(parsed) || parsed <= 0) {
    return fallback;
  }
  return Math.max(1, Math.round(parsed));
}

function clampByte(value) {
  return Math.max(0, Math.min(255, value));
}

function applyFloydSteinberg(grayPixels, widthPx, heightPx) {
  const work = new Float32Array(grayPixels.length);
  for (let i = 0; i < grayPixels.length; i += 1) {
    work[i] = grayPixels[i];
  }

  const output = new Uint8Array(grayPixels.length);
  for (let y = 0; y < heightPx; y += 1) {
    for (let x = 0; x < widthPx; x += 1) {
      const index = (y * widthPx) + x;
      const oldPixel = work[index];
      const newPixel = oldPixel >= 128 ? 255 : 0;
      output[index] = newPixel;
      const error = oldPixel - newPixel;

      if (x + 1 < widthPx) {
        work[index + 1] += error * (7 / 16);
      }
      if (y + 1 < heightPx) {
        const nextRow = index + widthPx;
        if (x > 0) {
          work[nextRow - 1] += error * (3 / 16);
        }
        work[nextRow] += error * (5 / 16);
        if (x + 1 < widthPx) {
          work[nextRow + 1] += error * (1 / 16);
        }
      }
    }
  }

  return output;
}

export async function loadImageFromFile(file) {
  const url = URL.createObjectURL(file);
  try {
    const image = new Image();
    image.decoding = 'async';
    const loaded = await new Promise((resolve, reject) => {
      image.onload = () => resolve(image);
      image.onerror = () => reject(new Error('Failed to decode image.'));
      image.src = url;
    });
    return loaded;
  } finally {
    URL.revokeObjectURL(url);
  }
}

export function rasterizeToGray8(image, options) {
  const widthMm = Number(options.widthMm);
  const heightMm = Number(options.heightMm);
  const dpi = Number(options.dpi);
  if (!Number.isFinite(widthMm) || !Number.isFinite(heightMm) || !Number.isFinite(dpi) ||
      widthMm <= 0 || heightMm <= 0 || dpi <= 0) {
    throw new Error('Invalid raster size or DPI.');
  }

  const targetWidthPx = clampPositiveInteger((widthMm / 25.4) * dpi, 1);
  const targetHeightPx = clampPositiveInteger((heightMm / 25.4) * dpi, 1);

  const canvas = document.createElement('canvas');
  canvas.width = targetWidthPx;
  canvas.height = targetHeightPx;
  const context = canvas.getContext('2d', { willReadFrequently: true });
  if (!context) {
    throw new Error('Canvas 2D context is unavailable.');
  }

  const binaryImage = options?.sourceType === 'image' && options?.imageToneMode === 'binary';
  context.imageSmoothingEnabled = !binaryImage;
  context.drawImage(image, 0, 0, targetWidthPx, targetHeightPx);
  const imageData = context.getImageData(0, 0, targetWidthPx, targetHeightPx);
  const pixels = imageData.data;
  const grayValues = new Uint8Array(targetWidthPx * targetHeightPx);
  const binaryPrimitive = options?.sourceType === 'primitive';
  const binaryText = options?.sourceType === 'text' && options?.textBinary === true;
  const ditherImage = options?.sourceType === 'image' && options?.imageToneMode === 'dither';
  const threshold = binaryText ? 248 : (binaryImage ? 128 : 224);

  for (let i = 0, p = 0; i < pixels.length; i += 4, p += 1) {
    const r = pixels[i];
    const g = pixels[i + 1];
    const b = pixels[i + 2];
    let gray = Math.max(0, Math.min(255, Math.round((0.299 * r) + (0.587 * g) + (0.114 * b))));
    if (binaryPrimitive || binaryText || binaryImage) {
      gray = gray >= threshold ? 255 : 0;
    }
    grayValues[p] = gray;
  }

  const previewGray = ditherImage ? applyFloydSteinberg(grayValues, targetWidthPx, targetHeightPx) : grayValues;

  const shouldCrop = options?.sourceType === 'image';
  let cropMinX = 0;
  let cropMinY = 0;
  let cropMaxX = targetWidthPx - 1;
  let cropMaxY = targetHeightPx - 1;
  if (shouldCrop) {
    cropMinX = targetWidthPx;
    cropMinY = targetHeightPx;
    cropMaxX = -1;
    cropMaxY = -1;
    for (let y = 0; y < targetHeightPx; y += 1) {
      for (let x = 0; x < targetWidthPx; x += 1) {
        const gray = clampByte(previewGray[(y * targetWidthPx) + x]);
        if ((255 - gray) > 0) {
          cropMinX = Math.min(cropMinX, x);
          cropMinY = Math.min(cropMinY, y);
          cropMaxX = Math.max(cropMaxX, x);
          cropMaxY = Math.max(cropMaxY, y);
        }
      }
    }
    if (cropMaxX < cropMinX || cropMaxY < cropMinY) {
      cropMinX = 0;
      cropMinY = 0;
      cropMaxX = 0;
      cropMaxY = 0;
    }
  }

  const widthPx = Math.max(1, cropMaxX - cropMinX + 1);
  const heightPx = Math.max(1, cropMaxY - cropMinY + 1);
  const raster = new Uint8Array(widthPx * heightPx);
  const croppedCanvas = document.createElement('canvas');
  croppedCanvas.width = widthPx;
  croppedCanvas.height = heightPx;
  const croppedContext = croppedCanvas.getContext('2d', { willReadFrequently: true });
  if (!croppedContext) {
    throw new Error('Canvas 2D context is unavailable.');
  }
  const croppedImageData = croppedContext.createImageData(widthPx, heightPx);

  for (let y = 0; y < heightPx; y += 1) {
    for (let x = 0; x < widthPx; x += 1) {
      const sourceX = cropMinX + x;
      const sourceY = cropMinY + y;
      const sourceIndex = (sourceY * targetWidthPx) + sourceX;
      const gray = clampByte(previewGray[sourceIndex]);
      raster[(y * widthPx) + x] = 255 - gray;
      const pixelIndex = ((y * widthPx) + x) * 4;
      croppedImageData.data[pixelIndex] = gray;
      croppedImageData.data[pixelIndex + 1] = gray;
      croppedImageData.data[pixelIndex + 2] = gray;
      croppedImageData.data[pixelIndex + 3] = 255;
    }
  }

  for (let i = 0, p = 0; i < pixels.length; i += 4, p += 1) {
    const gray = clampByte(previewGray[p]);
    pixels[i] = gray;
    pixels[i + 1] = gray;
    pixels[i + 2] = gray;
    pixels[i + 3] = 255;
  }

  context.putImageData(imageData, 0, 0);
  croppedContext.putImageData(croppedImageData, 0, 0);

  return {
    widthPx,
    heightPx,
    raster,
    previewCanvas: croppedCanvas,
    crop: {
      xPx: cropMinX,
      yPx: cropMinY,
      widthPx,
      heightPx,
      sourceWidthPx: targetWidthPx,
      sourceHeightPx: targetHeightPx,
    },
  };
}
