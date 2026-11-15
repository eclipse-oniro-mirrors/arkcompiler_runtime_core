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

import { CfgCanvas, refreshCanvasPalette } from './cfg-canvas.js';
import {
  blockInstructionPresentation,
  buildLayoutRequest,
  centerNodePan,
  clampZoom,
  edgePath,
  hasMeaningfulDrag,
  nodeOverlapsIndex,
  OrthogonalRouteSpatialIndex,
  RectSpatialIndex,
  screenToStage,
  validateLayoutResponse,
  visibleStageBounds,
} from './cfg-view-model.js';

const MAX_PINNED_ENTITIES = 64;

function canonicalMethodOffset(methodOffset) {
  if (methodOffset === null) {
    return null;
  }
  if (typeof methodOffset !== 'bigint' || methodOffset < 0n || methodOffset > 0xffff_ffff_ffff_ffffn) {
    throw new TypeError('CFG method offset must be an unsigned 64-bit BigInt');
  }
  return `0x${methodOffset.toString(16)}`;
}

export function cfgSelectionKey(entityId, methodOffset = null) {
  if (typeof entityId !== 'string' || !/^[1-9][0-9]*$/.test(entityId)) {
    throw new TypeError('CFG entity id must be a non-zero decimal string');
  }
  const offset = canonicalMethodOffset(methodOffset);
  return offset === null ? entityId : `${entityId}@${offset}`;
}

export function cfgRequestPath(entityId, methodOffset = null, layout = false) {
  const offset = canonicalMethodOffset(methodOffset);
  const query = `id=${encodeURIComponent(entityId)}` +
    (offset === null ? '' : `&methodOffset=${encodeURIComponent(offset)}`);
  cfgSelectionKey(entityId, methodOffset);
  return `/api/v1/cfg${layout ? '/layout' : ''}?${query}`;
}

export class CfgView {
  #client;
  #root;
  #viewport;
  #stage;
  #canvas;
  #zoomLabel;
  #summary;
  #resetCurrent;
  #resetLayout;
  #onStatus;
  #onError;
  #onClearError;
  #onBlockSelect;
  #entityId = null;
  #methodOffset = null;
  #response = null;
  #graph = null;
  #layout = null;
  #blocks = new Map();
  #edges = new Map();
  #edgeIdsByBlock = new Map();
  #edgeRoutes = new Map();
  #layoutNodes = new Map();
  #nodeSpatialIndex = null;
  #edgeSpatialIndex = null;
  #pins = new Map();
  #pinsByEntity = new Map();
  #geometryOverrides = new Map();
  #geometryOverridesByEntity = new Map();
  #expandedBlocks = new Set();
  #expandedBlocksByEntity = new Map();
  #selectedBlock = null;
  #zoom = 1;
  #pan = { x: 0, y: 0 };
  #panGesture = null;
  #dragGesture = null;
  #revision = 0;
  #relayoutEntity = null;
  #relayoutSequence = 0;
  #cullFrame = null;

  constructor({ client, elements, onStatus, onError, onClearError, onBlockSelect = () => {} }) {
    this.#client = client;
    this.#root = elements.root;
    this.#viewport = elements.viewport;
    this.#stage = elements.stage;
    this.#zoomLabel = elements.zoomLabel;
    this.#summary = elements.summary;
    this.#resetCurrent = elements.resetCurrent;
    this.#resetLayout = elements.resetLayout;
    this.#onStatus = onStatus;
    this.#onError = onError;
    this.#onClearError = onClearError;
    this.#onBlockSelect = onBlockSelect;
    this.#canvas = new CfgCanvas({
      edgeLayer: elements.edgeLayer,
      nodeLayer: elements.nodeLayer,
      onAttachDrag: (header, block) => this.#attachNodeDrag(header, block),
      onSelect: (block) => this.#selectBlock(block),
      onCenter: (block) => this.#centerBlock(block, Math.max(1, this.#zoom)),
      onToggleExpansion: (block, expanded) => this.#toggleBlockExpansion(block, expanded),
    });

    elements.zoomIn.addEventListener('click', () => this.zoomBy(1.2));
    elements.zoomOut.addEventListener('click', () => this.zoomBy(1 / 1.2));
    elements.actualSize.addEventListener('click', () => this.actualSize());
    elements.fit.addEventListener('click', () => this.fit());
    this.#resetCurrent.addEventListener('click', () => this.#resetPins(true));
    this.#resetLayout.addEventListener('click', () => this.#resetPins(false));
    this.#viewport.addEventListener('wheel', (event) => this.#onWheel(event), { passive: false });
    this.#viewport.addEventListener('pointerdown', (event) => this.#beginPan(event));
    this.#viewport.addEventListener('pointermove', (event) => this.#movePan(event));
    this.#viewport.addEventListener('pointerup', (event) => this.#finishPan(event));
    this.#viewport.addEventListener('pointercancel', (event) => this.#finishPan(event));
  }

  show() {
    this.#root.classList.remove('hidden');
  }

  // Rebinds the view to another document. Entity ids are per-document, so the
  // pins, geometry overrides and expansion state remembered per entity must
  // not survive a document switch even though the ids look alike.
  reset() {
    this.#cancelGestures();
    this.#pinsByEntity.clear();
    this.#geometryOverridesByEntity.clear();
    this.#expandedBlocksByEntity.clear();
    this.#relayoutEntity = null;
    this.showUnavailable('Select an executable function or method to build a CFG');
  }

  showUnavailable(message = 'Select an executable function or method to build a CFG') {
    this.#cancelGestures();
    this.#entityId = null;
    this.#methodOffset = null;
    this.#response = null;
    this.#graph = null;
    this.#layout = null;
    this.#blocks.clear();
    this.#edges.clear();
    this.#layoutNodes.clear();
    this.#canvas.clear();
    this.#summary.textContent = message;
    this.#root.classList.remove('loading');
    this.#root.classList.add('unavailable');
    this.#updateControls();
    this.show();
  }

  resize() {
    if (this.#layout === null || this.#root.classList.contains('hidden')) {
      return;
    }
    requestAnimationFrame(() => this.#renderVisibleContent(true));
  }

  // Re-reads the canvas marker palette and repaints the edge layer after a
  // theme change. The palette refresh runs even when the view is hidden or
  // empty so the next render picks up the new colors.
  refreshTheme() {
    refreshCanvasPalette();
    if (this.#layout !== null && !this.#root.classList.contains('hidden')) {
      this.#canvas.refreshEdges();
    }
  }

  hide() {
    this.#cancelGestures();
    this.#root.classList.add('hidden');
    this.#root.classList.remove('loading');
  }

  beginLoading(entityId, name, methodOffset = null) {
    this.#cancelGestures();
    this.#entityId = String(entityId);
    this.#methodOffset = methodOffset;
    this.#response = null;
    this.#graph = null;
    this.#layout = null;
    this.#blocks.clear();
    this.#edges.clear();
    this.#edgeIdsByBlock.clear();
    this.#edgeRoutes.clear();
    this.#pins.clear();
    this.#expandedBlocks.clear();
    this.#layoutNodes.clear();
    this.#canvas.clear();
    this.#selectedBlock = null;
    this.#summary.textContent = `Building ${name}…`;
    this.#root.classList.remove('unavailable');
    this.#root.classList.add('loading');
    this.#updateControls();
    this.show();
  }

  render(entityId, response) {
    this.#cancelGestures();
    this.#entityId = String(entityId);
    this.#methodOffset = BigInt(response.function.methodOffset);
    const selectionKey = this.#selectionKey();
    this.#response = response;
    this.#graph = response.graph;
    const storedExpandedBlocks = this.#expandedBlocksByEntity.get(selectionKey);
    this.#expandedBlocks = storedExpandedBlocks ? new Set(storedExpandedBlocks) : new Set();
    this.#blocks = new Map(response.graph.blocks.map((block) => [block.id, block]));
    this.#edges = new Map(response.graph.edges.map((edge) => [edge.id, edge]));
    this.#edgeIdsByBlock = new Map(response.graph.blocks.map((block) => [block.id, []]));
    for (const edge of response.graph.edges) {
      this.#edgeIdsByBlock.get(edge.source)?.push(edge.id);
      if (edge.target !== null && edge.target !== edge.source) {
        this.#edgeIdsByBlock.get(edge.target)?.push(edge.id);
      }
    }
    const storedPins = this.#pinsByEntity.get(selectionKey);
    this.#pins = storedPins ? new Map(storedPins) : new Map(response.layout.nodes
      .filter((node) => node.pinned)
      .map((node) => [node.block, { x: node.x, y: node.y }]));
    this.#persistPins();
    const storedGeometry = this.#geometryOverridesByEntity.get(selectionKey);
    this.#geometryOverrides = storedGeometry ? new Map(storedGeometry) : new Map();
    this.#selectedBlock = null;
    this.#revision = response.layout.revision;
    const irreducible = response.graph.stronglyConnectedComponents.filter((component) => component.irreducible).length;
    this.#summary.textContent = `${response.function.name} · ${response.graph.blocks.length} BB · ` +
      `${response.graph.edges.length} edges · ${response.graph.naturalLoops.length} loops` +
      (irreducible ? ` · ${irreducible} irreducible` : '');
    this.#root.classList.remove('loading');
    this.#root.classList.remove('unavailable');
    this.show();
    this.#applyLayout(response.layout);
    requestAnimationFrame(() => {
      if (this.#selectionKey() !== selectionKey || this.#root.classList.contains('hidden')) {
        return;
      }
      this.#centerBlock(response.graph.entryBlock, 1);
      this.#relayoutIfMeasuredGeometryDiffers();
    });
  }

  #selectionKey() {
    return this.#entityId === null ? null : cfgSelectionKey(this.#entityId, this.#methodOffset);
  }

  zoomBy(factor) {
    const rect = this.#viewport.getBoundingClientRect();
    this.#setZoom(this.#zoom * factor, rect.left + rect.width / 2, rect.top + rect.height / 2);
  }

  actualSize() {
    if (this.#graph) {
      this.#centerBlock(this.#graph.entryBlock, 1);
    }
  }

  fit() {
    if (!this.#layout) {
      return;
    }
    const width = this.#viewport.clientWidth;
    const height = this.#viewport.clientHeight;
    if (!width || !height || !this.#layout.canvas.width || !this.#layout.canvas.height) {
      return;
    }
    this.#zoom = clampZoom(Math.min(
      (width - 48) / this.#layout.canvas.width,
      (height - 48) / this.#layout.canvas.height,
    ));
    this.#pan = {
      x: Math.max(24, (width - this.#layout.canvas.width * this.#zoom) / 2),
      y: Math.max(24, (height - this.#layout.canvas.height * this.#zoom) / 2),
    };
    this.#updateTransform();
  }

  #applyLayout(layout) {
    this.#layout = layout;
    if (this.#response) {
      this.#response.layout = layout;
    }
    this.#revision = layout.revision;
    this.#layoutNodes = new Map(layout.nodes.map((node) => [node.block, { ...node }]));
    this.#nodeSpatialIndex = new RectSpatialIndex(this.#layoutNodes.values(), (node) => node.block);
    this.#edgeRoutes = new Map(layout.edges.map((route) => [route.edge, route]));
    this.#edgeSpatialIndex = new OrthogonalRouteSpatialIndex(layout.edges, (route) => route.edge);
    this.#stage.style.width = `${layout.canvas.width}px`;
    this.#stage.style.height = `${layout.canvas.height}px`;
    this.#canvas.setExtent(layout.canvas.width, layout.canvas.height);
    this.#canvas.configure({
      modelKey: this.#selectionKey(),
      blocks: this.#blocks,
      routes: this.#edgeRoutes,
      pins: this.#pins,
      selectedBlock: this.#selectedBlock,
      expandedBlocks: this.#expandedBlocks,
    });
    this.#renderVisibleContent(true);
    this.#updateControls();
    this.#updateTransform();
  }

  #visibleBounds() {
    if (!this.#viewport.clientWidth || !this.#viewport.clientHeight) {
      return null;
    }
    return visibleStageBounds(
      this.#viewport.clientWidth,
      this.#viewport.clientHeight,
      this.#pan,
      this.#zoom,
    );
  }

  #renderVisibleContent(force = false) {
    if (!this.#layout) {
      return;
    }
    const bounds = this.#visibleBounds();
    if (!bounds) {
      return;
    }
    const visibleBlocks = this.#nodeSpatialIndex.query(bounds);
    const visibleEdges = this.#edgeSpatialIndex.query(bounds);
    this.#canvas.renderEdges(visibleEdges, force);
    this.#canvas.renderNodes([...visibleBlocks].map((block) => this.#layoutNodes.get(block)), force);
  }

  #scheduleCull() {
    if (this.#cullFrame !== null || !this.#layout || this.#root.classList.contains('hidden')) {
      return;
    }
    this.#cullFrame = requestAnimationFrame(() => {
      this.#cullFrame = null;
      this.#renderVisibleContent();
    });
  }

  #attachNodeDrag(header, block) {
    header.addEventListener('pointerdown', (event) => {
      if (event.button !== 0 || !this.#layoutNodes.has(block) ||
          this.#relayoutEntity === this.#selectionKey()) {
        return;
      }
      event.preventDefault();
      event.stopPropagation();
      const point = screenToStage(
        event.clientX,
        event.clientY,
        this.#viewport.getBoundingClientRect(),
        this.#pan,
        this.#zoom,
      );
      const node = this.#layoutNodes.get(block);
      this.#dragGesture = {
        pointerId: event.pointerId,
        block,
        header,
        offset: { x: point.x - node.x, y: point.y - node.y },
        original: { x: node.x, y: node.y },
        priorPin: this.#pins.get(block),
        startClient: { x: event.clientX, y: event.clientY },
        moved: false,
        colliding: false,
      };
      header.setPointerCapture(event.pointerId);
    });
    header.addEventListener('pointermove', (event) => this.#moveNode(event));
    header.addEventListener('pointerup', (event) => this.#finishNodeDrag(event, false));
    header.addEventListener('pointercancel', (event) => this.#finishNodeDrag(event, true));
  }

  #moveNode(event) {
    const drag = this.#dragGesture;
    if (!drag || drag.pointerId !== event.pointerId) {
      return;
    }
    if (!drag.moved && !hasMeaningfulDrag(drag.startClient, { x: event.clientX, y: event.clientY })) {
      return;
    }
    drag.moved = true;
    const point = screenToStage(
      event.clientX,
      event.clientY,
      this.#viewport.getBoundingClientRect(),
      this.#pan,
      this.#zoom,
    );
    const position = {
      x: Math.max(0, point.x - drag.offset.x),
      y: Math.max(0, point.y - drag.offset.y),
    };
    const node = this.#layoutNodes.get(drag.block);
    if (!node) {
      return;
    }
    node.x = position.x;
    node.y = position.y;
    this.#canvas.moveNode(drag.block, position);
    drag.colliding = nodeOverlapsIndex(node, position, this.#nodeSpatialIndex);
    this.#canvas.setCollision(drag.block, drag.colliding);
    this.#renderRubberBands(drag.block);
  }

  async #finishNodeDrag(event, cancelled) {
    const drag = this.#dragGesture;
    if (!drag || drag.pointerId !== event.pointerId) {
      return;
    }
    this.#dragGesture = null;
    const node = this.#layoutNodes.get(drag.block);
    if (!node) {
      return;
    }
    if (!drag.moved) {
      return;
    }
    if (cancelled) {
      node.x = drag.original.x;
      node.y = drag.original.y;
      this.#applyLayout(this.#layout);
      return;
    }
    if (drag.colliding) {
      node.x = drag.original.x;
      node.y = drag.original.y;
      this.#applyLayout(this.#layout);
      this.#onStatus(`BB${drag.block} overlaps another block; pin rejected`);
      return;
    }
    this.#pins.set(drag.block, { x: node.x, y: node.y });
    this.#persistPins();
    this.#onClearError();
    this.#onStatus(`Re-routing CFG after pinning BB${drag.block}…`);
    const accepted = await this.#requestRelayout();
    if (accepted === false) {
      node.x = drag.original.x;
      node.y = drag.original.y;
      if (drag.priorPin) {
        this.#pins.set(drag.block, drag.priorPin);
      } else {
        this.#pins.delete(drag.block);
      }
      this.#persistPins();
      this.#applyLayout(this.#layout);
    }
  }

  #renderRubberBands(block) {
    for (const edgeId of this.#edgeIdsByBlock.get(block) ?? []) {
      const edge = this.#edges.get(edgeId);
      if (!edge) {
        continue;
      }
      const path = this.#canvas.edgeElement(edge.id);
      const source = this.#layoutNodes.get(edge.source);
      if (!path || !source) {
        continue;
      }
      const start = { x: source.x + source.width / 2, y: source.y + source.height / 2 };
      if (edge.target === null) {
        path.setAttribute('d', edgePath([start, { x: start.x, y: start.y + 42 }]));
        continue;
      }
      const target = this.#layoutNodes.get(edge.target);
      if (!target) {
        continue;
      }
      const end = { x: target.x + target.width / 2, y: target.y + target.height / 2 };
      path.setAttribute('d', edgePath([start, end]));
    }
  }

  #measurements() {
    return this.#canvas.measurements(this.#layoutNodes);
  }

  #recordGeometryOverrides(measurements) {
    let changed = false;
    for (const [block, measured] of measurements) {
      const node = this.#layoutNodes.get(block);
      if (!node || (Math.abs(measured.width - node.width) <= 0.5 &&
          Math.abs(measured.height - node.height) <= 0.5)) {
        continue;
      }
      this.#geometryOverrides.set(block, measured);
      changed = true;
    }
    if (changed) {
      this.#persistGeometryOverrides();
    }
    return changed;
  }

  #persistGeometryOverrides() {
    const selectionKey = this.#selectionKey();
    this.#geometryOverridesByEntity.delete(selectionKey);
    if (this.#geometryOverrides.size) {
      this.#geometryOverridesByEntity.set(selectionKey, new Map(this.#geometryOverrides));
    }
    while (this.#geometryOverridesByEntity.size > MAX_PINNED_ENTITIES) {
      this.#geometryOverridesByEntity.delete(this.#geometryOverridesByEntity.keys().next().value);
    }
  }

  #persistExpandedBlocks() {
    const selectionKey = this.#selectionKey();
    this.#expandedBlocksByEntity.delete(selectionKey);
    if (this.#expandedBlocks.size) {
      this.#expandedBlocksByEntity.set(selectionKey, new Set(this.#expandedBlocks));
    }
    while (this.#expandedBlocksByEntity.size > MAX_PINNED_ENTITIES) {
      this.#expandedBlocksByEntity.delete(this.#expandedBlocksByEntity.keys().next().value);
    }
  }

  async #relayoutIfMeasuredGeometryDiffers() {
    if (!this.#layout || this.#root.classList.contains('hidden')) {
      return;
    }
    const measurements = this.#measurements();
    const differs = this.#recordGeometryOverrides(measurements);
    const pinsDiffer = this.#layout.nodes.some((node) => {
      const pin = this.#pins.get(node.block);
      return Boolean(pin) !== node.pinned || (pin && (pin.x !== node.x || pin.y !== node.y));
    });
    if (differs || pinsDiffer) {
      await this.#requestRelayout();
    }
  }

  async #requestRelayout() {
    const entityAtRequest = this.#entityId;
    const methodOffsetAtRequest = this.#methodOffset;
    const selectionAtRequest = this.#selectionKey();
    const documentAtRequest = this.#response?.documentId;
    if (this.#relayoutEntity === selectionAtRequest) {
      return null;
    }
    const sequence = ++this.#relayoutSequence;
    this.#relayoutEntity = selectionAtRequest;
    this.#updateControls();
    const revision = this.#revision + 1;
    let request;
    try {
      request = buildLayoutRequest(this.#layout, this.#geometryOverrides, this.#pins, revision);
      const response = await this.#client.post(
        cfgRequestPath(entityAtRequest, methodOffsetAtRequest, true),
        request,
        'cfg-layout',
      );
      if (this.#selectionKey() !== selectionAtRequest) {
        return null;
      }
      const layout = validateLayoutResponse(response, {
        documentId: documentAtRequest,
        entityId: entityAtRequest,
        revision,
        blockIds: new Set(this.#blocks.keys()),
        edgeIds: new Set(this.#edges.keys()),
      });
      if (this.#root.classList.contains('hidden')) {
        if (this.#response) {
          this.#response.layout = layout;
        }
        return true;
      }
      this.#applyLayout(layout);
      this.#onClearError();
      this.#onStatus(`CFG layout revision ${layout.revision} applied`);
      return true;
    } catch (error) {
      if (/stale/i.test(error.message) || this.#selectionKey() !== selectionAtRequest ||
          this.#root.classList.contains('hidden')) {
        return null;
      }
      this.#onError(error);
      return false;
    } finally {
      if (this.#relayoutSequence === sequence) {
        this.#relayoutEntity = null;
        this.#updateControls();
      }
    }
  }

  async #toggleBlockExpansion(block, expanded) {
    if (!this.#layout || !this.#blocks.has(block) || this.#relayoutEntity === this.#selectionKey()) {
      return;
    }
    const wasExpanded = this.#expandedBlocks.has(block);
    if (wasExpanded === expanded) {
      return;
    }
    const previousOverride = this.#geometryOverrides.get(block);
    if (expanded) {
      this.#expandedBlocks.add(block);
    } else {
      this.#expandedBlocks.delete(block);
    }
    this.#persistExpandedBlocks();
    this.#renderVisibleContent(true);
    const measured = this.#measurements().get(block);
    if (!measured) {
      if (wasExpanded) {
        this.#expandedBlocks.add(block);
      } else {
        this.#expandedBlocks.delete(block);
      }
      this.#persistExpandedBlocks();
      this.#renderVisibleContent(true);
      return;
    }
    this.#recordGeometryOverrides(new Map([[block, measured]]));
    const presentation = blockInstructionPresentation(this.#blocks.get(block).instructions.length, expanded);
    this.#onClearError();
    this.#onStatus(
      `${expanded ? 'Expanding' : 'Collapsing'} BB${block}` +
      (presentation.limited ? ` to its first ${presentation.visibleRows} instructions` : '') + '…',
    );
    const accepted = await this.#requestRelayout();
    if (accepted !== false) {
      return;
    }
    if (wasExpanded) {
      this.#expandedBlocks.add(block);
    } else {
      this.#expandedBlocks.delete(block);
    }
    this.#persistExpandedBlocks();
    if (previousOverride) {
      this.#geometryOverrides.set(block, previousOverride);
    } else {
      this.#geometryOverrides.delete(block);
    }
    this.#persistGeometryOverrides();
    this.#applyLayout(this.#layout);
  }

  #beginPan(event) {
    if (event.button !== 0 || event.target.closest('.cfg-node')) {
      return;
    }
    this.#panGesture = {
      pointerId: event.pointerId,
      start: { x: event.clientX, y: event.clientY },
      origin: { ...this.#pan },
    };
    this.#viewport.setPointerCapture(event.pointerId);
  }

  #movePan(event) {
    const gesture = this.#panGesture;
    if (!gesture || gesture.pointerId !== event.pointerId) {
      return;
    }
    this.#pan = {
      x: gesture.origin.x + event.clientX - gesture.start.x,
      y: gesture.origin.y + event.clientY - gesture.start.y,
    };
    this.#updateTransform();
  }

  #finishPan(event) {
    if (this.#panGesture?.pointerId === event.pointerId) {
      this.#panGesture = null;
    }
  }

  #onWheel(event) {
    if (event.ctrlKey || event.metaKey) {
      event.preventDefault();
      this.#setZoom(this.#zoom * Math.exp(-event.deltaY * 0.0015), event.clientX, event.clientY);
      return;
    }
    event.preventDefault();
    this.#pan.x -= event.deltaX;
    this.#pan.y -= event.deltaY;
    this.#updateTransform();
  }

  #selectBlock(block) {
    const previous = this.#selectedBlock;
    this.#selectedBlock = block.id;
    this.#canvas.select(previous, block.id);
    this.#updateControls();
    this.#onStatus(`BB${block.id}: PC ${block.startPcHex}–${block.endPcHex}, ${block.instructionCount} instructions`);
    this.#onBlockSelect(block);
  }

  #centerBlock(block, zoom) {
    const node = this.#layoutNodes.get(block);
    if (!node || !this.#viewport.clientWidth || !this.#viewport.clientHeight) {
      return;
    }
    this.#zoom = clampZoom(zoom);
    this.#pan = centerNodePan(
      node,
      this.#viewport.clientWidth,
      this.#viewport.clientHeight,
      this.#zoom,
    );
    this.#updateTransform();
  }

  #persistPins() {
    if (this.#entityId === null) {
      return;
    }
    const selectionKey = this.#selectionKey();
    this.#pinsByEntity.delete(selectionKey);
    if (this.#pins.size) {
      this.#pinsByEntity.set(selectionKey, new Map(this.#pins));
    }
    while (this.#pinsByEntity.size > MAX_PINNED_ENTITIES) {
      this.#pinsByEntity.delete(this.#pinsByEntity.keys().next().value);
    }
    this.#updateControls();
  }

  async #resetPins(currentOnly) {
    if (!this.#layout || this.#relayoutEntity === this.#selectionKey()) {
      return;
    }
    if (currentOnly && (this.#selectedBlock === null || !this.#pins.has(this.#selectedBlock))) {
      return;
    }
    if (!currentOnly && !this.#pins.size) {
      return;
    }
    const previous = new Map(this.#pins);
    if (currentOnly) {
      this.#pins.delete(this.#selectedBlock);
    } else {
      this.#pins.clear();
    }
    this.#persistPins();
    this.#onClearError();
    this.#onStatus(currentOnly ? `Resetting BB${this.#selectedBlock}…` : 'Resetting CFG layout…');
    const accepted = await this.#requestRelayout();
    if (accepted === false) {
      this.#pins = previous;
      this.#persistPins();
      this.#applyLayout(this.#layout);
    }
  }

  #updateControls() {
    const pending = this.#entityId !== null && this.#relayoutEntity === this.#selectionKey();
    this.#resetCurrent.disabled = !this.#layout || pending || this.#selectedBlock === null ||
      !this.#pins.has(this.#selectedBlock);
    this.#resetLayout.disabled = !this.#layout || pending || !this.#pins.size;
  }

  #setZoom(value, clientX, clientY) {
    const next = clampZoom(value);
    const rect = this.#viewport.getBoundingClientRect();
    const stagePoint = screenToStage(clientX, clientY, rect, this.#pan, this.#zoom);
    this.#zoom = next;
    this.#pan = {
      x: clientX - rect.left - stagePoint.x * next,
      y: clientY - rect.top - stagePoint.y * next,
    };
    this.#updateTransform();
  }

  #updateTransform() {
    this.#stage.style.transform = `translate(${this.#pan.x}px, ${this.#pan.y}px) scale(${this.#zoom})`;
    this.#zoomLabel.textContent = `${Math.round(this.#zoom * 100)}%`;
    this.#scheduleCull();
  }

  #cancelGestures() {
    if (this.#dragGesture) {
      const { header, pointerId } = this.#dragGesture;
      if (header.hasPointerCapture?.(pointerId)) {
        header.releasePointerCapture(pointerId);
      }
    }
    if (this.#panGesture && this.#viewport.hasPointerCapture?.(this.#panGesture.pointerId)) {
      this.#viewport.releasePointerCapture(this.#panGesture.pointerId);
    }
    this.#panGesture = null;
    this.#dragGesture = null;
  }
}
