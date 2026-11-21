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
import { ApiClient } from '../src/api-client.js';
import {
  pollPickStatus,
  validatePickStatus,
  validateRecentFiles,
} from '../src/document-switching.js';

const response = (body) => ({ ok: true, headers: new Headers(), json: async () => body });

test('pick status accepts every terminal and running state', () => {
  assert.deepEqual(validatePickStatus({ state: 'idle', path: null, error: null }),
    { state: 'idle', path: null, error: null });
  assert.deepEqual(validatePickStatus({ state: 'picking' }), { state: 'picking', path: null, error: null });
  assert.deepEqual(validatePickStatus({ state: 'cancelled', path: null, error: null }),
    { state: 'cancelled', path: null, error: null });
  assert.deepEqual(validatePickStatus({ state: 'selected', path: '/tmp/demo.abc' }),
    { state: 'selected', path: '/tmp/demo.abc', error: null });
  assert.deepEqual(validatePickStatus({ state: 'failed', path: null, error: 'zenity is not installed' }),
    { state: 'failed', path: null, error: 'zenity is not installed' });
  assert.deepEqual(validatePickStatus({ state: 'failed', path: null, error: null }),
    { state: 'failed', path: null, error: null });
});

test('pick status rejects malformed payloads', () => {
  for (const wire of [
    null,
    { state: 'running' },
    { state: 'selected' },
    { state: 'selected', path: '' },
    { state: 'cancelled', path: '/tmp/demo.abc' },
    { state: 'picking', path: '/tmp/demo.abc' },
    { state: 'failed', error: 42 },
    { state: 'selected', path: 7 },
    { state: 'idle', path: null, error: 'stale text' },
    ['picking'],
  ]) {
    assert.throws(() => validatePickStatus(wire), /invalid.*picker.*status/i);
  }
});

test('pick polling waits while the dialog is open and settles on a terminal state', async () => {
  const polls = [
    { state: 'picking' },
    { state: 'picking' },
    { state: 'selected', path: '/tmp/demo.abc' },
  ];
  const waits = [];
  const client = new ApiClient('secret', async (path) => {
    assert.equal(path, '/api/v1/pick/status');
    return response(polls.shift());
  });
  const status = await pollPickStatus({
    client,
    onWaiting: () => waits.push('waiting'),
    pause: async () => waits.push('pause'),
  });
  assert.deepEqual(status, { state: 'selected', path: '/tmp/demo.abc', error: null });
  assert.equal(waits.filter((entry) => entry === 'waiting').length, 2);
  assert.equal(waits.filter((entry) => entry === 'pause').length, 2);
});

test('pick polling treats idle after a backend restart as terminal and rejects invalid responses', async () => {
  let polls = 0;
  const idle = new ApiClient('secret', async () => {
    polls += 1;
    return response({ state: 'idle' });
  });
  assert.deepEqual(await pollPickStatus({ client: idle, pause: async () => {} }),
    { state: 'idle', path: null, error: null });
  assert.equal(polls, 1);

  const invalid = new ApiClient('secret', async () => response({ state: 'picking', path: 42 }));
  await assert.rejects(pollPickStatus({ client: invalid, pause: async () => {} }), /invalid.*picker.*status/i);
});

test('pick polling keeps waiting without any total poll limit', async () => {
  let polls = 0;
  const client = new ApiClient('secret', async () =>
    response(++polls <= 500 ? { state: 'picking' } : { state: 'cancelled' }));
  const status = await pollPickStatus({ client, onWaiting() {}, pause: async () => {} });
  assert.equal(status.state, 'cancelled');
  assert.equal(polls, 501);
});

test('recent files validation accepts and freezes well-formed entries', () => {
  const entries = validateRecentFiles({
    apiVersion: 1,
    entries: [
      { path: '/tmp/demo.abc', fileName: 'demo.abc', openedAt: 1793072979713 },
      { path: '/tmp/other.abc', fileName: 'other.abc', openedAt: 0 },
    ],
  });
  assert.equal(entries.length, 2);
  assert.deepEqual(entries[0], { path: '/tmp/demo.abc', fileName: 'demo.abc', openedAt: 1793072979713 });
  assert.ok(Object.isFrozen(entries));
  assert.ok(Object.isFrozen(entries[0]));
});

test('recent files validation rejects malformed payloads', () => {
  for (const wire of [
    null,
    { apiVersion: 2, entries: [] },
    { apiVersion: 1, entries: {} },
    { entries: [] },
    { apiVersion: 1, entries: [{ path: '/tmp/demo.abc', fileName: 'demo.abc' }] },
    { apiVersion: 1, entries: [{ path: '', fileName: 'demo.abc', openedAt: 1 }] },
    { apiVersion: 1, entries: [{ path: '/tmp/demo.abc', fileName: 9, openedAt: 1 }] },
    { apiVersion: 1, entries: [{ path: '/tmp/demo.abc', fileName: 'demo.abc', openedAt: -1 }] },
    { apiVersion: 1, entries: [{ path: '/tmp/demo.abc', fileName: 'demo.abc', openedAt: 1.5 }] },
    { apiVersion: 1, entries: [null] },
  ]) {
    assert.throws(() => validateRecentFiles(wire), /invalid.*recent.*files/i);
  }
  assert.deepEqual(validateRecentFiles({ apiVersion: 1, entries: [] }), []);
});

test('bodyless POST sends no content type while JSON POST keeps one', async () => {
  const seen = [];
  const client = new ApiClient('secret', async (path, options) => {
    seen.push({ path, options });
    return response({ state: 'picking' });
  });
  await client.post('/api/v1/pick', undefined, 'pick-start');
  await client.post('/api/v1/open', { path: '/tmp/demo.abc' }, 'document-open');
  assert.equal(seen.length, 2);
  const pick = seen[0];
  assert.equal(pick.options.method, 'POST');
  assert.equal(pick.options.body, undefined);
  assert.equal(pick.options.headers['Content-Type'], undefined);
  assert.equal(pick.options.headers.Authorization, 'Bearer secret');
  const open = seen[1];
  assert.equal(open.options.body, '{"path":"/tmp/demo.abc"}');
  assert.equal(open.options.headers['Content-Type'], 'application/json');
  assert.equal(open.options.headers.Authorization, 'Bearer secret');
});

test('shell wires the switch entry points and registers the new asset', async () => {
  const html = await readFile(new URL('../index.html', import.meta.url), 'utf8');
  assert.match(html, /<button class="file-chip" id="file-chip" type="button" disabled/);
  assert.match(html, /id="landing-open"/);
  assert.match(html, /id="landing-recent"/);
  assert.match(html, /id="toast"/);
  const main = await readFile(new URL('../src/main.js', import.meta.url), 'utf8');
  assert.match(main, /startPickFlow/);
  assert.match(main, /openPathFlow/);
  assert.match(main, /completeDocumentSwitch/);
  assert.match(main, /presentDocument/);
  assert.match(main, /pollSession/);
  assert.match(main, /showToast/);
});
