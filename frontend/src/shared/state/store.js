const defaultState = {
  scene: {
    widthMm: 180,
    heightMm: 130
  },
  layers: [],
  operations: [],
  objects: [],
  ui: {
    selectedObjectId: null,
    showGrid: true
  }
};

function clone(value) {
  return JSON.parse(JSON.stringify(value));
}

export function createStore(initial = {}) {
  let state = {
    ...clone(defaultState),
    ...clone(initial)
  };
  const listeners = new Set();

  function notify() {
    for (const listener of listeners) {
      listener(state);
    }
  }

  return {
    getState() {
      return state;
    },
    setState(updater) {
      state = typeof updater === "function" ? updater(state) : updater;
      notify();
    },
    subscribe(listener) {
      listeners.add(listener);
      listener(state);
      return () => listeners.delete(listener);
    }
  };
}
