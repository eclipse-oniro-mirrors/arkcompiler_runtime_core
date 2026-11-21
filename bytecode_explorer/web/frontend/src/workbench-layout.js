/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

import {
  applySplitterDelta,
  defaultWorkbenchLayout,
  parseStoredWorkbenchLayout,
  serializeWorkbenchLayout,
  setMaximizedPanel,
} from './workbench-layout-model.js';

export const WORKBENCH_STORAGE_KEY = 'ark-bytecode-explorer.workbench-layout.v1';

const KEYBOARD_STEP = 16;

function defaultResizeObserverFactory(callback) {
  return new ResizeObserver(callback);
}

function coordinateFor(splitter, event) {
  return splitter === 'structure' || splitter === 'inspector' ? event.clientX : event.clientY;
}

export class WorkbenchLayout {
  #root;
  #storage;
  #windowTarget;
  #resizeObserverFactory;
  #onPanelResize;
  #onPresentationChange;
  #layout = defaultWorkbenchLayout();
  #panels = [];
  #buttons = [];
  #separators = [];
  #listeners = [];
  #resizeObserver = null;
  #drag = null;
  #mounted = false;

  constructor({
    root,
    storage = globalThis.localStorage,
    windowTarget = globalThis.window,
    resizeObserverFactory = defaultResizeObserverFactory,
    onPanelResize = () => {},
    onPresentationChange = () => {},
  }) {
    if (root === null || typeof root !== 'object') {
      throw new TypeError('workbench root is required');
    }
    this.#root = root;
    this.#storage = storage;
    this.#windowTarget = windowTarget;
    this.#resizeObserverFactory = resizeObserverFactory;
    this.#onPanelResize = onPanelResize;
    this.#onPresentationChange = onPresentationChange;
  }

  mount() {
    if (this.#mounted) {
      return;
    }
    this.#mounted = true;
    this.#panels = [...this.#root.querySelectorAll('[data-workbench-panel]')];
    this.#buttons = [...this.#root.querySelectorAll('[data-panel-maximize]')];
    this.#separators = [...this.#root.querySelectorAll('[data-workbench-separator]')];
    this.#layout = parseStoredWorkbenchLayout(this.#readStoredLayout(), this.#viewport());
    this.#applyLayout();

    for (const separator of this.#separators) {
      this.#listen(separator, 'pointerdown', (event) => this.#beginDrag(separator, event));
      this.#listen(separator, 'pointermove', (event) => this.#moveDrag(separator, event));
      this.#listen(separator, 'pointerup', (event) => this.#finishDrag(separator, event));
      this.#listen(separator, 'pointercancel', (event) => this.#finishDrag(separator, event));
      this.#listen(separator, 'keydown', (event) => this.#onSeparatorKey(separator, event));
    }
    for (const button of this.#buttons) {
      this.#listen(button, 'click', () => {
        const panelId = button.dataset.panelMaximize;
        if (this.#layout.maximizedPanel === panelId) {
          this.restore();
        } else {
          this.maximize(panelId);
        }
      });
    }
    this.#listen(this.#windowTarget, 'keydown', (event) => {
      if (event.key === 'Escape' && this.#layout.maximizedPanel !== null) {
        event.preventDefault();
        this.restore();
      }
    });
    this.#listen(this.#windowTarget, 'resize', () => {
      this.#layout = parseStoredWorkbenchLayout(serializeWorkbenchLayout(this.#layout), this.#viewport());
      this.#applyLayout();
    });

    this.#resizeObserver = this.#resizeObserverFactory((entries) => {
      for (const entry of entries) {
        const panelId = entry.target?.dataset?.panelId;
        if (typeof panelId === 'string' && Number.isFinite(entry.contentRect?.width)) {
          this.#onPanelResize(panelId, entry.contentRect.width);
        }
      }
    });
    for (const panel of this.#panels) {
      this.#resizeObserver.observe(panel);
    }
  }

  maximize(panelId) {
    this.#layout = setMaximizedPanel(this.#layout, panelId);
    this.#applyLayout();
    this.#persist();
  }

  restore() {
    this.#layout = setMaximizedPanel(this.#layout, null);
    this.#applyLayout();
    this.#persist();
  }

  destroy() {
    if (!this.#mounted) {
      return;
    }
    this.#mounted = false;
    for (const { target, type, listener } of this.#listeners) {
      target.removeEventListener(type, listener);
    }
    this.#listeners = [];
    this.#resizeObserver?.disconnect();
    this.#resizeObserver = null;
    this.#drag = null;
  }

  #viewport() {
    const rect = this.#root.getBoundingClientRect();
    return { width: rect.width, height: rect.height };
  }

  #listen(target, type, listener) {
    target.addEventListener(type, listener);
    this.#listeners.push({ target, type, listener });
  }

  #readStoredLayout() {
    try {
      return this.#storage?.getItem(WORKBENCH_STORAGE_KEY) ?? null;
    } catch {
      return null;
    }
  }

  #persist() {
    try {
      this.#storage?.setItem(WORKBENCH_STORAGE_KEY, serializeWorkbenchLayout(this.#layout));
    } catch {
      // Storage is an optional convenience; the workbench remains usable without it.
    }
  }

  #applyLayout() {
    this.#root.style.setProperty('--structure-width', `${this.#layout.structureWidth}px`);
    this.#root.style.setProperty('--inspector-width', `${this.#layout.inspectorWidth}px`);
    this.#root.style.setProperty('--center-top-ratio', String(this.#layout.centerRatio));
    this.#root.style.setProperty('--inspector-top-ratio', String(this.#layout.inspectorRatio));
    this.#root.dataset.maximizedPanel = this.#layout.maximizedPanel ?? '';
    this.#root.classList.toggle('workbench-maximized', this.#layout.maximizedPanel !== null);

    for (const panel of this.#panels) {
      const presented = this.#layout.maximizedPanel === null ||
        panel.dataset.panelId === this.#layout.maximizedPanel;
      panel.classList.toggle('workbench-concealed', !presented);
      panel.classList.toggle('workbench-maximized-panel',
        this.#layout.maximizedPanel !== null && panel.dataset.panelId === this.#layout.maximizedPanel);
      panel.setAttribute('aria-hidden', presented ? 'false' : 'true');
      this.#onPresentationChange(panel.dataset.panelId, presented);
    }
    for (const button of this.#buttons) {
      const restoring = button.dataset.panelMaximize === this.#layout.maximizedPanel;
      button.textContent = restoring ? 'RESTORE' : 'MAXIMIZE';
      button.setAttribute('aria-label', restoring ? 'Restore workbench layout' : `Maximize ${button.dataset.panelMaximize}`);
    }
    this.#updateSeparatorAria();
  }

  #updateSeparatorAria() {
    for (const separator of this.#separators) {
      const splitter = separator.dataset.workbenchSeparator;
      let value = 0;
      if (splitter === 'structure') {
        value = this.#layout.structureWidth;
      }
      else if (splitter === 'inspector') {
        value = this.#layout.inspectorWidth;
      }
      else if (splitter === 'centerRows') {
        value = Math.round(this.#layout.centerRatio * 100);
      }
      else if (splitter === 'inspectorRows') {
        value = Math.round(this.#layout.inspectorRatio * 100);
      }
      separator.setAttribute('aria-valuenow', value);
    }
  }

  #beginDrag(separator, event) {
    if (event.button !== 0 || this.#layout.maximizedPanel !== null) {
      return;
    }
    const splitter = separator.dataset.workbenchSeparator;
    this.#drag = {
      pointerId: event.pointerId,
      splitter,
      start: coordinateFor(splitter, event),
      layout: { ...this.#layout },
    };
    separator.setPointerCapture(event.pointerId);
    event.preventDefault();
  }

  #moveDrag(separator, event) {
    if (this.#drag === null || this.#drag.pointerId !== event.pointerId) {
      return;
    }
    const delta = coordinateFor(this.#drag.splitter, event) - this.#drag.start;
    this.#layout = applySplitterDelta(this.#drag.layout, this.#drag.splitter, delta, this.#viewport());
    this.#applyLayout();
    this.#persist();
  }

  #finishDrag(separator, event) {
    if (this.#drag === null || this.#drag.pointerId !== event.pointerId) {
      return;
    }
    separator.releasePointerCapture?.(event.pointerId);
    this.#drag = null;
  }

  #onSeparatorKey(separator, event) {
    if (this.#layout.maximizedPanel !== null) {
      return;
    }
    const splitter = separator.dataset.workbenchSeparator;
    let delta = null;
    if (splitter === 'structure' || splitter === 'inspector') {
      if (event.key === 'ArrowLeft') {
        delta = -KEYBOARD_STEP;
      }
      if (event.key === 'ArrowRight') {
        delta = KEYBOARD_STEP;
      }
    } else {
      if (event.key === 'ArrowUp') {
        delta = -KEYBOARD_STEP;
      }
      if (event.key === 'ArrowDown') {
        delta = KEYBOARD_STEP;
      }
    }

    if (event.key === 'Home') {
      const defaults = defaultWorkbenchLayout();
      if (splitter === 'structure') {
        this.#layout.structureWidth = defaults.structureWidth;
      }
      else if (splitter === 'inspector') {
        this.#layout.inspectorWidth = defaults.inspectorWidth;
      }
      else if (splitter === 'centerRows') {
        this.#layout.centerRatio = defaults.centerRatio;
      }
      else if (splitter === 'inspectorRows') {
        this.#layout.inspectorRatio = defaults.inspectorRatio;
      }
    } else if (delta !== null) {
      this.#layout = applySplitterDelta(this.#layout, splitter, delta, this.#viewport());
    } else {
      return;
    }
    event.preventDefault();
    this.#applyLayout();
    this.#persist();
  }
}
