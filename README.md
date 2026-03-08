# LaserGraver (ESP32-S3)

DIY laser engraver controller firmware + web UI on `ESP32-S3` (ESP-IDF 5.3.x).

Проект заменяет старую связку Benbox/Nano на модульную систему:
- embedded firmware (`ESP-IDF`)
- web UI для телефона/ПК
- pipeline загрузки и запуска задач
- отдельные слои `control / motion / laser / jobs / storage / web / grbl`

## Что уже работает

- Wi-Fi: попытка подключения к сохраненной STA сети + fallback в AP
- Web UI: управление осями, zero/home сценарии, frame, запуск/пауза/resume/abort
- Загрузка задач: raster image, primitives, text
- Режимы изображения: binary, grayscale, dither (+ опции обработки)
- Хранение задач и runtime-конфига в SPIFFS
- Soft-limits рабочей области
- Базовая GRBL-совместимость по serial (MVP)

## Что важно помнить сейчас

- Это активная стадия разработки и отладки железа, не production.
- Безопасность должна считаться неполной, пока не завершен hard emergency-stop pipeline.
- Качество/скорость зависят от текущего motion backend и калибровки драйвера лазера.

## Быстрый старт (firmware)

Требования:
- `ESP-IDF 5.3.x`
- target: `esp32s3`

Сборка:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -Command ". 'C:\Espressif\frameworks\esp-idf-v5.3.3\export.ps1'; idf.py set-target esp32s3; idf.py build"
```

Прошивка и монитор:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -Command ". 'C:\Espressif\frameworks\esp-idf-v5.3.3\export.ps1'; idf.py -p COMx flash monitor"
```

Настройка параметров платы:

```text
idf.py menuconfig -> LaserGraver
```

Детали по пинам, разделам конфигурации и слоям: `firmware/README.md`.

## Конфиг и данные во flash

- Runtime machine config: `/spiffs/config/machine.json`
- Jobs: `/spiffs/jobs/<job-id>/`

Если `machine.json` отсутствует, используются compile-time defaults из `menuconfig`.

## Структура репозитория

```text
docs/          архитектура, формат задач
firmware/      ESP-IDF проект
frontend/      UI исходники и notes
tools/         утилиты разработки
TODO.md        актуальный технический backlog
```

## Документация

- [Firmware README](C:/Users/test/Documents/Arduino/LaserGraver/firmware/README.md)
- [Architecture](C:/Users/test/Documents/Arduino/LaserGraver/docs/architecture.md)
- [Job Format](C:/Users/test/Documents/Arduino/LaserGraver/docs/job-format.md)
- [TODO / Roadmap](C:/Users/test/Documents/Arduino/LaserGraver/TODO.md)

## Принципы проекта

- Реалтайм-движение не должно зависеть от HTTP/UI нагрузки.
- Предобработка изображений по возможности в браузере, не на MCU.
- Лазер по умолчанию всегда в безопасном состоянии (`off`) при старте/паузе/alarm.
- Конфиг машины должен быть data-driven, а не захардкожен в коде.
