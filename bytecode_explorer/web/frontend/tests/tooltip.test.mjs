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

import test from 'node:test';
import assert from 'node:assert/strict';
import { computeTooltipPosition, installTooltips } from '../src/tooltip.js';

class FakeElement {
  constructor(document) {
    this.document = document;
    this.parent = null;
    this.children = [];
    this.attributes = new Map();
    this.className = '';
    this.id = '';
    this.textContent = '';
    this.style = {};
    this.offsetWidth = 0;
    this.offsetHeight = 0;
    this.rect = { left: 0, top: 0, right: 0, bottom: 0, width: 0, height: 0 };
    this.removed = false;
  }

  get classList() {
    const self = this;
    return {
      add(...names) {
        const set = new Set(self.className.split(/\s+/).filter(Boolean));
        names.forEach((name) => set.add(name));
        self.className = [...set].join(' ');
      },
      remove(...names) {
        const set = new Set(self.className.split(/\s+/).filter(Boolean));
        names.forEach((name) => set.delete(name));
        self.className = [...set].join(' ');
      },
      contains(name) {
        return self.className.split(/\s+/).includes(name);
      },
    };
  }

  setAttribute(name, value) {
    this.attributes.set(name, String(value));
  }

  getAttribute(name) {
    return this.attributes.has(name) ? this.attributes.get(name) : null;
  }

  removeAttribute(name) {
    this.attributes.delete(name);
  }

  appendChild(child) {
    child.parent = this;
    this.children.push(child);
    return child;
  }

  remove() {
    if (this.parent) {
      const index = this.parent.children.indexOf(this);
      if (index >= 0) {
        this.parent.children.splice(index, 1);
      }
      this.parent = null;
    }
    this.removed = true;
  }

  closest(selector) {
    let node = this;
    while (node) {
      if (selector === '[data-tip]' && node.attributes.has('data-tip')) {
        return node;
      }
      node = node.parent;
    }
    return null;
  }

  contains(node) {
    let current = node;
    while (current) {
      if (current === this) {
        return true;
      }
      current = current.parent;
    }
    return false;
  }

  getBoundingClientRect() {
    return { ...this.rect };
  }
}

class FakeEventTarget {
  constructor() {
    this.listeners = new Map();
  }

  addEventListener(type, handler) {
    this.listeners.set(type, (this.listeners.get(type) ?? []).concat(handler));
  }

  removeEventListener(type, handler) {
    this.listeners.set(type, (this.listeners.get(type) ?? []).filter((candidate) => candidate !== handler));
  }

  dispatch(type, event) {
    (this.listeners.get(type) ?? []).forEach((handler) => handler(event));
  }
}

class FakeDocument extends FakeEventTarget {
  constructor() {
    super();
    this.body = new FakeElement(this);
  }

  createElement() {
    return new FakeElement(this);
  }
}

function createFakeTimers() {
  const pending = [];
  return {
    setTimeout: (handler, delay) => {
      const id = { handler, delay };
      pending.push(id);
      return id;
    },
    clearTimeout: (id) => {
      const index = pending.indexOf(id);
      if (index >= 0) {
        pending.splice(index, 1);
      }
    },
    fire() {
      const id = pending.shift();
      if (id) {
        id.handler();
      }
      return id;
    },
    pending() {
      return pending.slice();
    },
  };
}

function createHarness() {
  const document = new FakeDocument();
  const window = new FakeEventTarget();
  window.innerWidth = 1280;
  window.innerHeight = 800;
  const timers = createFakeTimers();
  const dispose = installTooltips({
    document,
    window,
    setTimeout: timers.setTimeout,
    clearTimeout: timers.clearTimeout,
  });
  const tooltip = () => document.body.children.find((child) => child.id === 'app-tooltip');
  return { document, window, timers, dispose, tooltip };
}

function addTarget(document, { tip = 'hello', rect } = {}) {
  const element = document.createElement();
  element.setAttribute('data-tip', tip);
  element.rect = rect ?? { left: 100, top: 100, right: 180, bottom: 120, width: 80, height: 20 };
  document.body.appendChild(element);
  return element;
}

test('computeTooltipPosition centers the tooltip below the anchor', () => {
  const position = computeTooltipPosition({ left: 190, top: 10, bottom: 30, width: 100 }, 200, 20, 1280, 800);
  assert.deepEqual(position, { left: 140, top: 38 });
});

test('computeTooltipPosition clamps to the left viewport margin', () => {
  const position = computeTooltipPosition({ left: 0, top: 10, bottom: 30, width: 40 }, 200, 20, 1280, 800);
  assert.equal(position.left, 8);
});

test('computeTooltipPosition clamps to the right viewport margin', () => {
  const position = computeTooltipPosition({ left: 1240, top: 10, bottom: 30, width: 40 }, 200, 20, 1280, 800);
  assert.equal(position.left, 1280 - 200 - 8);
});

test('computeTooltipPosition flips above when the viewport bottom is close', () => {
  const position = computeTooltipPosition({ left: 540, top: 700, bottom: 780, width: 200 }, 240, 40, 1280, 800);
  assert.equal(position.top, 700 - 8 - 40);
});

test('computeTooltipPosition stays below when flipping does not fit either', () => {
  const position = computeTooltipPosition({ left: 540, top: 700, bottom: 780, width: 200 }, 240, 780, 1280, 800);
  assert.equal(position.top, 788);
});

test('installTooltips shows the data-tip text after the delay', () => {
  const harness = createHarness();
  const target = addTarget(harness.document);
  harness.document.dispatch('pointerover', { target });
  assert.equal(harness.timers.pending().length, 1);
  assert.equal(harness.timers.pending()[0].delay, 350);
  assert.equal(harness.tooltip().classList.contains('visible'), false);
  harness.timers.fire();
  assert.equal(harness.tooltip().textContent, 'hello');
  assert.equal(harness.tooltip().classList.contains('visible'), true);
  assert.equal(target.getAttribute('aria-describedby'), 'app-tooltip');
  assert.match(harness.tooltip().style.left, /px$/);
  assert.match(harness.tooltip().style.top, /px$/);
  harness.dispose();
});

test('installTooltips cancels the pending tooltip when the pointer leaves early', () => {
  const harness = createHarness();
  const target = addTarget(harness.document);
  harness.document.dispatch('pointerover', { target });
  harness.document.dispatch('pointerout', { target, relatedTarget: null });
  assert.equal(harness.timers.pending().length, 0);
  harness.timers.fire();
  assert.equal(harness.tooltip().textContent, '');
  assert.equal(harness.tooltip().classList.contains('visible'), false);
  harness.dispose();
});

test('installTooltips resolves nested targets to their data-tip ancestor', () => {
  const harness = createHarness();
  const target = addTarget(harness.document);
  const child = harness.document.createElement();
  target.appendChild(child);
  harness.document.dispatch('pointerover', { target: child });
  harness.timers.fire();
  assert.equal(harness.tooltip().textContent, 'hello');
  assert.equal(target.getAttribute('aria-describedby'), 'app-tooltip');
  harness.dispose();
});

test('installTooltips keeps the tooltip when moving between target children', () => {
  const harness = createHarness();
  const target = addTarget(harness.document);
  const child = harness.document.createElement();
  target.appendChild(child);
  harness.document.dispatch('pointerover', { target });
  harness.timers.fire();
  harness.document.dispatch('pointerout', { target, relatedTarget: child });
  assert.equal(harness.tooltip().classList.contains('visible'), true);
  harness.dispose();
});

test('installTooltips ignores elements with an empty data-tip', () => {
  const harness = createHarness();
  addTarget(harness.document, { tip: '' });
  const target = harness.document.body.children[harness.document.body.children.length - 1];
  harness.document.dispatch('pointerover', { target });
  harness.timers.fire();
  assert.equal(harness.tooltip().classList.contains('visible'), false);
  assert.equal(target.getAttribute('aria-describedby'), null);
  harness.dispose();
});

test('installTooltips hides on scroll', () => {
  const harness = createHarness();
  const target = addTarget(harness.document);
  harness.document.dispatch('pointerover', { target });
  harness.timers.fire();
  assert.equal(harness.tooltip().classList.contains('visible'), true);
  harness.window.dispatch('scroll');
  assert.equal(harness.tooltip().classList.contains('visible'), false);
  assert.equal(target.getAttribute('aria-describedby'), null);
  harness.dispose();
});

test('installTooltips hides on pointerdown', () => {
  const harness = createHarness();
  const target = addTarget(harness.document);
  harness.document.dispatch('pointerover', { target });
  harness.timers.fire();
  assert.equal(harness.tooltip().classList.contains('visible'), true);
  harness.document.dispatch('pointerdown', { target });
  assert.equal(harness.tooltip().classList.contains('visible'), false);
  assert.equal(target.getAttribute('aria-describedby'), null);
  harness.dispose();
});

test('installTooltips does not restart the timer for the hovered target', () => {
  const harness = createHarness();
  const target = addTarget(harness.document);
  harness.document.dispatch('pointerover', { target });
  harness.timers.fire();
  harness.document.dispatch('pointerover', { target });
  assert.equal(harness.timers.pending().length, 0);
  assert.equal(harness.tooltip().classList.contains('visible'), true);
  harness.dispose();
});

test('installTooltips hides the previous tooltip when the target switches', () => {
  const harness = createHarness();
  const first = addTarget(harness.document, { tip: 'first' });
  const second = addTarget(harness.document, { tip: 'second', rect: { left: 400, top: 100, right: 480, bottom: 120, width: 80, height: 20 } });
  harness.document.dispatch('pointerover', { target: first });
  harness.timers.fire();
  harness.document.dispatch('pointerover', { target: second });
  assert.equal(harness.tooltip().classList.contains('visible'), false);
  assert.equal(first.getAttribute('aria-describedby'), null);
  harness.timers.fire();
  assert.equal(harness.tooltip().textContent, 'second');
  assert.equal(second.getAttribute('aria-describedby'), 'app-tooltip');
  harness.dispose();
});

test('installTooltips dispose removes the tooltip and stops reacting', () => {
  const harness = createHarness();
  const target = addTarget(harness.document);
  const tooltip = harness.tooltip();
  harness.dispose();
  assert.equal(tooltip.parent, null);
  harness.document.dispatch('pointerover', { target });
  assert.equal(harness.timers.pending().length, 0);
});
