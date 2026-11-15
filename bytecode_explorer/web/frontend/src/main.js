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

import { ApiClient, RESPONSE_WIRE_BYTES } from './api-client.js';
import { BoundedResponseCache, bootstrapToken } from './app-state.js';
import { CfgView, cfgRequestPath, cfgSelectionKey } from './cfg-view.js';
import {
  SWITCH_REQUEST_TIMEOUT_MS,
  pollPickStatus,
  validateRecentFiles,
} from './document-switching.js';
import { preferredNodeRange, rangeFromWire } from './hex-view-model.js';
import { HexView } from './hex-view.js';
import { canonicalMetadataHex, metadataKey } from './metadata-view-model.js';
import { MetadataView } from './metadata-view.js';
import { flattenTree, indexStructure } from './tree-model.js';
import { WorkbenchLayout } from './workbench-layout.js';
import { loadExecutableWorkbenchViews } from './workbench-selection.js';
import { fetchSessionModels, loadSessionModels, pollSession } from './session-loading.js';
import { StatisticsView } from './statistics-view.js';
import { ModuleStatisticsPrefetch } from './module-statistics-prefetch.js';
import { installTooltips } from './tooltip.js';
import { buildModeTooltip, documentMetaText } from './document-meta.js';
import { THEME_LABELS, THEMES, applyTheme, isTheme, nextTheme } from './theme.js';

const ELEMENT_IDS = [
  'file-name', 'node-count', 'tree-search', 'structure-tree', 'breadcrumb', 'empty-state', 'detail-view',
  'entity-kicker', 'function-name', 'internal-name', 'origin-badge', 'metrics', 'instruction-count',
  'instruction-body', 'instruction-inspector', 'error-state', 'status-text', 'document-meta',
  'workbench', 'cfg-view', 'cfg-summary', 'cfg-viewport', 'cfg-stage', 'cfg-edge-layer',
  'cfg-node-layer', 'cfg-zoom-label', 'cfg-zoom-in', 'cfg-zoom-out', 'cfg-actual-size', 'cfg-fit',
  'cfg-reset-current', 'cfg-reset-layout', 'hex-view', 'hex-jump-form', 'hex-range-summary',
  'hex-offset', 'hex-go', 'hex-notice', 'hex-viewport', 'hex-spacer', 'hex-rows',
  'metadata-view', 'metadata-search-form', 'metadata-search', 'metadata-kind',
  'metadata-summary', 'metadata-progress', 'metadata-progress-bar', 'metadata-notice', 'metadata-list', 'metadata-objects',
  'metadata-load-more', 'metadata-selection', 'metadata-detail', 'metadata-references', 'metadata-references-back',
  'metadata-references-next',
  'file-chip', 'landing-state', 'landing-open', 'landing-recent', 'toast', 'theme-toggle', 'theme-label',
  'tree-sort',
];

export function metadataTargetForInstructionId(id) {
  if (id === null || typeof id !== 'object' ||
      !['String', 'Method', 'Literal Array'].includes(id.kind) ||
      typeof id.resolvedOffset !== 'string' || !/^0x(?:0|[1-9a-fA-F][0-9a-fA-F]*)$/.test(id.resolvedOffset)) {
    return null;
  }
  try {
    const offset = BigInt(id.resolvedOffset);
    metadataKey(id.kind, offset);
    return Object.freeze({ kind: id.kind, offset });
  } catch {
    return null;
  }
}

export function metadataDisassemblyPath(entityId, methodOffset) {
  if (typeof entityId !== 'string' || !/^(?:[1-9][0-9]*)$/.test(entityId)) {
    throw new Error('Invalid Metadata navigation entity id');
  }
  if (typeof methodOffset !== 'bigint' || methodOffset < 0n || methodOffset > 0xffff_ffff_ffff_ffffn) {
    throw new Error('Invalid Metadata navigation method offset');
  }
  return `/api/v1/disassembly?id=${entityId}&methodOffset=${canonicalMetadataHex(methodOffset)}`;
}

export function singleCompleteMetadataEntityId({ entityIds, entityIdsTruncated }) {
  if (entityIdsTruncated !== false || !Array.isArray(entityIds) || entityIds.length !== 1) {
    return null;
  }
  const entityId = entityIds[0];
  return typeof entityId === 'string' && /^(?:0|[1-9][0-9]*)$/.test(entityId) ? entityId : null;
}

export class DisassemblySelectionState {
  #selection = null;

  remember({ entityId, methodOffset, pc = null, sourceRange = null, source = 'structure', instruction = null }) {
    if (typeof entityId !== 'string' || !/^(?:[1-9][0-9]*)$/.test(entityId)) {
      throw new Error('Invalid disassembly selection entity id');
    }
    if (typeof methodOffset !== 'bigint' || methodOffset < 0n || methodOffset > 0xffff_ffff_ffff_ffffn) {
      throw new Error('Invalid disassembly selection method offset');
    }
    if (pc !== null && (!Number.isSafeInteger(pc) || pc < 0 || pc > 0xffff_ffff)) {
      throw new Error('Invalid disassembly selection PC');
    }
    if (typeof source !== 'string' || source.length === 0) {
      throw new Error('Invalid disassembly selection source');
    }
    let retainedRange = null;
    if (sourceRange !== null) {
      if (typeof sourceRange !== 'object' || typeof sourceRange.offset !== 'bigint' ||
          typeof sourceRange.size !== 'bigint' || sourceRange.offset < 0n || sourceRange.size <= 0n) {
        throw new Error('Invalid disassembly selection source range');
      }
      retainedRange = Object.freeze({ offset: sourceRange.offset, size: sourceRange.size });
    }
    this.#selection = Object.freeze({
      entityId, methodOffset, pc, sourceRange: retainedRange, source, instruction,
    });
    return this.#selection;
  }

  forEntity(entityId) {
    return this.#selection?.entityId === entityId ? this.#selection : null;
  }

  clear() {
    this.#selection = null;
  }
}

let elements;

const iconLabels = new Map([
  ['File', 'ABC'], ['Module', 'M'], ['Script', 'S'], ['Group', '·'], ['Namespace', 'N'], ['Class', 'C'],
  ['Interface', 'I'], ['Enum', 'E'], ['Annotation Interface', '@I'], ['Function', 'ƒ'], ['Method', 'ƒ'],
  ['Field', 'F'], ['Enum Member', 'E'], ['Import', '↙'], ['Export', '↗'], ['Annotation', '@'], ['ABC Item', '#'],
]);

let client;
let cfgView;
let hexView;
let metadataView;
let workbenchLayout;
let statisticsView;
let structure;
let selectedId = null;
let selectionGeneration = 0;
let fileSize = 0n;
let fileSelection = null;
let fileSelectionSource = 'structure';
let selectedInstruction = null;
let activeInstructionContext = null;
// Guards every "open another ABC" entry point: only one switch flow may run,
// and none may start before the initial bootstrap presented a document.
let documentSwitching = true;
let toastTimer = null;
const disassemblySelectionState = new DisassemblySelectionState();
const expandedDefaults = new Set();
const collapsedDefaults = new Set();
const filterCollapsed = new Set();
const filterExpanded = new Set();
let modulePrefetch = null;
let moduleStats = new Map();
let treeSortMode = 'file';
const MAX_VIEW_CACHE_WEIGHT = 24 * 1024 * 1024;

function responseWeight(response) {
  if (response[RESPONSE_WIRE_BYTES] !== undefined) {
    return response[RESPONSE_WIRE_BYTES] * 8;
  }
  const instructions = response.function?.instructionCount ?? response.instructions?.length ?? 0;
  const blocks = response.graph?.blocks?.length ?? 0;
  const edges = response.graph?.edges?.length ?? 0;
  return 4096 + instructions * 768 + blocks * 384 + edges * 256;
}

let disassemblyCache = new BoundedResponseCache(MAX_VIEW_CACHE_WEIGHT, responseWeight);
let cfgCache = new BoundedResponseCache(MAX_VIEW_CACHE_WEIGHT, responseWeight);

function element(tag, className, text) {
  const node = document.createElement(tag);
  if (className) {
    node.className = className;
  }
  if (text !== undefined) {
    node.textContent = text;
  }
  return node;
}

function setStatus(text) {
  elements['status-text'].textContent = text;
}

function showError(error) {
  elements['error-state'].textContent = error instanceof Error ? error.message : String(error);
  elements['error-state'].classList.remove('hidden');
  setStatus('Request failed');
}

function hideError() {
  elements['error-state'].classList.add('hidden');
}

// Transient failure notice for document switches; a failed switch keeps the
// previous document, so a full error banner would overstate the damage.
function showToast(message) {
  const toast = elements.toast;
  toast.textContent = message;
  toast.classList.remove('hidden');
  if (toastTimer !== null) {
    clearTimeout(toastTimer);
  }
  toastTimer = setTimeout(() => {
    toastTimer = null;
    toast.classList.add('hidden');
  }, 5000);
}

function toggleNode(node, expandable = true, matchedChildren = false) {
  if (!expandable || !node.children.length) {
    return;
  }
  if (elements['tree-search'].value.trim()) {
    const target = matchedChildren ? filterCollapsed : filterExpanded;
    if (target.has(node.id)) {
      target.delete(node.id);
    } else {
      target.add(node.id);
    }
    renderTree();
    return;
  }
  const target = node.defaultCollapsed ? expandedDefaults : collapsedDefaults;
  if (target.has(node.id)) {
    target.delete(node.id);
  } else {
    target.add(node.id);
  }
  renderTree();
}

function formatBytes(bytes) {
  if (bytes < 1024) {
    return `${bytes} B`;
  }
  if (bytes < 1024 * 1024) {
    return `${(bytes / 1024).toFixed(1)} KB`;
  }
  return `${(bytes / (1024 * 1024)).toFixed(1)} MB`;
}

function moduleSizeLabel(entry) {
  return `${entry.partial ? '≥ ' : ''}${formatBytes(entry.codeBytes)}`;
}

function updateSortButton() {
  const button = elements['tree-sort'];
  const total = modulePrefetch?.totalScopes ?? 0;
  const done = modulePrefetch?.doneScopes ?? 0;
  const computing = total > 0 && done < total;
  button.textContent = treeSortMode === 'file' ? 'File order' : 'Size';
  button.disabled = computing || total === 0;
  button.dataset.tip = computing
    ? `Computing scope sizes ${done}/${total}`
    : (treeSortMode === 'file'
      ? 'Modules in file order — click to sort by method code bytes'
      : 'Modules by method code bytes — click to restore file order');
}

// Patches size labels onto rendered rows without rebuilding the tree, so the
// prefetch progress never disturbs scroll position or selection.
function updateTreeSizeLabels() {
  for (const row of elements['structure-tree'].querySelectorAll('.tree-row')) {
    const entry = moduleStats.get(row.dataset.entityId);
    if (entry === undefined) {
      continue;
    }
    const label = row.querySelector('.tree-size');
    if (label === null) {
      row.append(element('span', 'tree-size', moduleSizeLabel(entry)));
    } else {
      label.textContent = moduleSizeLabel(entry);
    }
  }
}

function startModulePrefetch() {
  modulePrefetch?.cancel();
  moduleStats = new Map();
  treeSortMode = 'file';
  modulePrefetch = new ModuleStatisticsPrefetch({
    client,
    onProgress: () => {
      moduleStats = modulePrefetch.stats;
      updateSortButton();
      updateTreeSizeLabels();
    },
  });
  modulePrefetch.start(structure);
  updateSortButton();
}

function pathFor(node) {
  const names = [];
  let current = node;
  while (current) {
    names.push(current.name);
    current = current.parentId ? structure.nodes.get(current.parentId) : null;
  }
  return names.reverse();
}

function expandAncestors(node) {
  let parent = node.parentId ? structure.nodes.get(node.parentId) : null;
  while (parent !== null && parent !== undefined) {
    if (parent.defaultCollapsed) {
      expandedDefaults.add(parent.id);
    } else {
      collapsedDefaults.delete(parent.id);
    }
    parent = parent.parentId ? structure.nodes.get(parent.parentId) : null;
  }
}

function renderBreadcrumb(node) {
  elements.breadcrumb.replaceChildren();
  pathFor(node).forEach((name, index, names) => {
    elements.breadcrumb.append(element(index === names.length - 1 ? 'strong' : 'span', '', name));
    if (index !== names.length - 1) {
      elements.breadcrumb.append(element('i', '', '›'));
    }
  });
}

function renderTree() {
  if (structure === undefined) {
    return;
  }
  const query = elements['tree-search'].value;
  const moduleOrder = treeSortMode === 'size' && !query.trim()
    ? new Map([...moduleStats].map(([id, entry]) => [id, entry.codeBytes]))
    : null;
  const rows = flattenTree(structure, expandedDefaults, collapsedDefaults, query, filterCollapsed,
    filterExpanded, moduleOrder);
  elements['structure-tree'].replaceChildren();
  if (query.trim() && rows.length === 0) {
    elements['structure-tree'].append(element('div', 'tree-empty', 'No matching declarations'));
    return;
  }
  for (const { node, depth, expanded, expandable, matched, matchedChildren } of rows) {
    const row = element('button', `tree-row${node.executable ? ' executable' : ''}${node.id === selectedId ? ' selected' : ''}${expanded ? ' expanded' : ''}${matched ? ' search-hit' : ''}`);
    row.type = 'button';
    row.role = 'treeitem';
    row.dataset.entityId = node.id;
    row.dataset.tip = `${node.kind} · ${node.role} · ${node.origin} · #${node.id}`;
    for (let indent = 0; indent < depth; indent += 1) {
      row.append(element('span', 'tree-indent'));
    }
    row.append(element('span', 'tree-toggle', expandable ? '›' : ''));
    row.append(element('span', 'tree-icon', iconLabels.get(node.kind) ?? '?'));
    row.append(element('span', 'tree-label', node.name));
    if (node.executable) {
      row.append(element('span', 'tree-role', node.isStatic ? 'static' : 'code'));
    }
    const sizeEntry = moduleStats.get(node.id);
    if (sizeEntry !== undefined) {
      row.append(element('span', 'tree-size', moduleSizeLabel(sizeEntry)));
    }
    row.addEventListener('click', (event) => {
      if (expandable && event.offsetX < 36 + depth * 15) {
        toggleNode(node, expandable, matchedChildren);
        return;
      }
      selectNode(node);
    });
    row.addEventListener('dblclick', () => toggleNode(node, expandable, matchedChildren));
    elements['structure-tree'].append(row);
  }
}

function metric(label, value) {
  const card = element('div', 'metric');
  card.append(element('div', 'metric-label', label));
  card.append(element('div', 'metric-value', String(value)));
  return card;
}

function sameFileRange(left, right) {
  if (left === null || right === null) {
    return false;
  }
  try {
    const canonicalLeft = rangeFromWire(left);
    const canonicalRight = rangeFromWire(right);
    return canonicalLeft.offset === canonicalRight.offset && canonicalLeft.size === canonicalRight.size;
  } catch {
    return false;
  }
}

const ORIGIN_TITLES = {
  PHYSICAL: 'Backed by physical ABC structures (modules, record fields, _ES* entries) — as literally stored in the file',
  RECONSTRUCTED: 'Semantic view rebuilt from bytecode and metadata analysis (class members, fields)',
  INFERRED: 'Deduced from incomplete structural information',
};

function renderOriginBadge(origin) {
  const label = String(origin).toUpperCase();
  const badge = elements['origin-badge'];
  badge.textContent = label;
  const explanation = ORIGIN_TITLES[label];
  if (explanation) {
    badge.dataset.tip = explanation;
  } else {
    badge.removeAttribute('data-tip');
  }
}

function showSemanticNode(node) {
  elements['empty-state'].classList.add('hidden');
  elements['detail-view'].classList.remove('hidden');
  elements['entity-kicker'].textContent = `${node.kind.toUpperCase()} · ${node.role.toUpperCase()}`;
  elements['function-name'].textContent = node.name;
  elements['internal-name'].textContent = node.internalName || node.semanticName || 'No internal name retained';
  renderOriginBadge(node.origin);
  elements.metrics.replaceChildren(
    metric('Kind', node.kind), metric('Role', node.role), metric('Children', node.children.length),
    metric('ABC backing', node.backing.length), metric('Diagnostics', node.diagnostics.length),
    metric('Primary range', node.primaryRange ? `${node.primaryRange.offset} +${node.primaryRange.size}` : '—'),
  );
  elements['instruction-count'].textContent = '';
  elements['instruction-body'].replaceChildren();
  elements['instruction-inspector'].textContent = node.executableTargetCount > 1
    ? `${node.executableTargetCount} executable targets are retained for this entity.`
    : 'This semantic node has no directly executable METHOD/CODE backing.';
  setStatus(`${node.kind} selected`);
}

function renderInstructionInspector(instruction) {
  const inspector = elements['instruction-inspector'];
  inspector.replaceChildren();
  const grid = element('div', 'inspector-grid');
  const values = [
    ['Opcode', `${instruction.opcode} (${instruction.mnemonic})`],
    ['PC', `${instruction.pc} (${instruction.pcHex})`],
    ['ABC file range', `${instruction.fileRange.offset} +${instruction.fileRange.size}`],
    ['Bytes', instruction.bytes || '—'],
    ['Jump target', instruction.jumpTarget ?? '—'],
    ['Control flow', instruction.flowKind],
    ['Can throw', instruction.canThrow ? 'yes' : 'no'],
  ];
  for (const [label, value] of values) {
    const item = element('div', 'inspector-item');
    item.append(element('b', '', `${label}: `), element('span', '', String(value)));
    grid.append(item);
  }
  inspector.append(grid);
  for (const id of instruction.ids) {
    const resolved = id.resolvedOffset ? `, resolved ABC offset ${id.resolvedOffset}` : ', unresolved';
    const detail = element('div', 'id-detail',
      `${id.kind} ID: raw ${id.rawValue} (${id.rawHex}), mode ${id.mode}${resolved}, text “${id.text || '—'}”`);
    const target = metadataTargetForInstructionId(id);
    if (target !== null) {
      const open = element('button', 'open-metadata', 'OPEN IN METADATA');
      open.type = 'button';
      open.addEventListener('click', () => {
        openInstructionMetadata(target).catch(showError);
      });
      detail.append(open);
    }
    inspector.append(detail);
  }
  for (const immediate of instruction.immediates) {
    inspector.append(element('div', 'id-detail',
      `${immediate.kind} immediate: ${immediate.value}, ${immediate.bitWidth}-bit raw ${immediate.rawBits}`));
  }
}

function renderDisassembly(node, response, preferredPc = null) {
  const fn = response.function;
  const methodOffset = BigInt(fn.methodOffset);
  const previousSelection = disassemblySelectionState.forEntity(node.id);
  activeInstructionContext = { entityId: node.id, methodOffset, instructions: response.instructions };
  hexView.setInstructionContext(node.id, response.instructions);
  elements['empty-state'].classList.add('hidden');
  elements['detail-view'].classList.remove('hidden');
  elements['entity-kicker'].textContent = `${node.kind.toUpperCase()} · ${fn.role.toUpperCase()}`;
  elements['function-name'].textContent = fn.name;
  elements['internal-name'].textContent = node.internalName || response.target.name;
  renderOriginBadge(node.origin);
  elements.metrics.replaceChildren(
    metric('METHOD offset', fn.methodOffset), metric('CODE offset', fn.codeOffset),
    metric('Code size', `${fn.codeSize} bytes`), metric('VRegs', fn.vregCount),
    metric('Arguments', fn.argumentCount), metric('Instructions', fn.instructionCount),
  );
  elements['instruction-count'].textContent = `(${fn.instructionCount})`;
  elements['instruction-body'].replaceChildren();
  let retainedInstruction = null;
  let retainedRow = null;
  for (const instruction of response.instructions) {
    const retained = preferredPc === null
      ? selectedInstruction !== null && sameFileRange(instruction.fileRange, selectedInstruction.fileRange)
      : instruction.pc === preferredPc;
    const row = element('tr', `instruction-row${retained ? ' selected' : ''}`);
    row.dataset.pc = String(instruction.pc);
    if (retained) {
      retainedInstruction = instruction;
      retainedRow = row;
    }
    row.append(element('td', 'pc', instruction.pcHex));
    row.append(element('td', 'bytes', instruction.bytes));
    row.append(element('td', 'mnemonic', instruction.mnemonic));
    row.append(element('td', 'operands', instruction.operands || '—'));
    row.append(element('td', '', instruction.ids.length || instruction.immediates.length ? '›' : ''));
    row.addEventListener('click', () => {
      elements['instruction-body'].querySelectorAll('tr').forEach((candidate) => candidate.classList.remove('selected'));
      row.classList.add('selected');
      selectedInstruction = instruction;
      fileSelection = rangeFromWire(instruction.fileRange);
      fileSelectionSource = 'instruction';
      disassemblySelectionState.remember({
        entityId: node.id,
        methodOffset,
        pc: instruction.pc,
        sourceRange: fileSelection,
        source: fileSelectionSource,
        instruction,
      });
      activeInstructionContext = { entityId: node.id, methodOffset, instructions: response.instructions };
      hexView.setInstructionContext(node.id, response.instructions);
      renderInstructionInspector(instruction);
      hexView.revealRange(fileSelection, fileSelectionSource).catch(showError);
    });
    elements['instruction-body'].append(row);
  }
  if (retainedInstruction !== null) {
    selectedInstruction = retainedInstruction;
    const preservesExactSelection = previousSelection?.methodOffset === methodOffset &&
      previousSelection.pc === retainedInstruction.pc;
    disassemblySelectionState.remember({
      entityId: node.id,
      methodOffset,
      pc: retainedInstruction.pc,
      sourceRange: preservesExactSelection && previousSelection.sourceRange !== null
        ? previousSelection.sourceRange
        : rangeFromWire(retainedInstruction.fileRange),
      source: preservesExactSelection ? previousSelection.source : 'instruction',
      instruction: retainedInstruction,
    });
    renderInstructionInspector(retainedInstruction);
    if (preferredPc !== null) {
      retainedRow?.scrollIntoView?.({ block: 'center' });
    }
  } else {
    selectedInstruction = null;
    if (previousSelection?.methodOffset !== methodOffset || previousSelection.pc !== null) {
      disassemblySelectionState.remember({ entityId: node.id, methodOffset });
    }
    elements['instruction-inspector'].textContent =
      'Select an instruction to see its opcode, physical file range and typed ID resolution.';
  }
  setStatus(`Decoded ${fn.instructionCount} real instructions`);
}

function renderCfg(node, response) {
  cfgView.render(node.id, response);
  setStatus(`Built ${response.graph.blocks.length} real basic blocks in C++`);
}

async function requestDisassembly(node, methodOffset) {
  const requestedKey = methodOffset === null
    ? node.id
    : `${node.id}@${canonicalMetadataHex(methodOffset)}`;
  const cached = disassemblyCache.get(requestedKey);
  if (cached !== undefined) {
    return cached;
  }
  const path = methodOffset === null
    ? `/api/v1/disassembly?id=${encodeURIComponent(node.id)}`
    : metadataDisassemblyPath(node.id, methodOffset);
  const response = await client.get(path, 'workbench-disassembly');
  const exactKey = `${node.id}@${canonicalMetadataHex(BigInt(response.function.methodOffset))}`;
  disassemblyCache.set(requestedKey, response);
  disassemblyCache.set(exactKey, response);
  return response;
}

async function requestCfg(node, methodOffset) {
  const key = cfgSelectionKey(node.id, methodOffset);
  const cached = cfgCache.get(key);
  if (cached !== undefined) {
    return cached;
  }
  const response = await client.get(cfgRequestPath(node.id, methodOffset), 'workbench-cfg');
  cfgCache.set(key, response);
  return response;
}

async function loadExecutableNode(node, methodOffset, preferredPc, generation) {
  elements['empty-state'].classList.add('hidden');
  setStatus(`Decoding ${node.name}…`);
  try {
    await loadExecutableWorkbenchViews({
      entityId: node.id,
      requestedMethodOffset: methodOffset,
      requestDisassembly: (_entityId, exactOffset) => requestDisassembly(node, exactOffset),
      requestCfg: (_entityId, exactOffset) => requestCfg(node, exactOffset),
      isCurrent: () => generation === selectionGeneration && selectedId === node.id,
      onBeginLoading: () => {
        cfgView.beginLoading(node.id, node.name, methodOffset);
        activeInstructionContext = null;
        selectedInstruction = null;
        hexView.clearInstructionContext();
        elements['instruction-count'].textContent = '';
        elements['instruction-body'].replaceChildren();
        elements['instruction-inspector'].textContent = 'Loading executable instructions…';
      },
      onDisassembly: (response) => renderDisassembly(node, response, preferredPc),
      onCfg: (response) => renderCfg(node, response),
    });
  } catch (error) {
    if (generation === selectionGeneration && !/stale/i.test(error.message)) {
      cfgView.showUnavailable('CFG is unavailable for the selected executable');
      showError(error);
    }
  }
}

// The instruction inspector only applies to the previously selected method;
// any other selection clears it.
function clearInstructionSelection() {
  activeInstructionContext = null;
  selectedInstruction = null;
  hexView.clearInstructionContext();
}

// Structural scopes present statistics instead of a single disassembly.
function isSemanticScopeNode(node) {
  return ['File', 'Module', 'Script', 'Class', 'Interface'].includes(node.kind) ||
    (node.kind === 'Namespace' && !node.executable);
}

// Presents a node that has no disassembly: the semantic detail view plus a
// CFG panel explaining why no graph is available.
function showNonExecutableNode(node, message) {
  clearInstructionSelection();
  showSemanticNode(node);
  cfgView.showUnavailable(message);
}

// Marks the node as selected and refreshes the UI (error banner, tree,
// breadcrumb) around it.
function beginNodeSelection(node, preserveDisassemblySelection) {
  const changedNode = selectedId !== node.id;
  selectedId = node.id;
  if (changedNode) {
    if (!preserveDisassemblySelection) {
      disassemblySelectionState.clear();
    }
    clearInstructionSelection();
  }
  hideError();
  renderTree();
  renderBreadcrumb(node);
}

async function selectNode(node, {
  preserveHexSelection = false,
  preserveDisassemblySelection = false,
  methodOffset = null,
  preferredPc = null,
} = {}) {
  const generation = ++selectionGeneration;
  statisticsView.clear();
  beginNodeSelection(node, preserveDisassemblySelection);
  if (!preserveHexSelection) {
    fileSelection = preferredNodeRange(node, fileSize);
    fileSelectionSource = 'structure';
  }
  if (isSemanticScopeNode(node)) {
    showNonExecutableNode(node, `${node.kind} “${node.name}” is a statistics scope`);
    const hexUpdate = preserveHexSelection
      ? Promise.resolve()
      : hexView.revealRange(fileSelection, fileSelectionSource);
    await Promise.all([statisticsView.select(node), hexUpdate]);
    return;
  }
  if (!node.executable) {
    showNonExecutableNode(node, `${node.kind} “${node.name}” has no executable METHOD/CODE target`);
    if (!preserveHexSelection) {
      await hexView.revealRange(fileSelection, fileSelectionSource);
    }
    return;
  }

  if (methodOffset === null) {
    const retained = disassemblySelectionState.forEntity(node.id);
    if (retained !== null) {
      methodOffset = retained.methodOffset;
      preferredPc = retained.pc;
      selectedInstruction = retained.instruction;
      if (retained.sourceRange !== null) {
        fileSelection = retained.sourceRange;
        fileSelectionSource = retained.source;
      }
    }
  }
  const hexUpdate = preserveHexSelection
    ? Promise.resolve()
    : hexView.revealRange(fileSelection, fileSelectionSource);
  await Promise.all([loadExecutableNode(node, methodOffset, preferredPc, generation), hexUpdate]);
}

async function openInstructionMetadata(target) {
  metadataView.revealObject(target);
  flashPanel('metadata-panel');
}

function flashPanel(panelId) {
  const panel = document.getElementById(panelId);
  if (panel === null) {
    return;
  }
  panel.classList.remove('navigation-target');
  requestAnimationFrame(() => {
    panel.classList.add('navigation-target');
    globalThis.setTimeout(() => panel.classList.remove('navigation-target'), 900);
  });
}

async function handleMetadataStructure({ entityId }) {
  const node = structure.nodes.get(entityId);
  if (node === undefined) {
    throw new Error(`Structure entity ${entityId} is unavailable`);
  }
  elements['tree-search'].value = '';
  expandAncestors(node);
  await selectNode(node);
  const row = elements['structure-tree'].querySelector(`[data-entity-id="${entityId}"]`);
  row?.scrollIntoView?.({ block: 'nearest' });
}

async function handleMetadataHex({ range, entityIds, entityIdsTruncated, source }) {
  fileSelection = rangeFromWire(range);
  fileSelectionSource = source;
  const relatedId = singleCompleteMetadataEntityId({ entityIds, entityIdsTruncated });
  const related = relatedId === null ? null : structure.nodes.get(relatedId);
  hideError();
  if (related !== null && related !== undefined) {
    elements['tree-search'].value = '';
    expandAncestors(related);
    await selectNode(related, { preserveHexSelection: true });
  }
  await hexView.revealRange(fileSelection, fileSelectionSource);
  flashPanel('hex-panel');
}

async function handleMetadataDisassembly({ entityId, methodOffset, pc, sourceRange }) {
  const node = structure.nodes.get(entityId);
  if (node === undefined) {
    throw new Error(`Disassembly entity ${entityId} is unavailable`);
  }
  elements['tree-search'].value = '';
  expandAncestors(node);
  const canonicalRange = sourceRange === null ? null : rangeFromWire(sourceRange);
  if (canonicalRange !== null) {
    fileSelection = canonicalRange;
    fileSelectionSource = 'metadata';
  }
  selectedInstruction = null;
  disassemblySelectionState.remember({
    entityId,
    methodOffset,
    pc,
    sourceRange: canonicalRange,
    source: 'metadata',
  });
  hideError();
  await selectNode(node, {
    preserveHexSelection: sourceRange !== null,
    preserveDisassemblySelection: true,
    methodOffset,
    preferredPc: pc,
  });
  flashPanel('disassembly-panel');
}

async function handleHexLocation({ offset, instruction, instructionEntityId, structureMatch }) {
  fileSelection = { offset, size: 1n };
  fileSelectionSource = 'hex';
  if (instruction !== null && instructionEntityId === selectedId &&
      activeInstructionContext?.entityId === selectedId) {
    selectedInstruction = instruction;
    disassemblySelectionState.remember({
      entityId: selectedId,
      methodOffset: activeInstructionContext.methodOffset,
      pc: instruction.pc,
      sourceRange: rangeFromWire(instruction.fileRange),
      source: 'hex',
      instruction,
    });
    elements['instruction-body'].querySelectorAll('tr').forEach((row) => {
      row.classList.toggle('selected', row.dataset.pc === String(instruction.pc));
    });
    renderInstructionInspector(instruction);
    return;
  }

  selectedInstruction = null;
  const matchedNode = structureMatch === null ? null : structure.nodes.get(structureMatch.entityId);
  if (matchedNode !== undefined && matchedNode !== null) {
    await selectNode(matchedNode, { preserveHexSelection: true });
  }
}

function handleCfgBlock(block) {
  const instruction = block.instructions?.[0] ?? null;
  if (instruction === null || activeInstructionContext === null) {
    return;
  }
  selectedInstruction = instruction;
  fileSelection = rangeFromWire(instruction.fileRange);
  fileSelectionSource = 'cfg';
  disassemblySelectionState.remember({
    entityId: activeInstructionContext.entityId,
    methodOffset: activeInstructionContext.methodOffset,
    pc: instruction.pc,
    sourceRange: fileSelection,
    source: fileSelectionSource,
    instruction,
  });
  elements['instruction-body'].querySelectorAll('tr').forEach((row) => {
    row.classList.toggle('selected', row.dataset.pc === String(instruction.pc));
  });
  renderInstructionInspector(instruction);
  hexView.revealRange(fileSelection, fileSelectionSource).catch(showError);
}

function preferredInitialNode() {
  const nodes = [...structure.nodes.values()];
  return nodes.find((node) => node.kind === 'Method' && node.name === 'create' &&
    structure.nodes.get(node.parentId)?.name === 'BetaModel') ?? nodes.find((node) => node.executable);
}

function setSwitching(on) {
  documentSwitching = on;
  elements['file-chip'].disabled = on;
  elements['landing-open'].disabled = on;
}

let currentTheme = 'dark';

// Applies a color theme and refreshes the views that paint colors outside
// CSS (the CFG canvas markers). The default document carries no data-theme
// attribute and resolves to the dark tokens, matching this initial state.
function setTheme(theme, { persist = true } = {}) {
  currentTheme = theme;
  applyTheme(theme);
  const button = elements['theme-toggle'];
  elements['theme-label'].textContent = THEME_LABELS[theme];
  button.dataset.tip = `Color theme: ${THEME_LABELS[theme]} — click to switch`;
  button.setAttribute('aria-label', `Switch color theme, current: ${THEME_LABELS[theme]}`);
  cfgView?.refreshTheme();
  if (persist && client) {
    // The port changes on every restart, so the preference lives on the
    // server; failing to save it keeps the theme for this page only.
    client.put('/api/v1/preferences', { theme }, 'preferences')
      .catch((error) => setStatus(`Theme not saved: ${error.message}`));
  }
}

// Restores the persisted theme before anything else is presented, so the
// landing page and the first document already carry the user's choice.
async function restorePersistedTheme() {
  try {
    const preferences = await client.get('/api/v1/preferences', 'preferences');
    if (isTheme(preferences?.theme)) {
      setTheme(preferences.theme, { persist: false });
    }
  } catch {
    // Preferences are cosmetic: an unavailable store keeps the dark default.
  }
}

function hideLanding() {
  elements['landing-state'].classList.add('hidden');
  // The empty state has no panel boundary to drag: hide the separators
  // instead of relying on the landing overlay to cover them (they paint
  // above it: separator z-index exceeds the landing z-index).
  elements.workbench.classList.remove('workbench-idle');
}

function renderLandingRecents(entries) {
  const list = elements['landing-recent'];
  list.replaceChildren();
  if (entries.length === 0) {
    list.append(element('div', 'landing-recent-empty', 'No recently opened files'));
    return;
  }
  for (const entry of entries) {
    const item = element('button', 'landing-recent-item');
    item.type = 'button';
    item.dataset.tip = entry.path;
    item.append(element('span', 'landing-recent-name', entry.fileName));
    item.append(element('span', 'landing-recent-path', entry.path));
    item.addEventListener('click', () => {
      void openPathFlow(entry.path);
    });
    list.append(item);
  }
}

// Shows the empty workbench landing: the session has no document yet, so the
// page offers the native picker and the server-side recent list instead.
async function showLanding() {
  elements['file-name'].textContent = 'No ABC open';
  elements['document-meta'].textContent = '';
  elements['node-count'].textContent = '—';
  elements['landing-state'].classList.remove('hidden');
  elements.workbench.classList.add('workbench-idle');
  setStatus('No ABC file is open');
  try {
    renderLandingRecents(validateRecentFiles(
      await client.get('/api/v1/recent', 'landing-recent', { timeoutMs: SWITCH_REQUEST_TIMEOUT_MS })));
  } catch {
    // The recent list is auxiliary; a landing that cannot list it still opens files.
    renderLandingRecents([]);
  }
}

// Drops every piece of selection and cache state derived from the previous
// document, so the workbench starts from a clean slate.
function resetDocumentState() {
  ++selectionGeneration;
  selectedId = null;
  selectedInstruction = null;
  activeInstructionContext = null;
  fileSelection = null;
  fileSelectionSource = 'structure';
  disassemblySelectionState.clear();
  expandedDefaults.clear();
  collapsedDefaults.clear();
  filterCollapsed.clear();
  filterExpanded.clear();
  modulePrefetch?.cancel();
  moduleStats = new Map();
  treeSortMode = 'file';
  updateSortButton();
  disassemblyCache = new BoundedResponseCache(MAX_VIEW_CACHE_WEIGHT, responseWeight);
  cfgCache = new BoundedResponseCache(MAX_VIEW_CACHE_WEIGHT, responseWeight);
}

// Clears the rendered panels back to their placeholder content.
function resetWorkbenchUi() {
  hideError();
  elements['tree-search'].value = '';
  elements['empty-state'].classList.remove('hidden');
  elements['detail-view'].classList.add('hidden');
  elements['instruction-count'].textContent = '';
  elements['instruction-body'].replaceChildren();
  elements['instruction-inspector'].textContent =
    'Select an instruction to see its opcode, physical file range and typed ID resolution.';
  elements.breadcrumb.replaceChildren(
    element('span', '', 'ABC'), element('i', '', '›'), element('strong', '', 'Select a declaration'));
}

// Publishes the document identity in the header bar.
function presentDocumentHeader(documentModel, structureModel) {
  elements['file-name'].textContent = documentModel.fileName;
  elements['document-meta'].textContent = documentMetaText(documentModel);
  const buildTooltip = buildModeTooltip(documentModel);
  if (buildTooltip === null) {
    elements['document-meta'].removeAttribute('data-tip');
  } else {
    elements['document-meta'].setAttribute('data-tip', buildTooltip);
  }
  elements['node-count'].textContent = `${structureModel.nodes.length} nodes`;
}

// Creates the resizable workbench once; later documents reuse the mounted
// layout and its panel observers.
function mountWorkbenchLayout() {
  if (workbenchLayout !== undefined) {
    return;
  }
  workbenchLayout = new WorkbenchLayout({
    root: elements.workbench,
    onPanelResize: (panelId, width) => {
      if (panelId === 'hex-panel') {
        hexView.resize(width);
      }
      if (panelId === 'metadata-panel') {
        metadataView.resize(width);
      }
      if (panelId === 'cfg-panel') {
        cfgView.resize();
      }
    },
    onPresentationChange: (panelId, presented) => {
      if (panelId === 'metadata-panel') {
        metadataView.setPresented(presented);
      }
      if (presented && panelId === 'cfg-panel') {
        cfgView.resize();
      }
    },
  });
  workbenchLayout.mount();
}

// Presents the document the session currently publishes. Runs once for the
// initial bootstrap and again for every completed document switch; everything
// derived from the previous document is rebuilt from scratch.
async function presentDocument({ documentModel, structureModel }) {
  hideLanding();
  resetDocumentState();
  resetWorkbenchUi();
  presentDocumentHeader(documentModel, structureModel);
  fileSize = BigInt(documentModel.fileSize);
  statisticsView.clear();
  metadataView.reset();
  cfgView.reset();
  hexView.clearInstructionContext();
  hexView.setFileSize(fileSize);
  setStatus('Indexing Structure in browser…');
  await new Promise((resolve) => requestAnimationFrame(() => setTimeout(resolve, 0)));
  structure = indexStructure(structureModel);
  setStatus('Rendering workbench…');
  await new Promise((resolve) => requestAnimationFrame(() => setTimeout(resolve, 0)));
  renderTree();
  startModulePrefetch();
  metadataView.mount();
  hexView.show();
  mountWorkbenchLayout();
  setStatus('Local ABC loaded');
  const initial = preferredInitialNode();
  if (initial) {
    await selectNode(initial);
  }
}

// After the backend accepted a switch, the session reloads in the background
// while still serving the previous document. Wait for the terminal status:
// a `lastError` reports that the switch failed and the previous document
// stayed; without one the new document is ready and gets presented.
async function completeDocumentSwitch() {
  const status = await pollSession({ client, onStage: setStatus });
  if (status.state === 'empty') {
    // The backend never withdraws a published document; this only guards a
    // session that was restarted between accepting and completing a switch.
    await showLanding();
    return;
  }
  if (status.lastError !== null) {
    showToast(status.lastError);
    setStatus('ABC switch failed; the previous document is still open');
    return;
  }
  const models = await fetchSessionModels({ client, onStage: setStatus });
  await presentDocument(models);
  setStatus(`Opened ${models.documentModel.fileName}`);
}

// Opens the native file picker. The dialog blocks its own backend worker
// thread for as long as the user keeps it open, so the flow stays async here
// and simply polls until the picker settles.
async function startPickFlow() {
  if (documentSwitching) {
    return;
  }
  setSwitching(true);
  hideError();
  try {
    await client.post('/api/v1/pick', undefined, 'pick-start', { timeoutMs: SWITCH_REQUEST_TIMEOUT_MS });
    const waiting = 'Choose an ABC file in the system dialog…';
    setStatus(waiting);
    const status = await pollPickStatus({ client, onWaiting: () => setStatus(waiting) });
    if (status.state === 'selected') {
      await completeDocumentSwitch();
    } else if (status.state === 'failed') {
      showToast(status.error ?? 'The file dialog failed');
      setStatus('File dialog failed');
    } else {
      // Cancelled (or idle after a backend restart): nothing to report.
      setStatus('No ABC file selected');
    }
  } catch (error) {
    showError(error);
    setStatus('File dialog failed');
  } finally {
    setSwitching(false);
  }
}

// Reopens a server-known path: a recent file from the landing list.
async function openPathFlow(path) {
  if (documentSwitching) {
    return;
  }
  setSwitching(true);
  hideError();
  setStatus('Opening ABC file…');
  try {
    await client.post('/api/v1/open', { path }, 'document-open', { timeoutMs: SWITCH_REQUEST_TIMEOUT_MS });
    await completeDocumentSwitch();
  } catch (error) {
    showError(error);
    setStatus('Open failed');
  } finally {
    setSwitching(false);
  }
}

// Creates the workbench views once the DOM and the API client are ready.
function createStatisticsView() {
  statisticsView = new StatisticsView({
    client,
    elements: {
      root: elements['detail-view'], metrics: elements.metrics, inspector: elements['instruction-inspector'],
    },
  });
}

function createMetadataView() {
  metadataView = new MetadataView({
    client,
    elements: {
      root: elements['metadata-view'],
      kind: elements['metadata-kind'],
      searchForm: elements['metadata-search-form'],
      search: elements['metadata-search'],
      summary: elements['metadata-summary'],
      progress: elements['metadata-progress'],
      progressBar: elements['metadata-progress-bar'],
      notice: elements['metadata-notice'],
      list: elements['metadata-list'],
      objects: elements['metadata-objects'],
      loadMore: elements['metadata-load-more'],
      selection: elements['metadata-selection'],
      detail: elements['metadata-detail'],
      references: elements['metadata-references'],
      referencesBack: elements['metadata-references-back'],
      referencesNext: elements['metadata-references-next'],
    },
    onStatus: setStatus,
    onError: showError,
    onClearError: hideError,
    onStructure: (payload) => {
      handleMetadataStructure(payload).catch(showError);
    },
    onDisassembly: (payload) => {
      handleMetadataDisassembly(payload).catch(showError);
    },
    onHex: (payload) => {
      handleMetadataHex(payload).catch(showError);
    },
  });
}

function createHexView() {
  hexView = new HexView({
    client,
    elements: {
      root: elements['hex-view'],
      jumpForm: elements['hex-jump-form'],
      summary: elements['hex-range-summary'],
      offset: elements['hex-offset'],
      notice: elements['hex-notice'],
      viewport: elements['hex-viewport'],
      spacer: elements['hex-spacer'],
      rows: elements['hex-rows'],
    },
    onStatus: setStatus,
    onError: showError,
    onClearError: hideError,
    onLocation: handleHexLocation,
  });
}

function createCfgView() {
  cfgView = new CfgView({
    client,
    elements: {
      root: elements['cfg-view'],
      summary: elements['cfg-summary'],
      viewport: elements['cfg-viewport'],
      stage: elements['cfg-stage'],
      edgeLayer: elements['cfg-edge-layer'],
      nodeLayer: elements['cfg-node-layer'],
      zoomLabel: elements['cfg-zoom-label'],
      zoomIn: elements['cfg-zoom-in'],
      zoomOut: elements['cfg-zoom-out'],
      actualSize: elements['cfg-actual-size'],
      fit: elements['cfg-fit'],
      resetCurrent: elements['cfg-reset-current'],
      resetLayout: elements['cfg-reset-layout'],
    },
    onStatus: setStatus,
    onError: showError,
    onClearError: hideError,
    onBlockSelect: handleCfgBlock,
  });
}

// Wires the global UI listeners: document opening, the theme toggle and
// the structure tree search/sort controls.
function bindGlobalEvents() {
  elements['file-chip'].addEventListener('click', () => {
    void startPickFlow();
  });
  elements['landing-open'].addEventListener('click', () => {
    void startPickFlow();
  });
  elements['theme-toggle'].addEventListener('click', () => {
    setTheme(nextTheme(currentTheme));
  });
  elements['tree-search'].addEventListener('input', () => {
    if (!elements['tree-search'].value.trim()) {
      filterCollapsed.clear();
      filterExpanded.clear();
    }
    renderTree();
  });
  elements['tree-sort'].addEventListener('click', () => {
    if (elements['tree-sort'].disabled) {
      return;
    }
    treeSortMode = treeSortMode === 'file' ? 'size' : 'file';
    updateSortButton();
    renderTree();
  });
}

export async function start() {
  try {
    elements = Object.fromEntries(ELEMENT_IDS.map((id) => [id, document.getElementById(id)]));
    const token = bootstrapToken(location.hash, sessionStorage);
    history.replaceState(null, '', `${location.pathname}${location.search}`);
    client = new ApiClient(token);
    installTooltips();
    await restorePersistedTheme();
    createStatisticsView();
    createMetadataView();
    createHexView();
    createCfgView();
    bindGlobalEvents();
    const outcome = await loadSessionModels({ client, onStage: setStatus });
    if (outcome.empty) {
      await showLanding();
    } else {
      await presentDocument(outcome);
    }
    // Release the entry points only after a document or the landing page is
    // presented: picking while the bootstrap is still loading would be
    // rejected by the backend as a concurrent document load.
    setSwitching(false);
  } catch (error) {
    elements['file-name'].textContent = 'ABC opening failed';
    showError(error);
  }
}
