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

export const RESPONSE_WIRE_BYTES = Symbol('responseWireBytes');

export class ApiClient {
  #token;
  #fetch;
  #generations = new Map();
  #controllers = new Map();

  constructor(token, fetchImpl = globalThis.fetch.bind(globalThis)) {
    this.#token = token;
    this.#fetch = fetchImpl;
  }

  async get(path, channel = path, { timeoutMs = 0 } = {}) {
    return this.#request(path, { cache: 'no-store' }, channel, timeoutMs);
  }

  async post(path, body = undefined, channel = path, { timeoutMs = 0 } = {}) {
    // An omitted body keeps the request bodyless: some routes (the file
    // picker) reject any request body, so only objects opt into JSON encoding.
    const json = body === undefined ? {} : { headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) };
    return this.#request(path, {
      method: 'POST',
      cache: 'no-store',
      ...json,
    }, channel, timeoutMs);
  }

  async put(path, body = undefined, channel = path, { timeoutMs = 0 } = {}) {
    const json = body === undefined ? {} : { headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) };
    return this.#request(path, {
      method: 'PUT',
      cache: 'no-store',
      ...json,
    }, channel, timeoutMs);
  }

  cancel(channel) {
    this.#generations.set(channel, (this.#generations.get(channel) ?? 0) + 1);
    this.#controllers.get(channel)?.abort();
    this.#controllers.delete(channel);
  }

  async #request(path, options, channel, timeoutMs = 0) {
    this.#controllers.get(channel)?.abort();
    const controller = new AbortController();
    this.#controllers.set(channel, controller);
    const generation = (this.#generations.get(channel) ?? 0) + 1;
    this.#generations.set(channel, generation);
    const endpoint = path.split('?')[0];
    const timeoutError = new Error(`Request timed out (${endpoint}); check that the local explorer is still running, then reload.`);
    let timedOut = false;
    let timer;
    const checkCurrent = () => {
      if (generation !== this.#generations.get(channel)) {
        throw new Error('Stale bytecode explorer request');
      }
      if (timedOut) {
        throw timeoutError;
      }
    };
    const exchange = async () => {
      let response;
      try {
        response = await this.#fetch(path, {
          ...options,
          signal: controller.signal,
          headers: { ...options.headers, Authorization: `Bearer ${this.#token}` },
        });
      } catch (error) {
        checkCurrent();
        if (error.name === 'AbortError') {
          throw error;
        }
        throw new Error(`Connection failed (${endpoint}); check that the local explorer is still running, then reload.`);
      }
      checkCurrent();
      if (!response.ok) {
        let message = `Request failed (${response.status})`;
        try {
          const body = await response.json();
          if (typeof body?.error === 'string') {
            message += `: ${body.error}`;
          }
        } catch {
          // Keep the transport-level message for non-JSON failures.
        }
        throw new Error(message);
      }
      let body;
      try {
        body = await response.json();
      } catch (error) {
        checkCurrent();
        if (error.name !== 'SyntaxError') {
          throw new Error(`Connection interrupted (${endpoint}); reload the local explorer.`);
        }
        throw new Error(`Invalid JSON response (${endpoint}); reload the local explorer.`);
      }
      checkCurrent();
      const wireBytes = Number(response.headers?.get?.('content-length'));
      if (body !== null && typeof body === 'object' && Number.isFinite(wireBytes) && wireBytes >= 0) {
        Object.defineProperty(body, RESPONSE_WIRE_BYTES, { value: wireBytes });
      }
      return body;
    };
    try {
      if (timeoutMs <= 0) {
        return await exchange();
      }
      return await Promise.race([
        exchange(),
        new Promise((_, reject) => {
          timer = setTimeout(() => {
            timedOut = true;
            controller.abort();
            reject(timeoutError);
          }, timeoutMs);
        }),
      ]);
    } catch (error) {
      if (generation !== this.#generations.get(channel) || error.name === 'AbortError') {
        throw new Error('Stale bytecode explorer request');
      }
      // Backend diagnostics remain useful, but a reflected credential is never shown.
      if (this.#token && error.message.includes(this.#token)) {
        throw new Error(error.message.replaceAll(this.#token, '[redacted]'));
      }
      throw error;
    } finally {
      clearTimeout(timer);
      if (this.#controllers.get(channel) === controller) {
        this.#controllers.delete(channel);
      }
    }
  }
}
