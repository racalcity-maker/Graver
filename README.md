# LaserGraver (ESP32-S3)

Custom firmware and web UI for a DIY laser engraver controller based on `ESP32-S3`.

This project replaces legacy Benbox/Nano control logic with:
- ESP-IDF firmware
- built-in web interface
- job upload and execution pipeline
- modular architecture (`control`, `motion`, `laser`, `jobs`, `storage`, `web`)

## Repository Layout

```text
docs/
  architecture.md
  job-format.md

firmware/
  components/
  config/
  main/
  README.md

frontend/
  README.md

tools/
  README.md
```

## Current Capabilities

- AP + STA fallback networking
- Web UI (status, control, upload, jobs)
- Image upload pipeline (binary / grayscale / dither)
- Primitive generation (vector + raster modes)
- Text generation (fast vector + raster quality)
- Job storage in SPIFFS
- Raster execution with row-based optimization
- Pause / resume / abort job controls
- Soft limits for work area

## Build (Firmware)

Use `ESP-IDF 5.3.x` and target `esp32s3`.

PowerShell example:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -Command ". 'C:\Espressif\frameworks\esp-idf-v5.3.3\export.ps1'; idf.py set-target esp32s3; idf.py build"
```

See detailed firmware notes in:
- `firmware/README.md`

## Project Principles

- Keep realtime motion logic independent from HTTP/UI code.
- Do image preprocessing in browser, not on MCU.
- Keep laser safe by default (`off` on startup, pause, alarm, stop).
- Use data-driven machine config (`machine.json`) instead of hardcoding board-specific values.

## Notes

- Local runtime machine config path: `/spiffs/config/machine.json`
- Jobs path: `/spiffs/jobs/<job-id>/`

