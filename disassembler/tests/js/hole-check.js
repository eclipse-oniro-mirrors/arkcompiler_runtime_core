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
 * Hole-check elimination scenarios (intra-function).
 *
 * The disassembler golden .pa for this file is the OPTIMIZED output (es2abc
 * default opt-level 2 runs the bytecode optimizer, including
 * HoleCheckElimination). "can be optimized" functions lose their redundant
 * throw.undefinedifholewithname; "cannot be optimized" functions keep them.
 */
let captured;
let other;

// === can be optimized (redundant throw removed) ===

// Rule S, same block: store of a Constant dominates the post-store read.
function canStoreThenRead() {
  captured = 1;
  return captured;
}

// Rule T, same block: a prior throw for the same variable proves it non-hole.
function canMultiReadNoStore() {
  let a = captured;
  let b = captured;
  return a + b;
}

// Cross-block: the store in the entry block dominates the read in the if-body.
function canCrossBlockStore(flag) {
  captured = 1;
  if (flag) {
    return captured;
  }
  return 0;
}

// === cannot be optimized (throws kept) ===

// The throw guards the write target (emitted before the store).
function cantWriteOnly() {
  captured = 1;
}

// No intra-function store (cross-function capture -> Opt 2 territory).
function cantReadOnlyCapture() {
  return captured;
}

// Conditional store does not dominate the post-merge read.
function cantCondInit(flag) {
  if (flag) {
    captured = 1;
  }
  return captured;
}

// Any store proves non-hole (per ECMA: if the stored value were hole, the
// frontend's throw check on the source would have fired before the store).
// The write-side throw (before the store) is kept (no prior store), but the
// read-side throw (after the store) is eliminated.
function canStoreNonConst() {
  captured = other;
  return captured;
}

// Precise try/catch: inside a try region. The write-side throw (before the
// store) is kept (no prior store, captured may be hole), but the read-side
// throw (after `captured = 1`) is eliminated — per ECMA a catch cannot
// re-hole a variable, so once the Constant store proves captured non-hole,
// the throw (even though throwable) never fires.
function cantTryCatch() {
  try {
    captured = 1;
    return captured;
  } catch (e) {
    return 0;
  }
}

// === entry-seed (cross-function, Opt 1 entry seeds) ===
// A nested function's read-side throw is eliminated when the enclosing
// function stores the captured variable BEFORE the nested function's
// definefunc: the store seeds the nested entry as non-hole (a stlexvar only
// runs after a non-hole RHS, so the var is non-hole afterwards — sound per
// ECMA). NB: the nested function is an EXPRESSION here so its definefunc is
// emitted after the store; a function DECLARATION would be hoisted ahead of
// the store and would not receive the seed.

// Constant store before the expression's definefunc -> nested throw eliminated.
function canEntrySeed() {
  let x = 1; // stlexvar x before definefunc -> seed
  let inner = function () {
    return x;
  };
  return inner();
}

// Non-constant store before the definefunc also seeds (any store proves
// non-hole) -> nested throw eliminated.
function canEntrySeedNonConst() {
  let x = other; // stlexvar x before definefunc -> seed
  let inner = function () {
    return x;
  };
  return inner();
}

// Store AFTER the nested definefunc -> the snapshot at definefunc has no
// proof for the var -> nested throw kept (the seed only sees stores that
// precede the definefunc).
function cantEntrySeedStoreAfter() {
  let inner = function () {
    return x;
  }; // definefunc before stlexvar x
  let x = 1; // store after definefunc -> no seed
  return inner();
}
