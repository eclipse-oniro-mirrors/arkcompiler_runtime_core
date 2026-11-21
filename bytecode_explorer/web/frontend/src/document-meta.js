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

// Composes the top-bar document meta line: version, size, checksum and the
// build mode verdict. The ABC header carries no build flag, so the backend
// derives the verdict from content only debug compilation can produce
// (debug-only bytecode opcodes plus line-program variable/source markers).

const KNOWN_BUILD_MODES = new Set(['debug', 'release']);

function evidence(model) {
  const raw = model.buildEvidence;
  if (raw === null || raw === undefined || typeof raw !== 'object') {
    return null;
  }
  const count = (value) => (typeof value === 'number' && Number.isFinite(value) && value >= 0 ? value : null);
  return {
    debugOnlyInstructions: count(raw.debugOnlyInstructions),
    variableTableEntries: count(raw.variableTableEntries),
    sourceCodeMarkers: count(raw.sourceCodeMarkers),
    scannedMethods: count(raw.scannedMethods),
  };
}

// 'DEBUG' | 'RELEASE' | null — null means "no statement" (unknown or a wire
// shape we do not recognize); the meta line then stays unchanged.
export function buildModeBadge(model) {
  if (model === null || model === undefined) {
    return null;
  }
  if (!KNOWN_BUILD_MODES.has(model.buildMode)) {
    return null;
  }
  return model.buildMode === 'debug' ? 'DEBUG' : 'RELEASE';
}

export function documentMetaText(model) {
  const meta = `ABC ${model.version} · ${model.fileSize} bytes · ${model.checksum}`;
  const badge = buildModeBadge(model);
  return badge === null ? meta : `${meta} · ${badge}`;
}

export function buildModeTooltip(model) {
  if (buildModeBadge(model) === null) {
    return null;
  }
  const counts = evidence(model);
  const scope = counts === null || counts.scannedMethods === null
    ? null : ` across ${counts.scannedMethods} methods`;
  if (model.buildMode === 'debug') {
    const parts = [];
    if (counts !== null) {
      if (counts.debugOnlyInstructions > 0) {
        parts.push(`${counts.debugOnlyInstructions} debug-only instructions`);
      }
      if (counts.variableTableEntries > 0) {
        parts.push(`${counts.variableTableEntries} variable-table entries`);
      }
      if (counts.sourceCodeMarkers > 0) {
        parts.push(`${counts.sourceCodeMarkers} source-code markers`);
      }
    }
    const detail = parts.length === 0 ? 'debug-only markers' : parts.join(', ');
    return `Debug build${scope ?? ''} — ${detail}. Debug builds skip optimization and keep debugger metadata.`;
  }
  return `Release build${scope ?? ''} — no debug-only markers in bytecode or line programs.`;
}
