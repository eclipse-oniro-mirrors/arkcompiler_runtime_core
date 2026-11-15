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

import {
  blockInstructionPresentation,
  edgePath,
  instructionDisclosureText,
  visibleInstructionIndexes,
} from './cfg-view-model.js';

const SVG_NAMESPACE = 'http://www.w3.org/2000/svg';
const EDGE_STYLES = new Map([
  ['Fallthrough', 'normal'],
  ['Jump', 'normal'],
  ['True', 'true'],
  ['False', 'false'],
  ['Exception', 'exception'],
  ['Return', 'return'],
  ['Throw', 'throw'],
]);
// Canvas markers are SVG fill attributes, so they cannot follow CSS custom
// properties by themselves. The palette starts at the dark-theme values and
// is re-read from the document whenever a theme is applied, which keeps CSS
// the single source of color truth for the canvas.
const MARKER_TOKENS = new Map([
  ['normal', '--cfg-edge'],
  ['true', '--green'],
  ['false', '--amber'],
  ['exception', '--violet'],
  ['return', '--cfg-edge-return'],
  ['throw', '--red'],
  ['back', '--cfg-edge-back'],
]);
const MARKER_COLORS = new Map([
  ['normal', '#4d92a0'],
  ['true', '#7ee2a8'],
  ['false', '#f6c177'],
  ['exception', '#a68bff'],
  ['return', '#718198'],
  ['throw', '#ff7f87'],
  ['back', '#d78cff'],
]);

export function refreshCanvasPalette(doc = globalThis.document) {
  if (!doc || typeof doc.defaultView?.getComputedStyle !== 'function') {
    return;
  }
  const styles = doc.defaultView.getComputedStyle(doc.documentElement);
  for (const [name, token] of MARKER_TOKENS) {
    const value = styles.getPropertyValue(token).trim();
    if (value) {
      MARKER_COLORS.set(name, value);
    }
  }
}
const LABELED_EDGES = new Set(['True', 'False', 'Exception', 'Return', 'Throw']);

function html(tag, className, text) {
  const node = document.createElement(tag);
  if (className) {
    node.className = className;
  }
  if (text !== undefined) {
    node.textContent = text;
  }
  return node;
}

function svg(tag, attributes = {}) {
  const node = document.createElementNS(SVG_NAMESPACE, tag);
  Object.entries(attributes).forEach(([name, value]) => node.setAttribute(name, String(value)));
  return node;
}

function edgeLabelPoint(points) {
  if (points.length < 2) {
    return null;
  }
  let selected = [points[0], points[1]];
  let selectedLength = -1;
  for (let index = 1; index < points.length; index += 1) {
    const pair = [points[index - 1], points[index]];
    const length = Math.abs(pair[0].x - pair[1].x) + Math.abs(pair[0].y - pair[1].y);
    if (length > selectedLength) {
      selected = pair;
      selectedLength = length;
    }
  }
  return { x: (selected[0].x + selected[1].x) / 2, y: (selected[0].y + selected[1].y) / 2 };
}

function appendBadge(container, text, className = '') {
  container.append(html('span', `cfg-badge${className ? ` ${className}` : ''}`, text));
}

export class CfgCanvas {
  #edgeLayer;
  #nodeLayer;
  #onAttachDrag;
  #onSelect;
  #onCenter;
  #onToggleExpansion;
  #blocks = new Map();
  #routes = new Map();
  #pins = new Map();
  #selectedBlock = null;
  #nodeElements = new Map();
  #edgeElements = new Map();
  #visibleEdgeSignature = '';
  #lastVisibleEdges = [];
  #expandedBlocks = new Set();

  constructor({ edgeLayer, nodeLayer, onAttachDrag, onSelect, onCenter, onToggleExpansion }) {
    this.#edgeLayer = edgeLayer;
    this.#nodeLayer = nodeLayer;
    this.#onAttachDrag = onAttachDrag;
    this.#onSelect = onSelect;
    this.#onCenter = onCenter;
    this.#onToggleExpansion = onToggleExpansion;
  }

  configure({ blocks, routes, pins, selectedBlock, expandedBlocks }) {
    this.#blocks = blocks;
    this.#routes = routes;
    this.#pins = pins;
    this.#selectedBlock = selectedBlock;
    this.#expandedBlocks = expandedBlocks;
  }

  clear() {
    this.#edgeLayer.replaceChildren();
    this.#nodeLayer.replaceChildren();
    this.#nodeElements.clear();
    this.#edgeElements.clear();
    this.#visibleEdgeSignature = '';
    this.#expandedBlocks = new Set();
  }

  setExtent(width, height) {
    this.#edgeLayer.setAttribute('width', width);
    this.#edgeLayer.setAttribute('height', height);
    this.#edgeLayer.setAttribute('viewBox', `0 0 ${width} ${height}`);
  }

  nodeElement(block) {
    return this.#nodeElements.get(block);
  }

  edgeElement(edge) {
    return this.#edgeElements.get(edge);
  }

  moveNode(block, position) {
    const card = this.#nodeElements.get(block);
    if (!card) {
      return;
    }
    card.style.left = `${position.x}px`;
    card.style.top = `${position.y}px`;
  }

  setCollision(block, colliding) {
    this.#nodeElements.get(block)?.classList.toggle('collision', colliding);
  }

  select(previous, next) {
    this.#nodeElements.get(previous)?.classList.remove('selected');
    this.#nodeElements.get(next)?.classList.add('selected');
    this.#selectedBlock = next;
  }

  measurements(layoutNodes) {
    return new Map([...this.#nodeElements].map(([block, element]) => {
      const node = layoutNodes.get(block);
      return [block, {
        width: element.offsetWidth || node.width,
        height: element.offsetHeight || node.height,
      }];
    }));
  }

  renderEdges(visibleEdges, force) {
    const routes = [...visibleEdges].map((edge) => this.#routes.get(edge)).filter(Boolean);
    const signature = routes.map((route) => route.edge).join('|');
    this.#lastVisibleEdges = [...visibleEdges];
    if (!force && signature === this.#visibleEdgeSignature) {
      return;
    }
    this.#visibleEdgeSignature = signature;
    this.#edgeLayer.replaceChildren();
    this.#edgeElements.clear();
    const defs = svg('defs');
    this.#renderMarkers(defs);
    this.#edgeLayer.append(defs);
    for (const route of routes) {
      const styleCss = EDGE_STYLES.get(route.kind) ?? 'normal';
      const marker = route.backEdge ? 'back' : styleCss;
      const path = svg('path', {
        d: edgePath(route.points),
        class: `cfg-edge kind-${styleCss}${route.backEdge ? ' back-edge' : ''}`,
        'marker-end': `url(#cfg-arrow-${marker})`,
      });
      this.#edgeLayer.append(path);
      this.#edgeElements.set(route.edge, path);
      if (LABELED_EDGES.has(route.kind)) {
        const point = edgeLabelPoint(route.points);
        if (point) {
          const label = svg('text', { x: point.x, y: point.y, class: 'cfg-edge-label' });
          label.textContent = route.kind;
          this.#edgeLayer.append(label);
        }
      }
    }
  }

  // Repaints the last rendered edges. Used after a theme change: edge strokes
  // follow CSS automatically, but the SVG marker fills were painted from the
  // palette and must be re-rendered.
  refreshEdges() {
    this.renderEdges(this.#lastVisibleEdges, true);
  }

  renderNodes(visible, force) {
    if (force) {
      this.#nodeLayer.replaceChildren();
      this.#nodeElements.clear();
    }
    const wanted = new Set(visible.map((node) => node.block));
    for (const [block, card] of this.#nodeElements) {
      if (!wanted.has(block)) {
        card.remove();
        this.#nodeElements.delete(block);
      }
    }
    for (const nodeLayout of visible) {
      if (this.#nodeElements.has(nodeLayout.block)) {
        continue;
      }
      const block = this.#blocks.get(nodeLayout.block);
      if (!block) {
        continue;
      }
      const card = this.#createNode(block, nodeLayout);
      this.#nodeLayer.append(card);
      this.#nodeElements.set(block.id, card);
    }
  }

  #renderMarkers(defs) {
    for (const [name, color] of MARKER_COLORS) {
      const marker = svg('marker', {
        id: `cfg-arrow-${name}`,
        viewBox: '0 0 8 8',
        refX: 7,
        refY: 4,
        markerWidth: 8,
        markerHeight: 8,
        orient: 'auto-start-reverse',
      });
      marker.append(svg('path', { d: 'M 0 0 L 8 4 L 0 8 z', fill: color }));
      defs.append(marker);
    }
  }

  #createNode(block, nodeLayout) {
    const presentation = blockInstructionPresentation(
      block.instructions.length,
      this.#expandedBlocks.has(block.id),
    );
    const classes = ['cfg-node'];
    if (block.handler) {
      classes.push('handler');
    }
    if (block.loopHeader) {
      classes.push('loop-header');
    }
    if (block.irreducible) {
      classes.push('irreducible');
    }
    if (!block.reachable) {
      classes.push('unreachable');
    }
    if (block.id === this.#selectedBlock) {
      classes.push('selected');
    }
    const card = html('article', classes.join(' '));
    card.style.left = `${nodeLayout.x}px`;
    card.style.top = `${nodeLayout.y}px`;
    card.style.width = `${nodeLayout.width}px`;
    card.style.height = `${presentation.height}px`;
    card.dataset.block = String(block.id);

    const header = html('button', 'cfg-node-header');
    header.type = 'button';
    header.dataset.tip = `Drag BB${block.id} to pin it; PC range is half-open`;
    header.append(html('span', 'cfg-bb-id', `BB${block.id}`));
    header.append(html('span', 'cfg-pc-range', `${block.startPcHex} – ${block.endPcHex}`));
    const badges = html('span', 'cfg-badges');
    if (block.entry) {
      appendBadge(badges, 'Entry', 'entry');
    }
    if (block.handler) {
      appendBadge(badges, 'Handler', 'handler');
    }
    if (block.loopHeader) {
      appendBadge(badges, `Loop ${block.loopDepth || 1}`);
    }
    if (block.irreducible) {
      appendBadge(badges, 'Irreducible', 'warning');
    }
    if (!block.reachable) {
      appendBadge(badges, 'Unreachable', 'warning');
    }
    if (this.#pins.has(block.id)) {
      appendBadge(badges, 'Pinned', 'pinned');
    }
    header.append(badges);
    this.#onAttachDrag(header, block.id);
    card.append(header);

    const body = html('div', 'cfg-node-body');
    const instructions = html('div', 'cfg-node-instructions');
    const visibleIndexes = visibleInstructionIndexes(block.instructions.length, presentation);
    for (const index of visibleIndexes) {
      const instruction = block.instructions[index];
      const row = html('div', 'cfg-instruction');
      const omitted = index === block.instructions.length - 1
        ? block.instructions.length - visibleIndexes.length
        : 0;
      if (omitted > 0) {
        row.classList.add('truncated-tail');
        row.dataset.omittedInstructions = String(omitted);
      }
      const omissionTitle = omitted > 0
        ? `${omitted} omitted instruction${omitted === 1 ? '' : 's'} · `
        : '';
      row.dataset.tip = `${omissionTitle}${instruction.mnemonic}${instruction.operands ? ` ${instruction.operands}` : ''}`;
      row.append(html('span', 'pc', instruction.pcHex));
      row.append(html('span', 'mnemonic', instruction.mnemonic));
      row.append(html('span', 'operands', instruction.operands || '—'));
      instructions.append(row);
    }
    body.append(instructions);
    if (presentation.collapsible) {
      const disclosure = html(
        'button',
        `cfg-node-disclosure${presentation.limited ? ' limited' : ''}`,
        instructionDisclosureText(block.instructions.length, presentation),
      );
      disclosure.type = 'button';
      disclosure.dataset.tip = presentation.limited
        ? 'This block is unusually large; use Disassembly for every instruction'
        : presentation.expanded ? `Collapse BB${block.id}` : `Expand BB${block.id}`;
      disclosure.addEventListener('click', (event) => {
        event.preventDefault();
        event.stopPropagation();
        this.#onToggleExpansion(block.id, !presentation.expanded);
      });
      disclosure.addEventListener('dblclick', (event) => event.stopPropagation());
      body.append(disclosure);
    }
    card.append(body);

    const footer = html('div', 'cfg-node-footer');
    footer.append(html('span', '', `${block.instructionCount} instruction${block.instructionCount === 1 ? '' : 's'}`));
    footer.append(html('span', '', `idom ${block.immediateDominator === null ? '—' : `BB${block.immediateDominator}`}`));
    footer.append(html('span', '', `SCC ${block.scc ?? '—'}`));
    card.append(footer);
    card.addEventListener('click', () => this.#onSelect(block));
    card.addEventListener('dblclick', () => this.#onCenter(block.id));
    return card;
  }
}
