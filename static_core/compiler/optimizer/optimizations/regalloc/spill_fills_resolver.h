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

#ifndef COMPILER_OPTIMIZER_OPTIMIZATIONS_SPILL_FILLS_RESOLVER_H_
#define COMPILER_OPTIMIZER_OPTIMIZATIONS_SPILL_FILLS_RESOLVER_H_

#include "compiler/optimizer/ir/graph_visitor.h"
#include "optimizer/code_generator/registers_description.h"
#include "optimizer/ir/inst.h"
#include "utils/arena_containers.h"

namespace panda::compiler {
class RegAllocBase;

/*
 * There are 3 spill-fill location types:
 * - general register
 * - vector register
 * - stack slot
 *
 * Each spill-fill's source and destination is described by index inside these locations;
 *
 * Since  all destinations are unique, we can use them as keys for table, which collects information about
 * all moves:
 * - genenral register index maps to table key one by one
 * - vector register index offsets on number of genenral registers (MAX_NUM_REGS)
 * - stack slot index offsets on number of all registers (MAX_NUM_REGS + MAX_NUM_VREGS)
 *
 * Iterating over this table `SpillFillsResolver` builds non-conflict chains of spill-fills.
 * Building process is described before `SpillFillsResolver::Reorder()` method.
 */
class SpillFillsResolver : public GraphVisitor {
    static_assert(sizeof(Register) == sizeof(StackSlot), "Register and StackSlot should be the same size");

    using LocationIndex = uint16_t;
    static constexpr auto INVALID_LOCATION_INDEX = std::numeric_limits<LocationIndex>::max();

    struct LocationInfo {
        LocationType location;
        LocationIndex idx;
    };

    struct MoveInfo {
        LocationIndex src;
        DataType::Type reg_type;
    };

public:
    explicit SpillFillsResolver(Graph *graph);
    SpillFillsResolver(Graph *graph, Register resolver, size_t regs_count, size_t vregs_count = 0);
    NO_COPY_SEMANTIC(SpillFillsResolver);
    NO_MOVE_SEMANTIC(SpillFillsResolver);
    ~SpillFillsResolver() override = default;

    const ArenaVector<BasicBlock *> &GetBlocksToVisit() const override;

    void Run();

    void Resolve(SpillFillInst *spill_fill_inst);

    void ResolveIfRequired(SpillFillInst *spill_fill_inst);

    Graph *GetGraph() const;

protected:
    static void VisitSpillFill(GraphVisitor *v, Inst *inst);

private:
    bool NeedToResolve(const ArenaVector<SpillFillData> &spill_fills);
    void ResolveCallSpillFill(SpillFillInst *spill_fill_inst);
    void CollectSpillFillsData(SpillFillInst *spill_fill_inst);
    void Reorder(SpillFillInst *spill_fill_inst);
    LocationIndex CheckAndResolveCyclicDependency(LocationIndex dst_first);
    template <bool CICLYC>
    void AddMovesChain(LocationIndex dst, ArenaVector<LocationIndex> *remap, SpillFillInst *spill_fill_inst);
    LocationIndex GetResolver(DataType::Type type);

    // Get table index by Location type
    LocationIndex Map(Location location)
    {
        if (location.IsRegister()) {
            return location.GetValue();
        }
        if (location.IsFpRegister()) {
            return location.GetValue() + vregs_table_offset_;
        }
        if (location.IsStack()) {
            return location.GetValue() + slots_table_offset_;
        }
        ASSERT(location.IsStackParameter());
        return location.GetValue() + parameter_slots_offset_;
    }

    // Fetch location type and element number inside this location from table index
    Location ToLocation(LocationIndex reg)
    {
        if (reg >= parameter_slots_offset_) {
            return Location::MakeStackParameter(reg - parameter_slots_offset_);
        }
        if (reg >= slots_table_offset_) {
            return Location::MakeStackSlot(reg - slots_table_offset_);
        }
        if (reg >= vregs_table_offset_) {
            return Location::MakeFpRegister(reg - vregs_table_offset_);
        }
        return Location::MakeRegister(reg);
    }

    static inline bool IsPairedReg(Arch arch, DataType::Type type)
    {
        return arch != Arch::NONE && !Is64BitsArch(arch) && Is64Bits(type, arch);
    }

private:
    Graph *graph_ {nullptr};
    ArenaVector<MoveInfo> moves_table_;
    ArenaVector<uint8_t> loads_count_;
    // Group of moves which can be safely inserted before all others
    ArenaVector<SpillFillData> pre_moves_;
    // Group of moves which can be safely inserted after all others
    ArenaVector<SpillFillData> post_moves_;
    Register resolver_;

    const size_t vregs_table_offset_;
    const size_t slots_table_offset_;
    const size_t parameter_slots_offset_;
    const size_t locations_count_;
    ArenaVector<bool> reg_write_;
    ArenaVector<bool> stack_write_;

#include "optimizer/ir/visitor.inc"
};
}  // namespace panda::compiler

#endif  // COMPILER_OPTIMIZER_OPTIMIZATIONS_SPILL_FILLS_RESOLVER_H_
