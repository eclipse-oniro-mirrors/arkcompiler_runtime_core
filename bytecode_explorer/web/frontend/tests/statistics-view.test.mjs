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

import { ApiClient } from '../src/api-client.js';
import {
  STATISTICS_CHANNEL, StatisticsView, statisticsPath, statisticsPresentation, validateStatisticsResponse,
} from '../src/statistics-view.js';

class FakeElement {
  constructor(document) {
    this.ownerDocument = document;
    this.children = [];
    this.listeners = new Map();
    this.className = '';
    this.disabled = false;
    this.type = '';
    this._text = '';
    const classes = new Set();
    this.classList = {
      add: (...names) => names.forEach((name) => classes.add(name)),
      remove: (...names) => names.forEach((name) => classes.delete(name)),
      contains: (name) => classes.has(name),
    };
  }
  set textContent(value) { this._text = String(value); this.children = []; }
  get textContent() { return this._text + this.children.map((child) => child.textContent).join(''); }
  append(...children) { this.children.push(...children); }
  replaceChildren(...children) { this.children = [...children]; this._text = ''; }
  addEventListener(type, listener) { this.listeners.set(type, listener); }
  async click() { await this.listeners.get('click')?.({ preventDefault() {} }); }
}

class FakeDocument { createElement() { return new FakeElement(this); } }

function deferred() {
  let resolve;
  let reject;
  const promise = new Promise((yes, no) => { resolve = yes; reject = no; });
  return { promise, resolve, reject };
}

class Client {
  requests = [];
  cancelled = [];
  get(path, channel, options) { return this.request('GET', path, undefined, channel, options); }
  post(path, body, channel, options) { return this.request('POST', path, body, channel, options); }
  cancel(channel) { this.cancelled.push(channel); }
  request(method, path, body, channel, options) {
    const pending = deferred();
    this.requests.push({ method, path, body, channel, options, ...pending });
    return pending.promise;
  }
}

const decimalNames = [
  'methodCount', 'executableMethods', 'unassignedMethods', 'unassignedExecutables', 'scannedMethods',
  'decodedMethods', 'failedMethods', 'limitedMethods', 'knownCodeBytes', 'knownInstructionCount',
];

function wire(overrides = {}) {
  return {
    apiVersion: 1, documentId: 'doc', entityId: '42', kind: 'Module', phase: 'NotStarted',
    methodCount: '9007199254740993', executableMethods: '7', unassignedMethods: '0',
    unassignedExecutables: '0', scannedMethods: '0', decodedMethods: '0', failedMethods: '0',
    limitedMethods: '0', knownCodeBytes: '0', knownInstructionCount: '0', fileSize: null,
    codeBytes: null, instructionCount: null, activeScopeId: null, diagnostic: '', ...overrides,
  };
}

function harness() {
  const document = new FakeDocument();
  const client = new Client();
  const metrics = new FakeElement(document);
  const inspector = new FakeElement(document);
  const timers = [];
  const clearedTimers = [];
  const view = new StatisticsView({
    client, elements: { metrics, inspector },
    setTimer(callback, delay) { timers.push({ callback, delay }); return timers.length; },
    clearTimer(id) { clearedTimers.push(id); },
  });
  return { clearedTimers, client, metrics, inspector, timers, view };
}

test('statistics response preserves canonical decimal strings beyond Number precision', () => {
  const parsed = validateStatisticsResponse(wire(), { entityId: '42' });
  assert.equal(parsed.methodCount, '9007199254740993');
  for (const name of decimalNames) {
    assert.equal(typeof parsed[name], 'string');
  }
});

test('statistics response validates envelope identity, enums, decimals, and phase null relationships', () => {
  assert.throws(() => validateStatisticsResponse(wire({ apiVersion: 2 }), { entityId: '42' }), /version/i);
  assert.throws(() => validateStatisticsResponse(wire({ documentId: '' }), { entityId: '42' }), /document/i);
  assert.throws(() => validateStatisticsResponse(wire({ entityId: '042' }), { entityId: '42' }), /entity/i);
  assert.throws(() => validateStatisticsResponse(wire({ kind: 'Field' }), { entityId: '42' }), /kind/i);
  assert.throws(() => validateStatisticsResponse(wire({ methodCount: '01' }), { entityId: '42' }), /methodCount/);
  assert.throws(() => validateStatisticsResponse(wire({ activeScopeId: '0' }), { entityId: '42' }), /activeScopeId/);
  assert.throws(() => validateStatisticsResponse(wire({ fileSize: '4' }), { entityId: '42' }), /fileSize/);
  assert.throws(() => validateStatisticsResponse(wire({ codeBytes: '0' }), { entityId: '42' }), /codeBytes/);
  assert.throws(() => validateStatisticsResponse(wire({ phase: 'Complete' }), { entityId: '42' }), /codeBytes/);
  assert.doesNotThrow(() => validateStatisticsResponse(wire({
    phase: 'Complete', codeBytes: '0', instructionCount: '0', scannedMethods: '7', decodedMethods: '7',
  }), { entityId: '42', documentId: 'doc' }));
});

test('presentation distinguishes unknown, known partial, exact zero, file bytes, and module byte labels', () => {
  const unknown = statisticsPresentation(validateStatisticsResponse(wire(), { entityId: '42' }));
  assert.match(unknown.totals, /unknown/i);
  const partial = statisticsPresentation(validateStatisticsResponse(wire({
    phase: 'Building', scannedMethods: '2', knownCodeBytes: '11', knownInstructionCount: '3', activeScopeId: '42',
  }), { entityId: '42' }));
  assert.match(partial.totals, /known.*11.*known.*3/i);
  const complete = statisticsPresentation(validateStatisticsResponse(wire({
    phase: 'Complete', codeBytes: '0', instructionCount: '0', scannedMethods: '7', decodedMethods: '7',
  }), { entityId: '42' }));
  assert.match(complete.totals, /module method code bytes: 0/i);
  assert.match(complete.totals, /module method instructions: 0/i);
  const file = statisticsPresentation(validateStatisticsResponse(wire({
    kind: 'File', fileSize: '123', unassignedMethods: '2', unassignedExecutables: '1',
  }), { entityId: '42' }));
  assert.match(file.details.join(' '), /physical file bytes.*123/i);
  assert.match(file.details.join(' '), /unassigned methods.*2/i);
});

test('presentation keeps decoded clean and surfaces failed and limited only when nonzero', () => {
  const clean = statisticsPresentation(validateStatisticsResponse(wire({
    phase: 'Complete', codeBytes: '8', instructionCount: '2', scannedMethods: '3', decodedMethods: '3',
  }), { entityId: '42' }));
  assert.equal(clean.details[3], 'Decoded: 3');
  assert.equal(clean.details.slice(4).join(' '), '');
  const failed = statisticsPresentation(validateStatisticsResponse(wire({
    phase: 'Partial', scannedMethods: '5', decodedMethods: '4', failedMethods: '1',
    knownCodeBytes: '11', knownInstructionCount: '3',
  }), { entityId: '42' }));
  assert.equal(failed.details[3], 'Decoded: 4');
  assert.match(failed.details.slice(4).join(' '), /failed: 1/i);
  assert.doesNotMatch(failed.details.slice(4).join(' '), /limited/i);
  const both = statisticsPresentation(validateStatisticsResponse(wire({
    phase: 'Partial', scannedMethods: '5', decodedMethods: '2', failedMethods: '1', limitedMethods: '2',
    knownCodeBytes: '11', knownInstructionCount: '3',
  }), { entityId: '42' }), 0);
  assert.equal(both.details[3], 'Decoded: 2');
  assert.equal(both.details[4], 'Failed: 1 · Limited: 2');
  assert.match(both.details.slice(4).join(' '), /diagnostics: 0/i);
});

test('presentation labels class, interface, and namespace scopes and keeps diagnostics', () => {
  const semantic = statisticsPresentation(validateStatisticsResponse(wire({
    kind: 'Class', phase: 'Complete', codeBytes: '629', instructionCount: '29', scannedMethods: '2',
    decodedMethods: '2',
  }), { entityId: '42' }));
  assert.match(semantic.totals, /class method code bytes: 629/i);
  assert.match(semantic.totals, /class method instructions: 29/i);
  assert.doesNotMatch(semantic.details.join(' '), /diagnostics/i);
  const withDiagnostics = statisticsPresentation(validateStatisticsResponse(wire({
    kind: 'Class', phase: 'Complete', codeBytes: '629', instructionCount: '29', scannedMethods: '2',
    decodedMethods: '2',
  }), { entityId: '42' }), 1);
  assert.match(withDiagnostics.details.join(' '), /diagnostics: 1/i);
  const iface = statisticsPresentation(validateStatisticsResponse(wire({ kind: 'Interface' }),
    { entityId: '42' }), 0);
  assert.match(iface.totals, /interface method/i);
  assert.match(iface.details.join(' '), /diagnostics: 0/i);
  const namespace = statisticsPresentation(validateStatisticsResponse(wire({ kind: 'Namespace' }),
    { entityId: '42' }), null);
  assert.match(namespace.totals, /namespace method/i);
  assert.doesNotMatch(namespace.details.join(' '), /diagnostics/i);
  const file = statisticsPresentation(validateStatisticsResponse(wire({
    kind: 'File', fileSize: '123', unassignedMethods: '2', unassignedExecutables: '1',
  }), { entityId: '42' }), 3);
  const joined = file.details.join(' ');
  assert.match(joined, /physical file bytes.*123/i);
  assert.match(joined, /diagnostics: 3/i);
});

test('class selections render statistics overlay with diagnostics count', async () => {
  const h = harness();
  const selecting = h.view.select({ id: '42', kind: 'Class', diagnostics: [{}, {}] });
  h.client.requests[0].resolve(wire({
    kind: 'Class', phase: 'Complete', codeBytes: '629', instructionCount: '29', scannedMethods: '2',
    decodedMethods: '2',
  }));
  await selecting;
  assert.match(h.inspector.textContent, /class method code bytes: 629/i);
  assert.match(h.inspector.textContent, /diagnostics: 2/i);
});

test('path accepts only canonical positive ids', () => {
  assert.equal(statisticsPath('42'), '/api/v1/statistics?id=42');
  assert.equal(statisticsPath('42', 'cancel'), '/api/v1/statistics/cancel?id=42');
  assert.throws(() => statisticsPath('042'), /entity/i);
  assert.throws(() => statisticsPath('42', 'delete'), /action/i);
});

test('selection gets status with timeout and automatically starts using exact empty body', async () => {
  const h = harness();
  const selecting = h.view.select({ id: '42', kind: 'Module' });
  assert.deepEqual(h.client.requests[0], {
    ...h.client.requests[0], method: 'GET', path: '/api/v1/statistics?id=42', channel: STATISTICS_CHANNEL,
    options: { timeoutMs: 30000 },
  });
  h.client.requests[0].resolve(wire());
  await selecting;
  assert.doesNotMatch(h.inspector.textContent, /Calculate|Refresh/);
  assert.equal(h.client.requests.length, 2);
  assert.equal(h.client.requests[1].method, 'POST');
  assert.deepEqual(h.client.requests[1].body, {});
  assert.deepEqual(h.client.requests[1].options, { timeoutMs: 30000 });
  h.client.requests[1].resolve(wire({ phase: 'Building', activeScopeId: '42' }));
  await Promise.resolve();
  assert.equal(h.timers.at(-1).delay, 250);
  assert.deepEqual(h.inspector.children.filter((child) => child.type === 'button').map((child) => child.textContent), ['Cancel']);
});

test('active selected scope offers Cancel and continues polling through cancelled drain', async () => {
  const h = harness();
  const selecting = h.view.select({ id: '42', kind: 'Module' });
  h.client.requests[0].resolve(wire({ phase: 'Building', activeScopeId: '42' }));
  await selecting;
  const cancel = h.inspector.children.find((child) => child.textContent === 'Cancel');
  const action = cancel.click();
  assert.equal(h.client.requests[1].path, '/api/v1/statistics/cancel?id=42');
  h.client.requests[1].resolve(wire({ phase: 'Cancelled', activeScopeId: '42' }));
  await action;
  assert.equal(h.timers.length >= 1, true);
  assert.equal(h.inspector.children.find((child) => child.textContent === 'Retry').disabled, true);
  h.timers.at(-1).callback();
  h.client.requests[2].resolve(wire({ phase: 'Cancelled' }));
  await Promise.resolve();
  assert.equal(h.inspector.children.find((child) => child.textContent === 'Retry').disabled, false);
  assert.equal(h.client.requests.length, 3, 'cancel must not restart automatically');
});

test('another active scope shows waiting and automatically starts selected scope when free', async () => {
  const h = harness();
  const selecting = h.view.select({ id: '42', kind: 'Module' });
  h.client.requests[0].resolve(wire({ activeScopeId: '99' }));
  await selecting;
  assert.match(h.inspector.textContent, /scope 99/i);
  assert.match(h.inspector.textContent, /waiting/i);
  assert.doesNotMatch(h.inspector.textContent, /Calculate|Refresh|Cancel|Retry/);
  assert.equal(h.client.requests.length, 1);
  assert.equal(h.timers.at(-1).delay, 250);
  h.timers.at(-1).callback();
  h.client.requests[1].resolve(wire());
  await Promise.resolve();
  assert.equal(h.client.requests.length, 3);
  assert.equal(h.client.requests[2].method, 'POST');
  h.client.requests[2].resolve(wire({ phase: 'Building', activeScopeId: '42' }));
  await Promise.resolve();
  assert.match(h.inspector.textContent, /Cancel/);
});

test('complete and partial cached results need neither actions nor polling even when another scope runs', async () => {
  for (const phase of ['Complete', 'Partial']) {
    const h = harness();
    const selecting = h.view.select({ id: '42', kind: 'Module' });
    h.client.requests[0].resolve(wire({ phase, activeScopeId: '99',
      codeBytes: phase === 'Complete' ? '12' : null, instructionCount: phase === 'Complete' ? '4' : null }));
    await selecting;
    assert.equal(h.client.requests.length, 1);
    assert.equal(h.timers.length, 0);
    assert.equal(h.inspector.children.some((child) => child.type === 'button'), false);
  }
});

test('failed and cancelled scopes require explicit Retry rather than restarting on selection', async () => {
  for (const phase of ['Failed', 'Cancelled']) {
    const h = harness();
    const selecting = h.view.select({ id: '42', kind: 'Module' });
    h.client.requests[0].resolve(wire({ phase }));
    await selecting;
    assert.equal(h.client.requests.length, 1);
    assert.doesNotMatch(h.inspector.textContent, /Calculate|Refresh/);
    const action = h.inspector.children.find((child) => child.textContent === 'Retry').click();
    assert.equal(h.client.requests[1].method, 'POST');
    h.client.requests[1].resolve(wire({ phase: 'Building', activeScopeId: '42' }));
    await action;
    assert.match(h.inspector.textContent, /Cancel/);
  }
});

test('switching while waiting never starts abandoned scopes or cancels shared backend work', async () => {
  const h = harness();
  const first = h.view.select({ id: '42', kind: 'Module' });
  h.client.requests[0].resolve(wire({ activeScopeId: '99' }));
  await first;
  const oldTimer = h.timers.at(-1);
  oldTimer.callback();
  const second = h.view.select({ id: '43', kind: 'Script' });
  h.client.requests[1].resolve(wire());
  await Promise.resolve();
  assert.equal(h.client.requests.length, 3, 'late idle status must not start abandoned scope');
  oldTimer.callback();
  assert.equal(h.client.requests.length, 3);
  h.client.requests[2].resolve(wire({ entityId: '43', kind: 'Script' }));
  await second;
  assert.equal(h.client.requests[3].path, '/api/v1/statistics?id=43');
  h.view.clear();
  h.inspector.textContent = 'new executable';
  h.client.requests[3].resolve(wire({ entityId: '43', kind: 'Script', phase: 'Building', activeScopeId: '43' }));
  await Promise.resolve();
  assert.equal(h.inspector.textContent, 'new executable');
  assert.equal(h.client.requests.some((request) => request.path.includes('/cancel')), false);
});

test('a competing start between status and POST recovers by waiting without manual action', async () => {
  const h = harness();
  const selecting = h.view.select({ id: '42', kind: 'Module' });
  h.client.requests[0].resolve(wire());
  await selecting;
  h.client.requests[1].reject(new Error('another scope started'));
  await Promise.resolve();
  assert.equal(h.client.requests.length, 3);
  assert.equal(h.client.requests[2].method, 'GET');
  h.client.requests[2].resolve(wire({ activeScopeId: '99' }));
  await Promise.resolve();
  assert.match(h.inspector.textContent, /Waiting/);
  assert.doesNotMatch(h.inspector.textContent, /Retry/);
  h.timers.at(-1).callback();
  h.client.requests[3].resolve(wire());
  await Promise.resolve();
  assert.equal(h.client.requests[4].method, 'POST');
  h.client.requests[4].resolve(wire({ phase: 'Complete', codeBytes: '0', instructionCount: '0' }));
  await Promise.resolve();
  assert.match(h.inspector.textContent, /Statistics: Complete/);
});

test('an actual start failure offers Retry without looping on an idle status', async () => {
  const h = harness();
  const selecting = h.view.select({ id: '42', kind: 'Module' });
  h.client.requests[0].resolve(wire());
  await selecting;
  h.client.requests[1].reject(new Error('cannot start worker'));
  await Promise.resolve();
  assert.equal(h.client.requests.length, 3);
  h.client.requests[2].resolve(wire());
  await Promise.resolve();
  assert.match(h.inspector.textContent, /cannot start worker/);
  assert.match(h.inspector.textContent, /Retry/);
  assert.equal(h.client.requests.length, 3);
  assert.equal(h.timers.length, 0);
});

test('clear cancels browser channel and late responses cannot overwrite newer content', async () => {
  const h = harness();
  const selecting = h.view.select({ id: '42', kind: 'Module' });
  h.view.clear();
  h.metrics.textContent = 'new function metrics';
  h.inspector.textContent = 'new instruction inspector';
  h.client.requests[0].resolve(wire({ phase: 'Complete', codeBytes: '8', instructionCount: '2' }));
  await selecting;
  assert.deepEqual(h.client.cancelled, [STATISTICS_CHANNEL]);
  assert.equal(h.metrics.textContent, 'new function metrics');
  assert.equal(h.inspector.textContent, 'new instruction inspector');
});

test('diagnostics line toggles into the structured messages from the node', async () => {
  const h = harness();
  const selecting = h.view.select({
    id: '42', kind: 'Class',
    diagnostics: [
      { severity: 'Info', message: 'Declaration reconstructed from dynamic scope names' },
      { severity: 'Warning', message: 'Physical range is outside the ABC file' },
    ],
  });
  h.client.requests[0].resolve(wire({ kind: 'Class', phase: 'Complete', codeBytes: '742', instructionCount: '278' }));
  await selecting;
  const wrap = h.inspector.children.find((child) => child.className === 'statistics-diagnostics');
  assert.notEqual(wrap, undefined);
  const toggle = wrap.children[0];
  const list = wrap.children[1];
  assert.equal(toggle.textContent, 'Diagnostics: 2 ▸');
  assert.equal(list.children.length, 0);
  await toggle.click();
  assert.equal(toggle.textContent, 'Diagnostics: 2 ▾');
  assert.equal(list.children.length, 2);
  assert.match(list.textContent, /info.*reconstructed from dynamic scope names/i);
  assert.match(list.textContent, /warning.*outside the abc file/i);
  assert.equal(list.children[0].children[0].className, 'diagnostic-severity severity-info');
  await toggle.click();
  assert.equal(toggle.textContent, 'Diagnostics: 2 ▸');
  assert.equal(list.children.length, 0);
});

test('diagnostics line stays a plain detail without items to expand', async () => {
  const h = harness();
  const selecting = h.view.select({ id: '42', kind: 'Module', diagnostics: [] });
  h.client.requests[0].resolve(wire({}));
  await selecting;
  const plain = h.inspector.children.find((child) => child.textContent === 'Diagnostics: 0');
  assert.notEqual(plain, undefined);
  assert.equal(plain.className, 'statistics-detail');
});

test('errors stay local and unsupported scopes perform no requests', async () => {
  const h = harness();
  assert.equal(await h.view.select({ id: '9', kind: 'Field' }), false);
  assert.equal(h.client.requests.length, 0);
  const selecting = h.view.select({ id: '42', kind: 'Script' });
  h.client.requests[0].reject(new Error('backend unavailable'));
  await selecting;
  assert.match(h.inspector.textContent, /backend unavailable/);
  assert.equal(h.metrics.textContent, '');
});

test('ApiClient cancel aborts only the named browser request channel', async () => {
  const signals = [];
  const client = new ApiClient('token', (_path, options) => {
    signals.push(options.signal);
    return new Promise(() => {});
  });
  void client.get('/one', 'one').catch(() => {});
  void client.get('/two', 'two').catch(() => {});
  client.cancel('one');
  assert.equal(signals[0].aborted, true);
  assert.equal(signals[1].aborted, false);
});

test('starting an action clears its scheduled poll before posting', async () => {
  const h = harness();
  const selecting = h.view.select({ id: '42', kind: 'Module' });
  h.client.requests[0].resolve(wire({ phase: 'Building', activeScopeId: '42' }));
  await selecting;
  const timerId = h.timers.length;
  const action = h.inspector.children.find((child) => child.textContent === 'Cancel').click();
  assert.ok(h.clearedTimers.includes(timerId));
  h.client.requests[1].resolve(wire({ phase: 'Cancelled', activeScopeId: null }));
  await action;
});

test('an older noncooperative poll response cannot overwrite a cancellation response', async () => {
  const h = harness();
  const selecting = h.view.select({ id: '42', kind: 'Module' });
  h.client.requests[0].resolve(wire({ phase: 'Building', activeScopeId: '42' }));
  await selecting;
  h.timers.at(-1).callback();
  const newer = h.inspector.children.find((child) => child.textContent === 'Cancel').click();
  h.client.requests[2].resolve(wire({ phase: 'Cancelled', knownCodeBytes: '12', knownInstructionCount: '4' }));
  await newer;
  h.client.requests[1].resolve(wire({ phase: 'Complete', codeBytes: '99', instructionCount: '88' }));
  await Promise.resolve();
  assert.match(h.inspector.textContent, /Statistics: Cancelled/);
  assert.doesNotMatch(h.inspector.textContent, /99/);
});

test('ApiClient post supports an optional action timeout', async () => {
  let signal;
  const client = new ApiClient('token', (_path, options) => {
    signal = options.signal;
    return new Promise(() => {});
  });
  await assert.rejects(
    client.post('/action', {}, 'action', { timeoutMs: 5 }),
    /timed out.*\/action/i,
  );
  assert.equal(signal.aborted, true);
});

test('a transient status error offers Retry and rechecks status rather than restarting cached work', async () => {
  const h = harness();
  const selecting = h.view.select({ id: '42', kind: 'Module' });
  h.client.requests[0].reject(new Error('temporary failure'));
  await selecting;
  assert.match(h.inspector.textContent, /temporary failure/);
  const retry = h.inspector.children.find((child) => child.textContent === 'Retry');
  assert.ok(retry);
  const retrying = retry.click();
  assert.equal(h.client.requests[1].method, 'GET');
  h.client.requests[1].resolve(wire({ phase: 'Complete', codeBytes: '5', instructionCount: '2' }));
  await retrying;
  assert.match(h.inspector.textContent, /code bytes: 5/i);
  assert.doesNotMatch(h.inspector.textContent, /temporary failure/);
});

test('a detached error Retry cannot request or affect a newly selected scope', async () => {
  const h = harness();
  const first = h.view.select({ id: '42', kind: 'Module' });
  h.client.requests[0].reject(new Error('temporary failure'));
  await first;
  const detachedRetry = h.inspector.children.find((child) => child.textContent === 'Retry');
  const second = h.view.select({ id: '43', kind: 'Script' });
  assert.equal(h.client.requests.length, 2);
  await detachedRetry.click();
  assert.equal(h.client.requests.length, 2);
  h.client.requests[1].resolve(wire({ entityId: '43', kind: 'Script', phase: 'Cancelled' }));
  await second;
  assert.match(h.inspector.textContent, /Statistics: Cancelled/);
});

test('default timer wrappers preserve the native global receiver', async () => {
  const originalSetTimeout = globalThis.setTimeout;
  const originalClearTimeout = globalThis.clearTimeout;
  const document = new FakeDocument();
  const client = new Client();
  let scheduled = false;
  let cleared = false;
  globalThis.setTimeout = function (_callback, delay) {
    if (this !== globalThis) {
      throw new TypeError('Illegal invocation');
    }
    assert.equal(delay, 250);
    scheduled = true;
    return 71;
  };
  globalThis.clearTimeout = function (id) {
    if (this !== globalThis) {
      throw new TypeError('Illegal invocation');
    }
    assert.equal(id, 71);
    cleared = true;
  };
  try {
    const view = new StatisticsView({
      client,
      elements: { metrics: new FakeElement(document), inspector: new FakeElement(document) },
    });
    const selecting = view.select({ id: '42', kind: 'Module' });
    client.requests[0].resolve(wire({ phase: 'Building', activeScopeId: '42' }));
    await selecting;
    assert.equal(scheduled, true);
    view.clear();
    assert.equal(cleared, true);
  } finally {
    globalThis.setTimeout = originalSetTimeout;
    globalThis.clearTimeout = originalClearTimeout;
  }
});

test('statistics scope mode toggles only for supported scope selections', async () => {
  const document = new FakeDocument();
  const client = new Client();
  const root = new FakeElement(document);
  const view = new StatisticsView({
    client,
    elements: { root, metrics: new FakeElement(document), inspector: new FakeElement(document) },
    setTimer() { return 1; }, clearTimer() {},
  });
  const selecting = view.select({ id: '42', kind: 'Module' });
  assert.equal(root.classList.contains('statistics-scope'), true);
  client.requests[0].resolve(wire());
  await selecting;
  view.clear();
  assert.equal(root.classList.contains('statistics-scope'), false);
  assert.equal(await view.select({ id: '9', kind: 'Field' }), false);
  assert.equal(root.classList.contains('statistics-scope'), false);
});
