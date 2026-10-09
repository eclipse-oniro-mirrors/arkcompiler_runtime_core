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

#include "reg_alloc_resolver.h"
#include "reg_type.h"
#include "compiler/optimizer/ir/basicblock.h"

#include <algorithm>
#include <deque>
#include <limits>
#include <unordered_map>
#include <vector>

namespace panda::compiler {

namespace {

struct CatchPhiSaveFact {
    Register slot;
    Register src;
};

struct CatchPhiSaveInfo {
    SpillFillInst *spill_fill;
    size_t spill_fill_index;
    size_t fact_index;
};

constexpr size_t CATCH_PHI_SAVE_STATE_WORD_BITS = sizeof(uint64_t) * BITS_PER_BYTE;
constexpr size_t MAX_CATCH_PHI_SAVE_FACTS = 4096U;
constexpr size_t MAX_CATCH_PHI_SAVE_STATE_WORDS = 1U << 20U;
constexpr size_t MAX_CATCH_PHI_SAVE_BLOCK_VISITS = 1U << 18U;

using CatchPhiSaveState = std::vector<uint64_t>;
using CatchPhiSavesByBundle = std::unordered_map<SpillFillInst *, std::vector<size_t>>;
using CatchPhiFactIndicesByRegister = std::unordered_map<Register, std::vector<size_t>>;
using RegisterWriteCounts = std::unordered_map<Register, size_t>;
using RegisterWritesByBundle = std::unordered_map<SpillFillInst *, RegisterWriteCounts>;

uint32_t MakeCatchPhiSaveFactKey(Register slot, Register src)
{
    return (static_cast<uint32_t>(slot) << (sizeof(Register) * BITS_PER_BYTE)) | static_cast<uint32_t>(src);
}

bool HasCatchPhiSaveFact(const CatchPhiSaveState &state, size_t fact_index)
{
    auto word_index = fact_index / CATCH_PHI_SAVE_STATE_WORD_BITS;
    auto bit_index = fact_index % CATCH_PHI_SAVE_STATE_WORD_BITS;
    return (state[word_index] & (1ULL << bit_index)) != 0;
}

void SetCatchPhiSaveFact(CatchPhiSaveState *state, size_t fact_index)
{
    auto word_index = fact_index / CATCH_PHI_SAVE_STATE_WORD_BITS;
    auto bit_index = fact_index % CATCH_PHI_SAVE_STATE_WORD_BITS;
    (*state)[word_index] |= 1ULL << bit_index;
}

void KillCatchPhiSaveFacts(Register reg, const CatchPhiFactIndicesByRegister &fact_indices_by_register,
                           CatchPhiSaveState *state)
{
    auto facts_it = fact_indices_by_register.find(reg);
    if (facts_it == fact_indices_by_register.end()) {
        return;
    }
    for (auto fact_index : facts_it->second) {
        auto word_index = fact_index / CATCH_PHI_SAVE_STATE_WORD_BITS;
        auto bit_index = fact_index % CATCH_PHI_SAVE_STATE_WORD_BITS;
        (*state)[word_index] &= ~(1ULL << bit_index);
    }
}

bool IsOrdinaryRegister(Register reg)
{
    return reg != INVALID_REG && reg != ACC_REG_ID;
}

void ProcessCatchPhiSaveBundle(SpillFillInst *spill_fill, const std::vector<CatchPhiSaveFact> &facts,
                               const std::vector<CatchPhiSaveInfo> &save_infos,
                               const CatchPhiSavesByBundle &saves_by_bundle,
                               const CatchPhiFactIndicesByRegister &fact_indices_by_register,
                               const RegisterWritesByBundle &register_writes_by_bundle, CatchPhiSaveState *state,
                               std::vector<size_t> *redundant_saves)
{
    auto candidates_it = saves_by_bundle.find(spill_fill);
    // SpillFill entries are parallel moves. Redundancy must therefore be checked against the state before any
    // destination in this bundle is written.
    if (redundant_saves != nullptr && candidates_it != saves_by_bundle.end()) {
        for (auto info_index : candidates_it->second) {
            if (HasCatchPhiSaveFact(*state, save_infos[info_index].fact_index)) {
                redundant_saves->push_back(info_index);
            }
        }
    }

    // Apply all kills before generating new equalities. In particular, `slot <- src, src <- other` must not leave
    // Fact(slot, src) after the bundle.
    auto writes_it = register_writes_by_bundle.find(spill_fill);
    if (writes_it != register_writes_by_bundle.end()) {
        for (const auto &write : writes_it->second) {
            KillCatchPhiSaveFacts(write.first, fact_indices_by_register, state);
        }
    }

    if (candidates_it == saves_by_bundle.end()) {
        return;
    }

    for (auto info_index : candidates_it->second) {
        const auto &info = save_infos[info_index];
        const auto &fact = facts[info.fact_index];
        size_t slot_writes = 0;
        bool source_is_written = false;
        if (writes_it != register_writes_by_bundle.end()) {
            auto slot_it = writes_it->second.find(fact.slot);
            slot_writes = slot_it == writes_it->second.end() ? 0 : slot_it->second;
            source_is_written = writes_it->second.find(fact.src) != writes_it->second.end();
        }
        if (slot_writes == 1U && !source_is_written) {
            SetCatchPhiSaveFact(state, info.fact_index);
        }
    }
}

void ProcessCatchPhiSaveBlock(BasicBlock *block, const std::vector<CatchPhiSaveFact> &facts,
                              const std::vector<CatchPhiSaveInfo> &save_infos,
                              const CatchPhiSavesByBundle &saves_by_bundle,
                              const CatchPhiFactIndicesByRegister &fact_indices_by_register,
                              const RegisterWritesByBundle &register_writes_by_bundle, CatchPhiSaveState *state,
                              std::vector<size_t> *redundant_saves)
{
    for (auto inst : block->AllInsts()) {
        if (inst->IsSpillFill()) {
            ProcessCatchPhiSaveBundle(inst->CastToSpillFill(), facts, save_infos, saves_by_bundle,
                                      fact_indices_by_register, register_writes_by_bundle, state, redundant_saves);
            continue;
        }
        if (inst->IsPhi() || inst->IsCatchPhi() || inst->IsSaveState() || inst->NoDest()) {
            continue;
        }
        // Bytecode floats also use ordinary virtual registers (see ConvertRegType).
        // A FLOAT64 write invalidates saved equalities just like an integer write.
        for (size_t i = 0; i < inst->GetDstCount(); ++i) {
            auto reg = inst->GetDstReg(i);
            if (IsOrdinaryRegister(reg)) {
                KillCatchPhiSaveFacts(reg, fact_indices_by_register, state);
            }
        }
    }
}

CatchPhiSaveState ComputeCatchPhiSaveInState(BasicBlock *block, const CatchPhiSaveState &empty_state,
                                             const CatchPhiSaveState &all_facts,
                                             const std::vector<CatchPhiSaveState> &out_states)
{
    if (block->IsStartBlock() || block->IsCatchBegin() || block->GetPredsBlocks().empty()) {
        return empty_state;
    }

    auto state = all_facts;
    for (auto pred : block->GetPredsBlocks()) {
        const auto &pred_state = out_states[pred->GetId()];
        for (size_t i = 0; i < state.size(); ++i) {
            state[i] &= pred_state[i];
        }
    }
    return state;
}

}  // namespace

void RegAllocResolver::EliminateRedundantCatchPhiSaves(Graph *graph,
                                                       const ArenaVector<CatchPhiSaveCandidate> &candidates)
{
    if (candidates.empty()) {
        return;
    }

    std::vector<CatchPhiSaveFact> facts;
    std::vector<CatchPhiSaveInfo> save_infos;
    CatchPhiSavesByBundle saves_by_bundle;
    CatchPhiFactIndicesByRegister fact_indices_by_register;
    std::unordered_map<uint32_t, size_t> fact_indices;

    // Revalidate the side-table entries after SplitResolver. It may append moves to an existing bundle, but it must
    // not make an unrelated or shifted entry eligible for CatchPhi elimination.
    for (const auto &candidate : candidates) {
        auto spill_fill = candidate.spill_fill;
        if (spill_fill == nullptr || spill_fill->GetBasicBlock() == nullptr ||
            candidate.spill_fill_index >= spill_fill->GetSpillFills().size()) {
            continue;
        }
        const auto &spill_fill_data = spill_fill->GetSpillFill(candidate.spill_fill_index);
        if (spill_fill_data.SrcType() != LocationType::REGISTER ||
            spill_fill_data.DstType() != LocationType::REGISTER || spill_fill_data.SrcValue() != candidate.src ||
            spill_fill_data.DstValue() != candidate.slot || !IsOrdinaryRegister(candidate.src) ||
            !IsOrdinaryRegister(candidate.slot)) {
            continue;
        }

        auto fact_key = MakeCatchPhiSaveFactKey(candidate.slot, candidate.src);
        auto [fact_it, inserted] = fact_indices.emplace(fact_key, facts.size());
        if (inserted) {
            facts.push_back({candidate.slot, candidate.src});
            fact_indices_by_register[candidate.slot].push_back(fact_it->second);
            if (candidate.src != candidate.slot) {
                fact_indices_by_register[candidate.src].push_back(fact_it->second);
            }
        }
        auto info_index = save_infos.size();
        save_infos.push_back({spill_fill, candidate.spill_fill_index, fact_it->second});
        saves_by_bundle[spill_fill].push_back(info_index);
    }

    if (save_infos.empty()) {
        return;
    }

    // Bound this optional analysis; on exhaustion, retain every CatchPhi save.
    if (facts.size() > MAX_CATCH_PHI_SAVE_FACTS) {
        return;
    }

    const auto &blocks = graph->GetBlocksRPO();
    RegisterWritesByBundle register_writes_by_bundle;
    for (auto block : blocks) {
        for (auto inst : block->AllInsts()) {
            if (!inst->IsSpillFill()) {
                continue;
            }
            auto spill_fill = inst->CastToSpillFill();
            auto &write_counts = register_writes_by_bundle[spill_fill];
            for (const auto &spill_fill_data : spill_fill->GetSpillFills()) {
                if (spill_fill_data.DstType() == LocationType::REGISTER) {
                    ++write_counts[static_cast<Register>(spill_fill_data.DstValue())];
                }
            }
        }
    }

    auto block_count = graph->GetVectorBlocks().size();
    auto state_word_count = (facts.size() + CATCH_PHI_SAVE_STATE_WORD_BITS - 1U) / CATCH_PHI_SAVE_STATE_WORD_BITS;
    if (block_count > MAX_CATCH_PHI_SAVE_STATE_WORDS / state_word_count) {
        return;
    }
    CatchPhiSaveState empty_state(state_word_count, 0);
    CatchPhiSaveState all_facts(state_word_count, std::numeric_limits<uint64_t>::max());
    auto trailing_fact_bits = facts.size() % CATCH_PHI_SAVE_STATE_WORD_BITS;
    if (trailing_fact_bits != 0) {
        all_facts.back() = (1ULL << trailing_fact_bits) - 1U;
    }
    // This is a must analysis. Starting non-entry blocks at TOP lets loop facts converge without unrolling paths;
    // entry and catch-entry blocks are forced to the empty state by ComputeCatchPhiSaveInState.
    std::vector<CatchPhiSaveState> in_states(block_count, all_facts);
    std::vector<CatchPhiSaveState> out_states(block_count, all_facts);
    std::deque<BasicBlock *> worklist;
    std::vector<bool> queued(block_count, false);

    for (auto block : blocks) {
        worklist.push_back(block);
        queued[block->GetId()] = true;
    }

    size_t block_visits = 0;
    while (!worklist.empty()) {
        if (++block_visits > MAX_CATCH_PHI_SAVE_BLOCK_VISITS) {
            return;
        }
        auto block = worklist.front();
        worklist.pop_front();
        queued[block->GetId()] = false;

        auto new_in = ComputeCatchPhiSaveInState(block, empty_state, all_facts, out_states);
        auto new_out = new_in;
        ProcessCatchPhiSaveBlock(block, facts, save_infos, saves_by_bundle, fact_indices_by_register,
                                 register_writes_by_bundle, &new_out, nullptr);
        in_states[block->GetId()] = std::move(new_in);
        if (new_out == out_states[block->GetId()]) {
            continue;
        }
        out_states[block->GetId()] = std::move(new_out);
        for (auto succ : block->GetSuccsBlocks()) {
            if (!queued[succ->GetId()]) {
                worklist.push_back(succ);
                queued[succ->GetId()] = true;
            }
        }
    }

    // Fixed-point iterations above never mutate IR or record transient decisions. Use the stable IN states for one
    // final simulation, then remove entries in reverse index order.
    std::vector<size_t> redundant_saves;
    for (auto block : blocks) {
        auto state = in_states[block->GetId()];
        ProcessCatchPhiSaveBlock(block, facts, save_infos, saves_by_bundle, fact_indices_by_register,
                                 register_writes_by_bundle, &state, &redundant_saves);
    }

    std::unordered_map<SpillFillInst *, std::vector<size_t>> removals;
    for (auto info_index : redundant_saves) {
        const auto &info = save_infos[info_index];
        removals[info.spill_fill].push_back(info.spill_fill_index);
    }
    for (auto &[spill_fill, indices] : removals) {
        std::sort(indices.begin(), indices.end());
        indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
        for (auto it = indices.rbegin(); it != indices.rend(); ++it) {
            spill_fill->RemoveSpillFill(*it);
        }
        if (spill_fill->GetSpillFills().empty()) {
            spill_fill->GetBasicBlock()->RemoveInst(spill_fill);
        }
    }
}

/*
 * For each instruction set destination register if it is assigned,
 * Pop inputs from stack and push result on stack if stack slot is assigned.
 */
void RegAllocResolver::Resolve()
{
    // We use RPO order because we need to calculate Caller's roots mask before its inlined callees
    // (see PropagateCallerMasks).
    for (auto block : GetGraph()->GetBlocksRPO()) {
        for (auto inst : block->AllInstsSafe()) {
            if (inst->IsSaveState()) {
                continue;
            }
            ResolveInputs(inst);
            ResolveOutput(inst);
            if (GetGraph()->IsInstThrowable(inst)) {
                AddCatchPhiMoves(inst);
            }
        }
    }
}

void RegAllocResolver::AddCatchPhiMoves(Inst *inst)
{
    auto spill_fill_inst = GetGraph()->CreateInstSpillFill();
    spill_fill_inst->SetSpillFillType(SpillFillType::INPUT_FILL);
    auto handlers = GetGraph()->GetThrowableInstHandlers(inst);

    for (auto catch_handler : handlers) {
        for (auto catch_inst : catch_handler->AllInsts()) {
            if (!catch_inst->IsCatchPhi() || catch_inst->CastToCatchPhi()->IsAcc()) {
                continue;
            }
            auto catch_phi = catch_inst->CastToCatchPhi();
            const auto &throwable_insts = catch_phi->GetThrowableInsts();
            auto it = std::find(throwable_insts->begin(), throwable_insts->end(), inst);
            if (it == throwable_insts->end()) {
                continue;
            }
            int index = std::distance(throwable_insts->begin(), it);
            auto catch_input = catch_phi->GetDataFlowInput(index);
            auto input_interval = liveness_->GetInstLifeIntervals(catch_input);
            ASSERT(input_interval->GetSibling() == nullptr);
            auto catch_phi_interval = liveness_->GetInstLifeIntervals(catch_phi);
            if (input_interval->GetLocation() != catch_phi_interval->GetLocation()) {
                auto first_new_entry = spill_fill_inst->GetSpillFills().size();
                ConnectIntervals(spill_fill_inst, input_interval, catch_phi_interval);
                if (GetGraph()->IsBytecodeOptimizer()) {
                    for (size_t i = first_new_entry; i < spill_fill_inst->GetSpillFills().size(); ++i) {
                        const auto &spill_fill_data = spill_fill_inst->GetSpillFill(i);
                        if (spill_fill_data.SrcType() == LocationType::REGISTER &&
                            spill_fill_data.DstType() == LocationType::REGISTER) {
                            catch_phi_save_candidates_.push_back({spill_fill_inst, i,
                                                                  static_cast<Register>(spill_fill_data.SrcValue()),
                                                                  static_cast<Register>(spill_fill_data.DstValue())});
                        }
                    }
                }
            }
        }
    }
    if (!spill_fill_inst->GetSpillFills().empty()) {
        inst->InsertBefore(spill_fill_inst);
    }
}

void RegAllocResolver::EliminateRedundantCatchPhiSaves()
{
    EliminateRedundantCatchPhiSaves(GetGraph(), catch_phi_save_candidates_);
    catch_phi_save_candidates_.clear();
}

void RegAllocResolver::ResolveInputs(Inst *inst)
{
    if (inst->IsPhi() || inst->IsCatchPhi()) {
        return;
    }

    // Life-position before instruction to analyze intervals, that were splited directly before it
    auto pre_ins_ln = liveness_->GetInstLifeIntervals(inst)->GetBegin() - 1U;

    for (size_t i = 0; i < inst->GetInputsCount(); i++) {
        auto location = inst->GetLocation(i);
        auto input_interval = liveness_->GetInstLifeIntervals(inst->GetDataFlowInput(i));
        if (CanReadFromAccumulator(inst, i) || input_interval->NoDest() || location.IsInvalid()) {
            continue;
        }

        // Interval with fixed register can be splited before `inst`: we don't need any extra moves in that case,
        // since fixed register can't be overwrite
        auto sibling = input_interval->FindSiblingAt(pre_ins_ln);
        ASSERT(sibling != nullptr);
        if (location.IsFixedRegister() && sibling->GetLocation() == location) {
            continue;
        }

        // Otherwise use sibling covering `inst`
        if (sibling->GetEnd() == pre_ins_ln) {
            sibling = sibling->GetSibling();
        }

        // Input's location required any register: specify the allocated one
        if (location.IsUnallocatedRegister()) {
            ASSERT(sibling->HasReg());
            inst->SetLocation(i, sibling->GetLocation());
            continue;
        }

        // Finally, if input's location is not equal to the required one, add spill-fill
        if (sibling->GetLocation() != location) {
            AddMoveToFixedLocation(inst, sibling->GetLocation(), i);
        }
    }
}

void RegAllocResolver::AddMoveToFixedLocation(Inst *inst, Location input_location, size_t input_num)
{
    // Create or get existing SpillFillInst
    SpillFillInst *sf_inst {};
    if (inst->GetPrev() != nullptr && inst->GetPrev()->IsSpillFill()) {
        sf_inst = inst->GetPrev()->CastToSpillFill();
    } else {
        sf_inst = GetGraph()->CreateInstSpillFill();
        sf_inst->SetSpillFillType(SpillFillType::INPUT_FILL);
        inst->InsertBefore(sf_inst);
    }

    // Add move from input to fixed location
    auto type = ConvertRegType(GetGraph(), inst->GetInputType(input_num));
    auto fixed_location = inst->GetLocation(input_num);
    if (fixed_location.IsFixedRegister()) {
        GetGraph()->SetRegUsage(fixed_location.GetValue(), type);
    }
    sf_inst->AddSpillFill(input_location, fixed_location, type);
}

Inst *GetFirstUserOrInst(Inst *inst)
{
    if (!inst->GetUsers().Empty()) {
        return inst->GetFirstUser()->GetInst();
    }
    return inst;
}

// For implicit null check we need to find the first null check's user to
// correctly capture SaveState's input locations, because implicit null checks are fired
// when its input is accessed by its users (for example, when LoadArray instruction is loading
// value from null array reference). Some life intervals may change its location (due to spilling)
// between NullCheck and its users, so locations captured at implicit null check could be incorrect.
// While implicit NullCheck may have multiple users we can use only a user dominating all other users,
// because null check either will be fired at it, or won't be fired at all.
Inst *GetExplicitUser(Inst *inst)
{
    if (inst->GetUsers().Empty()) {
        return inst;
    }
    if (inst->HasSingleUser()) {
        return inst->GetUsers().Front().GetInst();
    }

    Inst *user_inst {nullptr};
#ifndef NDEBUG
    for (auto &user : inst->GetUsers()) {
        if (user.GetInst()->IsPhi()) {
            continue;
        }
        ASSERT(user_inst != nullptr && user_inst->IsDominate(user.GetInst()));
    }
#endif
    return user_inst;
}

void RegAllocResolver::PropagateCallerMasks(SaveStateInst *save_state)
{
    save_state->CreateRootsStackMask(GetGraph()->GetAllocator());
    auto user = GetExplicitUser(GetFirstUserOrInst(save_state));
    // Get location of save state inputs at the save state user (note that at this point
    // all inputs will have the same location at all users (excluding ReturnInlined that should be skipped)).
    FillSaveStateRootsMask(save_state, user, save_state);
}

void RegAllocResolver::FillSaveStateRootsMask(SaveStateInst *save_state, Inst *user, SaveStateInst *target_ss)
{
    auto dst_ln = liveness_->GetInstLifeIntervals(user)->GetBegin();

    for (size_t i = 0; i < save_state->GetInputsCount(); ++i) {
        auto input_inst = save_state->GetDataFlowInput(i);
        if (input_inst->IsConst() || !IsTypeCollectable(input_inst->GetType())) {
            continue;
        }
        auto input_interval = liveness_->GetInstLifeIntervals(input_inst);
        auto sibling = input_interval->FindSiblingAt(dst_ln);
        ASSERT(sibling != nullptr);
        if (!sibling->SplitCover(dst_ln)) {
            continue;
        }
        AddLocationToRoots(sibling->GetLocation(), target_ss, GetGraph());
#ifndef NDEBUG
        for (auto &test_user : target_ss->GetUsers()) {
            if (test_user.GetInst()->GetId() == user->GetId()) {
                continue;
            }
            auto explicit_test_user = GetExplicitUser(test_user.GetInst());
            auto udst_ln = liveness_->GetInstLifeIntervals(explicit_test_user)->GetBegin();
            ASSERT(sibling->GetLocation() == input_interval->FindSiblingAt(udst_ln)->GetLocation());
        }
#endif
    }
}

/*
 * Pop output on stack from reserved register
 */
void RegAllocResolver::ResolveOutput(Inst *inst)
{
    // Multi-output instructions' dst registers will be filled after procecssing theirs pseudo users
    if (inst->GetLinearNumber() == INVALID_LINEAR_NUM || inst->GetDstCount() > 1) {
        return;
    }

    if (CanStoreToAccumulator(inst)) {
        return;
    }

    auto inst_interval = liveness_->GetInstLifeIntervals(inst);
    if (inst_interval->NoDest()) {
        inst->SetDstReg(INVALID_REG);
        return;
    }

    if (inst->GetOpcode() == Opcode::Parameter) {
        inst->CastToParameter()->GetLocationData().SetDst(inst_interval->GetLocation());
    }
    // Process multi-output inst
    size_t dst_mum = inst->GetSrcRegIndex();
    // Wrtie dst
    auto reg_type = inst_interval->GetType();
    if (inst_interval->HasReg()) {
        auto reg = inst_interval->GetReg();
        inst->SetDstReg(dst_mum, reg);
        GetGraph()->SetRegUsage(reg, reg_type);
    } else {
        ASSERT(inst->IsConst() || inst->IsPhi() || inst->IsParameter());
    }
}

bool RegAllocResolver::ResolveCatchPhis()
{
    for (auto block : GetGraph()->GetBlocksRPO()) {
        if (!block->IsCatchBegin()) {
            continue;
        }
        for (auto inst : block->AllInstsSafe()) {
            if (!inst->IsCatchPhi()) {
                break;
            }
            if (inst->CastToCatchPhi()->IsAcc()) {
                continue;
            }
            // This is the case when all throwable instructions were removed from the try-block,
            // so that catch-handler is unreachable
            if (inst->GetInputs().Empty()) {
                return false;
            }
            auto new_catch_phi = SqueezeCatchPhiInputs(inst->CastToCatchPhi());
            if (new_catch_phi != nullptr) {
                inst->ReplaceUsers(new_catch_phi);
                block->RemoveInst(inst);
            }
        }
    }
    return true;
}

/**
 * Try to remove catch phi's inputs:
 * If the input's corresponding throwable instruction dominates other throwable inst, we can remove other equal catch
 * phi's input
 *
 * CatchPhi(v1, v1, v1, v2, v2, v2) -> CatchPhi(v1, v2)
 *
 * Return nullptr if inputs count was not reduced.
 */
Inst *RegAllocResolver::SqueezeCatchPhiInputs(CatchPhiInst *catch_phi)
{
    bool inputs_are_identical = true;
    auto first_input = catch_phi->GetInput(0).GetInst();
    for (size_t i = 1; i < catch_phi->GetInputsCount(); ++i) {
        if (catch_phi->GetInput(i).GetInst() != first_input) {
            inputs_are_identical = false;
            break;
        }
    }
    if (inputs_are_identical) {
        return first_input;
    }

    // Create a new one and fill it with the necessary inputs
    auto new_catch_phi = GetGraph()->CreateInstCatchPhi(catch_phi->GetType(), catch_phi->GetPc());
    ASSERT(catch_phi->GetBasicBlock()->GetFirstInst()->IsCatchPhi());
    catch_phi->GetBasicBlock()->PrependInst(new_catch_phi);
    for (size_t i = 0; i < catch_phi->GetInputsCount(); i++) {
        auto input_inst = catch_phi->GetInput(i).GetInst();
        auto current_throwable_inst = catch_phi->GetThrowableInst(i);
        ASSERT(GetGraph()->IsInstThrowable(current_throwable_inst));
        bool skip = false;
        for (size_t j = 0; j < new_catch_phi->GetInputsCount(); j++) {
            auto saved_inst = new_catch_phi->GetInput(j).GetInst();
            if (saved_inst != input_inst) {
                continue;
            }
            auto saved_throwable_inst = new_catch_phi->GetThrowableInst(j);
            if (saved_throwable_inst->IsDominate(current_throwable_inst)) {
                skip = true;
            }
            if (current_throwable_inst->IsDominate(saved_throwable_inst)) {
                new_catch_phi->ReplaceThrowableInst(saved_throwable_inst, current_throwable_inst);
                skip = true;
            }
            if (skip) {
                break;
            }
        }
        if (!skip) {
            new_catch_phi->AppendInput(input_inst);
            new_catch_phi->AppendThrowableInst(current_throwable_inst);
        }
    }
    if (new_catch_phi->GetInputsCount() == catch_phi->GetInputsCount()) {
        new_catch_phi->GetBasicBlock()->RemoveInst(new_catch_phi);
        return nullptr;
    }
    return new_catch_phi;
}

}  // namespace panda::compiler
