# Architecture

## 1. Responsibility Boundaries

### Firmware

Firmware owns:
- machine state and alarms
- motion execution and axis constraints
- laser drive and runtime safety enforcement
- job storage and execution
- device APIs (web and GRBL serial bridge)

Firmware does not do heavy image authoring tasks.

### Frontend

Frontend owns:
- scene/object editing
- operation staging (engrave/cut passes and order)
- raster/vector preparation
- planner export and upload/run orchestration

## 2. Runtime Service Layers

- `app`: startup order and dependency wiring
- `board`: hardware abstraction (GPIO/timer/RMT integration points)
- `machine`: top-level machine state owner and transitions
- `control`: command queue and operation sequencing
- `motion`: physical move execution, limits, homing primitives
- `laser`: PWM and arm/disarm contract
- `jobs`: manifest parsing + raster/vector executors
- `storage`: SPIFFS config and job persistence
- `web`: HTTP routes and web assets
- `grbl`: serial protocol compatibility layer

## 3. Command and State Model

- Web/GRBL layers submit commands.
- Control queue serializes execution.
- Machine/control services own active state.
- Status is exposed as snapshots for UI polling.

Target state model (partially implemented):
- `Idle`
- `Running`
- `Paused`
- `Homing`
- `Alarm` / `EstopLatched`

## 4. Data Flow

1. Frontend builds scene and stage plan.
2. Frontend uploads manifest (+ raster payload if needed).
3. Firmware stores under `/spiffs/jobs/<job-id>/`.
4. Frontend requests run.
5. Control dispatches to raster/vector executor.
6. Motion and laser run in real time with safety checks.

## 5. Execution Modes

### Raster
- row-oriented execution
- active segment trimming
- serpentine/bidirectional path support
- faster travel on laser-off segments

### Vector
- stroke/path execution
- primitives and vector text workflows
- contour and fill strategies

## 6. Safety Model

- laser off by default
- immediate laser off on pause/stop/alarm
- safety inputs (estop/lid/limits) can latch alarm state
- invalid-state commands are rejected
- soft limits clamp work envelope

## 7. Networking

- try STA first (when credentials exist)
- retry with bounded attempts/time window
- fallback to AP on STA failure
- AP immediately if no saved STA config

## 8. Configuration Precedence

1. `menuconfig` defaults
2. `/spiffs/config/machine.json` runtime overrides

Runtime JSON takes precedence after boot.
