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

#include "linear_order.h"
#include "optimizer/analysis/loop_analyzer.h"
#include "optimizer/ir/basicblock.h"
#include "optimizer/ir/graph.h"

namespace panda::compiler {
LinearOrder::LinearOrder(Graph *graph)
    : Analysis(graph),
      linear_blocks_(graph->GetAllocator()->Adapter()),
      rpo_blocks_(graph->GetAllocator()->Adapter()),
      reordered_blocks_(graph->GetAllocator()->Adapter())
{
}

void LinearOrder::HandleIfBlock(BasicBlock *if_true_block, BasicBlock *next_block)
{
    ASSERT(if_true_block != nullptr && next_block != nullptr);
    ASSERT(!if_true_block->IsEmpty());
    if (if_true_block->GetTrueSuccessor() == next_block) {
        // The following swap of successors could break loop analyzer results in the case of irreducible loop
        GetGraph()->InvalidateAnalysis<LoopAnalyzer>();

        auto if_inst = if_true_block->GetLastInst();
        if_true_block->SwapTrueFalseSuccessors<true>();
        if (if_inst->GetOpcode() == Opcode::IfImm) {
            if_inst->CastToIfImm()->InverseConditionCode();
        } else if (if_inst->GetOpcode() == Opcode::If) {
            if_inst->CastToIf()->InverseConditionCode();
        } else if (if_inst->GetOpcode() == Opcode::AddOverflow) {
            if_inst->CastToAddOverflow()->InverseConditionCode();
        } else if (if_inst->GetOpcode() == Opcode::SubOverflow) {
            if_inst->CastToSubOverflow()->InverseConditionCode();
        } else {
            LOG(FATAL, COMPILER) << "Unexpected `If` instruction: " << *if_inst;
        }
    } else if (if_true_block->GetFalseSuccessor() != next_block && !if_true_block->GetSuccessor(0)->IsEndBlock()) {
        if_true_block->SetNeedsJump(true);
    }
}

void LinearOrder::HandlePrevInstruction(BasicBlock *block, BasicBlock *prev_block)
{
    ASSERT(block != nullptr && prev_block != nullptr);
    ASSERT(!prev_block->NeedsJump());
    if (!prev_block->IsEmpty()) {
        auto prev_inst = prev_block->GetLastInst();
        switch (prev_inst->GetOpcode()) {
            case Opcode::IfImm:
            case Opcode::If:
            case Opcode::AddOverflow:
            case Opcode::SubOverflow:
                ASSERT(prev_block->GetSuccsBlocks().size() == MAX_SUCCS_NUM);
                HandleIfBlock(prev_block, block);
                break;

            case Opcode::Throw:
                break;

            default:
                ASSERT(prev_block->GetSuccsBlocks().size() == 1 || prev_block->IsTryBegin() || prev_block->IsTryEnd());
                if (block != prev_block->GetSuccessor(0) && !prev_block->GetLastInst()->IsControlFlow()) {
                    prev_block->SetNeedsJump(true);
                }
                break;
        }
    } else if (!prev_block->IsEndBlock() && block != prev_block->GetSuccessor(0) &&
               !prev_block->GetSuccessor(0)->IsEndBlock()) {
        ASSERT(prev_block->GetSuccsBlocks().size() == 1 || prev_block->IsTryEnd());
        prev_block->SetNeedsJump(true);
    }
}

static void AddSortedByPc(ArenaList<BasicBlock *> *rpo_blocks, BasicBlock *bb)
{
    auto cmp = [](BasicBlock *lhs, BasicBlock *rhs) { return lhs->GetGuestPc() >= rhs->GetGuestPc(); };

    if (rpo_blocks->empty()) {
        rpo_blocks->push_back(bb);
        return;
    }

    auto iter = rpo_blocks->end();
    --iter;
    while (true) {
        if (cmp(bb, *iter)) {
            rpo_blocks->insert(++iter, bb);
            break;
        }
        if (iter == rpo_blocks->begin()) {
            rpo_blocks->push_front(bb);
            break;
        }
        --iter;
    }
}

template <class T>
void LinearOrder::MakeLinearOrder(const T &blocks)
{
    linear_blocks_.clear();
    linear_blocks_.reserve(blocks.size());

    BasicBlock *prev = nullptr;
    for (auto block : blocks) {
        if (prev != nullptr) {
            HandlePrevInstruction(block, prev);
        }
        linear_blocks_.push_back(block);
        prev = block;
    }

    if (prev != nullptr && !prev->IsEndBlock() && !prev->GetSuccessor(0)->IsEndBlock()) {
        // Handle last block
        ASSERT(prev->GetSuccsBlocks().size() == 1 || prev->IsIfBlock());
        prev->SetNeedsJump(true);
    }
}

BasicBlock *LinearOrder::LeastLikelySuccessor(const BasicBlock *block)
{
    auto least_likely_successor = LeastLikelySuccessorByBranchCounter(block);
    if (least_likely_successor != nullptr) {
        return least_likely_successor;
    }

    least_likely_successor = LeastLikelySuccessorByPreference(block);
    if (least_likely_successor != nullptr) {
        return least_likely_successor;
    }

    if (block->GetSuccsBlocks().size() != MAX_SUCCS_NUM) {
        return nullptr;
    }

    auto true_succ = block->GetTrueSuccessor();
    auto false_succ = block->GetFalseSuccessor();
    ASSERT(true_succ != nullptr && false_succ != nullptr);
    if (false_succ->IsMarked(blocks_marker_) != true_succ->IsMarked(blocks_marker_)) {
        return false_succ->IsMarked(blocks_marker_) ? false_succ : true_succ;
    }
    return nullptr;
}

BasicBlock *LinearOrder::LeastLikelySuccessorByBranchCounter(const BasicBlock *block)
{
    if (!OPTIONS.IsCompilerFreqBasedBranchReorder()) {
        return nullptr;
    }

    if (block->GetSuccsBlocks().size() != MAX_SUCCS_NUM) {
        return nullptr;
    }

    auto counter0 = GetBranchCounter(block, true);
    auto counter1 = GetBranchCounter(block, false);

    if (counter0 > 0 || counter1 > 0) {
        auto denom = std::max(counter0, counter1);
        ASSERT(denom != 0);
        // NOLINTNEXTLINE(readability-magic-numbers)
        auto r = (counter0 - counter1) * 100 / denom;
        if (std::abs(r) < OPTIONS.GetCompilerFreqBasedBranchReorderThreshold()) {
            return nullptr;
        }
        return r < 0 ? block->GetTrueSuccessor() : block->GetFalseSuccessor();
    }

    return nullptr;
}

int64_t LinearOrder::GetBranchCounter(const BasicBlock *block, bool true_succ)
{
    auto counter = GetGraph()->GetBranchCounter(block, true_succ);
    if (counter > 0) {
        return counter;
    }
    if (IsConditionChainCounter(block)) {
        return GetConditionChainCounter(block, true_succ);
    }
    return 0;
}

bool LinearOrder::IsConditionChainCounter(const BasicBlock *block)
{
    auto last_inst = block->GetLastInst();
    if (last_inst->GetOpcode() != Opcode::IfImm) {
        return false;
    }
    auto last_inst_input = last_inst->GetInput(0).GetInst();
    if (!last_inst_input->IsPhi()) {
        return false;
    }
    for (auto &input : last_inst_input->GetInputs()) {
        if (!input.GetInst()->IsConst()) {
            return false;
        }
        if (input.GetInst()->GetType() != DataType::INT64) {
            return false;
        }
        auto val = input.GetInst()->CastToConstant()->GetIntValue();
        if (val != 0 && val != 1) {
            return false;
        }
    }
    return true;
}

int64_t LinearOrder::GetConditionChainCounter(const BasicBlock *block, bool true_succ)
{
    if (true_succ != block->IsInverted()) {
        return GetConditionChainTrueSuccessorCounter(block);
    }

    return GetConditionChainFalseSuccessorCounter(block);
}

int64_t LinearOrder::GetConditionChainTrueSuccessorCounter(const BasicBlock *block)
{
    auto last_inst = block->GetLastInst();
    auto last_inst_input = last_inst->GetInput(0).GetInst();
    int64_t counter = 0;
    for (size_t i = 0; i < last_inst_input->GetInputsCount(); i++) {
        auto input = last_inst_input->GetInput(i);
        auto val = input.GetInst()->CastToConstant()->GetIntValue();
        if (val != 1) {
            continue;
        }
        auto bb = last_inst_input->GetBasicBlock();
        auto pred = bb->GetPredBlockByIndex(i);
        while (pred->GetSuccsBlocks().size() != MAX_SUCCS_NUM) {
            bb = pred;
            if (pred->GetPredsBlocks().empty()) {
                return 0;
            }
            pred = pred->GetPredBlockByIndex(0);
        }
        counter += GetGraph()->GetBranchCounter(pred, pred->GetTrueSuccessor() == bb);
    }
    return counter;
}

int64_t LinearOrder::GetConditionChainFalseSuccessorCounter(const BasicBlock *block)
{
    auto last_inst = block->GetLastInst();
    auto last_inst_input = last_inst->GetInput(0).GetInst();
    auto bb = last_inst_input->GetBasicBlock();
    BasicBlock *false_pred = nullptr;
    for (size_t i = 0; i < last_inst_input->GetInputsCount(); i++) {
        auto input = last_inst_input->GetInput(i);
        auto val = input.GetInst()->CastToConstant()->GetIntValue();
        if (val == 0) {
            false_pred = bb->GetPredBlockByIndex(i);
            break;
        }
    }
    if (false_pred == nullptr) {
        return 0;
    }
    while (false_pred->GetSuccsBlocks().size() != MAX_SUCCS_NUM) {
        bb = false_pred;
        if (bb->GetPredsBlocks().empty()) {
            return 0;
        }
        false_pred = false_pred->GetPredBlockByIndex(0);
    }
    return GetGraph()->GetBranchCounter(false_pred, false_pred->GetTrueSuccessor() == bb);
}

BasicBlock *LinearOrder::LeastLikelySuccessorByPreference(const BasicBlock *block)
{
    if (block->GetSuccsBlocks().size() != MAX_SUCCS_NUM) {
        return nullptr;
    }

    auto last_inst = block->GetLastInst();
    switch (last_inst->GetOpcode()) {
        case Opcode::If: {
            auto if_inst = last_inst->CastToIf();
            if (if_inst->IsLikely()) {
                ASSERT(!if_inst->IsUnlikely());
                return block->GetFalseSuccessor();
            }
            if (if_inst->IsUnlikely()) {
                ASSERT(!if_inst->IsLikely());
                return block->GetTrueSuccessor();
            }
            return nullptr;
        }
        case Opcode::IfImm: {
            auto ifimm_inst = last_inst->CastToIfImm();
            if (ifimm_inst->IsLikely()) {
                ASSERT(!ifimm_inst->IsUnlikely());
                return block->GetFalseSuccessor();
            }
            if (ifimm_inst->IsUnlikely()) {
                ASSERT(!ifimm_inst->IsLikely());
                return block->GetTrueSuccessor();
            }
            return nullptr;
        }
        default:
            return nullptr;
    }
}
// Similar to DFS but move least frequent branch to the end.
// First time method is called with defer_least_frequent=true template param which moves least likely successors to the
// end. After all most likely successors are processed call method with defer_least_frequent=false and process least
// frequent successors with DFS.
template <bool DEFER_LEAST_FREQUENT>
void LinearOrder::DFSAndDeferLeastFrequentBranches(BasicBlock *block, size_t *blocks_count)
{
    ASSERT(block != nullptr);
    block->SetMarker(marker_);

    auto least_likely_successor = DEFER_LEAST_FREQUENT ? LeastLikelySuccessor(block) : nullptr;
    if (least_likely_successor == nullptr) {
        for (auto succ_block : block->GetSuccsBlocks()) {
            if (!succ_block->IsMarked(marker_)) {
                DFSAndDeferLeastFrequentBranches<DEFER_LEAST_FREQUENT>(succ_block, blocks_count);
            }
        }
    } else {
        linear_blocks_.push_back(least_likely_successor);
        auto most_likely_successor = least_likely_successor == block->GetTrueSuccessor() ? block->GetFalseSuccessor()
                                                                                         : block->GetTrueSuccessor();
        if (!most_likely_successor->IsMarked(marker_)) {
            DFSAndDeferLeastFrequentBranches<DEFER_LEAST_FREQUENT>(most_likely_successor, blocks_count);
        }
    }

    if constexpr (DEFER_LEAST_FREQUENT) {  // NOLINT(readability-braces-around-statements,bugprone-suspicious-semicolon)
        for (auto succ_block : linear_blocks_) {
            if (!succ_block->IsMarked(marker_)) {
                DFSAndDeferLeastFrequentBranches<false>(succ_block, blocks_count);
            }
        }
        linear_blocks_.clear();
    }

    ASSERT(blocks_count != nullptr && *blocks_count > 0);
    reordered_blocks_[--(*blocks_count)] = block;
}

void LinearOrder::MarkSideExitsBlocks()
{
    auto end_block = GetGraph()->GetEndBlock();
    // Check on infinite loop
    if (end_block == nullptr) {
        return;
    }
    for (auto pred_block : end_block->GetPredsBlocks()) {
        if (pred_block->IsEmpty() || pred_block->IsStartBlock()) {
            continue;
        }
        ASSERT(pred_block->GetSuccsBlocks().size() == 1);
        auto last_inst = pred_block->GetLastInst();
        ASSERT(last_inst != nullptr);
        if (last_inst->IsReturn()) {
            continue;
        }
        pred_block->SetMarker(blocks_marker_);
    }
}

bool LinearOrder::RunImpl()
{
    if (GetGraph()->IsBytecodeOptimizer()) {
        // Make blocks order sorted by bytecode PC
        rpo_blocks_.clear();
        for (auto bb : GetGraph()->GetBlocksRPO()) {
            ASSERT(bb->GetGuestPc() != INVALID_PC);
            AddSortedByPc(&rpo_blocks_, bb);
        }
        MakeLinearOrder(rpo_blocks_);
    } else {
        marker_ = GetGraph()->NewMarker();
        blocks_marker_ = GetGraph()->NewMarker();
        size_t blocks_count = GetGraph()->GetAliveBlocksCount();
        linear_blocks_.clear();
        reordered_blocks_.clear();
        reordered_blocks_.resize(blocks_count);
        MarkSideExitsBlocks();
        DFSAndDeferLeastFrequentBranches<true>(GetGraph()->GetStartBlock(), &blocks_count);
#ifndef NDEBUG
        if (blocks_count != 0) {
            std::cerr << "There are unreachable blocks:\n";
            for (auto bb : *GetGraph()) {
                if (bb != nullptr && !bb->IsMarked(marker_)) {
                    bb->Dump(&std::cerr);
                }
            }
            UNREACHABLE();
        }
#endif  // NDEBUG
        MakeLinearOrder(reordered_blocks_);

        GetGraph()->EraseMarker(marker_);
        GetGraph()->EraseMarker(blocks_marker_);
    }
    return true;
}
}  // namespace panda::compiler
