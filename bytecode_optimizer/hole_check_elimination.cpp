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

#include "hole_check_elimination.h"

#include "hole_check_target.h"
#include "compiler/optimizer/analysis/loop_analyzer.h"
#include "compiler/optimizer/ir/basicblock.h"
#include "compiler/optimizer/ir/graph.h"
#include "compiler/optimizer/ir/inst.h"
#include "compiler/optimizer/ir/runtime_interface.h"
#include "utils/logger.h"

#include <unordered_map>
#include <vector>
#include <unordered_set>

namespace panda::bytecodeopt {

using compiler::BasicBlock;
using compiler::Graph;
using compiler::Inst;
using compiler::RuntimeInterface;
using pandasm::HoleCheckTarget;

namespace {

constexpr int STORED_VALUE_INPUT_INDEX = 0;

// The seed fixpoint needs at most one iteration per function-nesting level.
// ES function nesting rarely exceeds 3; 8 gives a generous safety margin.
constexpr int MAX_SEED_ITERATIONS = 8;

bool IsHoleCheckThrow(const Inst *inst)
{
    return inst->IsIntrinsic() && inst->CastToIntrinsic()->GetIntrinsicId() ==
                                      RuntimeInterface::IntrinsicId::THROW_UNDEFINEDIFHOLEWITHNAME_PREF_ID16;
}

// Per-method context: the frontend-recorded table (throw/store pandasm::Ins
// -> HoleCheckTarget) and the pc->pandasm::Ins map. Absent when the graph was
// built without the pandasm Program (no frontend record -> no elimination).
struct HoleCheckCtx {
    const std::unordered_map<const pandasm::Ins *, HoleCheckTarget> *table {nullptr};
    const std::unordered_map<size_t, pandasm::Ins *> *pc_map {nullptr};
};

// Look up an instruction's variable identity from the frontend-recorded
// table (inst PC -> pc_map -> pandasm::Ins -> table) — the absolute varId
// for lexical / local-module bindings. External imports (ldexternalmodulevar)
// are not recorded here: their cross-module check is done at ins level in
// AsmEmitter::EmitPrograms (EliminateCrossModuleHoleChecks).
HoleCheckTarget LookupTarget(const Inst *inst, HoleCheckCtx ctx)
{
    if (ctx.table != nullptr && ctx.pc_map != nullptr) {
        auto pit = ctx.pc_map->find(inst->GetPc());
        if (pit != ctx.pc_map->end() && pit->second != nullptr) {
            auto tit = ctx.table->find(pit->second);
            if (tit != ctx.table->end() && tit->second.IsValid()) {
                return tit->second;
            }
        }
    }
    return HoleCheckTarget::Invalid();
}

// BFS the catch subgraph; return true if any hole-check throw for the same
// variable is reachable from `inst`'s catch handlers.
bool CatchReachableRethrow(const Graph *graph, const Inst *inst, const HoleCheckTarget &target, HoleCheckCtx ctx)
{
    if (!graph->IsInstThrowable(inst)) {
        return false;
    }
    std::unordered_set<const BasicBlock *> visited;
    std::vector<const BasicBlock *> queue;
    for (const auto *catchBb : graph->GetThrowableInstHandlers(inst)) {
        if (catchBb != nullptr) {
            queue.push_back(catchBb);
        }
    }
    while (!queue.empty()) {
        const auto *bb = queue.back();
        queue.pop_back();
        if (bb == nullptr || !visited.insert(bb).second) {
            continue;
        }
        for (const auto *i : bb->Insts()) {
            if (!IsHoleCheckThrow(i)) {
                continue;
            }
            auto t = LookupTarget(i, ctx);
            if (t.IsValid() && t.IsSameObject(target)) {
                return true;
            }
        }
        for (const auto *succ : bb->GetSuccsBlocks()) {
            if (succ != nullptr) {
                queue.push_back(succ);
            }
        }
    }
    return false;
}

// Transfer function: a hole-check throw of V -> gen V (Rule T: if it executed
// without throwing, V was non-hole). For a caught throw, only gen when the
// catch subgraph does not re-check V (otherwise a reachable throw for V could
// be unsoundly eliminated). Stores are NOT gen'd — every reassignment store
// is preceded by a write-side throw (Rule T covers it), and declaration
// stores are in another function (the entry seed covers it).
void ApplyTransfer(const Inst *inst, std::unordered_set<uint32_t> &nonHole, const Graph *graph, HoleCheckCtx ctx)
{
    if (!IsHoleCheckThrow(inst)) {
        return;
    }
    auto t = LookupTarget(inst, ctx);
    if (!t.IsValid()) {
        return;
    }
    if (!graph->IsInstThrowable(inst) || !CatchReachableRethrow(graph, inst, t, ctx)) {
        nonHole.insert(t.GetVarId());
    }
}

// Intersect predecessor OUT sets to compute this block's IN set (forward
// must-analysis: IN = intersection of all predecessor OUT sets).
std::unordered_set<uint32_t> IntersectPredecessorOuts(
    const BasicBlock *bb, const std::unordered_map<const BasicBlock *, std::unordered_set<uint32_t>> &outSets)
{
    const auto &preds = bb->GetPredsBlocks();
    if (preds.empty()) {
        return {};
    }
    auto it = outSets.find(preds[0U]);
    std::unordered_set<uint32_t> inSet = (it != outSets.end()) ? it->second : std::unordered_set<uint32_t> {};
    for (size_t i = 1U; i < preds.size(); ++i) {
        it = outSets.find(preds[i]);
        if (it == outSets.end()) {
            return {};
        }
        const auto &predOut = it->second;
        for (auto sit = inSet.begin(); sit != inSet.end();) {
            if (predOut.count(*sit) != 0U) {
                ++sit;
            } else {
                sit = inSet.erase(sit);
            }
        }
    }
    return inSet;
}

// Forward must-availability dataflow fixpoint. "V non-hole" is available at a
// point iff on every path from function entry a witness (a throw of V that
// already executed) occurs and is not killed. The entry block's IN is the
// seed (external precondition), not recomputed from preds.
std::unordered_map<const BasicBlock *, std::unordered_set<uint32_t>> ComputeAvailability(
    const Graph *graph, HoleCheckCtx ctx, const std::unordered_set<uint32_t> &entrySeed)
{
    auto *entryBlock = graph->GetStartBlock();
    std::unordered_map<const BasicBlock *, std::unordered_set<uint32_t>> inSets;
    std::unordered_map<const BasicBlock *, std::unordered_set<uint32_t>> outSets;
    bool changed = true;
    while (changed) {
        changed = false;
        for (auto *bb : graph->GetBlocksRPO()) {
            if (bb == nullptr) {
                continue;
            }
            std::unordered_set<uint32_t> inSet;
            if (bb == entryBlock && !entrySeed.empty()) {
                inSet = entrySeed;
            } else {
                inSet = IntersectPredecessorOuts(bb, outSets);
            }
            auto cur = inSet;
            for (auto *inst : bb->Insts()) {
                ApplyTransfer(inst, cur, graph, ctx);
            }
            if (cur != outSets[bb]) {
                outSets[bb] = std::move(cur);
                changed = true;
            }
            inSets[bb] = std::move(inSet);
        }
    }
    return inSets;
}

// Find throws whose variable is proven non-hole at the load that feeds them.
// A throw is redundant iff its variable is non-hole at the load (evaluated by
// replaying the block from IN up to the load).
ArenaVector<Inst *> CollectRedundantThrows(
    const Graph *graph, HoleCheckCtx ctx,
    const std::unordered_map<const BasicBlock *, std::unordered_set<uint32_t>> &inSets)
{
    auto isNonHoleAt = [&graph, &ctx, &inSets](const Inst *load, uint32_t varId) -> bool {
        auto *bb = load->GetBasicBlock();
        if (bb == nullptr) {
            return false;
        }
        auto it = inSets.find(bb);
        auto nonHole = (it != inSets.end()) ? it->second : std::unordered_set<uint32_t> {};
        for (auto *inst : bb->Insts()) {
            if (inst == load) {
                return nonHole.count(varId) != 0U;
            }
            ApplyTransfer(inst, nonHole, graph, ctx);
        }
        return false;
    };

    ArenaVector<Inst *> toRemove(graph->GetLocalAllocator()->Adapter());
    for (auto *bb : graph->GetBlocksRPO()) {
        if (bb == nullptr) {
            continue;
        }
        for (auto *inst : bb->Insts()) {
            if (!IsHoleCheckThrow(inst)) {
                continue;
            }
            auto target = LookupTarget(inst, ctx);
            if (!target.IsValid()) {
                continue;
            }
            auto *load = inst->GetInput(STORED_VALUE_INPUT_INDEX).GetInst();
            if (load != nullptr && isNonHoleAt(load, target.GetVarId())) {
                toRemove.push_back(inst);
            }
        }
    }
    return toRemove;
}

// Seed methods defined in a class literalarray (defineclasswithbuffer).
void SeedClassMethodsFromLiteralArray(const pandasm::Program *prog, const std::string &litId,
                                      const std::unordered_set<uint32_t> &nonHole,
                                      std::unordered_map<std::string, std::unordered_set<uint32_t>> *seeds)
{
    auto litIt = prog->literalarray_table.find(litId);
    if (litIt == prog->literalarray_table.end()) {
        return;
    }
    for (const auto &lit : litIt->second.literals_) {
        if (lit.tag_ != panda_file::LiteralTag::METHOD && lit.tag_ != panda_file::LiteralTag::GETTER &&
            lit.tag_ != panda_file::LiteralTag::SETTER && lit.tag_ != panda_file::LiteralTag::GENERATORMETHOD &&
            lit.tag_ != panda_file::LiteralTag::ASYNCGENERATORMETHOD) {
            continue;
        }
        const auto *methodName = std::get_if<std::string>(&lit.value_);
        if (methodName != nullptr) {
            (*seeds)[*methodName] = nonHole;
        }
    }
}

// Scan one function's instructions: accumulate non-hole varIds from
// stlexvar/stmodulevar, and snapshot the current set at each
// definefunc/defineclasswithbuffer for the defined function.
void ScanFunction(const pandasm::Program *prog, const std::string &funcName, const pandasm::Function &func,
                  std::unordered_map<std::string, std::unordered_set<uint32_t>> *seeds)
{
    std::unordered_set<uint32_t> nonHole;
    auto seedIt = seeds->find(funcName);
    if (seedIt != seeds->end()) {
        nonHole = seedIt->second;
    }
    const auto &storeTable = func.storeVarTable;
    for (const auto &insp : func.ins) {
        const auto *ins = insp.get();
        auto mn = ins->OpcodeToString();
        if (mn == "definefunc" || mn == "definemethod") {
            const auto &ids = ins->Ids();
            if (!ids.empty()) {
                (*seeds)[ids[0U]] = nonHole;
            }
        } else if (mn == "defineclasswithbuffer") {
            const auto &ids = ins->Ids();
            if (!ids.empty()) {
                (*seeds)[ids[0U]] = nonHole;
            }
            if (ids.size() > 1U) {
                SeedClassMethodsFromLiteralArray(prog, ids[1U], nonHole, seeds);
            }
        } else {
            auto tit = storeTable.find(ins);
            if (tit != storeTable.end() && tit->second.IsValid()) {
                nonHole.insert(tit->second.GetVarId());
            }
        }
    }
}

}  // namespace

bool HoleCheckElimination::RunImpl()
{
    auto *graph = GetGraph();

    HoleCheckCtx ctx;
    std::string funcName;
    if (iface_ != nullptr && iface_->IsMapsSet() && iface_->GetProgram() != nullptr) {
        ctx.pc_map = iface_->GetPcInsMap();
        auto offset = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(graph->GetMethod()));
        funcName = iface_->GetMethodIdByOffset(offset);
        auto *prog = iface_->GetProgram();
        auto fit = prog->function_table.find(funcName);
        if (fit != prog->function_table.end()) {
            ctx.table = &fit->second.holeCheckVarTable;
        }
    }

    std::unordered_set<uint32_t> entrySeed;
    if (ctx.table != nullptr && !funcName.empty() && iface_ != nullptr) {
        entrySeed = iface_->GetEntryNonHoleSeeds(funcName);
    }

    auto inSets = ComputeAvailability(graph, ctx, entrySeed);
    auto toRemove = CollectRedundantThrows(graph, ctx, inSets);

    for (auto *inst : toRemove) {
        LOG(DEBUG, BYTECODE_OPTIMIZER) << "HoleCheckElimination removes throw.undefinedifholewithname";
        auto *load = inst->GetInput(STORED_VALUE_INPUT_INDEX).GetInst();
        inst->GetBasicBlock()->RemoveInst(inst);
        // After removing the throw, its feeding load (a pure ldlexvar /
        // ldlocalmodulevar / ldexternalmodulevar that existed solely to supply
        // the throw's accumulator input) is dead; remove it too.
        if (load != nullptr && !load->HasUsers() && !load->CanThrow()) {
            load->GetBasicBlock()->RemoveInst(load);
        }
    }

    return !toRemove.empty();
}

void HoleCheckElimination::InvalidateAnalyses()
{
    GetGraph()->InvalidateAnalysis<compiler::LoopAnalyzer>();
    InvalidateBlocksOrderAnalyzes(GetGraph());
}

// --- Opt 2 seed cache ---

void ComputeEntryNonHoleSeeds(pandasm::Program *prog, BytecodeOptIrInterface *iface,
                              std::unordered_map<std::string, std::unordered_set<uint32_t>> *outSeeds)
{
    if (prog == nullptr || iface == nullptr || outSeeds == nullptr) {
        return;
    }
    // Multi-pass fixpoint: scan ALL functions (not just func_main_0) to handle
    // nested functions. Each function starts with its own entry seed (inherited
    // from the enclosing function at its definefunc/defineclasswithbuffer), then
    // accumulates non-hole varIds from its own stlexvar/stmodulevar. At each
    // definefunc/defineclasswithbuffer, snapshot for the defined function.
    // Iterate until no new seeds are discovered. The result is written into
    // the caller-owned `outSeeds` (a per-program stack local in
    // OptimizePandaFile). No global/static cache: multiple files are optimized
    // concurrently on different threads, so any shared cache would race.
    auto &seeds = *outSeeds;
    seeds.clear();

    bool changed = true;
    int maxIterations = MAX_SEED_ITERATIONS;
    while (changed && maxIterations-- > 0) {
        changed = false;
        auto snapshot = seeds;
        for (auto &[name, func] : prog->function_table) {
            ScanFunction(prog, name, func, &seeds);
        }
        if (seeds != snapshot) {
            changed = true;
        }
    }
}

}  // namespace panda::bytecodeopt
