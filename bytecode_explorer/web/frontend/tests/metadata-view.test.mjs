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

import { METADATA_POLL_MS, metadataKey } from '../src/metadata-view-model.js';
import { MetadataView } from '../src/metadata-view.js';

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

  toggle(name, force) {
    const enabled = force === undefined ? !this.#classes.has(name) : force;
    if (enabled) this.#classes.add(name); else this.#classes.delete(name);
    return enabled;
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
    this._className = '';
    this._textContent = '';
    this.value = '';
    this.disabled = false;
    this.type = '';
    this.attributes = new Map();
  }

  // Real DOM keeps className and classList in sync; mirror the assignment direction
  // so classList.contains() works on elements built via className writes.
  get className() {
    return this._className;
  }

  set className(value) {
    this._className = String(value);
    for (const name of this._className.split(/\s+/)) {
      if (name !== '') {
        this.classList.add(name);
      }
    }
  }

  addEventListener(type, listener) {
    const listeners = this.listeners.get(type) ?? [];
    listeners.push(listener);
    this.listeners.set(type, listeners);
  }

  setAttribute(name, value) {
    this.attributes.set(name, String(value));
  }

  getAttribute(name) {
    return this.attributes.get(name) ?? null;
  }

  get textContent() {
    return this._textContent + this.children.map((child) => child.textContent).join('');
  }

  set textContent(value) {
    this._textContent = String(value);
    this.children = [];
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
    return this.#request('GET', path, undefined, channel);
  }

  post(path, body, channel) {
    return this.#request('POST', path, body, channel);
  }

  #request(method, path, body, channel) {
    const operation = deferred();
    const request = { method, path, body, channel, claimed: false, ...operation };
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

function statusWire(state = 'Building', revision = 1, overrides = {}) {
  return {
    apiVersion: 1,
    documentId: 'document-one',
    state,
    revision: String(revision),
    scannedMethods: state === 'Building' ? '1' : '4',
    totalMethods: '4',
    objectCount: '400',
    referenceCount: '400',
    diagnostics: [],
    diagnosticsTruncated: false,
    ...overrides,
  };
}

function objectWire(index, kind = 'String') {
  const offset = `0x${(0x300 + index).toString(16)}`;
  return {
    key: { kind, offset },
    kind,
    range: { offset, size: 2 },
    preview: `${kind} ${index}`,
    truncated: false,
    origins: ['Structure'],
    entityIds: [String(65 + index)],
    entityIdsTruncated: false,
    referenceCount: 150,
    diagnostics: [],
    diagnosticsTruncated: false,
  };
}

function objectPage(start = 0, count = 2, nextCursor = null, revision = 1) {
  return {
    apiVersion: 1,
    documentId: 'document-one',
    revision: String(revision),
    total: '400',
    items: Array.from({ length: count }, (_, index) => {
      const object = objectWire(start + index);
      return index < 64 ? object : { ...object, entityIds: [], entityIdsTruncated: true };
    }),
    nextCursor,
  };
}

function objectDetail(index = 0) {
  return {
    apiVersion: 1,
    documentId: 'document-one',
    ...objectWire(index),
    value: `full value ${index}`,
    valueTruncated: false,
  };
}

function referenceWire(index = 0) {
  return {
    kind: 'Instruction Operand',
    sourceEntityIds: [String(900 + index)],
    sourceEntityIdsTruncated: false,
    sourceMethodOffset: `0x${(0x100 + index).toString(16)}`,
    pc: index,
    sourceRange: { offset: `0x${(0x700 + index * 2).toString(16)}`, size: 2 },
    sourceName: `source ${index}`,
    mnemonic: 'lda.str',
    operandIndex: 0,
  };
}

function referencePage(objectIndex = 0, start = 0, count = 2, nextCursor = null, revision = 1) {
  const object = objectWire(objectIndex);
  return {
    apiVersion: 1,
    documentId: 'document-one',
    key: object.key,
    revision: String(revision),
    total: '400',
    items: Array.from({ length: count }, (_, index) => {
      const reference = referenceWire(start + index);
      return index < 64 ? reference : {
        ...reference, sourceEntityIds: [], sourceEntityIdsTruncated: true,
      };
    }),
    nextCursor,
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

function findAll(root, predicate, output = []) {
  if (predicate(root)) {
    output.push(root);
  }
  for (const child of root.children) {
    findAll(child, predicate, output);
  }
  return output;
}

async function flushMicrotasks() {
  await Promise.resolve();
  await Promise.resolve();
  await Promise.resolve();
}

function createHarness() {
  const document = new FakeDocument();
  const elements = Object.fromEntries([
    'root', 'kind', 'searchForm', 'search', 'summary', 'notice', 'list', 'objects', 'loadMore',
    'selection', 'detail', 'references', 'referencesBack', 'referencesNext', 'progress', 'progressBar',
  ].map((name) => [name, new FakeElement(document)]));
  elements.root.classList.add('hidden');
  elements.selection.classList.add('hidden');
  elements.selection.append(
    elements.detail, elements.references, elements.referencesBack, elements.referencesNext,
  );
  elements.kind.value = 'All';
  const client = new ControlledClient();
  const callbacks = {
    statuses: [], errors: [], clearErrors: 0, structures: [], disassemblies: [], hex: [],
  };
  const timers = [];
  const schedule = (callback, delay) => {
    const timer = { callback, delay, cancelled: false };
    timers.push(timer);
    return timer;
  };
  const cancelSchedule = (timer) => { timer.cancelled = true; };
  const view = new MetadataView({
    client,
    elements,
    onStatus: (status) => callbacks.statuses.push(status),
    onError: (error) => callbacks.errors.push(error),
    onClearError: () => { callbacks.clearErrors += 1; },
    onStructure: (payload) => callbacks.structures.push(payload),
    onDisassembly: (payload) => callbacks.disassemblies.push(payload),
    onHex: (payload) => callbacks.hex.push(payload),
    schedule,
    cancelSchedule,
  });
  return { view, client, callbacks, elements, timers };
}

function nextLiveTimer(harness) {
  const timer = harness.timers.find((candidate) => !candidate.cancelled && !candidate.ran);
  assert.ok(timer, 'expected one live timer');
  timer.ran = true;
  timer.callback();
  return timer;
}

async function resolveInitial(harness, { state = 'Building', page = objectPage() } = {}) {
  harness.client.next('metadata-start').resolve(statusWire(state));
  harness.client.next('metadata-status').resolve(statusWire(state));
  harness.client.next('metadata-objects').resolve(page);
  await flushMicrotasks();
}

async function selectObject(harness, index = 0) {
  const key = metadataKey('String', BigInt(0x300 + index));
  const button = findElement(harness.elements.objects,
    (candidate) => candidate.dataset.action === 'expand' && candidate.dataset.key === key);
  assert.ok(button, `expected expand button for ${key}`);
  await button.dispatch('click');
  return {
    detail: harness.client.next('metadata-object'),
    references: harness.client.next('metadata-references'),
  };
}

test('mount exposes the base catalog without starting the reference index', async () => {
  const harness = createHarness();
  harness.view.mount();
  assert.equal(harness.elements.root.classList.contains('hidden'), false);
  assert.equal(harness.client.count('metadata-start'), 0);
  assert.equal(harness.client.count('metadata-status'), 1);
  assert.equal(harness.client.count('metadata-objects'), 1);

  harness.view.activate();
  harness.view.activate();
  assert.equal(harness.client.count('metadata-start'), 1);
  assert.deepEqual(harness.client.next('metadata-start').body, {});
});

test('panel resizing is presentation-only and hidden presentation pauses polling', async () => {
  const harness = createHarness();
  harness.elements.search.value = 'retained query';
  harness.view.resize(420);
  assert.equal(harness.elements.root.classList.contains('metadata-compact'), true);
  assert.equal(harness.elements.search.value, 'retained query');
  harness.view.resize(800);
  assert.equal(harness.elements.root.classList.contains('metadata-compact'), false);

  harness.view.show();
  await resolveInitial(harness);
  assert.equal(harness.timers.filter((timer) => !timer.cancelled).length, 1);
  harness.view.setPresented(false);
  assert.equal(harness.timers.filter((timer) => !timer.cancelled).length, 0);
  assert.equal(harness.elements.root.classList.contains('hidden'), false);
  harness.view.setPresented(true);
  assert.equal(harness.client.count('metadata-status'), 2);
  assert.equal(harness.client.count('metadata-start'), 1);
});

test('first show starts once and requests status plus page one while later shows reuse data', async () => {
  const harness = createHarness();
  harness.view.show();
  assert.equal(harness.client.count('metadata-start'), 1);
  assert.deepEqual(harness.client.next('metadata-start').body, {});
  assert.equal(harness.client.count('metadata-status'), 1);
  assert.equal(harness.client.count('metadata-objects'), 1);
  harness.client.requests.find((request) => request.channel === 'metadata-start').resolve(statusWire('Ready'));
  harness.client.next('metadata-status').resolve(statusWire('Ready'));
  harness.client.next('metadata-objects').resolve(objectPage());
  await flushMicrotasks();

  harness.view.hide();
  harness.view.show();
  assert.equal(harness.client.count('metadata-start'), 1);
  assert.equal(harness.client.count('metadata-status'), 2);
  assert.equal(harness.client.count('metadata-objects'), 1);
  assert.equal(harness.elements.objects.children.length, 2);
});

test('Building schedules one 500 ms poll, hide cancels it, and Ready does not reschedule', async () => {
  const harness = createHarness();
  harness.view.show();
  await resolveInitial(harness);
  assert.equal(harness.timers.length, 1);
  assert.equal(harness.timers[0].delay, METADATA_POLL_MS);
  harness.view.hide();
  assert.equal(harness.timers[0].cancelled, true);

  harness.view.show();
  const status = harness.client.next('metadata-status');
  status.resolve(statusWire('Ready'));
  await flushMicrotasks();
  assert.equal(harness.timers.filter((timer) => !timer.cancelled).length, 0);
});

test('status progress reflects scanned methods and reaches 100 percent for Ready', async () => {
  const harness = createHarness();
  harness.view.show();
  await resolveInitial(harness);
  assert.equal(harness.elements.progressBar.style.width, '25%');
  assert.equal(harness.elements.progress.getAttribute('aria-valuenow'), '25');
  assert.match(harness.elements.summary.textContent, /Building/);

  nextLiveTimer(harness);
  harness.client.next('metadata-status').resolve(statusWire('Ready'));
  await flushMicrotasks();
  assert.equal(harness.elements.progressBar.style.width, '100%');
  assert.equal(harness.elements.progress.getAttribute('aria-valuenow'), '100');
  assert.match(harness.elements.summary.textContent, /Ready/);
});

test('an active status error reports once and schedules the next 500 ms retry', async () => {
  const harness = createHarness();
  harness.view.show();
  harness.client.next('metadata-start').resolve(statusWire());
  harness.client.next('metadata-status').reject(new Error('temporary status failure'));
  harness.client.next('metadata-objects').resolve(objectPage());
  await flushMicrotasks();
  assert.equal(harness.callbacks.errors.length, 1);
  assert.equal(harness.timers.length, 1);
  assert.equal(harness.timers[0].delay, METADATA_POLL_MS);
});

test('a status revision that resolves first suppresses and reloads an older object page', async () => {
  const harness = createHarness();
  harness.view.show();
  harness.client.next('metadata-start').resolve(statusWire('Ready', 2));
  harness.client.next('metadata-status').resolve(statusWire('Ready', 2));
  const oldPage = harness.client.next('metadata-objects');
  await flushMicrotasks();
  oldPage.resolve(objectPage(0, 2, null, 1));
  await flushMicrotasks();
  assert.equal(harness.elements.objects.children.length, 0);
  assert.equal(harness.client.count('metadata-objects'), 2);
  harness.client.next('metadata-objects').resolve(objectPage(0, 2, null, 2));
  await flushMicrotasks();
  assert.equal(harness.elements.objects.children.length, 2);
});

test('a sliding-window collapse surfaces a back-to-start notice that restarts the first page', async () => {
  const harness = createHarness();
  harness.view.show();
  await resolveInitial(harness, { state: 'Ready', page: objectPage(0, 2, 'cursor-1') });
  assert.equal(harness.elements.objects.children.length, 2);

  for (let page = 2; page <= 6; page += 1) {
    await harness.elements.loadMore.dispatch('click');
    harness.client.next('metadata-objects').resolve(objectPage(page * 2, 2, `cursor-${page}`, 1));
    await flushMicrotasks();
  }
  const notice = findElement(harness.elements.objects,
    (candidate) => candidate.classList.contains('metadata-objects-collapsed'));
  assert.ok(notice, 'expected the collapsed-objects notice');
  assert.equal(notice.textContent, 'Back to start · earlier 2 objects collapsed');
  assert.equal(harness.elements.objects.children[0], notice);
  assert.equal(harness.elements.objects.children.length, 11); // notice + 5 window pages × 2 rows
  assert.equal(harness.callbacks.statuses.at(-1), 'Showing 3–12 of 400 Metadata objects');

  await notice.dispatch('click');
  const restart = harness.client.next('metadata-objects');
  assert.equal(restart.path.includes('kind=All'), true);
  assert.equal(restart.path.includes('cursor='), false);
  restart.resolve(objectPage(0, 2, 'cursor-1'));
  await flushMicrotasks();
  assert.equal(findElement(harness.elements.objects,
    (candidate) => candidate.classList.contains('metadata-objects-collapsed')), null);
  assert.equal(harness.elements.objects.children.length, 2);
  assert.equal(harness.callbacks.statuses.at(-1), 'Showing 2 of 400 Metadata objects');
});

test('an object page that resolves first rejects a later status from another document', async () => {
  const harness = createHarness();
  harness.view.show();
  const status = harness.client.next('metadata-status');
  harness.client.next('metadata-objects').resolve({ ...objectPage(), documentId: 'objects-document' });
  await flushMicrotasks();
  status.resolve(statusWire('Ready', 1, { documentId: 'status-document' }));
  await flushMicrotasks();

  assert.equal(harness.elements.objects.children.length, 2);
  assert.equal(harness.callbacks.errors.length, 1);
  assert.match(harness.callbacks.errors[0].message, /documentId/);
});

test('a status that resolves first rejects a later object page from another document', async () => {
  const harness = createHarness();
  harness.view.show();
  harness.client.next('metadata-status').resolve(statusWire('Ready', 1, {
    documentId: 'status-document',
  }));
  const objects = harness.client.next('metadata-objects');
  await flushMicrotasks();
  objects.resolve({ ...objectPage(), documentId: 'objects-document' });
  await flushMicrotasks();

  assert.equal(harness.elements.objects.children.length, 0);
  assert.equal(harness.callbacks.errors.length, 1);
  assert.match(harness.callbacks.errors[0].message, /documentId/);
});

test('hide suppresses late status, object, detail, and reference successes', async () => {
  const harness = createHarness();
  harness.view.show();
  await resolveInitial(harness, { page: objectPage(0, 2, 'objects-next') });
  nextLiveTimer(harness);
  const status = harness.client.next('metadata-status');
  await harness.elements.loadMore.dispatch('click');
  const objects = harness.client.next('metadata-objects');
  const selected = await selectObject(harness);
  const rowsAtHide = harness.elements.objects.children.length;
  const callbacksAtHide = { status: harness.callbacks.statuses.length, clear: harness.callbacks.clearErrors };
  harness.view.hide();
  status.resolve(statusWire('Ready'));
  objects.resolve(objectPage(100, 2));
  selected.detail.resolve(objectDetail());
  selected.references.resolve(referencePage());
  await flushMicrotasks();
  assert.equal(harness.elements.objects.children.length, rowsAtHide);
  assert.equal(harness.elements.detail.children.length, 0);
  assert.equal(harness.elements.references.children.length, 0);
  assert.equal(harness.callbacks.statuses.length, callbacksAtHide.status);
  assert.equal(harness.callbacks.clearErrors, callbacksAtHide.clear);
});

test('show restarts selected detail and references that had no last-good response before hide', async () => {
  const harness = createHarness();
  harness.view.show();
  await resolveInitial(harness, { state: 'Ready' });
  const selected = await selectObject(harness);
  harness.view.hide();
  selected.detail.resolve(objectDetail());
  selected.references.resolve(referencePage());
  await flushMicrotasks();

  harness.view.show();
  assert.equal(harness.client.count('metadata-object'), 2);
  assert.equal(harness.client.count('metadata-references'), 2);
});

test('a new filter suppresses late errors on every prior request generation', async () => {
  const harness = createHarness();
  harness.view.show();
  await resolveInitial(harness, { page: objectPage(0, 2, 'objects-next') });
  nextLiveTimer(harness);
  const status = harness.client.next('metadata-status');
  await harness.elements.loadMore.dispatch('click');
  const objects = harness.client.next('metadata-objects');
  const selected = await selectObject(harness);

  harness.elements.kind.value = 'Method';
  await harness.elements.kind.dispatch('change');
  status.reject(new Error('late status failure'));
  objects.reject(new Error('late objects failure'));
  selected.detail.reject(new Error('late detail failure'));
  selected.references.reject(new Error('late references failure'));
  await flushMicrotasks();
  assert.deepEqual(harness.callbacks.errors, []);
  assert.equal(harness.elements.objects.children.length, 0);
  assert.match(harness.client.next('metadata-objects').path, /kind=Method&limit=100$/);
  assert.equal(harness.client.count('metadata-status'), 3);
});

test('kind and UTF-8 search filters reset pagination and request encoded page one paths', async () => {
  const harness = createHarness();
  harness.view.show();
  await resolveInitial(harness, { state: 'Ready', page: objectPage(0, 2, 'objects-next') });
  harness.elements.kind.value = 'Literal Array';
  await harness.elements.kind.dispatch('change');
  const kindRequest = harness.client.next('metadata-objects');
  assert.equal(kindRequest.path,
    '/api/v1/metadata/objects?kind=Literal%20Array&limit=100');
  kindRequest.resolve(objectPage(20));
  harness.client.next('metadata-status').resolve(statusWire('Ready'));
  await flushMicrotasks();

  harness.elements.search.value = 'a b界';
  await harness.elements.searchForm.dispatch('submit');
  const searchRequest = harness.client.next('metadata-objects');
  assert.equal(searchRequest.path,
    '/api/v1/metadata/objects?kind=Literal%20Array&limit=100&query=a%20b%E7%95%8C');
  assert.equal(searchRequest.path.includes('+'), false);
  assert.equal(harness.elements.objects.children.length, 0);
});

test('Load More retains and renders at most five 100-row object pages', async () => {
  const harness = createHarness();
  harness.view.show();
  await resolveInitial(harness, { state: 'Ready', page: objectPage(0, 100, 'cursor-one') });
  assert.equal(harness.elements.objects.children.length, 100);

  for (let page = 2; page <= 5; page += 1) {
    await harness.elements.loadMore.dispatch('click');
    harness.client.next('metadata-objects').resolve(objectPage((page - 1) * 100, 100, `cursor-${page}`));
    await flushMicrotasks();
  }
  assert.equal(harness.elements.objects.children.length, 500);

  await harness.elements.loadMore.dispatch('click');
  harness.client.next('metadata-objects').resolve(objectPage(500, 100, 'cursor-six'));
  await flushMicrotasks();
  assert.equal(harness.elements.objects.children.length, 501); // collapse notice + 500 window rows
  assert.equal(findElement(harness.elements.objects,
    (candidate) => candidate.dataset.key === 'String@0x300'), null);
  assert.ok(findElement(harness.elements.objects,
    (candidate) => candidate.dataset.key === metadataKey('String', 0x300n + 500n)));
  assert.equal(harness.callbacks.statuses.at(-1), 'Showing 101–600 of 400 Metadata objects');
});

test('only one object expands and stale detail/reference responses cannot replace it', async () => {
  const harness = createHarness();
  harness.view.show();
  await resolveInitial(harness, { state: 'Ready' });
  const first = await selectObject(harness, 0);
  const second = await selectObject(harness, 1);
  first.detail.resolve(objectDetail(0));
  first.references.resolve(referencePage(0));
  second.detail.resolve(objectDetail(1));
  second.references.resolve(referencePage(1));
  await flushMicrotasks();

  assert.match(harness.elements.detail.textContent, /full value 1/);
  assert.equal(harness.elements.detail.textContent.includes('full value 0'), false);
  assert.ok(findElement(harness.elements.objects,
    (candidate) => candidate.dataset.key === 'String@0x301' && candidate.classList.contains('expanded')));
  assert.equal(findElement(harness.elements.objects,
    (candidate) => candidate.dataset.key === 'String@0x300' && candidate.classList.contains('expanded')), null);
  assert.deepEqual(harness.callbacks.errors, []);
});

test('truncated association lists render a +N more hint', async () => {
  const harness = createHarness();
  harness.view.show();
  await resolveInitial(harness, { state: 'Ready' });
  const selected = await selectObject(harness, 0);
  selected.detail.resolve({
    ...objectDetail(0),
    entityIds: ['900'],
    entityIdsTruncated: true,
    entityIdsTotal: 115,
  });
  selected.references.resolve({
    ...referencePage(0),
    items: [
      referenceWire(0),
      { ...referenceWire(1), sourceEntityIds: ['901'], sourceEntityIdsTruncated: true, sourceEntityIdsTotal: 17 },
    ],
  });
  await flushMicrotasks();

  assert.ok(findElement(harness.elements.detail,
    (node) => node.classList.contains('metadata-ids-more') && node.textContent === '+114 more'));
  assert.ok(findElement(harness.elements.references,
    (node) => node.classList.contains('metadata-ids-more') && node.textContent === '+16 more'));
});

test('object cards are a default-collapsed inline accordion and the open card collapses on a second click', async () => {
  const harness = createHarness();
  harness.view.show();
  await resolveInitial(harness, { state: 'Ready' });
  assert.equal(findElement(harness.elements.objects,
    (candidate) => candidate === harness.elements.selection), null);

  const firstButton = findElement(harness.elements.objects,
    (candidate) => candidate.dataset.key === 'String@0x300' && candidate.dataset.action === 'expand');
  await firstButton.dispatch('click');
  const firstRow = findElement(harness.elements.objects,
    (candidate) => candidate.dataset.key === 'String@0x300' && candidate.classList.contains('expanded'));
  assert.ok(firstRow);
  assert.equal(findElement(firstRow, (candidate) => candidate === harness.elements.selection),
    harness.elements.selection);
  assert.equal(harness.elements.selection.classList.contains('hidden'), false);

  const pendingDetail = harness.client.next('metadata-object');
  const pendingReferences = harness.client.next('metadata-references');
  await firstButton.dispatch('click');
  assert.equal(findElement(harness.elements.objects,
    (candidate) => candidate.classList.contains('expanded')), null);
  assert.equal(findElement(harness.elements.objects,
    (candidate) => candidate === harness.elements.selection), null);
  assert.equal(harness.elements.selection.classList.contains('hidden'), true);
  pendingDetail.resolve(objectDetail());
  pendingReferences.resolve(referencePage());
  await flushMicrotasks();
  assert.equal(harness.elements.detail.children.length, 0);
  assert.equal(harness.elements.references.children.length, 0);
});

test('reference NEXT replaces 100 rows and BACK history stays bounded at 32 cursors', async () => {
  const harness = createHarness();
  harness.view.show();
  await resolveInitial(harness, { state: 'Ready' });
  const selected = await selectObject(harness);
  selected.detail.resolve(objectDetail());
  selected.references.resolve(referencePage(0, 0, 100, 'ref-1'));
  await flushMicrotasks();
  assert.equal(harness.elements.references.children.length, 100);

  for (let page = 1; page <= 34; ++page) {
    await harness.elements.referencesNext.dispatch('click');
    const request = harness.client.next('metadata-references');
    assert.match(request.path, new RegExp(`cursor=ref-${page}$`));
    request.resolve(referencePage(0, page * 100, 100, `ref-${page + 1}`));
    await flushMicrotasks();
    assert.equal(harness.elements.references.children.length, 100);
  }

  let backRequests = 0;
  while (!harness.elements.referencesBack.disabled && backRequests < 40) {
    await harness.elements.referencesBack.dispatch('click');
    const request = harness.client.next('metadata-references');
    request.resolve(referencePage(0, 0, 1, 'next-again'));
    ++backRequests;
    await flushMicrotasks();
  }
  assert.equal(backRequests, 32);
  assert.equal(harness.elements.referencesBack.disabled, true);
});

test('a revision-stale failure reloads page one and restores a still-present selected key', async () => {
  const harness = createHarness();
  harness.view.show();
  await resolveInitial(harness, { state: 'Ready', page: objectPage(0, 2, 'objects-next') });
  const selected = await selectObject(harness);
  selected.detail.resolve(objectDetail());
  selected.references.resolve(referencePage());
  await flushMicrotasks();

  await harness.elements.loadMore.dispatch('click');
  harness.client.next('metadata-objects').reject(new Error('Metadata cursor is stale; reload the first page'));
  await flushMicrotasks();
  const reload = harness.client.next('metadata-objects');
  assert.equal(reload.path, '/api/v1/metadata/objects?kind=All&limit=100');
  reload.resolve(objectPage(0, 2, null, 2));
  await flushMicrotasks();
  assert.ok(findElement(harness.elements.objects,
    (candidate) => candidate.dataset.key === 'String@0x300' && candidate.classList.contains('expanded')));
  assert.equal(harness.client.count('metadata-object'), 2);
  assert.equal(harness.client.count('metadata-references'), 2);
});

test('a revision reload retains and revalidates a selected key outside page one', async () => {
  const harness = createHarness();
  harness.view.show();
  await resolveInitial(harness, { state: 'Ready', page: objectPage(0, 2, 'objects-next') });
  const selected = await selectObject(harness);
  selected.detail.resolve(objectDetail());
  selected.references.resolve(referencePage());
  await flushMicrotasks();

  await harness.elements.loadMore.dispatch('click');
  harness.client.next('metadata-objects').reject(new Error('Metadata cursor is stale; reload the first page'));
  await flushMicrotasks();
  harness.client.next('metadata-objects').resolve(objectPage(20, 2, 'new-next', 2));
  await flushMicrotasks();
  assert.equal(harness.client.count('metadata-object'), 2);
  assert.equal(harness.client.count('metadata-references'), 2);
  const refreshedDetail = harness.client.next('metadata-object');
  const refreshedReferences = harness.client.next('metadata-references');
  refreshedDetail.resolve(objectDetail());
  refreshedReferences.resolve(referencePage(0, 0, 2, null, 2));
  await flushMicrotasks();
  assert.ok(harness.elements.detail.children.length > 0);
  assert.equal(harness.elements.references.children.length, 2);
  assert.ok(findElement(harness.elements.objects,
    (candidate) => candidate.dataset.key === 'String@0x300' && candidate.classList.contains('expanded')));
  harness.view.hide();
  harness.view.show();
  assert.equal(harness.client.count('metadata-object'), 2);
  assert.equal(harness.client.count('metadata-references'), 2);
});

test('evicting the selected object page clears detail, references, cursors, and retained selection', async () => {
  const harness = createHarness();
  harness.view.show();
  await resolveInitial(harness, { state: 'Ready', page: objectPage(0, 100, 'cursor-one') });
  const selected = await selectObject(harness);
  selected.detail.resolve(objectDetail());
  selected.references.resolve(referencePage(0, 0, 2, 'reference-next'));
  await flushMicrotasks();

  for (let page = 2; page <= 6; page += 1) {
    await harness.elements.loadMore.dispatch('click');
    const nextCursor = page === 6 ? null : `cursor-${page}`;
    harness.client.next('metadata-objects').resolve(objectPage((page - 1) * 100, 100, nextCursor));
    await flushMicrotasks();
  }

  assert.equal(harness.elements.detail.children.length, 0);
  assert.equal(harness.elements.references.children.length, 0);
  assert.equal(harness.elements.referencesBack.disabled, true);
  assert.equal(harness.elements.referencesNext.disabled, true);
  harness.view.hide();
  harness.view.show();
  assert.equal(harness.client.count('metadata-object'), 1);
  assert.equal(harness.client.count('metadata-references'), 1);
});

test('a selected-object not-found response clears selection and suppresses its pending references', async () => {
  const harness = createHarness();
  harness.view.show();
  await resolveInitial(harness, { state: 'Ready' });
  const selected = await selectObject(harness);
  selected.detail.reject(new Error('Metadata object does not exist'));
  await flushMicrotasks();
  selected.references.resolve(referencePage());
  await flushMicrotasks();

  assert.equal(harness.callbacks.errors.length, 1);
  assert.equal(harness.elements.detail.children.length, 0);
  assert.equal(harness.elements.references.children.length, 0);
  assert.equal(findElement(harness.elements.objects,
    (candidate) => candidate.classList.contains('expanded')), null);
  harness.view.hide();
  harness.view.show();
  assert.equal(harness.client.count('metadata-object'), 1);
  assert.equal(harness.client.count('metadata-references'), 1);
});

test('detail rejects a different documentId even while the status request is pending', async () => {
  const harness = createHarness();
  harness.view.show();
  harness.client.next('metadata-objects').resolve({ ...objectPage(), documentId: 'objects-document' });
  await flushMicrotasks();
  const selected = await selectObject(harness);
  selected.detail.resolve({ ...objectDetail(), documentId: 'detail-document' });
  selected.references.resolve({ ...referencePage(), documentId: 'objects-document' });
  await flushMicrotasks();

  assert.equal(harness.elements.detail.children.length, 0);
  assert.equal(harness.elements.references.children.length, 2);
  assert.equal(harness.callbacks.errors.length, 1);
  assert.match(harness.callbacks.errors[0].message, /documentId/);
});

test('references reject a different documentId even while the status request is pending', async () => {
  const harness = createHarness();
  harness.view.show();
  harness.client.next('metadata-objects').resolve({ ...objectPage(), documentId: 'objects-document' });
  await flushMicrotasks();
  const selected = await selectObject(harness);
  selected.detail.resolve({ ...objectDetail(), documentId: 'objects-document' });
  selected.references.resolve({ ...referencePage(), documentId: 'references-document' });
  await flushMicrotasks();

  assert.ok(harness.elements.detail.children.length > 0);
  assert.equal(harness.elements.references.children.length, 0);
  assert.equal(harness.callbacks.errors.length, 1);
  assert.match(harness.callbacks.errors[0].message, /documentId/);
});

test('Structure, Disassembly, and Hex buttons emit validated frozen navigation payloads', async () => {
  const harness = createHarness();
  harness.view.show();
  await resolveInitial(harness, { state: 'Ready' });
  const selected = await selectObject(harness);
  selected.detail.resolve(objectDetail());
  selected.references.resolve(referencePage());
  await flushMicrotasks();

  const structure = findElement(harness.elements.detail,
    (candidate) => candidate.dataset.navigation === 'structure');
  const disassembly = findElement(harness.elements.references,
    (candidate) => candidate.dataset.navigation === 'disassembly');
  const hex = findElement(harness.elements.references,
    (candidate) => candidate.dataset.navigation === 'hex');
  await structure.dispatch('click');
  await disassembly.dispatch('click');
  await hex.dispatch('click');
  assert.deepEqual(harness.callbacks.structures, [{ entityId: '65' }]);
  assert.deepEqual(harness.callbacks.disassemblies, [{
    entityId: '900', methodOffset: 0x100n, pc: 0, sourceRange: { offset: 0x700n, size: 2 },
  }]);
  assert.deepEqual(harness.callbacks.hex, [{
    kind: 'String', offset: 0x300n, range: { offset: 0x700n, size: 2 },
    entityIds: ['900'], entityIdsTruncated: false, source: 'metadata',
  }]);
  assert.equal(Object.isFrozen(harness.callbacks.disassemblies[0].sourceRange), true);
  assert.equal(Object.isFrozen(harness.callbacks.disassemblies[0]), true);
  assert.equal(Object.isFrozen(harness.callbacks.hex[0].entityIds), true);
  assert.equal(Object.isFrozen(harness.callbacks.hex[0].range), true);
  assert.equal(Object.isFrozen(harness.callbacks.hex[0]), true);
});

test('multiple detail and reference associations expose unique EntityId labels and exact payloads', async () => {
  const harness = createHarness();
  const method = {
    ...objectWire(0, 'Method'),
    entityIds: ['65', '68'],
    preview: 'create',
  };
  harness.view.show();
  await resolveInitial(harness, {
    state: 'Ready',
    page: { ...objectPage(0, 0), total: '1', items: [method] },
  });
  const button = findElement(harness.elements.objects,
    (candidate) => candidate.dataset.action === 'expand' && candidate.dataset.key === metadataKey('Method', 0x300n));
  assert.ok(button);
  await button.dispatch('click');
  harness.client.next('metadata-object').resolve({
    apiVersion: 1,
    documentId: 'document-one',
    ...method,
    value: null,
    valueTruncated: false,
  });
  harness.client.next('metadata-references').resolve({
    ...referencePage(),
    key: method.key,
    items: [{ ...referenceWire(), sourceEntityIds: ['900', '901'] }],
  });
  await flushMicrotasks();

  const detailStructure = findAll(harness.elements.detail,
    (candidate) => candidate.dataset.navigation === 'structure');
  const detailDisassembly = findAll(harness.elements.detail,
    (candidate) => candidate.dataset.navigation === 'disassembly');
  const referenceStructure = findAll(harness.elements.references,
    (candidate) => candidate.dataset.navigation === 'structure');
  const referenceDisassembly = findAll(harness.elements.references,
    (candidate) => candidate.dataset.navigation === 'disassembly');
  assert.deepEqual(detailStructure.map((candidate) => candidate.textContent), ['Structure #65', 'Structure #68']);
  assert.deepEqual(detailDisassembly.map((candidate) => candidate.textContent),
    ['Disassembly #65', 'Disassembly #68']);
  assert.deepEqual(referenceStructure.map((candidate) => candidate.textContent),
    ['Structure #900', 'Structure #901']);
  assert.deepEqual(referenceDisassembly.map((candidate) => candidate.textContent),
    ['Disassembly #900', 'Disassembly #901']);
  assert.deepEqual(detailStructure.map((candidate) => candidate.dataset.entityId), ['65', '68']);
  assert.deepEqual(referenceDisassembly.map((candidate) => candidate.dataset.entityId), ['900', '901']);

  await detailStructure[1].dispatch('click');
  await detailDisassembly[1].dispatch('click');
  await referenceStructure[1].dispatch('click');
  await referenceDisassembly[1].dispatch('click');
  assert.deepEqual(harness.callbacks.structures, [{ entityId: '68' }, { entityId: '901' }]);
  assert.deepEqual(harness.callbacks.disassemblies, [
    { entityId: '68', methodOffset: 0x300n, pc: null, sourceRange: null },
    { entityId: '901', methodOffset: 0x100n, pc: 0, sourceRange: { offset: 0x700n, size: 2 } },
  ]);
});

test('a Literal Array with unknown extent renders without offering invalid Hex navigation', async () => {
  const harness = createHarness();
  const literal = { ...objectWire(0, 'Literal Array'), range: null, preview: 'literal array' };
  harness.view.show();
  await resolveInitial(harness, {
    state: 'Ready',
    page: { ...objectPage(0, 0), total: '1', items: [literal] },
  });
  const key = metadataKey('Literal Array', 0x300n);
  const button = findElement(harness.elements.objects,
    (candidate) => candidate.dataset.action === 'expand' && candidate.dataset.key === key);
  assert.ok(button);
  await button.dispatch('click');
  harness.client.next('metadata-object').resolve({
    apiVersion: 1,
    documentId: 'document-one',
    ...literal,
    value: 'literal array',
    valueTruncated: false,
  });
  harness.client.next('metadata-references').resolve({
    ...referencePage(),
    key: literal.key,
    total: '0',
    items: [],
  });
  await flushMicrotasks();

  assert.match(harness.elements.detail.textContent, /Literal Array/);
  assert.equal(findElement(harness.elements.detail,
    (candidate) => candidate.dataset.navigation === 'hex'), null);
  assert.equal(harness.callbacks.errors.length, 0);
});

test('references without source entities expose disabled Structure and Disassembly navigation', async () => {
  const harness = createHarness();
  harness.view.show();
  await resolveInitial(harness, { state: 'Ready' });
  const selected = await selectObject(harness);
  selected.detail.resolve(objectDetail());
  selected.references.resolve({
    ...referencePage(),
    items: [{ ...referenceWire(), sourceEntityIds: [], sourceEntityIdsTruncated: true }],
  });
  await flushMicrotasks();

  const structure = findElement(harness.elements.references,
    (candidate) => candidate.dataset.navigation === 'structure');
  const disassembly = findElement(harness.elements.references,
    (candidate) => candidate.dataset.navigation === 'disassembly');
  const hex = findElement(harness.elements.references,
    (candidate) => candidate.dataset.navigation === 'hex');
  assert.equal(structure.disabled, true);
  assert.equal(disassembly.disabled, true);
  assert.equal(hex.disabled, false);
});

test('Structure, Disassembly, CFG, Metadata, and Hex share one workbench shell', async () => {
  const html = await readFile(new URL('../index.html', import.meta.url), 'utf8');
  assert.doesNotMatch(html, /class="view-tabs"|id="tab-/);
  for (const panel of [
    'structure-panel', 'disassembly-panel', 'cfg-panel', 'metadata-panel', 'hex-panel',
  ]) {
    assert.match(html, new RegExp(`id="${panel}"[^>]*data-workbench-panel`));
    assert.match(html, new RegExp(`data-panel-maximize="${panel}"`));
  }
  assert.equal((html.match(/data-workbench-separator=/g) ?? []).length, 4);
});

test('exact disassembly selection retains method, pc, source range, and instruction across tab round trips', async () => {
  const { DisassemblySelectionState } = await import('../src/main.js');
  const state = new DisassemblySelectionState();
  const instruction = Object.freeze({ pc: 7, fileRange: { offset: '0x710', size: 2 } });
  state.remember({
    entityId: '900', methodOffset: 0x100n, pc: 7,
    sourceRange: { offset: 0x710n, size: 2n }, source: 'metadata', instruction,
  });

  const afterMetadata = state.forEntity('900');
  const afterHex = state.forEntity('900');
  assert.deepEqual(afterMetadata, afterHex);
  assert.equal(afterHex.methodOffset, 0x100n);
  assert.equal(afterHex.pc, 7);
  assert.equal(afterHex.source, 'metadata');
  assert.equal(afterHex.instruction, instruction);
  assert.equal(state.forEntity('901'), null);
});

test('a Structure association is auto-selectable only when exactly one complete EntityId is returned', async () => {
  const { singleCompleteMetadataEntityId } = await import('../src/main.js');
  assert.equal(singleCompleteMetadataEntityId({ entityIds: ['900'], entityIdsTruncated: false }), '900');
  assert.equal(singleCompleteMetadataEntityId({ entityIds: ['900'], entityIdsTruncated: true }), null);
  assert.equal(singleCompleteMetadataEntityId({ entityIds: ['900', '901'], entityIdsTruncated: false }), null);
  assert.equal(singleCompleteMetadataEntityId({ entityIds: [], entityIdsTruncated: false }), null);
});

test('resolved typed IDs alone produce canonical Metadata targets', async () => {
  const { metadataTargetForInstructionId } = await import('../src/main.js');
  assert.deepEqual(metadataTargetForInstructionId({ kind: 'String', resolvedOffset: '0x300' }), {
    kind: 'String', offset: 0x300n,
  });
  assert.deepEqual(metadataTargetForInstructionId({ kind: 'Literal Array', resolvedOffset: '0x400' }), {
    kind: 'Literal Array', offset: 0x400n,
  });
  assert.equal(metadataTargetForInstructionId({ kind: 'Unknown', resolvedOffset: '0x300' }), null);
  assert.equal(metadataTargetForInstructionId({ kind: 'Method', resolvedOffset: null }), null);
  assert.equal(metadataTargetForInstructionId({ kind: 'Method', resolvedOffset: '300' }), null);
});

test('Metadata disassembly paths retain exact entity and method offsets', async () => {
  const { metadataDisassemblyPath } = await import('../src/main.js');
  assert.equal(metadataDisassemblyPath('900', 0x100n),
    '/api/v1/disassembly?id=900&methodOffset=0x100');
  assert.throws(() => metadataDisassemblyPath('09', 0x100n), /entity/i);
  assert.throws(() => metadataDisassemblyPath('900', -1n), /method/i);
});

test('Partial shows a notice without clearing data and Failed preserves the last-good list', async () => {
  for (const state of ['Partial', 'Failed']) {
    const harness = createHarness();
    harness.view.show();
    await resolveInitial(harness, { page: objectPage(0, 2) });
    nextLiveTimer(harness);
    harness.client.next('metadata-status').resolve(statusWire(state, 2, {
      diagnostics: [`${state} diagnostic`],
    }));
    await flushMicrotasks();
    assert.equal(harness.elements.objects.children.length, 2, state);
    assert.match(harness.elements.notice.textContent, new RegExp(state, 'i'), state);
    assert.match(harness.elements.notice.textContent, new RegExp(`${state} diagnostic`, 'i'), state);
  }
});

test('revealObject validates its key before requesting and can select a retained object', async () => {
  const harness = createHarness();
  harness.view.show();
  await resolveInitial(harness, { state: 'Ready' });
  assert.throws(() => harness.view.revealObject({ kind: 'Field', offset: 0x300n }), /kind/i);
  harness.view.revealObject({ kind: 'String', offset: 0x301n });
  assert.equal(harness.client.count('metadata-object'), 1);
  assert.equal(harness.client.count('metadata-references'), 1);
});

test('revealObject keeps an exact external target while an unrelated first page arrives', async () => {
  const harness = createHarness();
  harness.view.show();
  const externalIndex = 0x600;
  harness.view.revealObject({ kind: 'String', offset: 0x900n });
  const detail = harness.client.next('metadata-object');
  const references = harness.client.next('metadata-references');

  harness.client.next('metadata-start').resolve(statusWire('Ready'));
  harness.client.next('metadata-status').resolve(statusWire('Ready'));
  harness.client.next('metadata-objects').resolve(objectPage());
  detail.resolve(objectDetail(externalIndex));
  references.resolve(referencePage(externalIndex));
  await flushMicrotasks();

  assert.match(harness.elements.detail.textContent, new RegExp(`full value ${externalIndex}`));
  assert.equal(harness.elements.references.children.length, 2);
  const detachedRow = findElement(harness.elements.objects,
    (candidate) => candidate.dataset.key === 'String@0x900' && candidate.classList.contains('expanded'));
  assert.ok(detachedRow);
  assert.equal(findElement(detachedRow, (candidate) => candidate === harness.elements.selection),
    harness.elements.selection);
});

test('a detached external row shares the 500-row DOM budget and collapse restores all retained rows', async () => {
  const harness = createHarness();
  harness.view.show();
  await resolveInitial(harness, { state: 'Ready', page: objectPage(0, 100, 'cursor-one') });
  for (let page = 2; page <= 5; page += 1) {
    await harness.elements.loadMore.dispatch('click');
    harness.client.next('metadata-objects').resolve(objectPage((page - 1) * 100, 100, `cursor-${page}`));
    await flushMicrotasks();
  }

  harness.view.revealObject({ kind: 'String', offset: 0x900n });
  const renderedRows = () => harness.elements.objects.children.filter(
    (candidate) => candidate.className.split(/\s+/).includes('metadata-object-row'));
  assert.equal(renderedRows().length, 500);
  assert.equal(renderedRows().filter((row) => row.className.split(/\s+/).includes('detached')).length, 1);
  assert.equal(renderedRows().filter((row) => !row.className.split(/\s+/).includes('detached')).length, 499);

  const detachedButton = findElement(harness.elements.objects,
    (candidate) => candidate.dataset.action === 'expand' && candidate.dataset.key === 'String@0x900');
  assert.ok(detachedButton);
  await detachedButton.dispatch('click');
  assert.equal(renderedRows().length, 500);
  assert.equal(renderedRows().filter((row) => row.className.split(/\s+/).includes('detached')).length, 0);
  assert.ok(findElement(harness.elements.objects,
    (candidate) => candidate.dataset.key === metadataKey('String', 0x300n + 499n)));
});

test('Hex navigation preserves truncated association evidence', async () => {
  const harness = createHarness();
  harness.view.show();
  await resolveInitial(harness, { state: 'Ready' });
  const selected = await selectObject(harness);
  selected.detail.resolve(objectDetail());
  selected.references.resolve({
    ...referencePage(),
    items: [{ ...referenceWire(), sourceEntityIds: ['900'], sourceEntityIdsTruncated: true }],
  });
  await flushMicrotasks();
  const hex = findElement(harness.elements.references,
    (candidate) => candidate.dataset.navigation === 'hex');
  await hex.dispatch('click');
  assert.deepEqual(harness.callbacks.hex, [{
    kind: 'String', offset: 0x300n, range: { offset: 0x700n, size: 2 },
    entityIds: ['900'], entityIdsTruncated: true, source: 'metadata',
  }]);
});
