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
  blockInstructionPresentation,
  instructionDisclosureText,
  visibleInstructionIndexes,
  buildLayoutRequest,
  centerNodePan,
  clampZoom,
  edgePath,
  hasMeaningfulDrag,
  MAX_LAYOUT_REQUEST_BYTES,
  MAX_LAYOUT_REQUEST_NODES,
  nodeOverlapsIndex,
  nodeOverlapsLayout,
  OrthogonalRouteSpatialIndex,
  rectIntersectsBounds,
  RectSpatialIndex,
  routeBounds,
  routeIntersectsBounds,
  screenToStage,
  validateLayoutResponse,
  visibleStageBounds,
} from '../src/cfg-view-model.js';

test('cfg basic blocks expose every ordinary instruction without an inner scroll window', () => {
  assert.deepEqual(blockInstructionPresentation(3, false), {
    visibleRows: 3,
    hiddenRows: 0,
    collapsible: false,
    expanded: false,
    limited: false,
    height: 166,
  });
});

test('cfg long basic blocks reserve a disclosure row and expand within the render safety bound', () => {
  const collapsed = blockInstructionPresentation(20, false);
  assert.deepEqual(collapsed, {
    visibleRows: 12,
    hiddenRows: 8,
    collapsible: true,
    expanded: false,
    limited: false,
    height: 452,
  });
  assert.equal(instructionDisclosureText(20, collapsed), 'Show 8 more instructions');
  assert.equal(
    instructionDisclosureText(13, blockInstructionPresentation(13, false)),
    'Show 1 more instruction',
  );

  const expanded = blockInstructionPresentation(20, true);
  assert.deepEqual(expanded, {
    visibleRows: 20,
    hiddenRows: 0,
    collapsible: true,
    expanded: true,
    limited: false,
    height: 676,
  });
  assert.equal(instructionDisclosureText(20, expanded), 'Collapse instructions');

  const limited = blockInstructionPresentation(500000, true);
  assert.deepEqual(limited, {
    visibleRows: 128,
    hiddenRows: 499872,
    collapsible: true,
    expanded: true,
    limited: true,
    height: 3700,
  });
  assert.equal(instructionDisclosureText(500000, limited), 'Showing 128 of 500000 (last included) · Collapse');
});

test('cfg instruction previews keep the true terminator without exceeding row caps', () => {
  assert.deepEqual(visibleInstructionIndexes(3, blockInstructionPresentation(3, false)), [0, 1, 2]);
  assert.deepEqual(visibleInstructionIndexes(12, blockInstructionPresentation(12, false)),
    Array.from({ length: 12 }, (_, index) => index));
  assert.deepEqual(visibleInstructionIndexes(13, blockInstructionPresentation(13, false)),
    [...Array.from({ length: 11 }, (_, index) => index), 12]);
  assert.deepEqual(visibleInstructionIndexes(500, blockInstructionPresentation(500, false)),
    [...Array.from({ length: 11 }, (_, index) => index), 499]);
  assert.deepEqual(visibleInstructionIndexes(500, blockInstructionPresentation(500, true)),
    [...Array.from({ length: 127 }, (_, index) => index), 499]);
});

test('cfg view model clamps zoom and translates viewport points into stage coordinates', () => {
  assert.equal(clampZoom(0.01), 0.25);
  assert.equal(clampZoom(9), 4);
  assert.deepEqual(
    screenToStage(250, 180, { left: 10, top: 20 }, { x: 40, y: 10 }, 2),
    { x: 100, y: 75 },
  );
});

test('cfg edge path retains backend orthogonal points verbatim', () => {
  assert.equal(edgePath([{ x: 10, y: 20 }, { x: 10, y: 40 }, { x: 70, y: 40 }]), 'M 10 20 L 10 40 L 70 40');
  assert.equal(edgePath([]), '');
});

test('cfg relayout request carries only geometry overrides and explicit pins', () => {
  const layout = { nodes: [
    { block: '0', width: 420, height: 180 },
    { block: '1', width: 500, height: 220 },
    { block: '2', width: 440, height: 200 },
  ] };
  const geometryOverrides = new Map([['0', { width: 430, height: 190 }]]);
  const pins = new Map([['1', { x: 32, y: 48 }]]);

  assert.deepEqual(buildLayoutRequest(layout, geometryOverrides, pins, 6), {
    revision: 6,
    nodes: [
      { block: '0', width: 430, height: 190, pinned: null },
      { block: '1', width: 500, height: 220, pinned: { x: 32, y: 48 } },
    ],
  });
});

test('cfg relayout request remains below the host body limit for a large unchanged graph', () => {
  const layout = {
    nodes: Array.from({ length: 20000 }, (_, index) => ({
      block: String(index), width: 420, height: 180,
    })),
  };
  const request = buildLayoutRequest(
    layout,
    new Map(),
    new Map([['19999', { x: 1200, y: 2400 }]]),
    3,
  );

  assert.equal(request.nodes.length, 1);
  assert.ok(Buffer.byteLength(JSON.stringify(request)) < 1024 * 1024);
});

test('cfg relayout rejects an unbounded set of manual overrides before exceeding the host body limit', () => {
  const layout = {
    nodes: Array.from({ length: MAX_LAYOUT_REQUEST_NODES + 1 }, (_, index) => ({
      block: String(index), width: 420, height: 180,
    })),
  };
  const pins = new Map(layout.nodes.map((node, index) => [node.block, { x: index * 500, y: 80 }]));

  assert.throws(() => buildLayoutRequest(layout, new Map(), pins, 4), /4096/);
});

test('cfg relayout checks exact UTF-8 request bytes before posting', () => {
  const longId = '节'.repeat(MAX_LAYOUT_REQUEST_BYTES / 2);
  const layout = { nodes: [{ block: longId, width: 420, height: 180 }] };

  assert.throws(
    () => buildLayoutRequest(layout, new Map(), new Map([[longId, { x: 80, y: 80 }]]), 5),
    /1 MiB/,
  );
});

test('cfg interaction detects overlap and ignores click-sized pointer jitter', () => {
  const nodes = [
    { block: '0', x: 20, y: 20, width: 100, height: 80 },
    { block: '1', x: 160, y: 20, width: 100, height: 80 },
  ];
  assert.equal(nodeOverlapsLayout('0', { x: 70, y: 30 }, nodes), true);
  assert.equal(nodeOverlapsLayout('0', { x: 60, y: 100 }, nodes), false);
  assert.equal(hasMeaningfulDrag({ x: 10, y: 10 }, { x: 12, y: 13 }), false);
  assert.equal(hasMeaningfulDrag({ x: 10, y: 10 }, { x: 15, y: 10 }), true);
});

test('cfg interaction detects collisions against offscreen indexed nodes', () => {
  const nodes = [
    { block: '0', x: 20, y: 20, width: 100, height: 80 },
    { block: 'offscreen', x: 5000, y: 5000, width: 100, height: 80 },
  ];
  const index = new RectSpatialIndex(nodes, (node) => node.block, 256);

  assert.equal(nodeOverlapsIndex(nodes[0], { x: 5020, y: 5010 }, index), true);
  assert.equal(nodeOverlapsIndex(nodes[0], { x: 4800, y: 4800 }, index), false);
});

test('cfg culling maps the viewport to stage space with overscan', () => {
  const bounds = visibleStageBounds(800, 600, { x: -100, y: -50 }, 2, 100);
  assert.deepEqual(bounds, { left: 0, top: -25, right: 500, bottom: 375 });
  assert.equal(rectIntersectsBounds({ x: 480, y: 0, width: 40, height: 40 }, bounds), true);
  assert.equal(rectIntersectsBounds({ x: 501, y: 0, width: 40, height: 40 }, bounds), false);
  assert.equal(routeIntersectsBounds([{ x: -50, y: 100 }, { x: 550, y: 100 }], bounds), true);
  assert.equal(routeIntersectsBounds([{ x: 600, y: 10 }, { x: 700, y: 10 }], bounds), false);
});

test('cfg spatial index queries only rectangles intersecting the visible bounds', () => {
  const index = new RectSpatialIndex([
    { block: 'a', x: 0, y: 0, width: 100, height: 100 },
    { block: 'b', x: 5000, y: 5000, width: 100, height: 100 },
    { block: 'c', x: 150, y: 150, width: 100, height: 100 },
  ], (node) => node.block, 256);

  assert.deepEqual(index.query({ left: -10, top: -10, right: 180, bottom: 180 }), new Set(['a', 'c']));
  assert.deepEqual(index.query({ left: 4900, top: 4900, right: 5200, bottom: 5200 }), new Set(['b']));
});

test('cfg route spatial index keeps a route crossing the viewport with both endpoints offscreen', () => {
  const routes = [
    { edge: 'crossing', points: [{ x: -5000, y: 100 }, { x: 5000, y: 100 }] },
    { edge: 'away', points: [{ x: -5000, y: 2000 }, { x: 5000, y: 2000 }] },
  ];
  const index = new OrthogonalRouteSpatialIndex(routes, (route) => route.edge, 256);

  assert.deepEqual(index.query({ left: 0, top: 0, right: 800, bottom: 600 }), new Set(['crossing']));
});

test('cfg route index does not duplicate long segments across every crossed grid cell', () => {
  const routes = Array.from({ length: 10000 }, (_, index) => ({
    edge: String(index),
    points: [{ x: -100000000, y: index * 100 }, { x: 100000000, y: index * 100 }],
  }));
  const index = new OrthogonalRouteSpatialIndex(routes, (route) => route.edge, 256);

  assert.deepEqual(index.query({ left: 0, top: 49990, right: 800, bottom: 50010 }), new Set(['500']));
});

test('cfg route interval buckets select only vertical segments overlapping the visible y range', () => {
  const routes = Array.from({ length: 10000 }, (_, index) => ({
    edge: String(index),
    points: [{ x: 100, y: index * 1000 }, { x: 100, y: index * 1000 + 100 }],
  }));
  const index = new OrthogonalRouteSpatialIndex(routes, (route) => route.edge, 256);

  assert.deepEqual(index.query({ left: 0, top: 4999990, right: 800, bottom: 5000110 }), new Set(['5000']));
});

test('cfg starts at readable 100 percent centered on the entry block', () => {
  assert.deepEqual(centerNodePan({ x: 80, y: 40, width: 420, height: 200 }, 1000, 700, 1), { x: 210, y: 210 });
});

test('cfg relayout accepts only the requested document entity and revision', () => {
  const response = {
    documentId: 'doc',
    entityId: '42',
    layout: {
      revision: 7,
      canvas: { width: 1000, height: 800 },
      nodes: [{ block: '0', x: 20, y: 30, width: 420, height: 180 }],
      edges: [{ edge: '0', points: [{ x: 200, y: 210 }, { x: 200, y: 252 }] }],
    },
  };
  const expected = {
    documentId: 'doc', entityId: '42', revision: 7, blockIds: new Set(['0']), edgeIds: new Set(['0']),
  };
  assert.equal(validateLayoutResponse(response, expected), response.layout);
  assert.throws(
    () => validateLayoutResponse(response, { documentId: 'other', entityId: '42', revision: 7 }),
    /document/i,
  );
  assert.throws(
    () => validateLayoutResponse(response, { documentId: 'doc', entityId: '41', revision: 7 }),
    /entity/i,
  );
  assert.throws(
    () => validateLayoutResponse(response, { documentId: 'doc', entityId: '42', revision: 8 }),
    /revision/i,
  );
  assert.throws(
    () => validateLayoutResponse({
      ...response,
      layout: { ...response.layout, nodes: [{ ...response.layout.nodes[0], x: Number.NaN }] },
    }, expected),
    /geometry/i,
  );
  assert.throws(
    () => validateLayoutResponse(response, { ...expected, blockIds: new Set(['0', '1']) }),
    /block set/i,
  );
});
