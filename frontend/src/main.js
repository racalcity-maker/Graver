import "./styles/app.css";
import { bootstrapApp } from "./app/bootstrap.js";
import { registerServiceWorker } from "./shared/pwa/register-sw.js";

bootstrapApp(document.getElementById("app"));
registerServiceWorker();
