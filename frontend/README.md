# Frontend

Frontend должен быть отдельным приложением, которое собирается в статические файлы и встраивается в firmware.

## Почему подготовка картинки должна жить здесь

Потому что браузер уже умеет:
- декодировать изображения;
- крутить и масштабировать canvas;
- показывать preview без нагрузки на микроконтроллер;
- быстро менять параметры без лишней записи во flash.

## Рекомендуемая структура

```text
frontend/
  package.json
  vite.config.ts
  src/
    app/
    features/
      image-prep/
      jobs/
      machine/
      control/
    entities/
      job/
      machine/
      settings/
    shared/
      api/
      ws/
      canvas/
      lib/
      ui/
```

## Основные сценарии UI

- загрузка изображения;
- crop / rotate / resize;
- grayscale / threshold / dither;
- настройка размеров в мм и DPI;
- preview области гравировки;
- загрузка job package на устройство;
- список jobs на устройстве;
- live status: state, progress, coordinates, power, alarms;
- кнопки `home`, `frame`, `start`, `pause`, `resume`, `stop`.

## API-контракт

Рекомендуемый раздел:
- REST для CRUD и upload;
- WebSocket для live state.

Пример REST:
- `GET /api/status`
- `GET /api/jobs`
- `POST /api/jobs`
- `POST /api/jobs/{id}/start`
- `POST /api/control/home`
- `POST /api/control/frame`
- `POST /api/control/pause`
- `POST /api/control/resume`
- `POST /api/control/stop`

WebSocket:
- `/ws`

