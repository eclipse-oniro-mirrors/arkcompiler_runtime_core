/*
 * Copyright (c) 2021-2024 Huawei Device Co., Ltd.
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

#include "analysis.h"

#include "optimizer/ir/basicblock.h"
#include "optimizer/analysis/dominators_tree.h"
#include "optimizer/analysis/loop_analyzer.h"
#include "compiler_logger.h"
namespace panda::compiler {

class BasicBlock;

class IsOsrEntryBlock {
public:
    bool operator()(const BasicBlock *bb) const
    {
        return bb->IsOsrEntry();
    }
};

class IsTryBlock {
public:
    bool operator()(const BasicBlock *bb) const
    {
        return bb->IsTry();
    }
};

class IsSaveState {
public:
    bool operator()(const Inst *inst) const
    {
        return inst->IsSaveState() && inst->IsNotRemovable();
    }
};

static bool IsSaveStateForGc(const Inst *inst);

class IsSaveStateCanTriggerGc {
public:
    bool operator()(const Inst *inst) const
    {
        return IsSaveStateForGc(inst);
    }
};

template <typename T>
bool FindBlockBetween(BasicBlock *dominateBb, BasicBlock *currentBb, Marker marker)
{
    if (dominateBb == currentBb) {
        return false;
    }
    if (currentBb->SetMarker(marker)) {
        return false;
    }
    if (T()(currentBb)) {
        return true;
    }
    for (auto pred : currentBb->GetPredsBlocks()) {
        if (FindBlockBetween<T>(dominateBb, pred, marker)) {
            return true;
        }
    }
    return false;
}

RuntimeInterface::ClassPtr GetClassPtrForObject(Inst *inst, size_t inputNum)
{
    auto objInst = inst->GetDataFlowInput(inputNum);
    if (objInst->GetOpcode() != Opcode::NewObject) {
        return nullptr;
    }
    auto initClass = objInst->GetInput(0).GetInst();
    if (initClass->GetOpcode() == Opcode::LoadAndInitClass) {
        return initClass->CastToLoadAndInitClass()->GetClass();
    }
    ASSERT(initClass->GetOpcode() == Opcode::LoadImmediate);
    return initClass->CastToLoadImmediate()->GetClass();
}

bool HasOsrEntryBetween(Inst *dominateInst, Inst *inst)
{
    ASSERT(dominateInst->IsDominate(inst));
    auto bb = inst->GetBasicBlock();
    auto graph = bb->GetGraph();
    if (!graph->IsOsrMode()) {
        return false;
    }
    MarkerHolder marker(graph);
    return FindBlockBetween<IsOsrEntryBlock>(dominateInst->GetBasicBlock(), bb, marker.GetMarker());
}

bool HasOsrEntryBetween(BasicBlock *dominateBb, BasicBlock *bb)
{
    ASSERT(dominateBb->IsDominate(bb));
    auto graph = bb->GetGraph();
    if (!graph->IsOsrMode()) {
        return false;
    }
    MarkerHolder marker(graph);
    return FindBlockBetween<IsOsrEntryBlock>(dominateBb, bb, marker.GetMarker());
}

bool HasTryBlockBetween(Inst *dominateInst, Inst *inst)
{
    ASSERT(dominateInst->IsDominate(inst));
    auto bb = inst->GetBasicBlock();
    MarkerHolder marker(bb->GetGraph());
    return FindBlockBetween<IsTryBlock>(dominateInst->GetBasicBlock(), bb, marker.GetMarker());
}

Inst *InstStoredValue(Inst *inst, Inst **secondValue)
{
    ASSERT_PRINT(inst->IsStore(), "Attempt to take a stored value on non-store instruction");
    Inst *val = nullptr;
    *secondValue = nullptr;
    switch (inst->GetOpcode()) {
        case Opcode::StoreArray:
        case Opcode::StoreObject:
        case Opcode::StoreStatic:
        case Opcode::StoreArrayI:
        case Opcode::Store:
        case Opcode::StoreI:
        case Opcode::StoreObjectDynamic:
            // Last input is a stored value
            val = inst->GetInput(inst->GetInputsCount() - 1).GetInst();
            break;
        case Opcode::StoreResolvedObjectField:
        case Opcode::StoreResolvedObjectFieldStatic:
            val = inst->GetInput(1).GetInst();
            break;
        case Opcode::UnresolvedStoreStatic:
            val = inst->GetInput(0).GetInst();
            break;
        case Opcode::StoreArrayPair:
        case Opcode::StoreArrayPairI: {
            val = inst->GetInput(inst->GetInputsCount() - 2U).GetInst();
            auto secondInst = inst->GetInput(inst->GetInputsCount() - 1U).GetInst();
            *secondValue = inst->GetDataFlowInput(secondInst);
            break;
        }
        case Opcode::FillConstArray: {
            return nullptr;
        }
        // Unhandled store instructions has been met
        default:
            UNREACHABLE();
    }
    return inst->GetDataFlowInput(val);
}

Inst *InstStoredValue(Inst *inst)
{
    Inst *secondValue = nullptr;
    Inst *val = InstStoredValue(inst, &secondValue);
    ASSERT(secondValue == nullptr);
    return val;
}

SaveStateInst *CopySaveState(Graph *graph, SaveStateInst *inst)
{
    auto copy = static_cast<SaveStateInst *>(inst->Clone(graph));
    ASSERT(copy->GetCallerInst() == inst->GetCallerInst());
    for (size_t inputIdx = 0; inputIdx < inst->GetInputsCount(); inputIdx++) {
        copy->AppendInput(inst->GetInput(inputIdx));
        copy->SetVirtualRegister(inputIdx, inst->GetVirtualRegister(inputIdx));
    }
    copy->SetLinearNumber(inst->GetLinearNumber());
    return copy;
}

template <typename T>
bool CanArrayAccessBeImplicit(T *array, RuntimeInterface *runtime)
{
    size_t index = array->GetImm();
    auto arch = array->GetBasicBlock()->GetGraph()->GetArch();
    size_t offset = runtime->GetArrayDataOffset(arch) + (index << DataType::ShiftByType(array->GetType(), arch));
    return offset < runtime->GetProtectedMemorySize();
}

bool IsSuitableForImplicitNullCheck(const Inst *inst)
{
    auto graph = inst->GetBasicBlock()->GetGraph();
    auto runtime = graph->GetRuntime();
    size_t maxOffset = runtime->GetProtectedMemorySize();
    switch (inst->GetOpcode()) {
        case Opcode::LoadArray:
        case Opcode::StoreArray:
        case Opcode::LoadArrayPair:
        case Opcode::StoreArrayPair: {
            // we don't know array index, so offset can be more than protected memory
            return false;
        }
        case Opcode::LoadArrayI: {
            ASSERT(inst->CastToLoadArrayI()->IsArray() || !runtime->IsCompressedStringsEnabled());

            auto instLoadArrayI = inst->CastToLoadArrayI();
            auto arch = graph->GetArch();

            size_t dataOffset =
                instLoadArrayI->IsArray() ? runtime->GetArrayDataOffset(arch) : runtime->GetStringDataOffset(arch);
            size_t shift = DataType::ShiftByType(inst->GetType(), arch);
            size_t offset = dataOffset + (instLoadArrayI->GetImm() << shift);
            return offset < maxOffset;
        }
        case Opcode::LenArray:
            return true;
        case Opcode::LoadObject: {
            auto loadObj = inst->CastToLoadObject();
            return GetObjectOffset(graph, loadObj->GetObjectType(), loadObj->GetObjField(), loadObj->GetTypeId()) <
                   maxOffset;
        }
        case Opcode::StoreObject: {
            auto storeObj = inst->CastToStoreObject();
            return GetObjectOffset(graph, storeObj->GetObjectType(), storeObj->GetObjField(), storeObj->GetTypeId()) <
                   maxOffset;
        }
        case Opcode::StoreArrayI:
            return CanArrayAccessBeImplicit(inst->CastToStoreArrayI(), runtime);
        case Opcode::LoadArrayPairI:
            return CanArrayAccessBeImplicit(inst->CastToLoadArrayPairI(), runtime);
        case Opcode::StoreArrayPairI:
            return CanArrayAccessBeImplicit(inst->CastToStoreArrayPairI(), runtime);

        default:
            return false;
    }
}

bool IsInstNotNull(const Inst *inst)
{
    // Allocations cannot return null pointer
    if (inst->IsAllocation() || inst->IsNullCheck()) {
        return true;
    }
    if (inst->IsParameter() && inst->CastToParameter()->GetArgNumber() == 0) {
        auto graph = inst->GetBasicBlock()->GetGraph();
        // The object is not null if object is first parameter and the method is virtual.
        return !graph->GetRuntime()->IsMethodStatic(graph->GetMethod());
    }
    return false;
}

static bool FindObjectInSaveState(Inst *object, Inst *ss)
{
    if (!object->IsMovableObject()) {
        return true;
    }
    while (ss != nullptr && object->IsDominate(ss)) {
        auto it = std::find_if(ss->GetInputs().begin(), ss->GetInputs().end(),
                               [object, ss](Input input) { return ss->GetDataFlowInput(input.GetInst()) == object; });
        if (it != ss->GetInputs().end()) {
            return true;
        }
        auto caller = static_cast<SaveStateInst *>(ss)->GetCallerInst();
        if (caller == nullptr) {
            break;
        }
        ss = caller->GetSaveState();
    }
    return false;
}

// Returns true if GC can be triggered at this point
static bool IsSaveStateForGc(const Inst *inst)
{
    if (inst->GetOpcode() == Opcode::SafePoint) {
        return true;
    }
    if (inst->GetOpcode() == Opcode::SaveState) {
        for (auto &user : inst->GetUsers()) {
            if (user.GetInst()->IsRuntimeCall()) {
                return true;
            }
        }
    }
    return false;
}

bool FindAndRemindObjectInSaveState(Inst *object, Inst *inst, Inst **failedSs)
{
    if (IsSaveStateForGc(inst) && !FindObjectInSaveState(object, inst)) {
        if (failedSs != nullptr) {
            *failedSs = inst;
        }
        return false;
    }
    return true;
}

// Checks if object is correctly used in SaveStates between it and user
bool CheckObjectRec(Inst *object, const Inst *user, const BasicBlock *block, Inst *startFrom, Marker visited,
                    Inst **failedSs)
{
    if (startFrom != nullptr) {
        auto it = InstSafeIterator<IterationType::ALL, IterationDirection::BACKWARD>(*block, startFrom);
        for (; it != block->AllInstsSafeReverse().end(); ++it) {
            auto inst = *it;
            if (inst == nullptr) {
                break;
            }
            if (inst->SetMarker(visited) || inst == object || inst == user) {
                return true;
            }
            if (!FindAndRemindObjectInSaveState(object, inst, failedSs)) {
                return false;
            }
        }
    }
    for (auto pred : block->GetPredsBlocks()) {
        // Catch-begin block has edge from try-end block, and all try-blocks should be visited from this edge.
        // `object` can be placed inside try-block - after try-begin, so that visiting try-begin is wrong
        if (block->IsCatchBegin() && pred->IsTryBegin()) {
            continue;
        }
        if (!CheckObjectRec(object, user, pred, pred->GetLastInst(), visited, failedSs)) {
            return false;
        }
    }
    return true;
}

// Checks if input edges of phi_block come from different branches of dominating if_imm instruction
// Returns true if the first input is in true branch, false if it is in false branch, and std::nullopt
// if branches intersect
std::optional<bool> IsIfInverted(BasicBlock *phiBlock, IfImmInst *ifImm)
{
    auto ifBlock = ifImm->GetBasicBlock();
    ASSERT(ifBlock == phiBlock->GetDominator());
    ASSERT(phiBlock->GetPredsBlocks().size() == MAX_SUCCS_NUM);
    auto trueBb = ifImm->GetEdgeIfInputTrue();
    auto falseBb = ifImm->GetEdgeIfInputFalse();
    auto pred0 = phiBlock->GetPredecessor(0);
    auto pred1 = phiBlock->GetPredecessor(1);

    // Triangle case: phi block is the first in true branch
    if (trueBb == phiBlock && falseBb->GetPredsBlocks().size() == 1) {
        return pred0 != ifBlock;
    }
    // Triangle case: phi block is the first in false branch
    if (falseBb == phiBlock && trueBb->GetPredsBlocks().size() == 1) {
        return pred0 == ifBlock;
    }
    // If true_bb has more than one predecessor, there can be a path from false_bb
    // to true_bb avoiding if_imm
    if (trueBb->GetPredsBlocks().size() > 1 || falseBb->GetPredsBlocks().size() > 1) {
        return std::nullopt;
    }
    // Every path through first input edge to phi_block comes from true branch
    // Every path through second input edge to phi_block comes from false branch
    if (trueBb->IsDominate(pred0) && falseBb->IsDominate(pred1)) {
        return false;
    }
    // Every path through first input edge to phi_block comes from false branch
    // Every path through second input edge to phi_block comes from true branch
    if (falseBb->IsDominate(pred0) && trueBb->IsDominate(pred1)) {
        return true;
    }
    // True and false branches intersect
    return std::nullopt;
}
ArenaVector<Inst *> *SaveStateBridgesBuilder::SearchMissingObjInSaveStates(Graph *graph, Inst *source, Inst *target,
                                                                           Inst *stopSearch, BasicBlock *targetBlock)
{
    ASSERT(graph != nullptr);
    ASSERT(source != nullptr);
    ASSERT(targetBlock != nullptr);
    ASSERT(source->IsMovableObject());

    if (bridges_ == nullptr) {
        auto adapter = graph->GetLocalAllocator();
        bridges_ = adapter->New<ArenaVector<Inst *>>(adapter->Adapter());
    } else {
        bridges_->clear();
    }
    auto visited = graph->NewMarker();
    SearchSSOnWay(targetBlock, target, source, visited, bridges_, stopSearch);
    graph->EraseMarker(visited);
    return bridges_;
}

void SaveStateBridgesBuilder::SearchSSOnWay(BasicBlock *block, Inst *startFrom, Inst *sourceInst, Marker visited,
                                            ArenaVector<Inst *> *bridges, Inst *stopSearch)
{
    ASSERT(block != nullptr);
    ASSERT(sourceInst != nullptr);
    ASSERT(bridges != nullptr);

    if (startFrom != nullptr) {
        auto it = InstSafeIterator<IterationType::ALL, IterationDirection::BACKWARD>(*block, startFrom);
        for (; it != block->AllInstsSafeReverse().end(); ++it) {
            auto inst = *it;
            if (inst == nullptr) {
                break;
            }
            COMPILER_LOG(DEBUG, BRIDGES_SS) << " See inst" << *inst;

            if (inst->SetMarker(visited)) {
                return;
            }
            if (IsSaveStateForGc(inst)) {
                COMPILER_LOG(DEBUG, BRIDGES_SS) << "\tSearch in SS";
                SearchInSaveStateAndFillBridgeVector(inst, sourceInst, bridges);
            }
            // When "stop_search" is nullptr second clause never causes early exit here
            if (inst == sourceInst || inst == stopSearch) {
                return;
            }
        }
    }
    for (auto pred : block->GetPredsBlocks()) {
        SearchSSOnWay(pred, pred->GetLastInst(), sourceInst, visited, bridges, stopSearch);
    }
}

void SaveStateBridgesBuilder::SearchInSaveStateAndFillBridgeVector(Inst *inst, Inst *searchedInst,
                                                                   ArenaVector<Inst *> *bridges)
{
    ASSERT(inst != nullptr);
    ASSERT(searchedInst != nullptr);
    ASSERT(bridges != nullptr);
    auto user = std::find_if(inst->GetInputs().begin(), inst->GetInputs().end(), [searchedInst, inst](Input input) {
        return inst->GetDataFlowInput(input.GetInst()) == searchedInst;
    });
    if (user == inst->GetInputs().end()) {
        COMPILER_LOG(DEBUG, BRIDGES_SS) << "\tNot found";
        bridges->push_back(inst);
    }
}

void SaveStateBridgesBuilder::FixUsagePhiInBB(BasicBlock *block, Inst *inst)
{
    ASSERT(block != nullptr);
    ASSERT(inst != nullptr);
    if (inst->IsMovableObject()) {
        for (auto &user : inst->GetUsers()) {
            auto targetInst = user.GetInst();
            COMPILER_LOG(DEBUG, BRIDGES_SS) << " Check usage: Try to do SSB for inst: " << inst->GetId() << "\t"
                                            << " For target inst: " << targetInst->GetId() << "\n";
            // If inst usage in other BB than in all case object must exist until the end of the BB
            if (targetInst->IsPhi() || targetInst->GetBasicBlock() != block) {
                targetInst = block->GetLastInst();
            }
            SearchAndCreateMissingObjInSaveState(block->GetGraph(), inst, targetInst, block->GetFirstInst());
        }
    }
}

void SaveStateBridgesBuilder::FixUsageInstInOtherBB(BasicBlock *block, Inst *inst)
{
    ASSERT(block != nullptr);
    ASSERT(inst != nullptr);
    if (inst->IsMovableObject()) {
        for (auto &user : inst->GetUsers()) {
            auto targetInst = user.GetInst();
            // This way "in same block" checked when we saw inputs of instructions
            if (targetInst->GetBasicBlock() == block) {
                continue;
            }
            COMPILER_LOG(DEBUG, BRIDGES_SS) << " Check inputs: Try to do SSB for real source inst: " << *inst << "\n"
                                            << "  For target inst: " << *targetInst << "\n";
            // If inst usage in other BB than in all case object must must exist until the end of the BB
            targetInst = block->GetLastInst();
            SearchAndCreateMissingObjInSaveState(block->GetGraph(), inst, targetInst, block->GetFirstInst());
        }
    }
}

void SaveStateBridgesBuilder::DeleteUnrealObjInSaveState(Inst *ss)
{
    ASSERT(ss != nullptr);
    size_t indexInput = 0;
    for (auto &input : ss->GetInputs()) {
        // If the user of SS before inst
        auto inputInst = input.GetInst();
        if (ss->GetBasicBlock() == inputInst->GetBasicBlock() && ss->IsDominate(inputInst)) {
            ss->RemoveInput(indexInput);
            COMPILER_LOG(DEBUG, BRIDGES_SS) << " Fixed incorrect user in ss: " << ss->GetId() << "  "
                                            << " deleted input: " << inputInst->GetId() << "\n";
        }
        indexInput++;
    }
}

void SaveStateBridgesBuilder::FixSaveStatesInBB(BasicBlock *block)
{
    ASSERT(block != nullptr);
    bool blockInLoop = !(block->GetLoop()->IsRoot());
    // Check usage ".ref" PHI inst
    for (auto phi : block->PhiInsts()) {
        FixUsagePhiInBB(block, phi);
    }
    // Check all insts
    for (auto inst : block->Insts()) {
        if (IsSaveStateForGc(inst)) {
            DeleteUnrealObjInSaveState(inst);
        }
        // Check reference inputs of instructions
        for (auto &input : inst->GetInputs()) {
            // We record the original object in SaveState without checks
            auto realSourceInst = inst->GetDataFlowInput(input.GetInst());
            if (!realSourceInst->IsMovableObject()) {
                continue;
            }
            // In case, when usege of object in loop and defenition is not in loop or usage's loop inside defenition's
            // loop, we should check SaveStates till the end of BasicBlock
            if (blockInLoop && (block->GetLoop()->IsInside(realSourceInst->GetBasicBlock()->GetLoop()))) {
                COMPILER_LOG(DEBUG, BRIDGES_SS)
                    << " Check inputs: Try to do SSB for real source inst: " << *realSourceInst << "\n"
                    << "  Block in loop:  " << block->GetLoop() << " So target is end of BB:" << *(block->GetLastInst())
                    << "\n";
                SearchAndCreateMissingObjInSaveState(block->GetGraph(), realSourceInst, block->GetLastInst(),
                                                     block->GetFirstInst());
            } else {
                COMPILER_LOG(DEBUG, BRIDGES_SS)
                    << " Check inputs: Try to do SSB for real source inst: " << *realSourceInst << "\n"
                    << "  For target inst: " << *inst << "\n";
                SearchAndCreateMissingObjInSaveState(block->GetGraph(), realSourceInst, inst, block->GetFirstInst());
            }
        }
        // Check usage reference instruction
        FixUsageInstInOtherBB(block, inst);
    }
}

bool SaveStateBridgesBuilder::IsSaveStateForGc(Inst *inst)
{
    return inst->GetOpcode() == Opcode::SafePoint || inst->GetOpcode() == Opcode::SaveState;
}

void SaveStateBridgesBuilder::CreateBridgeInSS(Inst *source, ArenaVector<Inst *> *bridges)
{
    ASSERT(bridges != nullptr);
    ASSERT(source != nullptr);
    ASSERT(source->IsMovableObject());

    for (Inst *ss : *bridges) {
        static_cast<SaveStateInst *>(ss)->AppendBridge(source);
    }
}

void SaveStateBridgesBuilder::SearchAndCreateMissingObjInSaveState(Graph *graph, Inst *source, Inst *target,
                                                                   Inst *stopSearchInst, BasicBlock *targetBlock)
{
    ASSERT(graph != nullptr);
    ASSERT(source != nullptr);
    ASSERT(source->IsMovableObject());

    if (graph->IsBytecodeOptimizer()) {
        return;  // SaveState bridges useless when bytecode optimizer enabled.
    }

    if (targetBlock == nullptr) {
        ASSERT(target != nullptr);
        targetBlock = target->GetBasicBlock();
    } else {
        ASSERT(target == targetBlock->GetLastInst());
    }
    auto bridges = SearchMissingObjInSaveStates(graph, source, target, stopSearchInst, targetBlock);
    if (!bridges->empty()) {
        CreateBridgeInSS(source, bridges);
        COMPILER_LOG(DEBUG, BRIDGES_SS) << " Created bridge(s)";
    }
}

void SaveStateBridgesBuilder::ProcessSSUserPreds(Graph *graph, Inst *inst, Inst *targetInst)
{
    for (auto predBlock : targetInst->GetBasicBlock()->GetPredsBlocks()) {
        if (targetInst->CastToPhi()->GetPhiInput(predBlock) == inst) {
            SearchAndCreateMissingObjInSaveState(graph, inst, predBlock->GetLastInst(), nullptr, predBlock);
        }
    }
}

void SaveStateBridgesBuilder::FixInstUsageInSS(Graph *graph, Inst *inst)
{
    if (!inst->IsMovableObject()) {
        return;
    }
    for (auto &user : inst->GetUsers()) {
        auto targetInst = user.GetInst();
        COMPILER_LOG(DEBUG, BRIDGES_SS) << " Check usage: Try to do SSB for real source inst: " << *inst << "\n"
                                        << "  For target inst: " << *targetInst << "\n";
        if (targetInst->IsPhi() && !(graph->IsAnalysisValid<DominatorsTree>() && inst->IsDominate(targetInst))) {
            ProcessSSUserPreds(graph, inst, targetInst);
        } else {
            SearchAndCreateMissingObjInSaveState(graph, inst, targetInst);
        }
    }
}

// Check instructions don't have their own vregs and thus are not added in SaveStates,
// but newly added Phi instructions with check inputs should be added
void SaveStateBridgesBuilder::FixPhisWithCheckInputs(BasicBlock *block)
{
    if (block == nullptr) {
        return;
    }
    auto graph = block->GetGraph();
    for (auto phi : block->PhiInsts()) {
        if (!phi->IsMovableObject()) {
            continue;
        }
        for (auto &input : phi->GetInputs()) {
            if (input.GetInst()->IsCheck()) {
                FixInstUsageInSS(graph, phi);
                break;
            }
        }
    }
}

void SaveStateBridgesBuilder::DumpBridges(std::ostream &out, Inst *source, ArenaVector<Inst *> *bridges)
{
    ASSERT(source != nullptr);
    ASSERT(bridges != nullptr);
    out << "Inst id " << source->GetId() << " with type ";
    source->DumpOpcode(&out);
    out << "need bridge in SS id: ";
    for (auto ss : *bridges) {
        out << ss->GetId() << " ";
    }
    out << '\n';
}

bool StoreValueCanBeObject(Inst *inst)
{
    switch (inst->GetOpcode()) {
        case Opcode::CastValueToAnyType: {
            auto type = AnyBaseTypeToDataType(inst->CastToCastValueToAnyType()->GetAnyType());
            return (type == DataType::ANY || type == DataType::REFERENCE);
        }
        case Opcode::Constant:
            return false;
        default:
            return true;
    }
}

bool IsConditionEqual(const Inst *inst0, const Inst *inst1, bool inverted)
{
    if (inst0->GetOpcode() != inst1->GetOpcode()) {
        return false;
    }
    if (inst0->GetOpcode() != Opcode::IfImm) {
        // investigate why Opcode::If cannot be lowered to Opcode::IfImm and support it if needed
        return false;
    }
    auto ifImm0 = inst0->CastToIfImm();
    auto ifImm1 = inst1->CastToIfImm();
    auto opcode = ifImm0->GetInput(0).GetInst()->GetOpcode();
    if (opcode != ifImm1->GetInput(0).GetInst()->GetOpcode()) {
        return false;
    }
    if (ifImm0->GetImm() != 0 && ifImm0->GetImm() != 1) {
        return false;
    }
    if (ifImm1->GetImm() != 0 && ifImm1->GetImm() != 1) {
        return false;
    }
    if (ifImm0->GetImm() != ifImm1->GetImm()) {
        inverted = !inverted;
    }
    if (opcode != Opcode::Compare) {
        if (ifImm0->GetInput(0).GetInst() != ifImm1->GetInput(0).GetInst()) {
            return false;
        }
        auto cc = inverted ? GetInverseConditionCode(ifImm0->GetCc()) : ifImm0->GetCc();
        return cc == ifImm1->GetCc();
    }
    auto cmp0 = ifImm0->GetInput(0).GetInst()->CastToCompare();
    auto cmp1 = ifImm1->GetInput(0).GetInst()->CastToCompare();
    if (cmp0->GetInput(0).GetInst() == cmp1->GetInput(0).GetInst() &&
        cmp0->GetInput(1).GetInst() == cmp1->GetInput(1).GetInst()) {
        if (GetInverseConditionCode(ifImm0->GetCc()) == ifImm1->GetCc()) {
            inverted = !inverted;
        } else if (ifImm0->GetCc() != ifImm1->GetCc()) {
            return false;
        }
        auto cc = inverted ? GetInverseConditionCode(cmp0->GetCc()) : cmp0->GetCc();
        return cc == cmp1->GetCc();
    }
    return false;
}

void CleanupGraphSaveStateOSR(Graph *graph)
{
    ASSERT(graph != nullptr);
    ASSERT(graph->IsOsrMode());
    graph->InvalidateAnalysis<LoopAnalyzer>();
    graph->RunPass<LoopAnalyzer>();
    for (auto block : graph->GetBlocksRPO()) {
        if (block->IsOsrEntry() && !block->IsLoopHeader()) {
            auto firstInst = block->GetFirstInst();
            if (firstInst == nullptr) {
                continue;
            }
            if (firstInst->GetOpcode() == Opcode::SaveStateOsr) {
                block->RemoveInst(firstInst);
                block->SetOsrEntry(false);
            }
        }
    }
}

template <typename T>
bool FindInstBetween(Inst *domInst, BasicBlock *currentBb, Marker marker)
{
    if (currentBb->SetMarker(marker)) {
        return false;
    }
    bool isSameBlock = domInst->GetBasicBlock() == currentBb;
    auto currInst = currentBb->GetLastInst();
    Inst *finish = isSameBlock ? domInst : nullptr;
    while (currInst != finish) {
        if (T()(currInst)) {
            return true;
        }
        currInst = currInst->GetPrev();
    }
    if (isSameBlock) {
        return false;
    }
    for (auto pred : currentBb->GetPredsBlocks()) {
        if (FindInstBetween<T>(domInst, pred, marker)) {
            return true;
        }
    }
    return false;
}

template bool HasSaveStateBetween<IsSaveState>(Inst *dom_inst, Inst *inst);
template bool HasSaveStateBetween<IsSaveStateCanTriggerGc>(Inst *dom_inst, Inst *inst);

template <typename T>
bool HasSaveStateBetween(Inst *domInst, Inst *inst)
{
    ASSERT(domInst->IsDominate(inst));
    if (domInst == inst) {
        return false;
    }
    auto bb = inst->GetBasicBlock();
    bool isSameBlock = domInst->GetBasicBlock() == bb;
    auto currInst = inst->GetPrev();
    Inst *finish = isSameBlock ? domInst : nullptr;
    while (currInst != finish) {
        if (T()(currInst)) {
            return true;
        }
        currInst = currInst->GetPrev();
    }
    if (isSameBlock) {
        return false;
    }
    MarkerHolder marker(bb->GetGraph());
    for (auto pred : bb->GetPredsBlocks()) {
        if (FindInstBetween<T>(domInst, pred, marker.GetMarker())) {
            return true;
        }
    }
    return false;
}

void InstAppender::Append(Inst *inst)
{
    if (prev_ == nullptr) {
        block_->AppendInst(inst);
    } else {
        block_->InsertAfter(inst, prev_);
    }
    prev_ = inst;
}

void InstAppender::Append(std::initializer_list<Inst *> instructions)
{
    for (auto *inst : instructions) {
        Append(inst);
    }
}

}  // namespace panda::compiler
