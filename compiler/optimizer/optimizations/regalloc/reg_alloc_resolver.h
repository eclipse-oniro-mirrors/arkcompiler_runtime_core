/**
 * Copyright (c) 2021-2026 Huawei Device Co., Ltd.
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

#ifndef COMPILER_OPTIMIZER_OPTIMIZATIONS_REGALLOC_REG_ALLOC_RESOLVER_H
#define COMPILER_OPTIMIZER_OPTIMIZATIONS_REGALLOC_REG_ALLOC_RESOLVER_H

#include "compiler/optimizer/analysis/liveness_analyzer.h"
#include "reg_alloc_base.h"
#include "compiler/optimizer/ir/graph.h"

namespace panda::compiler {

class RegAllocResolver {
public:
    explicit RegAllocResolver(Graph *graph)
        : graph_(graph),
          liveness_(&graph->GetAnalysis<LivenessAnalyzer>()),
          catch_phi_save_candidates_(graph->GetLocalAllocator()->Adapter())
    {
    }
    NO_MOVE_SEMANTIC(RegAllocResolver);
    NO_COPY_SEMANTIC(RegAllocResolver);
    ~RegAllocResolver() = default;

    void Resolve();
    bool ResolveCatchPhis();
    void EliminateRedundantCatchPhiSaves();

private:
    friend class RegAllocResolverTest;

    struct CatchPhiSaveCandidate {
        SpillFillInst *spill_fill;
        size_t spill_fill_index;
        Register src;
        Register slot;
    };

    static void EliminateRedundantCatchPhiSaves(Graph *graph, const ArenaVector<CatchPhiSaveCandidate> &candidates);

    /*
     * Methods to set instructions source/dest registers
     */
    void ResolveInputs(Inst *inst);
    void ResolveOutput(Inst *inst);
    // Save CatchPhi inputs before their corresponding throwable instruction.
    void AddCatchPhiMoves(Inst *inst);

    Inst *SqueezeCatchPhiInputs(CatchPhiInst *catch_phi);

    bool CanStoreToAccumulator(const Inst *inst) const
    {
        return graph_->IsBytecodeOptimizer() && inst->GetDstReg() == ACC_REG_ID;
    }
    bool CanReadFromAccumulator(const Inst *inst, size_t input_number) const
    {
        return graph_->IsBytecodeOptimizer() && inst->GetSrcReg(input_number) == ACC_REG_ID;
    }

    void PropagateCallerMasks(SaveStateInst *save_state);

    void FillSaveStateRootsMask(SaveStateInst *save_state, Inst *user, SaveStateInst *target_ss);

    void AddMoveToFixedLocation(Inst *inst, Location input_location, size_t input_num);

    static inline void AddLocationToRoots(Location location, SaveStateInst *save_state, const Graph *graph)
    {
        if (location.IsFixedRegister()) {
            save_state->GetRootsRegsMask().set(location.GetValue());
        } else if (location.IsStack()) {
            save_state->GetRootsStackMask()->SetBit(location.GetValue());
        } else {
            ASSERT(location.IsStackParameter());
            auto slot_offset = graph->GetStackSlotsCount();
            save_state->GetRootsStackMask()->SetBit(location.GetValue() + slot_offset);
        }
    }

    Graph *GetGraph() const
    {
        return graph_;
    }

private:
    Graph *graph_;
    LivenessAnalyzer *liveness_;
    ArenaVector<CatchPhiSaveCandidate> catch_phi_save_candidates_;
};

}  // namespace panda::compiler

#endif  // COMPILER_OPTIMIZER_OPTIMIZATIONS_REGALLOC_REG_ALLOC_RESOLVER_H
