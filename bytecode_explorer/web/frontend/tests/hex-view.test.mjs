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
import { readFile } from 'node:fs/promises';
import test from 'node:test';

import { HexView } from '../src/hex-view.js';

globalThis.requestAnimationFrame = (callback) => {
  callback();
  return 0;
};

class FakeClassList {
  #classes = new Set();

  add(...names) {
    names.forEach((name) => this.#classes.add(name));
  }

  remove(...names) {
    names.forEach((name) => this.#classes.delete(name));
  }

  contains(name) {
    return this.#classes.has(name);
  }
}

class FakeElement {
  constructor(ownerDocument, fragment = false) {
    this.ownerDocument = ownerDocument;
    this.fragment = fragment;
    this.children = [];
    this.classList = new FakeClassList();
    this.style = {};
    this.dataset = {};
    this.listeners = new Map();
    this.className = '';
    this.textContent = '';
    this.value = '';
    this.scrollTop = 0;
    this.clientHeight = 56;
  }

  addEventListener(type, listener) {
    const listeners = this.listeners.get(type) ?? [];
    listeners.push(listener);
    this.listeners.set(type, listeners);
  }

  append(...children) {
    for (const child of children) {
      if (child.fragment) {
        this.children.push(...child.children);
      }
      else this.children.push(child);
    }
  }

  replaceChildren(...children) {
    this.children = [];
    this.append(...children);
  }

  async dispatch(type) {
    const event = { preventDefault() {} };
    await Promise.all((this.listeners.get(type) ?? []).map((listener) => listener(event)));
  }
}

class FakeDocument {
  createElement() {
    return new FakeElement(this);
  }

  createDocumentFragment() {
    return new FakeElement(this, true);
  }
}

function deferred() {
  let resolve;
  let reject;
  const promise = new Promise((resolvePromise, rejectPromise) => {
    resolve = resolvePromise;
    reject = rejectPromise;
  });
  return { promise, resolve, reject };
}

class ControlledClient {
  requests = [];

  get(path, channel) {
    const operation = deferred();
    const request = { path, channel, claimed: false, ...operation };
    this.requests.push(request);
    return operation.promise;
  }

  next(channel) {
    const request = this.requests.find((candidate) => candidate.channel === channel && !candidate.claimed);
    assert.ok(request, `expected a pending ${channel} request`);
    request.claimed = true;
    return request;
  }

  count(channel) {
    return this.requests.filter((request) => request.channel === channel).length;
  }
}

function createHarness() {
  const document = new FakeDocument();
  const elements = Object.fromEntries([
    'root', 'jumpForm', 'summary', 'offset', 'notice', 'viewport', 'spacer', 'rows',
  ].map((name) => [name, new FakeElement(document)]));
  elements.root.classList.add('hidden');
  const client = new ControlledClient();
  const callbacks = { statuses: [], errors: [], clearErrors: 0, locations: [] };
  const view = new HexView({
    client,
    elements,
    onStatus: (status) => callbacks.statuses.push(status),
    onError: (error) => callbacks.errors.push(error),
    onClearError: () => { callbacks.clearErrors += 1; },
    onLocation: (location) => callbacks.locations.push(location),
  });
  return { view, client, callbacks, elements };
}

function hexResponse(offset = 0, size = 32, firstByte = offset) {
  return {
    apiVersion: 1,
    documentId: 'test-document',
    fileSize: '0x40',
    window: {
      offset: `0x${offset.toString(16)}`,
      size,
      eof: offset + size === 64,
      bytes: Array.from({ length: size }, (_, index) => (firstByte + index) & 0xff),
    },
  };
}

function locationResponse(offset = 0) {
  return {
    apiVersion: 1,
    documentId: 'test-document',
    offset: `0x${offset.toString(16)}`,
    structureMatch: {
      entityId: 'other-entity',
      rangeKind: 'primary',
      range: { offset: '0x0', size: 4 },
      backingIndex: null,
    },
  };
}

function findElement(root, predicate) {
  if (predicate(root)) {
    return root;
  }
  for (const child of root.children) {
    const match = findElement(child, predicate);
    if (match !== null) {
      return match;
    }
  }
  return null;
}

async function flushMicrotasks() {
  await Promise.resolve();
  await Promise.resolve();
}

async function beginLocationRequest() {
  const harness = createHarness();
  harness.view.setFileSize(64n);
  harness.view.show();
  const reveal = harness.view.revealRange({ offset: 0n, size: 1n }, 'structure');
  harness.client.next('hex-window').resolve(hexResponse());
  await reveal;
  harness.view.setInstructionContext('current-entity', [{ fileRange: { offset: '0x0', size: 1 } }]);
  const button = findElement(harness.elements.rows,
    (candidate) => candidate.dataset.offset === '0x0');
  assert.ok(button, 'the real HexView render should create the requested byte button');
  const click = button.dispatch('click');
  const request = harness.client.next('hex-location');
  return { ...harness, click, request };
}

async function loadedHarness() {
  const harness = createHarness();
  harness.view.setFileSize(64n);
  harness.view.show();
  const reveal = harness.view.revealRange({ offset: 0n, size: 1n }, 'structure');
  harness.client.next('hex-window').resolve(hexResponse());
  await reveal;
  return harness;
}

test('instruction and CFG selections reuse the current Hex window', async () => {
  const harness = await loadedHarness();
  assert.equal(harness.client.count('hex-window'), 1);
  await harness.view.revealRange({ offset: 4n, size: 2n }, 'instruction');
  await harness.view.revealRange({ offset: 8n, size: 2n }, 'cfg');
  assert.equal(harness.client.count('hex-window'), 1);
  assert.match(harness.elements.summary.textContent, /0x00000008 \+2/);
});

const locationInvalidators = [
  ['hide', async ({ view }) => view.hide()],
  ['revealRange', async ({ view, client }) => {
    const reveal = view.revealRange({ offset: 0x10n, size: 1n }, 'structure');
    client.next('hex-window').resolve(hexResponse());
    await reveal;
  }],
  ['clearInstructionContext', async ({ view }) => view.clearInstructionContext()],
];

for (const [name, invalidate] of locationInvalidators) {
  test(`${name} suppresses a pending location success`, async () => {
    const harness = await beginLocationRequest();
    await invalidate(harness);
    harness.request.resolve(locationResponse());
    await harness.click;
    assert.deepEqual(harness.callbacks.locations, []);
  });

  test(`${name} suppresses a pending location error`, async () => {
    const harness = await beginLocationRequest();
    await invalidate(harness);
    harness.request.reject(new Error('late location failure'));
    await harness.click;
    assert.deepEqual(harness.callbacks.errors, []);
  });
}

test('hide suppresses a pending window success and show starts a fresh request', async () => {
  const harness = createHarness();
  harness.view.setFileSize(64n);
  harness.view.show();
  await flushMicrotasks();
  const first = harness.client.next('hex-window');
  const statusCountAtHide = harness.callbacks.statuses.length;
  harness.view.hide();
  first.resolve(hexResponse());
  await flushMicrotasks();

  assert.equal(harness.callbacks.statuses.length, statusCountAtHide);
  assert.equal(harness.callbacks.clearErrors, 0);
  assert.equal(harness.elements.rows.children.length, 0);

  harness.view.show();
  await flushMicrotasks();
  assert.equal(harness.client.count('hex-window'), 2);
  const second = harness.client.next('hex-window');
  second.resolve(hexResponse());
  await flushMicrotasks();
  assert.ok(harness.elements.rows.children.length > 0);
});

test('hide suppresses a pending window error', async () => {
  const harness = createHarness();
  harness.view.setFileSize(64n);
  harness.view.show();
  await flushMicrotasks();
  const request = harness.client.next('hex-window');
  harness.view.hide();
  request.reject(new Error('late window failure'));
  await flushMicrotasks();
  assert.deepEqual(harness.callbacks.errors, []);
});

test('null range suppresses a pending window success and preserves the last window', async () => {
  const harness = await loadedHarness();
  const pending = harness.view.revealRange({ offset: 0x10n, size: 1n }, 'structure');
  const request = harness.client.next('hex-window');
  await harness.view.revealRange(null, 'structure');
  const statusCount = harness.callbacks.statuses.length;
  const clearErrorCount = harness.callbacks.clearErrors;
  request.resolve(hexResponse(0, 32, 0x80));
  await pending;

  const retained = findElement(harness.elements.rows,
    (candidate) => candidate.dataset.offset === '0x0');
  assert.equal(retained.textContent, '00');
  assert.equal(harness.callbacks.statuses.length, statusCount);
  assert.equal(harness.callbacks.clearErrors, clearErrorCount);

  const fresh = harness.view.revealRange({ offset: 0x10n, size: 1n }, 'structure');
  harness.client.next('hex-window').resolve(hexResponse(0, 32, 0x40));
  await fresh;
  const updated = findElement(harness.elements.rows,
    (candidate) => candidate.dataset.offset === '0x0');
  assert.equal(updated.textContent, '40');
});

test('invalid range suppresses a pending window error and preserves the last window', async () => {
  const harness = await loadedHarness();
  const pending = harness.view.revealRange({ offset: 0x10n, size: 1n }, 'structure');
  const request = harness.client.next('hex-window');
  await harness.view.revealRange({ offset: 64n, size: 1n }, 'structure');
  request.reject(new Error('late invalidated window failure'));
  await pending;

  const retained = findElement(harness.elements.rows,
    (candidate) => candidate.dataset.offset === '0x0');
  assert.equal(retained.textContent, '00');
  assert.deepEqual(harness.callbacks.errors, []);
});

test('the Hex header uses sixteen cells on the same tracks as byte rows', async () => {
  const html = await readFile(new URL('../index.html', import.meta.url), 'utf8');
  const css = await readFile(new URL('../styles.css', import.meta.url), 'utf8');
  const header = html.match(/<div class="hex-column-bytes">([\s\S]*?)<\/div>/);
  assert.ok(header, 'expected a dedicated Hex byte-column header grid');
  const labels = [...header[1].matchAll(/<span>([0-9A-F]{2})<\/span>/g)].map((match) => match[1]);
  assert.deepEqual(labels, Array.from({ length: 16 }, (_, index) => index.toString(16).padStart(2, '0').toUpperCase()));
  assert.match(css, /\.hex-column-bytes,\s*\.hex-byte-grid\s*\{[^}]*grid-template-columns:\s*repeat\(16,/s);
  assert.match(css, /\[data-bytes-per-row="8"\][^}]*\.hex-byte-grid\s*\{[^}]*grid-template-columns:\s*repeat\(8,/s);
});

test('resizing Hex changes cached rows and header density without requesting another window', async () => {
  const harness = await loadedHarness();
  assert.equal(harness.client.count('hex-window'), 1);
  harness.view.resize(420);
  assert.equal(harness.elements.root.dataset.bytesPerRow, '8');
  assert.equal(harness.client.count('hex-window'), 1);
  assert.equal(harness.elements.rows.children[0].children[1].children.length, 8);

  harness.view.resize(900);
  assert.equal(harness.elements.root.dataset.bytesPerRow, '16');
  assert.equal(harness.client.count('hex-window'), 1);
  assert.equal(harness.elements.rows.children[0].children[1].children.length, 16);
});

test('the optional Hex notice cannot displace the header and viewport grid rows', async () => {
  const css = await readFile(new URL('../styles.css', import.meta.url), 'utf8');
  const expectedRows = [
    ['hex-toolbar', 1],
    ['hex-notice', 2],
    ['hex-column-header', 3],
    ['hex-viewport', 4],
  ];
  for (const [className, row] of expectedRows) {
    assert.match(css, new RegExp(`\\.${className}\\s*\\{[^}]*grid-row:\\s*${row}\\s*;`, 's'));
  }
});
