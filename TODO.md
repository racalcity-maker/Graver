# TODO Roadmap (LaserGraver ESP32-S3)

Last update: 2026-03-10

Legend:
- `[x]` done
- `[~]` partial (implemented, but not stable/complete)
- `[ ]` todo

## Stage 1 - Critical Safety + Stable Core (in progress)

### 1.1 Emergency stop and alarms
- [x] Wire safety inputs in config/HAL (`estop`, `lid`, `limits`) and read them at runtime
- [x] Hard `tripAlarm(reason)` with latched `Alarm/EstopLatched` state
- [x] Immediate `laserOff()` on E-STOP and lid-open in all execution paths
- [~] Immediate motion stop (step engine stop/cancel), not only command rejection
- [x] `clearAlarm` only when root cause is no longer active
- [x] Auto block all new commands while alarm is latched

### 1.2 Control runtime architecture
- [x] Command queue exists (`jog`, `home/set zero`, `start`, `pause`, `resume`, `abort`)
- [~] Single-owner control flow is partially in place (`control` queue + worker ownership)
- [~] Formal state machine with explicit transitions (Idle/Running/Paused/Alarm/Homing) in progress
- [~] Remove remaining race-prone shared-state access patterns
- [x] Snapshot-based status read contract for web/API

### 1.3 Motion backend stability
- [~] Non-blocking/RMT-oriented step backend introduced
- [ ] Fix `rmt_tx_wait_all_done` timeout path completely
- [ ] Remove job/control worker WDT hangs on motion failures
- [~] Add deterministic recovery after motion error (`busy -> alarm/idle` cleanup)

### 1.4 Homing baseline
- [x] UI behavior split for machines with/without homing (Set Zero fallback)
- [x] Axis homing primitives started (seek/pull-off logic introduced)
- [x] Full 2-pass homing flow per axis (fast seek -> pull-off -> slow seek)
- [x] Timeout and "switch already active before start" checks
- [~] Limit polarity inversion and per-axis homing direction partially validated

## Stage 2 - Motion Quality + Throughput

### 2.1 Planner quality
- [x] Linear XY moves and basic queueing work
- [~] Acceleration/deceleration planner (baseline short-move feed limiting introduced)
- [ ] Junction handling / look-ahead for smooth corners
- [ ] Unified feed planning for vector + raster travel/print paths

### 2.2 Raster speed pipeline
- [x] Row/line based execution path exists
- [x] Empty-zone skipping and faster no-laser travel are partially implemented
- [~] Complete line streaming executor (low memory + no stalls + predictable timing) in progress
- [ ] Improve overscan handling and turn-around strategy
- [ ] Add tuning presets for fast/quality raster modes

### 2.3 Job lifecycle correctness
- [x] Pause/resume/abort commands are available
- [~] Make `abort -> run next job` deterministic every time
- [ ] Fix all stale job data cases when replacing same/new `jobId`
- [ ] Ensure job resume state persistence rules are explicit and safe

## Stage 3 - Laser Process Control

### 3.1 Power modes
- [x] Binary, grayscale, dither processing modes are available
- [x] Vector and raster text modes are available
- [ ] Strict "constant-power" mode path (no implicit grayscale modulation)
- [ ] Better low-power linearity mapping (`requested -> effective`)
- [ ] Separate travel/power behavior contract per job type

### 3.2 Material presets
- [ ] Material profile system (cardboard, plywood, acrylic, etc.)
- [ ] Preset bundles: power/feed/dpi/pass count
- [ ] User calibration workflow (test pattern -> recommended range)

## Stage 4 - UI/UX and Operator Flow

### 4.1 Upload workflow
- [x] Upload flow supports multiple job types (image/text/primitive/vector text)
- [x] Key parameters persistence with debounced save exists (browser-side settings)
- [ ] Improve mobile layout consistency for all parameter groups
- [~] Add clearer contextual hints for risky settings (power/feed/dpi)

### 4.2 Frontend studio ergonomics
- [x] Modal split: dedicated Upload window and Settings window
- [x] Device URL + workspace + grid settings are configurable and persisted
- [~] Layer/object side panel UX is in place, still needs refinement for advanced editing
- [ ] Add richer text tool controls (font families, advanced typography parity)

### 4.3 Runtime controls
- [x] Progress feedback is present
- [ ] Better progress model: phase + ETA + per-pass indicators
- [ ] Alarm banner with explicit cause and recovery action
- [ ] Unified controls behavior across AP mode and STA mode

### 4.4 Text and vector UX
- [x] Vector text mode exists (including fill mode support)
- [x] Additional glyph coverage has been extended
- [ ] Finalize clean default font set and preview parity with engraving output
- [x] Add predictable typography controls (size/box/alignment/line-height/letter-spacing)

## Stage 5 - GRBL Compatibility

- [x] GRBL serial MVP exists (status, basic commands, basic motion)
- [ ] Expand G-code coverage for common LaserGRBL/UGS workflows
- [ ] Verify realtime behavior under host streaming load
- [ ] Align `$` settings behavior with runtime machine config rules

## Stage 6 - Testing, Diagnostics, Docs

### 6.1 Automated checks
- [ ] API smoke tests (`run/pause/resume/abort/home/zero/alarm`)
- [ ] Regression suite for raster/vector/text jobs
- [ ] Error-injection tests for motion timeout and storage failures

### 6.2 Hardware diagnostics
- [~] Basic bring-up flows are known from manual testing
- [ ] Formal hardware checklist doc (pins, EN, STEP/DIR, limits, PWM, PSU)
- [ ] Runtime diagnostics page for live safety pin states and motion backend health

### 6.3 Documentation
- [x] Root and firmware README improved
- [ ] Keep architecture doc synced with actual service boundaries
- [ ] Add "first boot -> first engraving" operator guide
- [ ] Add troubleshooting matrix (laser weak output, WDT, RMT timeout, job load errors)
