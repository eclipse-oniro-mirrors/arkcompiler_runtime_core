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

import { statisticsPath } from './statistics-view.js';

const PREFETCH_CHANNEL = 'module-statistics-prefetch';
const POLL_INTERVAL_MS = 250;
const FETCH_RETRY_MS = 500;
const FETCH_RETRY_LIMIT = 60;
// Breathing room between modules so a user-triggered statistics scan can
// grab the single active scope slot without waiting for the whole queue.
const YIELD_MS = 150;
const KNOWN_PHASES = new Set(['NotStarted', 'Building', 'Complete', 'Partial', 'Cancelled', 'Failed']);
const STORED_PHASES = new Set(['Complete', 'Partial']);

function parseByteValue(value) {
  if (value === null || value === undefined) {
    return null;
  }
  const parsed = Number(value);
  return Number.isFinite(parsed) && parsed >= 0 ? parsed : null;
}

function parseSnapshot(payload) {
  if (payload === null || typeof payload !== 'object') {
    return null;
  }
  const phase = String(payload.phase ?? '');
  if (!KNOWN_PHASES.has(phase)) {
    return null;
  }
  const known = parseByteValue(payload.knownCodeBytes);
  const total = parseByteValue(payload.codeBytes);
  const bytes = phase === 'Partial' ? known : (total ?? known);
  return { phase, codeBytes: bytes };
}

// Prefetched scope kinds: Module/Script at the file root stay the sortable
// levels; Class scopes join so tree rows can show per-class method code bytes
// (class totals are a drill-down subset of their module). Interface and plain
// namespace scopes are served on demand only — their totals are usually zero,
// so prefetching them would add scheduling noise without information.
const PREFETCH_KINDS = new Set(['Module', 'Script', 'Class']);

export function collectModuleScopes(structure) {
  const ids = [];
  const visit = (nodeId) => {
    const node = structure.nodes.get(nodeId);
    if (node === undefined) {
      return;
    }
    if (PREFETCH_KINDS.has(node.kind)) {
      ids.push(nodeId);
    }
    for (const childId of node.children) {
      visit(childId);
    }
  };
  for (const rootId of structure.roots) {
    visit(rootId);
  }
  return ids;
}

export class ModuleStatisticsPrefetch {
  #client;
  #onProgress;
  #setTimer;
  #clearTimer;
  #generation = 0;
  #ids = [];
  #index = 0;
  #stats = new Map();
  #timer = null;

  constructor({ client, onProgress = () => {},
    setTimer = (callback, delay) => globalThis.setTimeout(callback, delay),
    clearTimer = (timer) => globalThis.clearTimeout(timer) } = {}) {
    this.#client = client;
    this.#onProgress = onProgress;
    this.#setTimer = setTimer;
    this.#clearTimer = clearTimer;
  }

  get stats() {
    return this.#stats;
  }

  get totalScopes() {
    return this.#ids.length;
  }

  get doneScopes() {
    return this.#index;
  }

  start(structure) {
    this.cancel();
    const generation = ++this.#generation;
    this.#ids = collectModuleScopes(structure);
    this.#notify();
    void this.#run(generation);
  }

  cancel() {
    this.#generation += 1;
    this.#clearTimer(this.#timer);
    this.#timer = null;
    this.#client.cancel?.(PREFETCH_CHANNEL);
    this.#ids = [];
    this.#index = 0;
    this.#stats = new Map();
  }

  #notify() {
    this.#onProgress({ done: this.#index, total: this.#ids.length, stats: this.#stats });
  }

  #pause(ms) {
    return new Promise((resolve) => {
      this.#timer = this.#setTimer(() => {
        this.#timer = null;
        resolve();
      }, ms);
    });
  }

  async #run(generation) {
    while (this.#generation === generation && this.#index < this.#ids.length) {
      const id = this.#ids[this.#index];
      await this.#prefetchScope(id, generation);
      if (this.#generation !== generation) {
        return;
      }
      this.#index += 1;
      this.#notify();
      if (this.#index < this.#ids.length) {
        await this.#pause(YIELD_MS);
      }
    }
  }

  // Returns when the scope reached a terminal phase ('done'), was abandoned
  // after repeated transport failures ('skip'), or the run went stale.
  async #prefetchScope(id, generation) {
    let retries = 0;
    for (;;) {
      if (this.#generation !== generation) {
        return 'stale';
      }
      let snapshot;
      try {
        snapshot = await this.#fetchSnapshot(id);
      } catch (error) {
        if (this.#generation !== generation) {
          return 'stale';
        }
        retries += 1;
        if (retries > FETCH_RETRY_LIMIT) {
          return 'skip';
        }
        await this.#pause(FETCH_RETRY_MS);
        continue;
      }
      if (this.#generation !== generation) {
        return 'stale';
      }
      const phase = snapshot.phase;
      if (STORED_PHASES.has(phase)) {
        if (snapshot.codeBytes !== null) {
          this.#stats.set(id, { codeBytes: snapshot.codeBytes, partial: phase === 'Partial' });
        }
        return 'done';
      }
      if (phase === 'Failed' || phase === 'Cancelled') {
        return 'skip';
      }
      if (phase === 'NotStarted') {
        try {
          await this.#client.post(statisticsPath(id, 'start'), {}, PREFETCH_CHANNEL, { timeoutMs: 30000 });
        } catch (error) {
          // Contention: a user-selected scope owns the active slot. The next
          // status poll decides whether to keep waiting or try starting again.
        }
      }
      await this.#pause(POLL_INTERVAL_MS);
    }
  }

  async #fetchSnapshot(id) {
    const response = await this.#client.get(statisticsPath(id), PREFETCH_CHANNEL, { timeoutMs: 30000 });
    const snapshot = parseSnapshot(response);
    if (snapshot === null) {
      throw new Error('Malformed statistics snapshot');
    }
    return snapshot;
  }
}
