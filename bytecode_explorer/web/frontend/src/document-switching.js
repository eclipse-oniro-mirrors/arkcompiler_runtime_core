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

// Models the "open another ABC" flows: the native file picker dialog and the
// recent-files list. Both end in the backend reloading the session document,
// so the frontend polls the picker state until it settles, then re-presents
// whatever document the session publishes afterwards.

const PICK_STATES = new Set(['idle', 'picking', 'selected', 'cancelled', 'failed']);

// This bounds a single HTTP exchange, never how long the user keeps the
// native dialog open.
const REQUEST_TIMEOUT_MS = 30000;
const POLL_INTERVAL_MS = 250;

// The status must be an object in a known picker state; `path` and `error`
// may be absent in a response, but a present value must be a string.
function invalidPickShape(wire, path, error) {
  return wire === null || typeof wire !== 'object' || Array.isArray(wire) ||
    typeof wire.state !== 'string' || !PICK_STATES.has(wire.state) ||
    (path !== null && typeof path !== 'string') ||
    (error !== null && typeof error !== 'string');
}

// The backend reports a path only for a selection and a diagnostic only for
// a failure; anything else is a contract drift worth refusing.
function invalidPickContract(wire, path, error) {
  return (wire.state === 'selected' && (path === null || path === '')) ||
    (wire.state !== 'selected' && path !== null) ||
    (wire.state !== 'failed' && error !== null);
}

export function validatePickStatus(wire) {
  // `path` and `error` may be absent in a response; only a non-string value is invalid.
  const path = wire?.path ?? null;
  const error = wire?.error ?? null;
  if (invalidPickShape(wire, path, error) || invalidPickContract(wire, path, error)) {
    throw new Error('Invalid file picker status response; reload the local explorer.');
  }
  return Object.freeze({ state: wire.state, path, error });
}

// Polls the picker status until the dialog run settles. "picking" is the only
// running state; every other state (including "idle", observed when the
// backend was restarted mid-dialog) is terminal for this flow.
export async function pollPickStatus({
  client,
  onWaiting = () => {},
  pause = (milliseconds) => new Promise((resolve) => setTimeout(resolve, milliseconds)),
}) {
  for (;;) {
    const status = validatePickStatus(
      await client.get('/api/v1/pick/status', 'pick-status', { timeoutMs: REQUEST_TIMEOUT_MS }));
    if (status.state !== 'picking') {
      return status;
    }
    onWaiting(status);
    await pause(POLL_INTERVAL_MS);
  }
}

export function validateRecentFiles(wire) {
  if (wire === null || typeof wire !== 'object' || Array.isArray(wire) || wire.apiVersion !== 1 ||
      !Array.isArray(wire.entries)) {
    throw new Error('Invalid recent files response; reload the local explorer.');
  }
  return Object.freeze(wire.entries.map((entry) => {
    if (entry === null || typeof entry !== 'object' || Array.isArray(entry) ||
        typeof entry.path !== 'string' || entry.path === '' ||
        typeof entry.fileName !== 'string' ||
        !Number.isSafeInteger(entry.openedAt) || entry.openedAt < 0) {
      throw new Error('Invalid recent files response; reload the local explorer.');
    }
    return Object.freeze({ path: entry.path, fileName: entry.fileName, openedAt: entry.openedAt });
  }));
}

export const SWITCH_REQUEST_TIMEOUT_MS = REQUEST_TIMEOUT_MS;
