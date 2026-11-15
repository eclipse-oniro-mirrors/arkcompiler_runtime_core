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

import {
  MAX_METADATA_OBJECT_PAGES,
  METADATA_PAGE_LIMIT,
  METADATA_POLL_MS,
  canonicalMetadataHex,
  mergeMetadataObjectPage,
  metadataKey,
  objectPath,
  objectsPath,
  parseMetadataObjectDetail,
  parseMetadataObjectPage,
  parseMetadataReferencePage,
  parseMetadataStatus,
  referencesPath,
} from './metadata-view-model.js';

const START_PATH = '/api/v1/metadata/index';
const STATUS_PATH = '/api/v1/metadata/status';
const MAX_REFERENCE_BACK_CURSORS = 32;
const MAX_RENDERED_OBJECT_ROWS = METADATA_PAGE_LIMIT * MAX_METADATA_OBJECT_PAGES;

function noOp() {}

function errorText(error) {
  return error instanceof Error ? error.message : String(error);
}

function isSuperseded(error) {
  return /stale bytecode explorer request|abort/i.test(errorText(error));
}

function isRevisionStale(error) {
  return /cursor is stale|stale cursor|revision[^\n]*stale/i.test(errorText(error));
}

function isObjectNotFound(error) {
  return /metadata object does not exist|metadata object[^\n]*not found/i.test(errorText(error));
}

function frozenStructurePayload(entityId) {
  if (typeof entityId !== 'string' || !/^(?:0|[1-9][0-9]*)$/.test(entityId)) {
    throw new Error('Invalid Structure navigation entityId');
  }
  return Object.freeze({ entityId });
}

function frozenRange(range, label) {
  if (range === null || typeof range !== 'object' || typeof range.offset !== 'bigint' ||
      range.offset < 0n || !Number.isSafeInteger(range.size) || range.size <= 0 ||
      range.offset + BigInt(range.size) > 0x1_0000_0000_0000_0000n) {
    throw new Error(`Invalid ${label} range`);
  }
  return Object.freeze({ offset: range.offset, size: range.size });
}

function frozenDisassemblyPayload(entityId, methodOffset, pc = null, sourceRange = null) {
  const structure = frozenStructurePayload(entityId);
  if (typeof methodOffset !== 'bigint' || methodOffset < 0n || methodOffset > 0xffff_ffff_ffff_ffffn) {
    throw new Error('Invalid Disassembly navigation methodOffset');
  }
  if (pc !== null && (!Number.isSafeInteger(pc) || pc < 0 || pc > 0xffff_ffff)) {
    throw new Error('Invalid Disassembly navigation pc');
  }
  return Object.freeze({
    entityId: structure.entityId,
    methodOffset,
    pc,
    sourceRange: sourceRange === null ? null : frozenRange(sourceRange, 'Disassembly navigation source'),
  });
}

function frozenHexPayload({ kind, offset, range, entityIds, entityIdsTruncated }) {
  metadataKey(kind, offset);
  if (typeof entityIdsTruncated !== 'boolean') {
    throw new Error('Invalid Hex navigation association state');
  }
  const ids = entityIds.map((entityId) => frozenStructurePayload(entityId).entityId);
  return Object.freeze({
    kind,
    offset: BigInt(canonicalMetadataHex(offset)),
    range: frozenRange(range, 'Hex navigation'),
    entityIds: Object.freeze(ids),
    entityIdsTruncated,
    source: 'metadata',
  });
}

export class MetadataView {
  #client;
  #elements;
  #onStatus;
  #onError;
  #onClearError;
  #onStructure;
  #onDisassembly;
  #onHex;
  #schedule;
  #cancelSchedule;
  #visible = false;
  #presented = true;
  #started = false;
  #lifecycleGeneration = 0;
  #statusGeneration = 0;
  #objectsGeneration = 0;
  #detailGeneration = 0;
  #referencesGeneration = 0;
  #status = null;
  #documentId = null;
  #pollTimer = null;
  #kind = 'All';
  #query = '';
  #objectState = Object.freeze({ revision: null, pages: Object.freeze([]), droppedBefore: 0, selectedKey: null });
  #objectsPending = false;
  #selected = null;
  #selectionDetached = false;
  #detail = null;
  #referencesPage = null;
  #referenceCursor = null;
  #referenceBackStack = [];
  #referencesPending = false;
  #recoveringRevision = false;

  constructor({
    client,
    elements,
    onStatus = noOp,
    onError = noOp,
    onClearError = noOp,
    onStructure = noOp,
    onDisassembly = noOp,
    onHex = noOp,
    schedule = (callback, delay) => globalThis.setTimeout(callback, delay),
    cancelSchedule = (timer) => globalThis.clearTimeout(timer),
  }) {
    this.#client = client;
    this.#elements = elements;
    this.#onStatus = onStatus;
    this.#onError = onError;
    this.#onClearError = onClearError;
    this.#onStructure = onStructure;
    this.#onDisassembly = onDisassembly;
    this.#onHex = onHex;
    this.#schedule = schedule;
    this.#cancelSchedule = cancelSchedule;
    this.#kind = elements.kind.value || 'All';

    elements.kind.addEventListener('change', () => this.#changeFilter(elements.kind.value, this.#query));
    elements.searchForm.addEventListener('submit', (event) => {
      event.preventDefault();
      this.#changeFilter(this.#kind, elements.search.value);
    });
    elements.loadMore.addEventListener('click', () => this.#loadMore());
    elements.referencesNext.addEventListener('click', () => this.#nextReferences());
    elements.referencesBack.addEventListener('click', () => this.#backReferences());
    this.#updateControls();
  }

  show() {
    this.mount();
    this.activate();
  }

  mount() {
    if (this.#visible) {
      return;
    }
    this.#visible = true;
    ++this.#lifecycleGeneration;
    this.#elements.root.classList.remove('hidden');
    this.#renderObjects();
    this.#renderDetail();
    this.#renderReferences();
    this.#fetchStatus();
    if (this.#objectState.pages.length === 0 && !this.#objectsPending) {
      this.#fetchObjects(null, false, null);
    }
    if (this.#selected !== null) {
      if (this.#detail === null) {
        this.#fetchDetail(this.#selected);
      }
      if (this.#referencesPage === null && !this.#referencesPending) {
        this.#fetchReferences(this.#selected, null, []);
      }
    }
  }

  activate() {
    if (!this.#visible) {
      this.mount();
    }
    if (this.#started) {
      return;
    }
    this.#started = true;
    this.#startIndex(this.#lifecycleGeneration);
  }

  resize(panelWidth) {
    if (!Number.isFinite(panelWidth) || panelWidth < 0) {
      throw new TypeError('Metadata panel width must be a non-negative finite number');
    }
    const compact = panelWidth < 620;
    this.#elements.root.classList.toggle('metadata-compact', compact);
    this.#elements.root.dataset.compact = compact ? 'true' : 'false';
  }

  setPresented(presented) {
    const next = presented === true;
    if (next === this.#presented) {
      return;
    }
    this.#presented = next;
    this.#elements.root.dataset.presented = next ? 'true' : 'false';
    if (!next) {
      this.#cancelPoll();
    } else if (this.#visible && this.#status !== null &&
               (this.#status.state === 'Building' || (this.#started && this.#status.state === 'NotStarted'))) {
      this.#fetchStatus();
    }
  }

  hide() {
    if (!this.#visible) {
      return;
    }
    this.#visible = false;
    ++this.#lifecycleGeneration;
    ++this.#statusGeneration;
    ++this.#objectsGeneration;
    ++this.#detailGeneration;
    ++this.#referencesGeneration;
    this.#objectsPending = false;
    this.#referencesPending = false;
    this.#recoveringRevision = false;
    this.#cancelPoll();
    this.#elements.root.classList.add('hidden');
    this.#updateControls();
  }

  // Rebinds the view to a freshly published document: forgets the previous
  // document's index, revisions, selection and memoized document id. Called
  // before every presentation, including the first one.
  reset() {
    this.#cancelPoll();
    ++this.#lifecycleGeneration;
    ++this.#statusGeneration;
    ++this.#objectsGeneration;
    ++this.#detailGeneration;
    ++this.#referencesGeneration;
    this.#objectsPending = false;
    this.#referencesPending = false;
    this.#recoveringRevision = false;
    this.#started = false;
    this.#status = null;
    this.#documentId = null;
    this.#kind = 'All';
    this.#query = '';
    this.#elements.search.value = '';
    this.#elements.kind.value = 'All';
    this.#objectState = Object.freeze({ revision: null, pages: Object.freeze([]), droppedBefore: 0, selectedKey: null });
    this.#selected = null;
    this.#selectionDetached = false;
    this.#detail = null;
    this.#referencesPage = null;
    this.#referenceCursor = null;
    this.#referenceBackStack = [];
    this.#elements.notice.textContent = '';
    this.#elements.notice.classList.add('hidden');
    this.#renderObjects();
    this.#renderDetail();
    this.#renderReferences();
    this.#updateSummary();
    this.#updateControls();
    if (this.#visible) {
      this.#fetchStatus();
      this.#fetchObjects(null, false, null);
    }
  }

  revealObject({ kind, offset }) {
    const key = metadataKey(kind, offset);
    this.activate();
    const retained = this.#objectState.pages
      .flatMap((page) => page.items)
      .find((object) => object.key === key);
    const selection = retained ?? Object.freeze({
      key,
      kind,
      offset: BigInt(canonicalMetadataHex(offset)),
    });
    if (this.#visible) {
      this.#selectObject(selection, retained === undefined);
    }
  }

  async #startIndex(lifecycleGeneration) {
    try {
      const wire = await this.#client.post(START_PATH, {}, 'metadata-start');
      if (!this.#visible || lifecycleGeneration !== this.#lifecycleGeneration) {
        return;
      }
      const status = parseMetadataStatus(wire);
      this.#verifyDocument(status.documentId);
      this.#status = status;
      this.#renderStatusNotice();
      this.#onClearError();
      if (status.state === 'Building' || status.state === 'NotStarted') {
        this.#schedulePoll();
      }
    } catch (error) {
      if (this.#visible && lifecycleGeneration === this.#lifecycleGeneration && !isSuperseded(error)) {
        this.#onError(error);
      }
    }
  }

  #cancelPoll() {
    if (this.#pollTimer === null) {
      return;
    }
    this.#cancelSchedule(this.#pollTimer);
    this.#pollTimer = null;
  }

  #schedulePoll() {
    if (this.#pollTimer !== null || !this.#visible || !this.#presented) {
      return;
    }
    this.#pollTimer = this.#schedule(() => {
      this.#pollTimer = null;
      if (this.#visible && this.#presented) {
        this.#fetchStatus();
      }
    }, METADATA_POLL_MS);
  }

  async #fetchStatus() {
    const generation = ++this.#statusGeneration;
    this.#cancelPoll();
    try {
      const wire = await this.#client.get(STATUS_PATH, 'metadata-status');
      if (!this.#visible || generation !== this.#statusGeneration) {
        return;
      }
      const status = parseMetadataStatus(wire, this.#status);
      this.#verifyDocument(status.documentId);
      if (this.#objectState.revision !== null && status.revision < this.#objectState.revision) {
        this.#schedulePoll();
        return;
      }
      this.#status = status;
      this.#onClearError();
      this.#onStatus(this.#statusText(status));
      this.#renderStatusNotice();
      if (status.state !== 'Failed' && this.#objectState.revision !== null &&
          status.revision !== this.#objectState.revision) {
        this.#recoverRevision();
      }
      if (status.state === 'Building' || (this.#started && status.state === 'NotStarted')) {
        this.#schedulePoll();
      }
    } catch (error) {
      if (this.#visible && generation === this.#statusGeneration && !isSuperseded(error)) {
        this.#schedulePoll();
        this.#onError(error);
      }
    }
  }

  #statusText(status) {
    return `Metadata ${status.state}: ${status.scannedMethods}/${status.totalMethods} methods, ` +
      `${status.objectCount} objects, ${status.referenceCount} references`;
  }

  #renderStatusNotice() {
    if (this.#status === null) {
      return;
    }
    const doneWithoutMethods = this.#status.totalMethods === 0n &&
      !['NotStarted', 'Building'].includes(this.#status.state);
    const percent = this.#status.totalMethods === 0n
      ? (doneWithoutMethods ? 100 : 0)
      : Number(this.#status.scannedMethods * 100n / this.#status.totalMethods);
    this.#elements.progressBar.style.width = `${percent}%`;
    this.#elements.progress.setAttribute('aria-valuenow', String(percent));
    this.#updateSummary();
    if (this.#status.state === 'Partial' || this.#status.state === 'Failed' ||
        this.#status.state === 'Cancelled') {
      const diagnostic = this.#status.diagnostics.length === 0 ? '' : `: ${this.#status.diagnostics.join('; ')}`;
      this.#elements.notice.textContent = `Metadata ${this.#status.state}${diagnostic}`;
      this.#elements.notice.classList.remove('hidden');
    } else {
      this.#elements.notice.textContent = '';
      this.#elements.notice.classList.add('hidden');
    }
  }

  #changeFilter(kind, query) {
    this.activate();
    try {
      objectsPath({ kind, query, cursor: null });
    } catch (error) {
      this.#onError(error);
      return;
    }
    this.#kind = kind;
    this.#query = query;
    this.#cancelPoll();
    ++this.#statusGeneration;
    ++this.#objectsGeneration;
    this.#invalidateSelection();
    this.#objectsPending = false;
    this.#recoveringRevision = false;
    this.#objectState = Object.freeze({ revision: null, pages: Object.freeze([]), droppedBefore: 0, selectedKey: null });
    this.#renderObjects();
    this.#fetchStatus();
    this.#fetchObjects(null, false, null);
  }

  #loadMore() {
    if (!this.#visible || this.#objectsPending) {
      return;
    }
    this.activate();
    const pages = this.#objectState.pages;
    const cursor = pages.length === 0 ? null : pages[pages.length - 1].nextCursor;
    if (cursor !== null) {
      this.#fetchObjects(cursor, true, null);
    }
  }

  // The sliding window dropped the oldest pages; re-fetch from the first page.
  // An existing selection survives as a detached (pinned) row when the fresh
  // first page does not contain it.
  #restartObjects() {
    if (!this.#visible || this.#objectsPending) {
      return;
    }
    this.activate();
    if (this.#selected !== null) {
      this.#selectionDetached = true;
    }
    this.#elements.list.scrollTop = 0;
    this.#fetchObjects(null, false, null);
  }

  #collapsedObjectsNotice() {
    const notice = this.#elements.root.ownerDocument.createElement('button');
    notice.type = 'button';
    notice.className = 'metadata-objects-collapsed';
    notice.textContent = `Back to start · earlier ${this.#objectState.droppedBefore} objects collapsed`;
    notice.addEventListener('click', () => this.#restartObjects());
    return notice;
  }

  async #fetchObjects(cursor, append, restoreKey) {
    let path;
    try {
      path = objectsPath({ kind: this.#kind, query: this.#query, cursor });
    } catch (error) {
      this.#onError(error);
      return;
    }
    const generation = ++this.#objectsGeneration;
    this.#objectsPending = true;
    this.#updateControls();
    try {
      const wire = await this.#client.get(path, 'metadata-objects');
      if (!this.#visible || generation !== this.#objectsGeneration) {
        return;
      }
      const page = parseMetadataObjectPage(wire);
      this.#verifyDocument(page.documentId);
      if (this.#status !== null && this.#status.state !== 'Failed' &&
          page.revision < this.#status.revision) {
        this.#recoveringRevision = false;
        this.#recoverRevision(restoreKey);
        return;
      }
      const previousRevision = this.#objectState.revision;
      const previousSelected = restoreKey ?? this.#objectState.selectedKey;
      const stateForMerge = Object.freeze({
        revision: this.#objectState.revision,
        pages: this.#objectState.pages,
        droppedBefore: this.#objectState.droppedBefore,
        selectedKey: previousSelected,
      });
      this.#objectState = mergeMetadataObjectPage(stateForMerge, page, append);
      this.#recoveringRevision = false;
      this.#onClearError();
      const selectionMissing = previousSelected !== null && this.#objectState.selectedKey === null;
      const retainDetachedSelection = selectionMissing && this.#selected?.key === previousSelected &&
        (this.#selectionDetached || restoreKey !== null);
      if (retainDetachedSelection) {
        this.#objectState = Object.freeze({ ...this.#objectState, selectedKey: previousSelected });
        this.#selectionDetached = true;
      } else if (selectionMissing) {
        this.#invalidateSelection();
      } else if (previousSelected !== null && this.#objectState.selectedKey === previousSelected) {
        this.#selectionDetached = false;
      }
      this.#renderObjects();

      const revisionChanged = previousRevision !== null && previousRevision !== page.revision;
      const statusRevisionMismatch = this.#status !== null && page.revision !== this.#status.revision;
      if (revisionChanged || statusRevisionMismatch) {
        ++this.#statusGeneration;
        this.#cancelPoll();
        this.#fetchStatus();
      }
      if (revisionChanged || restoreKey !== null) {
        const selectionKey = this.#objectState.selectedKey;
        const selected = page.items.find((object) => object.key === selectionKey) ?? null;
        if (selected !== null) {
          this.#selectObject(selected);
        } else if (retainDetachedSelection && this.#selected !== null) {
          this.#selectObject(this.#selected, true);
        }
      }
      this.#updateSummary();
      const shown = this.#objectState.pages.reduce((count, entry) => count + entry.items.length, 0);
      this.#onStatus(this.#objectState.droppedBefore > 0
        ? `Showing ${this.#objectState.droppedBefore + 1}–${this.#objectState.droppedBefore + shown} ` +
          `of ${page.total} Metadata objects`
        : `Showing ${shown} of ${page.total} Metadata objects`);
    } catch (error) {
      if (!this.#visible || generation !== this.#objectsGeneration || isSuperseded(error)) {
        return;
      }
      if (isRevisionStale(error)) {
        this.#recoveringRevision = false;
        this.#recoverRevision(restoreKey);
      } else {
        this.#recoveringRevision = false;
        this.#onError(error);
      }
    } finally {
      if (generation === this.#objectsGeneration) {
        this.#objectsPending = false;
        this.#updateControls();
      }
    }
  }

  #recoverRevision(explicitKey = null) {
    if (!this.#visible || this.#recoveringRevision) {
      return;
    }
    this.#recoveringRevision = true;
    const restoreKey = explicitKey ?? this.#selected?.key ?? this.#objectState.selectedKey;
    ++this.#objectsGeneration;
    this.#objectsPending = false;
    this.#invalidateSelection(false);
    if (restoreKey !== null) {
      this.#objectState = Object.freeze({ ...this.#objectState, selectedKey: restoreKey });
    }
    this.#fetchObjects(null, false, restoreKey);
  }

  #verifyDocument(documentId) {
    if (this.#documentId === null) {
      this.#documentId = documentId;
    } else if (documentId !== this.#documentId) {
      throw new Error('Invalid Metadata response documentId');
    }
  }

  #updateSummary() {
    const pages = this.#objectState.pages;
    const total = pages.length === 0 ? (this.#status?.objectCount ?? 0n) : pages[pages.length - 1].total;
    const state = this.#status?.state ?? 'NotStarted';
    this.#elements.summary.textContent = `Metadata ${state} · ${total} objects`;
  }

  #selectObject(object, detached = false) {
    if (!this.#visible) {
      return;
    }
    this.activate();
    const key = metadataKey(object.kind, object.offset);
    this.#selected = Object.freeze({ key, kind: object.kind, offset: object.offset });
    this.#selectionDetached = detached;
    this.#objectState = Object.freeze({ ...this.#objectState, selectedKey: key });
    this.#detail = null;
    this.#referencesPage = null;
    this.#referenceCursor = null;
    this.#referenceBackStack = [];
    ++this.#detailGeneration;
    ++this.#referencesGeneration;
    this.#referencesPending = false;
    this.#renderObjects();
    this.#renderDetail();
    this.#renderReferences();
    this.#fetchDetail(this.#selected);
    this.#fetchReferences(this.#selected, null, []);
  }

  #toggleObject(object, detached = false) {
    const key = metadataKey(object.kind, object.offset);
    if (this.#selected?.key === key) {
      this.#invalidateSelection();
      return;
    }
    this.#selectObject(object, detached);
  }

  #invalidateSelection(clearSelected = true) {
    ++this.#detailGeneration;
    ++this.#referencesGeneration;
    this.#referencesPending = false;
    this.#detail = null;
    this.#referencesPage = null;
    this.#referenceCursor = null;
    this.#referenceBackStack = [];
    if (clearSelected) {
      this.#selected = null;
      this.#selectionDetached = false;
      this.#objectState = Object.freeze({ ...this.#objectState, selectedKey: null });
      this.#renderObjects();
    }
    this.#renderDetail();
    this.#renderReferences();
  }

  async #fetchDetail(selected) {
    const generation = ++this.#detailGeneration;
    try {
      const wire = await this.#client.get(objectPath(selected), 'metadata-object');
      if (!this.#visible || generation !== this.#detailGeneration || this.#selected?.key !== selected.key) {
        return;
      }
      const detail = parseMetadataObjectDetail(wire);
      this.#verifyDocument(detail.documentId);
      if (detail.object.key !== selected.key) {
        throw new Error('Invalid Metadata object detail key');
      }
      this.#detail = detail.object;
      this.#onClearError();
      this.#renderDetail();
    } catch (error) {
      if (!this.#visible || generation !== this.#detailGeneration || isSuperseded(error)) {
        return;
      }
      if (isRevisionStale(error)) {
        this.#recoverRevision(selected.key);
      } else {
        if (isObjectNotFound(error)) {
          this.#invalidateSelection();
        }
        this.#onError(error);
      }
    }
  }

  #nextReferences() {
    if (!this.#visible || this.#selected === null || this.#referencesPending ||
        this.#referencesPage?.nextCursor === null) {
      return;
    }
    const stack = [...this.#referenceBackStack, this.#referenceCursor].slice(-MAX_REFERENCE_BACK_CURSORS);
    this.#fetchReferences(this.#selected, this.#referencesPage.nextCursor, stack);
  }

  #backReferences() {
    if (!this.#visible || this.#selected === null || this.#referencesPending ||
        this.#referenceBackStack.length === 0) {
      return;
    }
    const cursor = this.#referenceBackStack[this.#referenceBackStack.length - 1];
    const stack = this.#referenceBackStack.slice(0, -1);
    this.#fetchReferences(this.#selected, cursor, stack);
  }

  async #fetchReferences(selected, cursor, backStack) {
    const generation = ++this.#referencesGeneration;
    this.#referencesPending = true;
    this.#updateControls();
    try {
      const wire = await this.#client.get(referencesPath({ ...selected, cursor }), 'metadata-references');
      if (!this.#visible || generation !== this.#referencesGeneration || this.#selected?.key !== selected.key) {
        return;
      }
      const page = parseMetadataReferencePage(wire, selected);
      this.#verifyDocument(page.documentId);
      if (this.#objectState.revision !== null && page.revision !== this.#objectState.revision) {
        this.#recoverRevision(selected.key);
        return;
      }
      this.#referencesPage = page;
      this.#referenceCursor = cursor;
      this.#referenceBackStack = backStack.slice(-MAX_REFERENCE_BACK_CURSORS);
      this.#onClearError();
      this.#renderReferences();
    } catch (error) {
      if (!this.#visible || generation !== this.#referencesGeneration || isSuperseded(error)) {
        return;
      }
      if (isRevisionStale(error)) {
        this.#recoverRevision(selected.key);
      } else {
        if (isObjectNotFound(error)) {
          this.#invalidateSelection();
        }
        this.#onError(error);
      }
    } finally {
      if (generation === this.#referencesGeneration) {
        this.#referencesPending = false;
        this.#updateControls();
      }
    }
  }

  #renderObjects() {
    const document = this.#elements.root.ownerDocument;
    const fragment = document.createDocumentFragment();
    let remainingRows = MAX_RENDERED_OBJECT_ROWS;
    this.#elements.selection.classList.add('hidden');
    if (this.#objectState.droppedBefore > 0) {
      fragment.append(this.#collapsedObjectsNotice());
    }
    if (this.#selectionDetached && this.#selected !== null) {
      fragment.append(this.#objectRow(this.#selected, true));
      --remainingRows;
    }
    outer: for (const page of this.#objectState.pages) {
      for (const object of page.items) {
        if (remainingRows === 0) {
          break outer;
        }
        fragment.append(this.#objectRow(object, false));
        --remainingRows;
      }
    }
    this.#elements.objects.replaceChildren(fragment);
    this.#updateControls();
  }

  #objectRow(object, detached) {
    const document = this.#elements.root.ownerDocument;
    const key = metadataKey(object.kind, object.offset);
    const expanded = key === this.#objectState.selectedKey;
    const row = document.createElement('div');
    row.className = `metadata-object-row${detached ? ' detached' : ''}`;
    row.dataset.key = key;
    if (expanded) {
      row.classList.add('expanded');
    }
    const button = document.createElement('button');
    button.type = 'button';
    button.dataset.action = 'expand';
    button.dataset.key = key;
    button.setAttribute('aria-expanded', String(expanded));
    const preview = Object.hasOwn(object, 'preview') ? object.preview : 'Exact external target';
    button.textContent = `${object.kind} ${canonicalMetadataHex(object.offset)} ${preview}`;
    button.addEventListener('click', () => this.#toggleObject(object, detached));
    const references = document.createElement('span');
    references.textContent = Object.hasOwn(object, 'referenceCount')
      ? `${object.referenceCount} references`
      : 'Direct lookup';
    row.append(button, references);
    if (expanded) {
      this.#elements.selection.classList.remove('hidden');
      row.append(this.#elements.selection);
    }
    return row;
  }

  #renderDetail() {
    const container = this.#elements.detail;
    container.replaceChildren();
    if (this.#detail === null) {
      return;
    }
    const document = this.#elements.root.ownerDocument;
    const title = document.createElement('div');
    title.textContent = `${this.#detail.kind} ${canonicalMetadataHex(this.#detail.offset)} ` +
      `${this.#detail.value ?? this.#detail.preview}`;
    container.append(title);
    for (const entityId of this.#detail.entityIds) {
      container.append(this.#navigationButton('Structure', 'structure', () =>
        this.#onStructure(frozenStructurePayload(entityId)), entityId));
      if (this.#detail.kind === 'Method') {
        container.append(this.#navigationButton('Disassembly', 'disassembly', () =>
          this.#onDisassembly(frozenDisassemblyPayload(entityId, this.#detail.offset)), entityId));
      }
    }
    if (this.#detail.entityIds.length === 0) {
      container.append(this.#navigationButton('Structure', 'structure', null));
      if (this.#detail.kind === 'Method') {
        container.append(this.#navigationButton('Disassembly', 'disassembly', null));
      }
    }
    if (this.#detail.entityIdsTruncated) {
      const more = document.createElement('span');
      more.className = 'metadata-ids-more';
      const hidden = this.#detail.entityIdsTotal - this.#detail.entityIds.length;
      more.textContent = hidden > 0 ? `+${hidden} more` : 'more';
      container.append(more);
    }
    if (this.#detail.range !== null) {
      container.append(this.#navigationButton('Hex', 'hex', () =>
        this.#onHex(frozenHexPayload({
          kind: this.#detail.kind,
          offset: this.#detail.offset,
          range: this.#detail.range,
          entityIds: this.#detail.entityIds,
          entityIdsTruncated: this.#detail.entityIdsTruncated,
        }))));
    }
  }

  #renderReferences() {
    const container = this.#elements.references;
    container.replaceChildren();
    if (this.#referencesPage !== null) {
      const document = this.#elements.root.ownerDocument;
      for (const reference of this.#referencesPage.items) {
        const row = document.createElement('div');
        row.className = 'metadata-reference-row';
        const text = document.createElement('span');
        text.textContent = `${reference.kind} ${reference.sourceName} ${reference.mnemonic}`;
        row.append(text);
        for (const entityId of reference.sourceEntityIds) {
          row.append(this.#navigationButton('Structure', 'structure', () =>
            this.#onStructure(frozenStructurePayload(entityId)), entityId));
          if (reference.sourceMethodOffset !== null) {
            row.append(this.#navigationButton('Disassembly', 'disassembly', () =>
              this.#onDisassembly(frozenDisassemblyPayload(
                entityId, reference.sourceMethodOffset, reference.pc, reference.sourceRange,
              )), entityId));
          }
        }
        if (reference.sourceEntityIds.length === 0) {
          row.append(this.#navigationButton('Structure', 'structure', null));
          if (reference.sourceMethodOffset !== null) {
            row.append(this.#navigationButton('Disassembly', 'disassembly', null));
          }
        }
        if (reference.sourceEntityIdsTruncated) {
          const more = document.createElement('span');
          more.className = 'metadata-ids-more';
          const hidden = reference.sourceEntityIdsTotal - reference.sourceEntityIds.length;
          more.textContent = hidden > 0 ? `+${hidden} more` : 'more';
          row.append(more);
        }
        if (reference.sourceRange !== null) {
          row.append(this.#navigationButton('Hex', 'hex', () =>
            this.#onHex(frozenHexPayload({
              kind: this.#selected.kind,
              offset: this.#selected.offset,
              range: reference.sourceRange,
              entityIds: reference.sourceEntityIds,
              entityIdsTruncated: reference.sourceEntityIdsTruncated,
            }))));
        }
        container.append(row);
      }
    }
    this.#updateControls();
  }

  #navigationButton(label, navigation, callback, entityId = null) {
    const button = this.#elements.root.ownerDocument.createElement('button');
    button.type = 'button';
    button.dataset.navigation = navigation;
    if (entityId !== null) {
      button.dataset.entityId = entityId;
    }
    button.textContent = entityId === null ? label : `${label} #${entityId}`;
    button.disabled = callback === null;
    if (callback === null) {
      return button;
    }
    button.addEventListener('click', () => {
      try {
        callback();
      } catch (error) {
        this.#onError(error);
      }
    });
    return button;
  }

  #updateControls() {
    const pages = this.#objectState.pages;
    this.#elements.loadMore.disabled = !this.#visible || this.#objectsPending || pages.length === 0 ||
      pages[pages.length - 1].nextCursor === null;
    this.#elements.referencesBack.disabled = !this.#visible || this.#referencesPending ||
      this.#referenceBackStack.length === 0;
    this.#elements.referencesNext.disabled = !this.#visible || this.#referencesPending ||
      this.#referencesPage === null || this.#referencesPage.nextCursor === null;
  }
}
