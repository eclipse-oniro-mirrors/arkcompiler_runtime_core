/**
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

#ifndef BYTECODE_OPTIMIZER_HOLE_CHECK_ELIMINATION_H
#define BYTECODE_OPTIMIZER_HOLE_CHECK_ELIMINATION_H

#include "compiler/optimizer/pass.h"
#include "ir_interface.h"
#include "libpandabase/macros.h"

#include <string>
#include <unordered_map>
#include <unordered_set>

namespace panda::bytecodeopt {

// Opt 1: intra-function redundant `throw.undefinedifholewithname` elimination.
//
// A `throw.undefinedifholewithname` reads acc (the value loaded by a preceding
// `ldlexvar`/`ldlocalmodulevar`) and throws a ReferenceError when that value
// is the hole sentinel. The throw is redundant when the loaded variable is
// provably non-hole at the load point.
//
// Per ECMA, a lexical binding is hole only until its declaration executes;
// afterwards it never becomes hole again. In Ark bytecode the hole sentinel
// is never stored into a lexical slot by the frontend (it exists only as
// newlexenv's pre-init state), so a catch handler cannot re-hole a variable.
// The transfer function is a monotone must-availability analysis:
//   * a hole-check throw of V -> gen V (Rule T: if it executed without
//     throwing, V was non-hole; if it threw, control left the region);
//   * stores are NOT gen'd — every reassignment store is preceded by a
//     write-side throw (Rule T covers it), and declaration stores are in
//     another function (the entry seed covers it);
//   * a caught CanThrow -> no effect (the catch cannot re-hole any variable).
// A throw (incl. one inside a try region / async / generator implicit
// try-catch) is redundant iff its varId is non-hole at the load point on
// every path; eliminating it never changes observable behaviour (the throw
// would not have fired, so the catch would not have run due to this throw).
//
// The variable identity of each throw is obtained from the frontend compiler:
// the frontend records, per method, a table (pandasm::Function::holeCheckVarTable)
// mapping each throw to its HoleCheckTarget (kind + varId). The pass reads
// that table through the BytecodeOptIrInterface (pcInsMap ->
// pandasm::Ins -> table) and falls back to ldexternalmodulevar index-based
// identity when the table is absent for external imports.
class HoleCheckElimination : public compiler::Optimization {
public:
    HoleCheckElimination(compiler::Graph *graph, BytecodeOptIrInterface *iface) : Optimization(graph), iface_(iface) {}

    NO_MOVE_SEMANTIC(HoleCheckElimination);
    NO_COPY_SEMANTIC(HoleCheckElimination);
    ~HoleCheckElimination() override = default;

    bool RunImpl() override;

    const char *GetPassName() const override
    {
        return "HoleCheckElimination";
    }

private:
    BytecodeOptIrInterface *iface_ {nullptr};

    void InvalidateAnalyses() override;
};

// Opt 2 seed cache: per-function set of varIds non-hole at the function's
// entry. Computed by ComputeEntryNonHoleSeeds as a pre-pass before any function
// is optimized (so func_main_0's Ins* are still valid). The result is owned by
// the caller (OptimizePandaFile) as a per-program local and published to the
// per-function pass via BytecodeOptIrInterface::SetEntryNonHoleSeeds, so there
// is no global state shared across concurrently-optimized files.
void ComputeEntryNonHoleSeeds(pandasm::Program *prog, BytecodeOptIrInterface *iface,
                              std::unordered_map<std::string, std::unordered_set<uint32_t>> *outSeeds);

}  // namespace panda::bytecodeopt

#endif  // BYTECODE_OPTIMIZER_HOLE_CHECK_ELIMINATION_H
