# Frontend (LaserGraver Studio)

Browser studio for preparing jobs and uploading them to the ESP32-S3 controller over Wi-Fi.

## Stack

- Vite
- Vanilla JavaScript (ES modules)
- Canvas-based scene editor
- PWA assets (`manifest`, service worker registration)

## Current Features

- Object-based scene editing:
  - text
  - primitives
  - imported SVG objects
  - imported raster images
- Layer/object panel with selection and per-object settings
- Operation modeling (engrave/cut style stages with speed/power/passes/order)
- Multi-stage planner export
- Upload modal:
  - upload job
  - upload + run job
  - planner preview and API log
- Settings modal:
  - device base URL
  - workspace width/height
  - grid visibility
- Device config persistence in browser local storage

## Project Structure

```text
frontend/
  index.html
  package.json
  vite.config.js
  public/
    manifest.webmanifest
    sw.js
    icons/
  src/
    main.js
    app/bootstrap.js
    styles/app.css
    features/
      editor/
      layers/
      operations/
      objects/
      import/
      jobs/
    shared/
      api/
      state/
      settings/
      pwa/
```

## Run

```powershell
cd frontend
npm install
npm run dev
```

Build:

```powershell
npm run build
```

Preview:

```powershell
npm run preview
```

## Notes

- The frontend prepares job data and calls firmware APIs.
- Real-time motion/laser logic remains in firmware.
- Device API endpoint is configurable in Settings.
