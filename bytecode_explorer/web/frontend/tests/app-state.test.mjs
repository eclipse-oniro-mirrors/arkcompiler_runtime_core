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

import { BoundedResponseCache, bootstrapToken } from '../src/app-state.js';
import { ApiClient, RESPONSE_WIRE_BYTES } from '../src/api-client.js';

function storage() {
  const values = new Map();
  return {
    getItem: (key) => values.get(key) ?? null,
    setItem: (key, value) => values.set(key, value),
  };
}

test('fragment token is retained only in per-tab session storage', () => {
  const session = storage();
  const token = 'a'.repeat(64);

  assert.equal(bootstrapToken(`#token=${token}`, session), token);
  assert.equal(bootstrapToken('', session), token);
  assert.throws(() => bootstrapToken('#token=bad', storage()), /session token/i);
});

test('response cache evicts by estimated bytes and refreshes LRU order', () => {
  const cache = new BoundedResponseCache(10, (value) => value.bytes);
  cache.set('a', { bytes: 4 });
  cache.set('b', { bytes: 4 });
  assert.deepEqual(cache.get('a'), { bytes: 4 });
  cache.set('c', { bytes: 4 });

  assert.equal(cache.get('b'), undefined);
  assert.deepEqual(cache.get('a'), { bytes: 4 });
  assert.deepEqual(cache.get('c'), { bytes: 4 });
  cache.set('oversized', { bytes: 11 });
  assert.equal(cache.get('oversized'), undefined);
});

test('api client sends the bearer token and rejects stale requests', async () => {
  const calls = [];
  const fetchImpl = async (path, options) => {
    calls.push({ path, options });
    return { ok: true, json: async () => ({ path }) };
  };
  const client = new ApiClient('b'.repeat(64), fetchImpl);

  const first = client.get('/api/v1/document', 'document');
  const second = client.get('/api/v1/structure', 'document');

  await assert.rejects(first, /stale/i);
  assert.deepEqual(await second, { path: '/api/v1/structure' });
  assert.equal(calls[0].options.headers.Authorization, `Bearer ${'b'.repeat(64)}`);
  assert.equal(calls[0].options.signal.aborted, true);
  assert.equal(calls[1].options.signal.aborted, false);
});

test('api client posts authenticated json through the same stale channel guard', async () => {
  const calls = [];
  const fetchImpl = async (path, options) => {
    calls.push({ path, options });
    return {
      ok: true,
      headers: { get: (name) => name.toLowerCase() === 'content-length' ? '123' : null },
      json: async () => ({ revision: JSON.parse(options.body).revision }),
    };
  };
  const client = new ApiClient('c'.repeat(64), fetchImpl);

  const response = await client.post('/api/v1/cfg/layout?id=7', { revision: 3 }, 'cfg-layout');
  assert.deepEqual(response, { revision: 3 });
  assert.equal(response[RESPONSE_WIRE_BYTES], 123);
  assert.equal(calls[0].options.method, 'POST');
  assert.equal(calls[0].options.headers.Authorization, `Bearer ${'c'.repeat(64)}`);
  assert.equal(calls[0].options.headers['Content-Type'], 'application/json');
  assert.equal(calls[0].options.body, '{"revision":3}');
});
