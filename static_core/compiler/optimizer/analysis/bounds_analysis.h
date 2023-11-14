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

#ifndef COMPILER_OPTIMIZER_ANALYSIS_BOUNDSRANGE_ANALYSIS_H_
#define COMPILER_OPTIMIZER_ANALYSIS_BOUNDSRANGE_ANALYSIS_H_

#include "optimizer/ir/graph_visitor.h"
#include "optimizer/ir/datatype.h"
#include "optimizer/ir/inst.h"
#include "optimizer/pass.h"
#include "utils/arena_containers.h"

namespace panda::compiler {
/**
 * Represents a range of values that a variable might have.
 *
 * It is used to represent variables of integral types according to their size
 * and sign.
 * It is used for REFERENCE type as well but only for reasoning whether a
 * variable is NULL or not.
 */
class BoundsRange {
public:
    using RangePair = std::pair<BoundsRange, BoundsRange>;

    explicit BoundsRange(DataType::Type type = DataType::INT64) : left_(GetMin(type)), right_(GetMax(type)) {};

    explicit BoundsRange(int64_t left, int64_t right, const Inst *inst = nullptr,
                         DataType::Type type = DataType::INT64);

    explicit BoundsRange(int64_t val, DataType::Type type = DataType::INT64);

    DEFAULT_COPY_SEMANTIC(BoundsRange);
    DEFAULT_MOVE_SEMANTIC(BoundsRange);
    ~BoundsRange() = default;

    void SetLenArray(const Inst *inst);

    const Inst *GetLenArray()
    {
        return len_array_;
    }
    int64_t GetLeft() const;

    int64_t GetRight() const;

    BoundsRange FitInType(DataType::Type type) const;

    BoundsRange Neg() const;

    BoundsRange Abs() const;

    BoundsRange Add(const BoundsRange &range) const;

    BoundsRange Sub(const BoundsRange &range) const;

    BoundsRange Mul(const BoundsRange &range) const;

    BoundsRange Div(const BoundsRange &range) const;

    BoundsRange Mod(const BoundsRange &range);

    BoundsRange And(const BoundsRange &range);

    BoundsRange Shr(const BoundsRange &range, DataType::Type type = DataType::INT64);

    BoundsRange AShr(const BoundsRange &range, DataType::Type type = DataType::INT64);

    BoundsRange Shl(const BoundsRange &range, DataType::Type type = DataType::INT64);

    bool IsConst() const;

    bool IsMaxRange(DataType::Type type = DataType::INT64) const;

    bool IsEqual(const BoundsRange &range) const;

    bool IsLess(const BoundsRange &range) const;

    bool IsLess(const Inst *inst) const;

    bool IsMore(const BoundsRange &range) const;

    bool IsMoreOrEqual(const BoundsRange &range) const;

    bool IsNotNegative() const;

    bool IsNegative() const;

    bool IsPositive() const;

    bool IsNotPositive() const;

    bool CanOverflow(DataType::Type type = DataType::INT64) const;

    bool CanOverflowNeg(DataType::Type type = DataType::INT64) const;

    static int64_t GetMin(DataType::Type type);

    static int64_t GetMax(DataType::Type type);

    static BoundsRange Union(const ArenaVector<BoundsRange> &ranges);

    static RangePair NarrowBoundsByNE(RangePair const &ranges);
    static RangePair NarrowBoundsCase1(ConditionCode cc, RangePair const &ranges);
    static RangePair NarrowBoundsCase2(ConditionCode cc, RangePair const &ranges);
    static RangePair NarrowBoundsCase3(ConditionCode cc, RangePair const &ranges);
    static RangePair NarrowBoundsCase4(ConditionCode cc, RangePair const &ranges);
    static RangePair NarrowBoundsCase5(ConditionCode cc, RangePair const &ranges);
    static RangePair NarrowBoundsCase6(ConditionCode cc, RangePair const &ranges);

    static RangePair TryNarrowBoundsByCC(ConditionCode cc, RangePair const &ranges);

    static int64_t AddWithOverflowCheck(int64_t left, int64_t right);

    static int64_t MulWithOverflowCheck(int64_t left, int64_t right);

    static int64_t DivWithOverflowCheck(int64_t left, int64_t right);

    static constexpr int64_t MAX_RANGE_VALUE = INT64_MAX;
    static constexpr int64_t MIN_RANGE_VALUE = INT64_MIN;

    bool operator==(const BoundsRange &rhs) const
    {
        return left_ == rhs.left_ && right_ == rhs.right_ && len_array_ == rhs.len_array_;
    }

    void Dump(std::ostream &out = std::cerr) const
    {
        out << "Range = [" << left_ << ", ";
        out << right_ << "]";
        if (len_array_ != nullptr) {
            out << ", len_array = " << len_array_->GetId();
        }
        out << "\n";
    }

private:
    int64_t left_ = MIN_RANGE_VALUE;
    int64_t right_ = MAX_RANGE_VALUE;
    const Inst *len_array_ {nullptr};
};

class BoundsRangeInfo {
public:
    explicit BoundsRangeInfo(ArenaAllocator *aa) : aa_(*aa), bounds_range_info_(aa->Adapter()) {}
    NO_COPY_SEMANTIC(BoundsRangeInfo);
    NO_MOVE_SEMANTIC(BoundsRangeInfo);
    ~BoundsRangeInfo() = default;

    BoundsRange FindBoundsRange(const BasicBlock *block, const Inst *inst) const;

    void SetBoundsRange(const BasicBlock *block, const Inst *inst, BoundsRange range);

    void Clear()
    {
        bounds_range_info_.clear();
    }

private:
    ArenaAllocator &aa_;
    ArenaDoubleUnorderedMap<const BasicBlock *, const Inst *, BoundsRange> bounds_range_info_;
};

// NOLINTNEXTLINE(fuchsia-multiple-inheritance)
class BoundsAnalysis : public Analysis, public GraphVisitor {
public:
    explicit BoundsAnalysis(Graph *graph);
    NO_MOVE_SEMANTIC(BoundsAnalysis);
    NO_COPY_SEMANTIC(BoundsAnalysis);
    ~BoundsAnalysis() override = default;

    const ArenaVector<BasicBlock *> &GetBlocksToVisit() const override;

    bool RunImpl() override;

    const char *GetPassName() const override
    {
        return "BoundsAnalysis";
    }

    BoundsRangeInfo *GetBoundsRangeInfo()
    {
        return &bounds_range_info_;
    }

    const BoundsRangeInfo *GetBoundsRangeInfo() const
    {
        return &bounds_range_info_;
    }

    static bool IsInstNotNull(const Inst *inst, BasicBlock *block);

    static void VisitNeg(GraphVisitor *v, Inst *inst);
    static void VisitNegOverflowAndZeroCheck(GraphVisitor *v, Inst *inst);
    static void VisitAbs(GraphVisitor *v, Inst *inst);
    static void VisitAdd(GraphVisitor *v, Inst *inst);
    static void VisitAddOverflowCheck(GraphVisitor *v, Inst *inst);
    static void VisitSub(GraphVisitor *v, Inst *inst);
    static void VisitSubOverflowCheck(GraphVisitor *v, Inst *inst);
    static void VisitMod(GraphVisitor *v, Inst *inst);
    static void VisitDiv(GraphVisitor *v, Inst *inst);
    static void VisitMul(GraphVisitor *v, Inst *inst);
    static void VisitAnd(GraphVisitor *v, Inst *inst);
    static void VisitShr(GraphVisitor *v, Inst *inst);
    static void VisitAShr(GraphVisitor *v, Inst *inst);
    static void VisitShl(GraphVisitor *v, Inst *inst);
    static void VisitIfImm(GraphVisitor *v, Inst *inst);
    static void VisitPhi(GraphVisitor *v, Inst *inst);
    static void VisitNullCheck(GraphVisitor *v, Inst *inst);

#include "optimizer/ir/visitor.inc"
private:
    static bool ProcessCountableLoop(PhiInst *phi, BoundsRangeInfo *bri);
    static bool CheckTriangleCase(const BasicBlock *block, const BasicBlock *tgt_block);
    static void ProcessNullCheck(GraphVisitor *v, const Inst *check_inst, const Inst *ref_input);

    static BoundsRange UpdateLenArray(BoundsRange range, const Inst *len_array, const Inst *upper);
    static void CalcNewBoundsRangeForIsInstanceInput(GraphVisitor *v, IsInstanceInst *is_instance, IfImmInst *if_imm);
    static void CalcNewBoundsRangeForCompare(GraphVisitor *v, BasicBlock *block, ConditionCode cc, Inst *left,
                                             Inst *right, BasicBlock *tgt_block);
    template <Opcode OPC>
    static void CalcNewBoundsRangeUnary(GraphVisitor *v, const Inst *inst);
    template <Opcode OPC>
    static void CalcNewBoundsRangeBinary(GraphVisitor *v, const Inst *inst);

private:
    BoundsRangeInfo bounds_range_info_;
};
}  // namespace panda::compiler

#endif  // COMPILER_OPTIMIZER_ANALYSIS_BOUNDS_ANALYSIS_H_
