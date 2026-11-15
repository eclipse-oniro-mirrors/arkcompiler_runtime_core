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
import { ModuleStatisticsPrefetch, collectModuleScopes } from '../src/module-statistics-prefetch.js';

class FakeClient {
  constructor() {
    this.responses = new Map();
    this.postHandler = null;
    this.getCalls = [];
    this.postCalls = [];
    this.cancelled = [];
  }

  get(path) {
    this.getCalls.push(path);
    const value = this.responses.get(path);
    if (typeof value === 'function') {
      return Promise.resolve(value());
    }
    return Promise.resolve(value ?? { phase: 'NotStarted', knownCodeBytes: '0' });
  }

  post(path) {
    this.postCalls.push(path);
    if (this.postHandler !== null) {
      return Promise.resolve(this.postHandler(path));
    }
    return Promise.resolve({});
  }

  cancel(channel) { this.cancelled.push(channel); }
}

class FakeTimers {
  constructor() { this.queue = []; }

  setTimer(callback, delay) {
    this.queue.push({ callback, delay });
    return this.queue.length;
  }

  clearTimer(timer) {
    if (typeof timer === 'number') {
      this.queue.splice(timer - 1, 1);
    }
  }

  flushNext() { this.queue.shift().callback(); }

  get pending() { return this.queue.length; }
}

function structureFixture(ids) {
  return {
    roots: ['file'],
    nodes: new Map([
      ['file', { id: 'file', kind: 'File', children: ids }],
      ...ids.map((id) => [id, { id, kind: 'Module', children: [] }]),
    ]),
  };
}

async function drive(timers, rounds = 40) {
  for (let round = 0; round < rounds; round += 1) {
    // Let the microtask chain advance first: after start() the run is still
    // before its first pause, so a synchronous pending-check would bail out
    // immediately without driving anything.
    await new Promise((resolve) => setImmediate(resolve));
    if (timers.pending > 0) {
      timers.flushNext();
      continue;
    }
    await new Promise((resolve) => setImmediate(resolve));
    if (timers.pending === 0) {
      return;
    }
  }
}

test('collectModuleScopes collects Module/Script scopes plus nested Class scopes', () => {
  const structure = {
    roots: ['file'],
    nodes: new Map([
      ['file', { id: 'file', kind: 'File', children: ['m1', 'ns', 'g1', 's1', 'deep'] }],
      ['m1', { id: 'm1', kind: 'Module', children: ['c1'] }],
      ['c1', { id: 'c1', kind: 'Class', children: ['c2', 'iface'] }],
      ['c2', { id: 'c2', kind: 'Class', children: [] }],
      ['iface', { id: 'iface', kind: 'Interface', children: [] }],
      ['ns', { id: 'ns', kind: 'Namespace', children: [] }],
      ['g1', { id: 'g1', kind: 'Group', children: [] }],
      ['s1', { id: 's1', kind: 'Script', children: [] }],
      ['deep', { id: 'deep', kind: 'Module', children: ['m2'] }],
      ['m2', { id: 'm2', kind: 'Module', children: [] }],
    ]),
  };
  // Interface and plain Namespace stay on-demand-only; Class scopes join so
  // tree rows can show per-class sizes.
  assert.deepEqual(collectModuleScopes(structure), ['m1', 'c1', 'c2', 's1', 'deep', 'm2']);
});

// statisticsPath is query-style: status and start share /api/v1/statistics?id=<n>
// (GET vs POST tells them apart; only cancel appends /cancel).
test('prefetch walks scopes serially and stores code bytes', async () => {
  const client = new FakeClient();
  const timers = new FakeTimers();
  client.responses.set('/api/v1/statistics?id=1', { phase: 'Complete', codeBytes: '100' });
  let calls2 = 0;
  client.responses.set('/api/v1/statistics?id=2', () => {
    calls2 += 1;
    if (calls2 === 1) {
      return { phase: 'NotStarted', knownCodeBytes: '0' };
    }
    if (calls2 === 2) {
      return { phase: 'Building', knownCodeBytes: '200' };
    }
    return { phase: 'Complete', codeBytes: '250' };
  });
  client.responses.set('/api/v1/statistics?id=3', { phase: 'Partial', knownCodeBytes: '80' });
  const progress = [];
  const prefetch = new ModuleStatisticsPrefetch({
    client,
    onProgress: (state) => progress.push({ done: state.done, total: state.total }),
    setTimer: (callback, delay) => timers.setTimer(callback, delay),
    clearTimer: (timer) => timers.clearTimer(timer),
  });
  prefetch.start(structureFixture(['1', '2', '3']));
  await drive(timers);
  assert.deepEqual(prefetch.stats.get('1'), { codeBytes: 100, partial: false });
  assert.deepEqual(prefetch.stats.get('2'), { codeBytes: 250, partial: false });
  assert.deepEqual(prefetch.stats.get('3'), { codeBytes: 80, partial: true });
  assert.deepEqual(progress, [
    { done: 0, total: 3 }, { done: 1, total: 3 }, { done: 2, total: 3 }, { done: 3, total: 3 },
  ]);
  assert.ok(!client.postCalls.includes('/api/v1/statistics?id=1'));
  assert.ok(client.postCalls.includes('/api/v1/statistics?id=2'));
});

test('prefetch retries a contended start without dying', async () => {
  const client = new FakeClient();
  const timers = new FakeTimers();
  let statusCalls = 0;
  client.responses.set('/api/v1/statistics?id=1', () => {
    statusCalls += 1;
    return statusCalls >= 3
      ? { phase: 'Complete', codeBytes: '42' }
      : { phase: 'NotStarted', knownCodeBytes: '0' };
  });
  let postAttempts = 0;
  client.postHandler = () => {
    postAttempts += 1;
    if (postAttempts === 1) {
      throw new Error('Another statistics calculation is still running');
    }
    return {};
  };
  const prefetch = new ModuleStatisticsPrefetch({
    client,
    setTimer: (callback, delay) => timers.setTimer(callback, delay),
    clearTimer: (timer) => timers.clearTimer(timer),
  });
  prefetch.start(structureFixture(['1']));
  await drive(timers);
  assert.equal(postAttempts, 2);
  assert.deepEqual(prefetch.stats.get('1'), { codeBytes: 42, partial: false });
  assert.equal(prefetch.doneScopes, 1);
});

test('cancel stops the queue and cancels the transport channel', async () => {
  const client = new FakeClient();
  const timers = new FakeTimers();
  client.responses.set('/api/v1/statistics?id=1', { phase: 'NotStarted', knownCodeBytes: '0' });
  const prefetch = new ModuleStatisticsPrefetch({
    client,
    setTimer: (callback, delay) => timers.setTimer(callback, delay),
    clearTimer: (timer) => timers.clearTimer(timer),
  });
  prefetch.start(structureFixture(['1']));
  await new Promise((resolve) => setImmediate(resolve));
  const getsBefore = client.getCalls.length;
  prefetch.cancel();
  await drive(timers);
  assert.equal(client.getCalls.length, getsBefore);
  assert.ok(client.cancelled.includes('module-statistics-prefetch'));
  assert.equal(prefetch.doneScopes, 0);
  assert.equal(prefetch.totalScopes, 0);
});

test('start replaces the previous run', async () => {
  const client = new FakeClient();
  const timers = new FakeTimers();
  client.responses.set('/api/v1/statistics?id=1', { phase: 'NotStarted', knownCodeBytes: '0' });
  client.responses.set('/api/v1/statistics?id=5', { phase: 'Complete', codeBytes: '7' });
  const prefetch = new ModuleStatisticsPrefetch({
    client,
    setTimer: (callback, delay) => timers.setTimer(callback, delay),
    clearTimer: (timer) => timers.clearTimer(timer),
  });
  prefetch.start(structureFixture(['1']));
  await new Promise((resolve) => setImmediate(resolve));
  prefetch.start(structureFixture(['5']));
  await drive(timers);
  assert.equal(prefetch.stats.get('1'), undefined);
  assert.deepEqual(prefetch.stats.get('5'), { codeBytes: 7, partial: false });
});
