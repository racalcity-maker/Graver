# Architecture

## 1. Responsibility Boundaries

### Firmware

Firmware is responsible for:
- machine state and safety
- motor motion and step generation
- laser PWM and gating
- job persistence and execution
- HTTP/WebSocket API

Firmware is **not** responsible for heavy image preprocessing.

### Browser UI

Frontend is responsible for:
- image/text/primitive preparation
- preview and parameter tuning
- manifest + payload generation
- upload and run control

## 2. Runtime Layers

### `app`
- service wiring
- boot sequence
- startup order and dependency ownership

### `board`
- low-level hardware access (GPIO, timers, step pulses)
- board-specific pin behavior

### `motion`
- movement API (`jog`, `moveTo`, `goToZero`)
- step/mm conversions
- soft limits
- row execution path for raster jobs

### `laser`
- PWM configuration
- laser arm/disarm state
- safe off behavior

### `jobs`
- manifest parsing and validation
- raster/vector job loading
- raster/vector executors

### `storage`
- SPIFFS mount and file I/O
- machine config load/save
- job files load/save

### `control`
- command queue and runtime orchestration
- run/pause/resume/abort command routing

### `web`
- REST endpoints
- status API
- web asset serving

## 3. Data Flow

1. User prepares job in browser.
2. Browser uploads `manifest.json` (+ `raster.bin` for raster jobs).
3. Firmware stores job under `/spiffs/jobs/<job-id>/`.
4. User runs job via API/UI.
5. Control layer dispatches to raster/vector executor.
6. Motion + laser layers execute in realtime.

## 4. Job Execution Modes

### Raster
- row-driven execution
- per-row active-range trimming
- serpentine support
- travel speed on laser-off segments

### Vector
- path/stroke based execution
- useful for fast primitives and fast text mode

## 5. Safety Model

- Laser off by default.
- Laser off on pause, stop, alarm.
- Commands rejected in invalid state.
- Soft limits enforce configured work area.

## 6. Networking Model

- If saved STA credentials exist:
  - try connect up to 3 times within 15 seconds
  - fallback to AP if connection fails
- If no saved STA:
  - start AP immediately

## 7. Configuration

- Compile-time defaults: `menuconfig`
- Runtime override: `/spiffs/config/machine.json`
- Runtime config always takes precedence over defaults.

