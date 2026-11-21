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

export const METADATA_KINDS = Object.freeze(['All', 'String', 'Method', 'Literal Array']);
export const METADATA_PAGE_LIMIT = 100;
// The objects list is a sliding window over cursor pages: LOAD MORE appends and
// the oldest page slides out beyond this bound. The view surfaces the collapsed
// prefix with a "back to start" affordance instead of dropping it silently.
export const MAX_METADATA_OBJECT_PAGES = 5;
export const METADATA_POLL_MS = 500;

const WIRE_KINDS = new Set(METADATA_KINDS.slice(1));
const HEX = /^0x(?:0|[1-9a-f][0-9a-f]*)$/i;
const DECIMAL = /^(?:0|[1-9][0-9]*)$/;
const UINT64_MAX = 0xffff_ffff_ffff_ffffn;
const UINT32_MAX = 0xffff_ffff;
const MAX_INLINE_ITEMS = 256;
const MAX_PAGE_ENTITY_IDS = 4096;
const MAX_PREVIEW_BYTES = 512;
const MAX_DETAIL_BYTES = 64 * 1024;
const MAX_QUERY_BYTES = 256;
const MAX_CURSOR_BYTES = 512;

const STATUS_STATES = new Set(['NotStarted', 'Building', 'Ready', 'Partial', 'Failed', 'Cancelled']);
const TERMINAL_STATES = new Set(['Ready', 'Partial', 'Failed', 'Cancelled']);
const ORIGINS = new Set(['Index', 'Structure', 'Literal', 'Instruction']);
const REFERENCE_KINDS = new Set([
  'Record Name',
  'Method Name',
  'Field Name',
  'Annotation Name',
  'Annotation Element Name',
  'Annotation String Value',
  'Module Request',
  'Import Name',
  'Export Name',
  'Local Name',
  'Scope Name',
  'Literal String',
  'Literal Method',
  'Nested Literal Array',
  'Instruction Operand',
]);

const encoder = new TextEncoder();

function fail(label) {
  throw new Error(`Invalid ${label}`);
}

function requireObject(value, label) {
  if (value === null || typeof value !== 'object' || Array.isArray(value)) {
    fail(label);
  }
  return value;
}

function requireBoolean(value, label) {
  if (typeof value !== 'boolean') {
    fail(label);
  }
  return value;
}

function hasValidSurrogates(value) {
  for (let index = 0; index < value.length; ++index) {
    const code = value.charCodeAt(index);
    if (code >= 0xd800 && code <= 0xdbff) {
      const next = value.charCodeAt(++index);
      if (next < 0xdc00 || next > 0xdfff) {
        return false;
      }
    } else if (code >= 0xdc00 && code <= 0xdfff) {
      return false;
    }
  }
  return true;
}

function requireString(value, label, maximumBytes, allowEmpty = true) {
  if (typeof value !== 'string' || (!allowEmpty && value.length === 0) ||
      !hasValidSurrogates(value) || encoder.encode(value).byteLength > maximumBytes) {
    throw new Error(`Invalid ${label}; maximum is ${maximumBytes} UTF-8 bytes`);
  }
  return value;
}

function requireEnum(value, values, label) {
  if (typeof value !== 'string' || !values.has(value)) {
    fail(label);
  }
  return value;
}

function parseWireInteger(value, label, maximum = UINT64_MAX) {
  let result;
  if (typeof value === 'string' && DECIMAL.test(value)) {
    result = BigInt(value);
  } else if (Number.isSafeInteger(value) && value >= 0) {
    result = BigInt(value);
  } else {
    fail(label);
  }
  if (result > maximum) {
    fail(label);
  }
  return result;
}

function parseWireNumber(value, label, maximum = Number.MAX_SAFE_INTEGER) {
  if (!Number.isSafeInteger(value) || value < 0 || value > maximum) {
    fail(label);
  }
  return value;
}

function parseEntityId(value, label) {
  if (typeof value !== 'string' || !DECIMAL.test(value) || BigInt(value) > UINT64_MAX) {
    fail(label);
  }
  return value;
}

function requireArray(value, label, maximum) {
  if (!Array.isArray(value)) {
    fail(label);
  }
  if (value.length > maximum) {
    throw new Error(`Invalid ${label}; maximum is ${maximum}`);
  }
  return value;
}

function freezeArray(values) {
  return Object.freeze(values);
}

function parseStringArray(value, label, maximum, maximumBytes, allowEmpty = true) {
  const parsed = requireArray(value, label, maximum)
    .map((entry, index) => requireString(entry, `${label}[${index}]`, maximumBytes, allowEmpty));
  return freezeArray(parsed);
}

function parseEntityIds(value, label) {
  const parsed = requireArray(value, label, MAX_INLINE_ITEMS)
    .map((entry, index) => parseEntityId(entry, `${label}[${index}]`));
  return freezeArray(parsed);
}

function parseRange(value, label) {
  const wire = requireObject(value, label);
  const offset = parseHex(wire.offset, `${label} offset`);
  const size = parseWireNumber(wire.size, `${label} size`);
  if (size === 0 || offset + BigInt(size) > UINT64_MAX + 1n) {
    fail(label);
  }
  return Object.freeze({ offset, size });
}

function parseNullable(value, label, parser) {
  if (value === null) {
    return null;
  }
  if (value === undefined) {
    fail(label);
  }
  return parser(value, label);
}

function parseEnvelope(wire, label) {
  const envelope = requireObject(wire, label);
  if (envelope.apiVersion !== 1) {
    fail(`${label} apiVersion`);
  }
  const documentId = requireString(envelope.documentId, `${label} documentId`, 1024, false);
  return { envelope, documentId };
}

function parseCursor(value, label) {
  return parseNullable(value, label, (cursor) => {
    const parsed = requireString(cursor, label, MAX_CURSOR_BYTES, false);
    if (parsed.includes('\0')) {
      fail(label);
    }
    return parsed;
  });
}

function normalizeOffset(value, label) {
  if (typeof value === 'bigint') {
    if (value < 0n || value > UINT64_MAX) {
      fail(label);
    }
    return value;
  }
  return parseHex(value, label);
}

function requireWireKind(value, label = 'Metadata kind') {
  return requireEnum(value, WIRE_KINDS, label);
}

function requireSelectorKind(value) {
  if (typeof value !== 'string' || !METADATA_KINDS.includes(value)) {
    fail('Metadata kind');
  }
  return value;
}

function encodedParams(params) {
  return params.toString().replaceAll('+', '%20');
}

export function parseHex(value, label) {
  if (typeof value !== 'string' || !HEX.test(value)) {
    fail(label);
  }
  const parsed = BigInt(value);
  if (parsed > UINT64_MAX) {
    fail(label);
  }
  return parsed;
}

export function canonicalMetadataHex(value) {
  return `0x${normalizeOffset(value, 'Metadata offset').toString(16)}`;
}

export function metadataKey(kind, offset) {
  requireWireKind(kind);
  return `${kind}@${canonicalMetadataHex(offset)}`;
}

// A new status must follow the previous one: terminal states freeze, Building
// never regresses to NotStarted, revisions never decrease and progress never
// rewinds.
function requireStatusTransition(previous, current, documentId) {
  requireObject(previous, 'previous Metadata status');
  if (previous.documentId !== documentId) {
    fail('Metadata status document transition');
  }
  if (TERMINAL_STATES.has(previous.state) &&
      (current.state !== previous.state || current.revision !== previous.revision ||
       current.scannedMethods !== previous.scannedMethods ||
       current.totalMethods !== previous.totalMethods ||
       current.objectCount !== previous.objectCount ||
       current.referenceCount !== previous.referenceCount ||
       current.diagnosticsTruncated !== previous.diagnosticsTruncated ||
       current.diagnostics.length !== previous.diagnostics.length ||
       current.diagnostics.some((diagnostic, index) => diagnostic !== previous.diagnostics[index]))) {
    fail('Metadata status transition');
  }
  if (previous.state === 'Building' && current.state === 'NotStarted') {
    fail('Metadata status transition');
  }
  if (current.revision < previous.revision) {
    fail('Metadata status revision transition');
  }
  if (current.scannedMethods < previous.scannedMethods || current.objectCount < previous.objectCount ||
      current.referenceCount < previous.referenceCount) {
    fail('Metadata status progress transition');
  }
}

export function parseMetadataStatus(wire, previous = null) {
  const { envelope, documentId } = parseEnvelope(wire, 'Metadata status');
  const state = requireEnum(envelope.state, STATUS_STATES, 'Metadata status state');
  const revision = parseWireInteger(envelope.revision, 'Metadata status revision');
  const scannedMethods = parseWireInteger(envelope.scannedMethods, 'Metadata scannedMethods');
  const totalMethods = parseWireInteger(envelope.totalMethods, 'Metadata totalMethods');
  const objectCount = parseWireInteger(envelope.objectCount, 'Metadata objectCount');
  const referenceCount = parseWireInteger(envelope.referenceCount, 'Metadata referenceCount');
  if (scannedMethods > totalMethods) {
    fail('Metadata status progress');
  }
  const diagnostics = parseStringArray(
    envelope.diagnostics, 'Metadata status diagnostics', MAX_INLINE_ITEMS, MAX_PREVIEW_BYTES,
  );
  const diagnosticsTruncated = requireBoolean(
    envelope.diagnosticsTruncated, 'Metadata status diagnosticsTruncated',
  );

  if (previous !== null) {
    requireStatusTransition(previous, {
      state, revision, scannedMethods, totalMethods, objectCount, referenceCount,
      diagnostics, diagnosticsTruncated,
    }, documentId);
  }

  return Object.freeze({
    documentId,
    state,
    revision,
    scannedMethods,
    totalMethods,
    objectCount,
    referenceCount,
    diagnostics,
    diagnosticsTruncated,
  });
}

// Validates the identity part of a metadata object row: key, offsets, preview,
// origins and the per-row entity id caps.
function parseObjectIdentity(object) {
  const key = requireObject(object.key, 'Metadata object key');
  const kind = requireWireKind(object.kind);
  if (requireWireKind(key.kind, 'Metadata object key kind') !== kind) {
    fail('Metadata object key');
  }
  const offset = parseHex(key.offset, 'Metadata object offset');
  const range = parseNullable(object.range, 'Metadata object range', parseRange);
  const preview = requireString(object.preview, 'Metadata object preview', MAX_PREVIEW_BYTES);
  const truncated = requireBoolean(object.truncated, 'Metadata object truncated');
  const origins = freezeArray(requireArray(object.origins, 'Metadata object origins', ORIGINS.size)
    .map((origin) => requireEnum(origin, ORIGINS, 'Metadata object origin')));
  if (new Set(origins).size !== origins.length) {
    fail('Metadata object origins');
  }
  const entityIds = parseEntityIds(object.entityIds, 'Metadata object entityIds');
  const entityIdsTruncated = requireBoolean(
    object.entityIdsTruncated, 'Metadata object entityIdsTruncated',
  );
  const entityIdsTotal = Object.hasOwn(object, 'entityIdsTotal')
    ? parseWireNumber(object.entityIdsTotal, 'Metadata object entityIdsTotal')
    : entityIds.length;
  if (entityIdsTotal < entityIds.length) {
    fail('Metadata object entityIdsTotal');
  }
  return { kind, offset, range, preview, truncated, origins, entityIds, entityIdsTruncated, entityIdsTotal };
}

// Detail rows carry an optional value/valueTruncated pair; one without the
// other is a contract violation.
function applyObjectDetailValue(object, model) {
  const hasValue = Object.hasOwn(object, 'value');
  const hasValueTruncated = Object.hasOwn(object, 'valueTruncated');
  if (hasValue !== hasValueTruncated) {
    fail('Metadata object detail value');
  }
  if (hasValue) {
    model.value = parseNullable(object.value, 'Metadata object value',
      (value) => requireString(value, 'Metadata object value', MAX_DETAIL_BYTES));
    model.valueTruncated = requireBoolean(object.valueTruncated, 'Metadata object valueTruncated');
  }
}

export function parseMetadataObject(wire) {
  const object = requireObject(wire, 'Metadata object');
  const identity = parseObjectIdentity(object);
  const referenceCount = parseWireNumber(object.referenceCount, 'Metadata object referenceCount');
  const diagnostics = parseStringArray(
    object.diagnostics, 'Metadata object diagnostics', MAX_INLINE_ITEMS, MAX_PREVIEW_BYTES,
  );
  const diagnosticsTruncated = requireBoolean(
    object.diagnosticsTruncated, 'Metadata object diagnosticsTruncated',
  );

  const model = {
    key: metadataKey(identity.kind, identity.offset),
    ...identity,
    referenceCount,
    diagnostics,
    diagnosticsTruncated,
  };
  applyObjectDetailValue(object, model);
  return Object.freeze(model);
}

export function parseMetadataReference(wire) {
  const reference = requireObject(wire, 'Metadata reference');
  const kind = requireEnum(reference.kind, REFERENCE_KINDS, 'Metadata reference kind');
  const sourceEntityIds = parseEntityIds(reference.sourceEntityIds, 'Metadata reference sourceEntityIds');
  const sourceEntityIdsTruncated = requireBoolean(
    reference.sourceEntityIdsTruncated, 'Metadata reference sourceEntityIdsTruncated',
  );
  const sourceEntityIdsTotal = Object.hasOwn(reference, 'sourceEntityIdsTotal')
    ? parseWireNumber(reference.sourceEntityIdsTotal, 'Metadata reference sourceEntityIdsTotal')
    : sourceEntityIds.length;
  if (sourceEntityIdsTotal < sourceEntityIds.length) {
    fail('Metadata reference sourceEntityIdsTotal');
  }
  const sourceMethodOffset = parseNullable(reference.sourceMethodOffset, 'Metadata reference sourceMethodOffset',
    parseHex);
  const pc = parseNullable(reference.pc, 'Metadata reference pc',
    (value, label) => parseWireNumber(value, label, UINT32_MAX));
  const sourceRange = parseNullable(reference.sourceRange, 'Metadata reference sourceRange', parseRange);
  const sourceName = requireString(reference.sourceName, 'Metadata reference sourceName', MAX_PREVIEW_BYTES);
  const mnemonic = requireString(reference.mnemonic, 'Metadata reference mnemonic', 128);
  const operandIndex = parseNullable(reference.operandIndex, 'Metadata reference operandIndex',
    (value, label) => parseWireNumber(value, label, UINT32_MAX));
  return Object.freeze({
    kind,
    sourceEntityIds,
    sourceEntityIdsTruncated,
    sourceEntityIdsTotal,
    sourceMethodOffset,
    pc,
    sourceRange,
    sourceName,
    mnemonic,
    operandIndex,
  });
}

function compareNullable(left, right) {
  if (left === null) {
    return right === null ? 0 : 1;
  }
  if (right === null) {
    return -1;
  }
  return left < right ? -1 : left > right ? 1 : 0;
}

export function sortMetadataReferences(references) {
  if (!Array.isArray(references)) {
    fail('Metadata references');
  }
  return freezeArray([...references].sort((left, right) =>
    compareNullable(left.sourceMethodOffset, right.sourceMethodOffset) ||
    compareNullable(left.pc, right.pc) ||
    compareNullable(left.operandIndex, right.operandIndex)));
}

export function parseMetadataObjectPage(wire) {
  const { envelope, documentId } = parseEnvelope(wire, 'Metadata objects page');
  const revision = parseWireInteger(envelope.revision, 'Metadata objects page revision');
  const total = parseWireInteger(envelope.total, 'Metadata objects page total');
  const items = freezeArray(requireArray(envelope.items, 'Metadata objects page items', METADATA_PAGE_LIMIT)
    .map(parseMetadataObject));
  if (items.reduce((count, item) => count + item.entityIds.length, 0) > MAX_PAGE_ENTITY_IDS) {
    throw new Error(`Invalid Metadata objects page entityIds; maximum is ${MAX_PAGE_ENTITY_IDS}`);
  }
  if (items.reduce((count, item) => count + item.diagnostics.length, 0) > MAX_INLINE_ITEMS) {
    throw new Error(`Invalid Metadata objects page diagnostics; maximum is ${MAX_INLINE_ITEMS}`);
  }
  const nextCursor = parseCursor(envelope.nextCursor, 'Metadata objects page nextCursor');
  return Object.freeze({ documentId, revision, total, items, nextCursor });
}

export function parseMetadataObjectDetail(wire) {
  const { envelope, documentId } = parseEnvelope(wire, 'Metadata object detail');
  const object = parseMetadataObject(envelope);
  if (!Object.hasOwn(object, 'value')) {
    fail('Metadata object detail value');
  }
  return Object.freeze({ documentId, object });
}

export function parseMetadataReferencePage(wire, expectedKey = null) {
  const { envelope, documentId } = parseEnvelope(wire, 'Metadata references page');
  const key = requireObject(envelope.key, 'Metadata references page key');
  const kind = requireWireKind(key.kind, 'Metadata references page key kind');
  const offset = parseHex(key.offset, 'Metadata references page key offset');
  const stableKey = metadataKey(kind, offset);
  if (expectedKey !== null && stableKey !== metadataKey(expectedKey.kind, expectedKey.offset)) {
    fail('Metadata references page key');
  }
  const revision = parseWireInteger(envelope.revision, 'Metadata references page revision');
  const total = parseWireInteger(envelope.total, 'Metadata references page total');
  const parsedItems = requireArray(
    envelope.items, 'Metadata references page items', METADATA_PAGE_LIMIT,
  ).map(parseMetadataReference);
  if (parsedItems.reduce((count, item) => count + item.sourceEntityIds.length, 0) > MAX_PAGE_ENTITY_IDS) {
    throw new Error(`Invalid Metadata references page sourceEntityIds; maximum is ${MAX_PAGE_ENTITY_IDS}`);
  }
  const items = sortMetadataReferences(parsedItems);
  const nextCursor = parseCursor(envelope.nextCursor, 'Metadata references page nextCursor');
  return Object.freeze({
    documentId,
    key: stableKey,
    kind,
    offset,
    revision,
    total,
    items,
    nextCursor,
  });
}

export function objectsPath({ kind, query = '', cursor = null }) {
  requireSelectorKind(kind);
  const parsedQuery = requireString(query, 'Metadata query', MAX_QUERY_BYTES);
  if (parsedQuery.includes('\0')) {
    fail('Metadata query');
  }
  const parsedCursor = parseCursor(cursor, 'Metadata cursor');
  const params = new URLSearchParams({ kind, limit: String(METADATA_PAGE_LIMIT) });
  if (parsedQuery !== '') {
    params.set('query', parsedQuery);
  }
  if (parsedCursor !== null) {
    params.set('cursor', parsedCursor);
  }
  return `/api/v1/metadata/objects?${encodedParams(params)}`;
}

export function objectPath({ kind, offset }) {
  requireWireKind(kind);
  const params = new URLSearchParams({ kind, offset: canonicalMetadataHex(offset) });
  return `/api/v1/metadata/object?${encodedParams(params)}`;
}

export function referencesPath({ kind, offset, cursor = null }) {
  requireWireKind(kind);
  const parsedCursor = parseCursor(cursor, 'Metadata cursor');
  const params = new URLSearchParams({
    kind,
    offset: canonicalMetadataHex(offset),
    limit: String(METADATA_PAGE_LIMIT),
  });
  if (parsedCursor !== null) {
    params.set('cursor', parsedCursor);
  }
  return `/api/v1/metadata/references?${encodedParams(params)}`;
}

export function mergeMetadataObjectPage(state, page, append) {
  requireObject(state, 'Metadata page state');
  requireObject(page, 'Metadata page');
  const revisionChanged = state.revision !== null && state.revision !== page.revision;
  const retained = append && !revisionChanged;
  let pages = retained ? [...state.pages, page] : [page];
  let droppedBefore = retained ? (state.droppedBefore ?? 0) : 0;
  if (pages.length > MAX_METADATA_OBJECT_PAGES) {
    const overflow = pages.length - MAX_METADATA_OBJECT_PAGES;
    droppedBefore += pages.slice(0, overflow)
      .reduce((count, entry) => count + entry.items.length, 0);
    pages = pages.slice(-MAX_METADATA_OBJECT_PAGES);
  }
  const keys = new Set(pages.flatMap((entry) => entry.items.map((item) => item.key)));
  const selectedKey = state.selectedKey !== null && keys.has(state.selectedKey) ? state.selectedKey : null;
  return Object.freeze({
    revision: page.revision,
    pages: freezeArray(pages),
    droppedBefore,
    selectedKey,
  });
}
