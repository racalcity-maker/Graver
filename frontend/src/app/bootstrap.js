import { createStore } from "../shared/state/store.js";
import { createEditorView } from "../features/editor/editor-view.js";
import { createObjectLayersPanel } from "../features/layers/object-layers-panel.js";
import { createJobsPanel } from "../features/jobs/jobs-panel.js";

export function bootstrapApp(root) {
  if (!root) {
    throw new Error("App root is missing.");
  }

  const store = createStore();

  root.innerHTML = `
    <main class="layout">
      <header class="hero">
        <h1>LaserGraver Studio</h1>
        <p>Веб-приложение: гравировка и резка в одном задании.</p>
      </header>
      <section class="workspace">
        <section id="editor-panel" class="panel editor"></section>
        <aside class="panel-col right">
          <section id="objects-panel" class="panel"></section>
        </aside>
      </section>
      <section id="jobs-panel" class="jobs-host"></section>
    </main>
  `;

  createEditorView(root.querySelector("#editor-panel"), store);
  createObjectLayersPanel(root.querySelector("#objects-panel"), store);
  createJobsPanel(root.querySelector("#jobs-panel"), store);
}
