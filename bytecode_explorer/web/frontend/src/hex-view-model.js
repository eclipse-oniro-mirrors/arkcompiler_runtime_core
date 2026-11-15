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

export const BYTES_PER_ROW = 16;
export const DEFAULT_WINDOW_SIZE = 16 * 1024;
export const WINDOW_SHIFT_SIZE = DEFAULT_WINDOW_SIZE / 2;
export const DEFAULT_OVERSCAN_ROWS = 8;

function requireBigInt(value, label) {
  if (typeof value !== 'bigint' || value < 0n) {
    throw new TypeError(`${label} must be a non-negative BigInt`);
  }
  return value;
}

function requireNonNegativeInteger(value, label) {
  if (!Number.isSafeInteger(value) || value < 0) {
    throw new TypeError(`${label} must be a non-negative integer`);
  }
  return value;
}

function sizeAsBigInt(value) {
  if (typeof value === 'bigint') {
    if (value < 0n) {
      throw new TypeError('range size must be a non-negative integer');
    }
    return value;
  }
  return BigInt(requireNonNegativeInteger(value, 'range size'));
}

function offsetFromWire(value) {
  if (typeof value === 'bigint') {
    return requireBigInt(value, 'range offset');
  }
  if (typeof value !== 'string') {
    throw new TypeError('range offset must be a hexadecimal string or BigInt');
  }

  const text = value.trim();
  if (!/^(?:0[xX])?[0-9a-fA-F]+$/.test(text)) {
    throw new TypeError('range offset must be non-negative hexadecimal');
  }
  return BigInt(/^0[xX]/.test(text) ? text : `0x${text}`);
}

function requireBytesPerRow(value) {
  if (value !== 8 && value !== 16) {
    throw new TypeError('bytes per row must be 8 or 16');
  }
  return value;
}

function alignDownToRow(value, bytesPerRow) {
  const rowSize = BigInt(requireBytesPerRow(bytesPerRow));
  return value - value % rowSize;
}

function alignUpToRow(value, bytesPerRow) {
  const rowSize = BigInt(requireBytesPerRow(bytesPerRow));
  return alignDownToRow(value + rowSize - 1n, bytesPerRow);
}

function maximumWindowStart(fileSize, windowSize, bytesPerRow) {
  if (fileSize <= windowSize) {
    return 0n;
  }
  return alignUpToRow(fileSize - windowSize, bytesPerRow);
}

export function parseOffsetText(text, fileSize) {
  const limit = requireBigInt(fileSize, 'file size');
  if (typeof text !== 'string') {
    throw new TypeError('offset must be hexadecimal text');
  }

  const trimmed = text.trim();
  if (!/^(?:0[xX])?[0-9a-fA-F]+$/.test(trimmed)) {
    throw new TypeError('offset must be a non-negative hexadecimal value');
  }

  const offset = BigInt(/^0[xX]/.test(trimmed) ? trimmed : `0x${trimmed}`);
  if (offset >= limit) {
    throw new RangeError('offset is outside the file');
  }
  return offset;
}

export function formatOffset(offset, minimumDigits = 8) {
  requireBigInt(offset, 'offset');
  requireNonNegativeInteger(minimumDigits, 'minimum digits');
  return `0x${offset.toString(16).padStart(minimumDigits, '0')}`;
}

export function rangeFromWire(range) {
  if (range === null || typeof range !== 'object') {
    throw new TypeError('range must be an object');
  }
  return {
    offset: offsetFromWire(range.offset),
    size: sizeAsBigInt(range.size),
  };
}

function validNodeRange(range, fileSize) {
  try {
    const canonical = rangeFromWire(range);
    if (canonical.size === 0n || canonical.offset >= fileSize) {
      return null;
    }
    return canonical.offset + canonical.size <= fileSize ? canonical : null;
  } catch {
    return null;
  }
}

export function preferredNodeRange(node, fileSize) {
  const limit = requireBigInt(fileSize, 'file size');
  if (node === null || typeof node !== 'object') {
    return null;
  }

  const primary = validNodeRange(node.primaryRange, limit);
  if (primary !== null) {
    return primary;
  }

  for (const backing of Array.isArray(node.backing) ? node.backing : []) {
    const candidate = validNodeRange(backing?.range, limit);
    if (candidate !== null) {
      return candidate;
    }
  }
  return null;
}

export function windowStartForOffset(offset, fileSize, windowSize = DEFAULT_WINDOW_SIZE,
                                     bytesPerRow = BYTES_PER_ROW) {
  const target = requireBigInt(offset, 'offset');
  const limit = requireBigInt(fileSize, 'file size');
  const size = BigInt(requireNonNegativeInteger(windowSize, 'window size'));
  if (size === 0n) {
    throw new RangeError('window size must be positive');
  }
  if (target >= limit) {
    throw new RangeError('offset is outside the file');
  }

  const quarterWindow = size / 4n;
  const desired = target > quarterWindow ? target - quarterWindow : 0n;
  requireBytesPerRow(bytesPerRow);
  const aligned = alignDownToRow(desired, bytesPerRow);
  const maximum = maximumWindowStart(limit, size, bytesPerRow);
  return aligned < maximum ? aligned : maximum;
}

export function shiftWindowStart(currentStart, direction, fileSize,
                                 windowSize = DEFAULT_WINDOW_SIZE, bytesPerRow = BYTES_PER_ROW) {
  requireBytesPerRow(bytesPerRow);
  const current = alignDownToRow(requireBigInt(currentStart, 'window start'), bytesPerRow);
  const limit = requireBigInt(fileSize, 'file size');
  const size = BigInt(requireNonNegativeInteger(windowSize, 'window size'));
  if (size === 0n) {
    throw new RangeError('window size must be positive');
  }

  const upward = direction === 'up' || direction === -1;
  const downward = direction === 'down' || direction === 1;
  if (!upward && !downward) {
    throw new TypeError('window shift direction must be up or down');
  }

  let shifted;
  if (upward) {
    shifted = current > BigInt(WINDOW_SHIFT_SIZE) ? current - BigInt(WINDOW_SHIFT_SIZE) : 0n;
  } else {
    shifted = current + BigInt(WINDOW_SHIFT_SIZE);
  }
  const maximum = maximumWindowStart(limit, size, bytesPerRow);
  return shifted < maximum ? shifted : maximum;
}

export function visibleRowInterval(scrollTop, viewportHeight, rowHeight, rowCount,
                                   overscan = DEFAULT_OVERSCAN_ROWS) {
  if (!Number.isFinite(scrollTop) || !Number.isFinite(viewportHeight) ||
      !Number.isFinite(rowHeight) || rowHeight <= 0) {
    throw new TypeError('scroll and row dimensions must be finite with a positive row height');
  }
  requireNonNegativeInteger(rowCount, 'row count');
  requireNonNegativeInteger(overscan, 'overscan');

  const top = Math.max(0, scrollTop);
  const height = Math.max(0, viewportHeight);
  const firstVisible = Math.floor(top / rowHeight);
  if (height === 0) {
    const currentRow = Math.max(0, Math.min(rowCount, firstVisible));
    return { firstRow: currentRow, lastRowExclusive: currentRow };
  }
  const lastVisibleExclusive = Math.ceil((top + height) / rowHeight);
  return {
    firstRow: Math.max(0, Math.min(rowCount, firstVisible - overscan)),
    lastRowExclusive: Math.max(0, Math.min(rowCount, lastVisibleExclusive + overscan)),
  };
}

export function asciiForByte(byte) {
  return Number.isInteger(byte) && byte >= 0x20 && byte <= 0x7e ? String.fromCharCode(byte) : '.';
}

export function buildVisibleRows(windowModel, firstRow, lastRowExclusive, bytesPerRow = BYTES_PER_ROW) {
  const windowOffset = requireBigInt(windowModel?.offset, 'window offset');
  if (!Array.isArray(windowModel.bytes) && !ArrayBuffer.isView(windowModel.bytes)) {
    throw new TypeError('window bytes must be an array of byte values');
  }
  requireNonNegativeInteger(firstRow, 'first row');
  requireNonNegativeInteger(lastRowExclusive, 'last row');

  requireBytesPerRow(bytesPerRow);
  const rowCount = Math.ceil(windowModel.bytes.length / bytesPerRow);
  const first = Math.min(firstRow, rowCount);
  const last = Math.max(first, Math.min(lastRowExclusive, rowCount));
  const selection = windowModel.selection === null ? null : rangeFromWire(windowModel.selection);
  const cursorOffset = windowModel.cursorOffset === null
    ? null
    : requireBigInt(windowModel.cursorOffset, 'cursor offset');
  const rows = [];

  for (let rowIndex = first; rowIndex < last; ++rowIndex) {
    const byteIndex = rowIndex * bytesPerRow;
    const rowOffset = windowOffset + BigInt(byteIndex);
    const cells = Array(bytesPerRow).fill(null);
    for (let column = 0; column < bytesPerRow; ++column) {
      const index = byteIndex + column;
      if (index >= windowModel.bytes.length) {
        break;
      }

      const value = windowModel.bytes[index];
      if (!Number.isInteger(value) || value < 0 || value > 0xff) {
        throw new TypeError('window bytes must contain integer values from 0 through 255');
      }
      const offset = windowOffset + BigInt(index);
      const selected = selection !== null &&
        offset >= selection.offset && offset < selection.offset + selection.size;
      cells[column] = {
        offset,
        value,
        hex: value.toString(16).padStart(2, '0'),
        ascii: asciiForByte(value),
        selected,
        cursor: cursorOffset === offset,
      };
    }
    rows.push({ offset: rowOffset, offsetText: formatOffset(rowOffset), cells });
  }
  return rows;
}

export function intersectRanges(left, right) {
  const canonicalLeft = rangeFromWire(left);
  const canonicalRight = rangeFromWire(right);
  const start = canonicalLeft.offset > canonicalRight.offset
    ? canonicalLeft.offset
    : canonicalRight.offset;
  const leftEnd = canonicalLeft.offset + canonicalLeft.size;
  const rightEnd = canonicalRight.offset + canonicalRight.size;
  const end = leftEnd < rightEnd ? leftEnd : rightEnd;
  return start < end ? { offset: start, size: end - start } : null;
}

export function findInstructionAtOffset(instructions, offset) {
  const target = requireBigInt(offset, 'offset');
  for (const instruction of instructions) {
    try {
      const range = rangeFromWire(instruction.fileRange);
      if (target >= range.offset && target < range.offset + range.size) {
        return instruction;
      }
    } catch {
      // Ignore instructions that have no usable file range.
    }
  }
  return null;
}

export function scrollTopForAnchor(anchorOffset, windowOffset, rowHeight, bytesPerRow = BYTES_PER_ROW) {
  const anchor = requireBigInt(anchorOffset, 'anchor offset');
  const start = requireBigInt(windowOffset, 'window offset');
  if (!Number.isFinite(rowHeight) || rowHeight <= 0) {
    throw new TypeError('row height must be a positive finite number');
  }

  const rowSize = BigInt(requireBytesPerRow(bytesPerRow));
  const delta = anchor - start;
  if (delta < 0n || delta >= BigInt(DEFAULT_WINDOW_SIZE)) {
    throw new RangeError('anchor is outside the current window');
  }
  if (delta % rowSize !== 0n) {
    throw new RangeError('anchor must be row-aligned within the current window');
  }

  const rowDifference = delta / rowSize;
  const scrollTop = Number(rowDifference) * rowHeight;
  if (!Number.isFinite(scrollTop)) {
    throw new RangeError('anchor scroll position is outside the supported pixel range');
  }
  return scrollTop;
}
