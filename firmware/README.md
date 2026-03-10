# Firmware

ESP-IDF firmware for the `ESP32-S3` laser graver controller.

## Build

Validated with `ESP-IDF 5.3.x`, target `esp32s3`.

PowerShell quick build:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -Command ". 'C:\Espressif\frameworks\esp-idf-v5.3.3\export.ps1'; idf.py set-target esp32s3; idf.py build"
```

If target is already configured:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -Command ". 'C:\Espressif\frameworks\esp-idf-v5.3.3\export.ps1'; idf.py build"
```

## Configuration Model

Settings come from two layers:

1. Compile-time defaults (`menuconfig -> LaserGraver`)
2. Runtime config (`/spiffs/config/machine.json`)

Runtime config overrides compile-time defaults after boot.

Reference files:
- `firmware/config/machine.example.json`
- `firmware/config/machine.example.yaml`

## Key Modules

- `app`: startup and dependency wiring
- `machine`: machine-level state and orchestration
- `control`: command queue (`jog`, `home`, `set zero`, `run`, `pause`, `resume`, `abort`, `clear alarm`)
- `motion`: move execution and backend step engine integration
- `laser`: PWM, arm/disarm, safe-off behavior
- `jobs`: raster/vector job parsing and execution entry
- `storage`: SPIFFS config and job persistence
- `web`: API and embedded web resources
- `grbl`: serial GRBL-compatible command adapter
- `board`: HAL and pin-level operations

## Default Pin Map (Current Bring-Up)

- `X STEP = GPIO8`
- `X DIR = GPIO9`
- `X secondary STEP = -1`
- `X secondary DIR = -1`
- `Y STEP = GPIO4`
- `Y DIR = GPIO5`
- `Y secondary STEP = GPIO6`
- `Y secondary DIR = GPIO7`
- `MOTORS ENABLE = GPIO10`
- `LASER PWM = GPIO20`
- `LASER GATE = -1`
- `X LIMIT = GPIO14`
- `Y LIMIT = GPIO15`
- `E-STOP = GPIO16`
- `LID INTERLOCK = GPIO17`

Axis layout defaults:
- X axis: single motor/driver path
- Y axis: dual-motor capable path

## Safety and Runtime Guarantees

- Laser defaults to off.
- Alarms can latch machine in blocked state until cleared.
- Web/API handlers do not control GPIO directly.
- Motion and laser execution go through control/machine services.

## GRBL Compatibility (MVP)

Enable in `menuconfig -> LaserGraver -> GRBL Compatibility`.

Implemented baseline:
- banner `Grbl 1.1f ['$' for help]`
- `ok` / `error:n`
- realtime commands `?`, `!`, `~`, `Ctrl-X`
- `$` commands: `$$`, `$I`, `$G`, `$X`, `$H`
- core gcode: `G0`, `G1`, `G4`, `G20`, `G21`, `G90`, `G91`, `G92(X0 Y0)`
- spindle words: `M3`, `M4`, `M5`, `S`

Current status: compatibility is usable for basic host tooling, but still not full GRBL parity.
