# Hole Check Elimination

Redundant `throw.undefinedifholewithname` (TDZ check) elimination, implemented
at two levels: intra-function (bytecode optimizer, IR dataflow) and
cross-module (assembler, pandasm ins-level dataflow in `EmitPrograms`).

## Background

`throw.undefinedifholewithname` checks whether the accumulator holds the hole
sentinel; if so it throws `ReferenceError`, otherwise it passes the value
through. The frontend emits it after any `ldlexvar` / `ldlocalmodulevar` /
`ldexternalmodulevar` whose initialization status it cannot prove
syntactically (the temporal dead zone).

Per ECMA, a lexical binding is hole only until its declaration executes;
afterwards it never becomes hole again. A catch handler cannot re-hole a
variable. This is the soundness basis for both levels.

## Level 1 — Intra-function (bytecode optimizer)

`HoleCheckElimination` is an `Optimization` pass in
`bytecode_optimizer/optimize_bytecode.cpp`, inside `RunOptimizations`:

```
Cleanup -> ConstantPropagation + BranchElimination -> HoleCheckElimination
  -> ValNum -> Lowering -> MoveConstants -> Cleanup -> RegAccAlloc -> RegAlloc -> RegEncoder
```

### Variable identity

The frontend assigns each binding a module-level `var_id` (shared across all
functions of the module via `CompilerContext::AssignVarId`). At emit time the
`ThrowUndefinedIfHole(node, var)` overload records the target
(`HoleCheckTarget::ForId(kind, var_id)`) into a per-`PandaGen` map; the emitter
propagates it into `pandasm::Function::holeCheckVarTable` (throw entries) and
`storeVarTable` (store entries), both non-serialized. At optimization time the
pass reads the table through `BytecodeOptIrInterface` (throw PC -> pc_map ->
`pandasm::Ins*` -> table).

### Entry seed (cross-function)

`ComputeEntryNonHoleSeeds` is a pre-pass that scans all functions' ins
sequences. It accumulates `var_id`s from `stlexvar` / `stmodulevar` and, at
each `definefunc` / `defineclasswithbuffer`, snapshots the current set as the
defined function's entry seed. A fixpoint (multi-pass) propagates seeds
through nested function definitions. The seed is published via
`BytecodeOptIrInterface::SetEntryNonHoleSeeds` — a per-program stack local,
never global.

### Forward must-availability dataflow

The pass computes IN/OUT sets of non-hole `var_id`s per basic block (IN =
intersection of predecessor OUTs; entry block IN = the seed). A throw is
redundant when its `var_id` is in the IN set at the load point (replayed block
from IN to the load).

### Rule T (prior throw)

A `throw.undefinedifholewithname` that executed without throwing proves its
slot non-hole. So a later throw for the same `var_id` on a dominated path is
redundant. For caught throws, `CatchReachableRethrow` guards soundness: it
BFSes the catch subgraph and only gens the slot when no throw for the same
variable is reachable from the catch handlers.

### Dead load removal

After removing a throw, its feeding pure load (`ldlexvar` /
`ldlocalmodulevar` with no other users and `!CanThrow`) is also removed.

## Level 2 — Cross-module (assembler, ins-level dataflow)

`EliminateCrossModuleHoleChecks` runs in `AsmEmitter::EmitPrograms` (merge-abc
path, release only — `!isDebug`). It operates on `pandasm::Program` ins
directly, no bytecode optimizer involved.

### Import graph

`ForEachModuleLiteral` enumerates each program's module records (two layouts:
merge-abc `moduleRecordIdx` field, or single-file `_ESModuleRecord` keyed by
source path). `ParseModuleLiteral` decodes the module-request and regular-import
sections from the literal array. `ModulePathToRecordName` maps paths to record
names. The result is an `ImportGraph`: record name -> (external slot -> {source
record, local name}).

### Must-definition dataflow on ins

A `CfgBuilder` splits `func.ins` into basic blocks (labels, jumps, returns as
boundaries; catch entries marked). `SolveDataflow` iterates the fixpoint: per
block, `FlowState` tracks the accumulator's origin (`ValueOrigin`: known
external slot, or unknown) and each register's origin, plus a `proven` set of
slots proven non-hole by a prior throw (Rule T at ins level). Join = field-wise
equality (must); catch entries start all-unknown, so gen inside try cannot
pollute handlers.

### Elimination

A throw is removable when its acc origin is a known external slot and either:
1. The import is acyclic (`IsImportAcyclic`: DFS the import graph; all records
   visible and the source cannot reach back to the importer; unknown records
   treated as cyclic — conservative).
2. The slot is in the `proven` set (Rule T: a prior throw of the same slot
   executed without firing).

Only the throw is removed — it passes the acc value through, which stays live.
Dead-load cleanup: if the throw's feeding `ldexternalmodulevar` has no spill
(`sta` in between) and the next surviving instruction overwrites acc without
reading it (or the function ends), the load is also removed.

### Single-module fallback

For programs with a single module record, unmangled function names (no
`record.` prefix) are attributed to that record via `OwnerRecord` with a
`fallback` pointer.

## Source layout

| File | Purpose |
|------|---------|
| `assembler/cross_module_hole_check.{h,cpp}` | Level 2: ins-level dataflow, import graph, cycle detection, dead-load cleanup |
| `assembler/hole_check_target.h` | `HoleCheckTarget`: uniform identity (kind + var_id), `IsSameObject` by var_id |
| `assembler/assembly-function.h` | `holeCheckVarTable` / `storeVarTable` (non-serialized) |
| `assembler/assembly-emitter.cpp` | `EmitPrograms` calls `EliminateCrossModuleHoleChecks` (release only) |
| `bytecode_optimizer/hole_check_elimination.{h,cpp}` | Level 1: IR dataflow pass, `ComputeEntryNonHoleSeeds` |
| `bytecode_optimizer/optimize_bytecode.cpp` | `RunOptimizations` wiring; `OptimizePandaFile` seed pre-pass |
| `bytecode_optimizer/ir_interface.h` | `EntryNonHoleSeeds` type, `SetEntryNonHoleSeeds` / `GetEntryNonHoleSeeds` |
| `ets_frontend/.../pandagen.{h,cpp}` | `ThrowUndefinedIfHole(node, var)` records target; `AssignVarId` delegates to `CompilerContext` |
| `ets_frontend/.../compilerContext.h` | `AssignVarId`: per-module var_id assignment (thread-safe) |
| `ets_frontend/.../regAllocator.h` | `EmitAndGet`: returns emitted IRNode for metadata attachment |
| `ets_frontend/.../emitter.cpp` | Propagates `holeCheckVars_` into `holeCheckVarTable` / `storeVarTable` |
| `ets_frontend/.../lexenv.cpp` | Call sites pass `var` to `ThrowUndefinedIfHole` |

## Testing

| Suite | What |
|-------|------|
| `BytecodeOptimizerTest` (84 tests) | Level 1 scenarios via `hole_check_elimination_test.cpp` + `holeCheckElim.js` |
| `DisasmHoleCheckTest` (1 test) | End-to-end: compile `hole-check.js` with opt-level 2, disassemble, compare golden `.pa` |
| `Abc2ProgScriptTest` (39 tests) | abc2program golden regression (release line-number expected files updated) |
| `test262 es2021` | 19749/19749 |
