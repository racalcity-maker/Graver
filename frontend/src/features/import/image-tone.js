function clampByte(value) {
  if (value < 0) {
    return 0;
  }
  if (value > 255) {
    return 255;
  }
  return value;
}

function toGray(r, g, b) {
  return Math.round((0.299 * r) + (0.587 * g) + (0.114 * b));
}

function applyBinary(imageData, threshold) {
  const data = imageData.data;
  let darkPixels = 0;
  for (let i = 0; i < data.length; i += 4) {
    const gray = toGray(data[i], data[i + 1], data[i + 2]);
    const value = gray < threshold ? 0 : 255;
    if (value === 0) {
      darkPixels += 1;
    }
    data[i] = value;
    data[i + 1] = value;
    data[i + 2] = value;
    data[i + 3] = 255;
  }
  return darkPixels;
}

function applyGrayscale(imageData) {
  const data = imageData.data;
  let darkPixels = 0;
  for (let i = 0; i < data.length; i += 4) {
    const gray = toGray(data[i], data[i + 1], data[i + 2]);
    if (gray < 128) {
      darkPixels += 1;
    }
    data[i] = gray;
    data[i + 1] = gray;
    data[i + 2] = gray;
    data[i + 3] = 255;
  }
  return darkPixels;
}

function applyFloydSteinberg(imageData, threshold) {
  const width = imageData.width;
  const height = imageData.height;
  const data = imageData.data;
  const buffer = new Float32Array(width * height);

  for (let y = 0; y < height; y += 1) {
    for (let x = 0; x < width; x += 1) {
      const i = ((y * width) + x) * 4;
      buffer[(y * width) + x] = toGray(data[i], data[i + 1], data[i + 2]);
    }
  }

  let darkPixels = 0;

  for (let y = 0; y < height; y += 1) {
    for (let x = 0; x < width; x += 1) {
      const index = (y * width) + x;
      const oldPixel = buffer[index];
      const newPixel = oldPixel < threshold ? 0 : 255;
      if (newPixel === 0) {
        darkPixels += 1;
      }
      const error = oldPixel - newPixel;
      buffer[index] = newPixel;

      if (x + 1 < width) {
        buffer[index + 1] = clampByte(buffer[index + 1] + (error * (7 / 16)));
      }
      if (x - 1 >= 0 && y + 1 < height) {
        buffer[index + width - 1] = clampByte(buffer[index + width - 1] + (error * (3 / 16)));
      }
      if (y + 1 < height) {
        buffer[index + width] = clampByte(buffer[index + width] + (error * (5 / 16)));
      }
      if (x + 1 < width && y + 1 < height) {
        buffer[index + width + 1] = clampByte(buffer[index + width + 1] + (error * (1 / 16)));
      }
    }
  }

  for (let y = 0; y < height; y += 1) {
    for (let x = 0; x < width; x += 1) {
      const i = ((y * width) + x) * 4;
      const value = clampByte(Math.round(buffer[(y * width) + x]));
      data[i] = value;
      data[i + 1] = value;
      data[i + 2] = value;
      data[i + 3] = 255;
    }
  }

  return darkPixels;
}

export function processImageTone(sourceImage, options) {
  const widthMm = Math.max(1, Number(options.widthMm));
  const heightMm = Math.max(1, Number(options.heightMm));
  const dpi = Math.max(25, Number(options.dpi));
  const mode = options.mode || "binary";
  const threshold = Math.max(1, Math.min(254, Number(options.threshold ?? 128)));

  const widthPx = Math.max(1, Math.round((widthMm / 25.4) * dpi));
  const heightPx = Math.max(1, Math.round((heightMm / 25.4) * dpi));

  const canvas = document.createElement("canvas");
  canvas.width = widthPx;
  canvas.height = heightPx;
  const context = canvas.getContext("2d", { willReadFrequently: true });
  if (!context) {
    throw new Error("Canvas context is not available.");
  }

  context.imageSmoothingEnabled = mode !== "binary";
  context.fillStyle = "#fff";
  context.fillRect(0, 0, widthPx, heightPx);
  context.drawImage(sourceImage, 0, 0, widthPx, heightPx);

  const imageData = context.getImageData(0, 0, widthPx, heightPx);
  let darkPixels = 0;
  if (mode === "grayscale") {
    darkPixels = applyGrayscale(imageData);
  } else if (mode === "dither") {
    darkPixels = applyFloydSteinberg(imageData, threshold);
  } else {
    darkPixels = applyBinary(imageData, threshold);
  }
  context.putImageData(imageData, 0, 0);

  return {
    widthPx,
    heightPx,
    widthMm,
    heightMm,
    dpi,
    mode,
    threshold,
    darkRatio: darkPixels / Math.max(1, widthPx * heightPx),
    previewDataUrl: canvas.toDataURL("image/png")
  };
}
