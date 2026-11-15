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

const MAX_U64 = 0xffff_ffff_ffff_ffffn;

function requireEntityId(entityId) {
  if (typeof entityId !== 'string' || !/^[1-9][0-9]*$/.test(entityId)) {
    throw new TypeError('workbench entity id must be a non-zero decimal string');
  }
}

function requireMethodOffset(methodOffset, label) {
  if (typeof methodOffset !== 'bigint' || methodOffset < 0n || methodOffset > MAX_U64) {
    throw new TypeError(`${label} method offset must be an unsigned 64-bit BigInt`);
  }
  return methodOffset;
}

function responseMethodOffset(response, label) {
  const text = response?.function?.methodOffset;
  if (typeof text !== 'string' || !/^0x(?:0|[1-9a-f][0-9a-f]*)$/i.test(text)) {
    throw new Error(`${label} contains an invalid method offset`);
  }
  const methodOffset = BigInt(text);
  if (methodOffset > MAX_U64) {
    throw new Error(`${label} method offset exceeds 64 bits`);
  }
  return methodOffset;
}

export async function loadExecutableWorkbenchViews({
  entityId,
  requestedMethodOffset = null,
  requestDisassembly,
  requestCfg,
  isCurrent,
  onBeginLoading,
  onDisassembly,
  onCfg,
}) {
  requireEntityId(entityId);
  if (requestedMethodOffset !== null) {
    requireMethodOffset(requestedMethodOffset, 'requested');
  }
  for (const [name, callback] of Object.entries({
    requestDisassembly, requestCfg, isCurrent, onBeginLoading, onDisassembly, onCfg,
  })) {
    if (typeof callback !== 'function') {
      throw new TypeError(`${name} callback is required`);
    }
  }

  onBeginLoading();
  const disassembly = await requestDisassembly(entityId, requestedMethodOffset);
  if (!isCurrent()) {
    return { stale: true, methodOffset: null };
  }
  const methodOffset = responseMethodOffset(disassembly, 'Disassembly response');
  if (requestedMethodOffset !== null && requestedMethodOffset !== methodOffset) {
    throw new Error('Disassembly response method offset does not match the requested method');
  }
  onDisassembly(disassembly, methodOffset);

  const cfg = await requestCfg(entityId, methodOffset);
  if (!isCurrent()) {
    return { stale: true, methodOffset };
  }
  if (responseMethodOffset(cfg, 'CFG response') !== methodOffset) {
    throw new Error('CFG response method offset does not match Disassembly');
  }
  onCfg(cfg, methodOffset);
  return { stale: false, methodOffset };
}
