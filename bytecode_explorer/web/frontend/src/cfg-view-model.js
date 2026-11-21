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

export const MIN_ZOOM = 0.25;
export const MAX_ZOOM = 4;
export const VIEWPORT_OVERSCAN = 240;
export const MAX_LAYOUT_REQUEST_NODES = 4096;
export const MAX_LAYOUT_REQUEST_BYTES = 1024 * 1024;
export const COLLAPSED_INSTRUCTION_ROWS = 12;
export const MAX_EXPANDED_INSTRUCTION_ROWS = 128;

const NODE_HEADER_HEIGHT = 62;
const INSTRUCTION_ROW_HEIGHT = 28;
const NODE_DISCLOSURE_HEIGHT = 34;
const NODE_FOOTER_HEIGHT = 20;

export function blockInstructionPresentation(instructionCount, requestedExpanded) {
  const count = Number.isSafeInteger(instructionCount) && instructionCount > 0 ? instructionCount : 0;
  const collapsible = count > COLLAPSED_INSTRUCTION_ROWS;
  const expanded = collapsible && requestedExpanded;
  const rowLimit = expanded ? MAX_EXPANDED_INSTRUCTION_ROWS : COLLAPSED_INSTRUCTION_ROWS;
  const visibleRows = Math.min(count, rowLimit);
  const hiddenRows = count - visibleRows;
  return {
    visibleRows,
    hiddenRows,
    collapsible,
    expanded,
    limited: expanded && hiddenRows > 0,
    height: NODE_HEADER_HEIGHT + visibleRows * INSTRUCTION_ROW_HEIGHT +
      (collapsible ? NODE_DISCLOSURE_HEIGHT : 0) + NODE_FOOTER_HEIGHT,
  };
}

export function instructionDisclosureText(instructionCount, presentation) {
  if (!presentation.collapsible) {
    return '';
  }
  if (!presentation.expanded) {
    const noun = presentation.hiddenRows === 1 ? 'instruction' : 'instructions';
    return `Show ${presentation.hiddenRows} more ${noun}`;
  }
  if (presentation.limited) {
    return `Showing ${presentation.visibleRows} of ${instructionCount} (last included) · Collapse`;
  }
  return 'Collapse instructions';
}

export function visibleInstructionIndexes(instructionCount, presentation) {
  const count = Number.isSafeInteger(instructionCount) && instructionCount > 0 ? instructionCount : 0;
  const visibleRows = Math.min(count, presentation.visibleRows);
  if (visibleRows === count) {
    return Array.from({ length: visibleRows }, (_, index) => index);
  }
  return [...Array.from({ length: Math.max(0, visibleRows - 1) }, (_, index) => index), count - 1];
}

export function clampZoom(value) {
  return Math.min(MAX_ZOOM, Math.max(MIN_ZOOM, value));
}

export function screenToStage(clientX, clientY, viewportRect, pan, zoom) {
  return {
    x: (clientX - viewportRect.left - pan.x) / zoom,
    y: (clientY - viewportRect.top - pan.y) / zoom,
  };
}

export function edgePath(points) {
  if (!points.length) {
    return '';
  }
  return points.map((point, index) => `${index ? 'L' : 'M'} ${point.x} ${point.y}`).join(' ');
}

export function hasMeaningfulDrag(start, current, threshold = 4) {
  return Math.hypot(current.x - start.x, current.y - start.y) >= threshold;
}

export function rectanglesOverlap(left, right) {
  return left.x < right.x + right.width && right.x < left.x + left.width &&
    left.y < right.y + right.height && right.y < left.y + left.height;
}

export function nodeOverlapsLayout(block, position, nodes) {
  const dragged = nodes.find((node) => node.block === block);
  if (!dragged) {
    return false;
  }
  const candidate = { ...dragged, ...position };
  return nodes.some((node) => node.block !== block && rectanglesOverlap(candidate, node));
}

export function nodeOverlapsIndex(node, position, spatialIndex) {
  const candidate = { ...node, ...position };
  const bounds = {
    left: candidate.x,
    top: candidate.y,
    right: candidate.x + candidate.width,
    bottom: candidate.y + candidate.height,
  };
  return spatialIndex.query(bounds, node.block).size !== 0;
}

export function visibleStageBounds(viewportWidth, viewportHeight, pan, zoom, overscan = VIEWPORT_OVERSCAN) {
  const stageOverscan = overscan / zoom;
  return {
    left: -pan.x / zoom - stageOverscan,
    top: -pan.y / zoom - stageOverscan,
    right: (viewportWidth - pan.x) / zoom + stageOverscan,
    bottom: (viewportHeight - pan.y) / zoom + stageOverscan,
  };
}

export function rectIntersectsBounds(rect, bounds) {
  return rect.x < bounds.right && rect.x + rect.width > bounds.left &&
    rect.y < bounds.bottom && rect.y + rect.height > bounds.top;
}

export class RectSpatialIndex {
  static MAX_CELLS_PER_RECTANGLE = 256;

  #cellSize;
  #rectangles = new Map();
  #cells = new Map();
  #broad = new Set();

  constructor(rectangles, idOf, cellSize = 1024) {
    this.#cellSize = cellSize;
    for (const rectangle of rectangles) {
      const id = idOf(rectangle);
      this.#rectangles.set(id, rectangle);
      if (!this.#forEachCell(rectangle, (key) => {
        const occupants = this.#cells.get(key) ?? [];
        occupants.push(id);
        this.#cells.set(key, occupants);
      })) {
        this.#broad.add(id);
      }
    }
  }

  query(bounds, excludedId = null) {
    const candidates = new Set(this.#broad);
    const indexed = this.#forEachCell({
      x: bounds.left,
      y: bounds.top,
      width: bounds.right - bounds.left,
      height: bounds.bottom - bounds.top,
    }, (key) => this.#cells.get(key)?.forEach((id) => candidates.add(id)));
    if (!indexed) {
      this.#rectangles.forEach((_, id) => candidates.add(id));
    }
    return new Set([...candidates].filter((id) => id !== excludedId &&
      rectIntersectsBounds(this.#rectangles.get(id), bounds)));
  }

  #forEachCell(rectangle, callback) {
    const firstX = Math.floor(rectangle.x / this.#cellSize);
    const lastX = Math.floor((rectangle.x + rectangle.width) / this.#cellSize);
    const firstY = Math.floor(rectangle.y / this.#cellSize);
    const lastY = Math.floor((rectangle.y + rectangle.height) / this.#cellSize);
    if ((lastX - firstX + 1) * (lastY - firstY + 1) > RectSpatialIndex.MAX_CELLS_PER_RECTANGLE) {
      return false;
    }
    for (let x = firstX; x <= lastX; x += 1) {
      for (let y = firstY; y <= lastY; y += 1) {
        callback(`${x}:${y}`);
      }
    }
    return true;
  }
}

class IntervalNode {
  constructor(segments) {
    const centers = segments.map((segment) => (segment.start + segment.end) / 2).sort((left, right) => left - right);
    this.center = centers[Math.floor(centers.length / 2)];
    const left = [];
    const right = [];
    const crossing = [];
    for (const segment of segments) {
      if (segment.end < this.center) {
        left.push(segment);
      }
      else if (segment.start > this.center) {
        right.push(segment);
      } else {
        crossing.push(segment);
      }
    }
    this.byStart = [...crossing].sort((first, second) => first.start - second.start);
    this.byEnd = [...crossing].sort((first, second) => second.end - first.end);
    this.left = left.length ? new IntervalNode(left) : null;
    this.right = right.length ? new IntervalNode(right) : null;
  }

  query(minimum, maximum, visit) {
    if (maximum < this.center) {
      for (const segment of this.byStart) {
        if (segment.start >= maximum) {
          break;
        }
        visit(segment);
      }
      this.left?.query(minimum, maximum, visit);
      return;
    }
    if (minimum > this.center) {
      for (const segment of this.byEnd) {
        if (segment.end <= minimum) {
          break;
        }
        visit(segment);
      }
      this.right?.query(minimum, maximum, visit);
      return;
    }
    this.byStart.forEach(visit);
    this.left?.query(minimum, maximum, visit);
    this.right?.query(minimum, maximum, visit);
  }
}

class IntervalIndex {
  constructor(segments) {
    this.root = segments.length ? new IntervalNode(segments) : null;
  }

  query(minimum, maximum, visit) {
    this.root?.query(minimum, maximum, visit);
  }
}

export class OrthogonalRouteSpatialIndex {
  #cellSize;
  #horizontal = new Map();
  #vertical = new Map();
  #diagonal = [];

  constructor(routes, idOf, cellSize = 1024) {
    this.#cellSize = cellSize;
    for (const route of routes) {
      const id = idOf(route);
      for (let index = 1; index < route.points.length; index += 1) {
        const start = route.points[index - 1];
        const end = route.points[index];
        if (start.y === end.y) {
          this.#append(this.#horizontal, Math.floor(start.y / cellSize), {
            id, coordinate: start.y, start: Math.min(start.x, end.x), end: Math.max(start.x, end.x),
          });
        } else if (start.x === end.x) {
          this.#append(this.#vertical, Math.floor(start.x / cellSize), {
            id, coordinate: start.x, start: Math.min(start.y, end.y), end: Math.max(start.y, end.y),
          });
        } else {
          this.#diagonal.push({ id, bounds: routeBounds([start, end]) });
        }
      }
    }
    this.#horizontal.forEach((segments, band) => this.#horizontal.set(band, new IntervalIndex(segments)));
    this.#vertical.forEach((segments, band) => this.#vertical.set(band, new IntervalIndex(segments)));
  }

  query(bounds) {
    const found = new Set();
    this.#queryBands(this.#horizontal, bounds.top, bounds.bottom, bounds.left, bounds.right,
      (segment) => segment.coordinate >= bounds.top && segment.coordinate <= bounds.bottom, found);
    this.#queryBands(this.#vertical, bounds.left, bounds.right, bounds.top, bounds.bottom,
      (segment) => segment.coordinate >= bounds.left && segment.coordinate <= bounds.right, found);
    for (const segment of this.#diagonal) {
      if (rectIntersectsBounds(segment.bounds, bounds)) {
        found.add(segment.id);
      }
    }
    return found;
  }

  #append(index, band, segment) {
    const segments = index.get(band) ?? [];
    segments.push(segment);
    index.set(band, segments);
  }

  #queryBands(index, bandMinimum, bandMaximum, intervalMinimum, intervalMaximum, intersects, found) {
    const first = Math.floor(bandMinimum / this.#cellSize);
    const last = Math.floor(bandMaximum / this.#cellSize);
    for (let band = first; band <= last; band += 1) {
      index.get(band)?.query(intervalMinimum, intervalMaximum, (segment) => {
        if (intersects(segment)) {
          found.add(segment.id);
        }
      });
    }
  }
}

export function routeBounds(points) {
  if (!points.length) {
    return null;
  }
  const extent = points.reduce((result, point) => ({
    left: Math.min(result.left, point.x),
    top: Math.min(result.top, point.y),
    right: Math.max(result.right, point.x),
    bottom: Math.max(result.bottom, point.y),
  }), { left: Infinity, top: Infinity, right: -Infinity, bottom: -Infinity });
  return {
    x: extent.left,
    y: extent.top,
    width: Math.max(1, extent.right - extent.left),
    height: Math.max(1, extent.bottom - extent.top),
  };
}

export function routeIntersectsBounds(points, bounds) {
  const rectangle = routeBounds(points);
  return rectangle !== null && rectIntersectsBounds(rectangle, bounds);
}

export function centerNodePan(node, viewportWidth, viewportHeight, zoom = 1) {
  return {
    x: viewportWidth / 2 - (node.x + node.width / 2) * zoom,
    y: viewportHeight / 2 - (node.y + node.height / 2) * zoom,
  };
}

function isFiniteNumber(value) {
  return typeof value === 'number' && Number.isFinite(value);
}

function validateLayoutCanvas(layout) {
  if (!isFiniteNumber(layout?.canvas?.width) || !isFiniteNumber(layout?.canvas?.height) ||
      layout.canvas.width <= 0 || layout.canvas.height <= 0 ||
      !Array.isArray(layout.nodes) || !Array.isArray(layout.edges)) {
    throw new Error('CFG layout response has invalid canvas or collections');
  }
}

function collectValidatedNodeIds(layout) {
  const nodeIds = new Set();
  for (const node of layout.nodes) {
    if (typeof node?.block !== 'string' || nodeIds.has(node.block) || !isFiniteNumber(node.x) ||
        !isFiniteNumber(node.y) || !isFiniteNumber(node.width) || !isFiniteNumber(node.height) ||
        node.x < 0 || node.y < 0 || node.width <= 0 || node.height <= 0) {
      throw new Error('CFG layout response has invalid node geometry');
    }
    nodeIds.add(node.block);
  }
  return nodeIds;
}

function collectValidatedEdgeIds(layout) {
  const edgeIds = new Set();
  for (const edge of layout.edges) {
    if (typeof edge?.edge !== 'string' || edgeIds.has(edge.edge) || !Array.isArray(edge.points) ||
        edge.points.length < 2 ||
        edge.points.some((point) => !isFiniteNumber(point?.x) || !isFiniteNumber(point?.y))) {
      throw new Error('CFG layout response has invalid edge route');
    }
    edgeIds.add(edge.edge);
  }
  return edgeIds;
}

function assertExpectedIds(expectedIds, actualIds, message) {
  if (expectedIds && (actualIds.size !== expectedIds.size ||
      [...expectedIds].some((id) => !actualIds.has(id)))) {
    throw new Error(message);
  }
}

export function validateLayoutResponse(response, expected) {
  if (response?.documentId !== expected.documentId) {
    throw new Error('CFG layout response document mismatch');
  }
  if (response?.entityId !== expected.entityId) {
    throw new Error('CFG layout response entity mismatch');
  }
  if (response?.layout?.revision !== expected.revision) {
    throw new Error('CFG layout response revision mismatch');
  }
  const layout = response.layout;
  validateLayoutCanvas(layout);
  const nodeIds = collectValidatedNodeIds(layout);
  const edgeIds = collectValidatedEdgeIds(layout);
  assertExpectedIds(expected.blockIds, nodeIds, 'CFG layout response block set mismatch');
  assertExpectedIds(expected.edgeIds, edgeIds, 'CFG layout response edge set mismatch');
  return layout;
}

export function buildLayoutRequest(layout, geometryOverrides, pins, revision) {
  const nodes = layout.nodes.filter(({ block }) => geometryOverrides.has(block) || pins.has(block)).map((node) => {
    const geometry = geometryOverrides.get(node.block) ?? node;
    return {
      block: node.block,
      width: geometry.width,
      height: geometry.height,
      pinned: pins.get(node.block) ?? null,
    };
  });
  if (nodes.length > MAX_LAYOUT_REQUEST_NODES) {
    throw new Error(`CFG layout has more than ${MAX_LAYOUT_REQUEST_NODES} geometry overrides or pins`);
  }
  const request = { revision, nodes };
  if (new TextEncoder().encode(JSON.stringify(request)).byteLength > MAX_LAYOUT_REQUEST_BYTES) {
    throw new Error('CFG layout request exceeds the 1 MiB local host limit');
  }
  return request;
}
