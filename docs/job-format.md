# Job Format

## 1. Storage Layout

Each job is stored in SPIFFS under:

```text
/spiffs/jobs/<job-id>/
  manifest.json
  raster.bin        # only for raster jobs
```

## 2. Manifest Core Fields

```json
{
  "version": 1,
  "jobId": "demo-001",
  "jobType": "raster",
  "output": {
    "widthMm": 50.0,
    "heightMm": 50.0,
    "dpi": 127
  },
  "raster": {
    "widthPx": 250,
    "heightPx": 250,
    "encoding": "gray8-row-major",
    "serpentine": true,
    "bidirectional": true
  },
  "motion": {
    "printSpeedMmMin": 1200,
    "travelSpeedMmMin": 2400,
    "lineStepMm": 0.2,
    "overscanMm": 0.0
  },
  "laser": {
    "minPower": 0,
    "maxPower": 255,
    "pwmMode": "dynamic"
  }
}
```

## 3. Raster Payload

`raster.bin` is:
- 8-bit row-major (`gray8-row-major`)
- one byte per pixel
- row length = `raster.widthPx`
- total bytes = `widthPx * heightPx`

Pixel meaning:
- `0`: laser off
- `255`: max requested pixel power

Final output power is mapped by manifest laser settings.

## 4. PWM Modes

- `dynamic`: grayscale power mapping (`minPower..maxPower`)
- `binary`: on/off marking at fixed max power

`binary` is recommended for:
- contour images
- logos
- raster text with hard edges

## 5. Vector Jobs

`jobType` can be vector-based (`vector-primitive`, `vector-text`), where manifest contains strokes/paths and no raster payload is required.

## 6. Runtime Contract

- Manifest is validated before execution.
- Job runs only after work-zero is set (or homed mode where applicable).
- Abort/stop must force laser off and clear active run state.

