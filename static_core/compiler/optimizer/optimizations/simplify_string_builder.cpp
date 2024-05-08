/**
 * Copyright (c) 2023-2024 Huawei Device Co., Ltd.
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

#include "simplify_string_builder.h"

#include "compiler_logger.h"

#include "optimizer/analysis/alias_analysis.h"
#include "optimizer/analysis/bounds_analysis.h"
#include "optimizer/analysis/dominators_tree.h"
#include "optimizer/ir/analysis.h"
#include "optimizer/ir/inst.h"

#include "optimizer/optimizations/cleanup.h"
#include "optimizer/optimizations/string_builder_utils.h"

namespace ark::compiler {

constexpr size_t ARG_IDX_0 = 0;
constexpr size_t ARG_IDX_1 = 1;
constexpr size_t ARG_IDX_2 = 2;
constexpr size_t ARG_IDX_3 = 3;

SimplifyStringBuilder::SimplifyStringBuilder(Graph *graph)
    : Optimization(graph),
      instructionsStack_ {graph->GetLocalAllocator()->Adapter()},
      instructionsVector_ {graph->GetLocalAllocator()->Adapter()},
      inputDescriptors_ {graph->GetLocalAllocator()->Adapter()},
      usages_ {graph->GetLocalAllocator()->Adapter()},
      matches_ {graph->GetLocalAllocator()->Adapter()}
{
}

bool HasTryCatchBlocks(Graph *graph)
{
    for (auto block : graph->GetBlocksRPO()) {
        if (block->IsTryCatch()) {
            return true;
        }
    }
    return false;
}

bool SimplifyStringBuilder::RunImpl()
{
    isApplied_ = false;

    if (!GetGraph()->IsAnalysisValid<DominatorsTree>()) {
        GetGraph()->RunPass<DominatorsTree>();
    }

    ASSERT(GetGraph()->GetRootLoop() != nullptr);

    // Loops with try-catch block and OSR mode are not supported in current implementation
    if (!HasTryCatchBlocks(GetGraph()) && !GetGraph()->IsOsrMode()) {
        for (auto loop : GetGraph()->GetRootLoop()->GetInnerLoops()) {
            OptimizeStringConcatenation(loop);
        }
    }

    for (auto block : GetGraph()->GetBlocksRPO()) {
        if (block->IsEmpty()) {
            continue;
        }
        OptimizeStringBuilderToString(block);
        OptimizeStringConcatenation(block);
    }

    COMPILER_LOG(DEBUG, SIMPLIFY_SB) << "Simplify StringBuilder complete";

    // Remove save state inserted in loop post exit block at IR builder
    for (auto block : GetGraph()->GetBlocksRPO()) {
        for (auto inst : block->Insts()) {
            if (inst->GetOpcode() != Opcode::SaveState) {
                continue;
            }
            inst->ClearFlag(inst_flags::NO_DCE);
        }
    }

    // Cleanup should be done inside pass, to satisfy GraphChecker
    GetGraph()->RunPass<compiler::Cleanup>();

    return isApplied_;
}

void SimplifyStringBuilder::InvalidateAnalyses()
{
    GetGraph()->InvalidateAnalysis<BoundsAnalysis>();
    GetGraph()->InvalidateAnalysis<AliasAnalysis>();
}

InstIter SimplifyStringBuilder::SkipToStringBuilderConstructorWithStringArg(InstIter begin, InstIter end)
{
    return std::find_if(std::move(begin), std::move(end),
                        [](auto inst) { return IsMethodStringBuilderConstructorWithStringArg(inst); });
}

bool IsDataFlowInput(Inst *inst, Inst *input)
{
    for (size_t i = 0; i < inst->GetInputsCount(); ++i) {
        if (inst->GetDataFlowInput(i) == input) {
            return true;
        }
    }
    return false;
}

bool IsUsedOutsideBasicBlock(Inst *inst, BasicBlock *bb)
{
    for (auto &user : inst->GetUsers()) {
        auto userInst = user.GetInst();
        if (userInst->IsCheck()) {
            if (!userInst->HasSingleUser()) {
                // In case of multi user check-instruction we assume it is used outside current basic block without
                // actually testing it.
                return true;
            }
            // In case of single user check-instruction we test its the only user.
            userInst = userInst->GetUsers().Front().GetInst();
        }
        if (userInst->GetBasicBlock() != bb) {
            return true;
        }
    }
    return false;
}

void SimplifyStringBuilder::OptimizeStringBuilderToString(BasicBlock *block)
{
    // Removes unnecessary String Builder instances

    ASSERT(block != nullptr);
    ASSERT(block->GetGraph() == GetGraph());

    // Walk through a basic block, find every StringBuilder instance and constructor call,
    // and check it we can remove/replace them
    InstIter inst = block->Insts().begin();
    while ((inst = SkipToStringBuilderConstructorWithStringArg(inst, block->Insts().end())) != block->Insts().end()) {
        ASSERT((*inst)->IsStaticCall());
        auto ctorCall = (*inst)->CastToCallStatic();

        // void StringBuilder::<ctor> instance, arg, save_state
        ASSERT(ctorCall->GetInputsCount() == ARGS_NUM_3);
        auto instance = ctorCall->GetInput(0).GetInst();
        auto arg = ctorCall->GetInput(1).GetInst();

        // Look for StringBuilder usages within current basic block
        auto nextInst = block->Insts().end();
        bool removeInstance = true;
        for (++inst; inst != block->Insts().end(); ++inst) {
            // Skip SaveState instructions
            if ((*inst)->IsSaveState()) {
                continue;
            }

            // Skip check instructions, like NullCheck, RefTypeCheck, etc.
            if ((*inst)->IsCheck()) {
                continue;
            }

            // Continue (outer loop) with the next StringBuilder constructor,
            // in case we met one in inner loop
            if (IsMethodStringBuilderConstructorWithStringArg(*inst)) {
                nextInst = nextInst != block->Insts().end() ? nextInst : inst;
            }

            if (!IsDataFlowInput(*inst, instance)) {
                continue;
            }

            // Process usages of StringBuilder instance:
            // replace toString()-calls until we met something else
            if (IsStringBuilderToString(*inst)) {
                (*inst)->ReplaceUsers(arg);
                (*inst)->ClearFlag(compiler::inst_flags::NO_DCE);
                COMPILER_LOG(DEBUG, SIMPLIFY_SB)
                    << "Remove StringBuilder toString()-call (id=" << (*inst)->GetId() << ")";
                isApplied_ = true;
            } else {
                removeInstance = false;
                break;
            }
        }

        // Remove StringBuilder instance unless it has usages
        if (removeInstance && !IsUsedOutsideBasicBlock(instance, instance->GetBasicBlock())) {
            RemoveStringBuilderInstance(instance);
            isApplied_ = true;
        }

        // Proceed to the next StringBuilder constructor
        inst = nextInst != block->Insts().end() ? nextInst : inst;
    }
}

InstIter SimplifyStringBuilder::SkipToStringBuilderDefaultConstructor(InstIter begin, InstIter end)
{
    return std::find_if(std::move(begin), std::move(end),
                        [](auto inst) { return IsMethodStringBuilderDefaultConstructor(inst); });
}

IntrinsicInst *SimplifyStringBuilder::CreateConcatIntrinsic(Inst *lhs, Inst *rhs, DataType::Type type,
                                                            SaveStateInst *saveState)
{
    auto concatIntrinsic =
        GetGraph()->CreateInstIntrinsic(GetGraph()->GetRuntime()->GetStringBuilderConcatStringsIntrinsicId());
    ASSERT(concatIntrinsic->RequireState());

    concatIntrinsic->SetType(type);
    auto saveStateClone = CopySaveState(GetGraph(), saveState);
    concatIntrinsic->SetInputs(
        GetGraph()->GetAllocator(),
        {{lhs, lhs->GetType()}, {rhs, rhs->GetType()}, {saveStateClone, saveStateClone->GetType()}});

    return concatIntrinsic;
}

bool SimplifyStringBuilder::IsIntrinsicStringBuilderAppendString(Inst *inst) const
{
    if (!inst->IsIntrinsic()) {
        return false;
    }

    auto runtime = GetGraph()->GetRuntime();
    return runtime->IsIntrinsicStringBuilderAppendString(inst->CastToIntrinsic()->GetIntrinsicId());
}

bool SimplifyStringBuilder::MatchConcatenation(InstIter &begin, const InstIter &end, ConcatenationMatch &match)
{
    // Walk instruction range [begin, end) and fill the match structure with StringBuilder usage instructions found

    auto instance = match.instance;

    if (IsUsedOutsideBasicBlock(instance, instance->GetBasicBlock())) {
        return false;
    }

    int toStringCallsCount = 0;
    for (; begin != end; ++begin) {
        if ((*begin)->IsSaveState()) {
            continue;
        }

        // Skip instruction having nothing to do with current instance
        if (!IsDataFlowInput(*begin, instance)) {
            continue;
        }

        // Walk through NullChecks
        auto inst = *begin;
        if (inst->IsNullCheck()) {
            if (!inst->HasSingleUser()) {
                return false;  // Unsupported case: doesn't look like concatenation pattern
            }
            inst = inst->GetUsers().Front().GetInst();
            if (inst->GetBasicBlock() != instance->GetBasicBlock()) {
                return false;  // Unsupported case: doesn't look like concatenation pattern
            }
            continue;
        }

        if (IsIntrinsicStringBuilderAppendString(inst)) {
            if (match.appendCount >= match.appendIntrinsics.size()) {
                return false;  // Unsupported case: too many arguments concatenated
            }
            auto intrinsic = inst->CastToIntrinsic();
            match.appendIntrinsics[match.appendCount++] = intrinsic;
        } else if (IsStringBuilderToString(inst)) {
            toStringCallsCount++;
            match.toStringCall = *begin;
        } else {
            break;
        }
    }

    // Supported case: number of toString-calls is one,
    // number of append calls is between 2 and 4
    return toStringCallsCount == 1 && match.appendCount > 1;
}

void SimplifyStringBuilder::FixBrokenSaveStates(Inst *source, Inst *target)
{
    if (source->IsMovableObject()) {
        ssb_.SearchAndCreateMissingObjInSaveState(GetGraph(), source, target);
    }
}

void SimplifyStringBuilder::Check(const ConcatenationMatch &match)
{
    [[maybe_unused]] auto &appendIntrinsics = match.appendIntrinsics;
    ASSERT(match.appendCount > 1);
    ASSERT(appendIntrinsics[ARG_IDX_0] != nullptr);
    ASSERT(appendIntrinsics[ARG_IDX_0]->GetInputsCount() > 1);
    ASSERT(appendIntrinsics[ARG_IDX_1] != nullptr);
    ASSERT(appendIntrinsics[ARG_IDX_1]->GetInputsCount() > 1);

    switch (match.appendCount) {
        case ARGS_NUM_2:
            break;
        case ARGS_NUM_3: {
            ASSERT(appendIntrinsics[ARG_IDX_2] != nullptr);
            ASSERT(appendIntrinsics[ARG_IDX_2]->GetInputsCount() > 1);
            break;
        }
        case ARGS_NUM_4: {
            ASSERT(appendIntrinsics[ARG_IDX_2] != nullptr);
            ASSERT(appendIntrinsics[ARG_IDX_2]->GetInputsCount() > 1);
            ASSERT(appendIntrinsics[ARG_IDX_3] != nullptr);
            ASSERT(appendIntrinsics[ARG_IDX_3]->GetInputsCount() > 1);
            break;
        }
        default:
            UNREACHABLE();
    }
}

void SimplifyStringBuilder::InsertIntrinsicAndFixSaveStates(IntrinsicInst *concatIntrinsic, Inst *lhs, Inst *rhs,
                                                            Inst *before)
{
    InsertBeforeWithSaveState(concatIntrinsic, before);
    FixBrokenSaveStates(lhs, concatIntrinsic);
    FixBrokenSaveStates(rhs, concatIntrinsic);
}

void SimplifyStringBuilder::ReplaceWithIntrinsic(const ConcatenationMatch &match)
{
    auto &appendIntrinsics = match.appendIntrinsics;
    auto arg0 = appendIntrinsics[ARG_IDX_0]->GetInput(1).GetInst();
    auto arg1 = appendIntrinsics[ARG_IDX_1]->GetInput(1).GetInst();
    auto toStringCall = match.toStringCall;
    auto concat01 = CreateConcatIntrinsic(arg0, arg1, toStringCall->GetType(), toStringCall->GetSaveState());
    COMPILER_LOG(DEBUG, SIMPLIFY_SB) << "Replace StringBuilder append-intrinsics (id="
                                     << appendIntrinsics[ARG_IDX_0]->GetId()
                                     << " and id=" << appendIntrinsics[ARG_IDX_1]->GetId()
                                     << ") with concat intrinsic (id=" << concat01->GetId() << ")";
    switch (match.appendCount) {
        case ARGS_NUM_2: {
            InsertIntrinsicAndFixSaveStates(concat01, arg0, arg1, toStringCall);
            toStringCall->ReplaceUsers(concat01);
            break;
        }
        case ARGS_NUM_3: {
            auto arg2 = appendIntrinsics[ARG_IDX_2]->GetInput(1).GetInst();
            auto concat012 =
                CreateConcatIntrinsic(concat01, arg2, toStringCall->GetType(), toStringCall->GetSaveState());
            COMPILER_LOG(DEBUG, SIMPLIFY_SB)
                << "Replace StringBuilder append-intrinsic (id=" << appendIntrinsics[ARG_IDX_2]->GetId()
                << ") with concat intrinsic (id=" << concat012->GetId() << ")";
            InsertIntrinsicAndFixSaveStates(concat01, arg0, arg1, toStringCall);
            InsertIntrinsicAndFixSaveStates(concat012, concat01, arg2, toStringCall);
            toStringCall->ReplaceUsers(concat012);
            break;
        }
        case ARGS_NUM_4: {
            auto arg2 = appendIntrinsics[ARG_IDX_2]->GetInput(1).GetInst();
            auto arg3 = appendIntrinsics[ARG_IDX_3]->GetInput(1).GetInst();
            auto concat23 = CreateConcatIntrinsic(arg2, arg3, toStringCall->GetType(), toStringCall->GetSaveState());
            auto concat0123 =
                CreateConcatIntrinsic(concat01, concat23, toStringCall->GetType(), toStringCall->GetSaveState());
            COMPILER_LOG(DEBUG, SIMPLIFY_SB)
                << "Replace StringBuilder append-intrinsics (id=" << appendIntrinsics[ARG_IDX_2]->GetId()
                << " and id=" << appendIntrinsics[ARG_IDX_3]->GetId()
                << ") with concat intrinsic (id=" << concat23->GetId() << ")";
            InsertIntrinsicAndFixSaveStates(concat01, arg0, arg1, toStringCall);
            InsertIntrinsicAndFixSaveStates(concat23, arg2, arg3, toStringCall);
            InsertIntrinsicAndFixSaveStates(concat0123, concat01, concat23, toStringCall);
            toStringCall->ReplaceUsers(concat0123);
            break;
        }
        default:
            UNREACHABLE();
    }
}

void SimplifyStringBuilder::Cleanup(const ConcatenationMatch &match)
{
    RemoveStringBuilderInstance(match.instance);
}

void SimplifyStringBuilder::OptimizeStringConcatenation(BasicBlock *block)
{
    // Replace String Builder usage with string concatenation whenever optimal

    ASSERT(block != nullptr);
    ASSERT(block->GetGraph() == GetGraph());

    MarkerHolder visitedMarker {GetGraph()};
    Marker visited = visitedMarker.GetMarker();

    bool isAppliedLocal;
    do {
        isAppliedLocal = false;

        // Walk through a basic block, find every String concatenation pattern,
        // and check if we can optimize them
        InstIter inst = block->Insts().begin();
        while ((inst = SkipToStringBuilderDefaultConstructor(inst, block->Insts().end())) != block->Insts().end()) {
            ASSERT((*inst)->IsStaticCall());
            auto ctorCall = (*inst)->CastToCallStatic();

            ASSERT(ctorCall->GetInputsCount() > 0);
            auto instance = ctorCall->GetInput(0).GetInst();

            ++inst;
            if (instance->IsMarked(visited)) {
                continue;
            }

            ConcatenationMatch match {instance, ctorCall};
            // Check if current StringBuilder instance can be optimized
            if (MatchConcatenation(inst, block->Insts().end(), match)) {
                Check(match);
                ReplaceWithIntrinsic(match);
                Cleanup(match);

                instance->SetMarker(visited);

                isAppliedLocal = true;
                isApplied_ = true;
            }
        }
    } while (isAppliedLocal);
}

void SimplifyStringBuilder::ConcatenationLoopMatch::TemporaryInstructions::Clear()
{
    intermediateValue = nullptr;
    toStringCall = nullptr;
    instance = nullptr;
    ctorCall = nullptr;
    appendAccValue = nullptr;
}

bool SimplifyStringBuilder::ConcatenationLoopMatch::TemporaryInstructions::IsEmpty() const
{
    return toStringCall == nullptr || intermediateValue == nullptr || instance == nullptr || ctorCall == nullptr ||
           appendAccValue == nullptr;
}

void SimplifyStringBuilder::ConcatenationLoopMatch::Clear()
{
    block = nullptr;
    accValue = nullptr;
    initialValue = nullptr;

    preheader.instance = nullptr;
    preheader.ctorCall = nullptr;
    preheader.appendAccValue = nullptr;

    loop.appendInstructions.clear();
    temp.clear();
    exit.toStringCall = nullptr;
}

bool SimplifyStringBuilder::HasAppendUsersOnly(Inst *inst) const
{
    MarkerHolder visited {inst->GetBasicBlock()->GetGraph()};
    bool found = HasUserPhiRecursively(inst, visited.GetMarker(), [inst](auto &user) {
        bool sameLoop = user.GetInst()->GetBasicBlock()->GetLoop() == inst->GetBasicBlock()->GetLoop();
        bool isSaveState = user.GetInst()->IsSaveState();
        bool isPhi = user.GetInst()->IsPhi();
        bool isAppendInstruction = IsStringBuilderAppend(user.GetInst());
        return sameLoop && !isSaveState && !isPhi && !isAppendInstruction;
    });
    ResetUserMarkersRecursively(inst, visited.GetMarker());
    return !found;
}

bool IsCheckCastWithoutUsers(Inst *inst)
{
    return inst->GetOpcode() == Opcode::CheckCast && !inst->HasUsers();
}

bool SimplifyStringBuilder::HasPhiOrAppendUsersOnly(Inst *inst, Marker appendInstructionVisited) const
{
    MarkerHolder phiVisited {GetGraph()};
    bool found = HasUserPhiRecursively(
        inst, phiVisited.GetMarker(), [loop = inst->GetBasicBlock()->GetLoop(), appendInstructionVisited](auto &user) {
            bool sameLoop = user.GetInst()->GetBasicBlock()->GetLoop() == loop;
            bool isSaveState = user.GetInst()->IsSaveState();
            bool isCheckCast = IsCheckCastWithoutUsers(user.GetInst());
            bool isPhi = user.GetInst()->IsPhi();
            bool isVisited = user.GetInst()->IsMarked(appendInstructionVisited);
            bool isAppendInstruction = IsStringBuilderAppend(user.GetInst());
            return sameLoop && !isSaveState && !isCheckCast && !isPhi && !(isAppendInstruction && isVisited);
        });
    return !found;
}

bool SimplifyStringBuilder::ConcatenationLoopMatch::IsInstanceHoistable() const
{
    if (block == nullptr || accValue == nullptr || initialValue == nullptr) {
        return false;
    }

    if (preheader.instance == nullptr || preheader.ctorCall == nullptr || preheader.appendAccValue == nullptr) {
        return false;
    }

    if (block != preheader.instance->GetBasicBlock() || block != preheader.ctorCall->GetBasicBlock() ||
        block != preheader.appendAccValue->GetBasicBlock()) {
        return false;
    }

    if ((block->IsTry() || block->IsCatch()) && block->GetTryId() != block->GetLoop()->GetPreHeader()->GetTryId()) {
        return false;
    }

    if (loop.appendInstructions.empty()) {
        return false;
    }

    if (exit.toStringCall == nullptr) {
        return false;
    }

    return true;
}

bool SimplifyStringBuilder::IsInstanceHoistable(const ConcatenationLoopMatch &match) const
{
    return match.IsInstanceHoistable() && HasAppendUsersOnly(match.accValue);
}

bool SimplifyStringBuilder::IsToStringHoistable(const ConcatenationLoopMatch &match,
                                                Marker appendInstructionVisited) const
{
    return HasPhiOrAppendUsersOnly(match.exit.toStringCall, appendInstructionVisited);
}

SaveStateInst *FindPreHeaderSaveState(Loop *loop)
{
    for (const auto &inst : loop->GetPreHeader()->InstsReverse()) {
        if (inst->GetOpcode() == Opcode::SaveState) {
            return inst->CastToSaveState();
        }
    }
    return nullptr;
}

SaveStateInst *FindFirstSaveState(BasicBlock *block)
{
    if (block->IsEmpty()) {
        return nullptr;
    }

    for (auto inst : block->Insts()) {
        if (inst->GetOpcode() == Opcode::SaveState) {
            return inst->CastToSaveState();
        }
    }

    return nullptr;
}

size_t CountOuterLoopSuccs(BasicBlock *block)
{
    return std::count_if(block->GetSuccsBlocks().begin(), block->GetSuccsBlocks().end(),
                         [block](auto succ) { return succ->GetLoop() == block->GetLoop()->GetOuterLoop(); });
}

BasicBlock *GetOuterLoopSucc(BasicBlock *block)
{
    auto found = std::find_if(block->GetSuccsBlocks().begin(), block->GetSuccsBlocks().end(),
                              [block](auto succ) { return succ->GetLoop() == block->GetLoop()->GetOuterLoop(); });
    return found != block->GetSuccsBlocks().end() ? *found : nullptr;
}

BasicBlock *GetLoopPostExit(Loop *loop)
{
    // Find a block immediately following after loop
    // Supported case:
    //  1. Preheader block exists
    //  2. Loop has exactly one block with exactly one successor outside a loop (post exit block)
    //  3. Preheader dominates post exit block found
    // Loop structures different from the one described above are not supported in current implementation

    BasicBlock *postExit = nullptr;
    for (auto block : loop->GetBlocks()) {
        size_t count = CountOuterLoopSuccs(block);
        if (count == 0) {
            continue;
        }
        if (count == 1 && postExit == nullptr) {
            postExit = GetOuterLoopSucc(block);
            continue;
        }
        // Unsupported case
        return nullptr;
    }

    // Supported case
    if (postExit != nullptr && postExit->GetPredsBlocks().size() == 1 && loop->GetPreHeader() != nullptr &&
        loop->GetPreHeader()->IsDominate(postExit)) {
        return postExit;
    }

    // Unsupported case
    return nullptr;
}

IntrinsicInst *SimplifyStringBuilder::CreateIntrinsicStringBuilderAppendString(Inst *instance, Inst *arg,
                                                                               SaveStateInst *saveState)
{
    auto appendIntrinsic = GetGraph()->CreateInstIntrinsic(
        GetGraph()->GetRuntime()->ConvertTypeToStringBuilderAppendIntrinsicId(DataType::REFERENCE));
    ASSERT(appendIntrinsic->RequireState());

    appendIntrinsic->SetType(instance->GetType());
    appendIntrinsic->SetInputs(
        GetGraph()->GetAllocator(),
        {{instance, instance->GetType()}, {arg, arg->GetType()}, {saveState, saveState->GetType()}});

    return appendIntrinsic;
}

void SimplifyStringBuilder::NormalizeStringBuilderAppendInstructionUsers(Inst *instance, SaveStateInst *saveState)
{
    [[maybe_unused]] Inst *ctorCall = nullptr;

    for (auto &user : instance->GetUsers()) {
        auto userInst = SkipSingleUserCheckInstruction(user.GetInst());
        // Make additional append-call out of constructor argument (if present)
        if (IsMethodStringBuilderConstructorWithStringArg(userInst)) {
            ASSERT(ctorCall == nullptr);
            ctorCall = userInst;
            auto ctorArg = ctorCall->GetInput(1).GetInst();
            CreateIntrinsicStringBuilderAppendString(instance, ctorArg, saveState);
        } else if (IsMethodStringBuilderDefaultConstructor(userInst)) {
            ASSERT(ctorCall == nullptr);
            ctorCall = userInst;
        } else if (IsStringBuilderAppend(userInst)) {
            // StringBuilder append-call returns 'this' (instance)
            // Replace all users of append-call by instance for simplicity
            userInst->ReplaceUsers(instance);
        }
    }
    ASSERT(ctorCall != nullptr);
}

ArenaVector<Inst *> SimplifyStringBuilder::FindStringBuilderAppendInstructions(Inst *instance)
{
    ArenaVector<Inst *> appendInstructions {GetGraph()->GetAllocator()->Adapter()};
    for (auto &user : instance->GetUsers()) {
        auto userInst = SkipSingleUserCheckInstruction(user.GetInst());
        if (IsStringBuilderAppend(userInst)) {
            appendInstructions.push_back(userInst);
        }
    }

    return appendInstructions;
}

void RemoveFromInstructionInputs(ArenaVector<std::pair<Inst *, size_t>> &inputDescriptors)
{
    // Inputs must be walked in reverse order for removal
    std::sort(inputDescriptors.begin(), inputDescriptors.end(),
              [](auto inputDescX, auto inputDescY) { return inputDescX.second > inputDescY.second; });

    for (auto inputDesc : inputDescriptors) {
        auto inst = inputDesc.first;
        auto index = inputDesc.second;

        [[maybe_unused]] auto inputInst = inst->GetInput(index).GetInst();
        COMPILER_LOG(DEBUG, SIMPLIFY_SB) << "Remove input id=" << inputInst->GetId() << " ("
                                         << GetOpcodeString(inputInst->GetOpcode())
                                         << ") from instruction id=" << inst->GetId() << " ("
                                         << GetOpcodeString(inst->GetOpcode()) << ")";

        inst->RemoveInput(index);
    }
}

void SimplifyStringBuilder::RemoveFromSaveStateInputs(Inst *inst)
{
    inputDescriptors_.clear();

    for (auto &user : inst->GetUsers()) {
        if (!user.GetInst()->IsSaveState()) {
            continue;
        }
        inputDescriptors_.emplace_back(user.GetInst(), user.GetIndex());
    }

    RemoveFromInstructionInputs(inputDescriptors_);
}

void SimplifyStringBuilder::RemoveFromAllExceptPhiInputs(Inst *inst)
{
    inputDescriptors_.clear();

    for (auto &user : inst->GetUsers()) {
        if (user.GetInst()->IsPhi()) {
            continue;
        }
        inputDescriptors_.emplace_back(user.GetInst(), user.GetIndex());
    }

    RemoveFromInstructionInputs(inputDescriptors_);
}

void SimplifyStringBuilder::RemoveStringBuilderInstance(Inst *instance)
{
    ASSERT(!HasUser(instance, [](auto &user) {
        auto userInst = SkipSingleUserCheckInstruction(user.GetInst());
        auto hasUsers = userInst->HasUsers();
        auto isSaveState = userInst->IsSaveState();
        auto isCtorCall = IsMethodStringBuilderDefaultConstructor(userInst) ||
                          IsMethodStringBuilderConstructorWithStringArg(userInst) ||
                          IsMethodStringBuilderConstructorWithCharArrayArg(userInst);
        auto isAppendInstruction = IsStringBuilderAppend(userInst);
        auto isToStringCall = IsStringBuilderToString(userInst);
        return !(isSaveState || isCtorCall || ((isAppendInstruction || isToStringCall) && !hasUsers));
    }));

    RemoveFromSaveStateInputs(instance);

    for (auto &user : instance->GetUsers()) {
        auto userInst = user.GetInst();
        if (userInst->IsCheck()) {
            auto checkUserInst = SkipSingleUserCheckInstruction(userInst);
            checkUserInst->GetBasicBlock()->RemoveInst(checkUserInst);
        }

        userInst->GetBasicBlock()->RemoveInst(userInst);
        COMPILER_LOG(DEBUG, SIMPLIFY_SB) << "Remove StringBuilder user instruction (id=" << userInst->GetId() << ")";
    }

    ASSERT(!instance->HasUsers());
    instance->GetBasicBlock()->RemoveInst(instance);
    COMPILER_LOG(DEBUG, SIMPLIFY_SB) << "Remove StringBuilder instance (id=" << instance->GetId() << ")";
}

void SimplifyStringBuilder::ReconnectStringBuilderCascade(Inst *instance, Inst *inputInst, Inst *appendInstruction,
                                                          SaveStateInst *saveState)
{
    // Reconnect all append-calls of input_instance to instance instruction

    // Check if input of append-call is toString-call
    if (inputInst->GetBasicBlock() != appendInstruction->GetBasicBlock() || !IsStringBuilderToString(inputInst)) {
        return;
    }

    // Get cascading instance of input toString-call
    auto inputToStringCall = inputInst;
    ASSERT(inputToStringCall->GetInputsCount() > 0);
    auto inputInstance = inputToStringCall->GetDataFlowInput(0);
    if (inputInstance == instance) {
        return;
    }

    instructionsStack_.push(inputInstance);

    NormalizeStringBuilderAppendInstructionUsers(inputInstance, saveState);
    for (auto inputAppendInstruction : FindStringBuilderAppendInstructions(inputInstance)) {
        inputAppendInstruction->SetInput(0, instance);
        inputAppendInstruction->SetSaveState(saveState);

        if (inputAppendInstruction->GetBasicBlock() != nullptr) {
            inputAppendInstruction->GetBasicBlock()->EraseInst(inputAppendInstruction, true);
        }
        appendInstruction->InsertAfter(inputAppendInstruction);

        for (auto &input : inputAppendInstruction->GetInputs()) {
            if (input.GetInst()->IsSaveState()) {
                continue;
            }
            FixBrokenSaveStates(input.GetInst(), inputAppendInstruction);
        }
    }

    // Erase input_instance constructor from block
    for (auto &user : inputInstance->GetUsers()) {
        auto userInst = SkipSingleUserCheckInstruction(user.GetInst());
        if (IsMethodStringBuilderConstructorWithStringArg(userInst) ||
            IsMethodStringBuilderDefaultConstructor(userInst)) {
            userInst->ClearFlag(compiler::inst_flags::NO_DCE);
        }
    }

    // Cleanup save states
    RemoveFromSaveStateInputs(inputInstance);
    // Erase input_instance itself
    inputInstance->ClearFlag(compiler::inst_flags::NO_DCE);

    // Cleanup instructions we don't need anymore
    appendInstruction->GetBasicBlock()->RemoveInst(appendInstruction);
    RemoveFromSaveStateInputs(inputToStringCall);
    inputToStringCall->ClearFlag(compiler::inst_flags::NO_DCE);
}

void SimplifyStringBuilder::ReconnectStringBuilderCascades(const ConcatenationLoopMatch &match)
{
    // Consider the following code:
    //      str += a + b + c + ...
    // StringBuilder equivalent view:
    //      sb_abc.append(a)
    //      sb_abc.append(b)
    //      sb_abc.append(c)
    //      sb_str.append(sb_abc.toString())
    //
    // We call StringBuilder cascading to be calls like sb_str.append(sb_abc.toString()), i.e output of one SB used as
    // input of another SB
    //
    // The code below transforms this into:
    //      sb_str.append(a)
    //      sb_str.append(b)
    //      sb_str.append(c)
    // i.e appending args directly to string 'str', w/o the use of intermediate SB

    ASSERT(instructionsStack_.empty());
    instructionsStack_.push(match.preheader.instance);

    while (!instructionsStack_.empty()) {
        auto instance = instructionsStack_.top();
        instructionsStack_.pop();

        // For each append-call of current StringBuilder instance
        for (auto appendInstruction : FindStringBuilderAppendInstructions(instance)) {
            for (auto &input : appendInstruction->GetInputs()) {
                auto inputInst = input.GetInst();

                // Reconnect append-calls of cascading instance
                ReconnectStringBuilderCascade(instance, inputInst, appendInstruction,
                                              appendInstruction->GetSaveState());
            }
        }
    }
}

void SimplifyStringBuilder::ReconnectInstructions(const ConcatenationLoopMatch &match)
{
    // Make StringBuilder append-call hoisted point to an instance hoisted and initialize it with string initial value
    match.preheader.appendAccValue->SetInput(0, match.preheader.instance);
    match.preheader.appendAccValue->SetInput(1, match.initialValue);

    // Make temporary instance users point to an instance hoisted
    for (auto &temp : match.temp) {
        instructionsVector_.clear();
        for (auto &user : temp.instance->GetUsers()) {
            auto userInst = user.GetInst();
            if (userInst->IsSaveState()) {
                continue;
            }
            instructionsVector_.push_back(userInst);
        }
        for (auto userInst : instructionsVector_) {
            userInst->ReplaceInput(temp.instance, match.preheader.instance);

            COMPILER_LOG(DEBUG, SIMPLIFY_SB)
                << "Replace input of instruction "
                << "id=" << userInst->GetId() << " (" << GetOpcodeString(userInst->GetOpcode())
                << ") old id=" << temp.instance->GetId() << " (" << GetOpcodeString(temp.instance->GetOpcode())
                << ") new id=" << match.preheader.instance->GetId() << " ("
                << GetOpcodeString(match.preheader.instance->GetOpcode()) << ")";
        }
    }

    // Replace users of accumulated value outside the loop by toString-call hoisted to post exit block
    instructionsVector_.clear();
    for (auto &user : match.accValue->GetUsers()) {
        auto userInst = user.GetInst();
        if (userInst->IsSaveState()) {
            continue;
        }
        if (userInst->GetBasicBlock() != match.block) {
            instructionsVector_.push_back(userInst);
        }
    }

    for (auto userInst : instructionsVector_) {
        userInst->ReplaceInput(match.accValue, match.exit.toStringCall);

        COMPILER_LOG(DEBUG, SIMPLIFY_SB) << "Replace input of instruction "
                                         << "id=" << userInst->GetId() << " (" << GetOpcodeString(userInst->GetOpcode())
                                         << ") old id=" << match.accValue->GetId() << " ("
                                         << GetOpcodeString(match.accValue->GetOpcode())
                                         << ") new id=" << match.exit.toStringCall->GetId() << " ("
                                         << GetOpcodeString(match.exit.toStringCall->GetOpcode()) << ")";
    }

    ReconnectStringBuilderCascades(match);
}

bool AllInstructionInputsDominate(Inst *inst, Inst *other)
{
    return !HasInput(inst, [other](auto &input) { return !input.GetInst()->IsDominate(other); });
}

Inst *SimplifyStringBuilder::HoistInstructionToPreHeader(BasicBlock *preHeader, Inst *lastInst, Inst *inst,
                                                         SaveStateInst *saveState)
{
    // Based on similar code in LICM-pass

    COMPILER_LOG(DEBUG, SIMPLIFY_SB) << "Hoist instruction id=" << inst->GetId() << " ("
                                     << GetOpcodeString(inst->GetOpcode()) << ")"
                                     << " from loop block id=" << inst->GetBasicBlock()->GetId()
                                     << " to preheader block id=" << preHeader->GetId();

    Inst *target = nullptr;
    if (inst->IsMovableObject()) {
        target = inst->GetPrev();
        if (target == nullptr) {
            target = inst->GetNext();
        }
        if (target == nullptr) {
            target = GetGraph()->CreateInstNOP();
            inst->InsertAfter(target);
        }
    }
    inst->GetBasicBlock()->EraseInst(inst, true);
    if (lastInst == nullptr || lastInst->IsPhi()) {
        preHeader->AppendInst(inst);
        lastInst = inst;
    } else {
        ASSERT(lastInst->GetBasicBlock() == preHeader);
        Inst *saveStateDeoptimize = preHeader->FindSaveStateDeoptimize();
        if (saveStateDeoptimize != nullptr && AllInstructionInputsDominate(inst, saveStateDeoptimize)) {
            saveStateDeoptimize->InsertBefore(inst);
        } else {
            lastInst->InsertAfter(inst);
            lastInst = inst;
        }
    }

    if (inst->RequireState()) {
        ASSERT(saveState != nullptr);
        auto saveStateClone = CopySaveState(GetGraph(), saveState);
        if (saveStateClone->GetBasicBlock() == nullptr) {
            inst->InsertBefore(saveStateClone);
        }
        inst->SetSaveState(saveStateClone);
        inst->SetPc(saveStateClone->GetPc());
    }

    FixBrokenSaveStates(inst, target);

    return lastInst;
}

Inst *SimplifyStringBuilder::HoistInstructionToPreHeaderRecursively(BasicBlock *preHeader, Inst *lastInst, Inst *inst,
                                                                    SaveStateInst *saveState)
{
    // Hoist all non-SaveState instruction inputs first
    for (auto &input : inst->GetInputs()) {
        auto inputInst = input.GetInst();
        if (inputInst->IsSaveState()) {
            continue;
        }

        if (inputInst->GetBasicBlock() == preHeader) {
            continue;
        }

        lastInst = HoistInstructionToPreHeaderRecursively(preHeader, lastInst, inputInst, saveState);
        if (lastInst->RequireState()) {
            saveState = lastInst->GetSaveState();
        }
    }

    // Hoist instruction itself
    return HoistInstructionToPreHeader(preHeader, lastInst, inst, saveState);
}

void SimplifyStringBuilder::HoistInstructionsToPreHeader(const ConcatenationLoopMatch &match,
                                                         SaveStateInst *initSaveState)
{
    // Move StringBuilder construction and initialization from inside loop to preheader block

    auto loop = match.block->GetLoop();
    auto preHeader = loop->GetPreHeader();

    HoistInstructionToPreHeaderRecursively(preHeader, preHeader->GetLastInst(), match.preheader.ctorCall,
                                           initSaveState);

    ASSERT(match.preheader.ctorCall->RequireState());
    HoistInstructionToPreHeader(preHeader, match.preheader.ctorCall, match.preheader.appendAccValue,
                                match.preheader.ctorCall->GetSaveState());

    for (auto &input : match.preheader.appendAccValue->GetInputs()) {
        auto inputInst = input.GetInst();
        if (inputInst->IsSaveState()) {
            continue;
        }

        FixBrokenSaveStates(inputInst, match.preheader.appendAccValue);
    }
}

void HoistCheckInsturctionInputs(Inst *inst, BasicBlock *loopBlock, BasicBlock *postExit)
{
    for (auto &input : inst->GetInputs()) {
        auto inputInst = input.GetInst();
        if (inputInst->GetBasicBlock() == loopBlock && inputInst->IsCheck()) {
            inputInst->GetBasicBlock()->EraseInst(inputInst, true);
            if (inputInst->RequireState()) {
                inputInst->SetSaveState(CopySaveState(loopBlock->GetGraph(), inst->GetSaveState()));
            }
            InsertBeforeWithSaveState(inputInst, inst);

            COMPILER_LOG(DEBUG, SIMPLIFY_SB)
                << "Hoist instruction id=" << inputInst->GetId() << " (" << GetOpcodeString(inputInst->GetOpcode())
                << ") from loop block id=" << loopBlock->GetId() << " to post exit block id=" << postExit->GetId();
        }
    }
}

void SimplifyStringBuilder::HoistCheckCastInstructionUsers(Inst *inst, BasicBlock *loopBlock, BasicBlock *postExit)
{
    // Hoist CheckCast instruction to post exit block

    // Start with CheckCast instruction itself
    ASSERT(instructionsStack_.empty());
    for (auto &user : inst->GetUsers()) {
        auto userInst = user.GetInst();
        if (userInst->GetBasicBlock() == loopBlock && IsCheckCastWithoutUsers(userInst)) {
            instructionsStack_.push(userInst);
        }
    }

    // Collect all the inputs of CheckCast instruction as well
    instructionsVector_.clear();
    while (!instructionsStack_.empty()) {
        auto userInst = instructionsStack_.top();
        instructionsStack_.pop();
        for (auto &input : userInst->GetInputs()) {
            auto inputInst = input.GetInst();
            if (inputInst->IsSaveState()) {
                continue;
            }
            if (inputInst->GetBasicBlock() != loopBlock) {
                continue;
            }

            instructionsStack_.push(inputInst);
        }
        instructionsVector_.push_back(userInst);
    }

    // Hoist collected instructions
    for (auto userInst : instructionsVector_) {
        userInst->GetBasicBlock()->EraseInst(userInst, true);
        if (userInst->RequireState()) {
            userInst->SetSaveState(CopySaveState(GetGraph(), inst->GetSaveState()));
        }
        InsertAfterWithSaveState(userInst, inst);

        COMPILER_LOG(DEBUG, SIMPLIFY_SB) << "Hoist instruction id=" << userInst->GetId() << " ("
                                         << GetOpcodeString(userInst->GetOpcode())
                                         << ") from loop block id=" << loopBlock->GetId()
                                         << " to post exit block id=" << postExit->GetId();
    }
}

void SimplifyStringBuilder::HoistInstructionsToPostExit(const ConcatenationLoopMatch &match, SaveStateInst *saveState)
{
    // Move toString()-call and its inputs/users (null-checks, etc) out of the loop
    // and place them in the loop exit successor block

    auto postExit = GetLoopPostExit(match.block->GetLoop());
    ASSERT(postExit != nullptr);
    ASSERT(!postExit->IsEmpty());
    ASSERT(saveState->GetBasicBlock() == postExit);
    ASSERT(match.exit.toStringCall->RequireState());

    auto loopBlock = match.exit.toStringCall->GetBasicBlock();
    loopBlock->EraseInst(match.exit.toStringCall, true);

    if (!saveState->HasUsers()) {
        // First use of save state, insert toString-call after it
        match.exit.toStringCall->SetSaveState(saveState);
        saveState->InsertAfter(match.exit.toStringCall);
    } else {
        // Duplicate save state and prepend both instructions
        match.exit.toStringCall->SetSaveState(CopySaveState(GetGraph(), saveState));
        InsertBeforeWithSaveState(match.exit.toStringCall, postExit->GetFirstInst());
    }

    COMPILER_LOG(DEBUG, SIMPLIFY_SB) << "Hoist toString()-call instruction id=" << match.exit.toStringCall->GetId()
                                     << " from loop block id=" << loopBlock->GetId()
                                     << " to post exit block id=" << postExit->GetId();

    // Hoist all the toString-call Check instructions inputs
    HoistCheckInsturctionInputs(match.exit.toStringCall, loopBlock, postExit);

    // Hoist toString-call instructions users
    HoistCheckCastInstructionUsers(match.exit.toStringCall, loopBlock, postExit);
}

void SimplifyStringBuilder::Cleanup(const ConcatenationLoopMatch &match)
{
    // Remove temporary instructions

    for (auto &temp : match.temp) {
        temp.toStringCall->ClearFlag(compiler::inst_flags::NO_DCE);
        for (auto &user : temp.toStringCall->GetUsers()) {
            auto userInst = user.GetInst();
            if (IsCheckCastWithoutUsers(userInst)) {
                userInst->ClearFlag(compiler::inst_flags::NO_DCE);
            }
        }
        temp.intermediateValue->ClearFlag(compiler::inst_flags::NO_DCE);
        temp.instance->ReplaceUsers(match.preheader.instance);
        temp.instance->ClearFlag(compiler::inst_flags::NO_DCE);
        temp.ctorCall->ClearFlag(compiler::inst_flags::NO_DCE);
        temp.appendAccValue->ClearFlag(compiler::inst_flags::NO_DCE);
    }
}

bool SimplifyStringBuilder::NeedRemoveInputFromSaveStateInstruction(Inst *inputInst)
{
    for (auto &match : matches_) {
        // If input is hoisted toString-call or accumulated phi instruction mark it for removal
        if (inputInst == match.exit.toStringCall || inputInst == match.accValue) {
            return true;
        }

        // If input is removed toString-call (temporary instruction) mark it for removal
        bool isIntermediateValue = std::find_if(match.temp.begin(), match.temp.end(), [inputInst](auto &temp) {
                                       return inputInst == temp.toStringCall;
                                   }) != match.temp.end();
        if (isIntermediateValue) {
            return true;
        }
    }
    return false;
}

void SimplifyStringBuilder::CollectSaveStateInputsForRemoval(Inst *inst)
{
    ASSERT(inst->IsSaveState());

    for (size_t i = 0; i < inst->GetInputsCount(); ++i) {
        auto inputInst = inst->GetInput(i).GetInst();
        if (NeedRemoveInputFromSaveStateInstruction(inputInst)) {
            inputDescriptors_.emplace_back(inst, i);
        }
    }
}

void SimplifyStringBuilder::CleanupSaveStateInstructionInputs(Loop *loop)
{
    // StringBuilder toString-call is either hoisted to post exit block or erased from loop, accumulated value is erased
    // from loop, so this instructions need to be removed from the inputs of save state instructions within current loop

    inputDescriptors_.clear();
    for (auto block : loop->GetBlocks()) {
        for (auto inst : block->Insts()) {
            if (!inst->IsSaveState()) {
                continue;
            }
            CollectSaveStateInputsForRemoval(inst);
        }
    }
    RemoveFromInstructionInputs(inputDescriptors_);
}

bool SimplifyStringBuilder::NeedRemoveInputFromPhiInstruction(Inst *inputInst)
{
    for (auto &match : matches_) {
        // If input is hoisted toString-call mark it for removal
        if (inputInst == match.exit.toStringCall) {
            return true;
        }
    }

    return false;
}

void SimplifyStringBuilder::CleanupPhiInstructionInputs(Inst *phi)
{
    ASSERT(phi->IsPhi());

    for (size_t i = 0; i < phi->GetInputsCount(); ++i) {
        auto inputInst = phi->GetInput(i).GetInst();
        if (NeedRemoveInputFromPhiInstruction(inputInst)) {
            phi->SetInput(i, phi);
        }
    }
}

void SimplifyStringBuilder::CleanupPhiInstructionInputs(Loop *loop)
{
    // Remove toString()-call from accumulated value phi-instruction inputs
    for (auto block : loop->GetBlocks()) {
        for (auto phi : block->PhiInsts()) {
            CleanupPhiInstructionInputs(phi);
        }
    }
}

bool SimplifyStringBuilder::HasNotHoistedUser(PhiInst *phi)
{
    return HasUser(phi, [phi](auto &user) {
        auto userInst = user.GetInst();
        bool isSelf = userInst == phi;
        bool isSaveState = userInst->IsSaveState();
        bool isRemovedAppendInstruction =
            IsStringBuilderAppend(userInst) && !userInst->GetFlag(compiler::inst_flags::NO_DCE);
        return !isSelf && !isSaveState && !isRemovedAppendInstruction;
    });
}

void SimplifyStringBuilder::RemoveUnusedPhiInstructions(Loop *loop)
{
    // Remove instructions having no users, or instruction with all users hoisted out of the loop
    for (auto block : loop->GetBlocks()) {
        for (auto phi : block->PhiInstsSafe()) {
            if (HasNotHoistedUser(phi->CastToPhi())) {
                continue;
            }

            RemoveFromAllExceptPhiInputs(phi);
            block->RemoveInst(phi);

            COMPILER_LOG(DEBUG, SIMPLIFY_SB)
                << "Remove unused instruction id=" << phi->GetId() << " (" << GetOpcodeString(phi->GetOpcode())
                << ") from header block id=" << block->GetId();
        }
    }
}

void SimplifyStringBuilder::FixBrokenSaveStates(Loop *loop)
{
    ssb_.FixSaveStatesInBB(loop->GetPreHeader());
    ssb_.FixSaveStatesInBB(GetLoopPostExit(loop));
}

void SimplifyStringBuilder::Cleanup(Loop *loop)
{
    if (!isApplied_) {
        return;
    }

    FixBrokenSaveStates(loop);
    CleanupSaveStateInstructionInputs(loop);
    CleanupPhiInstructionInputs(loop);
    RemoveUnusedPhiInstructions(loop);
}

void SimplifyStringBuilder::MatchStringBuilderUsage(Inst *instance, StringBuilderUsage &usage)
{
    // Find all the usages of a given StringBuilder instance, and pack them into StringBuilderUsage structure:
    // i.e instance itself, constructor-call, all append-calls, all toString-calls

    usage.instance = instance;

    for (auto &user : instance->GetUsers()) {
        auto userInst = SkipSingleUserCheckInstruction(user.GetInst());
        if (IsMethodStringBuilderConstructorWithStringArg(userInst)) {
            usage.ctorCall = userInst;
        } else if (IsMethodStringBuilderDefaultConstructor(userInst)) {
            usage.ctorCall = userInst;
        } else if (IsStringBuilderAppend(userInst)) {
            // StringBuilder append-call returns 'this' (instance)
            // Replace all users of append-call by instance for simplicity
            userInst->ReplaceUsers(instance);
        }
    }
    ASSERT(usage.ctorCall != nullptr);

    for (auto &user : instance->GetUsers()) {
        auto userInst = SkipSingleUserCheckInstruction(user.GetInst());
        if (IsStringBuilderAppend(userInst)) {
            usage.appendInstructions.push_back(userInst);
        } else if (IsStringBuilderToString(userInst)) {
            usage.toStringCalls.push_back(userInst);
        }
    }
}

bool SimplifyStringBuilder::HasInputFromPreHeader(PhiInst *phi) const
{
    auto preHeader = phi->GetBasicBlock()->GetLoop()->GetPreHeader();
    for (size_t i = 0; i < phi->GetInputsCount(); ++i) {
        if (phi->GetPhiInputBb(i) == preHeader) {
            return true;
        }
    }
    return false;
}

bool SimplifyStringBuilder::HasToStringCallInput(PhiInst *phi) const
{
    // Returns true if
    //  (1) 'phi' has StringBuilder.toString call as one of its inputs, and
    //  (2) StringBuilder.toString call has no effective usages except this 'phi' itself.
    //     Users being save states, check casts, and not used users are skipped.

    MarkerHolder visited {GetGraph()};

    // (1)
    bool hasToStringCallInput = HasInputPhiRecursively(
        phi, visited.GetMarker(), [](auto &input) { return IsStringBuilderToString(input.GetInst()); });

    // (2)
    bool toStringCallInputUsedAnywhereExceptPhi = HasInput(phi, [phi](auto &input) {
        return IsStringBuilderToString(input.GetInst()) && HasUser(input.GetInst(), [phi](auto &user) {
                   auto userInst = user.GetInst();
                   bool isPhi = userInst == phi;
                   bool isSaveState = userInst->IsSaveState();
                   bool isCheckCast = IsCheckCastWithoutUsers(userInst);
                   bool hasUsers = userInst->HasUsers();
                   return !isPhi && !isSaveState && !isCheckCast && hasUsers;
               });
    });
    ResetInputMarkersRecursively(phi, visited.GetMarker());
    return hasToStringCallInput && !toStringCallInputUsedAnywhereExceptPhi;
}

bool SimplifyStringBuilder::HasInputInst(Inst *inputInst, Inst *inst) const
{
    MarkerHolder visited {GetGraph()};
    bool found = HasInputPhiRecursively(inst, visited.GetMarker(),
                                        [inputInst](auto &input) { return inputInst == input.GetInst(); });
    ResetInputMarkersRecursively(inst, visited.GetMarker());
    return found;
}

bool SimplifyStringBuilder::HasAppendInstructionUser(Inst *inst) const
{
    for (auto &user : inst->GetUsers()) {
        auto userInst = SkipSingleUserCheckInstruction(user.GetInst());
        if (userInst->IsSaveState()) {
            continue;
        }

        if (IsStringBuilderAppend(userInst)) {
            return true;
        }
    }

    return false;
}

bool SimplifyStringBuilder::IsPhiAccumulatedValue(PhiInst *phi) const
{
    // Phi-instruction is accumulated value, if it is used in the following way:
    //  bb_preheader:
    //      0 LoadString ...
    //      ...
    //  bb_header:
    //      10p Phi v0(bb_preheader), v20(bb_back_edge)
    //      ...
    //  bb_back_edge:
    //      ...
    //      15 Intrinsic.StdCoreSbAppendString sb, v10p, ss
    //      ...
    //      20 CallStatic std.core.StringBuilder::toString sb, ss
    //      ...

    return HasInputFromPreHeader(phi) && HasToStringCallInput(phi) && HasAppendInstructionUser(phi);
}

ArenaVector<Inst *> SimplifyStringBuilder::GetPhiAccumulatedValues(Loop *loop)
{
    // Search loop for all accumulated values
    // Phi accumulated value is an instruction used to store string concatenation result between loop iterations

    instructionsVector_.clear();

    for (auto inst : loop->GetHeader()->PhiInsts()) {
        if (IsPhiAccumulatedValue(inst->CastToPhi())) {
            instructionsVector_.push_back(inst);
        }
    }

    for (auto backEdge : loop->GetBackEdges()) {
        if (backEdge == loop->GetHeader()) {
            // Already processed above
            continue;
        }

        for (auto inst : backEdge->PhiInsts()) {
            if (IsPhiAccumulatedValue(inst->CastToPhi())) {
                instructionsVector_.push_back(inst);
            }
        }
    }

    return instructionsVector_;
}

void SimplifyStringBuilder::StringBuilderUsagesDFS(Inst *inst, Loop *loop, Marker visited)
{
    // Recursively traverse current instruction users, and collect all the StringBuilder usages met

    ASSERT(inst != nullptr);
    if (inst->GetBasicBlock()->GetLoop() != loop || inst->IsMarked(visited)) {
        return;
    }

    inst->SetMarker(visited);

    for (auto &user : inst->GetUsers()) {
        auto userInst = user.GetInst();
        if (userInst->IsPhi() && !userInst->IsMarked(visited)) {
            StringBuilderUsagesDFS(userInst, loop, visited);
        }

        if (!IsStringBuilderAppend(userInst)) {
            continue;
        }

        auto appendInstruction = userInst;
        ASSERT(appendInstruction->GetInputsCount() > 1);
        auto instance = appendInstruction->GetDataFlowInput(0);
        if (instance->GetBasicBlock()->GetLoop() == loop) {
            StringBuilderUsage usage {nullptr, nullptr, GetGraph()->GetAllocator()};
            MatchStringBuilderUsage(instance, usage);

            if (usage.toStringCalls.size() != 1) {
                continue;
            }

            if (!usage.toStringCalls[0]->IsMarked(visited)) {
                StringBuilderUsagesDFS(usage.toStringCalls[0], loop, visited);
                usages_.push_back(usage);
            }
        }
    }
}

const ArenaVector<SimplifyStringBuilder::StringBuilderUsage> &SimplifyStringBuilder::GetStringBuilderUsagesPO(
    Inst *accValue)
{
    // Get all the StringBuilder usages under the phi accumulated value instruction data flow graph in post order (PO)

    usages_.clear();

    MarkerHolder usageMarker {GetGraph()};
    Marker visited = usageMarker.GetMarker();

    StringBuilderUsagesDFS(accValue, accValue->GetBasicBlock()->GetLoop(), visited);

    return usages_;
}

bool SimplifyStringBuilder::AllUsersAreVisitedAppendInstructions(Inst *inst, Marker appendInstructionVisited)
{
    bool allUsersVisited = true;
    for (auto &user : inst->GetUsers()) {
        auto userInst = user.GetInst();
        if (userInst->IsSaveState()) {
            continue;
        }
        if (IsCheckCastWithoutUsers(userInst)) {
            continue;
        }
        allUsersVisited &= IsStringBuilderAppend(userInst);
        allUsersVisited &= userInst->IsMarked(appendInstructionVisited);
    }
    return allUsersVisited;
}

Inst *SimplifyStringBuilder::UpdateIntermediateValue(const StringBuilderUsage &usage, Inst *intermediateValue,
                                                     Marker appendInstructionVisited)
{
    // Update intermediate value with toString-call of the current StringBuilder usage, if all its append-call users
    // were already visited

    size_t usersCount = 0;
    bool allUsersVisited = true;

    for (auto &user : intermediateValue->GetUsers()) {
        auto userInst = user.GetInst();
        if (userInst->IsSaveState()) {
            continue;
        }

        if (userInst->IsPhi() && !userInst->HasUsers()) {
            continue;
        }

        if (userInst->IsPhi() && !userInst->HasUsers()) {
            continue;
        }

        if (userInst->IsPhi()) {
            ++usersCount;
            allUsersVisited &= AllUsersAreVisitedAppendInstructions(userInst, appendInstructionVisited);
        } else if (IsStringBuilderAppend(userInst)) {
            ++usersCount;
            allUsersVisited &= userInst->IsMarked(appendInstructionVisited);
        } else {
            break;
        }
    }

    ASSERT(usage.toStringCalls.size() == 1);
    if (usersCount == 1) {
        intermediateValue = usage.toStringCalls[0];
    } else if (usersCount > 1 && allUsersVisited) {
        intermediateValue = usage.toStringCalls[0];
        for (auto &user : usage.toStringCalls[0]->GetUsers()) {
            auto userInst = user.GetInst();
            if (userInst->IsPhi() && userInst->HasUsers()) {
                intermediateValue = userInst;
                break;
            }
        }
    }

    return intermediateValue;
}

void SimplifyStringBuilder::MatchTemporaryInstructions(const StringBuilderUsage &usage, ConcatenationLoopMatch &match,
                                                       Inst *accValue, Inst *intermediateValue,
                                                       Marker appendInstructionVisited)
{
    // Split all the instructions of a given StringBuilder usage into groups (substructures of ConcatenationLoopMatch):
    //  'temp' group - temporary instructions to be erased from loop
    //  'loop' group - append-call instructions to be kept inside loop

    ConcatenationLoopMatch::TemporaryInstructions temp {};
    temp.intermediateValue = intermediateValue;
    temp.toStringCall = usage.toStringCalls[0];
    temp.instance = usage.instance;
    temp.ctorCall = usage.ctorCall;

    for (auto appendInstruction : usage.appendInstructions) {
        ASSERT(appendInstruction->GetInputsCount() > 1);
        auto appendArg = appendInstruction->GetDataFlowInput(1);
        if ((appendArg->IsPhi() && IsDataFlowInput(appendArg, intermediateValue)) || appendArg == intermediateValue ||
            appendArg == accValue) {
            // Append-call needs to be removed, if its argument is either accumulated value, or intermediate value;
            // or intermediate value is data flow input of argument

            if (temp.appendAccValue == nullptr) {
                temp.appendAccValue = appendInstruction;
            } else {
                // Does not look like string concatenation pattern
                temp.Clear();
                break;
            }
            appendInstruction->SetMarker(appendInstructionVisited);
        } else {
            // Keep append-call inside loop otherwise
            match.loop.appendInstructions.push_back(appendInstruction);
        }
    }

    if (!temp.IsEmpty()) {
        match.temp.push_back(temp);
    }
}

Inst *SimplifyStringBuilder::MatchHoistableInstructions(const StringBuilderUsage &usage, ConcatenationLoopMatch &match,
                                                        Marker appendInstructionVisited)
{
    // Split all the instructions of a given StringBuilder usage into groups (substructures of ConcatenationLoopMatch):
    //  'preheader' group - instructions to be hoisted to preheader block
    //  'loop' group - append-call instructions to be kept inside loop
    //  'exit' group - instructions to be hoisted to post exit block

    match.block = usage.instance->GetBasicBlock();

    ASSERT(usage.instance->GetInputsCount() > 0);
    match.preheader.instance = usage.instance;
    match.preheader.ctorCall = usage.ctorCall;
    ASSERT(usage.toStringCalls.size() == 1);
    match.exit.toStringCall = usage.toStringCalls[0];

    for (auto &user : usage.instance->GetUsers()) {
        auto userInst = SkipSingleUserCheckInstruction(user.GetInst());
        if (userInst->IsSaveState()) {
            continue;
        }

        if (!IsStringBuilderAppend(userInst)) {
            continue;
        }

        // Check if append-call needs to be hoisted or kept inside loop
        auto appendInstruction = userInst;
        ASSERT(appendInstruction->GetInputsCount() > 1);
        auto appendArg = appendInstruction->GetDataFlowInput(1);
        if (appendArg->IsPhi() && IsPhiAccumulatedValue(appendArg->CastToPhi())) {
            // Append-call needs to be hoisted, if its argument is accumulated value
            auto phiAppendArg = appendArg->CastToPhi();
            auto initialValue =
                phiAppendArg->GetPhiDataflowInput(usage.instance->GetBasicBlock()->GetLoop()->GetPreHeader());
            if (match.initialValue != nullptr || match.accValue != nullptr ||
                match.preheader.appendAccValue != nullptr) {
                // Does not look like string concatenation pattern
                match.Clear();
                break;
            }

            match.initialValue = initialValue;
            match.accValue = phiAppendArg;
            match.preheader.appendAccValue = appendInstruction;

            appendInstruction->SetMarker(appendInstructionVisited);
        } else {
            // Keep append-call inside loop otherwise
            match.loop.appendInstructions.push_back(appendInstruction);
        }
    }

    // Initialize intermediate value with toString-call to be hoisted to post exit block
    return match.exit.toStringCall;
}

const ArenaVector<SimplifyStringBuilder::ConcatenationLoopMatch> &SimplifyStringBuilder::MatchLoopConcatenation(
    Loop *loop)
{
    // Search loop for string concatenation patterns like the following:
    //      let str = initial_value: String
    //      for (...) {
    //          str += a0 + b0 + ...
    //          str += a1 + b2 + ...
    //          ...
    //      }
    // And fill ConcatenationLoopMatch structure with instructions from the pattern found

    matches_.clear();

    MarkerHolder appendInstructionMarker {GetGraph()};
    Marker appendInstructionVisited = appendInstructionMarker.GetMarker();

    // Accumulated value (acc_value) is a phi-instruction holding concatenation result between loop iterations, and
    // final result after loop completes
    for (auto accValue : GetPhiAccumulatedValues(loop)) {
        // Intermediate value is an instruction holding concatenation result during loop iteration execution.
        // It is initialized with acc_value, and updated with either toString-calls, or other phi-instructions
        // (depending on the loop control flow and data flow)
        Inst *intermediateValue = accValue;
        ConcatenationLoopMatch match {GetGraph()->GetAllocator()};

        // Get all the StringBuilders used to calculate current accumulated value (in PO)
        auto &usages = GetStringBuilderUsagesPO(accValue);
        // RPO traversal: walk through PO usages backwards
        for (auto usage = usages.rbegin(); usage != usages.rend(); ++usage) {
            if (usage->toStringCalls.size() != 1) {
                continue;  // Unsupported: doesn't look like string concatenation pattern
            }

            if (match.preheader.instance == nullptr) {
                // First StringBuilder instance are set to be hoisted
                intermediateValue = MatchHoistableInstructions(*usage, match, appendInstructionVisited);
            } else {
                // All other StringBuilder instances are set to be removed as temporary
                MatchTemporaryInstructions(*usage, match, accValue, intermediateValue, appendInstructionVisited);
                intermediateValue = UpdateIntermediateValue(*usage, intermediateValue, appendInstructionVisited);
            }
        }

        if (IsInstanceHoistable(match) && IsToStringHoistable(match, appendInstructionVisited)) {
            matches_.push_back(match);
        }

        // Reset markers
        if (match.preheader.appendAccValue != nullptr) {
            match.preheader.appendAccValue->ResetMarker(appendInstructionVisited);
        }
        for (auto &temp : match.temp) {
            if (temp.appendAccValue != nullptr) {
                temp.appendAccValue->ResetMarker(appendInstructionVisited);
            }
        }
    }

    return matches_;
}

void SimplifyStringBuilder::OptimizeStringConcatenation(Loop *loop)
{
    // Optimize String Builder concatenation loops

    // Process inner loops first
    for (auto innerLoop : loop->GetInnerLoops()) {
        OptimizeStringConcatenation(innerLoop);
    }

    // Check if basic block for instructions to hoist exist
    // Alternative way maybe to create one
    if (loop->GetPreHeader() == nullptr) {
        return;
    }

    auto preHeaderSaveState = FindPreHeaderSaveState(loop);
    if (preHeaderSaveState == nullptr) {
        return;
    }

    // Check if basic block for instructions to hoist exist
    // Alternative way maybe to create one
    auto postExit = GetLoopPostExit(loop);
    if (postExit == nullptr) {
        return;
    }

    auto postExitSaveState = FindFirstSaveState(postExit);
    ASSERT(postExitSaveState);  // IR Builder must create empty save states for loop exits

    for (auto &match : MatchLoopConcatenation(loop)) {
        ReconnectInstructions(match);
        HoistInstructionsToPreHeader(match, preHeaderSaveState);
        HoistInstructionsToPostExit(match, postExitSaveState);
        Cleanup(match);

        isApplied_ = true;
    }
    Cleanup(loop);
}

}  // namespace ark::compiler
