/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * holeCheckElim.js — HoleCheckElimination (Opt 1) scenario suite.
 *
 * Organized by the JS syntactic construct that gives rise to a
 * `throw.undefinedifholewithname` (a TDZ / hole check). Each top-level
 * function below is a self-contained case; the companion test asserts the
 * throw count before/after the pass per the Opt 1 soundness contract:
 *
 *   eliminate  <=>  same basic block, a dominating `stlexvar`/`stmodulevar`
 *                   of the same slot stores a compile-time Constant, no
 *                   CanThrow inst intervenes, and the throw is not inside a
 *                   try region that has a live catch handler.
 *
 * Group A — baseline (frontend keeps the var in a vreg; no throw emitted).
 * Group B — positive: top-level `let`, intra-function store dominates.
 * Group C — write-target throw (emitted before the store; never redundant).
 * Group D — cross-function capture (no intra-function store => Opt 2 territory).
 * Group E — soundness: store does not dominate (conditional init).
 * Group F — soundness: stored value may be hole (assignment from another TDZ var).
 * Group G — try/catch region (Opt 1 stays conservative).
 */
let captured; // slot 0 — top-level let, the common capture target
let other; // slot 1 — top-level let, stays hole (re-hole source)
const K = 1; // slot 2 — top-level const
class C {
  static m() {
    return 1;
  }
} // slot 3

// === Group A: baseline (expect 0 throws) ===
// NOTE: a `for (let i...)` loop body is intentionally NOT included here: the
// debug test harness runs the graph checker after IrBuilder, whose
// CheckLoopHasSafePoint asserts on loops in the bytecode-optimizer IR. This is a
// harness limitation, not a pass limitation (hole-check semantics are
// loop-independent), so the loop scenario is exercised via the pandasm-level
// unit tests instead.
function localLet() {
  let x = 1;
  return x;
}
function localConst() {
  const y = 2;
  return y;
}

// === Group B: positive — top-level let, intra-function store dominates (2->1, 3->1) ===
function writeThenRead() {
  captured = 1;
  return captured;
}
function multiRead() {
  captured = 1;
  let a = captured;
  let b = captured;
  return a + b;
}
// Rule T: two reads of the same top-level let with NO store in between. The
// first throw proves the slot non-hole (it ran without throwing), so the
// second throw on the dominated path is redundant. 2->1.
function multiReadNoStore() {
  let a = captured;
  let b = captured;
  return a + b;
}

// === Group C: write-target throw only (1->1, keep) ===
function writeOnly() {
  captured = 1;
}

// === Group D: cross-function capture (1->1, Opt 2 territory) ===
function readOnlyCapture() {
  return captured;
}
function constAccess() {
  return K;
}
function classAccess() {
  return C;
}
const arrowCapture = () => captured;

// === Group E: soundness — store does not dominate (2->2, keep) ===
function condInit(flag) {
  if (flag) {
    captured = 1;
  }
  return captured;
}

// === Group F: soundness — stored value may be hole (3->3, keep) ===
function reHole() {
  captured = other;
  return captured;
}

// === Group G: try/catch region (2->2, Opt 1 conservative) ===
function tryCatch() {
  try {
    captured = 1;
    return captured;
  } catch (e) {
    return 0;
  }
}
