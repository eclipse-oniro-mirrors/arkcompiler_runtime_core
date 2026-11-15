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

export function indexStructure(structure) {
  return {
    roots: structure.roots,
    nodes: new Map(structure.nodes.map((node) => [node.id, node])),
  };
}

function nodeMatches(node, query) {
  // "#N" performs an exact entity-id lookup (same notation as Panel 5's
  // "Structure #N" buttons). Only digit-only suffixes take this path:
  // internal names may legitimately start with '#' (e.g. "#~@0>name" or
  // "#run.task") and keep flowing through the field search below.
  if (query.startsWith('#') && /^\d+$/.test(query.slice(1))) {
    return String(node.id) === query.slice(1);
  }
  return ['name', 'semanticName', 'internalName', 'kind']
    .some((field) => String(node[field] ?? '').toLocaleLowerCase().includes(query));
}

// moduleOrder: Map<id, codeBytes> — when set, Module/Script children of File
// roots are sorted by descending code bytes (scopes without a value keep
// their original relative order after the sized ones). Filtering ignores it.
function sortedModuleChildren(ids, moduleOrder, nodes) {
  const scored = ids.map((id, index) => {
    const node = nodes.get(id);
    const moduleLike = node !== undefined && (node.kind === 'Module' || node.kind === 'Script');
    const bytes = moduleLike ? moduleOrder.get(id) : undefined;
    return { id, rank: bytes === undefined ? 1 : 0, bytes: bytes ?? 0, index };
  });
  scored.sort((a, b) => {
    if (a.rank !== b.rank) {
      return a.rank - b.rank;
    }
    if (a.rank === 0) {
      return b.bytes - a.bytes;
    }
    return a.index - b.index;
  });
  return scored.map((entry) => entry.id);
}

// Depth-first pass collecting every node with its match flags; an included
// node then marks its ancestors as having an included child.
function collectSearchEntries(structure, normalizedQuery) {
  const entries = [];
  const stack = structure.roots.map((id) => ({ id, depth: 0, parent: -1 })).reverse();
  while (stack.length) {
    const { id, depth, parent } = stack.pop();
    const node = structure.nodes.get(id);
    if (!node) {
      continue;
    }
    const index = entries.length;
    const matched = nodeMatches(node, normalizedQuery);
    entries.push({ node, depth, parent, included: matched, matched, hasIncludedChild: false });
    for (let child = node.children.length - 1; child >= 0; child -= 1) {
      stack.push({ id: node.children[child], depth: depth + 1, parent: index });
    }
  }
  for (let index = entries.length - 1; index >= 0; index -= 1) {
    if (entries[index].included && entries[index].parent >= 0) {
      const parent = entries[entries[index].parent];
      parent.included = true;
      parent.hasIncludedChild = true;
    }
  }
  return entries;
}

// Emits the filtered rows: included subtrees and branches the user explicitly
// expanded stay visible, every other entry is hidden.
function emitFilteredRows(entries, filterCollapsed, filterExpanded) {
  const rows = [];
  const childIndices = new Map();
  for (let index = 0; index < entries.length; index += 1) {
    const parent = entries[index].parent;
    if (parent < 0) {
      continue;
    }
    if (!childIndices.has(parent)) {
      childIndices.set(parent, []);
    }
    childIndices.get(parent).push(index);
  }
  const visitStack = [];
  for (let index = entries.length - 1; index >= 0; index -= 1) {
    if (entries[index].parent < 0) {
      visitStack.push({ index, inExplore: false });
    }
  }
  while (visitStack.length) {
    const { index, inExplore } = visitStack.pop();
    const entry = entries[index];
    if (!entry.included && !inExplore) {
      continue;
    }
    const expanded = (entry.included && entry.hasIncludedChild)
      ? !filterCollapsed.has(entry.node.id)
      : filterExpanded.has(entry.node.id);
    const expandable = entry.node.children.length > 0;
    rows.push({
      node: entry.node,
      depth: entry.depth,
      expandable,
      expanded: expandable && expanded,
      matched: entry.matched,
      matchedChildren: entry.hasIncludedChild,
    });
    if (expanded) {
      const exploreNext = inExplore || filterExpanded.has(entry.node.id);
      const children = childIndices.get(index) ?? [];
      for (let child = children.length - 1; child >= 0; child -= 1) {
        visitStack.push({ index: children[child], inExplore: exploreNext });
      }
    }
  }
  return rows;
}

// Flattens the tree with no query: every expanded node's children follow it in
// depth-first order, honoring default collapse state and the module order.
function flattenDefault(structure, expandedDefaults, collapsedDefaults, moduleOrder) {
  const rows = [];
  const stack = structure.roots.map((id) => ({ id, depth: 0 })).reverse();
  while (stack.length) {
    const { id, depth } = stack.pop();
    const node = structure.nodes.get(id);
    if (!node) {
      continue;
    }
    const isExpanded = node.defaultCollapsed
      ? expandedDefaults.has(node.id)
      : !collapsedDefaults.has(node.id);
    rows.push({ node, depth, expandable: node.children.length > 0,
      expanded: isExpanded && node.children.length > 0, matched: false, matchedChildren: false });
    if (isExpanded) {
      let children = node.children;
      if (moduleOrder !== null && node.kind === 'File') {
        children = sortedModuleChildren(children, moduleOrder, structure.nodes);
      }
      for (let child = children.length - 1; child >= 0; child -= 1) {
        stack.push({ id: children[child], depth: depth + 1 });
      }
    }
  }
  return rows;
}

export function flattenTree(structure, expandedDefaults, collapsedDefaults = new Set(), query = '',
  filterCollapsed = new Set(), filterExpanded = new Set(), moduleOrder = null) {
  const normalizedQuery = String(query).trim().toLocaleLowerCase();
  if (normalizedQuery) {
    return emitFilteredRows(collectSearchEntries(structure, normalizedQuery), filterCollapsed, filterExpanded);
  }
  return flattenDefault(structure, expandedDefaults, collapsedDefaults, moduleOrder);
}
