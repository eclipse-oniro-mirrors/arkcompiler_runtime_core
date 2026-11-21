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

import {
  METADATA_KINDS,
  METADATA_PAGE_LIMIT,
  canonicalMetadataHex,
  mergeMetadataObjectPage,
  metadataKey,
  objectPath,
  objectsPath,
  parseHex,
  parseMetadataObject,
  parseMetadataObjectPage,
  parseMetadataReference,
  parseMetadataReferencePage,
  parseMetadataStatus,
  referencesPath,
  sortMetadataReferences,
} from '../src/metadata-view-model.js';

function statusWire(overrides = {}) {
  return {
    apiVersion: 1,
    documentId: 'document-one',
    state: 'Building',
    revision: '7',
    scannedMethods: '3',
    totalMethods: '8',
    objectCount: '12',
    referenceCount: '4',
    diagnostics: [],
    diagnosticsTruncated: false,
    ...overrides,
  };
}

function objectWire(kind = 'String', offset = '0x300', overrides = {}) {
  return {
    key: { kind, offset },
    kind,
    range: { offset, size: 4 },
    preview: 'preview',
    truncated: false,
    origins: ['Structure'],
    entityIds: ['65'],
    entityIdsTruncated: false,
    referenceCount: 1,
    diagnostics: [],
    diagnosticsTruncated: false,
    ...overrides,
  };
}

function referenceWire(overrides = {}) {
  return {
    kind: 'Instruction Operand',
    sourceEntityIds: ['65'],
    sourceEntityIdsTruncated: false,
    sourceMethodOffset: '0x20',
    pc: 4,
    sourceRange: { offset: '0x40', size: 2 },
    sourceName: 'source',
    mnemonic: 'lda.str',
    operandIndex: 0,
    ...overrides,
  };
}

function objectPageWire(items, overrides = {}) {
  return {
    apiVersion: 1,
    documentId: 'document-one',
    revision: '7',
    total: String(items.length),
    items,
    nextCursor: null,
    ...overrides,
  };
}

test('accepts exactly the public selector and three wire Metadata kinds', () => {
  assert.deepEqual(METADATA_KINDS, ['All', 'String', 'Method', 'Literal Array']);
  for (const kind of METADATA_KINDS.slice(1)) {
    assert.equal(parseMetadataObject(objectWire(kind)).kind, kind);
  }
  for (const kind of ['All', 'Field', 'Unknown', 'string', '']) {
    assert.throws(() => parseMetadataObject(objectWire(kind)), /kind/i);
  }
});

test('parses canonical hexadecimal offsets as BigInt and rejects noncanonical or overflowing values', () => {
  assert.equal(parseHex('0x0', 'offset'), 0n);
  assert.equal(parseHex('0x12AF', 'offset'), 0x12afn);
  assert.equal(parseHex('0X12af', 'offset'), 0x12afn);
  assert.equal(canonicalMetadataHex(0x300n), '0x300');
  for (const value of ['300', '0x', '0x00', '+0x1', '0x1z', '0x10000000000000000']) {
    assert.throws(() => parseHex(value, 'offset'), /offset/i, value);
  }
  assert.throws(() => parseMetadataObject(objectWire('String', Number.MAX_SAFE_INTEGER + 1)), /offset/i);
});

test('parses revision counters as BigInt without accepting unsafe JSON numbers', () => {
  assert.equal(parseMetadataStatus(statusWire({ revision: '9007199254740993' })).revision,
    9007199254740993n);
  assert.equal(parseMetadataStatus(statusWire({ revision: 9 })).revision, 9n);
  assert.throws(() => parseMetadataStatus(statusWire({ revision: Number.MAX_SAFE_INTEGER + 1 })), /revision/i);
  assert.throws(() => parseMetadataStatus(statusWire({ revision: '-1' })), /revision/i);
});

test('rejects malformed ranges, arrays, nullable fields, and mismatched keys', () => {
  const invalidObjects = [
    objectWire('String', '0x300', { key: { kind: 'Method', offset: '0x300' } }),
    objectWire('String', '0x300', { range: { offset: '0x300', size: 0 } }),
    objectWire('String', '0x300', { range: { offset: '0xffffffffffffffff', size: 2 } }),
    objectWire('String', '0x300', { origins: 'Structure' }),
    objectWire('String', '0x300', { entityIds: [65] }),
    objectWire('String', '0x300', { diagnostics: [null] }),
    objectWire('String', '0x300', { value: null }),
  ];
  for (const wire of invalidObjects) {
    assert.throws(() => parseMetadataObject(wire));
  }

  for (const field of ['sourceMethodOffset', 'pc', 'sourceRange', 'operandIndex']) {
    assert.doesNotThrow(() => parseMetadataReference(referenceWire({ [field]: null })));
    assert.throws(() => parseMetadataReference(referenceWire({ [field]: undefined })), undefined, field);
  }
  assert.throws(() => parseMetadataReference(referenceWire({ sourceRange: { offset: '0x40', size: -1 } })));
  assert.throws(() => parseMetadataReference(referenceWire({ pc: 0x1_0000_0000 })));
  assert.throws(() => parseMetadataReference(referenceWire({ sourceEntityIds: '65' })));
});

test('rejects invalid status states and backwards or impossible status transitions', () => {
  const building = parseMetadataStatus(statusWire());
  assert.equal(parseMetadataStatus(statusWire({ state: 'Ready', revision: '8', scannedMethods: '8' }), building).state,
    'Ready');
  assert.throws(() => parseMetadataStatus(statusWire({ state: 'Unknown' })), /state/i);
  assert.throws(() => parseMetadataStatus(statusWire({ state: 'NotStarted' }), building), /transition/i);
  assert.throws(() => parseMetadataStatus(statusWire({ revision: '6' }), building), /revision/i);
  assert.throws(() => parseMetadataStatus(statusWire({ scannedMethods: '2' }), building), /progress/i);
  const ready = parseMetadataStatus(statusWire({ state: 'Ready', revision: '8', scannedMethods: '8' }), building);
  assert.throws(() => parseMetadataStatus(statusWire({ state: 'Building', revision: '9', scannedMethods: '8' }), ready),
    /transition/i);
  assert.throws(() => parseMetadataStatus(statusWire({ state: 'Ready', revision: '9', scannedMethods: '8' }), ready),
    /transition/i);
  assert.throws(() => parseMetadataStatus(statusWire({
    state: 'Ready', revision: '8', scannedMethods: '8', diagnostics: ['late diagnostic'],
  }), ready), /transition/i);
  assert.throws(() => parseMetadataStatus(statusWire({
    state: 'Ready', revision: '8', scannedMethods: '8', diagnosticsTruncated: true,
  }), ready), /transition/i);
});

test('enforces inline entity, diagnostic, preview, mnemonic, and query UTF-8 byte caps', () => {
  const twoFiftySeven = Array.from({ length: 257 }, (_, index) => String(index));
  assert.throws(() => parseMetadataObject(objectWire('String', '0x300', { entityIds: twoFiftySeven })), /256/);
  assert.throws(() => parseMetadataObject(objectWire('String', '0x300', {
    diagnostics: Array.from({ length: 257 }, () => 'x'),
  })), /256/);
  assert.throws(() => parseMetadataStatus(statusWire({ diagnostics: Array.from({ length: 257 }, () => 'x') })), /256/);
  assert.throws(() => parseMetadataObject(objectWire('String', '0x300', { preview: '界'.repeat(171) })), /512/);
  assert.throws(() => parseMetadataReference(referenceWire({ sourceName: '界'.repeat(171) })), /512/);
  assert.throws(() => parseMetadataReference(referenceWire({ mnemonic: '界'.repeat(43) })), /128/);
  assert.throws(() => objectsPath({ kind: 'All', query: '界'.repeat(86) }), /256/);
  assert.throws(() => objectsPath({ kind: 'All', query: 'a\0b' }), /query/i);
});

test('parses association totals with a backward-compatible fallback and consistency checks', () => {
  assert.equal(parseMetadataObject(objectWire('String', '0x300', {
    entityIds: ['1', '2'], entityIdsTotal: 115,
  })).entityIdsTotal, 115);
  assert.equal(parseMetadataObject(objectWire('String', '0x300', {
    entityIds: ['1', '2'],
  })).entityIdsTotal, 2);
  assert.throws(() => parseMetadataObject(objectWire('String', '0x300', {
    entityIds: ['1', '2'], entityIdsTotal: 1,
  })), /entityIdsTotal/i);
  assert.equal(parseMetadataReference(referenceWire({
    sourceEntityIds: ['1'], sourceEntityIdsTotal: 17,
  })).sourceEntityIdsTotal, 17);
  assert.equal(parseMetadataReference(referenceWire({ sourceEntityIds: ['1'] })).sourceEntityIdsTotal, 1);
  assert.throws(() => parseMetadataReference(referenceWire({
    sourceEntityIds: ['1', '2'], sourceEntityIdsTotal: 1,
  })), /sourceEntityIdsTotal/i);
});

test('creates strict paths with percent-encoded spaces, Unicode, and cursors but never plus', () => {
  assert.equal(METADATA_PAGE_LIMIT, 100);
  assert.equal(objectsPath({ kind: 'Literal Array', query: 'a b界', cursor: 'm1:a/b +c' }),
    '/api/v1/metadata/objects?kind=Literal%20Array&limit=100&query=a%20b%E7%95%8C&cursor=m1%3Aa%2Fb%20%2Bc');
  assert.equal(objectsPath({ kind: 'All' }), '/api/v1/metadata/objects?kind=All&limit=100');
  assert.equal(objectsPath({ kind: 'String', query: '', cursor: null }),
    '/api/v1/metadata/objects?kind=String&limit=100');
  assert.equal(objectPath({ kind: 'Literal Array', offset: 0x300n }),
    '/api/v1/metadata/object?kind=Literal%20Array&offset=0x300');
  assert.equal(referencesPath({ kind: 'String', offset: '0x300', cursor: 'm1:a+b/c' }),
    '/api/v1/metadata/references?kind=String&offset=0x300&limit=100&cursor=m1%3Aa%2Bb%2Fc');
  for (const path of [objectsPath({ kind: 'All', query: 'a b' }), referencesPath({
    kind: 'String', offset: 0x300n, cursor: 'a b',
  })]) {
    assert.equal(path.includes('+'), false);
  }
});

test('metadataKey canonicalizes offsets into a stable plain key', () => {
  assert.equal(metadataKey('String', 0x300n), 'String@0x300');
  assert.equal(metadataKey('String', '0x300'), 'String@0x300');
  assert.throws(() => metadataKey('All', 0x300n), /kind/i);
});

test('parsers return frozen plain models without retaining mutable wire objects', () => {
  const wire = objectWire();
  const model = parseMetadataObject(wire);
  wire.key.offset = '0x400';
  wire.entityIds[0] = '99';
  assert.equal(model.offset, 0x300n);
  assert.deepEqual(model.entityIds, ['65']);
  assert.equal(Object.isFrozen(model), true);
  assert.equal(Object.isFrozen(model.range), true);
  assert.equal(Object.isFrozen(model.entityIds), true);
  assert.equal(Object.getPrototypeOf(model), Object.prototype);
});

test('reference sorting is source method offset, PC, then operand index with nulls last', () => {
  const references = [
    parseMetadataReference(referenceWire({ sourceMethodOffset: '0x20', pc: 4, operandIndex: 2 })),
    parseMetadataReference(referenceWire({ sourceMethodOffset: '0x10', pc: null, operandIndex: null })),
    parseMetadataReference(referenceWire({ sourceMethodOffset: '0x20', pc: 3, operandIndex: 9 })),
    parseMetadataReference(referenceWire({ sourceMethodOffset: '0x20', pc: 4, operandIndex: 1 })),
    parseMetadataReference(referenceWire({ sourceMethodOffset: null, pc: 0, operandIndex: 0 })),
  ];
  const sorted = sortMetadataReferences(references);
  assert.deepEqual(sorted.map((reference) => [reference.sourceMethodOffset, reference.pc, reference.operandIndex]), [
    [0x10n, null, null], [0x20n, 3, 9], [0x20n, 4, 1], [0x20n, 4, 2], [null, 0, 0],
  ]);
  assert.notEqual(sorted, references);
  assert.equal(Object.isFrozen(sorted), true);
});

test('page parsers enforce page limits, revision precision, and reference keys', () => {
  const page = parseMetadataObjectPage(objectPageWire([objectWire()]));
  assert.equal(page.revision, 7n);
  assert.equal(page.items[0].offset, 0x300n);
  assert.throws(() => parseMetadataObjectPage(objectPageWire(Array.from({ length: 101 }, () => objectWire()))), /100/);
  assert.throws(() => parseMetadataObjectPage(objectPageWire([], { revision: Number.MAX_SAFE_INTEGER + 1 })), /revision/i);
  assert.throws(() => parseMetadataReferencePage({
    ...objectPageWire([referenceWire()]),
    key: { kind: 'String', offset: '0x301' },
  }, { kind: 'String', offset: 0x300n }), /key/i);
});

test('page parsers enforce page-level and per-array inline budgets across the whole response', () => {
  const wideIds = Array.from({ length: 241 }, (_, index) => String(index));
  const idItems = Array.from({ length: 17 }, (_, index) =>
    objectWire('String', `0x${(0x300 + index).toString(16)}`, { entityIds: wideIds }));
  assert.throws(() => parseMetadataObjectPage(objectPageWire(idItems)), /4096/);
  assert.throws(() => parseMetadataObjectPage(objectPageWire([
    objectWire('String', '0x300', { diagnostics: Array.from({ length: 129 }, () => 'a') }),
    objectWire('String', '0x301', { diagnostics: Array.from({ length: 129 }, () => 'b') }),
  ])), /256/);
  assert.throws(() => parseMetadataReferencePage({
    ...objectPageWire(Array.from({ length: 17 }, () => referenceWire({ sourceEntityIds: wideIds }))),
    key: { kind: 'String', offset: '0x300' },
  }), /4096/);
});

test('revision changes reset pagination while preserving a selected stable key that still exists', () => {
  const oldPage = parseMetadataObjectPage(objectPageWire([
    objectWire('String', '0x100'), objectWire('String', '0x300'),
  ], { revision: '7', nextCursor: 'next-old' }));
  const oldState = mergeMetadataObjectPage({ revision: null, pages: [], selectedKey: null }, oldPage, false);
  const selectedState = { ...oldState, selectedKey: 'String@0x300' };
  const appended = mergeMetadataObjectPage(selectedState, parseMetadataObjectPage(objectPageWire([
    objectWire('String', '0x400'),
  ], { revision: '7' })), true);
  assert.equal(appended.pages.length, 2);

  const revised = mergeMetadataObjectPage(appended, parseMetadataObjectPage(objectPageWire([
    objectWire('String', '0x300'), objectWire('Method', '0x500'),
  ], { revision: '8' })), true);
  assert.equal(revised.revision, 8n);
  assert.equal(revised.pages.length, 1);
  assert.equal(revised.selectedKey, 'String@0x300');

  const removed = mergeMetadataObjectPage(revised, parseMetadataObjectPage(objectPageWire([
    objectWire('String', '0x700'),
  ], { revision: '9' })), false);
  assert.equal(removed.selectedKey, null);
});

test('the object window keeps five pages and reports the collapsed prefix', () => {
  const makePage = (index) => parseMetadataObjectPage(objectPageWire([
    objectWire('String', `0x${(0x100 + index).toString(16)}`),
    objectWire('String', `0x${(0x200 + index).toString(16)}`),
  ], { nextCursor: `cursor-${index}` }));

  let state = mergeMetadataObjectPage({ revision: null, pages: [], selectedKey: null }, makePage(1), false);
  assert.equal(state.droppedBefore, 0);
  for (let index = 2; index <= 5; index += 1) {
    state = mergeMetadataObjectPage(state, makePage(index), true);
  }
  assert.equal(state.pages.length, 5);
  assert.equal(state.droppedBefore, 0);

  state = mergeMetadataObjectPage(state, makePage(6), true);
  assert.equal(state.pages.length, 5);
  assert.equal(state.droppedBefore, 2);
  assert.equal(state.pages[0].items[0].key, 'String@0x102');

  state = mergeMetadataObjectPage(Object.freeze({ ...state, droppedBefore: 4 }), makePage(7), true);
  assert.equal(state.pages.length, 5);
  assert.equal(state.droppedBefore, 6);

  const revised = mergeMetadataObjectPage(state, parseMetadataObjectPage(objectPageWire([
    objectWire('String', '0x900'),
  ], { revision: '8' })), true);
  assert.equal(revised.pages.length, 1);
  assert.equal(revised.droppedBefore, 0);
});
