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

import { flattenTree, indexStructure } from '../src/tree-model.js';

test('tree flattening preserves backend order and default collapsed groups', () => {
  const structure = {
    roots: ['1'],
    nodes: [
      { id: '1', name: '<hostile>', kind: 'File', children: ['2', '4'], defaultCollapsed: false },
      { id: '2', name: 'Exports', kind: 'Group', children: ['3'], defaultCollapsed: true },
      { id: '3', name: '*', kind: 'Export', children: [], defaultCollapsed: false },
      { id: '4', name: 'create', kind: 'Method', children: [], defaultCollapsed: false },
    ],
  };
  const indexed = indexStructure(structure);

  assert.deepEqual(
    flattenTree(indexed, new Set()).map(({ node, depth }) => [node.name, depth]),
    [
      ['<hostile>', 0],
      ['Exports', 1],
      ['create', 1],
    ],
  );
  assert.deepEqual(
    flattenTree(indexed, new Set(['2'])).map(({ node }) => node.name),
    ['<hostile>', 'Exports', '*', 'create'],
  );
});

function searchFixture() {
  return indexStructure({
    roots: ['file'],
    nodes: [
      { id: 'file', name: 'sample.abc', kind: 'File', children: ['module', 'other'], defaultCollapsed: false },
      { id: 'module', name: 'Hidden Module', kind: 'Module', children: ['class', 'sibling'], defaultCollapsed: true, parentId: 'file' },
      { id: 'class', name: 'Worker', semanticName: 'Worker API', kind: 'Class', children: ['method'], defaultCollapsed: true, parentId: 'module' },
      { id: 'method', name: 'run-task(v1)', internalName: '#run.task', kind: 'Function', children: [], defaultCollapsed: false, parentId: 'class' },
      { id: 'sibling', name: 'Unrelated', kind: 'Class', children: [], defaultCollapsed: false, parentId: 'module' },
      { id: 'other', name: 'Elsewhere', kind: 'Namespace', children: [], defaultCollapsed: false, parentId: 'file' },
    ],
  });
}

test('search finds a hidden function and returns only its ancestor path at true depths', () => {
  const rows = flattenTree(searchFixture(), new Set(), new Set(), 'run-task');
  assert.deepEqual(rows.map(({ node, depth }) => [node.id, depth]), [
    ['file', 0], ['module', 1], ['class', 2], ['method', 3],
  ]);
  assert.deepEqual(rows.map(({ expanded }) => expanded), [true, true, true, false]);
});

test('search does not mutate expansion state and clearing restores collapsed visibility', () => {
  const expanded = new Set();
  const collapsed = new Set(['file']);
  flattenTree(searchFixture(), expanded, collapsed, 'Worker API');
  assert.deepEqual([...expanded], []);
  assert.deepEqual([...collapsed], ['file']);
  assert.deepEqual(flattenTree(searchFixture(), expanded, collapsed).map(({ node }) => node.id), ['file']);
});

test('matching a parent filters out its unrelated descendants', () => {
  assert.deepEqual(
    flattenTree(searchFixture(), new Set(), new Set(), 'hidden module').map(({ node }) => node.id),
    ['file', 'module'],
  );
});

test('search honors manually collapsed nodes by hiding their matched descendants', () => {
  const rows = flattenTree(searchFixture(), new Set(), new Set(), 'run-task', new Set(['module']));
  assert.deepEqual(rows.map(({ node }) => node.id), ['file', 'module']);
  assert.deepEqual(rows.map(({ expandable, expanded }) => [expandable, expanded]), [[true, true], [true, false]]);
});

test('search explore-expansion reveals unmatched children of a matched node', () => {
  const rows = flattenTree(searchFixture(), new Set(), new Set(), 'hidden module', new Set(), new Set(['module']));
  assert.deepEqual(rows.map(({ node }) => node.id), ['file', 'module', 'class', 'sibling']);
  assert.deepEqual(rows.map(({ expandable, expanded }) => [expandable, expanded]),
    [[true, true], [true, true], [true, false], [false, false]]);
  assert.deepEqual(rows.map(({ matchedChildren }) => matchedChildren), [true, false, false, false]);
});

test('search explore-expansion continues into deeper levels on demand', () => {
  const rows = flattenTree(searchFixture(), new Set(), new Set(), 'hidden module',
    new Set(), new Set(['module', 'class']));
  assert.deepEqual(rows.map(({ node }) => node.id), ['file', 'module', 'class', 'method', 'sibling']);
  assert.equal(rows[3].expanded, false);
});

test('search track separation ignores explore-set leftovers for matched-path nodes', () => {
  const rows = flattenTree(searchFixture(), new Set(), new Set(), 'run-task',
    new Set(['class']), new Set(['class']));
  assert.deepEqual(rows.map(({ node }) => node.id), ['file', 'module', 'class']);
  assert.equal(rows[2].expanded, false);
});

test('search track separation ignores collapse-set leftovers for explore nodes', () => {
  const rows = flattenTree(searchFixture(), new Set(), new Set(), 'hidden module',
    new Set(['module']), new Set(['module']));
  assert.deepEqual(rows.map(({ node }) => node.id), ['file', 'module', 'class', 'sibling']);
});

test('search keeps the manually collapsed node itself visible with a collapsed chevron', () => {
  const rows = flattenTree(searchFixture(), new Set(), new Set(), 'run-task', new Set(['class']));
  assert.deepEqual(rows.map(({ node }) => node.id), ['file', 'module', 'class']);
  assert.deepEqual(rows.map(({ expandable, expanded }) => [expandable, expanded]),
    [[true, true], [true, true], [true, false]]);
});

test('expandable marks matched-path containers and plain leaves in both modes', () => {
  const filtered = flattenTree(searchFixture(), new Set(), new Set(), 'run-task');
  assert.deepEqual(filtered.map(({ node, expandable }) => [node.id, expandable]),
    [['file', true], ['module', true], ['class', true], ['method', false]]);
  const plain = flattenTree(searchFixture(), new Set());
  assert.deepEqual(plain.map(({ node, expandable }) => [node.id, expandable]),
    [['file', true], ['module', true], ['other', false]]);
});

test('search marks a parent expanded when a later child matches after an excluded sibling', () => {
  const rows = flattenTree(searchFixture(), new Set(), new Set(), 'Unrelated');
  assert.deepEqual(rows.map(({ node }) => node.id), ['file', 'module', 'sibling']);
  assert.equal(rows[1].expanded, true);
});

test('search trims input and matches all supported fields case-insensitively as literal text', () => {
  const fixture = searchFixture();
  assert.equal(flattenTree(fixture, new Set(), new Set(), '  WORKER api ').at(-1).node.id, 'class');
  assert.equal(flattenTree(fixture, new Set(), new Set(), ' #RUN.TASK ').at(-1).node.id, 'method');
  assert.equal(flattenTree(fixture, new Set(), new Set(), ' FUNCTION ').at(-1).node.id, 'method');
  assert.deepEqual(flattenTree(fixture, new Set(), new Set(), 'run.task['), []);
});

test('search with #id matches the node id exactly and keeps its ancestor path', () => {
  const fixture = indexStructure({
    roots: ['1'],
    nodes: [
      { id: '1', name: 'modules.abc', kind: 'File', children: ['2', '9'], defaultCollapsed: false },
      { id: '2', name: 'Card', kind: 'Module', children: ['1598'], defaultCollapsed: true, parentId: '1' },
      { id: '1598', name: 'initializeModifier', kind: 'Method', children: [], defaultCollapsed: false, parentId: '2' },
      { id: '9', name: 'Other', kind: 'Module', children: [], defaultCollapsed: false, parentId: '1' },
    ],
  });
  assert.deepEqual(
    flattenTree(fixture, new Set(), new Set(), '#1598').map(({ node }) => node.id),
    ['1', '2', '1598'],
  );
  assert.deepEqual(flattenTree(fixture, new Set(), new Set(), '#159'), []);
  assert.deepEqual(flattenTree(fixture, new Set(), new Set(), '1598'), []);
});

test('search with a # prefix and non-digit suffix falls back to the field search', () => {
  const fixture = searchFixture();
  assert.deepEqual(
    flattenTree(fixture, new Set(), new Set(), '#run').map(({ node }) => node.id),
    ['file', 'module', 'class', 'method'],
  );
});

test('search output marks self-matched rows and leaves ancestor-context rows unmarked', () => {
  const fixture = searchFixture();
  assert.deepEqual(
    flattenTree(fixture, new Set(), new Set(), 'hidden module').map(({ node, matched }) => [node.id, !!matched]),
    [['file', false], ['module', true]],
  );
  const idFixture = indexStructure({
    roots: ['1'],
    nodes: [
      { id: '1', name: 'modules.abc', kind: 'File', children: ['2'], defaultCollapsed: false },
      { id: '2', name: 'Card', kind: 'Module', children: ['1598'], defaultCollapsed: true, parentId: '1' },
      { id: '1598', name: 'initializeModifier', kind: 'Method', children: [], defaultCollapsed: false, parentId: '2' },
    ],
  });
  assert.deepEqual(
    flattenTree(idFixture, new Set(), new Set(), '#1598').map(({ node, matched }) => [node.id, !!matched]),
    [['1', false], ['2', false], ['1598', true]],
  );
  assert.equal(
    flattenTree(idFixture, new Set()).every(({ matched }) => matched === false),
    true,
  );
});

test('search handles a deeply nested hierarchy without recursive traversal', () => {
  const count = 20_000;
  const nodes = Array.from({ length: count }, (_, index) => ({
    id: String(index), name: index === count - 1 ? 'needle' : `node-${index}`, kind: 'Class',
    children: index + 1 < count ? [String(index + 1)] : [],
    parentId: index === 0 ? undefined : String(index - 1), defaultCollapsed: true,
  }));
  const rows = flattenTree(indexStructure({ roots: ['0'], nodes }), new Set(), new Set(), 'needle');
  assert.equal(rows.length, count);
  assert.equal(rows.at(-1).depth, count - 1);
});

test('search scans 80k collapsed nodes and finds the final node', () => {
  const count = 80_000;
  const nodes = Array.from({ length: count }, (_, index) => ({
    id: String(index), name: index === count - 1 ? 'final needle' : `node-${index}`, kind: 'Class',
    children: index + 1 < count ? [String(index + 1)] : [], defaultCollapsed: true,
  }));
  const rows = flattenTree(indexStructure({ roots: ['0'], nodes }), new Set(), new Set(), 'final needle');
  assert.equal(rows.length, count);
  assert.equal(rows.at(-1).node.id, String(count - 1));
});

function sortFixture() {
  return indexStructure({
    roots: ['file'],
    nodes: [
      { id: 'file', name: 'sort.abc', kind: 'File', children: ['ma', 'mb', 'mc', 'ns', 'grp'], defaultCollapsed: false },
      { id: 'ma', name: 'A', kind: 'Module', children: [], defaultCollapsed: true, parentId: 'file' },
      { id: 'mb', name: 'B', kind: 'Module', children: [], defaultCollapsed: true, parentId: 'file' },
      { id: 'mc', name: 'C', kind: 'Module', children: [], defaultCollapsed: true, parentId: 'file' },
      { id: 'ns', name: 'D', kind: 'Script', children: [], defaultCollapsed: true, parentId: 'file' },
      { id: 'grp', name: 'Imports', kind: 'Group', children: [], defaultCollapsed: true, parentId: 'file' },
    ],
  });
}

test('module order sorts Module/Script children of the File root by size descending', () => {
  const order = new Map([['ma', 100], ['mb', 500], ['mc', 200], ['ns', 50]]);
  const rows = flattenTree(sortFixture(), new Set(), new Set(), '', new Set(), new Set(), order);
  assert.deepEqual(rows.map(({ node }) => node.id), ['file', 'mb', 'mc', 'ma', 'ns', 'grp']);
});

test('module order keeps unsized scopes and groups at the tail in file order', () => {
  const order = new Map([['ma', 100]]);
  const rows = flattenTree(sortFixture(), new Set(), new Set(), '', new Set(), new Set(), order);
  assert.deepEqual(rows.map(({ node }) => node.id), ['file', 'ma', 'mb', 'mc', 'ns', 'grp']);
});

test('module order does not apply while filtering', () => {
  const order = new Map([['mb', 500]]);
  const rows = flattenTree(sortFixture(), new Set(), new Set(), 'B', new Set(), new Set(), order);
  assert.deepEqual(rows.map(({ node }) => node.id), ['file', 'mb']);
});
