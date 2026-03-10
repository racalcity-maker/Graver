# Job Format

## 1. Storage Layout

Jobs are stored in SPIFFS:

```text
/spiffs/jobs/<job-id>/
  manifest.json
  raster.bin        # raster jobs only
```

## 2. Manifest Core

All jobs include:
- `version`
- `jobId`
- `jobType`
- output geometry (`widthMm`, `heightMm`, `dpi` where relevant)
- motion parameters
- laser parameters

Typical `jobType` values:
- `raster`
- `vector-primitive`
- `vector-text`

Frontend planner may generate staged operations that are flattened into executable job data for firmware runtime.

## 3. Raster Payload Contract

`raster.bin` uses `gray8-row-major`:
- 8-bit unsigned per pixel
- row-major ordering
- total bytes = `widthPx * heightPx`

Interpretation:
- `0` -> laser off
- `255` -> maximum requested pixel intensity

Actual emitted laser power depends on job mode and runtime mapping.

## 4. Raster Intensity Modes

- `binary`: thresholded on/off output (fast, sharp edges)
- `dynamic`/grayscale: per-pixel mapped intensity
- dither workflows are represented as binary patterns in payload generation stage

## 5. Vector Contract

Vector jobs do not require `raster.bin`.
Manifest carries vector primitives/strokes and execution options:
- stroke width / contour settings
- fill mode (when supported)
- print and travel feeds
- fixed power for vector passes

## 6. Runtime Rules

- manifest must validate before run
- machine must be in a valid state (work zero or homed policy)
- abort/stop must force laser off
- safety/alarm conditions can interrupt or reject execution
