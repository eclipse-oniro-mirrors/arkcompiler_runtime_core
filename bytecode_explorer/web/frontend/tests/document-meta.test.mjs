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

import { buildModeBadge, buildModeTooltip, documentMetaText } from '../src/document-meta.js';

const BASE_MODEL = { version: '0.0.0.2', fileSize: 1248, checksum: '0x009abcdef' };
const DEBUG_EVIDENCE = {
  debugOnlyInstructions: 1,
  variableTableEntries: 2,
  sourceCodeMarkers: 0,
  scannedMethods: 3,
};

test('buildModeBadge maps wire values to badges and rejects everything else', () => {
  assert.equal(buildModeBadge({ ...BASE_MODEL, buildMode: 'debug' }), 'DEBUG');
  assert.equal(buildModeBadge({ ...BASE_MODEL, buildMode: 'release' }), 'RELEASE');
  assert.equal(buildModeBadge({ ...BASE_MODEL, buildMode: 'unknown' }), null);
  assert.equal(buildModeBadge({ ...BASE_MODEL }), null);
  assert.equal(buildModeBadge({ ...BASE_MODEL, buildMode: 42 }), null);
  assert.equal(buildModeBadge(null), null);
  assert.equal(buildModeBadge(undefined), null);
});

test('documentMetaText appends the badge only for known build modes', () => {
  assert.equal(
    documentMetaText({ ...BASE_MODEL, buildMode: 'debug' }),
    'ABC 0.0.0.2 · 1248 bytes · 0x009abcdef · DEBUG',
  );
  assert.equal(
    documentMetaText({ ...BASE_MODEL, buildMode: 'release' }),
    'ABC 0.0.0.2 · 1248 bytes · 0x009abcdef · RELEASE',
  );
  assert.equal(
    documentMetaText({ ...BASE_MODEL, buildMode: 'unknown' }),
    'ABC 0.0.0.2 · 1248 bytes · 0x009abcdef',
  );
  assert.equal(
    documentMetaText(BASE_MODEL),
    'ABC 0.0.0.2 · 1248 bytes · 0x009abcdef',
  );
});

test('buildModeTooltip lists the debug evidence counts it can validate', () => {
  const tooltip = buildModeTooltip({ ...BASE_MODEL, buildMode: 'debug', buildEvidence: DEBUG_EVIDENCE });
  assert.ok(tooltip.startsWith('Debug build across 3 methods — '));
  assert.ok(tooltip.includes('1 debug-only instructions'));
  assert.ok(tooltip.includes('2 variable-table entries'));
  assert.ok(!tooltip.includes('0 source-code markers'));
  assert.ok(tooltip.includes('Debug builds skip optimization and keep debugger metadata.'));
});

test('buildModeTooltip falls back to a generic statement without usable evidence', () => {
  assert.equal(
    buildModeTooltip({ ...BASE_MODEL, buildMode: 'debug' }),
    'Debug build — debug-only markers. Debug builds skip optimization and keep debugger metadata.',
  );
  assert.equal(
    buildModeTooltip({ ...BASE_MODEL, buildMode: 'debug', buildEvidence: 'corrupt' }),
    'Debug build — debug-only markers. Debug builds skip optimization and keep debugger metadata.',
  );
  assert.equal(
    buildModeTooltip({
      ...BASE_MODEL,
      buildMode: 'debug',
      buildEvidence: { debugOnlyInstructions: -1, variableTableEntries: NaN, sourceCodeMarkers: 'many' },
    }),
    'Debug build — debug-only markers. Debug builds skip optimization and keep debugger metadata.',
  );
});

test('buildModeTooltip states the release verdict and omits counts when scannedMethods is unusable', () => {
  assert.equal(
    buildModeTooltip({ ...BASE_MODEL, buildMode: 'release', buildEvidence: { scannedMethods: 5 } }),
    'Release build across 5 methods — no debug-only markers in bytecode or line programs.',
  );
  assert.equal(
    buildModeTooltip({ ...BASE_MODEL, buildMode: 'release' }),
    'Release build — no debug-only markers in bytecode or line programs.',
  );
});

test('buildModeTooltip makes no statement for unknown build modes', () => {
  assert.equal(buildModeTooltip({ ...BASE_MODEL, buildMode: 'unknown' }), null);
  assert.equal(buildModeTooltip(BASE_MODEL), null);
  assert.equal(buildModeTooltip(null), null);
});
