# LaserGraver

LaserGraver is an ESP32-S3 based laser engraver controller plus a browser studio for preparing and running jobs over Wi-Fi.

The project includes:
- firmware (`ESP-IDF 5.3.x`) for motion, laser control, safety, storage, and APIs
- a modular browser frontend (Vite + vanilla JS) for scene editing, staging operations, and job upload/run

## Why This Project

Main goals:
- replace closed/legacy controller stacks with an open and maintainable architecture
- keep the whole flow reproducible: scene -> job -> execution
- make motion and safety behavior explicit (state, alarms, command queue)

## Current Capabilities

- XY motion with configurable machine limits and work-zero workflow
- laser PWM control with runtime safety checks
- raster jobs (binary / grayscale / dither modes)
- vector jobs (primitives and vector text, including fill mode)
- pause / resume / abort controls
- web API + embedded UI
- GRBL serial compatibility (MVP level)
- AP + STA networking fallback logic

## Repository Layout

```text
LaserGraver/
  firmware/     ESP-IDF firmware
  frontend/     Browser studio (PWA-ready)
  docs/         Architecture and job format notes
  tools/        Utility scripts
  TODO.md       Staged roadmap and implementation tracker
```

## Firmware Overview

Core firmware components:
- `app`: wiring and startup
- `machine`: top-level orchestration and state ownership
- `control`: command queue and operation flow
- `motion`: movement execution and step backend integration
- `laser`: PWM and laser arm/disarm safety behavior
- `jobs`: raster/vector parsing and execution entry points
- `storage`: SPIFFS and persisted configs/jobs
- `web`: HTTP API and embedded pages/assets
- `grbl`: serial command compatibility layer
- `board`: GPIO and low-level board HAL

Build details and pin defaults: see `firmware/README.md`.

## Frontend Overview

The browser app supports:
- scene object editing (text, primitives, imported vectors/raster)
- layer and operation assignment
- per-operation process parameters (engrave/cut stages)
- upload window with planner output and one-click run
- settings window (device URL, workspace size, grid)

Run details: see `frontend/README.md`.

## Typical Workflow

1. Configure machine defaults (`menuconfig`) and optionally runtime JSON config.
2. Flash firmware and connect to device AP/STA.
3. Open browser studio, prepare scene objects and operations.
4. Generate staged job plan.
5. Upload manifest/raster payload to device.
6. Run, monitor, pause/resume/abort if needed.

## Safety Notes

- Laser should remain off by default and only arm during valid active operations.
- E-stop, lid interlock, and limits must be wired and validated before production use.
- Never run unattended while tuning power/speed/material presets.

## Roadmap and Docs

- Roadmap: `TODO.md`
- Architecture notes: `docs/architecture.md`
- Job format contract: `docs/job-format.md`

---

This is a personal project made for educational purposes.
