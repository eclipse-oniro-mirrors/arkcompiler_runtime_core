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

const SHOW_DELAY_MS = 350;
const VIEWPORT_MARGIN = 8;
const ANCHOR_GAP = 8;
const TOOLTIP_ID = 'app-tooltip';

/**
 * Positions a tooltip below the anchor (centered on it), flips above the anchor
 * when the viewport bottom is too close, and keeps the tooltip inside the
 * viewport horizontally.
 */
export function computeTooltipPosition(anchor, tooltipWidth, tooltipHeight, viewportWidth, viewportHeight) {
  const maxLeft = Math.max(viewportWidth - tooltipWidth - VIEWPORT_MARGIN, VIEWPORT_MARGIN);
  const left = Math.min(Math.max(anchor.left + anchor.width / 2 - tooltipWidth / 2, VIEWPORT_MARGIN), maxLeft);
  let top = anchor.bottom + ANCHOR_GAP;
  if (top + tooltipHeight > viewportHeight - VIEWPORT_MARGIN) {
    const above = anchor.top - ANCHOR_GAP - tooltipHeight;
    if (above >= VIEWPORT_MARGIN) {
      top = above;
    }
  }
  return { left, top: Math.max(top, VIEWPORT_MARGIN) };
}

// Creates the single shared tooltip element that gets appended to <body>.
function createTooltipElement(doc) {
  const tooltip = doc.createElement('div');
  tooltip.className = 'tooltip';
  tooltip.id = TOOLTIP_ID;
  tooltip.setAttribute('role', 'tooltip');
  return tooltip;
}

// Resolves the closest element (self included) carrying a tooltip attribute.
function tooltipTarget(node) {
  return node && typeof node.closest === 'function' ? node.closest('[data-tip]') : null;
}

// Positions the tooltip below the anchor, flipping above it when needed.
function positionTooltip(target, tooltip, win) {
  const rect = target.getBoundingClientRect();
  const position = computeTooltipPosition(
    rect, tooltip.offsetWidth, tooltip.offsetHeight, win.innerWidth, win.innerHeight);
  tooltip.style.left = `${position.left}px`;
  tooltip.style.top = `${position.top}px`;
}

// Cancels a pending show and detaches the tooltip from its described target.
function hideTooltip(state) {
  state.cancel(state.timer);
  state.timer = 0;
  state.hoverTarget = null;
  if (state.describedTarget) {
    state.describedTarget.removeAttribute('aria-describedby');
    state.describedTarget = null;
  }
  state.tooltip.classList.remove('visible');
}

// Reveals the tooltip for the target after positioning it in the viewport.
function showTooltip(target, state, win) {
  const text = target.getAttribute('data-tip');
  if (!text) {
    return;
  }
  state.tooltip.textContent = text;
  positionTooltip(target, state.tooltip, win);
  state.describedTarget = target;
  target.setAttribute('aria-describedby', TOOLTIP_ID);
  state.tooltip.classList.add('visible');
}

/**
 * Installs one shared hover tooltip for every element carrying a non-empty
 * `data-tip` attribute. The tooltip element is fixed-positioned on <body>, so
 * scroll containers (instruction table, panel viewports) can never clip it.
 * Returns a dispose function that removes listeners and the element.
 */
export function installTooltips(overrides = {}) {
  const doc = overrides.document ?? globalThis.document;
  const win = overrides.window ?? globalThis.window;
  const schedule = overrides.setTimeout ?? ((handler, delay) => globalThis.setTimeout(handler, delay));
  const cancel = overrides.clearTimeout ?? ((id) => globalThis.clearTimeout(id));

  const tooltip = createTooltipElement(doc);
  const state = { tooltip, cancel, timer: 0, hoverTarget: null, describedTarget: null };

  const hide = () => hideTooltip(state);
  const show = (target) => showTooltip(target, state, win);

  const onPointerOver = (event) => {
    const target = tooltipTarget(event.target);
    if (!target || target === state.hoverTarget) {
      return;
    }
    hide();
    state.hoverTarget = target;
    state.timer = schedule(() => show(target), SHOW_DELAY_MS);
  };

  const onPointerOut = (event) => {
    const target = tooltipTarget(event.target);
    if (!target || target !== state.hoverTarget) {
      return;
    }
    if (event.relatedTarget && target.contains(event.relatedTarget)) {
      return;
    }
    hide();
  };

  const onScroll = () => {
    hide();
  };

  const onPointerDown = () => {
    hide();
  };

  doc.addEventListener('pointerover', onPointerOver);
  doc.addEventListener('pointerout', onPointerOut);
  doc.addEventListener('pointerdown', onPointerDown);
  win.addEventListener('scroll', onScroll, true);
  doc.body.appendChild(tooltip);

  return () => {
    hide();
    doc.removeEventListener('pointerover', onPointerOver);
    doc.removeEventListener('pointerout', onPointerOut);
    doc.removeEventListener('pointerdown', onPointerDown);
    win.removeEventListener('scroll', onScroll, true);
    tooltip.remove();
  };
}
