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

// === local export (module variable) scenarios ===
// `export let` binds a module variable accessed via stmodulevar /
// ldlocalmodulevar. The frontend records the same module-level var_id for
// these throws/stores (HoleCheckTarget::MODULE_LOCAL), so the optimizer
// treats them identically to lexical bindings — Rule T and entry seeds both
// apply. NB: these functions are function declarations (hoisted), so their
// definefunc precedes the `stmodulevar localExport` declaration in
// func_main_0 — the entry seed from func_main_0 is empty and the write-side
// throw (which guards the first write) is kept.

// Rule T, same block: a prior throw for the same module variable proves it
// non-hole -> the second throw is eliminated.
function canLocalMultiRead() {
  let a = localExport;
  let b = localExport;
  return a + b;
}

// Rule T, write-then-read: the write-side throw (before the reassignment
// store) is kept; the read-side throw (after the store) is eliminated.
function canLocalStoreThenRead() {
  localExport = 1;
  return localExport;
}

// Cross-block: the reassignment store in the entry block dominates the read
// in the if-body -> read-side throw eliminated.
function canLocalCrossBlock(flag) {
  localExport = 1;
  if (flag) {
    return localExport;
  }
  return 0;
}

// Write-only: the throw guards the write target (emitted before the store)
// and there is no prior proof -> kept.
function cantLocalWriteOnly() {
  localExport = 1;
}

// No intra-function store and the function declaration is hoisted ahead of
// the `stmodulevar localExport` declaration -> entry seed empty -> kept.
function cantLocalReadOnly() {
  return localExport;
}

// Entry-seed for a module variable: the reassignment store
// (`stmodulevar localExport`) runs before the nested expression's definefunc,
// so the snapshot at definefunc seeds `inner` with localExport non-hole ->
// the nested read-side throw is eliminated.
function canLocalEntrySeed() {
  localExport = 1; // stmodulevar before definefunc -> seed
  let inner = function () {
    return localExport;
  };
  return inner();
}

// Module variable declaration (local export). Placed after the functions so
// its stmodulevar declaration store runs after the hoisted definefunc
// instructions — mirroring how function declarations precede let/const
// initialization in the func_main_0 emission order.
export let localExport = 1;

// Rule S (same-function declaration store then read): the stmodulevar
// declaration store of localExport runs BEFORE this read in func_main_0, so
// the transfer function gen's localExport at the store — the read-side throw
// is eliminated. The entry-seed mechanism does NOT cover this (seeds only
// propagate to nested functions); Rule S in the intra-function dataflow does.
// `export` prevents the frontend from optimizing away the unused binding.
export let consumer = localExport;

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
