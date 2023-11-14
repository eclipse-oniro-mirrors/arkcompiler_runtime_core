/**
 * Copyright (c) 2021-2022 Huawei Device Co., Ltd.
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

#ifndef COMPILER_OPTIMIZER_OPTIMIZATIONS_LSE_H_
#define COMPILER_OPTIMIZER_OPTIMIZATIONS_LSE_H_

#include "optimizer/ir/analysis.h"
#include "optimizer/ir/graph.h"
#include "optimizer/pass.h"
#include "compiler_options.h"

namespace panda::compiler {
/**
 * Load Store Elimination (Lse) optimization is aimed to eliminate redundant
 * loads and store.  It uses Alias Analysis to determine which memory
 * instructions are redundant.  Lse has the heap representation in form of
 *
 * "Memory Instruction" -> "Value stored at location pointed by instruction"
 *
 * Generally, this optimization tries to:
 * 1) delete stores that attempt to store the same values that already have
 * been been stored by a previous store
 * 2) delete loads that attempt to load values that were previously loaded or
 * stored (optimization keep track what was stored previously)
 *
 * Traversing basic blocks in RPO optimization does the following with
 * instructions:
 * - if the instruction is a store and a stored value is equal to value from
 * heap for this store then this store can be eliminated.
 * - if the instruction is a store and a value from heap for this store is
 * absent or differs from new stored value then the new stored value is written
 * into heap.  The values of memory instructions that MUST_ALIAS this store are
 * updated as well.  All values in the heap that MAY_ALIAS this store
 * instruction are invalidated.
 * - if the instruction is a load and there is a value from the heap for this
 * load then this load can be eliminated.
 * - if the instruction is a load and there is no value from the heap for this
 * load then we update heap value for this load with the result of this load.
 * All instructions that MUST_ALIAS this load updated as well.
 * - if the instruction is a volatile load then the whole heap is invalidated.
 * - if the instruction is a call then the whole heap is invalidated.
 *
 * Instructions that invalidate heap are enumerated in IsHeapInvalidatingInst.
 * Instructions that cannot be eliminated are presented in
 * CanEliminateInstruction.
 *
 * Another noticeable points:
 * - Heap is invalided at loop headers basic blocks because values can be
 * overwritten by using back edges.
 * - Heap for basic block is obtained by merging heaps from predecessors.  If
 * heap value conflicts among predecessors it is not added.
 * - Instructions are deleted at the end of pass.  If a deleted instruction is
 * the cause that another instruction now without users, this instruction is
 * deleted as well.  This process continues recursively.
 *
 * Loops are handled in the following way:
 * - on the loop header we record all loads from preheader's heap.  They are
 * potential memory accesses that can be used to eliminated accesses inside the
 * loop. We call them phi-candidates (in future they can be used to reuse
 * stores inside the loop).
 * - visiting any memory access inside a loop we check the aliasing with
 * phi-candidates and record aliased ones to a corresponding candidate.
 * - all phi-candidates of a loop (and of all outer loops of this loop) are
 * invalidated if any of instructions that invalidate heap have been met in
 * this loop.
 * - after actual deletion of collected accesses for elimination we iterate
 * over candidates with aliased accesses.  If any of aliased accesses for a
 * candidate is a store, we do nothing.  If among aliased accesses only loads,
 * we simply replace MUST_ALIASed loads with the corresponding candidate.
 */
class Lse : public Optimization {
    using Optimization::Optimization;

public:
    enum EquivClass { EQ_ARRAY = 0, EQ_STATIC, EQ_POOL, EQ_OBJECT, EQ_LAST };

    struct HeapValue {
        Inst *origin;  // The instruction the value comes from
        Inst *val;     // The value itself
        bool read;     // Whether the value could be read by the code we don't know anything about
        bool local;    // Whether this value should be only used in the BasicBlock it originated from
    };

    using Heap = ArenaDoubleUnorderedMap<BasicBlock *, Inst *, struct HeapValue>;
    using PhiCands = ArenaDoubleUnorderedMap<Loop *, Inst *, InstVector>;
    using HeapEqClasses = ArenaUnorderedMap<int, std::pair<Heap, PhiCands>>;

    explicit Lse(Graph *graph, bool hoist_loads = true)
        : Optimization(graph), hoist_loads_(hoist_loads), be_alive_(GetGraph()->GetLocalAllocator()->Adapter()) {};

    NO_MOVE_SEMANTIC(Lse);
    NO_COPY_SEMANTIC(Lse);
    ~Lse() override = default;

    bool RunImpl() override;

    bool IsEnable() const override
    {
        return OPTIONS.IsCompilerLse();
    }

    const char *GetPassName() const override
    {
        return "LSE";
    }

    static bool CanEliminateInstruction(Inst *inst);

    static constexpr size_t AA_CALLS_LIMIT = 20000;
    static constexpr size_t LS_ACCESS_LIMIT = 32;

private:
    void InitializeHeap(BasicBlock *block, HeapEqClasses *heaps);
    void MergeHeapValuesForLoop(BasicBlock *block, HeapEqClasses *heaps);
    int MergeHeapValuesForBlock(BasicBlock *block, HeapEqClasses *heaps, Marker phi_fixup_mrk);
    void FixupPhisInBlock(BasicBlock *block, Marker phi_fixup_mrk);
    const char *GetEliminationCode(Inst *inst, Inst *origin);
    void ApplyHoistToCandidate(Loop *loop, Inst *alive);
    void TryToHoistLoadFromLoop(Loop *loop, HeapEqClasses *heaps,
                                const ArenaUnorderedMap<Inst *, struct HeapValue> *eliminated);
    void DeleteInstruction(Inst *inst, Inst *value);
    void DeleteInstructions(const ArenaUnorderedMap<Inst *, struct HeapValue> &eliminated);

private:
    bool applied_ {false};
    bool hoist_loads_;
    SaveStateBridgesBuilder ssb_;
    ArenaUnorderedSet<Inst *> be_alive_;
};

}  // namespace panda::compiler

#endif  //  COMPILER_OPTIMIZER_OPTIMIZATIONS_LSE_H_
