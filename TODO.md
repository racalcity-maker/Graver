# TODO (LaserGraver ESP32-S3)

Текущий backlog по тому, что осталось довести до стабильного рабочего состояния.

## 1) Safety (критично)

- [ ] Завершить hard emergency stop:
  - [ ] `tripAlarm(reason)` с latch-состоянием `Alarm/EstopLatched`
  - [ ] мгновенно `laserOff()` при E-STOP и lid open
  - [ ] немедленная остановка step engine
  - [ ] блокировка новых команд до `clearAlarm`
- [ ] `clearAlarm` разрешать только после исчезновения причины аварии
- [ ] Limit switch в обычной работе должен вызывать alarm (кроме homing)

## 2) Motion core и стабильность

- [ ] Довести неблокирующий step backend (RMT/GPTimer/ISR) до стабильной работы без `flush timeout`
- [ ] Убрать зависания `job_worker/control_worker` и WDT-триггеры при ошибках движения
- [ ] Разнести planner и physical pulse generation окончательно (четкие интерфейсы)
- [ ] Добавить recovery path после ошибок motion (чистый выход из busy/alarm)

## 3) Homing (полноценный)

- [ ] Переключатель в UI: `Homing enabled/disabled` (по умолчанию off для станков без концевиков)
- [ ] Двухфазный homing по осям X/Y:
  - [ ] fast seek
  - [ ] pull-off
  - [ ] slow seek
  - [ ] set machine zero
- [ ] Timeout, инверсия логики концевиков, проверка "концевик уже активен до старта"

## 4) Planner и качество траекторий

- [ ] Нормальный planner: ускорение/торможение, junction handling, look-ahead
- [ ] Параллельные XY-движения с корректной синхронизацией скоростей (без рывков)
- [ ] Настройки jerk/accel в конфиге и UI

## 5) Jobs pipeline

- [ ] Завершить построчный streaming executor для raster (устойчивый, без регрессий)
- [ ] Улучшить skip-пустых зон и быстрые travel-переходы (с контролем качества)
- [ ] Привести поведение `abort -> run` к полностью предсказуемому состоянию
- [ ] Проверить консистентность замены job с одинаковым/новым `jobId`

## 6) Laser control и материалы

- [ ] Профили материалов (картон/фанера/акрил и т.д.): power/feed/dpi presets
- [ ] Отдельные режимы laser power:
  - [ ] constant power (binary/vector)
  - [ ] grayscale modulation
  - [ ] dither mode
- [ ] Калибровочная таблица `requested power -> effective output` (по материалу)

## 7) UI/UX

- [ ] Доработать страницу Upload:
  - [ ] единый сценарий выбора типа job (image/text/primitive)
  - [ ] сохранение ключевых параметров (debounced persistence)
  - [ ] понятные tooltips по параметрам
- [ ] Улучшить progress/ETA и отображение текущей фазы выполнения job
- [ ] Добавить явную индикацию alarm/estop/lid/limit в статусе

## 8) GRBL compatibility (MVP -> usable)

- [ ] Расширить покрытие G-code (core laser workflows)
- [ ] Проверить корректность realtime-команд и статус-репортов под LaserGRBL/UGS
- [ ] Свести настройки `$` с runtime-config без конфликтов

## 9) Тестирование и эксплуатация

- [ ] Добавить smoke-тесты API сценариев (run/pause/resume/abort/home)
- [ ] Добавить hardware bring-up чеклист (пины, EN, DIR, STEP, limits, PWM)
- [ ] Добавить регрессионный набор тестовых jobs (raster/text/primitive)

## 10) Документация

- [ ] Зафиксировать стабильную pinout-матрицу для ESP32-S3 платы и типовых драйверов
- [ ] Описать полностью workflow "из коробки" (первый запуск -> первая гравировка)
- [ ] Синхронизировать `docs/architecture.md` с фактической реализацией сервисов
