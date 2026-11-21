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
import { readFile } from 'node:fs/promises';
import vm from 'node:vm';
import { ApiClient } from '../src/api-client.js';
import * as loading from '../src/session-loading.js';

const status = (state, stage = state, error = null) => ({ apiVersion: 1, state, stage, error });
const response = (body) => ({ ok: true, headers: new Headers(), json: async () => body });

test('opening polls every backend phase before requesting document and Structure', async () => {
  assert.equal(typeof loading.loadSessionModels, 'function');
  const stages = ['opening', 'methods', 'physical', 'structure', 'locations', 'metadata', 'serializing'];
  const pending = [...stages.map((stage) => status('loading', stage)), status('ready')];
  const requests = [];
  const labels = [];
  const client = new ApiClient('secret', async (path, options) => {
    assert.equal(options.headers.Authorization, 'Bearer secret');
    requests.push(path);
    if (path.endsWith('/status')) {
      return response(pending.shift());
    }
    assert.equal(pending.length, 0, 'no document data requested before ready');
    return response(path.endsWith('/document') ? { fileName: 'test.abc' } : { nodes: [] });
  });
  const models = await loading.loadSessionModels({ client, onStage: (label) => labels.push(label), pause: async () => {} });
  assert.deepEqual(models, { empty: false, documentModel: { fileName: 'test.abc' }, structureModel: { nodes: [] } });
  assert.equal(requests.filter((path) => path.endsWith('/status')).length, 8);
  assert.equal(new Set(labels.slice(0, 7)).size, 7);
  assert.match(labels.at(-1), /download.*structure/i);
  assert.ok(labels.every((label) => !label.includes('%')));
});

test('empty session resolves without requesting document or Structure', async () => {
  const paths = [];
  const client = new ApiClient('secret', async (path) => { paths.push(path); return response(status('empty')); });
  const models = await loading.loadSessionModels({ client, onStage() {}, pause: async () => {} });
  assert.deepEqual(models, { empty: true });
  assert.deepEqual(paths, ['/api/v1/session/status']);
});

test('poll session surfaces a failed switch through lastError on ready', async () => {
  const statusPayloads = [
    status('loading', 'opening'),
    { ...status('ready'), lastError: 'Cannot open ABC file: /missing.abc' },
  ];
  const client = new ApiClient('secret', async () => response(statusPayloads.shift()));
  const stages = [];
  const final = await loading.pollSession({ client, onStage: (label) => stages.push(label), pause: async () => {} });
  assert.equal(final.state, 'ready');
  assert.equal(final.lastError, 'Cannot open ABC file: /missing.abc');
  assert.deepEqual(stages, ['Opening ABC file…']);
});

test('invalid session payloads including malformed lastError are rejected', async () => {
  for (const payload of [
    status('empty', 'ready'),
    { ...status('ready'), lastError: 42 },
    { ...status('ready'), lastError: false },
    { ...status('ready'), stage: 'empty' },
  ]) {
    await assert.rejects(loading.pollSession({
      client: new ApiClient('secret', async () => response(payload)),
      onStage() {}, pause: async () => {},
    }), /invalid.*status/i);
  }
});

test('loading has no arbitrary total poll limit', async () => {
  assert.equal(typeof loading.loadSessionModels, 'function');
  let polls = 0;
  await loading.loadSessionModels({
    client: new ApiClient('secret', async (path) => response(path.endsWith('/status')
      ? (++polls <= 1001 ? status('loading', 'methods') : status('ready')) : {})),
    onStage() {}, pause: async () => {},
  });
  assert.equal(polls, 1002);
});

test('failed, cancelled and invalid statuses stop before heavy requests', async () => {
  assert.equal(typeof loading.loadSessionModels, 'function');
  for (const [payload, expected] of [
    [status('failed', 'failed', 'ABC reader rejected file'), /ABC reader rejected file/],
    [status('cancelled'), /cancelled/i],
    [status('loading', 'invented'), /invalid.*status/i],
    [{ ...status('ready'), apiVersion: 2 }, /invalid.*status/i],
    [status('loading', 'ready'), /invalid.*status/i],
  ]) {
    const paths = [];
    await assert.rejects(loading.loadSessionModels({
      client: new ApiClient('secret', async (path) => { paths.push(path); return response(payload); }),
      onStage() {}, pause: async () => {},
    }), expected);
    assert.deepEqual(paths, ['/api/v1/session/status']);
  }
});

test('bounded API requests time out both headers and stalled JSON bodies', async () => {
  for (const phase of ['headers', 'body']) {
    let signal;
    const client = new ApiClient('secret', async (_path, options) => {
      signal = options.signal;
      if (phase === 'headers') {
        return new Promise(() => {});
      }
      return { ok: true, json: () => new Promise(() => {}) };
    });
    const outcome = await Promise.race([
      client.get('/api/v1/session/status', 'opening', { timeoutMs: 10 }).then(() => 'success', (error) => error.message),
      new Promise((resolve) => setTimeout(() => resolve('unbounded request'), 100)),
    ]);
    assert.match(outcome, /timed out.*session\/status/i);
    assert.equal(signal.aborted, true);
    assert.ok(!outcome.includes('secret'));
  }
});

test('API distinguishes connection failure, HTTP rejection and malformed JSON', async () => {
  for (const [fetcher, expected] of [
    [async () => { throw new TypeError('secret in an unsafe transport diagnostic'); }, /connection.*session\/status/i],
    [async () => ({ ok: false, status: 403, json: async () => ({ error: 'Forbidden' }) }), /403.*Forbidden/],
    [async () => ({ ok: true, json: async () => { throw new SyntaxError('bad json'); } }), /invalid JSON.*session\/status/i],
    [async () => ({ ok: true, json: async () => { throw new TypeError('body interrupted'); } }), /connection interrupted.*session\/status/i],
  ]) {
    const client = new ApiClient('secret', fetcher);
    await assert.rejects(client.get('/api/v1/session/status', 'opening', { timeoutMs: 50 }), (error) => {
      assert.match(error.message, expected);
      assert.ok(!error.message.includes('secret'));
      return true;
    });
  }
});

test('stale bounded response cannot replace a newer request', async () => {
  let resolveOld;
  let calls = 0;
  const client = new ApiClient('secret', async () => ++calls === 1
    ? new Promise((resolve) => { resolveOld = resolve; }) : response({ fresh: true }));
  const old = client.get('/api/v1/session/status', 'opening', { timeoutMs: 50 });
  const current = client.get('/api/v1/session/status', 'opening', { timeoutMs: 50 });
  resolveOld(response({ fresh: false }));
  await assert.rejects(old, /stale/i);
  assert.deepEqual(await current, { fresh: true });
});

test('classic bootstrap catches module import failure before main initializes', async () => {
  const source = await readFile(new URL('../src/bootstrap.js', import.meta.url), 'utf8').catch(() => '');
  assert.match(source, /function bootstrapExplorer/);
  const elements = new Map(['file-name', 'error-state', 'status-text'].map((id) => [id, {
    textContent: '', classList: { remove(name) { this.removed = name; } },
  }]));
  const context = vm.createContext({ setTimeout, clearTimeout });
  vm.runInContext(source, context);
  context.document = { getElementById: (id) => elements.get(id) };
  await context.bootstrapExplorer(async () => { throw new Error('secret module URL'); });
  assert.match(elements.get('error-state').textContent, /JavaScript.*reload/i);
  assert.ok(!elements.get('error-state').textContent.includes('secret'));
  assert.equal(elements.get('error-state').classList.removed, 'hidden');
  assert.match(elements.get('file-name').textContent, /failed/i);
});

test('classic bootstrap times out a stalled module without starting it after late resolution', async () => {
  const source = await readFile(new URL('../src/bootstrap.js', import.meta.url), 'utf8');
  const elements = new Map(['file-name', 'error-state', 'status-text'].map((id) => [id, {
    textContent: '', classList: { remove() {} },
  }]));
  let timeout;
  const context = vm.createContext({
    setTimeout(callback, duration) { assert.equal(duration, 30000); timeout = callback; return 1; },
    clearTimeout() {},
  });
  vm.runInContext(source, context);
  context.document = { getElementById: (id) => elements.get(id) };
  let resolveModule;
  let starts = 0;
  const operation = context.bootstrapExplorer(() => new Promise((resolve) => { resolveModule = resolve; }));
  timeout();
  await operation;
  assert.match(elements.get('error-state').textContent, /JavaScript.*timed out.*reload/i);
  resolveModule({ start: () => { starts += 1; } });
  await Promise.resolve();
  assert.equal(starts, 0);
});

test('classic bootstrap clears its module timer before awaiting backend startup', async () => {
  const source = await readFile(new URL('../src/bootstrap.js', import.meta.url), 'utf8');
  let timerActive = false;
  const context = vm.createContext({
    setTimeout() { timerActive = true; return 1; },
    clearTimeout() { timerActive = false; },
  });
  vm.runInContext(source, context);
  context.document = { getElementById: () => ({ textContent: '' }) };
  let started = false;
  await context.bootstrapExplorer(async () => ({ start: async () => {
    assert.equal(timerActive, false, 'backend load is not capped by the module deadline');
    started = true;
  } }));
  assert.equal(started, true);
});

test('shell uses external classic bootstrap and main exports explicit startup', async () => {
  const html = await readFile(new URL('../index.html', import.meta.url), 'utf8');
  assert.match(html, /<script src="\/assets\/bootstrap.js" defer><\/script>/);
  assert.ok(!html.includes('type="module"'));
  const main = await readFile(new URL('../src/main.js', import.meta.url), 'utf8');
  assert.match(main, /export async function start/);
  assert.match(main, /await loadSessionModels/);
  assert.match(main, /Indexing Structure/);
  assert.match(main, /Rendering workbench/);
});
