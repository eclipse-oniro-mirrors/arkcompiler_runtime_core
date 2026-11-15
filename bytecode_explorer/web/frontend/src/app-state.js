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

const TOKEN_KEY = 'ark-bytecode-explorer-token';
const TOKEN_PATTERN = /^[0-9a-f]{64}$/i;

export class BoundedResponseCache {
  #maxWeight;
  #weightOf;
  #entries = new Map();
  #totalWeight = 0;

  constructor(maxWeight, weightOf) {
    this.#maxWeight = maxWeight;
    this.#weightOf = weightOf;
  }

  get(key) {
    const entry = this.#entries.get(key);
    if (!entry) {
      return undefined;
    }
    this.#entries.delete(key);
    this.#entries.set(key, entry);
    return entry.value;
  }

  set(key, value) {
    const previous = this.#entries.get(key);
    if (previous) {
      this.#entries.delete(key);
      this.#totalWeight -= previous.weight;
    }
    const weight = Math.max(0, this.#weightOf(value));
    if (!Number.isFinite(weight) || weight > this.#maxWeight) {
      return;
    }
    while (this.#entries.size && this.#totalWeight + weight > this.#maxWeight) {
      const oldestKey = this.#entries.keys().next().value;
      const oldest = this.#entries.get(oldestKey);
      this.#entries.delete(oldestKey);
      this.#totalWeight -= oldest.weight;
    }
    this.#entries.set(key, { value, weight });
    this.#totalWeight += weight;
  }
}

export function bootstrapToken(hash, storage) {
  const fragment = hash.startsWith('#') ? hash.slice(1) : hash;
  const fragmentToken = new URLSearchParams(fragment).get('token');
  if (fragmentToken !== null) {
    if (!TOKEN_PATTERN.test(fragmentToken)) {
      throw new Error('Invalid bytecode explorer session token');
    }
    storage.setItem(TOKEN_KEY, fragmentToken);
    return fragmentToken;
  }

  const stored = storage.getItem(TOKEN_KEY);
  if (!TOKEN_PATTERN.test(stored ?? '')) {
    throw new Error('Missing bytecode explorer session token');
  }
  return stored;
}
