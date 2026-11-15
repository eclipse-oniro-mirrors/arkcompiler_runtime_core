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
  HEX_WIDE_MIN_WIDTH,
  WORKBENCH_LAYOUT_VERSION,
  applySplitterDelta,
  clampWorkbenchLayout,
  defaultWorkbenchLayout,
  hexBytesPerRow,
  parseStoredWorkbenchLayout,
  serializeWorkbenchLayout,
  setMaximizedPanel,
} from '../src/workbench-layout-model.js';

const VIEWPORT = { width: 1600, height: 900 };

test('workbench defaults keep Structure left and two stacked center and inspector panes', () => {
  assert.deepEqual(defaultWorkbenchLayout(), {
    structureWidth: 280,
    inspectorWidth: 420,
    centerRatio: 0.52,
    inspectorRatio: 0.58,
    maximizedPanel: null,
  });
});

test('column widths and row ratios clamp to readable bounds', () => {
  assert.deepEqual(clampWorkbenchLayout({
    structureWidth: 20,
    inspectorWidth: 20,
    centerRatio: -4,
    inspectorRatio: 4,
    maximizedPanel: null,
  }, { width: 1280, height: 720 }), {
    structureWidth: 220,
    inspectorWidth: 360,
    centerRatio: 0.25,
    inspectorRatio: 0.75,
    maximizedPanel: null,
  });

  const maximums = clampWorkbenchLayout({
    structureWidth: 900,
    inspectorWidth: 900,
    centerRatio: 0.5,
    inspectorRatio: 0.5,
    maximizedPanel: null,
  }, { width: 1280, height: 720 });
  assert.ok(maximums.structureWidth >= 220);
  assert.ok(maximums.inspectorWidth >= 360);
  assert.ok(1280 - maximums.structureWidth - maximums.inspectorWidth - 12 >= 520);
});

test('splitter deltas alter only their owned axis', () => {
  const initial = defaultWorkbenchLayout();
  assert.deepEqual(applySplitterDelta(initial, 'structure', 40, VIEWPORT), {
    ...initial,
    structureWidth: 320,
  });
  assert.deepEqual(applySplitterDelta(initial, 'inspector', 40, VIEWPORT), {
    ...initial,
    inspectorWidth: 380,
  });
  assert.deepEqual(applySplitterDelta(initial, 'centerRows', 90, VIEWPORT), {
    ...initial,
    centerRatio: 0.62,
  });
  assert.deepEqual(applySplitterDelta(initial, 'inspectorRows', -90, VIEWPORT), {
    ...initial,
    inspectorRatio: 0.48,
  });
  assert.throws(() => applySplitterDelta(initial, 'unknown', 1, VIEWPORT), /splitter/i);
});

test('maximize changes only presentation and restore keeps geometry', () => {
  const layout = { ...defaultWorkbenchLayout(), structureWidth: 310, centerRatio: 0.6 };
  const maximized = setMaximizedPanel(layout, 'cfg-panel');
  assert.deepEqual(maximized, { ...layout, maximizedPanel: 'cfg-panel' });
  assert.deepEqual(setMaximizedPanel(maximized, null), layout);
  assert.throws(() => setMaximizedPanel(layout, 'unknown-panel'), /panel/i);
});

test('versioned persistence rejects corrupt, stale, and non-finite layouts', () => {
  const layout = { ...defaultWorkbenchLayout(), structureWidth: 330 };
  const encoded = serializeWorkbenchLayout(layout);
  assert.equal(JSON.parse(encoded).version, WORKBENCH_LAYOUT_VERSION);
  assert.deepEqual(parseStoredWorkbenchLayout(encoded, VIEWPORT), layout);

  for (const invalid of [
    null,
    '',
    '{',
    JSON.stringify({ version: WORKBENCH_LAYOUT_VERSION - 1 }),
    JSON.stringify({ version: WORKBENCH_LAYOUT_VERSION, layout: { structureWidth: 'wide' } }),
  ]) {
    assert.deepEqual(parseStoredWorkbenchLayout(invalid, VIEWPORT), defaultWorkbenchLayout());
  }
});

test('Hex density follows its panel width', () => {
  assert.equal(HEX_WIDE_MIN_WIDTH, 760);
  assert.equal(hexBytesPerRow(420), 8);
  assert.equal(hexBytesPerRow(HEX_WIDE_MIN_WIDTH - 1), 8);
  assert.equal(hexBytesPerRow(HEX_WIDE_MIN_WIDTH), 16);
  assert.equal(hexBytesPerRow(1200), 16);
  assert.throws(() => hexBytesPerRow(Number.NaN), /width/i);
});
