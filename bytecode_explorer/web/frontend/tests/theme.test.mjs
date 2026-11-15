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

import { THEMES, THEME_LABELS, applyTheme, isTheme, nextTheme } from '../src/theme.js';

function documentStub() {
  const events = [];
  return {
    events,
    documentElement: { dataset: {} },
    dispatchEvent(event) {
      events.push(event);
    },
  };
}

test('every theme has a UI label and only known themes are accepted', () => {
  for (const theme of THEMES) {
    assert.ok(THEME_LABELS[theme], `missing label for ${theme}`);
    assert.equal(isTheme(theme), true);
  }
  assert.equal(isTheme('monokai'), false);
});

test('nextTheme cycles through the theme list and wraps around', () => {
  assert.equal(nextTheme('dark'), 'light');
  assert.equal(nextTheme('light'), 'high-contrast');
  assert.equal(nextTheme('high-contrast'), 'dark');
});

test('nextTheme rejects unknown themes', () => {
  assert.throws(() => nextTheme('solarized'), RangeError);
});

test('applyTheme sets the document theme and broadcasts the change', () => {
  const doc = documentStub();
  assert.equal(applyTheme('light', doc), 'light');
  assert.equal(doc.documentElement.dataset.theme, 'light');
  assert.equal(doc.events.length, 1);
  assert.equal(doc.events[0].type, 'themechange');
  assert.deepEqual(doc.events[0].detail, { theme: 'light' });
});

test('applyTheme is a no-op without a document and rejects unknown themes', () => {
  assert.equal(applyTheme('dark', null), 'dark');
  assert.throws(() => applyTheme('high-noon', null), RangeError);
});
