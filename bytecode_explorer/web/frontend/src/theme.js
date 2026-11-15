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

// Color themes. The default document state carries no data-theme attribute and
// resolves to the dark token block, so applying 'dark' explicitly and shipping
// without JavaScript both land on the same visuals. 'high-contrast' keeps the
// dark hues but raises boundaries, text and accents for low-vision use.
export const THEMES = Object.freeze(['dark', 'light', 'high-contrast']);
export const THEME_LABELS = Object.freeze({ dark: 'Dark', light: 'Light', 'high-contrast': 'High Contrast' });

export function isTheme(theme) {
  return THEMES.includes(theme);
}

// The toggle cycles through the themes in THEMES order, wrapping around.
export function nextTheme(theme) {
  const index = THEMES.indexOf(theme);
  if (index < 0) {
    throw new RangeError(`Unknown theme: ${theme}`);
  }
  return THEMES[(index + 1) % THEMES.length];
}

// Applies the theme to the document root and broadcasts 'themechange' so
// views that paint theme colors outside CSS (the CFG canvas markers) can
// refresh. The document is injected so tests can run without a DOM.
export function applyTheme(theme, doc = globalThis.document) {
  if (!isTheme(theme)) {
    throw new RangeError(`Unknown theme: ${theme}`);
  }
  if (!doc) {
    return theme;
  }
  doc.documentElement.dataset.theme = theme;
  doc.dispatchEvent(new CustomEvent('themechange', { detail: { theme } }));
  return theme;
}
