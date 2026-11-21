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

export const STATISTICS_CHANNEL = 'statistics-summary';
export const STATISTICS_POLL_MS = 250;

const KINDS = new Set(['File', 'Module', 'Script', 'Class', 'Interface', 'Namespace']);
const TOTAL_PREFIXES = new Map([
  ['Module', 'Module method '],
  ['File', 'File method '],
  ['Class', 'Class method '],
  ['Interface', 'Interface method '],
  ['Namespace', 'Namespace method '],
]);
const PHASES = new Set(['NotStarted', 'Building', 'Complete', 'Partial', 'Cancelled', 'Failed']);
const DECIMALS = [
  'methodCount', 'executableMethods', 'unassignedMethods', 'unassignedExecutables', 'scannedMethods',
  'decodedMethods', 'failedMethods', 'limitedMethods', 'knownCodeBytes', 'knownInstructionCount',
];
const CANONICAL_UNSIGNED = /^(?:0|[1-9][0-9]*)$/;
const CANONICAL_POSITIVE = /^[1-9][0-9]*$/;

function fail(field) {
  throw new Error(`Invalid statistics ${field}`);
}

export function statisticsPath(entityId, action = 'status') {
  if (typeof entityId !== 'string' || !CANONICAL_POSITIVE.test(entityId)) {
    fail('entity id');
  }
  if (!['status', 'start', 'cancel'].includes(action)) {
    fail('action');
  }
  const suffix = action === 'cancel' ? '/cancel' : '';
  return `/api/v1/statistics${suffix}?id=${entityId}`;
}

// Envelope and identity fields: shape, API version, document and entity ids,
// kind, phase, and the expected-value cross-checks.
function validateStatisticsIdentity(wire, expected) {
  if (wire === null || typeof wire !== 'object' || Array.isArray(wire)) {
    fail('response');
  }
  if (wire.apiVersion !== 1) {
    fail('API version');
  }
  if (typeof wire.documentId !== 'string' || wire.documentId.length === 0 || wire.documentId.length > 1024) {
    fail('documentId');
  }
  if (expected.documentId !== undefined && wire.documentId !== expected.documentId) {
    fail('documentId mismatch');
  }
  if (typeof wire.entityId !== 'string' || !CANONICAL_POSITIVE.test(wire.entityId) ||
      (expected.entityId !== undefined && wire.entityId !== expected.entityId)) {
    fail('entityId');
  }
  if (!KINDS.has(wire.kind) || (expected.kind !== undefined && wire.kind !== expected.kind)) {
    fail('kind');
  }
  if (!PHASES.has(wire.phase)) {
    fail('phase');
  }
}

// Counter payload: the decimal counters, the kind-conditional fileSize, the
// phase-conditional code totals, the optional active scope and the diagnostic.
function validateStatisticsPayload(wire) {
  for (const name of DECIMALS) {
    if (typeof wire[name] !== 'string' || !CANONICAL_UNSIGNED.test(wire[name])) {
      fail(name);
    }
  }
  if (wire.kind === 'File') {
    if (typeof wire.fileSize !== 'string' || !CANONICAL_UNSIGNED.test(wire.fileSize)) {
      fail('fileSize');
    }
  } else if (wire.fileSize !== null) {
    fail('fileSize');
  }
  if (wire.phase === 'Complete') {
    if (typeof wire.codeBytes !== 'string' || !CANONICAL_UNSIGNED.test(wire.codeBytes)) {
      fail('codeBytes');
    }
    if (typeof wire.instructionCount !== 'string' || !CANONICAL_UNSIGNED.test(wire.instructionCount)) {
      fail('instructionCount');
    }
  } else if (wire.codeBytes !== null || wire.instructionCount !== null) {
    fail('codeBytes/instructionCount');
  }
  if (wire.activeScopeId !== null &&
      (typeof wire.activeScopeId !== 'string' || !CANONICAL_POSITIVE.test(wire.activeScopeId))) {
    fail('activeScopeId');
  }
  if (typeof wire.diagnostic !== 'string') {
    fail('diagnostic');
  }
}

export function validateStatisticsResponse(wire, expected = {}) {
  validateStatisticsIdentity(wire, expected);
  validateStatisticsPayload(wire);
  return Object.freeze({ ...wire });
}

export function statisticsPresentation(snapshot, diagnosticsCount = null) {
  const prefix = TOTAL_PREFIXES.get(snapshot.kind) ?? 'Script method ';
  let totals;
  if (snapshot.phase === 'Complete') {
    totals = `${prefix}code bytes: ${snapshot.codeBytes} · ${prefix}instructions: ${snapshot.instructionCount}`;
  } else if (snapshot.phase === 'NotStarted') {
    totals = `${prefix}code bytes and instructions: unknown until calculated`;
  } else {
    totals = `Known ${prefix.toLowerCase()}code bytes (partial): ${snapshot.knownCodeBytes} · ` +
      `Known ${prefix.toLowerCase()}instructions (partial): ${snapshot.knownInstructionCount}`;
  }
  const details = [
    `Methods: ${snapshot.methodCount}`,
    `Executable methods: ${snapshot.executableMethods}`,
    `Progress: ${snapshot.scannedMethods}/${snapshot.executableMethods} executable methods scanned`,
    `Decoded: ${snapshot.decodedMethods}`,
  ];
  // Failed/Limited stay quiet while zero (the common case) and become an
  // inspector detail line only when a scan was not fully successful — that
  // line sits next to the totals it explains (Limited > 0 means totals are
  // a lower bound).
  const anomalies = [];
  if (Number(snapshot.failedMethods) > 0) {
    anomalies.push(`Failed: ${snapshot.failedMethods}`);
  }
  if (Number(snapshot.limitedMethods) > 0) {
    anomalies.push(`Limited: ${snapshot.limitedMethods}`);
  }
  if (anomalies.length > 0) {
    details.push(anomalies.join(' · '));
  }
  if (snapshot.kind === 'File') {
    details.push(`Physical file bytes: ${snapshot.fileSize}`);
    details.push(`Unassigned methods: ${snapshot.unassignedMethods} · Unassigned executables: ${snapshot.unassignedExecutables}`);
  }
  // Structure-extraction diagnostics survive the statistics overlay: they are
  // the honesty channel for reconstructed scopes (a class total can look small
  // when reconstruction lost methods).
  if (diagnosticsCount !== null) {
    details.push(`Diagnostics: ${diagnosticsCount}`);
  }
  return Object.freeze({ totals, details });
}

function makeElement(owner, tag, className, text) {
  const value = owner.createElement(tag);
  value.className = className;
  if (text !== undefined) {
    value.textContent = text;
  }
  return value;
}

export class StatisticsView {
  #client;
  #elements;
  #generation = 0;
  #requestEpoch = 0;
  #selected = null;
  #diagnosticsCount = null;
  #diagnosticsItems = null;
  #documentId = null;
  #timer = null;
  #setTimer;
  #clearTimer;

  constructor({
    client, elements,
    setTimer = (callback, delay) => globalThis.setTimeout(callback, delay),
    clearTimer = (timer) => globalThis.clearTimeout(timer),
  }) {
    this.#client = client;
    this.#elements = elements;
    this.#setTimer = setTimer;
    this.#clearTimer = clearTimer;
  }

  clear() {
    const hadSelection = this.#selected !== null;
    this.#generation += 1;
    this.#requestEpoch += 1;
    this.#selected = null;
    this.#diagnosticsCount = null;
    this.#diagnosticsItems = null;
    // The next response of a fresh selection re-establishes the memo; this
    // also forgets a previous document after an in-page switch.
    this.#documentId = null;
    this.#elements.root?.classList.remove('statistics-scope');
    if (this.#timer !== null) {
      this.#clearTimer(this.#timer);
    }
    this.#timer = null;
    if (hadSelection) {
      this.#client.cancel?.(STATISTICS_CHANNEL);
    }
  }

  async select(node) {
    this.clear();
    if (node === null || !KINDS.has(node.kind)) {
      return false;
    }
    statisticsPath(node.id);
    this.#selected = { id: node.id, kind: node.kind };
    this.#diagnosticsCount = Array.isArray(node.diagnostics) ? node.diagnostics.length : null;
    // The structure wire already carries full diagnostics ({severity, message,
    // range} per item, web_api.cpp serializes them); the count line only
    // needs .length, the toggle below needs the items themselves.
    this.#diagnosticsItems = Array.isArray(node.diagnostics) && node.diagnostics.length > 0
      ? node.diagnostics : null;
    this.#elements.root?.classList.add('statistics-scope');
    const generation = this.#generation;
    await this.#request('status', generation);
    return true;
  }

  async #request(action, generation, startError = null) {
    if (generation !== this.#generation) {
      return;
    }
    const selected = this.#selected;
    if (selected === null) {
      return;
    }
    const requestEpoch = ++this.#requestEpoch;
    if (this.#timer !== null) {
      this.#clearTimer(this.#timer);
    }
    this.#timer = null;
    try {
      const response = action === 'status'
        ? await this.#client.get(statisticsPath(selected.id), STATISTICS_CHANNEL, { timeoutMs: 30000 })
        : await this.#client.post(statisticsPath(selected.id, action), {}, STATISTICS_CHANNEL, { timeoutMs: 30000 });
      if (generation !== this.#generation || requestEpoch !== this.#requestEpoch ||
          this.#selected?.id !== selected.id) {
        return;
      }
      const snapshot = validateStatisticsResponse(response, {
        entityId: selected.id, kind: selected.kind, ...(this.#documentId === null ? {} : { documentId: this.#documentId }),
      });
      if (this.#documentId === null) {
        this.#documentId = snapshot.documentId;
      }
      // Reconcile a rejected/timed-out start once. Only actual contention should
      // resume waiting; an idle failed start must not become an automatic retry loop.
      if (startError !== null && snapshot.phase === 'NotStarted' && snapshot.activeScopeId === null) {
        throw startError;
      }
      this.#render(snapshot, generation);
      if (action === 'status' && snapshot.phase === 'NotStarted' && snapshot.activeScopeId === null) {
        void this.#request('start', generation);
      }
    } catch (error) {
      if (generation !== this.#generation || requestEpoch !== this.#requestEpoch || /stale/i.test(error.message)) {
        return;
      }
      if (action === 'start') {
        void this.#request('status', generation, error);
        return;
      }
      const owner = this.#elements.inspector.ownerDocument;
      const message = makeElement(owner, 'div', 'statistics-error', error instanceof Error ? error.message : String(error));
      const retry = makeElement(owner, 'button', 'statistics-action', 'Retry');
      retry.type = 'button';
      // A failed request may have reached the backend. Recheck before starting more work.
      retry.addEventListener('click', () => this.#request('status', generation));
      this.#elements.inspector.replaceChildren(message, retry);
    }
  }

  // Inspector detail line renderer. The "Diagnostics: N" line upgrades to an
  // expandable toggle when the selection carries the diagnostics items (the
  // structure wire already serializes {severity, message, range} per item):
  // a bare count answers nothing, the messages are the actual content.
  #renderDetail(owner, detail) {
    const items = this.#diagnosticsItems;
    if (items === null || !detail.startsWith('Diagnostics: ')) {
      return makeElement(owner, 'div', 'statistics-detail', detail);
    }
    const wrap = makeElement(owner, 'div', 'statistics-diagnostics');
    const toggle = makeElement(owner, 'button', 'statistics-diagnostics-toggle', `Diagnostics: ${items.length} ▸`);
    toggle.type = 'button';
    const list = makeElement(owner, 'div', 'statistics-diagnostics-list');
    const renderList = (expanded) => {
      if (!expanded) {
        list.replaceChildren();
        toggle.textContent = `Diagnostics: ${items.length} ▸`;
        return;
      }
      list.replaceChildren(...items.map((item) => {
        const row = makeElement(owner, 'div', 'statistics-diagnostic-item');
        const severity = item === null || typeof item !== 'object' ? '' : String(item.severity ?? '');
        const message = item === null || typeof item !== 'object' ? String(item) : String(item.message ?? '');
        row.append(makeElement(owner, 'span', `diagnostic-severity severity-${severity.toLowerCase()}`, severity),
          makeElement(owner, 'span', 'diagnostic-message', message));
        return row;
      }));
      toggle.textContent = `Diagnostics: ${items.length} ▾`;
    };
    toggle.addEventListener('click', () => renderList(list.children.length === 0));
    wrap.append(toggle, list);
    return wrap;
  }

  #render(snapshot, generation) {
    if (this.#timer !== null) {
      this.#clearTimer(this.#timer);
    }
    this.#timer = null;
    const owner = this.#elements.metrics.ownerDocument;
    const presentation = statisticsPresentation(snapshot, this.#diagnosticsCount);
    const isOwnActive = snapshot.activeScopeId === snapshot.entityId;
    const blocked = snapshot.activeScopeId !== null && !isOwnActive;
    const cached = snapshot.phase === 'Complete' || snapshot.phase === 'Partial';
    const phase = snapshot.phase === 'NotStarted'
      ? (snapshot.activeScopeId === null ? 'Starting…' : 'Waiting…') : snapshot.phase;
    this.#elements.metrics.replaceChildren(...presentation.details.slice(0, 4).map((detail) => {
      const separator = detail.indexOf(':');
      const card = makeElement(owner, 'div', 'metric');
      card.append(makeElement(owner, 'div', 'metric-label', detail.slice(0, separator)),
        makeElement(owner, 'div', 'metric-value', detail.slice(separator + 1).trim()));
      return card;
    }));
    const children = [
      makeElement(owner, 'div', 'statistics-phase', `Statistics: ${phase}`),
      makeElement(owner, 'div', 'statistics-help',
        'METHOD totals include generated initializers. Scope code bytes count unique METHOD code only; they exclude metadata and are not physical size. Class and namespace totals follow the reconstructed subtree.'),
      makeElement(owner, 'div', 'statistics-totals', presentation.totals),
      ...presentation.details.slice(4).map((detail) => this.#renderDetail(owner, detail)),
    ];
    if (snapshot.diagnostic) {
      children.push(makeElement(owner, 'div', 'statistics-diagnostic', snapshot.diagnostic));
    }
    if (blocked && !cached) {
      children.push(makeElement(owner, 'div', 'statistics-notice',
        `Waiting for statistics scope ${snapshot.activeScopeId} to finish.`));
    }
    if (isOwnActive && snapshot.phase === 'Building') {
      const cancel = makeElement(owner, 'button', 'statistics-action', 'Cancel');
      cancel.type = 'button';
      cancel.addEventListener('click', () => this.#request('cancel', generation));
      children.push(cancel);
    } else if (snapshot.phase === 'Failed' || snapshot.phase === 'Cancelled') {
      const retry = makeElement(owner, 'button', 'statistics-action', 'Retry');
      retry.type = 'button';
      retry.disabled = snapshot.activeScopeId !== null;
      retry.addEventListener('click', () => this.#request('start', generation));
      children.push(retry);
    }
    this.#elements.inspector.replaceChildren(...children);
    if (snapshot.activeScopeId !== null && !cached) {
      this.#timer = this.#setTimer(() => {
        this.#timer = null;
        this.#request('status', generation);
      }, STATISTICS_POLL_MS);
    }
  }
}
