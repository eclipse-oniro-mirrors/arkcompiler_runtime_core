/*
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

#include "peephole_optimizer.h"

#include "compiler/optimizer/analysis/loop_analyzer.h"
#include "compiler/optimizer/ir/basicblock.h"

namespace panda::bytecodeopt {

namespace DataType = compiler::DataType;
using Inst = compiler::Inst;
using BasicBlock = compiler::BasicBlock;
using IntrinsicId = compiler::RuntimeInterface::IntrinsicId;

bool PeepholeOptimizer::IsTrueIntrinsic(IntrinsicId id)
{
    return id == IntrinsicId::ISTRUE || id == IntrinsicId::CALLRUNTIME_ISTRUE_PREF_IMM8;
}

bool PeepholeOptimizer::IsFalseIntrinsic(IntrinsicId id)
{
    return id == IntrinsicId::ISFALSE || id == IntrinsicId::CALLRUNTIME_ISFALSE_PREF_IMM8;
}

bool PeepholeOptimizer::IsToNumeric(IntrinsicId id)
{
    return id == IntrinsicId::TONUMERIC_IMM8 || id == IntrinsicId::DEPRECATED_TONUMERIC_PREF_V8;
}

bool PeepholeOptimizer::IsStrictComparison(IntrinsicId id)
{
    return id == IntrinsicId::STRICTEQ_IMM8_V8 || id == IntrinsicId::STRICTNOTEQ_IMM8_V8;
}

bool PeepholeOptimizer::IsStrictNumericType(DataType::Type type)
{
    switch (type) {
        case DataType::INT8:
        case DataType::UINT8:
        case DataType::INT16:
        case DataType::UINT16:
        case DataType::INT32:
        case DataType::UINT32:
        case DataType::INT64:
        case DataType::UINT64:
        case DataType::FLOAT32:
        case DataType::FLOAT64:
            return true;
        default:
            return false;
    }
}

size_t PeepholeOptimizer::GetDataInputCount(const Inst *inst)
{
    size_t count = inst->GetInputsCount();
    if (inst->RequireState() && count != 0 && inst->GetInput(count - 1).GetInst()->IsSaveState()) {
        --count;
    }
    return count;
}

bool PeepholeOptimizer::HasOnlyUser(const Inst *definition, const Inst *user)
{
    if (!definition->HasSingleUser()) {
        return false;
    }
    return definition->GetUsers().begin()->GetInst() == user;
}

bool PeepholeOptimizer::RunImpl()
{
    bool changed = false;
    for (auto *block : GetGraph()->GetBlocksRPO()) {
        changed = RewriteNullishCheckChain(block) || changed;
        for (auto *inst : block->AllInsts()) {
            if (inst->IsIntrinsic()) {
                changed = RewriteBoolean(inst) || RewriteToNumeric(inst) || changed;
            }
        }
    }
    return changed;
}

void PeepholeOptimizer::InvalidateAnalyses()
{
    GetGraph()->InvalidateAnalysis<compiler::LoopAnalyzer>();
    compiler::InvalidateBlocksOrderAnalyzes(GetGraph());
}

PeepholeOptimizer::NullishConstant PeepholeOptimizer::GetNullishConstant(const Inst *inst)
{
    if (inst == nullptr) {
        return NullishConstant::NONE;
    }
    if (inst->IsIntrinsic() && GetDataInputCount(inst) == 0) {
        switch (inst->CastToIntrinsic()->GetIntrinsicId()) {
            case IntrinsicId::LDNULL:
                return NullishConstant::NULL_VALUE;
            case IntrinsicId::LDUNDEFINED:
                return NullishConstant::UNDEFINED_VALUE;
            default:
                return NullishConstant::NONE;
        }
    }
    if (inst->GetOpcode() != compiler::Opcode::CastValueToAnyType) {
        return NullishConstant::NONE;
    }
    auto *cast = inst->CastToCastValueToAnyType();
    if (cast->GetInput(0).GetInst() == nullptr || !cast->GetInput(0).GetInst()->IsConst()) {
        return NullishConstant::NONE;
    }
    switch (cast->GetAnyType()) {
        case compiler::AnyBaseType::ECMASCRIPT_NULL_TYPE:
            return NullishConstant::NULL_VALUE;
        case compiler::AnyBaseType::ECMASCRIPT_UNDEFINED_TYPE:
            return NullishConstant::UNDEFINED_VALUE;
        default:
            return NullishConstant::NONE;
    }
}

bool PeepholeOptimizer::GetNullishCompare(const Inst *inst, Inst **value, Inst **constant, NullishConstant *kind)
{
    if (inst == nullptr || !inst->IsIntrinsic() || !IsStrictComparison(inst->CastToIntrinsic()->GetIntrinsicId()) ||
        GetDataInputCount(inst) != 2) {
        return false;
    }
    auto *left = inst->GetInput(0).GetInst();
    auto *right = inst->GetInput(1).GetInst();
    auto left_kind = GetNullishConstant(left);
    auto right_kind = GetNullishConstant(right);
    if ((left_kind == NullishConstant::NONE) == (right_kind == NullishConstant::NONE)) {
        return false;
    }
    if (left_kind != NullishConstant::NONE) {
        *value = right;
        *constant = left;
        *kind = left_kind;
    } else {
        *value = left;
        *constant = right;
        *kind = right_kind;
    }
    return *value != nullptr;
}

Inst *PeepholeOptimizer::GetStrictCompareFromBranch(Inst *branch)
{
    if (branch == nullptr || branch->GetOpcode() != compiler::Opcode::IfImm) {
        return nullptr;
    }
    auto *condition = branch->GetInput(0).GetInst();
    if (condition != nullptr && condition->IsIntrinsic() &&
        IsStrictComparison(condition->CastToIntrinsic()->GetIntrinsicId())) {
        return condition;
    }
    if (condition == nullptr || condition->GetOpcode() != compiler::Opcode::Compare) {
        return nullptr;
    }
    auto *left = condition->GetInput(0).GetInst();
    auto *right = condition->GetInput(1).GetInst();
    auto *candidate = compiler::IsZeroConstant(left) ? right : (compiler::IsZeroConstant(right) ? left : nullptr);
    if (candidate == nullptr || !candidate->IsIntrinsic() ||
        !IsStrictComparison(candidate->CastToIntrinsic()->GetIntrinsicId())) {
        return nullptr;
    }
    return candidate;
}

bool PeepholeOptimizer::GetEqualitySuccessors(Inst *compare, Inst *branch, EqualitySuccessors *successors,
                                              Inst **condition)
{
    if (compare == nullptr || branch == nullptr || !compare->IsIntrinsic() ||
        branch->GetOpcode() != compiler::Opcode::IfImm) {
        return false;
    }
    auto *if_imm = branch->CastToIfImm();
    if (if_imm->GetImm() != 0 || (if_imm->GetCc() != compiler::CC_EQ && if_imm->GetCc() != compiler::CC_NE)) {
        return false;
    }
    auto id = compare->CastToIntrinsic()->GetIntrinsicId();
    if (!IsStrictComparison(id)) {
        return false;
    }
    *condition = branch->GetInput(0).GetInst();
    bool condition_true_means_compare_true = true;
    if (*condition == compare) {
        if (!HasOnlyUser(compare, branch)) {
            return false;
        }
    } else {
        if (*condition == nullptr || (*condition)->GetOpcode() != compiler::Opcode::Compare ||
            !HasOnlyUser(compare, *condition) || !HasOnlyUser(*condition, branch)) {
            return false;
        }
        auto *zero_compare = (*condition)->CastToCompare();
        if (zero_compare->GetCc() != compiler::CC_EQ && zero_compare->GetCc() != compiler::CC_NE) {
            return false;
        }
        auto *left = zero_compare->GetInput(0).GetInst();
        auto *right = zero_compare->GetInput(1).GetInst();
        if (!((left == compare && compiler::IsZeroConstant(right)) ||
              (right == compare && compiler::IsZeroConstant(left)))) {
            return false;
        }
        condition_true_means_compare_true = zero_compare->GetCc() == compiler::CC_NE;
    }
    bool compare_true_means_equal = id == IntrinsicId::STRICTEQ_IMM8_V8;
    bool branch_true_means_condition_true = if_imm->GetCc() == compiler::CC_NE;
    bool branch_true_means_compare_true = condition_true_means_compare_true == branch_true_means_condition_true;
    bool equality_uses_true_edge = compare_true_means_equal == branch_true_means_compare_true;
    auto *block = branch->GetBasicBlock();
    if (block == nullptr || block->GetSuccsBlocks().size() != 2) {
        return false;
    }
    successors->equal = equality_uses_true_edge ? block->GetTrueSuccessor() : block->GetFalseSuccessor();
    successors->not_equal = equality_uses_true_edge ? block->GetFalseSuccessor() : block->GetTrueSuccessor();
    return true;
}

bool PeepholeOptimizer::HasSameEhRegion(const BasicBlock *first, const BasicBlock *second)
{
    if (first->IsTry() != second->IsTry() || first->IsCatch() != second->IsCatch()) {
        return false;
    }
    return !first->IsTry() || first->GetTryId() == second->GetTryId();
}

bool PeepholeOptimizer::IsOnlyNullishCheckScaffolding(const BasicBlock *block, const Inst *constant,
                                                      const Inst *compare, const Inst *condition, const Inst *branch)
{
    if (!HasOnlyUser(constant, compare)) {
        return false;
    }
    for (auto *inst : block->AllInsts()) {
        if (inst != constant && inst != compare && inst != condition && inst != branch && !inst->IsSaveState()) {
            return false;
        }
        if (inst->IsSaveState()) {
            for (const auto &user : inst->GetUsers()) {
                auto *user_inst = user.GetInst();
                if (user_inst != constant && user_inst != compare && user_inst != condition) {
                    return false;
                }
            }
        }
    }
    return true;
}

bool PeepholeOptimizer::RewriteNullishCheckChain(BasicBlock *block)
{
    if (block == nullptr || block->IsEmpty() || block->GetSuccsBlocks().size() != 2) {
        return false;
    }
    auto *first_branch = block->GetLastInst();
    if (first_branch == nullptr || first_branch->GetOpcode() != compiler::Opcode::IfImm) {
        return false;
    }
    auto *first_compare = GetStrictCompareFromBranch(first_branch);
    Inst *first_value = nullptr;
    Inst *first_constant = nullptr;
    NullishConstant first_kind = NullishConstant::NONE;
    EqualitySuccessors first_successors;
    Inst *first_condition = nullptr;
    if (!GetNullishCompare(first_compare, &first_value, &first_constant, &first_kind) ||
        !GetEqualitySuccessors(first_compare, first_branch, &first_successors, &first_condition)) {
        return false;
    }

    auto *middle = first_successors.not_equal;
    if (middle == nullptr || middle->IsEmpty() || middle->GetPredsBlocks().size() != 1 ||
        middle->GetPredsBlocks()[0] != block || middle->GetSuccsBlocks().size() != 2 || middle->IsTryBegin() ||
        middle->IsTryEnd() || middle->IsCatchBegin() || middle->IsCatchEnd() || first_successors.equal->HasPhi() ||
        !HasSameEhRegion(block, middle)) {
        return false;
    }
    auto *second_branch = middle->GetLastInst();
    if (second_branch == nullptr || second_branch->GetOpcode() != compiler::Opcode::IfImm) {
        return false;
    }
    auto *second_compare = GetStrictCompareFromBranch(second_branch);
    Inst *second_value = nullptr;
    Inst *second_constant = nullptr;
    NullishConstant second_kind = NullishConstant::NONE;
    EqualitySuccessors second_successors;
    Inst *second_condition = nullptr;
    if (!GetNullishCompare(second_compare, &second_value, &second_constant, &second_kind) ||
        !GetEqualitySuccessors(second_compare, second_branch, &second_successors, &second_condition) ||
        first_value != second_value || first_kind == second_kind ||
        first_compare->CastToIntrinsic()->GetIntrinsicId() != second_compare->CastToIntrinsic()->GetIntrinsicId() ||
        first_successors.equal != second_successors.equal ||
        !IsOnlyNullishCheckScaffolding(middle, second_constant, second_compare, second_condition, second_branch)) {
        return false;
    }

    auto *nonnull = second_successors.not_equal;
    if (nonnull == nullptr || nonnull == first_successors.equal || nonnull == middle || nonnull->HasPhi()) {
        return false;
    }
    auto *first_intrinsic = first_compare->CastToIntrinsic();
    first_intrinsic->SetIntrinsicId(first_intrinsic->GetIntrinsicId() == IntrinsicId::STRICTEQ_IMM8_V8
                                        ? IntrinsicId::EQ_IMM8_V8
                                        : IntrinsicId::NOTEQ_IMM8_V8);
    middle->RemovePred(block);
    block->ReplaceSucc(middle, nonnull);
    GetGraph()->DisconnectBlock(middle, true, false);
    return true;
}

bool PeepholeOptimizer::RewriteBoolean(Inst *inst)
{
    auto *outer = inst->CastToIntrinsic();
    auto outer_id = outer->GetIntrinsicId();
    if (!IsTrueIntrinsic(outer_id) && !IsFalseIntrinsic(outer_id)) {
        return false;
    }

    if (GetDataInputCount(inst) != 1) {
        return false;
    }
    auto *inner = inst->GetInput(0).GetInst();
    if (inner == nullptr || !inner->IsIntrinsic() || inner->GetBasicBlock() != inst->GetBasicBlock()) {
        return false;
    }

    auto *inner_intrinsic = inner->CastToIntrinsic();
    auto inner_id = inner_intrinsic->GetIntrinsicId();

    if (IsTrueIntrinsic(outer_id)) {
        // These producers already return Boolean values.
        if (!IsTrueIntrinsic(inner_id) && !IsFalseIntrinsic(inner_id) && !IsStrictComparison(inner_id)) {
            return false;
        }
        inst->ReplaceUsers(inner);
        inst->ClearFlag(compiler::inst_flags::NO_DCE);
        return true;
    }

    if (IsTrueIntrinsic(inner_id) || IsFalseIntrinsic(inner_id)) {
        // isfalse(istrue(x)) -> isfalse(x)
        // isfalse(isfalse(x)) -> istrue(x)
        if (IsFalseIntrinsic(inner_id)) {
            // Keep the outer instruction family (direct/callruntime), so its immediate layout remains valid.
            auto replacement_id =
                outer_id == IntrinsicId::ISFALSE ? IntrinsicId::ISTRUE : IntrinsicId::CALLRUNTIME_ISTRUE_PREF_IMM8;
            outer->SetIntrinsicId(replacement_id);
        }
        inst->SetInput(0, inner->GetInput(0).GetInst());
        inner->ClearFlag(compiler::inst_flags::NO_DCE);
        return true;
    }

    // Invert the comparison only when no other user needs its original result.
    if (!IsStrictComparison(inner_id) || !HasOnlyUser(inner, inst) || GetDataInputCount(inner) != 2) {
        return false;
    }

    auto replacement_id =
        inner_id == IntrinsicId::STRICTEQ_IMM8_V8 ? IntrinsicId::STRICTNOTEQ_IMM8_V8 : IntrinsicId::STRICTEQ_IMM8_V8;
    inner_intrinsic->SetIntrinsicId(replacement_id);
    inst->ReplaceUsers(inner);
    inst->ClearFlag(compiler::inst_flags::NO_DCE);
    return true;
}

bool PeepholeOptimizer::RewriteToNumeric(Inst *inst)
{
    auto *outer = inst->CastToIntrinsic();
    if (!IsToNumeric(outer->GetIntrinsicId()) || GetDataInputCount(inst) != 1) {
        return false;
    }

    auto *input = inst->GetInput(0).GetInst();
    if (input == nullptr) {
        return false;
    }

    if (input->IsIntrinsic() && IsToNumeric(input->CastToIntrinsic()->GetIntrinsicId())) {
        // Do not infer cross-block equivalence from a dynamic conversion's output type.
        if (input->GetBasicBlock() != inst->GetBasicBlock()) {
            return false;
        }
        // Keep the first conversion, including its side effects and exception point.
        if (GetDataInputCount(input) == 1) {
            inst->ReplaceUsers(input);
            inst->ClearFlag(compiler::inst_flags::NO_DCE);
            return true;
        }
    }

    // Only the strict integer and floating-point IR types prove that ToNumeric is redundant.
    // In particular, BOOL, POINTER and ANY are deliberately excluded.
    if (IsStrictNumericType(input->GetType())) {
        inst->ReplaceUsers(input);
        inst->ClearFlag(compiler::inst_flags::NO_DCE);
        return true;
    }
    return false;
}

}  // namespace panda::bytecodeopt
