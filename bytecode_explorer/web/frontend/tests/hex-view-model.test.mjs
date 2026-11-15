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
  BYTES_PER_ROW,
  DEFAULT_OVERSCAN_ROWS,
  DEFAULT_WINDOW_SIZE,
  WINDOW_SHIFT_SIZE,
  asciiForByte,
  buildVisibleRows,
  findInstructionAtOffset,
  formatOffset,
  intersectRanges,
  parseOffsetText,
  preferredNodeRange,
  rangeFromWire,
  scrollTopForAnchor,
  shiftWindowStart,
  visibleRowInterval,
  windowStartForOffset,
} from '../src/hex-view-model.js';

test('hex model defaults use 16-byte rows and half-window shifts', () => {
  assert.equal(BYTES_PER_ROW, 16);
  assert.equal(DEFAULT_WINDOW_SIZE, 16 * 1024);
  assert.equal(WINDOW_SHIFT_SIZE, 8 * 1024);
  assert.equal(DEFAULT_OVERSCAN_ROWS, 8);
});

test('offsets stay exact as BigInt and format canonically', () => {
  const huge = parseOffsetText('0x20000000000001', 0x30000000000000n);
  assert.equal(huge, 0x20000000000001n);
  assert.equal(parseOffsetText('2a', 0x100n), 0x2an);
  assert.equal(formatOffset(huge), '0x20000000000001');
  assert.equal(formatOffset(0x2an), '0x0000002a');
  assert.throws(() => parseOffsetText('', 0x100n), /offset/i);
  assert.throws(() => parseOffsetText('-1', 0x100n), /offset/i);
  assert.throws(() => parseOffsetText('0xzz', 0x100n), /offset/i);
  assert.throws(() => parseOffsetText('0x100', 0x100n), /outside/i);
});

test('wire ranges become canonical BigInt ranges and reject invalid sizes', () => {
  assert.deepEqual(rangeFromWire({ offset: '0x12A0', size: 16 }), {
    offset: 0x12a0n,
    size: 16n,
  });
  assert.throws(() => rangeFromWire({ offset: '0x10', size: -1 }), /size/i);
  assert.throws(() => rangeFromWire({ offset: '0x10', size: 1.5 }), /size/i);
});

test('structure range preference is primary then first valid backing', () => {
  const primary = { offset: '0x20', size: 4 };
  assert.deepEqual(preferredNodeRange({
    primaryRange: primary,
    backing: [{ range: { offset: '0x50', size: 8 } }],
  }, 0x100n), { offset: 0x20n, size: 4n });

  const node = {
    primaryRange: null,
    backing: [
      { range: { offset: '0x40', size: 0 } },
      { range: { offset: '0x50', size: 8 } },
      { range: { offset: '0x60', size: 4 } },
    ],
  };
  assert.deepEqual(preferredNodeRange(node, 0x100n), { offset: 0x50n, size: 8n });
  assert.equal(preferredNodeRange({ primaryRange: { offset: '0x10', size: 0 }, backing: [] }, 0x100n), null);
});

test('initial window jumps place the target near one quarter and clamp at file bounds', () => {
  assert.equal(windowStartForOffset(0x5001n, 0x10000n), 0x4000n);
  assert.equal(windowStartForOffset(0x000fn, 0x10000n), 0n);
  assert.equal(windowStartForOffset(0xffffn, 0x10000n), 0xc000n);
  assert.equal(windowStartForOffset(0x20n, 0x100n), 0n);

  const huge = windowStartForOffset(0x20000000000001n, 0x30000000000000n);
  assert.equal(huge, 0x1ffffffffff000n);
});

test('window shifts move by half a window and clamp at file start and end', () => {
  const fileSize = 0x10000n;
  assert.equal(shiftWindowStart(0x4000n, 'down', fileSize), 0x6000n);
  assert.equal(shiftWindowStart(0x4000n, 'up', fileSize), 0x2000n);
  assert.equal(shiftWindowStart(0x1000n, 'up', fileSize), 0n);
  assert.equal(shiftWindowStart(0xb000n, 'down', fileSize), 0xc000n);
  assert.equal(shiftWindowStart(0n, 'down', 0x1000n), 0n);
});

test('file-end clamping keeps the final partial row inside the window', () => {
  const fileSize = 0x10003n;
  const start = windowStartForOffset(fileSize - 1n, fileSize);

  assert.equal(start, 0xc010n);
  assert.ok(start <= fileSize - 1n);
  assert.ok(fileSize - 1n < start + BigInt(DEFAULT_WINDOW_SIZE));
  assert.equal(shiftWindowStart(0xb000n, 'down', fileSize), start);
  assert.equal(windowStartForOffset(fileSize - 1n, fileSize, DEFAULT_WINDOW_SIZE, 8), 0xc008n);
  assert.equal(shiftWindowStart(0xb000n, 'down', fileSize, DEFAULT_WINDOW_SIZE, 8), 0xc008n);
});

test('visible row intervals apply overscan and remain half-open at viewport boundaries', () => {
  assert.deepEqual(visibleRowInterval(100, 50, 10, 100, 2), {
    firstRow: 8,
    lastRowExclusive: 17,
  });
  assert.deepEqual(visibleRowInterval(0, 20, 10, 100), {
    firstRow: 0,
    lastRowExclusive: 10,
  });
  assert.deepEqual(visibleRowInterval(950, 100, 10, 100, 8), {
    firstRow: 87,
    lastRowExclusive: 100,
  });
});

test('a zero-height viewport produces an empty interval at the current row', () => {
  assert.deepEqual(visibleRowInterval(15, 0, 10, 100), {
    firstRow: 1,
    lastRowExclusive: 1,
  });
});

test('ASCII output includes exactly the printable byte range', () => {
  assert.equal(asciiForByte(0x1f), '.');
  assert.equal(asciiForByte(0x20), ' ');
  assert.equal(asciiForByte(0x7e), '~');
  assert.equal(asciiForByte(0x7f), '.');
});

test('visible rows keep exact offsets, fixed cells, partial tails, selection, and cursor', () => {
  const offset = 0x20000000000001n;
  const rows = buildVisibleRows({
    offset,
    bytes: Array.from({ length: 18 }, (_, index) => index),
    selection: { offset: offset + 15n, size: 2n },
    cursorOffset: offset + 17n,
  }, 0, 2);

  assert.equal(rows.length, 2);
  assert.equal(rows[0].offset, offset);
  assert.equal(rows[0].offsetText, '0x20000000000001');
  assert.equal(rows[0].cells.length, 16);
  assert.deepEqual(rows[0].cells[0], {
    offset,
    value: 0,
    hex: '00',
    ascii: '.',
    selected: false,
    cursor: false,
  });
  assert.equal(rows[0].cells[15].selected, true);
  assert.equal(rows[1].cells[0].selected, true);
  assert.deepEqual(rows[1].cells[1], {
    offset: offset + 17n,
    value: 17,
    hex: '11',
    ascii: '.',
    selected: false,
    cursor: true,
  });
  assert.equal(rows[1].cells.length, 16);
  assert.deepEqual(rows[1].cells.slice(2), Array(14).fill(null));
});

test('eight-byte rows preserve exact selection across their row boundary', () => {
  const offset = 0x1000n;
  const rows = buildVisibleRows({
    offset,
    bytes: Array.from({ length: 18 }, (_, index) => index),
    selection: { offset: offset + 7n, size: 2n },
    cursorOffset: offset + 8n,
  }, 0, 3, 8);

  assert.equal(rows.length, 3);
  assert.equal(rows[0].cells.length, 8);
  assert.equal(rows[0].cells[7].selected, true);
  assert.equal(rows[1].offset, offset + 8n);
  assert.equal(rows[1].cells[0].selected, true);
  assert.equal(rows[1].cells[0].cursor, true);
  assert.deepEqual(rows[2].cells.slice(2), Array(6).fill(null));
});

test('visible row construction clips to only the requested rows and window bytes', () => {
  const offset = 0x1000n;
  const rows = buildVisibleRows({
    offset,
    bytes: Array.from({ length: 64 }, (_, index) => index),
    selection: { offset: offset - 2n, size: 4n },
    cursorOffset: null,
  }, 0, 1);

  assert.equal(rows.length, 1);
  assert.equal(rows[0].cells[0].selected, true);
  assert.equal(rows[0].cells[1].selected, true);
  assert.equal(rows[0].cells[2].selected, false);

  const thirdRowOnly = buildVisibleRows({
    offset,
    bytes: Array.from({ length: 64 }, (_, index) => index),
    selection: null,
    cursorOffset: null,
  }, 2, 3);
  assert.equal(thirdRowOnly.length, 1);
  assert.equal(thirdRowOnly[0].offset, 0x1020n);
  assert.equal(thirdRowOnly[0].cells[0].value, 32);
});

test('range intersection observes both boundaries of half-open ranges', () => {
  assert.deepEqual(intersectRanges(
    { offset: 0x100n, size: 0x10n },
    { offset: 0x108n, size: 0x10n },
  ), { offset: 0x108n, size: 8n });
  assert.equal(intersectRanges(
    { offset: 0x100n, size: 0x10n },
    { offset: 0x110n, size: 1n },
  ), null);
  assert.equal(intersectRanges(
    { offset: 0x100n, size: 0n },
    { offset: 0x100n, size: 1n },
  ), null);
});

test('instruction matching uses half-open file ranges', () => {
  const instruction = { fileRange: { offset: '0x80', size: 2 } };
  assert.equal(findInstructionAtOffset([instruction], 0x80n), instruction);
  assert.equal(findInstructionAtOffset([instruction], 0x81n), instruction);
  assert.equal(findInstructionAtOffset([instruction], 0x82n), null);
});

test('absolute top-row anchors survive down and up 8 KiB window shifts', () => {
  const rowHeight = 16;
  const anchor = 12n * 1024n;

  const downStart = shiftWindowStart(0n, 'down', 64n * 1024n);
  const downScrollTop = scrollTopForAnchor(anchor, downStart, rowHeight);
  assert.equal(downScrollTop, (4 * 1024 / BYTES_PER_ROW) * rowHeight);
  assert.equal(downStart + BigInt(downScrollTop / rowHeight) * 16n, anchor);

  const upStart = shiftWindowStart(8n * 1024n, 'up', 64n * 1024n);
  const upScrollTop = scrollTopForAnchor(anchor, upStart, rowHeight);
  assert.equal(upScrollTop, (12 * 1024 / BYTES_PER_ROW) * rowHeight);
  assert.equal(upStart + BigInt(upScrollTop / rowHeight) * 16n, anchor);
});

test('anchor restoration uses the active eight-byte row geometry', () => {
  const start = 8n * 1024n;
  const anchor = 12n * 1024n;
  const scrollTop = scrollTopForAnchor(anchor, start, 16, 8);
  assert.equal(scrollTop, (4 * 1024 / 8) * 16);
  assert.equal(start + BigInt(scrollTop / 16) * 8n, anchor);
  assert.throws(() => scrollTopForAnchor(anchor + 1n, start, 16, 8), /row-aligned/i);
});

test('anchor restoration rejects an anchor before the window', () => {
  const start = 0x20000000000000n;
  assert.throws(
    () => scrollTopForAnchor(start - 16n, start, 16),
    { name: 'RangeError', message: /current window/i },
  );
});

test('anchor restoration rejects an anchor at or beyond the default window end', () => {
  const start = 0x20000000000000n;
  assert.throws(
    () => scrollTopForAnchor(start + BigInt(DEFAULT_WINDOW_SIZE), start, 16),
    { name: 'RangeError', message: /current window/i },
  );
});

test('anchor restoration rejects an anchor that is not row-aligned', () => {
  const start = 0x20000000000000n;
  assert.throws(
    () => scrollTopForAnchor(start + 1n, start, 16),
    { name: 'RangeError', message: /row-aligned/i },
  );
});

test('anchor restoration rejects invalid row heights', () => {
  const start = 0x20000000000000n;
  assert.throws(() => scrollTopForAnchor(start, start, 0), /row height/i);
  assert.throws(() => scrollTopForAnchor(start, start, Infinity), /row height/i);
});
