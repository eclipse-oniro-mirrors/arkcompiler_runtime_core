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

import assert from 'node:assert/strict';
import test from 'node:test';

import { WORKBENCH_STORAGE_KEY, WorkbenchLayout } from '../src/workbench-layout.js';

class FakeClassList {
  values = new Set();
  add(...names) { names.forEach((name) => this.values.add(name)); }
  remove(...names) { names.forEach((name) => this.values.delete(name)); }
  toggle(name, force) {
    if (force === undefined ? !this.values.has(name) : force) {
      this.values.add(name);
    }
    else this.values.delete(name);
  }
  contains(name) { return this.values.has(name); }
}

class FakeStyle {
  values = new Map();
  setProperty(name, value) { this.values.set(name, value); }
  getPropertyValue(name) { return this.values.get(name) ?? ''; }
}

class FakeElement {
  constructor(dataset = {}) {
    this.dataset = { ...dataset };
    this.classList = new FakeClassList();
    this.style = new FakeStyle();
    this.listeners = new Map();
    this.attributes = new Map();
    this.textContent = '';
    this.captures = new Set();
  }
  addEventListener(type, listener) {
    const values = this.listeners.get(type) ?? [];
    values.push(listener);
    this.listeners.set(type, values);
  }
  removeEventListener(type, listener) {
    this.listeners.set(type, (this.listeners.get(type) ?? []).filter((item) => item !== listener));
  }
  dispatch(type, values = {}) {
    const event = { preventDefault() {}, ...values };
    for (const listener of [...(this.listeners.get(type) ?? [])]) {
      listener(event);
    }
  }
  setAttribute(name, value) { this.attributes.set(name, String(value)); }
  getAttribute(name) { return this.attributes.get(name) ?? null; }
  setPointerCapture(id) { this.captures.add(id); }
  releasePointerCapture(id) { this.captures.delete(id); }
}

class FakeRoot extends FakeElement {
  constructor() {
    super();
    this.rect = { width: 1600, height: 900 };
    this.separators = [
      new FakeElement({ workbenchSeparator: 'structure' }),
      new FakeElement({ workbenchSeparator: 'centerRows' }),
      new FakeElement({ workbenchSeparator: 'inspector' }),
      new FakeElement({ workbenchSeparator: 'inspectorRows' }),
    ];
    this.panels = ['structure-panel', 'disassembly-panel', 'cfg-panel', 'metadata-panel', 'hex-panel']
      .map((panelId) => new FakeElement({ panelId }));
    this.buttons = this.panels.map((panel) => new FakeElement({ panelMaximize: panel.dataset.panelId }));
  }
  getBoundingClientRect() { return this.rect; }
  querySelectorAll(selector) {
    if (selector === '[data-workbench-separator]') {
      return this.separators;
    }
    if (selector === '[data-workbench-panel]') {
      return this.panels;
    }
    if (selector === '[data-panel-maximize]') {
      return this.buttons;
    }
    return [];
  }
}

class FakeEventTarget extends FakeElement {}

class FakeStorage {
  values = new Map();
  getItem(key) { return this.values.get(key) ?? null; }
  setItem(key, value) { this.values.set(key, value); }
}

class FakeResizeObserver {
  constructor(callback) { this.callback = callback; }
  observed = [];
  disconnected = false;
  observe(target) { this.observed.push(target); }
  disconnect() { this.disconnected = true; }
  trigger(target, width) { this.callback([{ target, contentRect: { width } }]); }
}

function createHarness(stored = null) {
  const root = new FakeRoot();
  const storage = new FakeStorage();
  if (stored !== null) {
    storage.setItem(WORKBENCH_STORAGE_KEY, stored);
  }
  const windowTarget = new FakeEventTarget();
  const resized = [];
  const presentation = [];
  let observer;
  const layout = new WorkbenchLayout({
    root,
    storage,
    windowTarget,
    resizeObserverFactory: (callback) => {
      observer = new FakeResizeObserver(callback);
      return observer;
    },
    onPanelResize: (panelId, width) => resized.push([panelId, width]),
    onPresentationChange: (panelId, visible) => presentation.push([panelId, visible]),
  });
  layout.mount();
  return { layout, root, storage, windowTarget, resized, presentation, observer };
}

test('mount restores geometry and observes every feature panel width', () => {
  const harness = createHarness(JSON.stringify({
    version: 1,
    layout: { structureWidth: 320, inspectorWidth: 460, centerRatio: 0.6, inspectorRatio: 0.4,
      maximizedPanel: null },
  }));
  assert.equal(harness.root.style.getPropertyValue('--structure-width'), '320px');
  assert.equal(harness.root.style.getPropertyValue('--inspector-width'), '460px');
  assert.equal(harness.root.style.getPropertyValue('--center-top-ratio'), '0.6');
  assert.equal(harness.root.style.getPropertyValue('--inspector-top-ratio'), '0.4');
  assert.equal(harness.observer.observed.length, 5);

  const hex = harness.root.panels.find((panel) => panel.dataset.panelId === 'hex-panel');
  harness.observer.trigger(hex, 418);
  assert.deepEqual(harness.resized, [['hex-panel', 418]]);
});

test('pointer and keyboard resizing update CSS and persisted geometry', () => {
  const harness = createHarness();
  const structure = harness.root.separators[0];
  structure.dispatch('pointerdown', { pointerId: 7, button: 0, clientX: 280, clientY: 0 });
  structure.dispatch('pointermove', { pointerId: 7, clientX: 320, clientY: 0 });
  structure.dispatch('pointerup', { pointerId: 7, clientX: 320, clientY: 0 });
  assert.equal(harness.root.style.getPropertyValue('--structure-width'), '320px');
  assert.equal(JSON.parse(harness.storage.getItem(WORKBENCH_STORAGE_KEY)).layout.structureWidth, 320);

  const centerRows = harness.root.separators[1];
  centerRows.dispatch('keydown', { key: 'ArrowDown' });
  assert.ok(Number(harness.root.style.getPropertyValue('--center-top-ratio')) > 0.52);
  centerRows.dispatch('keydown', { key: 'Home' });
  assert.equal(harness.root.style.getPropertyValue('--center-top-ratio'), '0.52');
});

test('maximize keeps panels mounted, Escape restores, and destroy releases observers and listeners', () => {
  const harness = createHarness();
  const cfgButton = harness.root.buttons.find((button) => button.dataset.panelMaximize === 'cfg-panel');
  cfgButton.dispatch('click');
  assert.equal(harness.root.dataset.maximizedPanel, 'cfg-panel');
  const cfg = harness.root.panels.find((panel) => panel.dataset.panelId === 'cfg-panel');
  const hex = harness.root.panels.find((panel) => panel.dataset.panelId === 'hex-panel');
  assert.equal(cfg.classList.contains('workbench-concealed'), false);
  assert.equal(hex.classList.contains('workbench-concealed'), true);
  assert.equal(cfgButton.textContent, 'RESTORE');

  harness.windowTarget.dispatch('keydown', { key: 'Escape' });
  assert.equal(harness.root.dataset.maximizedPanel, '');
  assert.equal(hex.classList.contains('workbench-concealed'), false);
  assert.ok(harness.presentation.some(([panelId, visible]) => panelId === 'hex-panel' && !visible));
  assert.ok(harness.presentation.some(([panelId, visible]) => panelId === 'hex-panel' && visible));

  harness.layout.destroy();
  assert.equal(harness.observer.disconnected, true);
  cfgButton.dispatch('click');
  assert.equal(harness.root.dataset.maximizedPanel, '');
});
