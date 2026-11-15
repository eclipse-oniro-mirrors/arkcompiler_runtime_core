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

import { cfgRequestPath, cfgSelectionKey } from '../src/cfg-view.js';
import { CfgCanvas } from '../src/cfg-canvas.js';

class FakeElement {
  constructor(tag) {
    this.tag = tag;
    this.children = [];
    this.style = {};
    this.dataset = {};
    this.className = '';
    this.classList = { add() {}, remove() {}, toggle() {} };
  }
  append(...children) { this.children.push(...children); }
  replaceChildren(...children) { this.children = [...children]; }
  addEventListener() {}
  setAttribute(name, value) { this[name] = String(value); }
  remove() {}
}

function instruction(pc) {
  return { pc, pcHex: `0x${pc.toString(16)}`, mnemonic: pc === 12 ? 'jmp' : 'nop', operands: '' };
}

test('CFG request paths retain the exact selected method offset', () => {
  assert.equal(cfgRequestPath('42', 0x123n), '/api/v1/cfg?id=42&methodOffset=0x123');
  assert.equal(cfgRequestPath('42', 0x123n, true), '/api/v1/cfg/layout?id=42&methodOffset=0x123');
  assert.equal(cfgRequestPath('42', null), '/api/v1/cfg?id=42');
  assert.equal(cfgSelectionKey('42', 0x123n), '42@0x123');
  assert.throws(() => cfgRequestPath('042', 0x123n), /entity/i);
  assert.throws(() => cfgRequestPath('42', -1n), /offset/i);
});

test('CFG canvas collapsed preview renders the true last instruction PC', () => {
  const previousDocument = globalThis.document;
  globalThis.document = {
    createElement: (tag) => new FakeElement(tag),
    createElementNS: (_namespace, tag) => new FakeElement(tag),
  };
  try {
    const nodeLayer = new FakeElement('div');
    const canvas = new CfgCanvas({
      edgeLayer: new FakeElement('svg'), nodeLayer,
      onAttachDrag() {}, onSelect() {}, onCenter() {}, onToggleExpansion() {},
    });
    const instructions = Array.from({ length: 13 }, (_, pc) => instruction(pc));
    const block = {
      id: '0', instructions, instructionCount: 13, startPcHex: '0x0', endPcHex: '0xd',
      entry: true, handler: false, loopHeader: false, irreducible: false, reachable: true,
      immediateDominator: null, scc: 0,
    };
    canvas.configure({
      blocks: new Map([['0', block]]), routes: new Map(), pins: new Map(),
      selectedBlock: null, expandedBlocks: new Set(),
    });
    canvas.renderNodes([{ block: '0', x: 0, y: 0, width: 420, height: 452 }], true);
    const rows = nodeLayer.children[0].children[1].children[0].children;
    assert.equal(rows.length, 12);
    assert.equal(rows.at(-1).children[0].textContent, '0xc');
    assert.match(rows.at(-1).dataset.tip, /1 omitted instruction/);
  } finally {
    globalThis.document = previousDocument;
  }
});
