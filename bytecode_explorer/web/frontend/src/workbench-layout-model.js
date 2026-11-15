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

export const WORKBENCH_LAYOUT_VERSION = 1;
export const HEX_WIDE_MIN_WIDTH = 760;

export const WORKBENCH_LIMITS = Object.freeze({
  structureMinWidth: 220,
  centerMinWidth: 520,
  inspectorMinWidth: 360,
  separatorSize: 6,
  rowRatioMin: 0.25,
  rowRatioMax: 0.75,
});

const PANELS = new Set([
  'structure-panel',
  'disassembly-panel',
  'cfg-panel',
  'metadata-panel',
  'hex-panel',
]);

const SPLITTERS = new Set(['structure', 'inspector', 'centerRows', 'inspectorRows']);

export function defaultWorkbenchLayout() {
  return {
    structureWidth: 280,
    inspectorWidth: 420,
    centerRatio: 0.52,
    inspectorRatio: 0.58,
    maximizedPanel: null,
  };
}

function requireViewport(viewport) {
  if (viewport === null || typeof viewport !== 'object' ||
      !Number.isFinite(viewport.width) || viewport.width <= 0 ||
      !Number.isFinite(viewport.height) || viewport.height <= 0) {
    throw new TypeError('viewport width and height must be positive finite numbers');
  }
  return viewport;
}

function clamp(value, minimum, maximum) {
  return Math.min(Math.max(value, minimum), maximum);
}

function isValidLayout(layout) {
  return layout !== null && typeof layout === 'object' &&
    Number.isFinite(layout.structureWidth) &&
    Number.isFinite(layout.inspectorWidth) &&
    Number.isFinite(layout.centerRatio) &&
    Number.isFinite(layout.inspectorRatio) &&
    (layout.maximizedPanel === null || PANELS.has(layout.maximizedPanel));
}

export function clampWorkbenchLayout(layout, viewport) {
  requireViewport(viewport);
  if (!isValidLayout(layout)) {
    throw new TypeError('layout contains invalid workbench geometry');
  }

  const limits = WORKBENCH_LIMITS;
  const available = Math.max(0, viewport.width - limits.separatorSize * 2);
  const structureMaximum = Math.max(limits.structureMinWidth,
    available - limits.centerMinWidth - limits.inspectorMinWidth);
  const structureWidth = clamp(layout.structureWidth, limits.structureMinWidth, structureMaximum);
  const inspectorMaximum = Math.max(limits.inspectorMinWidth,
    available - limits.centerMinWidth - structureWidth);
  const inspectorWidth = clamp(layout.inspectorWidth, limits.inspectorMinWidth, inspectorMaximum);

  return {
    structureWidth,
    inspectorWidth,
    centerRatio: clamp(layout.centerRatio, limits.rowRatioMin, limits.rowRatioMax),
    inspectorRatio: clamp(layout.inspectorRatio, limits.rowRatioMin, limits.rowRatioMax),
    maximizedPanel: layout.maximizedPanel,
  };
}

export function applySplitterDelta(layout, splitter, delta, viewport) {
  if (!SPLITTERS.has(splitter)) {
    throw new TypeError('unknown workbench splitter');
  }
  if (!Number.isFinite(delta)) {
    throw new TypeError('splitter delta must be finite');
  }
  requireViewport(viewport);

  const next = { ...layout };
  switch (splitter) {
    case 'structure':
      next.structureWidth += delta;
      break;
    case 'inspector':
      next.inspectorWidth -= delta;
      break;
    case 'centerRows':
      next.centerRatio += delta / viewport.height;
      break;
    case 'inspectorRows':
      next.inspectorRatio += delta / viewport.height;
      break;
    default:
      break;
  }
  return clampWorkbenchLayout(next, viewport);
}

export function setMaximizedPanel(layout, panelId) {
  if (panelId !== null && !PANELS.has(panelId)) {
    throw new TypeError('unknown workbench panel');
  }
  if (!isValidLayout(layout)) {
    throw new TypeError('layout contains invalid workbench geometry');
  }
  return { ...layout, maximizedPanel: panelId };
}

export function serializeWorkbenchLayout(layout) {
  if (!isValidLayout(layout)) {
    throw new TypeError('layout contains invalid workbench geometry');
  }
  return JSON.stringify({ version: WORKBENCH_LAYOUT_VERSION, layout });
}

export function parseStoredWorkbenchLayout(text, viewport) {
  requireViewport(viewport);
  if (typeof text !== 'string' || text.length === 0) {
    return defaultWorkbenchLayout();
  }

  try {
    const stored = JSON.parse(text);
    if (stored?.version !== WORKBENCH_LAYOUT_VERSION || !isValidLayout(stored.layout)) {
      return defaultWorkbenchLayout();
    }
    return clampWorkbenchLayout(stored.layout, viewport);
  } catch {
    return defaultWorkbenchLayout();
  }
}

export function hexBytesPerRow(panelWidth) {
  if (!Number.isFinite(panelWidth) || panelWidth < 0) {
    throw new TypeError('Hex panel width must be a non-negative finite number');
  }
  return panelWidth >= HEX_WIDE_MIN_WIDTH ? 16 : 8;
}
