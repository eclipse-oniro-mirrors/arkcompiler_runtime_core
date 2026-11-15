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

import { loadExecutableWorkbenchViews } from '../src/workbench-selection.js';

function deferred() {
  let resolve;
  const promise = new Promise((accept) => { resolve = accept; });
  return { promise, resolve };
}

test('Disassembly resolves before CFG and supplies its exact method offset', async () => {
  const disassembly = deferred();
  const cfg = deferred();
  const events = [];
  const operation = loadExecutableWorkbenchViews({
    entityId: '42',
    requestedMethodOffset: null,
    requestDisassembly: (entityId, methodOffset) => {
      events.push(['request-disassembly', entityId, methodOffset]);
      return disassembly.promise;
    },
    requestCfg: (entityId, methodOffset) => {
      events.push(['request-cfg', entityId, methodOffset]);
      return cfg.promise;
    },
    isCurrent: () => true,
    onBeginLoading: () => events.push(['begin-loading']),
    onDisassembly: (_response, methodOffset) => events.push(['render-disassembly', methodOffset]),
    onCfg: (_response, methodOffset) => events.push(['render-cfg', methodOffset]),
  });
  assert.deepEqual(events, [['begin-loading'], ['request-disassembly', '42', null]]);
  disassembly.resolve({ function: { methodOffset: '0x123' } });
  await Promise.resolve();
  await Promise.resolve();
  assert.deepEqual(events, [
    ['begin-loading'],
    ['request-disassembly', '42', null],
    ['render-disassembly', 0x123n],
    ['request-cfg', '42', 0x123n],
  ]);
  cfg.resolve({ function: { methodOffset: '0x123' } });
  assert.deepEqual(await operation, { stale: false, methodOffset: 0x123n });
  assert.deepEqual(events.at(-1), ['render-cfg', 0x123n]);
});

test('a stale selection never launches its dependent CFG request', async () => {
  const disassembly = deferred();
  let current = true;
  let cfgRequests = 0;
  let renders = 0;
  const operation = loadExecutableWorkbenchViews({
    entityId: '42',
    requestedMethodOffset: 0x100n,
    requestDisassembly: () => disassembly.promise,
    requestCfg: () => { cfgRequests += 1; },
    isCurrent: () => current,
    onBeginLoading: () => {},
    onDisassembly: () => { renders += 1; },
    onCfg: () => { renders += 1; },
  });
  current = false;
  disassembly.resolve({ function: { methodOffset: '0x100' } });
  assert.deepEqual(await operation, { stale: true, methodOffset: null });
  assert.equal(cfgRequests, 0);
  assert.equal(renders, 0);
});

test('mismatched CFG identity is rejected instead of joining two methods', async () => {
  await assert.rejects(loadExecutableWorkbenchViews({
    entityId: '42',
    requestedMethodOffset: 0x100n,
    requestDisassembly: async () => ({ function: { methodOffset: '0x100' } }),
    requestCfg: async () => ({ function: { methodOffset: '0x101' } }),
    isCurrent: () => true,
    onBeginLoading() {},
    onDisassembly() {},
    onCfg() {},
  }), /method offset/i);
});

test('selection invalidates executable views synchronously on delayed, failed, and cached requests', async () => {
  for (const scenario of ['delayed', 'failed', 'cached']) {
    const events = [];
    const disassembly = deferred();
    const operation = loadExecutableWorkbenchViews({
      entityId: '42',
      requestDisassembly: () => {
        events.push('request');
        if (scenario === 'failed') {
          return Promise.reject(new Error('network failure'));
        }
        if (scenario === 'cached') {
          return Promise.resolve({ function: { methodOffset: '0x100' } });
        }
        return disassembly.promise;
      },
      requestCfg: async () => ({ function: { methodOffset: '0x100' } }),
      isCurrent: () => true,
      onBeginLoading: () => events.push('clear'),
      onDisassembly: () => events.push('disassembly'),
      onCfg: () => events.push('cfg'),
    });
    assert.deepEqual(events, ['clear', 'request'], scenario);
    if (scenario === 'delayed') {
      disassembly.resolve({ function: { methodOffset: '0x100' } });
    }
    if (scenario === 'failed') {
      await assert.rejects(operation, /network failure/);
    } else {
      await operation;
    }
  }
});

test('rapid switch-back invalidates each selection before either delayed request settles', async () => {
  const first = deferred();
  const second = deferred();
  const events = [];
  let generation = 1;
  const start = (promise, ownGeneration) => loadExecutableWorkbenchViews({
    entityId: '42',
    requestDisassembly: () => promise,
    requestCfg: async () => ({ function: { methodOffset: '0x100' } }),
    isCurrent: () => generation === ownGeneration,
    onBeginLoading: () => events.push(`clear-${ownGeneration}`),
    onDisassembly: () => events.push(`disassembly-${ownGeneration}`),
    onCfg: () => events.push(`cfg-${ownGeneration}`),
  });
  const stale = start(first.promise, generation);
  generation = 2;
  const current = start(second.promise, generation);
  assert.deepEqual(events, ['clear-1', 'clear-2']);
  first.resolve({ function: { methodOffset: '0x100' } });
  assert.deepEqual(await stale, { stale: true, methodOffset: null });
  second.resolve({ function: { methodOffset: '0x100' } });
  assert.deepEqual(await current, { stale: false, methodOffset: 0x100n });
});
