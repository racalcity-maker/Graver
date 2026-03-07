# Firmware

ESP-IDF firmware for the `ESP32-S3` laser graver controller.

## Quick Start

Validated with `ESP-IDF 5.3.x` and target `esp32s3`.

PowerShell:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -Command ". 'C:\Espressif\frameworks\esp-idf-v5.3.3\export.ps1'; idf.py set-target esp32s3; idf.py build"
```

If target is already set:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -Command ". 'C:\Espressif\frameworks\esp-idf-v5.3.3\export.ps1'; idf.py build"
```

## Menuconfig

Machine-specific options are available in:

```text
idf.py menuconfig -> LaserGraver
```

Main sections:
- `GPIO Pins`
- `Motion Defaults`
- `Laser Defaults`
- `Network Defaults`
- `Safety Defaults`

Important: values from `/spiffs/config/machine.json` override these compile-time defaults after boot.

## Runtime Config

Firmware first tries to load machine config from:

```text
/spiffs/config/machine.json
```

If the file is missing, firmware uses compiled defaults from `shared::BuildDefaultMachineConfig()`.

Examples:
- [machine.example.json](C:/Users/test/Documents/Arduino/LaserGraver/firmware/config/machine.example.json)
- [machine.example.yaml](C:/Users/test/Documents/Arduino/LaserGraver/firmware/config/machine.example.yaml)

## Current Default Pins

Default `menuconfig` mapping for bring-up on `ESP32-S3`:
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
- `LASER GATE = -1` (disabled)
- `X LIMIT = GPIO14`
- `Y LIMIT = GPIO15`
- `E-STOP = GPIO16`
- `LID INTERLOCK = GPIO17`

Default axis layout:
- `X`: one motor
- `Y`: two motors
- `Y secondaryUsesSeparateDriver = false`

## Layer Responsibilities

- `board`: low-level GPIO and ESP-IDF hardware access
- `shared`: shared DTO/config types
- `machine`: state machine and orchestration
- `motion`: step generation and axis services
- `laser`: PWM and laser safety behavior
- `jobs`: job metadata and execution inputs
- `storage`: SPIFFS, machine config, job files
- `web`: HTTP API and future web assets
- `app`: service wiring and startup sequence

## Engineering Rules

- Laser must default to off.
- Web handlers must not drive GPIO directly.
- UI commands should go through `machine` or dedicated services.
- Motion code should stay independent from HTTP and JSON.
