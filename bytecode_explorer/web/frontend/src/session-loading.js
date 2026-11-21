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

const STAGES = new Map([
  ['opening', 'Opening ABC file…'],
  ['methods', 'Reading methods…'],
  ['physical', 'Reading physical ABC items…'],
  ['structure', 'Building Structure…'],
  ['locations', 'Indexing byte locations…'],
  ['metadata', 'Preparing Metadata…'],
  ['serializing', 'Serializing Structure…'],
]);

const STATES = new Set(['loading', 'ready', 'failed', 'cancelled', 'empty']);

// This bounds a single HTTP exchange, never the duration of the backend load.
const REQUEST_TIMEOUT_MS = 30000;
const POLL_INTERVAL_MS = 250;

function validateSessionStatus(status) {
  // `lastError` may be absent in older responses; only a non-string value is invalid.
  const lastError = status.lastError ?? null;
  if (status?.apiVersion !== 1 || !STATES.has(status.state) ||
      (status.error !== null && typeof status.error !== 'string') ||
      (lastError !== null && typeof lastError !== 'string') ||
      (status.state === 'loading' ? !STAGES.has(status.stage) : status.stage !== status.state)) {
    throw new Error('Invalid session status response; reload the local explorer.');
  }
  return { ...status, lastError };
}

// Polls the session status until it leaves the loading state. Serves both the
// initial bootstrap and document switches: a switch reads `lastError` on the
// final `ready` status to learn whether the new document or the previous one
// is being served.
export async function pollSession({
  client,
  onStage,
  pause = (milliseconds) => new Promise((resolve) => setTimeout(resolve, milliseconds)),
}) {
  for (;;) {
    const status = validateSessionStatus(
      await client.get('/api/v1/session/status', 'bootstrap-status', { timeoutMs: REQUEST_TIMEOUT_MS }));
    if (status.state === 'failed' || status.state === 'cancelled') {
      throw new Error(status.error || `ABC opening ${status.state}.`);
    }
    if (status.state !== 'loading') {
      return status;
    }
    onStage(STAGES.get(status.stage));
    await pause(POLL_INTERVAL_MS);
  }
}

// Downloads the models of the document the session currently publishes.
export async function fetchSessionModels({ client, onStage }) {
  onStage('Downloading document information…');
  const documentModel = await client.get('/api/v1/document', 'bootstrap-document', { timeoutMs: REQUEST_TIMEOUT_MS });
  onStage('Downloading Structure…');
  const structureModel = await client.get('/api/v1/structure', 'bootstrap-structure', { timeoutMs: REQUEST_TIMEOUT_MS });
  return { documentModel, structureModel };
}

export async function loadSessionModels({
  client,
  onStage,
  pause = (milliseconds) => new Promise((resolve) => setTimeout(resolve, milliseconds)),
}) {
  const status = await pollSession({ client, onStage, pause });
  if (status.state === 'empty') {
    return { empty: true };
  }
  return { empty: false, ...(await fetchSessionModels({ client, onStage })) };
}
