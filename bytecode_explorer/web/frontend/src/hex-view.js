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
  BYTES_PER_ROW,
  DEFAULT_WINDOW_SIZE,
  buildVisibleRows,
  findInstructionAtOffset,
  formatOffset,
  parseOffsetText,
  rangeFromWire,
  scrollTopForAnchor,
  shiftWindowStart,
  visibleRowInterval,
  windowStartForOffset,
} from './hex-view-model.js';
import { hexBytesPerRow } from './workbench-layout-model.js';

const ROW_HEIGHT = 28;

function isStale(error) {
  return /stale/i.test(error instanceof Error ? error.message : String(error));
}

function rowAnchor(offset, windowOffset, bytesPerRow) {
  const delta = offset - windowOffset;
  return windowOffset + delta - delta % BigInt(bytesPerRow);
}

export class HexView {
  #client;
  #elements;
  #onStatus;
  #onError;
  #onClearError;
  #onLocation;
  #fileSize = 0n;
  #visible = false;
  #windowStart = 0n;
  #window = null;
  #selection = null;
  #selectionSource = null;
  #cursorOffset = null;
  #instructionEntityId = null;
  #instructions = [];
  #windowRequestId = 0;
  #windowRequestPending = false;
  #locationRequestId = 0;
  #shifting = false;
  #suppressWindowShift = false;
  #noticeSticky = false;
  #bytesPerRow = BYTES_PER_ROW;

  constructor({ client, elements, onStatus, onError, onClearError, onLocation }) {
    this.#client = client;
    this.#elements = elements;
    this.#onStatus = onStatus;
    this.#onError = onError;
    this.#onClearError = onClearError;
    this.#onLocation = onLocation;
    this.#elements.root.dataset.bytesPerRow = String(this.#bytesPerRow);

    elements.viewport.addEventListener('scroll', () => this.#handleScroll());
    elements.jumpForm.addEventListener('submit', (event) => {
      event.preventDefault();
      this.#jumpToInput();
    });
  }

  setFileSize(fileSize) {
    if (typeof fileSize !== 'bigint' || fileSize < 0n) {
      throw new TypeError('file size must be a non-negative BigInt');
    }
    // A document switch must also drop in-flight window requests of the
    // previous document, or a late response would render its old bytes.
    this.#invalidateWindowOperation();
    this.#fileSize = fileSize;
    this.#windowStart = 0n;
    this.#window = null;
    this.#selection = null;
    this.#selectionSource = null;
    this.#cursorOffset = null;
    this.#elements.offset.value = formatOffset(0n);
    this.#elements.spacer.style.height = '0px';
    this.#elements.rows.replaceChildren();
    this.#updateSummary();
  }

  show() {
    this.#visible = true;
    this.#elements.root.classList.remove('hidden');
    queueMicrotask(() => {
      if (this.#visible && this.#window === null && !this.#windowRequestPending) {
        this.#fetchWindow(this.#windowStart);
      } else if (this.#visible) {
        this.#render();
      }
    });
  }

  hide() {
    this.#invalidateLocationOperation();
    this.#invalidateWindowOperation();
    this.#visible = false;
    this.#elements.root.classList.add('hidden');
  }

  resize(panelWidth) {
    const next = hexBytesPerRow(panelWidth);
    if (next === this.#bytesPerRow) {
      return;
    }
    let anchor = null;
    if (this.#window !== null && this.#window.bytes.length !== 0) {
      const topRow = Math.floor(this.#elements.viewport.scrollTop / ROW_HEIGHT);
      anchor = this.#window.offset + BigInt(topRow * this.#bytesPerRow);
    }
    this.#bytesPerRow = next;
    this.#elements.root.dataset.bytesPerRow = String(next);
    if (anchor !== null) {
      const aligned = rowAnchor(anchor, this.#window.offset, this.#bytesPerRow);
      this.#elements.viewport.scrollTop = scrollTopForAnchor(
        aligned, this.#window.offset, ROW_HEIGHT, this.#bytesPerRow,
      );
    }
    this.#render();
  }

  async revealRange(range, source) {
    this.#invalidateLocationOperation();
    this.#invalidateWindowOperation();
    if (range === null) {
      this.#selection = null;
      this.#selectionSource = null;
      this.#cursorOffset = null;
      this.#showNotice('This node has no direct ABC file range', true);
      this.#updateSummary();
      this.#render();
      return;
    }

    const selection = rangeFromWire(range);
    if (selection.size === 0n || selection.offset >= this.#fileSize) {
      this.#selection = null;
      this.#selectionSource = null;
      this.#cursorOffset = null;
      this.#showNotice('This node has no direct ABC file range', true);
      this.#updateSummary();
      this.#render();
      return;
    }

    this.#selection = selection;
    this.#selectionSource = source;
    this.#cursorOffset = source === 'hex' ? selection.offset : null;
    this.#elements.offset.value = formatOffset(selection.offset);
    this.#updateSummary();
    this.#clearNotice();
    const windowEnd = this.#window === null
      ? 0n
      : this.#window.offset + BigInt(this.#window.bytes.length);
    if ((source === 'instruction' || source === 'cfg') && this.#window !== null &&
        selection.offset >= this.#window.offset && selection.offset + selection.size <= windowEnd) {
      this.#scrollToOffset(selection.offset);
      this.#onStatus(`Showing cached ABC bytes at ${formatOffset(selection.offset)}`);
      return;
    }
    const start = windowStartForOffset(
      selection.offset, this.#fileSize, DEFAULT_WINDOW_SIZE, this.#bytesPerRow,
    );
    if (await this.#fetchWindow(start)) {
      this.#scrollToOffset(selection.offset);
    }
  }

  setInstructionContext(entityId, instructions) {
    this.#instructionEntityId = entityId;
    this.#instructions = Array.isArray(instructions) ? [...instructions] : [];
  }

  clearInstructionContext() {
    this.#invalidateLocationOperation();
    this.#instructionEntityId = null;
    this.#instructions = [];
  }

  async #fetchWindow(start, anchor = null) {
    const requestId = ++this.#windowRequestId;
    this.#windowRequestPending = true;
    this.#onStatus(`Loading ABC bytes at ${formatOffset(start)}…`);
    try {
      const response = await this.#client.get(
        `/api/v1/hex?offset=${encodeURIComponent(formatOffset(start, 1))}&size=${DEFAULT_WINDOW_SIZE}`,
        'hex-window',
      );
      if (requestId !== this.#windowRequestId) {
        return false;
      }
      const responseWindow = response?.window;
      const canonical = rangeFromWire({ offset: responseWindow?.offset, size: responseWindow?.size });
      if (!Array.isArray(responseWindow?.bytes) || canonical.size !== BigInt(responseWindow.bytes.length)) {
        throw new Error('Hex response contains an invalid byte window');
      }

      this.#windowStart = canonical.offset;
      this.#window = {
        offset: canonical.offset,
        bytes: [...responseWindow.bytes],
        eof: responseWindow.eof === true,
      };
      if (!this.#noticeSticky) {
        this.#clearNotice();
      }
      this.#onClearError();
      this.#render();
      if (anchor !== null) {
        this.#restoreAnchor(anchor);
      }
      this.#onStatus(`Showing ${responseWindow.bytes.length} ABC bytes at ${formatOffset(canonical.offset)}`);
      return true;
    } catch (error) {
      if (requestId === this.#windowRequestId && !isStale(error)) {
        this.#onError(error);
      }
      return false;
    } finally {
      if (requestId === this.#windowRequestId) {
        this.#windowRequestPending = false;
      }
    }
  }

  #render() {
    if (!this.#visible || this.#window === null) {
      return;
    }
    const rowCount = Math.ceil(this.#window.bytes.length / this.#bytesPerRow);
    this.#elements.spacer.style.height = `${rowCount * ROW_HEIGHT}px`;
    if (rowCount === 0) {
      this.#elements.rows.replaceChildren();
      this.#showNotice('End of file');
      return;
    }

    const interval = visibleRowInterval(
      this.#elements.viewport.scrollTop,
      this.#elements.viewport.clientHeight,
      ROW_HEIGHT,
      rowCount,
    );
    const rows = buildVisibleRows({
      offset: this.#window.offset,
      bytes: this.#window.bytes,
      selection: this.#selection,
      cursorOffset: this.#cursorOffset,
    }, interval.firstRow, interval.lastRowExclusive, this.#bytesPerRow);
    const fragment = this.#elements.root.ownerDocument.createDocumentFragment();
    for (const row of rows) {
      fragment.append(this.#renderRow(row));
    }
    this.#elements.rows.style.transform = `translateY(${interval.firstRow * ROW_HEIGHT}px)`;
    this.#elements.rows.replaceChildren(fragment);
  }

  #renderRow(row) {
    const document = this.#elements.root.ownerDocument;
    const rowElement = document.createElement('div');
    rowElement.className = 'hex-row';
    const offset = document.createElement('span');
    offset.className = 'hex-row-offset';
    offset.textContent = row.offsetText;
    const bytes = document.createElement('div');
    bytes.className = 'hex-byte-grid';
    const ascii = document.createElement('div');
    ascii.className = 'hex-ascii-grid';

    for (const cell of row.cells) {
      if (cell === null) {
        const bytePlaceholder = document.createElement('span');
        bytePlaceholder.className = 'hex-byte-placeholder';
        bytes.append(bytePlaceholder);
        ascii.append(document.createElement('span'));
        continue;
      }

      const selectedClass = cell.selected
        ? ` selected${this.#selectionSource === 'instruction' ? ' instruction-origin' : ''}`
        : '';
      const button = document.createElement('button');
      button.type = 'button';
      button.className = `hex-byte${selectedClass}${cell.cursor ? ' cursor' : ''}`;
      button.dataset.offset = formatOffset(cell.offset, 1);
      button.textContent = cell.hex.toUpperCase();
      button.dataset.tip = `${formatOffset(cell.offset)} · ${cell.value}`;
      button.addEventListener('click', () => this.#selectByte(cell.offset));
      bytes.append(button);

      const character = document.createElement('span');
      character.className = `hex-ascii${selectedClass}`;
      character.textContent = cell.ascii;
      ascii.append(character);
    }
    rowElement.append(offset, bytes, ascii);
    return rowElement;
  }

  #handleScroll() {
    this.#render();
    if (this.#suppressWindowShift || this.#shifting || this.#window === null || this.#window.bytes.length === 0) {
      return;
    }

    const viewport = this.#elements.viewport;
    const totalHeight = Math.ceil(this.#window.bytes.length / this.#bytesPerRow) * ROW_HEIGHT;
    let direction = null;
    if (viewport.scrollTop <= totalHeight / 4 && this.#window.offset > 0n) {
      direction = 'up';
    } else if (viewport.scrollTop + viewport.clientHeight >= totalHeight * 3 / 4 &&
               this.#window.offset + BigInt(this.#window.bytes.length) < this.#fileSize) {
      direction = 'down';
    }
    if (direction === null) {
      return;
    }

    const topRow = Math.floor(viewport.scrollTop / ROW_HEIGHT);
    const anchor = this.#window.offset + BigInt(topRow * this.#bytesPerRow);
    const nextStart = shiftWindowStart(
      this.#window.offset, direction, this.#fileSize, DEFAULT_WINDOW_SIZE, this.#bytesPerRow,
    );
    if (nextStart === this.#window.offset) {
      return;
    }
    this.#shifting = true;
    const shift = this.#fetchWindow(nextStart, anchor);
    const requestId = this.#windowRequestId;
    shift.finally(() => {
      if (requestId === this.#windowRequestId) {
        this.#shifting = false;
      }
    });
  }

  #invalidateWindowOperation() {
    this.#windowRequestId += 1;
    this.#windowRequestPending = false;
    this.#shifting = false;
  }

  #restoreAnchor(anchor) {
    this.#suppressWindowShift = true;
    try {
      this.#elements.viewport.scrollTop = scrollTopForAnchor(
        rowAnchor(anchor, this.#window.offset, this.#bytesPerRow),
        this.#window.offset,
        ROW_HEIGHT,
        this.#bytesPerRow,
      );
      this.#render();
    } finally {
      requestAnimationFrame(() => { this.#suppressWindowShift = false; });
    }
  }

  #scrollToOffset(offset) {
    if (this.#window === null) {
      return;
    }
    this.#suppressWindowShift = true;
    try {
      this.#elements.viewport.scrollTop = scrollTopForAnchor(
        rowAnchor(offset, this.#window.offset, this.#bytesPerRow),
        this.#window.offset,
        ROW_HEIGHT,
        this.#bytesPerRow,
      );
      this.#render();
    } finally {
      requestAnimationFrame(() => { this.#suppressWindowShift = false; });
    }
  }

  async #selectByte(offset) {
    this.#selection = { offset, size: 1n };
    this.#selectionSource = 'hex';
    this.#cursorOffset = offset;
    this.#elements.offset.value = formatOffset(offset);
    this.#updateSummary();
    this.#clearNotice();
    this.#onClearError();
    this.#render();
    const instruction = findInstructionAtOffset(this.#instructions, offset);
    this.#onStatus(`Selected ABC byte ${formatOffset(offset)}`);
    const requestId = ++this.#locationRequestId;
    try {
      const response = await this.#client.get(
        `/api/v1/location?offset=${encodeURIComponent(formatOffset(offset, 1))}`,
        'hex-location',
      );
      if (requestId !== this.#locationRequestId) {
        return;
      }
      this.#onLocation({
        offset,
        instruction,
        instructionEntityId: instruction === null ? null : this.#instructionEntityId,
        structureMatch: response.structureMatch ?? null,
      });
    } catch (error) {
      if (requestId === this.#locationRequestId && !isStale(error)) {
        this.#onError(error);
      }
    }
  }

  #invalidateLocationOperation() {
    this.#locationRequestId += 1;
  }

  async #jumpToInput() {
    try {
      const offset = parseOffsetText(this.#elements.offset.value, this.#fileSize);
      await this.revealRange({ offset, size: 1n }, 'jump');
    } catch (error) {
      this.#showNotice(`Invalid offset: ${error instanceof Error ? error.message : String(error)}`, true);
    }
  }

  #updateSummary() {
    if (this.#selection === null) {
      this.#elements.summary.textContent = 'No byte range selected';
      return;
    }
    const prefix = this.#selectionSource === 'instruction' ? 'Instruction range' : 'Selected range';
    this.#elements.summary.textContent =
      `${prefix} ${formatOffset(this.#selection.offset)} +${this.#selection.size.toString()}`;
  }

  #showNotice(text, sticky = false) {
    this.#noticeSticky = sticky;
    this.#elements.notice.textContent = text;
    this.#elements.notice.classList.remove('hidden');
  }

  #clearNotice() {
    this.#noticeSticky = false;
    this.#elements.notice.classList.add('hidden');
  }
}
