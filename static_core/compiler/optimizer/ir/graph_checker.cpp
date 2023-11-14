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

#include "compiler_options.h"
#include "graph_checker.h"
#include "graph_cloner.h"
#include "optimizer/analysis/dominators_tree.h"
#include "optimizer/analysis/rpo.h"
#include "optimizer/analysis/linear_order.h"
#include "optimizer/analysis/loop_analyzer.h"
#include "optimizer/ir/analysis.h"
#include "optimizer/ir/datatype.h"
#include "optimizer/ir/inst.h"
#include "optimizer/optimizations/cleanup.h"
#include "inst_checker_gen.h"

namespace panda::compiler {

// ---- Below extended ASSERT and ASSERT_DO for GraphChecker ----

// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define ASSERT_DO_EXT(cond, func) ASSERT_DO(cond, func; PrintFailedMethodAndPass();)

// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define ASSERT_DO_EXT_VISITOR(cond, func) ASSERT_DO(cond, func; PrintFailedMethodAndPassVisitor(v);)

// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define ASSERT_EXT(cond) ASSERT_DO_EXT(cond, )

// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define ASSERT_EXT_VISITOR(cond) ASSERT_DO_EXT_VISITOR(cond, )

// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define ASSERT_EXT_PRINT(cond, message) ASSERT_DO(cond, std::cerr << message << std::endl; PrintFailedMethodAndPass();)

// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define ASSERT_EXT_PRINT_VISITOR(cond, message) \
    ASSERT_DO(cond, std::cerr << message << std::endl; PrintFailedMethodAndPassVisitor(v);)
// --------------------------------------------------------------

GraphChecker::GraphChecker(Graph *graph)
{
    graph_ = graph;
    PreCloneChecks(graph);
    graph_ = GraphCloner(graph, GetAllocator(), GetLocalAllocator()).CloneGraph();
    GetGraph()->GetPassManager()->SetCheckMode(true);
    pass_name_ = std::string("");
}

GraphChecker::GraphChecker(Graph *graph, const char *pass_name)
{
    ASSERT_EXT(pass_name != nullptr);
    graph_ = graph;
    PreCloneChecks(graph);
    graph_ = GraphCloner(graph, GetAllocator(), GetLocalAllocator()).CloneGraph();
    GetGraph()->GetPassManager()->SetCheckMode(true);
    pass_name_ = std::string(pass_name);
}

void GraphChecker::PreCloneChecks(Graph *graph)
{
    UserInputCheck(graph);
}

void GraphChecker::UserInputCheck(Graph *graph)
{
    for (auto block : graph->GetVectorBlocks()) {
        if (block == nullptr) {
            continue;
        }
        for (auto inst : block->AllInsts()) {
            auto u = inst->GetFirstUser();
            ASSERT_EXT(u == nullptr || u->GetPrev() == nullptr);
            while (u != nullptr) {
                ASSERT_EXT(u->GetNext() == nullptr || u->GetNext()->GetPrev() == u);
                u = u->GetNext();
            }
            for (auto &user : inst->GetUsers()) {
                [[maybe_unused]] auto user_inst = user.GetInst();
                ASSERT_EXT(user_inst->GetBasicBlock() != nullptr);
                ASSERT_DO_EXT(CheckInstHasInput(user_inst, inst), std::cerr
                                                                      << "Instruction is not an input to its user\n"
                                                                      << "input: " << *inst << std::endl
                                                                      << "user:  " << *user_inst << std::endl);
            }
            for (auto &input : inst->GetInputs()) {
                [[maybe_unused]] auto input_inst = input.GetInst();
                ASSERT_DO_EXT(input_inst != nullptr && input_inst->GetBasicBlock() != nullptr,
                              std::cerr << "Instruction has invalid input:\n"
                                        << "user: " << *inst << std::endl);
                ASSERT_DO_EXT(CheckInstHasUser(input_inst, inst), std::cerr
                                                                      << "Instruction is not a user to its input:\n"
                                                                      << "user: " << *inst << std::endl
                                                                      << "input:  " << *input_inst << std::endl);
            }
            // Check `require_state` flag
            auto it = std::find_if(inst->GetInputs().begin(), inst->GetInputs().end(),
                                   [](Input input) { return input.GetInst()->IsSaveState(); });
            [[maybe_unused]] bool has_save_state = (it != inst->GetInputs().end());
            ASSERT_DO_EXT(inst->RequireState() == has_save_state,
                          std::cerr << "Incorrect 'require_state' flag in the inst: " << *inst);
            if (inst->RequireState()) {
                ASSERT_EXT(it->GetInst() == inst->GetSaveState());
            }
            if (inst->IsPhi()) {
                // causes failure in GraphCloner::BuildDataFlow if not checked before clone
                ASSERT_DO_EXT(inst->GetInputsCount() == block->GetPredsBlocks().size(),
                              std::cerr << "Incorrect phi's inputs count: " << inst->GetInputsCount() << " inputs, "
                                        << block->GetPredsBlocks().size() << " pred blocks\n"
                                        << *inst);
            }
        }
    }
}

void GraphChecker::Check()
{
    InstChecker::Run(GetGraph());

#ifndef NDEBUG
    if (GetGraph()->IsAnalysisValid<DominatorsTree>()) {
        CheckDomTree();
    } else {
        GetGraph()->RunPass<DominatorsTree>();
    }
    if (GetGraph()->IsAnalysisValid<LoopAnalyzer>()) {
        CheckLoopAnalysis();
    } else {
        GetGraph()->RunPass<LoopAnalyzer>();
    }
    CheckStartBlock();
    CheckEndBlock();
    size_t blocks_count = 0;
    size_t blocks_id = -1;
    for (auto block : GetGraph()->GetVectorBlocks()) {
        ++blocks_id;
        if (block == nullptr) {
            continue;
        }
        ASSERT_EXT_PRINT(block->GetGraph() == GetGraph(), "Block linked to incorrect graph");
        ASSERT_EXT_PRINT(block->GetId() == blocks_id, "Block ID must be equal to its ID in graph vector");
        CheckBlock(block);
        blocks_count++;
    }
    ASSERT_EXT_PRINT(blocks_count == GetGraph()->GetBlocksRPO().size(), "There is disconnected block");
    CheckLoops();
    // Visit graph to check instructions types
    CheckGraph();
    // Check that call.Inlined and Return.Inlined in correct order
    // and check that savestate has correct link to call.inlined.
    CheckCallReturnInlined();
    if (NeedCheckSaveState()) {
        // Check that objects in stack.
        CheckSaveStateInputs();
        // Check that between savestate and it's runtime call user have not reference insts.
        CheckSaveStatesWithRuntimeCallUsers();
    }

#endif  // !NDEBUG
}

#ifndef NDEBUG
bool GraphChecker::NeedCheckSaveState()
{
    return !GetGraph()->IsBytecodeOptimizer() && GetGraph()->GetParentGraph() == nullptr &&
           GetGraph()->IsInliningComplete();
}
#endif  // !NDEBUG

void GraphChecker::CheckBlock([[maybe_unused]] BasicBlock *block)
{
#ifndef NDEBUG
    CheckControlFlow(block);
    CheckDataFlow(block);
    for (auto phi_inst : block->PhiInsts()) {
        CheckPhiInputs(phi_inst);
    }
    if (!GetGraph()->IsLowLevelInstructionsEnabled() && !GetGraph()->IsDynamicMethod()) {
        CheckNoLowLevel(block);
    }
    if (!block->IsEndBlock() && !block->IsStartBlock()) {
        CheckBlockEdges(*block);
    }
    if (block->IsTryBegin()) {
        CheckTryBeginBlock(*block);
    }
    if (block->NeedsJump()) {
        CheckJump(*block);
    }
#endif  // !NDEBUG
}

void GraphChecker::CheckControlFlow(BasicBlock *block)
{
    auto num_succs = block->GetSuccsBlocks().size();
    ASSERT_EXT_PRINT(block->IsEndBlock() || block->IsTryBegin() || block->IsTryEnd() ||
                         (num_succs > 0 && num_succs <= MAX_SUCCS_NUM) ||
                         block->GetLastInst()->GetOpcode() == Opcode::Throw,
                     "Non-end block and non-try-begin block should have 1 or 2 successesors");

    for ([[maybe_unused]] auto pred : block->GetPredsBlocks()) {
        ASSERT_DO_EXT(CheckBlockHasSuccessor(pred, block), std::cerr << "Block " << block->GetId()
                                                                     << " is not a successor to its predecessor "
                                                                     << pred->GetId());
    }
    for ([[maybe_unused]] auto succ : block->GetSuccsBlocks()) {
        ASSERT_EXT_PRINT(CheckBlockHasPredecessor(succ, block), "Block is not a predecessor to its successor");
    }

    if (num_succs == MAX_SUCCS_NUM) {
        ASSERT_EXT_PRINT(block->GetSuccessor(0) != block->GetSuccessor(1),
                         "Wrong CFG - block with two same successors");
    }
}

void GraphChecker::PrintFailedMethodAndPass() const
{
    auto method_name = GetGraph()->GetRuntime()->GetMethodFullName(GetGraph()->GetMethod(), false);
    std::cerr << std::endl
              << "Failed method: " << method_name << std::endl
              << (!GetPassName().empty() ? ("After pass: " + GetPassName() + "\n") : "");
}
void GraphChecker::PrintFailedMethodAndPassVisitor(GraphVisitor *v)
{
    auto graph_checker = static_cast<GraphChecker *>(v);
    auto method_name =
        graph_checker->GetGraph()->GetRuntime()->GetMethodFullName(graph_checker->GetGraph()->GetMethod(), false);
    std::cerr << std::endl
              << "Failed method: " << method_name << std::endl
              << (!graph_checker->GetPassName().empty() ? ("After pass: " + graph_checker->GetPassName() + "\n") : "");
}

void GraphChecker::CheckInputType(Inst *inst) const
{
    // TODO(mbolshov): Fix types for LiveOut in irtoc
    if (inst->GetOpcode() == Opcode::LiveOut) {
        return;
    }
    // TODO(dkofanov): Fix get input type for call insts
    if (inst->IsCall()) {
        return;
    }
    for (size_t i = 0; i < inst->GetInputsCount(); ++i) {
        [[maybe_unused]] auto input = inst->GetInput(i).GetInst();
        // TODO(mbolshov): Fix types for LiveIn in irtoc
        if (input->GetOpcode() == Opcode::LiveIn) {
            continue;
        }
        [[maybe_unused]] auto input_type = GetCommonType(inst->GetInputType(i));
        [[maybe_unused]] auto real_input_type = GetCommonType(input->GetType());
        ASSERT_DO_EXT(
            input_type == real_input_type ||
                (real_input_type == DataType::ANY &&
                 (input_type == DataType::REFERENCE || input_type == DataType::POINTER)) ||
                (IsZeroConstant(input) && (input_type == DataType::REFERENCE || input_type == DataType::POINTER)) ||
                (input_type == DataType::ANY && input->IsConst() && real_input_type == DataType::INT64),
            std::cerr << "Input type don't equal to real input type\n"
                      << "inst: " << *inst << std::endl
                      << "input type: " << DataType::ToString(input_type) << std::endl
                      << "input: " << *input << std::endl);
    }
}

uint32_t GetInliningDepth(Inst *inst)
{
    uint32_t inlining_depth = 0;
    auto ss = inst->IsSaveState() ? inst : inst->GetSaveState();
    while (ss != nullptr) {
        auto caller = static_cast<SaveStateInst *>(ss)->GetCallerInst();
        if (caller == nullptr) {
            break;
        }
        ss = caller->GetSaveState();
        inlining_depth++;
    }
    return inlining_depth;
}

void GraphChecker::CheckDataFlow(BasicBlock *block)
{
    auto graph = block->GetGraph();
    for (auto inst : block->AllInsts()) {
        ASSERT_DO_EXT(inst->GetBasicBlock() == block,
                      std::cerr << "Instruction block's pointer isn't correct" << *inst << std::endl);
        if (block != graph->GetStartBlock()) {
            ASSERT_DO_EXT(inst->GetOpcode() != Opcode::Parameter,
                          std::cerr << "Not entry block can't contain Parameter instructions" << *inst << std::endl);
        }
        if (inst->GetPrev() == nullptr) {
            ASSERT_EXT_PRINT(*block->AllInsts().begin() == inst, "First block instruction isn't correct");
        }
        if (inst->GetNext() == nullptr) {
            ASSERT_EXT_PRINT(*block->AllInstsSafeReverse().begin() == inst, "Last block instruction isn't correct");
        }
        CheckInputType(inst);

#ifndef NDEBUG
        if (GetGraph()->IsInliningComplete() && GetGraph()->GetParentGraph() == nullptr) {
            // Check depth
            ASSERT_DO_EXT(inst->GetInliningDepth() == GetInliningDepth(inst),
                          std::cerr << *inst << std::endl
                                    << "Current depth = " << inst->GetInliningDepth() << std::endl
                                    << "Correct depth = " << GetInliningDepth(inst) << std::endl);
        }
#endif

        for ([[maybe_unused]] auto &user : inst->GetUsers()) {
            auto user_inst = user.GetInst();
            ASSERT_DO_EXT(CheckInstHasInput(user_inst, inst), std::cerr << "Instruction is not an input to its user\n"
                                                                        << "input: " << *inst << std::endl
                                                                        << "user:  " << *user_inst << std::endl);
            if (!user_inst->IsPhi() && !user_inst->IsCatchPhi()) {
                ASSERT_DO_EXT(inst->IsDominate(user_inst) ||
                                  (GetGraph()->IsRegAllocApplied() &&
                                   IsTryCatchDomination(inst->GetBasicBlock(), user_inst->GetBasicBlock())),
                              std::cerr << "Instruction doesn't dominate its user\n"
                                        << "input: bb " << inst->GetBasicBlock()->GetId() << *inst << std::endl
                                        << "user: bb " << user_inst->GetBasicBlock()->GetId() << *user_inst
                                        << std::endl);
            }
            auto arch = graph->GetArch();
            if (DataType::Is32Bits(inst->GetType(), arch)) {
                if (!user_inst->HasType()) {
                    continue;
                }
                // Unsigned Load in AARCH64 zerod all high bits
#ifndef NDEBUG
                if (inst->IsLoad() && !DataType::IsTypeSigned(inst->GetType()) && arch == Arch::AARCH64 &&
                    graph->IsLowLevelInstructionsEnabled()) {
#else
                if (inst->IsLoad() && !DataType::IsTypeSigned(inst->GetType()) && arch == Arch::AARCH64) {
#endif  // !NDEBUG
                    continue;
                }
                [[maybe_unused]] auto user_input_type = user_inst->GetInputType(user.GetIndex());
                [[maybe_unused]] bool ref_to_ptr =
                    user_input_type == DataType::POINTER && inst->GetType() == DataType::REFERENCE;
                ASSERT_DO_EXT(DataType::Is32Bits(user_input_type, arch) || ref_to_ptr ||
                                  (block->GetGraph()->IsDynamicMethod() && user_input_type == DataType::ANY),
                              std::cerr << "Undefined high-part of input instruction for its user\n"
                                        << "input: " << *inst << std::endl
                                        << "user:  " << *user_inst << std::endl);
            }
        }
        for ([[maybe_unused]] auto input : inst->GetInputs()) {
            ASSERT_DO_EXT(CheckInstHasUser(input.GetInst(), inst), std::cerr
                                                                       << "Instruction is not a user to its input:\n"
                                                                       << "input: " << *input.GetInst() << std::endl
                                                                       << "user:  " << *inst << std::endl);
        }
    }
}

void GraphChecker::CheckCallReturnInlined()
{
    [[maybe_unused]] bool throw_exit = false;
    if (GetGraph()->HasEndBlock()) {
        for (auto block : GetGraph()->GetEndBlock()->GetPredsBlocks()) {
            if (block->IsTryEnd()) {
                continue;
            }
            if (block->IsEndWithThrowOrDeoptimize()) {
                throw_exit = true;
                break;
            }
        }
    }
    ArenaStack<Inst *> inlined_calles(GetLocalAllocator()->Adapter());
    for (auto block : GetGraph()->GetBlocksRPO()) {
        for (auto inst : block->Insts()) {
            if (inst->IsCall() && static_cast<CallInst *>(inst)->IsInlined()) {
                ASSERT_EXT_PRINT(inst->NoDest(), "Inlined call should have NO_DST flag");
                inlined_calles.push(inst);
            } else if (inst->GetOpcode() == Opcode::ReturnInlined) {
                // TODO(Sergey Chernykh) fix checker
                if (block->IsEndWithThrowOrDeoptimize()) {
                    continue;
                }
                ASSERT_EXT(!inlined_calles.empty());
                ASSERT(inlined_calles.top()->GetSaveState() == inst->GetSaveState() || throw_exit ||
                       GetGraph()->IsRegAllocApplied());
                inlined_calles.pop();
            }
        }
    }
    ASSERT_EXT(inlined_calles.empty() || throw_exit);
#ifndef NDEBUG
    // avoid check after ir_builder in inline pass
    if (!GetGraph()->IsInliningComplete() || GetGraph()->GetParentGraph() != nullptr) {
        return;
    }
    for (auto block : GetGraph()->GetBlocksRPO()) {
        for (auto inst : block->Insts()) {
            if (inst->IsSaveState()) {
                CheckSaveStateCaller(static_cast<SaveStateInst *>(inst));
            }
        }
    }
#endif
}

void GraphChecker::CheckSaveStateCaller(SaveStateInst *savestate)
{
    ASSERT_EXT(savestate != nullptr);
    auto block = savestate->GetBasicBlock();
    ArenaStack<Inst *> inlined_calles(GetLocalAllocator()->Adapter());
    auto caller = savestate->GetCallerInst();
    if (caller == nullptr) {
        return;
    }
    ASSERT_EXT(caller->GetBasicBlock() != nullptr);
    ASSERT_EXT(caller->GetBasicBlock()->GetGraph() == block->GetGraph());
    ASSERT(caller->IsInlined());
    auto dom_block = block;
    bool skip = true;
    for (auto inst : dom_block->InstsSafeReverse()) {
        if (inst == savestate) {
            skip = false;
        }
        if (skip) {
            continue;
        }
        if (inst->GetOpcode() == Opcode::ReturnInlined) {
            inlined_calles.push(inst);
        } else if (inst->IsCall() && static_cast<CallInst *>(inst)->IsInlined()) {
            if (!inlined_calles.empty()) {
                inlined_calles.pop();
            } else {
                ASSERT_EXT(caller == inst);
                return;
            }
        }
    }
    dom_block = dom_block->GetDominator();
    while (dom_block != nullptr) {
        for (auto inst : dom_block->InstsSafeReverse()) {
            if (inst->GetOpcode() == Opcode::ReturnInlined) {
                inlined_calles.push(inst);
            } else if (inst->IsCall() && static_cast<CallInst *>(inst)->IsInlined()) {
                if (!inlined_calles.empty()) {
                    inlined_calles.pop();
                } else {
                    ASSERT_EXT(caller == inst);
                    return;
                }
            }
        }
        dom_block = dom_block->GetDominator();
    }
    UNREACHABLE();
}

void GraphChecker::CheckStartBlock()
{
    [[maybe_unused]] Inst *has_nullptr = nullptr;
    [[maybe_unused]] int32_t last_num = -2;
    ASSERT_EXT(GetGraph()->GetStartBlock());
    ASSERT_EXT_PRINT(GetGraph()->GetStartBlock()->GetPredsBlocks().empty(), "Start block can't have predecessors");
    ASSERT_EXT_PRINT(GetGraph()->GetStartBlock()->GetSuccsBlocks().size() == 1,
                     "Start block should have one successor");
    for (auto inst : GetGraph()->GetStartBlock()->AllInsts()) {
        [[maybe_unused]] Opcode opc = inst->GetOpcode();
        ASSERT_DO_EXT(
            opc == Opcode::Constant || opc == Opcode::Parameter || opc == Opcode::SafePoint ||
                opc == Opcode::SpillFill || opc == Opcode::NullPtr || opc == Opcode::NOP || opc == Opcode::LiveIn,
            std::cerr
                << "Entry block can contain Constant, Parameter, NullPtr, SafePoint, NOP or SpillFill instructions"
                << *inst << std::endl);
        if (opc == Opcode::Parameter) {
            auto arg_num = inst->CastToParameter()->GetArgNumber();
            auto num = static_cast<int32_t>(arg_num);
            if (arg_num == ParameterInst::DYNAMIC_NUM_ARGS) {
                num = -1;
            }
            ASSERT_DO_EXT(
                last_num < num,
                std::cerr << "The argument number in the parameter must be greater than that of the previous parameter"
                          << *inst << std::endl);
            last_num = num;
        }
        if (opc == Opcode::NullPtr) {
            ASSERT_EXT_PRINT(has_nullptr == nullptr, "There should be not more than one NullPtr instruction");
            has_nullptr = inst;
        }
    }
}

void GraphChecker::CheckEndBlock()
{
    if (!GetGraph()->HasEndBlock()) {
        ASSERT_EXT_PRINT(HasOuterInfiniteLoop(), "Graph without infinite loops should have end block");
        return;
    }
    ASSERT_EXT_PRINT(GetGraph()->GetEndBlock()->GetSuccsBlocks().empty(), "End block can't have successors");
    [[maybe_unused]] auto iter = GetGraph()->GetEndBlock()->Insts();
    ASSERT_EXT_PRINT(iter.begin() == iter.end(), "End block can't have instructions");
}

void GraphChecker::CheckGraph()
{
    size_t num_inst = GetGraph()->GetCurrentInstructionId();
    ArenaVector<bool> inst_vec(num_inst, GetLocalAllocator()->Adapter());
    for (auto &bb : GetGraph()->GetBlocksRPO()) {
        for (auto inst : bb->AllInsts()) {
            auto id = inst->GetId();
            ASSERT_DO_EXT(id < num_inst, (std::cerr << "Instruction ID must be less than graph instruction counter: "
                                                    << num_inst << "\n",
                                          inst->Dump(&std::cerr)));
            ASSERT_DO_EXT(!inst_vec[id],
                          (std::cerr << "Instruction with same Id already exists:\n", inst->Dump(&std::cerr)));
            inst_vec[id] = true;
            ASSERT_DO_EXT(
                GetGraph()->IsDynamicMethod() || inst->GetType() != DataType::ANY,
                (std::cerr << "The type ANY is supported only for dynamic languages\n", inst->Dump(&std::cerr)));
            ASSERT_DO_EXT(inst->SupportsMode(GetGraph()->GetCompilerMode()),
                          (std::cerr << "Instruction used in wrong mode\n", inst->Dump(&std::cerr)));
            VisitInstruction(inst);
        }
    }
}

void GraphChecker::CheckPhiInputs(Inst *phi_inst)
{
    for (size_t index = 0; index < phi_inst->GetInputsCount(); ++index) {
        [[maybe_unused]] auto pred = phi_inst->CastToPhi()->GetPhiInputBb(index);
        [[maybe_unused]] auto input_bb = phi_inst->CastToPhi()->GetPhiInput(pred)->GetBasicBlock();
        ASSERT_DO_EXT(
            input_bb->IsDominate(pred) || (GetGraph()->IsRegAccAllocApplied() && IsTryCatchDomination(input_bb, pred)),
            (std::cerr
             << "Block where phi-input is located should dominate predecessor block corresponding to this input\n"
             << "Block " << input_bb->GetId() << " should dominate " << pred->GetId() << std::endl
             << *phi_inst));
    }
}

bool GraphChecker::CheckInstRegUsageSaved(const Inst *inst, Register reg) const
{
    if (reg == ACC_REG_ID) {
        return true;
    }
    auto graph = inst->GetBasicBlock()->GetGraph();
    // Empty vector regs mask means we are using dynamic general regs set.
    if (DataType::IsFloatType(inst->GetType()) && !graph->GetUsedRegs<DataType::FLOAT64>()->empty()) {
        return graph->GetUsedRegs<DataType::FLOAT64>()->at(reg);
    }
    return graph->GetUsedRegs<DataType::INT64>()->at(reg);
}

[[maybe_unused]] static bool CheckSpillFillMultiple(const compiler::Inst *inst)
{
    switch (inst->GetOpcode()) {
        case Opcode::Parameter:
            return false;
        case Opcode::LoadObject:
        case Opcode::NewArray:
        case Opcode::NewObject:
            // In this case for BytecodeOptimizer SpillFill will be added after instruction, not before
            // user-insturction. So this check can't find it and it is skipped.
            return !inst->GetBasicBlock()->GetGraph()->IsBytecodeOptimizer();
        default:
            return true;
    }
}

void GraphChecker::CheckNoLowLevel(BasicBlock *block)
{
    for ([[maybe_unused]] auto inst : block->Insts()) {
        ASSERT_DO_EXT(!inst->IsLowLevel(), inst->Dump(&std::cerr));
    }
}

void GraphChecker::MarkBlocksInLoop(Loop *loop, Marker mrk)
{
    ASSERT_EXT(loop->IsIrreducible() || loop->IsRoot() || loop->GetHeader() != nullptr);
    ASSERT_EXT(loop->IsIrreducible() || loop->IsRoot() || loop->IsTryCatchLoop() || loop->GetPreHeader() != nullptr);
    // Mark blocks and check if marker was not set before
    for ([[maybe_unused]] auto block : loop->GetBlocks()) {
        ASSERT_EXT(!block->SetMarker(mrk));
    }

    for (auto inner : loop->GetInnerLoops()) {
        MarkBlocksInLoop(inner, mrk);
    }
}

bool GraphChecker::CheckBlockHasPredecessor(BasicBlock *block, BasicBlock *predecessor)
{
    ASSERT_EXT(block != nullptr && predecessor != nullptr);
    for (auto pred : block->GetPredsBlocks()) {
        if (pred == predecessor) {
            return true;
        }
    }
    return false;
}

bool GraphChecker::CheckBlockHasSuccessor(BasicBlock *block, BasicBlock *successor)
{
    ASSERT_EXT(block != nullptr && successor != nullptr);
    for (auto succ : block->GetSuccsBlocks()) {
        if (succ == successor) {
            return true;
        }
    }
    return false;
}

bool GraphChecker::BlockContainsInstruction(BasicBlock *block, Opcode opcode)
{
    return std::find_if(block->Insts().begin(), block->Insts().end(),
                        [opcode](Inst *inst) { return inst->GetOpcode() == opcode; }) != block->Insts().end();
}

void GraphChecker::CheckLoopHasSafePoint(Loop *loop)
{
    [[maybe_unused]] auto it =
        std::find_if(loop->GetBlocks().begin(), loop->GetBlocks().end(),
                     [this](BasicBlock *block) { return BlockContainsInstruction(block, Opcode::SafePoint); });
    // Irreducible isn't fully populated - only 'one of the headers' and back-edge,
    // SafePoint can be inserted to the another 'header' and search will be failed
    ASSERT_DO_EXT(loop->IsTryCatchLoop() || loop->IsIrreducible() || it != loop->GetBlocks().end(),
                  std::cerr << "Loop " << loop->GetId() << " must have safepoint\n");
    for (auto inner : loop->GetInnerLoops()) {
        CheckLoopHasSafePoint(inner);
    }
}

void GraphChecker::CheckLoops()
{
    ASSERT_EXT(GetGraph()->GetAnalysis<LoopAnalyzer>().IsValid());
    ASSERT_EXT(GetGraph()->GetRootLoop() != nullptr);
    ASSERT_EXT(GetGraph()->GetRootLoop()->IsRoot());
    ASSERT_EXT(GetGraph()->GetRootLoop()->GetHeader() == nullptr);
    ASSERT_EXT(GetGraph()->GetRootLoop()->GetPreHeader() == nullptr);
    auto root_loop = GetGraph()->GetRootLoop();
    auto mrk = GetGraph()->NewMarker();
    MarkBlocksInLoop(root_loop, mrk);

    for ([[maybe_unused]] auto block : GetGraph()->GetBlocksRPO()) {
        [[maybe_unused]] auto loop = block->GetLoop();
        ASSERT_EXT(loop != nullptr);
        ASSERT_EXT(block->IsMarked(mrk));
        if (block->IsLoopHeader()) {
            if (block->IsOsrEntry()) {
                ASSERT_EXT(GetGraph()->IsOsrMode());
                auto ss_osr = block->GetFirstInst();
                while (ss_osr != nullptr && (ss_osr->IsCatchPhi() || ss_osr->GetOpcode() == Opcode::Try)) {
                    ss_osr = ss_osr->GetNext();
                }
                ASSERT_EXT(ss_osr != nullptr && ss_osr->GetOpcode() == Opcode::SaveStateOsr);
            }
            [[maybe_unused]] auto preds = block->GetPredsBlocks();
            for ([[maybe_unused]] auto pred : preds) {
                ASSERT_EXT(pred->GetLoop() != loop || loop->HasBackEdge(pred));
            }

            if (!loop->IsIrreducible()) {
                for ([[maybe_unused]] auto back : loop->GetBackEdges()) {
                    ASSERT_EXT(std::find(preds.begin(), preds.end(), back) != preds.end());
                }
            }
        } else {
            ASSERT_EXT(!block->IsOsrEntry());
        }
    }
    GetGraph()->EraseMarker(mrk);
    if (OPTIONS.IsCompilerUseSafepoint() && GetGraph()->SupportManagedCode()) {
        for (auto inner : root_loop->GetInnerLoops()) {
            CheckLoopHasSafePoint(inner);
        }
    }
}

void GraphChecker::CheckDomTree()
{
    ASSERT_EXT(GetGraph()->GetAnalysis<DominatorsTree>().IsValid());
    ArenaVector<BasicBlock *> dominators(GetGraph()->GetVectorBlocks().size(), GetLocalAllocator()->Adapter());
    for (auto block : GetGraph()->GetBlocksRPO()) {
        dominators[block->GetId()] = block->GetDominator();
    }
    // Rebuild dom-tree
    GetGraph()->InvalidateAnalysis<DominatorsTree>();
    GetGraph()->RunPass<DominatorsTree>();

    for ([[maybe_unused]] auto block : GetGraph()->GetBlocksRPO()) {
        ASSERT_DO_EXT(
            dominators[block->GetId()] == block->GetDominator(),
            std::cerr << "Basic block with id " << block->GetId() << " has incorrect dominator with id "
                      << dominators[block->GetId()]->GetId() << std::endl
                      << "Correct dominator must be block with id " << block->GetDominator()->GetId() << std::endl
                      << "Note: basic blocks' ids in the original graph and in the cloned graph can be different"
                      << std::endl);
    }
}

void GraphChecker::CheckLoopAnalysis()
{
    // Save current loop info
    ArenaUnorderedMap<BasicBlock *, Loop *> loops(GetLocalAllocator()->Adapter());
    [[maybe_unused]] auto root_loop = GetGraph()->GetRootLoop();
    for (auto block : GetGraph()->GetBlocksRPO()) {
        if (block->IsLoopHeader()) {
            loops.emplace(block, block->GetLoop());
        }
    }
    for (auto block : GetGraph()->GetBlocksRPO()) {
        if (block->GetSuccsBlocks().size() != 2U) {
            continue;
        }
        [[maybe_unused]] auto loop = block->GetLoop();
        ASSERT_DO_EXT(loop->IsIrreducible() || block->GetFalseSuccessor()->GetLoop() == loop ||
                          block->GetFalseSuccessor()->GetLoop()->GetOuterLoop() == loop ||
                          block->GetTrueSuccessor()->GetLoop() == loop ||
                          block->GetTrueSuccessor()->GetLoop()->GetOuterLoop() == loop,
                      std::cerr << "One of successors of loop exit basic block id " << block->GetId()
                                << " must be in same or inner loop" << std::endl);
    }

    // Build new loop info and compare with saved one
    GetGraph()->InvalidateAnalysis<LoopAnalyzer>();
    GetGraph()->RunPass<LoopAnalyzer>();
    ASSERT_EXT_PRINT(*root_loop == *GetGraph()->GetRootLoop(), "Root loop is incorrect\n");
    for (auto &[block, loop] : loops) {
        auto expected_loop = block->GetLoop();
        // An irreducible loop can have different heads, depending on the order of traversal
        if (loop->IsIrreducible()) {
            ASSERT_EXT(expected_loop->IsIrreducible());
            continue;
        }
        ASSERT_EXT(block->IsLoopHeader());
        if (loop == nullptr || expected_loop == nullptr) {
            UNREACHABLE();
            return;
        }
        ASSERT_DO_EXT(*loop == *expected_loop, std::cerr << "Loop " << loop->GetId() << " is incorrect\n");
    }
}

/// Check that there is root's inner loop without exit-points
bool GraphChecker::HasOuterInfiniteLoop()
{
    const auto &loops = GetGraph()->GetRootLoop()->GetInnerLoops();
    return std::find_if(loops.begin(), loops.end(), [](const Loop *loop) { return loop->IsInfinite(); }) != loops.end();
}

bool GraphChecker::CheckInstHasInput(Inst *inst, Inst *input)
{
    ASSERT_EXT(inst != nullptr && input != nullptr);
    ASSERT_EXT(input->GetBasicBlock() != nullptr);
    ASSERT_EXT(input->GetBasicBlock()->GetGraph() != nullptr);
    for (auto node : inst->GetInputs()) {
        if (node.GetInst() == input) {
            return true;
        }
    }
    return false;
}

bool GraphChecker::CheckInstHasUser(Inst *inst, Inst *user)
{
    ASSERT_EXT(inst != nullptr && user != nullptr);
    ASSERT_EXT(user->GetBasicBlock() != nullptr);
    ASSERT_EXT(user->GetBasicBlock()->GetGraph() != nullptr);
    for (auto &node : inst->GetUsers()) {
        if (node.GetInst() == user) {
            return true;
        }
    }
    return false;
}

void GraphChecker::CheckBlockEdges(const BasicBlock &block)
{
    [[maybe_unused]] auto last_inst_in_block = block.GetLastInst();
    if (block.GetSuccsBlocks().size() > 1) {
        ASSERT_EXT_PRINT(!block.IsEmpty() || block.IsTryEnd(),
                         "Block with 2 successors have no instructions or should be try-end");
        ASSERT_EXT_PRINT(block.IsTryBegin() || block.IsTryEnd() || last_inst_in_block->IsControlFlow(),
                         "Last instruction must be control flow in block with 2 successors");
    } else if (block.GetSuccsBlocks().size() == 1) {
        if (block.GetSuccsBlocks()[0]->IsEndBlock()) {
            if (block.IsEmpty()) {
                ASSERT_EXT(block.IsTryEnd());
                return;
            }
            auto last_inst = block.GetLastInst();
            [[maybe_unused]] auto opc = last_inst->GetOpcode();
            ASSERT_EXT_PRINT(last_inst->GetFlag(inst_flags::TERMINATOR),
                             "Last instruction in block before exit-block must be Return or Throw instruction.");
        }
    }
}

void GraphChecker::CheckTryBeginBlock(const BasicBlock &block)
{
    ASSERT_EXT(block.IsTryBegin());
    auto try_inst_it = std::find_if(block.AllInsts().begin(), block.AllInsts().end(),
                                    [](Inst *inst) { return inst->GetOpcode() == Opcode::Try; });
    ASSERT_EXT_PRINT(try_inst_it != block.AllInsts().end(), "Try-begin basic block should contain try-instructions");
    [[maybe_unused]] auto try_inst = (*try_inst_it)->CastToTry();
    for ([[maybe_unused]] auto succ_index : *try_inst->GetCatchEdgeIndexes()) {
        ASSERT_EXT_PRINT(succ_index < block.GetSuccsBlocks().size(),
                         "Try instruction holds incorrect try-begin block successor number");
    }
}

void GraphChecker::CheckJump(const BasicBlock &block)
{
    ASSERT_EXT(GetGraph()->IsRegAllocApplied());
    ASSERT_EXT(GetGraph()->IsAnalysisValid<LinearOrder>());
    if (block.IsIfBlock()) {
        const auto &blocks_vector = GetGraph()->GetBlocksLinearOrder();
        auto if_block_it = std::find(blocks_vector.begin(), blocks_vector.end(), &block);
        ASSERT_EXT(if_block_it != blocks_vector.end());
        auto block_after_if = std::next(if_block_it);
        if (block_after_if != blocks_vector.end()) {
            ASSERT_EXT_PRINT(*block_after_if != (*if_block_it)->GetFalseSuccessor(),
                             "`If-block` with immediate `false`-successor shouldn't have `JumpFlag`");
            ASSERT_EXT_PRINT(*block_after_if != (*if_block_it)->GetTrueSuccessor(),
                             "`true`-successor should be replaced with `false`-successor");
        }
    }
    [[maybe_unused]] auto num_succs = block.GetSuccsBlocks().size();
    ASSERT_EXT_PRINT(num_succs == 1 || block.IsTryBegin() || block.IsTryEnd() || block.IsIfBlock(),
                     "Basic block with Jump must have 1 successor or should be try-begin or if block");
}

/**
 * Regalloc propagates catch-phi's inputs to the users and can broke user's domination. In this case:
 * - input_block should be placed inside try block;
 * - try-begin block should dominate user_block;
 *
 * [try-begin]----------\
 *     |                |
 * [input_block]        |
 *     |                |
 * [try-end]----------->|
 *                      |
 *                [catch-begin]
 *                      |
 *                [user_block]
 */
bool GraphChecker::IsTryCatchDomination(const BasicBlock *input_block, const BasicBlock *user_block) const
{
    if (!GetGraph()->IsRegAllocApplied()) {
        return false;
    }
    if (input_block->IsTry()) {
        auto blocks = GetGraph()->GetTryBeginBlocks();
        auto it =
            std::find_if(blocks.begin(), blocks.end(), [user_block](auto &bb) { return bb->IsDominate(user_block); });
        return it != blocks.end();
    }
    return false;
}

#ifndef NDEBUG
static inline bool AllUsersDominate(Inst *inst1, Inst *inst2)
{
    for (auto &user : inst1->GetUsers()) {
        if (!user.GetInst()->IsDominate(inst2)) {
            return false;
        }
    }
    return true;
}
#endif

// It is necessary to check because if reference instruction (it is some object) will contain between SaveState and some
// RuntimeCall user of SaveState, during startup in runtime call GC can be triggered and move our object, so we
// will lost them, his new position (object is not contained in SaveState).
void GraphChecker::CheckSaveStatesWithRuntimeCallUsers()
{
#ifndef NDEBUG
    for (auto &block : GetGraph()->GetBlocksRPO()) {
        for (const auto &ss : block->AllInsts()) {
            if (ss->GetOpcode() != Opcode::SaveState) {
                continue;
            }
            for (auto &user : ss->GetUsers()) {
                auto user_inst = user.GetInst();
                if (!user_inst->IsRuntimeCall() || user_inst->IsCheck()) {
                    continue;
                }
                ASSERT(user_inst->GetBasicBlock() == ss->GetBasicBlock());
                auto it = InstSafeIterator<IterationType::ALL, IterationDirection::BACKWARD>(*block, user_inst);
                for (++it; *it != ss; ++it) {
                    if ((*it)->GetOpcode() == Opcode::CastAnyTypeValue) {
                        continue;
                    }
                    // Non-reference instructions, checks, nullptr and classes cannot be moved by GC
                    // The correct state when all users of Object(is "*it") between SaveState and RuntimeCall(is
                    // "user_inst")
                    ASSERT_DO_EXT(!(*it)->IsMovableObject() || AllUsersDominate(*it, user_inst),
                                  std::cerr << "We have inst v" << (*it)->GetId()
                                            << " which is located between SaveState v" << ss->GetId()
                                            << " and RuntimeCall v" << user_inst->GetId() << ":\n"
                                            << **it << std::endl
                                            << *ss << std::endl
                                            << *user_inst << std::endl);
                }
            }
        }
    }
#endif
}

#ifndef NDEBUG
void GraphChecker::PrepareUsers(Inst *inst, ArenaVector<User *> *users)
{
    for (auto &user : inst->GetUsers()) {
        users->push_back(&user);
    }
    auto i = std::find_if(users->begin(), users->end(), [](User *user) { return user->GetInst()->IsCheck(); });
    while (i != users->end()) {
        auto user_inst = (*i)->GetInst();
        // erase before push_backs to avoid iterator invalidation
        users->erase(i);
        // skip AnyTypeChecks with primitive type
        if (user_inst->GetOpcode() != Opcode::AnyTypeCheck || user_inst->IsReferenceOrAny()) {
            for (auto &u : user_inst->GetUsers()) {
                users->push_back(&u);
            }
        }
        i = std::find_if(users->begin(), users->end(), [](User *user) { return user->GetInst()->IsCheck(); });
    }
    for (auto &it : (*users)) {
        [[maybe_unused]] auto user = it->GetInst();
        ASSERT_EXT(!user->IsCheck());
    }
}
#endif

#ifndef NDEBUG
// If all indirect (through other Phis) inputs of Phi cannot be moved by GC, result of Phi itself
// also cannot be moved (and does not need to be checked in CheckSaveStateInputs as input of its users)
bool GraphChecker::IsPhiSafeToSkipObjectCheck(Inst *inst, Marker visited)
{
    ASSERT_EXT(inst->IsPhi());
    ASSERT_EXT(visited != UNDEF_MARKER);
    if (inst->SetMarker(visited)) {
        return true;
    }
    for (auto &input : inst->GetInputs()) {
        auto input_inst = input.GetInst();
        if (input_inst->IsPhi()) {
            if (!IsPhiSafeToSkipObjectCheck(input_inst, visited)) {
                return false;
            }
        } else if (input_inst->IsMovableObject() || input_inst->IsCheck()) {
            // Check instructions with reference type do not need to be saved in SaveState themselves,
            // but we need to return false here for them (because their result can be moved by GC)
            return false;
        }
    }
    return true;
}

// If all indirect (through other Phis) users of Phi are SaveState instructions, result of Phi
// is not used in compiled code (and does not need to be checked in CheckSaveStateInputs as user of its inputs)
bool GraphChecker::IsPhiUserSafeToSkipObjectCheck(Inst *inst, Marker visited)
{
    ASSERT_EXT(inst->IsPhi());
    ASSERT_EXT(visited != UNDEF_MARKER);
    if (inst->SetMarker(visited)) {
        return true;
    }
    for (auto &user : inst->GetUsers()) {
        auto user_inst = user.GetInst();
        if (user_inst->IsSaveState() || user_inst->IsCatchPhi() ||
            (user_inst->IsPhi() && IsPhiUserSafeToSkipObjectCheck(user_inst, visited))) {
            continue;
        }
        return false;
    }
    return true;
}
#endif

void GraphChecker::CheckSaveStateInputs()
{
#ifndef NDEBUG
    ArenaVector<User *> users(GetLocalAllocator()->Adapter());
    for (auto &block : GetGraph()->GetBlocksRPO()) {
        for (const auto &inst : block->AllInsts()) {
            if (!inst->IsMovableObject()) {
                continue;
            }
            // true if we do not need check for this input (because it cannot be moved by GC)
            bool skip_obj_check = false;
            if (inst->GetOpcode() == Opcode::Phi) {
                MarkerHolder visited(GetGraph());
                skip_obj_check = IsPhiSafeToSkipObjectCheck(inst, visited.GetMarker());
            }

            PrepareUsers(inst, &users);

            Inst *failed_ss = nullptr;
            MarkerHolder object_visited(GetGraph());
            MarkerHolder osr_visited(GetGraph());
            for (auto &it : users) {
                auto user = it->GetInst();
                if (user->IsCatchPhi()) {
                    continue;
                }
                // true if we do not need check for this user (because it is Phi used only in SaveStates)
                bool skip_obj_check_user = false;
                BasicBlock *start_bb;
                Inst *start_inst;
                if (user->IsPhi()) {
                    MarkerHolder visited(GetGraph());
                    skip_obj_check_user = IsPhiUserSafeToSkipObjectCheck(user, visited.GetMarker());
                    // check SaveStates on path between inst and the end of corresponding predecessor of Phi's block
                    start_bb = user->GetBasicBlock()->GetPredsBlocks()[it->GetBbNum()];
                    start_inst = start_bb->GetLastInst();
                } else {
                    // check SaveStates on path between inst and user
                    start_bb = user->GetBasicBlock();
                    start_inst = user->GetPrev();
                }
                ASSERT_DO_EXT(
                    skip_obj_check || skip_obj_check_user ||
                        CheckObjectRec(inst, user, start_bb, start_inst, object_visited.GetMarker(), &failed_ss),
                    std::cerr << "Object v" << inst->GetId() << " used in v" << user->GetId()
                              << ", but not found on the path between them in the " << failed_ss->GetOpcodeStr() << " v"
                              << failed_ss->GetId() << ":\n"
                              << *inst << std::endl
                              << *user << std::endl
                              << *failed_ss << std::endl);
                CheckSaveStateOsrRec(inst, user, start_bb, osr_visited.GetMarker());
            }
            users.clear();
        }
    }
#endif
}

void GraphChecker::CheckSaveStateOsrRec(const Inst *inst, const Inst *user, BasicBlock *block, Marker visited)
{
    if (block->SetMarker(visited)) {
        return;
    }
    if (inst->GetBasicBlock() == block) {
        return;
    }
    if (block->IsOsrEntry()) {
        ASSERT_EXT(GetGraph()->IsOsrMode());
        auto ss = block->GetFirstInst();
        ASSERT_EXT(ss != nullptr && ss->GetOpcode() == Opcode::SaveStateOsr);
        [[maybe_unused]] auto it =
            std::find_if(ss->GetInputs().begin(), ss->GetInputs().end(),
                         [inst, ss](Input input) { return ss->GetDataFlowInput(input.GetInst()) == inst; });
        ASSERT_DO_EXT(it != ss->GetInputs().end(), std::cerr << "Inst v" << inst->GetId() << " used in v"
                                                             << user->GetId()
                                                             << ", but not found on the path between them in the "
                                                             << ss->GetOpcodeStr() << " v" << ss->GetId() << ":\n"
                                                             << *inst << std::endl
                                                             << *user << std::endl
                                                             << *ss << std::endl);
    }
    for (auto pred : block->GetPredsBlocks()) {
        CheckSaveStateOsrRec(inst, user, pred, visited);
    }
}

/*
 * Visitors to check instructions types
 */
void GraphChecker::VisitMov([[maybe_unused]] GraphVisitor *v, Inst *inst)
{
    CheckUnaryOperationTypes(inst);
}
void GraphChecker::VisitNeg([[maybe_unused]] GraphVisitor *v, Inst *inst)
{
    CheckUnaryOperationTypes(inst);
}
void GraphChecker::VisitAbs([[maybe_unused]] GraphVisitor *v, Inst *inst)
{
    CheckUnaryOperationTypes(inst);
}
void GraphChecker::VisitSqrt([[maybe_unused]] GraphVisitor *v, Inst *inst)
{
    ASSERT_DO_EXT_VISITOR(DataType::IsFloatType(inst->GetType()),
                          (std::cerr << "\nSqrt must have float type\n", inst->Dump(&std::cerr)));
    CheckUnaryOperationTypes(inst);
}

void GraphChecker::VisitAddI([[maybe_unused]] GraphVisitor *v, Inst *inst)
{
    if (inst->GetType() == DataType::POINTER) {
        ASSERT_DO_EXT_VISITOR(inst->GetInputType(0) == DataType::POINTER ||
                                  inst->GetInputType(0) == DataType::REFERENCE,
                              (std::cerr << "\nptr AddI must have ptr or ref input type\n", inst->Dump(&std::cerr)));
        return;
    }
    ASSERT_DO_EXT_VISITOR(DataType::GetCommonType(inst->GetType()) == DataType::INT64,
                          (std::cerr << "\nAddI must have integer type\n", inst->Dump(&std::cerr)));
    CheckUnaryOperationTypes(inst);
}
void GraphChecker::VisitSubI([[maybe_unused]] GraphVisitor *v, Inst *inst)
{
    if (inst->GetType() == DataType::POINTER) {
        ASSERT_DO_EXT_VISITOR(inst->GetInputType(0) == DataType::POINTER ||
                                  inst->GetInputType(0) == DataType::REFERENCE,
                              (std::cerr << "\nptr SubI must have ptr or ref input type\n", inst->Dump(&std::cerr)));
        return;
    }
    ASSERT_DO_EXT_VISITOR(DataType::GetCommonType(inst->GetType()) == DataType::INT64,
                          (std::cerr << "\nSubI must have integer type\n", inst->Dump(&std::cerr)));
    CheckUnaryOperationTypes(inst);
}
void GraphChecker::VisitMulI([[maybe_unused]] GraphVisitor *v, Inst *inst)
{
    [[maybe_unused]] auto type = inst->GetType();
    ASSERT_DO_EXT_VISITOR(DataType::Is32Bits(type, static_cast<GraphChecker *>(v)->GetGraph()->GetArch()) &&
                              !DataType::IsReference(type),
                          (std::cerr << "\nMulI must have Int32 type\n", inst->Dump(&std::cerr)));
    CheckUnaryOperationTypes(inst);
}
void GraphChecker::VisitDivI([[maybe_unused]] GraphVisitor *v, Inst *inst)
{
    [[maybe_unused]] auto type = inst->GetType();
    ASSERT_DO_EXT_VISITOR(DataType::Is32Bits(type, static_cast<GraphChecker *>(v)->GetGraph()->GetArch()) &&
                              !DataType::IsReference(type),
                          (std::cerr << "\nDivI must have Int32 type\n", inst->Dump(&std::cerr)));
    CheckUnaryOperationTypes(inst);
}
void GraphChecker::VisitModI([[maybe_unused]] GraphVisitor *v, Inst *inst)
{
    [[maybe_unused]] auto type = inst->GetType();
    ASSERT_DO_EXT_VISITOR(DataType::Is32Bits(type, static_cast<GraphChecker *>(v)->GetGraph()->GetArch()) &&
                              !DataType::IsReference(type),
                          (std::cerr << "\nModI must have Int32 type\n", inst->Dump(&std::cerr)));
    CheckUnaryOperationTypes(inst);
}
void GraphChecker::VisitAndI([[maybe_unused]] GraphVisitor *v, Inst *inst)
{
    ASSERT_DO_EXT_VISITOR(DataType::GetCommonType(inst->GetType()) == DataType::INT64,
                          (std::cerr << "\nAndI must have integer type\n", inst->Dump(&std::cerr)));
    CheckUnaryOperationTypes(inst);
}
void GraphChecker::VisitOrI([[maybe_unused]] GraphVisitor *v, Inst *inst)
{
    ASSERT_DO_EXT_VISITOR(DataType::GetCommonType(inst->GetType()) == DataType::INT64,
                          (std::cerr << "\nOrI must have integer type\n", inst->Dump(&std::cerr)));
    CheckUnaryOperationTypes(inst);
}
void GraphChecker::VisitXorI([[maybe_unused]] GraphVisitor *v, Inst *inst)
{
    ASSERT_DO_EXT_VISITOR(DataType::GetCommonType(inst->GetType()) == DataType::INT64,
                          (std::cerr << "\nXorI must have integer type\n", inst->Dump(&std::cerr)));
    CheckUnaryOperationTypes(inst);
}
void GraphChecker::VisitShlI([[maybe_unused]] GraphVisitor *v, Inst *inst)
{
    ASSERT_DO_EXT_VISITOR(DataType::GetCommonType(inst->GetType()) == DataType::INT64,
                          (std::cerr << "\nShlI must have integer type\n", inst->Dump(&std::cerr)));
    CheckUnaryOperationTypes(inst);
    [[maybe_unused]] auto imm = static_cast<BinaryImmOperation *>(inst)->GetImm();
    ASSERT_DO_EXT_VISITOR(
        imm <= DataType::GetTypeSize(inst->GetType(), static_cast<GraphChecker *>(v)->GetGraph()->GetArch()),
        (std::cerr << "\nShlI have shift more then size of type\n", inst->Dump(&std::cerr)));
}
void GraphChecker::VisitShrI([[maybe_unused]] GraphVisitor *v, Inst *inst)
{
    ASSERT_DO_EXT_VISITOR(DataType::GetCommonType(inst->GetType()) == DataType::INT64,
                          (std::cerr << "\nShrI must have integer type\n", inst->Dump(&std::cerr)));
    CheckUnaryOperationTypes(inst);
    [[maybe_unused]] auto imm = static_cast<BinaryImmOperation *>(inst)->GetImm();
    ASSERT_DO_EXT_VISITOR(
        imm <= DataType::GetTypeSize(inst->GetType(), static_cast<GraphChecker *>(v)->GetGraph()->GetArch()),
        (std::cerr << "\nShrI have shift more then size of type\n", inst->Dump(&std::cerr)));
}
void GraphChecker::VisitAShlI([[maybe_unused]] GraphVisitor *v, Inst *inst)
{
    ASSERT_DO_EXT_VISITOR(DataType::GetCommonType(inst->GetType()) == DataType::INT64,
                          (std::cerr << "\nAShrI must have integer type\n", inst->Dump(&std::cerr)));
    CheckUnaryOperationTypes(inst);
    [[maybe_unused]] auto imm = static_cast<BinaryImmOperation *>(inst)->GetImm();
    ASSERT_DO_EXT_VISITOR(
        imm <= DataType::GetTypeSize(inst->GetType(), static_cast<GraphChecker *>(v)->GetGraph()->GetArch()),
        (std::cerr << "\nAShlI have shift more then size of type\n", inst->Dump(&std::cerr)));
}
void GraphChecker::VisitNot([[maybe_unused]] GraphVisitor *v, Inst *inst)
{
    ASSERT_DO_EXT_VISITOR(DataType::GetCommonType(inst->GetType()) == DataType::INT64,
                          (std::cerr << "\nNot must have integer type\n", inst->Dump(&std::cerr)));
    CheckUnaryOperationTypes(inst);
}

// Size does't check after lowering, because for example lowering for Arm64 can remove some casts (ex. u32->u64)
bool IsIntWithPointerSize([[maybe_unused]] DataType::Type type, [[maybe_unused]] Graph *graph)
{
#ifndef NDEBUG
    auto arch = graph->GetArch();
    return DataType::GetCommonType(type) == DataType::INT64 &&
           (DataType::GetTypeSize(type, arch) == DataType::GetTypeSize(DataType::POINTER, arch) ||
            graph->IsLowLevelInstructionsEnabled());
#else
    return true;
#endif
}

void GraphChecker::VisitAdd([[maybe_unused]] GraphVisitor *v, Inst *inst)
{
    auto graph = static_cast<GraphChecker *>(v)->GetGraph();
    if (!graph->SupportManagedCode() && inst->GetType() == DataType::POINTER) {
        [[maybe_unused]] auto type_1 = inst->GetInput(0).GetInst()->GetType();
        [[maybe_unused]] auto type_2 = inst->GetInput(1).GetInst()->GetType();
        ASSERT_DO_EXT_VISITOR(type_1 != type_2,
                              (std::cerr << "\nptr Add must have ptr and int input types\n", inst->Dump(&std::cerr)));
        ASSERT_DO_EXT_VISITOR((type_1 == DataType::POINTER && IsIntWithPointerSize(type_2, graph)) ||
                                  (type_2 == DataType::POINTER && IsIntWithPointerSize(type_1, graph)),
                              (std::cerr << "\nptr Add must have ptr and int input types\n", inst->Dump(&std::cerr)));
        return;
    }
    CheckBinaryOperationTypes(inst);
}
void GraphChecker::VisitSub([[maybe_unused]] GraphVisitor *v, Inst *inst)
{
    auto graph = static_cast<GraphChecker *>(v)->GetGraph();
    if (!graph->SupportManagedCode()) {
        [[maybe_unused]] auto type_1 = inst->GetInput(0).GetInst()->GetType();
        [[maybe_unused]] auto type_2 = inst->GetInput(1).GetInst()->GetType();
        if (inst->GetType() == DataType::POINTER) {
            ASSERT_DO_EXT_VISITOR(type_1 != type_2, (std::cerr << "\nptr Sub must have ptr and int input types\n",
                                                     inst->Dump(&std::cerr)));
            ASSERT_DO_EXT_VISITOR(
                (type_1 == DataType::POINTER && IsIntWithPointerSize(type_2, graph)),
                (std::cerr << "\nptr Sub must have ptr and int input types\n", inst->Dump(&std::cerr)));
            return;
        }
        if (type_1 == DataType::POINTER && type_2 == DataType::POINTER) {
            ASSERT_DO_EXT_VISITOR(
                DataType::GetCommonType(inst->GetType()) == DataType::INT64 &&
                    IsIntWithPointerSize(inst->GetType(), graph),
                (std::cerr << "\n Sub with 2 ptr inputs must have int type\n", inst->Dump(&std::cerr)));
            return;
        }
    }
    CheckBinaryOperationTypes(inst);
}
void GraphChecker::VisitMul([[maybe_unused]] GraphVisitor *v, Inst *inst)
{
    CheckBinaryOperationTypes(inst);
}
void GraphChecker::VisitDiv([[maybe_unused]] GraphVisitor *v, Inst *inst)
{
    CheckBinaryOperationTypes(inst);
}
void GraphChecker::VisitMod([[maybe_unused]] GraphVisitor *v, Inst *inst)
{
    CheckBinaryOperationTypes(inst);
}
void GraphChecker::VisitMin([[maybe_unused]] GraphVisitor *v, Inst *inst)
{
    CheckBinaryOperationTypes(inst);
}
void GraphChecker::VisitMax([[maybe_unused]] GraphVisitor *v, Inst *inst)
{
    CheckBinaryOperationTypes(inst);
}
void GraphChecker::VisitShl([[maybe_unused]] GraphVisitor *v, Inst *inst)
{
    CheckBinaryOperationTypes(inst, true);
}
void GraphChecker::VisitShr([[maybe_unused]] GraphVisitor *v, Inst *inst)
{
    CheckBinaryOperationTypes(inst, true);
}
void GraphChecker::VisitAShr([[maybe_unused]] GraphVisitor *v, Inst *inst)
{
    CheckBinaryOperationTypes(inst, true);
}
void GraphChecker::VisitAnd([[maybe_unused]] GraphVisitor *v, Inst *inst)
{
    CheckBinaryOperationTypes(inst, true);
}
void GraphChecker::VisitOr([[maybe_unused]] GraphVisitor *v, Inst *inst)
{
    CheckBinaryOperationTypes(inst, true);
}
void GraphChecker::VisitXor([[maybe_unused]] GraphVisitor *v, Inst *inst)
{
    CheckBinaryOperationTypes(inst, true);
}

void GraphChecker::VisitAddOverflow([[maybe_unused]] GraphVisitor *v, Inst *inst)
{
    CheckBinaryOverflowOperation(inst->CastToAddOverflow());
}
void GraphChecker::VisitSubOverflow([[maybe_unused]] GraphVisitor *v, Inst *inst)
{
    CheckBinaryOverflowOperation(inst->CastToSubOverflow());
}
void GraphChecker::VisitLoadArray([[maybe_unused]] GraphVisitor *v, Inst *inst)
{
    CheckMemoryInstruction(inst);
}
void GraphChecker::VisitLoadArrayI([[maybe_unused]] GraphVisitor *v, Inst *inst)
{
    CheckMemoryInstruction(inst);
}
void GraphChecker::VisitLoadArrayPair([[maybe_unused]] GraphVisitor *v, Inst *inst)
{
    ASSERT_DO_EXT_VISITOR(MemoryCoalescing::AcceptedType(inst->GetType()) || DataType::IsReference(inst->GetType()),
                          (std::cerr << "Unallowed type of coalesced load\n", inst->Dump(&std::cerr)));
    CheckMemoryInstruction(inst);
}
void GraphChecker::VisitLoadArrayPairI([[maybe_unused]] GraphVisitor *v, Inst *inst)
{
    ASSERT_DO_EXT_VISITOR(MemoryCoalescing::AcceptedType(inst->GetType()) || DataType::IsReference(inst->GetType()),
                          (std::cerr << "Unallowed type of coalesced load\n", inst->Dump(&std::cerr)));
    CheckMemoryInstruction(inst);
}

void GraphChecker::VisitLoadPairPart([[maybe_unused]] GraphVisitor *v, Inst *inst)
{
    ASSERT_DO_EXT_VISITOR(MemoryCoalescing::AcceptedType(inst->GetType()) || DataType::IsReference(inst->GetType()),
                          (std::cerr << "Unallowed type of coalesced load\n", inst->Dump(&std::cerr)));
    CheckMemoryInstruction(inst);
    [[maybe_unused]] auto op1 = inst->GetInputs()[0].GetInst();
    [[maybe_unused]] auto idx = inst->CastToLoadPairPart()->GetImm();
    ASSERT_DO_EXT_VISITOR(op1->GetOpcode() == Opcode::LoadArrayPair || op1->GetOpcode() == Opcode::LoadArrayPairI,
                          (std::cerr << "Input instruction is not a Pair\n", inst->Dump(&std::cerr)));
    if (op1->GetOpcode() == Opcode::LoadArrayPairI) {
        ASSERT_DO_EXT_VISITOR(idx < op1->CastToLoadArrayPairI()->GetDstCount(),
                              (std::cerr << "Pair index is out of bounds\n", inst->Dump(&std::cerr)));
    } else {
        ASSERT_DO_EXT_VISITOR(idx < op1->CastToLoadArrayPair()->GetDstCount(),
                              (std::cerr << "Pair index is out of bounds\n", inst->Dump(&std::cerr)));
    }
    ASSERT_DO_EXT_VISITOR(
        CheckCommonTypes(inst, inst->GetInputs()[0].GetInst()),
        (std::cerr << "Types of load vector element and vector input are not compatible\n", inst->Dump(&std::cerr)));

    // Strict order here
    auto prev = inst->GetPrev();
    while (prev != nullptr && prev != op1) {
        if (prev->GetOpcode() == Opcode::LoadPairPart || prev->GetOpcode() == Opcode::SpillFill) {
            prev = prev->GetPrev();
        } else {
            break;
        }
    }
    ASSERT_DO_EXT_VISITOR(
        prev != nullptr && prev == op1,
        (std::cerr << "LoadPairPart(s) instructions must follow immediately after appropriate LoadArrayPair(I)\n",
         inst->Dump(&std::cerr)));
}

void GraphChecker::VisitStoreArrayPair([[maybe_unused]] GraphVisitor *v, Inst *inst)
{
    ASSERT_DO_EXT_VISITOR(MemoryCoalescing::AcceptedType(inst->GetType()) || DataType::IsReference(inst->GetType()),
                          (std::cerr << "Unallowed type of coalesced store\n", inst->Dump(&std::cerr)));
    CheckMemoryInstruction(inst);
    ASSERT_DO_EXT_VISITOR(
        CheckCommonTypes(inst, inst->GetInputs()[2U].GetInst()),
        (std::cerr << "Types of store and the first stored value are not compatible\n", inst->Dump(&std::cerr)));
    ASSERT_DO_EXT_VISITOR(
        CheckCommonTypes(inst, inst->GetInputs()[3U].GetInst()),
        (std::cerr << "Types of store and the second stored value are not compatible\n", inst->Dump(&std::cerr)));
    [[maybe_unused]] bool need_barrier = inst->CastToStoreArrayPair()->GetNeedBarrier();
    ASSERT_DO_EXT_VISITOR(need_barrier == (inst->GetType() == DataType::REFERENCE) || inst->GetType() == DataType::ANY,
                          (std::cerr << "StoreArrayPair has incorrect value NeedBarrier", inst->Dump(&std::cerr)));
}

void GraphChecker::VisitStoreArrayPairI([[maybe_unused]] GraphVisitor *v, Inst *inst)
{
    bool need_barrier = inst->CastToStoreArrayPairI()->GetNeedBarrier();
    ASSERT_DO_EXT_VISITOR(MemoryCoalescing::AcceptedType(inst->GetType()) || DataType::IsReference(inst->GetType()),
                          (std::cerr << "Unallowed type of coalesced store\n", inst->Dump(&std::cerr)));
    CheckMemoryInstruction(inst, need_barrier);
    ASSERT_DO_EXT_VISITOR(
        CheckCommonTypes(inst, inst->GetInputs()[1].GetInst()),
        (std::cerr << "Types of store and the first stored value are not compatible\n", inst->Dump(&std::cerr)));
    ASSERT_DO_EXT_VISITOR(
        CheckCommonTypes(inst, inst->GetInputs()[2U].GetInst()),
        (std::cerr << "Types of store and the second stored value are not compatible\n", inst->Dump(&std::cerr)));
    ASSERT_DO_EXT_VISITOR(need_barrier == (inst->GetType() == DataType::REFERENCE) || inst->GetType() == DataType::ANY,
                          (std::cerr << "StoreArrayPairI has incorrect value NeedBarrier", inst->Dump(&std::cerr)));
}

void GraphChecker::VisitStore([[maybe_unused]] GraphVisitor *v, Inst *inst)
{
    bool need_barrier = inst->CastToStore()->GetNeedBarrier();
    CheckMemoryInstruction(inst, need_barrier);
}

void GraphChecker::VisitStoreI([[maybe_unused]] GraphVisitor *v, Inst *inst)
{
    bool need_barrier = inst->CastToStoreI()->GetNeedBarrier();
    CheckMemoryInstruction(inst, need_barrier);
}

void GraphChecker::VisitStoreArray([[maybe_unused]] GraphVisitor *v, Inst *inst)
{
    bool need_barrier = inst->CastToStoreArray()->GetNeedBarrier();
    CheckMemoryInstruction(inst, need_barrier);
    ASSERT_DO_EXT_VISITOR(CheckCommonTypes(inst, inst->GetInputs()[2U].GetInst()),
                          (std::cerr << "Types of store and store input are not compatible\n", inst->Dump(&std::cerr)));
    ASSERT_DO_EXT_VISITOR(need_barrier == (inst->GetType() == DataType::REFERENCE) || inst->GetType() == DataType::ANY,
                          (std::cerr << "StoreArray has incorrect value NeedBarrier", inst->Dump(&std::cerr)));
}

void GraphChecker::VisitStoreArrayI([[maybe_unused]] GraphVisitor *v, Inst *inst)
{
    bool need_barrier = inst->CastToStoreArrayI()->GetNeedBarrier();
    CheckMemoryInstruction(inst, need_barrier);
    ASSERT_DO_EXT_VISITOR(CheckCommonTypes(inst, inst->GetInputs()[1].GetInst()),
                          (std::cerr << "Types of store and store input are not compatible\n", inst->Dump(&std::cerr)));
    ASSERT_DO_EXT_VISITOR(need_barrier == (inst->GetType() == DataType::REFERENCE) || inst->GetType() == DataType::ANY,
                          (std::cerr << "StoreArrayI has incorrect value NeedBarrier", inst->Dump(&std::cerr)));
}

void GraphChecker::VisitStoreStatic([[maybe_unused]] GraphVisitor *v, Inst *inst)
{
    bool need_barrier = inst->CastToStoreStatic()->GetNeedBarrier();
    CheckMemoryInstruction(inst, need_barrier);
    auto graph = static_cast<GraphChecker *>(v)->GetGraph();
    ASSERT_DO_EXT_VISITOR(CheckCommonTypes(inst, inst->GetInputs()[1].GetInst()),
                          (std::cerr << "Types of store and store input are not compatible\n", inst->Dump(&std::cerr)));
    ASSERT_DO_EXT_VISITOR(need_barrier == (inst->GetType() == DataType::REFERENCE),
                          (std::cerr << "StoreStatic has incorrect value NeedBarrier", inst->Dump(&std::cerr)));
    [[maybe_unused]] auto init_inst = inst->GetInputs()[0].GetInst();
    if (init_inst->IsPhi()) {
        return;
    }
    [[maybe_unused]] auto opcode = init_inst->GetOpcode();
    ASSERT_DO_EXT_VISITOR(
        (opcode == Opcode::LoadAndInitClass || opcode == Opcode::LoadImmediate),
        (std::cerr << "The first input for the StoreStatic should be LoadAndInitClass or LoadImmediate",
         inst->Dump(&std::cerr), init_inst->Dump(&std::cerr)));
    [[maybe_unused]] auto store_static = inst->CastToStoreStatic();
    [[maybe_unused]] auto class_id =
        graph->GetRuntime()->GetClassIdForField(store_static->GetMethod(), store_static->GetTypeId());
    // See comment in VisitNewObject about this if statement
    if (opcode == Opcode::LoadAndInitClass && init_inst->CastToLoadAndInitClass()->GetClass() == nullptr) {
        ASSERT_DO_EXT_VISITOR(init_inst->CastToLoadAndInitClass()->GetTypeId() == class_id,
                              (std::cerr << "StoreStatic and LoadAndInitClass must have equal class",
                               inst->Dump(&std::cerr), init_inst->Dump(&std::cerr)));
    }
}

void GraphChecker::VisitUnresolvedStoreStatic([[maybe_unused]] GraphVisitor *v, Inst *inst)
{
    bool need_barrier = inst->CastToUnresolvedStoreStatic()->GetNeedBarrier();
    CheckMemoryInstruction(inst, need_barrier);
    ASSERT_DO_EXT_VISITOR(CheckCommonTypes(inst, inst->GetInputs()[0].GetInst()),
                          (std::cerr << "Types of store and store input are not compatible\n", inst->Dump(&std::cerr)));
    ASSERT_DO_EXT_VISITOR(
        need_barrier == (inst->GetType() == DataType::REFERENCE),
        (std::cerr << "UnresolvedStoreStatic has incorrect value NeedBarrier", inst->Dump(&std::cerr)));
    [[maybe_unused]] auto ss = inst->GetInputs()[1].GetInst();
    ASSERT_DO_EXT_VISITOR(ss->GetOpcode() == Opcode::SaveState,
                          (std::cerr << "UnresolvedStoreStatic instruction second operand is not a SaveState",
                           inst->Dump(&std::cerr), ss->Dump(&std::cerr)));
}

void GraphChecker::VisitStoreObject([[maybe_unused]] GraphVisitor *v, Inst *inst)
{
    bool need_barrier = inst->CastToStoreObject()->GetNeedBarrier();
    CheckMemoryInstruction(inst, need_barrier);
    ASSERT_DO_EXT_VISITOR(CheckCommonTypes(inst, inst->GetInputs()[1].GetInst()),
                          (std::cerr << "Types of store and store input are not compatible\n", inst->Dump(&std::cerr)));
    ASSERT_DO_EXT_VISITOR(need_barrier == (inst->GetType() == DataType::REFERENCE) || inst->GetType() == DataType::ANY,
                          (std::cerr << "StoreObject has incorrect value NeedBarrier", inst->Dump(&std::cerr)));
    CheckObjectType(inst, inst->CastToStoreObject()->GetObjectType(), inst->CastToStoreObject()->GetTypeId());
}

void GraphChecker::VisitStoreObjectDynamic([[maybe_unused]] GraphVisitor *v, Inst *inst)
{
    bool need_barrier = inst->CastToStoreObjectDynamic()->IsBarrier();
    CheckMemoryInstruction(inst, need_barrier);
}

void GraphChecker::VisitFillConstArray([[maybe_unused]] GraphVisitor *v, Inst *inst)
{
    bool need_barrier = inst->CastToFillConstArray()->GetNeedBarrier();
    CheckMemoryInstruction(inst, need_barrier);
}

void GraphChecker::VisitStoreResolvedObjectField([[maybe_unused]] GraphVisitor *v, Inst *inst)
{
    bool need_barrier = inst->CastToStoreResolvedObjectField()->GetNeedBarrier();
    CheckMemoryInstruction(inst, need_barrier);
}

void GraphChecker::VisitStoreResolvedObjectFieldStatic([[maybe_unused]] GraphVisitor *v, Inst *inst)
{
    bool need_barrier = inst->CastToStoreResolvedObjectFieldStatic()->GetNeedBarrier();
    CheckMemoryInstruction(inst, need_barrier);
}

void GraphChecker::VisitLoadStatic([[maybe_unused]] GraphVisitor *v, Inst *inst)
{
    CheckMemoryInstruction(inst);
    auto graph = static_cast<GraphChecker *>(v)->GetGraph();
    [[maybe_unused]] auto init_inst = inst->GetInputs()[0].GetInst();
    if (init_inst->IsPhi()) {
        return;
    }
    [[maybe_unused]] auto opcode = init_inst->GetOpcode();
    ASSERT_DO_EXT_VISITOR(
        opcode == Opcode::LoadAndInitClass || opcode == Opcode::LoadImmediate,
        (std::cerr << "The first input for the LoadStatic should be LoadAndInitClass or LoadImmediate",
         inst->Dump(&std::cerr), init_inst->Dump(&std::cerr)));
    [[maybe_unused]] auto load_static = inst->CastToLoadStatic();
    [[maybe_unused]] auto class_id =
        graph->GetRuntime()->GetClassIdForField(load_static->GetMethod(), load_static->GetTypeId());
    // See comment in VisitNewObject about this if statement
    if (opcode == Opcode::LoadAndInitClass && init_inst->CastToLoadAndInitClass()->GetClass() == nullptr) {
        ASSERT_DO_EXT_VISITOR(init_inst->CastToLoadAndInitClass()->GetTypeId() == class_id,
                              (std::cerr << "LoadStatic and LoadAndInitClass must have equal class",
                               inst->Dump(&std::cerr), init_inst->Dump(&std::cerr)));
    }
}

void GraphChecker::VisitLoadClass([[maybe_unused]] GraphVisitor *v, Inst *inst)
{
    ASSERT_DO_EXT_VISITOR(inst->GetType() == DataType::REFERENCE,
                          (std::cerr << "LoadClass must have Reference type", inst->Dump(&std::cerr)));
    for (auto &user : inst->GetUsers()) {
        [[maybe_unused]] auto user_inst = user.GetInst();
        ASSERT_DO_EXT_VISITOR(
            user_inst->GetOpcode() == Opcode::CheckCast || user_inst->GetOpcode() == Opcode::IsInstance ||
                user_inst->GetOpcode() == Opcode::Phi || user_inst->GetOpcode() == Opcode::Intrinsic,
            (std::cerr << "Incorrect user of the LoadClass", inst->Dump(&std::cerr), user_inst->Dump(&std::cerr)));
    }
}

void GraphChecker::VisitLoadAndInitClass([[maybe_unused]] GraphVisitor *v, Inst *inst)
{
    ASSERT_DO_EXT_VISITOR(inst->GetType() == DataType::REFERENCE,
                          (std::cerr << "LoadAndInitClass must have Reference type", inst->Dump(&std::cerr)));
    for (auto &user : inst->GetUsers()) {
        [[maybe_unused]] auto user_inst = user.GetInst();
        ASSERT_DO_EXT_VISITOR(
            user_inst->GetOpcode() == Opcode::LoadStatic ||
                (user_inst->GetOpcode() == Opcode::LoadObject &&
                 user_inst->CastToLoadObject()->GetObjectType() == ObjectType::MEM_STATIC) ||
                user_inst->GetOpcode() == Opcode::StoreStatic ||
                (user_inst->GetOpcode() == Opcode::StoreObject &&
                 user_inst->CastToStoreObject()->GetObjectType() == ObjectType::MEM_STATIC) ||
                user_inst->GetOpcode() == Opcode::NewObject || user_inst->GetOpcode() == Opcode::Phi ||
                user_inst->GetOpcode() == Opcode::MultiArray || user_inst->GetOpcode() == Opcode::InitObject ||
                user_inst->GetOpcode() == Opcode::UnresolvedStoreStatic ||
                user_inst->GetOpcode() == Opcode::Intrinsic || user_inst->GetOpcode() == Opcode::NewArray ||
                user_inst->GetOpcode() == Opcode::IsInstance || user_inst->GetOpcode() == Opcode::CheckCast,
            (std::cerr << "Incorrect user of the LoadAndInitClass", inst->Dump(&std::cerr),
             user_inst->Dump(&std::cerr)));
    }
}

void GraphChecker::VisitUnresolvedLoadAndInitClass([[maybe_unused]] GraphVisitor *v, Inst *inst)
{
    ASSERT_DO_EXT_VISITOR(inst->GetType() == DataType::REFERENCE,
                          (std::cerr << "UnresolvedLoadAndInitClass must have Reference type", inst->Dump(&std::cerr)));
    ASSERT_DO_EXT_VISITOR(
        inst->CastToUnresolvedLoadAndInitClass()->GetClass() == nullptr,
        (std::cerr << "UnresolvedLoadAndInitClass must have a null ClassPtr", inst->Dump(&std::cerr)));
    [[maybe_unused]] auto ss = inst->GetInputs()[0].GetInst();
    ASSERT_DO_EXT_VISITOR(ss->GetOpcode() == Opcode::SaveState,
                          (std::cerr << "UnresolvedLoadAndInitClass instruction first operand is not a SaveState",
                           inst->Dump(&std::cerr), ss->Dump(&std::cerr)));
    for (auto &user : inst->GetUsers()) {
        [[maybe_unused]] auto user_inst = user.GetInst();
        ASSERT_DO_EXT_VISITOR(
            user_inst->GetOpcode() == Opcode::LoadStatic || user_inst->GetOpcode() == Opcode::StoreStatic ||
                user_inst->GetOpcode() == Opcode::NewObject || user_inst->GetOpcode() == Opcode::NewArray ||
                user_inst->GetOpcode() == Opcode::Phi || user_inst->GetOpcode() == Opcode::MultiArray ||
                user_inst->GetOpcode() == Opcode::UnresolvedStoreStatic,
            (std::cerr << "Incorrect user of the UnresolvedLoadAndInitClass", inst->Dump(&std::cerr),
             user_inst->Dump(&std::cerr)));
    }
}

void GraphChecker::VisitGetInstanceClass([[maybe_unused]] GraphVisitor *v, Inst *inst)
{
    ASSERT_DO_EXT_VISITOR(inst->GetType() == DataType::REFERENCE,
                          (std::cerr << "GetInstanceClass must have Reference type", inst->Dump(&std::cerr)));
    for (auto &user : inst->GetUsers()) {
        [[maybe_unused]] auto user_inst = user.GetInst();
        ASSERT_DO_EXT_VISITOR(!user_inst->IsSaveState(), (std::cerr << "Incorrect user of the GetInstanceClass",
                                                          inst->Dump(&std::cerr), user_inst->Dump(&std::cerr)));
    }
    ASSERT_DO(
        !inst->GetBasicBlock()->GetGraph()->IsDynamicMethod(),
        (std::cerr
         << "we can't use GetInstanceClass in dynamic, because object class can be changed. Use LoadObject MEM_CLASS"));
}

void GraphChecker::VisitNewObject([[maybe_unused]] GraphVisitor *v, Inst *inst)
{
    ASSERT_DO_EXT_VISITOR(inst->GetType() == DataType::REFERENCE,
                          (std::cerr << "NewObject must be have Reference type", inst->Dump(&std::cerr)));
    [[maybe_unused]] auto init_inst = inst->GetInputs()[0].GetInst();
    if (init_inst->IsPhi()) {
        return;
    }
    [[maybe_unused]] auto opcode = init_inst->GetOpcode();
    ASSERT_DO_EXT_VISITOR(opcode == Opcode::LoadAndInitClass || opcode == Opcode::LoadRuntimeClass ||
                              opcode == Opcode::UnresolvedLoadAndInitClass || opcode == Opcode::LoadImmediate,
                          (std::cerr << "The first input for the NewObject should be LoadAndInitClass or "
                                        "UnresolvedLoadAndInitClass or LoadRuntimeClass or LoadImmediate",
                           inst->Dump(&std::cerr), init_inst->Dump(&std::cerr)));
    [[maybe_unused]] auto ss_inst = inst->GetInputs()[1].GetInst();
    ASSERT_DO_EXT_VISITOR(ss_inst->GetOpcode() == Opcode::SaveState,
                          (std::cerr << "The second input for the NewObject should be SaveState",
                           inst->Dump(&std::cerr), ss_inst->Dump(&std::cerr)));
    // If InitClass contains an already resolved class, then IDs may be different. Because VN can remove the
    // duplicated InitClass and keep only one that is located in the inlined method and has a different id
    // accordingly.
    if (init_inst->GetOpcode() == Opcode::LoadAndInitClass &&
        init_inst->CastToLoadAndInitClass()->GetClass() == nullptr) {
        ASSERT_DO_EXT_VISITOR(init_inst->CastToLoadAndInitClass()->GetTypeId() == inst->CastToNewObject()->GetTypeId(),
                              std::cerr << "NewObject and LoadAndInitClass must have equal class:\n"
                                        << *inst << '\n'
                                        << *init_inst << std::endl);
    } else if (init_inst->GetOpcode() == Opcode::UnresolvedLoadAndInitClass) {
        ASSERT_DO_EXT_VISITOR(init_inst->CastToUnresolvedLoadAndInitClass()->GetTypeId() ==
                                  inst->CastToNewObject()->GetTypeId(),
                              std::cerr << "NewObject and UnresolvedLoadAndInitClass must have equal class:\n"
                                        << *inst << '\n'
                                        << *init_inst << std::endl);
    }
}

void GraphChecker::VisitLoadRuntimeClass([[maybe_unused]] GraphVisitor *v, [[maybe_unused]] Inst *inst)
{
    ASSERT_DO_EXT_VISITOR(
        inst->CastToLoadRuntimeClass()->GetTypeId() == TypeIdMixin::MEM_PROMISE_CLASS_ID,
        (std::cerr << "LoadRuntimeClass should have TypeId MEM_PROMISE_CLASS_ID", inst->Dump(&std::cerr)));
}

void GraphChecker::VisitInitObject([[maybe_unused]] GraphVisitor *v, [[maybe_unused]] Inst *inst)
{
    ASSERT_DO_EXT_VISITOR(OPTIONS.IsCompilerSupportInitObjectInst(),
                          (std::cerr << "Instruction InitObject isn't supported", inst->Dump(&std::cerr)));
}

void GraphChecker::VisitInitClass([[maybe_unused]] GraphVisitor *v, [[maybe_unused]] Inst *inst)
{
    ASSERT_DO_EXT_VISITOR(inst->GetType() == DataType::NO_TYPE,
                          (std::cerr << "InitClass doesn't have type", inst->Dump(&std::cerr)));
}

void GraphChecker::VisitIntrinsic([[maybe_unused]] GraphVisitor *v, Inst *inst)
{
    switch (inst->CastToIntrinsic()->GetIntrinsicId()) {
#include "intrinsics_graph_checker.inl"
        default: {
            return;
        }
    }
}

void GraphChecker::VisitLoadObject([[maybe_unused]] GraphVisitor *v, Inst *inst)
{
    CheckMemoryInstruction(inst);
    CheckObjectType(inst, inst->CastToLoadObject()->GetObjectType(), inst->CastToLoadObject()->GetTypeId());
}

void GraphChecker::VisitConstant([[maybe_unused]] GraphVisitor *v, [[maybe_unused]] Inst *inst)
{
    [[maybe_unused]] auto type = inst->GetType();
    [[maybe_unused]] auto is_dynamic = static_cast<GraphChecker *>(v)->GetGraph()->IsDynamicMethod();
    if (static_cast<GraphChecker *>(v)->GetGraph()->IsBytecodeOptimizer()) {
        ASSERT_DO_EXT_VISITOR(
            type == DataType::FLOAT32 || type == DataType::FLOAT64 || type == DataType::INT64 ||
                type == DataType::INT32 || (type == DataType::ANY && is_dynamic),
            (std::cerr << "Constant inst can be only FLOAT32, FLOAT64, INT32 or INT64\n", inst->Dump(&std::cerr)));

    } else {
        ASSERT_DO_EXT_VISITOR(
            type == DataType::FLOAT32 || type == DataType::FLOAT64 || type == DataType::INT64 ||
                (type == DataType::ANY && is_dynamic),
            (std::cerr << "Constant instruction can be only FLOAT32, FLOAT64 or INT64\n", inst->Dump(&std::cerr)));
    }
}

void GraphChecker::VisitNullPtr([[maybe_unused]] GraphVisitor *v, [[maybe_unused]] Inst *inst)
{
    ASSERT_DO_EXT_VISITOR(
        inst->GetType() == DataType::REFERENCE,
        (std::cerr << "NullPtr instruction should have REFERENCE type only\n", inst->Dump(&std::cerr)));

    ASSERT_DO_EXT_VISITOR(static_cast<GraphChecker *>(v)->IncrementNullPtrInstCounterAndGet() == 1,
                          (std::cerr << "There should be not more than one NullPtr instruction in graph\n",
                           inst->GetBasicBlock()->Dump(&std::cerr)));
}

void GraphChecker::VisitPhi([[maybe_unused]] GraphVisitor *v, Inst *inst)
{
    for ([[maybe_unused]] auto input : inst->GetInputs()) {
        ASSERT_DO_EXT_VISITOR(CheckCommonTypes(inst, input.GetInst()),
                              (std::cerr << "Types of phi result and phi input are not compatible\n"
                                         << *inst << std::endl
                                         << *input.GetInst()));
    }
}

void GraphChecker::VisitParameter([[maybe_unused]] GraphVisitor *v, [[maybe_unused]] Inst *inst)
{
    ASSERT_DO_EXT_VISITOR(inst->GetType() != DataType::NO_TYPE,
                          (std::cerr << "The parametr doesn't have type:\n", inst->Dump(&std::cerr)));
}

void GraphChecker::VisitCompare([[maybe_unused]] GraphVisitor *v, Inst *inst)
{
    [[maybe_unused]] auto op1 = inst->GetInputs()[0].GetInst();
    [[maybe_unused]] auto op2 = inst->GetInputs()[1].GetInst();
    for (size_t i = 0; i < inst->GetInputsCount(); i++) {
        ASSERT_DO_EXT_VISITOR(inst->GetInputType(i) != DataType::NO_TYPE,
                              std::cerr << "Source operand type is not set: " << *inst << std::endl);
    }
    ASSERT_DO_EXT_VISITOR(inst->GetInputType(0) == inst->GetInputType(1),
                          std::cerr << "Conditional instruction has different inputs type: " << *inst << std::endl);
    if (inst->GetInputType(0) == DataType::REFERENCE) {
        ASSERT_DO_EXT_VISITOR(inst->CastToCompare()->GetCc() == ConditionCode::CC_NE ||
                                  inst->CastToCompare()->GetCc() == ConditionCode::CC_EQ,
                              (std::cerr << "Reference compare must have CC_NE or CC_EQ: \n", inst->Dump(&std::cerr)));
        if (op1->IsConst()) {
            ASSERT_DO_EXT_VISITOR(IsZeroConstant(op1), (std::cerr << "Constant reference input must be integer 0: \n",
                                                        inst->Dump(&std::cerr), op1->Dump(&std::cerr)));
        } else {
            ASSERT_DO_EXT_VISITOR(op1->GetType() == DataType::REFERENCE,
                                  (std::cerr << "Condition instruction 1st operand type is not a reference\n",
                                   inst->Dump(&std::cerr), op1->Dump(&std::cerr)));
        }
        if (op2->IsConst()) {
            ASSERT_DO_EXT_VISITOR(IsZeroConstant(op2), (std::cerr << "Constant reference input must be integer 0: \n",
                                                        inst->Dump(&std::cerr), op2->Dump(&std::cerr)));
        } else {
            ASSERT_DO_EXT_VISITOR(op2->GetType() == DataType::REFERENCE,
                                  (std::cerr << "Condition instruction 2nd operand type is not a reference\n",
                                   inst->Dump(&std::cerr), op2->Dump(&std::cerr)));
        }
    }
    ASSERT_EXT_PRINT_VISITOR(inst->GetType() == DataType::BOOL, "Condition instruction type is not a bool");
}

void GraphChecker::VisitCast([[maybe_unused]] GraphVisitor *v, Inst *inst)
{
    [[maybe_unused]] auto dst_type = inst->GetType();
    [[maybe_unused]] auto src_type = inst->GetInputType(0);
    [[maybe_unused]] auto input_type = inst->GetInput(0).GetInst()->GetType();

    ASSERT_DO_EXT_VISITOR(DataType::IsTypeNumeric(dst_type),
                          (std::cerr << "Cast instruction dst type is not a numeric type\n", inst->Dump(&std::cerr)));
    if (static_cast<GraphChecker *>(v)->GetGraph()->GetMode().SupportManagedCode()) {
        ASSERT_DO_EXT_VISITOR(
            DataType::IsTypeNumeric(src_type),
            (std::cerr << "Cast instruction src type is not a numeric type\n", inst->Dump(&std::cerr)));
        ASSERT_DO_EXT_VISITOR(
            DataType::IsTypeNumeric(input_type),
            (std::cerr << "Cast instruction operand type is not a numeric type\n", inst->Dump(&std::cerr)));
    }
    ASSERT_DO_EXT_VISITOR(DataType::GetCommonType(src_type) == DataType::GetCommonType(input_type),
                          (std::cerr << "Incorrect src_type and input type\n", inst->Dump(&std::cerr)));
    ASSERT_DO_EXT_VISITOR(!(DataType::IsFloatType(src_type) && DataType::IsLessInt32(dst_type)),
                          (std::cerr << "Cast instruction from " << DataType::internal::TYPE_NAMES.at(src_type)
                                     << " to " << DataType::internal::TYPE_NAMES.at(dst_type) << " don't support\n",
                           inst->Dump(&std::cerr)));
}

void GraphChecker::VisitCmp([[maybe_unused]] GraphVisitor *v, Inst *inst)
{
    [[maybe_unused]] auto op1 = inst->GetInput(0).GetInst();
    [[maybe_unused]] auto op2 = inst->GetInput(1).GetInst();
    ASSERT_DO_EXT_VISITOR(DataType::IsTypeNumeric(op1->GetType()),
                          (std::cerr << "Cmp instruction 1st operand type is not a numeric type\n",
                           inst->Dump(&std::cerr), op1->Dump(&std::cerr)));
    ASSERT_DO_EXT_VISITOR(DataType::IsTypeNumeric(op2->GetType()),
                          (std::cerr << "Cmp instruction 2st operand type is not a numeric type\n",
                           inst->Dump(&std::cerr), op2->Dump(&std::cerr)));
    ASSERT_DO_EXT_VISITOR(
        DataType::GetCommonType(op1->GetType()) == DataType::GetCommonType(inst->GetInputType(0)),
        (std::cerr << "Input type and Cmp Input Type are not equal\n", inst->Dump(&std::cerr), op1->Dump(&std::cerr)));
    ASSERT_DO_EXT_VISITOR(
        DataType::GetCommonType(op2->GetType()) == DataType::GetCommonType(inst->GetInputType(1)),
        (std::cerr << "Input type and Cmp Input Type are not equal\n", inst->Dump(&std::cerr), op2->Dump(&std::cerr)));
    ASSERT_DO_EXT_VISITOR(inst->GetType() == DataType::INT32,
                          (std::cerr << "Cmp instruction type is not a int32\n", inst->Dump(&std::cerr)));
    for (size_t i = 0; i < inst->GetInputsCount(); i++) {
        ASSERT_DO_EXT_VISITOR(inst->GetInputType(i) != DataType::NO_TYPE,
                              std::cerr << "Source operand type is not set: " << *inst << std::endl);
    }
}

void GraphChecker::VisitMonitor([[maybe_unused]] GraphVisitor *v, Inst *inst)
{
    [[maybe_unused]] auto op = inst->GetInputs()[0].GetInst();
    ASSERT_DO_EXT_VISITOR(inst->GetType() == DataType::VOID,
                          (std::cerr << "Monitor type is not a void", inst->Dump(&std::cerr)));
    ASSERT_DO_EXT_VISITOR(DataType::IsReference(op->GetType()),
                          (std::cerr << "Monitor instruction 1st operand type is not a reference",
                           inst->Dump(&std::cerr), op->Dump(&std::cerr)));
    [[maybe_unused]] auto op1 = inst->GetInputs()[1].GetInst();
    ASSERT_DO_EXT_VISITOR(op1->GetOpcode() == Opcode::SaveState,
                          (std::cerr << "Monitor instruction second operand is not a SaveState", inst->Dump(&std::cerr),
                           op1->Dump(&std::cerr)));
}

void GraphChecker::VisitReturn([[maybe_unused]] GraphVisitor *v, Inst *inst)
{
    [[maybe_unused]] auto op = inst->GetInputs()[0].GetInst();
    ASSERT_DO_EXT_VISITOR(CheckCommonTypes(inst, op),
                          (std::cerr << "Types of return and its input are not compatible\n return:\n",
                           inst->Dump(&std::cerr), std::cerr << "\n input:\n", op->Dump(&std::cerr)));
    CheckContrlFlowInst(inst);
    [[maybe_unused]] auto num_succs = inst->GetBasicBlock()->GetSuccsBlocks().size();
    ASSERT_EXT_PRINT_VISITOR(num_succs == 1, "Basic block with Return must have 1 successor");
    [[maybe_unused]] auto succ = inst->GetBasicBlock()->GetSuccsBlocks()[0];
    ASSERT_DO_EXT_VISITOR(succ->IsEndBlock() || succ->IsTryEnd(),
                          std::cerr << "Basic block with Return must have end or try end block as successor:\n"
                                    << *inst << std::endl);
}

void GraphChecker::VisitReturnVoid([[maybe_unused]] GraphVisitor *v, Inst *inst)
{
    CheckContrlFlowInst(inst);
    [[maybe_unused]] auto num_succs = inst->GetBasicBlock()->GetSuccsBlocks().size();
    ASSERT_EXT_PRINT_VISITOR(num_succs == 1, "Basic block with ReturnVoid must have 1 successor");
    [[maybe_unused]] auto succ = inst->GetBasicBlock()->GetSuccsBlocks()[0];
    ASSERT_EXT_PRINT_VISITOR(succ->IsEndBlock() || succ->IsTryEnd(),
                             "Basic block with ReturnVoid must have end or try_end block as successor.");
}

void GraphChecker::VisitNullCheck([[maybe_unused]] GraphVisitor *v, Inst *inst)
{
    [[maybe_unused]] Inst *array = inst->GetInput(0).GetInst();
    ASSERT_DO_EXT_VISITOR(DataType::IsReference(array->GetType()) || array->GetType() == DataType::ANY,
                          (std::cerr << "\n Types of input NullCheck must be REFERENCE or ANY: \n",
                           inst->Dump(&std::cerr), array->Dump(&std::cerr)));
    [[maybe_unused]] auto ss = inst->GetInput(1).GetInst();
    ASSERT_DO_EXT_VISITOR(ss->GetOpcode() == Opcode::SaveState || ss->GetOpcode() == Opcode::SaveStateDeoptimize,
                          (std::cerr << "\n Second input of NullCheck must be SaveState: \n", inst->Dump(&std::cerr),
                           ss->Dump(&std::cerr)));
}

void GraphChecker::VisitBoundsCheck([[maybe_unused]] GraphVisitor *v, Inst *inst)
{
    for (int i = 0; i < 1; i++) {
        [[maybe_unused]] auto op = inst->GetInputs()[i].GetInst();
        [[maybe_unused]] auto op_type = op->GetType();
        // TODO(pishin): actually type should be INT32, but predecessor may be Call instruction with type u16, u8
        // e.t.c
        ASSERT_DO_EXT_VISITOR(
            (op->IsConst() && op_type == DataType::INT64) ||
                (DataType::GetCommonType(op_type) == DataType::INT64 &&
                 Is32Bits(op_type, static_cast<GraphChecker *>(v)->GetGraph()->GetArch())),
            (std::cerr << "Types of " << i << " input BoundsCheck must be INT32 or less:\n", inst->Dump(&std::cerr)));
    }
}

void GraphChecker::VisitBoundsCheckI([[maybe_unused]] GraphVisitor *v, [[maybe_unused]] Inst *inst)
{
    ASSERT_EXT_VISITOR(!inst->HasUsers());
}

void GraphChecker::VisitRefTypeCheck([[maybe_unused]] GraphVisitor *v, Inst *inst)
{
    ASSERT_DO_EXT_VISITOR((inst->GetType() == DataType::REFERENCE),
                          (std::cerr << "Types of RefTypeCheck must be REFERENCE\n", inst->Dump(&std::cerr)));
    for (unsigned i = 0; i < 2U; i++) {
        [[maybe_unused]] auto op = inst->GetInputs()[i].GetInst();
        [[maybe_unused]] auto op_type = op->GetType();
        ASSERT_DO_EXT_VISITOR((op_type == DataType::REFERENCE),
                              (std::cerr << "Types of " << i << " input RefTypeCheck must be REFERENCE\n",
                               inst->Dump(&std::cerr), op->Dump(&std::cerr)));
    }
    CheckThrows(inst, {Opcode::StoreArray, Opcode::StoreArrayPair, Opcode::StoreArrayI, Opcode::StoreArrayPairI,
                       Opcode::SaveState, Opcode::SafePoint});
}

void GraphChecker::VisitNegativeCheck([[maybe_unused]] GraphVisitor *v, Inst *inst)
{
    [[maybe_unused]] auto op = inst->GetInputs()[0].GetInst();
    [[maybe_unused]] auto op_type = op->GetType();
    ASSERT_DO_EXT_VISITOR(DataType::GetCommonType(op_type) == DataType::INT64,
                          (std::cerr << "Type of NegativeCheck must be integer\n", inst->Dump(&std::cerr)));
    if (inst->GetBasicBlock()->GetGraph()->IsDynamicMethod()) {
        // In dynamic methods for negative values we creates f64 Mod, so we insert NegativeCheck before Mod
        // Lowering can change Mod to And(I)
        CheckThrows(inst, {Opcode::NewArray, Opcode::MultiArray, Opcode::Phi, Opcode::Mod, Opcode::And, Opcode::AndI});
    } else {
        CheckThrows(inst, {Opcode::NewArray, Opcode::MultiArray, Opcode::Phi});
    }
}

void GraphChecker::VisitNotPositiveCheck([[maybe_unused]] GraphVisitor *v, Inst *inst)
{
    [[maybe_unused]] auto op = inst->GetInputs()[0].GetInst();
    [[maybe_unused]] auto op_type = op->GetType();
    ASSERT_DO_EXT_VISITOR(DataType::GetCommonType(op_type) == DataType::INT64,
                          (std::cerr << "Type of NotPositiveCheck must be integer\n", inst->Dump(&std::cerr)));
    ASSERT(inst->GetBasicBlock()->GetGraph()->IsDynamicMethod());
    CheckThrows(inst, {Opcode::Phi, Opcode::Mod});
}

void GraphChecker::VisitZeroCheck([[maybe_unused]] GraphVisitor *v, Inst *inst)
{
    [[maybe_unused]] auto op = inst->GetInputs()[0].GetInst();
    [[maybe_unused]] auto op_type = op->GetType();
    ASSERT_DO_EXT_VISITOR(DataType::GetCommonType(op_type) == DataType::INT64,
                          (std::cerr << "Type of ZeroCheck input must be integer\n", inst->Dump(&std::cerr)));
    CheckThrows(inst, {Opcode::Div, Opcode::Mod, Opcode::Phi});
}

void GraphChecker::VisitDeoptimizeIf([[maybe_unused]] GraphVisitor *v, Inst *inst)
{
    [[maybe_unused]] auto op = inst->GetInput(0).GetInst();
    ASSERT_DO_EXT_VISITOR(op->GetType() == DataType::BOOL || op->IsBoolConst(),
                          (std::cerr << "Type of first input DeoptimizeIf must be BOOL:\n", inst->Dump(&std::cerr)));
    [[maybe_unused]] auto ss = inst->GetInput(1).GetInst();
    ASSERT_DO_EXT_VISITOR(
        ss->GetOpcode() == Opcode::SaveStateDeoptimize || ss->GetOpcode() == Opcode::SaveState,
        (std::cerr << "Second input DeoptimizeIf must be SaveStateDeoptimize or SaveState:\n", inst->Dump(&std::cerr)));
}

void GraphChecker::VisitLenArray([[maybe_unused]] GraphVisitor *v, Inst *inst)
{
    ASSERT_DO_EXT_VISITOR(inst->GetType() == DataType::INT32,
                          (std::cerr << "Type of LenArray must be INT32:\n", inst->Dump(&std::cerr)));
    [[maybe_unused]] auto op = inst->GetInputs()[0].GetInst();
    if (op->GetOpcode() == Opcode::NullCheck) {
        op = op->GetInput(0).GetInst();
    }
    ASSERT_DO_EXT_VISITOR(
        DataType::IsReference(op->GetType()),
        (std::cerr << "Types of input LenArray must be REFERENCE:\n", inst->Dump(&std::cerr), op->Dump(&std::cerr)));
}

void GraphChecker::VisitCallVirtual([[maybe_unused]] GraphVisitor *v, Inst *inst)
{
    ASSERT_DO_EXT_VISITOR(inst->GetInputs().Size() > 0,
                          (std::cerr << "Virtual function must have inputs:\n", inst->Dump(&std::cerr)));
    [[maybe_unused]] auto op = inst->GetInputs()[0].GetInst();
    ASSERT_DO_EXT_VISITOR(DataType::IsReference(op->GetType()),
                          (std::cerr << "Types of first input CallVirtual must be REFERENCE(this):\n",
                           inst->Dump(&std::cerr), op->Dump(&std::cerr)));
}

void GraphChecker::VisitCallDynamic([[maybe_unused]] GraphVisitor *v, [[maybe_unused]] Inst *inst)
{
    ASSERT_DO_EXT_VISITOR(
        static_cast<GraphChecker *>(v)->GetGraph()->IsDynamicMethod(),
        (std::cerr << "CallDynamic is supported only for dynamic languages:\n", inst->Dump(&std::cerr)));
}

void GraphChecker::VisitSaveState([[maybe_unused]] GraphVisitor *v, [[maybe_unused]] Inst *inst)
{
    ASSERT_DO_EXT_VISITOR((static_cast<SaveStateInst *>(inst))->Verify(), std::cerr
                                                                              << "Inconsistent SaveState instruction:\n"
                                                                              << *inst << std::endl);
#ifndef NDEBUG
    auto ss = inst->CastToSaveState();
    if (ss->GetInputsWereDeleted()) {
        for (auto &user : inst->GetUsers()) {
            ASSERT_DO_EXT_VISITOR(!user.GetInst()->RequireRegMap(),
                                  std::cerr << "Some inputs from save_state were deleted, but the user requireRegMap:\n"
                                            << *inst << std::endl
                                            << *(user.GetInst()) << std::endl);
        }
    }
    auto graph = inst->GetBasicBlock()->GetGraph();
    if (graph->IsDynamicMethod() && !graph->IsDynUnitTest() && !graph->IsBytecodeOptimizer()) {
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define VREG_ENV_TYPES(VREG_TYPE) VRegInfo::VRegType::VREG_TYPE,
        for (auto env_type : {VREGS_ENV_TYPE_DEFS(VREG_ENV_TYPES)}) {
            bool founded = false;
            for (size_t i = 0; i < inst->GetInputsCount(); ++i) {
                auto vreg = ss->GetVirtualRegister(i);
                founded |= vreg.GetVRegType() == env_type;
            }
            ASSERT_DO_EXT_VISITOR(founded, std::cerr << VRegInfo::VRegTypeToString(env_type) << " not found");
        }
#undef VREG_ENV_TYPES
    }

#endif
}

void GraphChecker::VisitSafePoint([[maybe_unused]] GraphVisitor *v, [[maybe_unused]] Inst *inst)
{
    ASSERT_DO_EXT_VISITOR(!inst->HasUsers(), std::cerr << "SafePoint must not have users:\n" << *inst << std::endl);
    ASSERT_DO_EXT_VISITOR((static_cast<SaveStateInst *>(inst))->Verify(), std::cerr
                                                                              << "Inconsistent SafePoint instruction:\n"
                                                                              << *inst << std::endl);
}

void GraphChecker::VisitSaveStateOsr([[maybe_unused]] GraphVisitor *v, [[maybe_unused]] Inst *inst)
{
    ASSERT_DO_EXT_VISITOR(!inst->HasUsers(), std::cerr << "SafeStateOsr must not have users:\n" << *inst << std::endl);
    ASSERT_DO_EXT_VISITOR((static_cast<SaveStateInst *>(inst))->Verify(),
                          std::cerr << "Inconsistent SafeStateOsr instruction:\n"
                                    << *inst << std::endl);
    ASSERT_DO_EXT_VISITOR(static_cast<GraphChecker *>(v)->GetGraph()->IsOsrMode(),
                          std::cerr << "SafeStateOsr must be created in the OSR mode only\n");
    ASSERT_DO_EXT_VISITOR(inst->GetBasicBlock()->IsOsrEntry(),
                          std::cerr << "SafeStateOsr's basic block must be osr-entry\n");
    auto first_inst = inst->GetBasicBlock()->GetFirstInst();
    while (first_inst != nullptr && (first_inst->IsCatchPhi() || first_inst->GetOpcode() == Opcode::Try)) {
        first_inst = first_inst->GetNext();
    }
    ASSERT_DO_EXT_VISITOR(first_inst == inst,
                          std::cerr << "SafeStateOsr must be the first instruction in the basic block\n");
}

void GraphChecker::VisitThrow([[maybe_unused]] GraphVisitor *v, [[maybe_unused]] Inst *inst)
{
    ASSERT_DO_EXT_VISITOR(DataType::IsReference(inst->GetInput(0).GetInst()->GetType()),
                          std::cerr << "Throw instruction must have input with reference type: " << *inst << std::endl);
    [[maybe_unused]] auto bb = inst->GetBasicBlock();
    ASSERT_DO_EXT_VISITOR(inst == bb->GetLastInst(),
                          std::cerr << "Throw instruction must be last instruction in the basic block: " << *inst
                                    << std::endl);
    for ([[maybe_unused]] auto succ : bb->GetSuccsBlocks()) {
        ASSERT_DO_EXT_VISITOR(succ->IsEndBlock() || succ->IsTryEnd(),
                              std::cerr << "Throw block must have end block or try-end block as successor\n");
    }
}

void GraphChecker::VisitCheckCast([[maybe_unused]] GraphVisitor *v, [[maybe_unused]] Inst *inst)
{
    ASSERT_DO_EXT_VISITOR(DataType::IsReference(inst->GetInput(0).GetInst()->GetType()),
                          std::cerr << "CheckCast instruction must have input 0 with reference type: " << *inst
                                    << std::endl);

    ASSERT_DO_EXT_VISITOR(DataType::IsReference(inst->GetInput(1).GetInst()->GetType()),
                          std::cerr << "CheckCast instruction must have input 1 with reference type: " << *inst
                                    << std::endl);

    [[maybe_unused]] auto save_state = inst->GetInput(2).GetInst();
    ASSERT_DO_EXT_VISITOR((save_state != nullptr && save_state->GetOpcode() == Opcode::SaveState),
                          std::cerr << "CheckCast instruction must have SaveState as input 2: " << *inst << std::endl);
}

void GraphChecker::VisitIsInstance([[maybe_unused]] GraphVisitor *v, [[maybe_unused]] Inst *inst)
{
    ASSERT_DO_EXT_VISITOR(DataType::IsReference(inst->GetInput(0).GetInst()->GetType()),
                          std::cerr << "IsInstance instruction must have input 0 with reference type: " << *inst
                                    << std::endl);
    ASSERT_DO_EXT_VISITOR(DataType::IsReference(inst->GetInput(1).GetInst()->GetType()),
                          std::cerr << "IsInstance instruction must have input 1 with reference type: " << *inst
                                    << std::endl);

    [[maybe_unused]] auto save_state = inst->GetInput(2).GetInst();
    ASSERT_DO_EXT_VISITOR((save_state != nullptr && save_state->GetOpcode() == Opcode::SaveState),
                          std::cerr << "IsInstance instruction must have SaveState as input 2: " << *inst << std::endl);

    ASSERT_DO_EXT_VISITOR(inst->GetType() == DataType::BOOL,
                          (std::cerr << "Types of IsInstance must be BOOL:\n", inst->Dump(&std::cerr)));
}

void GraphChecker::VisitSelect([[maybe_unused]] GraphVisitor *v, [[maybe_unused]] Inst *inst)
{
    [[maybe_unused]] auto op0 = inst->GetInput(0).GetInst();
    [[maybe_unused]] auto op1 = inst->GetInput(1).GetInst();
    [[maybe_unused]] auto op2 = inst->GetInput(2U).GetInst();
    [[maybe_unused]] auto op3 = inst->GetInput(3U).GetInst();

    for (size_t i = 0; i < inst->GetInputsCount(); i++) {
        ASSERT_DO_EXT_VISITOR(inst->GetInputType(i) != DataType::NO_TYPE,
                              std::cerr << "Source operand type is not set: " << *inst << std::endl);
    }

    ASSERT_DO_EXT_VISITOR(
        DataType::GetCommonType(inst->GetType()) == DataType::INT64 ||
            IsFloatType(DataType::GetCommonType(inst->GetType())) || inst->GetType() == DataType::REFERENCE ||
            inst->GetType() == DataType::ANY,
        (std::cerr << "Select instruction type is not integer or reference or any", inst->Dump(&std::cerr)));
    ASSERT_DO_EXT_VISITOR(DataType::GetCommonType(op0->GetType()) == DataType::INT64 ||
                              IsFloatType(DataType::GetCommonType(op0->GetType())) ||
                              op0->GetType() == DataType::REFERENCE || op0->GetType() == DataType::ANY,
                          (std::cerr << "Select instruction 1st operand type is not integer or reference or any",
                           inst->Dump(&std::cerr)));
    ASSERT_DO_EXT_VISITOR(DataType::GetCommonType(op1->GetType()) == DataType::INT64 ||
                              IsFloatType(DataType::GetCommonType(op1->GetType())) ||
                              op1->GetType() == DataType::REFERENCE || op1->GetType() == DataType::ANY,
                          (std::cerr << "Select instruction 2nd operand type is not integer or reference or any",
                           inst->Dump(&std::cerr)));

    ASSERT_DO_EXT_VISITOR(CheckCommonTypes(op0, op1),
                          (std::cerr << "Types of two first select instruction operands are not compatible\n",
                           op0->Dump(&std::cerr), op1->Dump(&std::cerr), inst->Dump(&std::cerr)));
    ASSERT_DO_EXT_VISITOR(
        CheckCommonTypes(inst, op0),
        (std::cerr << "Types of instruction result and its operands are not compatible\n", inst->Dump(&std::cerr)));

    ASSERT_DO_EXT_VISITOR(inst->GetInputType(2U) == inst->GetInputType(3U),
                          std::cerr << "Select comparison arguments has different inputs type: " << *inst << std::endl);
    if (inst->GetInputType(2U) == DataType::REFERENCE) {
        [[maybe_unused]] auto cc = inst->CastToSelect()->GetCc();
        ASSERT_DO_EXT_VISITOR(
            cc == ConditionCode::CC_NE || cc == ConditionCode::CC_EQ,
            (std::cerr << "Select reference comparison must be CC_NE or CC_EQ: \n", inst->Dump(&std::cerr)));
        if (op2->IsConst()) {
            ASSERT_DO_EXT_VISITOR(IsZeroConstant(op2), (std::cerr << "Constant reference input must be integer 0: \n",
                                                        inst->Dump(&std::cerr), op1->Dump(&std::cerr)));
        } else {
            ASSERT_DO_EXT_VISITOR(op2->GetType() == DataType::REFERENCE,
                                  (std::cerr << "Select instruction 3rd operand type is not a reference\n",
                                   inst->Dump(&std::cerr), op1->Dump(&std::cerr)));
        }
        if (op3->IsConst()) {
            ASSERT_DO_EXT_VISITOR(IsZeroConstant(op3), (std::cerr << "Constant reference input must be integer 0: \n",
                                                        inst->Dump(&std::cerr), op2->Dump(&std::cerr)));
        } else {
            ASSERT_DO_EXT_VISITOR(op3->GetType() == DataType::REFERENCE,
                                  (std::cerr << "Select instruction 4th operand type is not a reference\n",
                                   inst->Dump(&std::cerr), op2->Dump(&std::cerr)));
        }
    }
}

void GraphChecker::VisitSelectImm([[maybe_unused]] GraphVisitor *v, [[maybe_unused]] Inst *inst)
{
    [[maybe_unused]] auto op0 = inst->GetInput(0).GetInst();
    [[maybe_unused]] auto op1 = inst->GetInput(1).GetInst();
    [[maybe_unused]] auto op2 = inst->GetInput(2U).GetInst();
    [[maybe_unused]] auto op3 = inst->CastToSelectImm()->GetImm();
    [[maybe_unused]] bool is_dynamic = static_cast<GraphChecker *>(v)->GetGraph()->IsDynamicMethod();

    for (size_t i = 0; i < inst->GetInputsCount(); i++) {
        ASSERT_DO_EXT_VISITOR(inst->GetInputType(i) != DataType::NO_TYPE,
                              std::cerr << "Source operand type is not set: " << *inst << std::endl);
    }

    ASSERT_DO_EXT_VISITOR(
        DataType::GetCommonType(inst->GetType()) == DataType::INT64 || inst->GetType() == DataType::REFERENCE ||
            IsFloatType(DataType::GetCommonType(inst->GetType())) || (is_dynamic && inst->GetType() == DataType::ANY),
        (std::cerr << "SelectImm instruction type is not integer or reference or any", inst->Dump(&std::cerr)));
    ASSERT_DO_EXT_VISITOR(DataType::GetCommonType(op0->GetType()) == DataType::INT64 ||
                              IsFloatType(DataType::GetCommonType(op0->GetType())) ||
                              op0->GetType() == DataType::REFERENCE || (is_dynamic && op0->GetType() == DataType::ANY),
                          (std::cerr << "SelectImm instruction 1st operand type is not integer or reference or any",
                           inst->Dump(&std::cerr)));
    ASSERT_DO_EXT_VISITOR(DataType::GetCommonType(op1->GetType()) == DataType::INT64 ||
                              IsFloatType(DataType::GetCommonType(op1->GetType())) ||
                              op1->GetType() == DataType::REFERENCE || (is_dynamic && op1->GetType() == DataType::ANY),
                          (std::cerr << "SelectImm instruction 2nd operand type is not integer or reference or any",
                           inst->Dump(&std::cerr)));

    ASSERT_DO_EXT_VISITOR(CheckCommonTypes(op0, op1),
                          (std::cerr << "Types of two first SelectImm instruction operands are not compatible\n",
                           op0->Dump(&std::cerr), op1->Dump(&std::cerr), inst->Dump(&std::cerr)));
    ASSERT_DO_EXT_VISITOR(
        CheckCommonTypes(inst, op0),
        (std::cerr << "Types of instruction result and its operands are not compatible\n", inst->Dump(&std::cerr)));

    if (inst->GetInputType(2U) == DataType::REFERENCE) {
        [[maybe_unused]] auto cc = inst->CastToSelectImm()->GetCc();
        ASSERT_DO_EXT_VISITOR(
            cc == ConditionCode::CC_NE || cc == ConditionCode::CC_EQ,
            (std::cerr << "SelectImm reference comparison must be CC_NE or CC_EQ: \n", inst->Dump(&std::cerr)));
        if (op2->IsConst()) {
            ASSERT_DO_EXT_VISITOR(IsZeroConstant(op2), (std::cerr << "Constant reference input must be integer 0: \n",
                                                        inst->Dump(&std::cerr), op1->Dump(&std::cerr)));
        } else {
            ASSERT_DO_EXT_VISITOR(op2->GetType() == DataType::REFERENCE,
                                  (std::cerr << "Condition with immediate jump 1st operand type is not a reference\n",
                                   inst->Dump(&std::cerr), op1->Dump(&std::cerr)));
        }
        ASSERT_DO_EXT_VISITOR(
            op3 == 0, (std::cerr << "Reference can be compared only with 0 immediate: \n", inst->Dump(&std::cerr)));
    } else {
        ASSERT_EXT_PRINT_VISITOR(DataType::GetCommonType(op2->GetType()) == DataType::INT64 ||
                                     (is_dynamic && DataType::GetCommonType(op2->GetType()) == DataType::ANY),
                                 "SelectImm 3rd operand type is not an integer or any");

        if (DataType::GetCommonType(op2->GetType()) == DataType::ANY) {
            [[maybe_unused]] auto cc = inst->CastToSelectImm()->GetCc();
            ASSERT_DO_EXT_VISITOR(
                cc == ConditionCode::CC_NE || cc == ConditionCode::CC_EQ,
                (std::cerr << "SelectImm any comparison must be CC_NE or CC_EQ: \n", inst->Dump(&std::cerr)));
        }
    }
}

void GraphChecker::VisitIf([[maybe_unused]] GraphVisitor *v, Inst *inst)
{
    CheckContrlFlowInst(inst);
    [[maybe_unused]] auto num_succs = inst->GetBasicBlock()->GetSuccsBlocks().size();
    ASSERT_EXT_PRINT_VISITOR(num_succs == MAX_SUCCS_NUM, "Basic block with If must have 2 successesors");

    [[maybe_unused]] auto op1 = inst->GetInputs()[0].GetInst();
    [[maybe_unused]] auto op2 = inst->GetInputs()[1].GetInst();
    for (size_t i = 0; i < inst->GetInputsCount(); i++) {
        ASSERT_DO_EXT_VISITOR(inst->GetInputType(i) != DataType::NO_TYPE,
                              std::cerr << "Source operand type is not set: " << *inst << std::endl);
    }
    ASSERT_DO_EXT_VISITOR(inst->GetInputType(0) == inst->GetInputType(1),
                          std::cerr << "If has different inputs type: " << *inst << std::endl);
    if (inst->GetInputType(0) == DataType::REFERENCE) {
        [[maybe_unused]] auto cc = inst->CastToIf()->GetCc();
        ASSERT_DO_EXT_VISITOR(
            cc == ConditionCode::CC_NE || cc == ConditionCode::CC_EQ,
            (std::cerr << "Reference comparison in If must be CC_NE or CC_EQ: \n", inst->Dump(&std::cerr)));
        if (op1->IsConst()) {
            ASSERT_DO_EXT_VISITOR(IsZeroConstant(op1), (std::cerr << "Constant reference input must be integer 0: \n",
                                                        inst->Dump(&std::cerr), op1->Dump(&std::cerr)));
        } else {
            ASSERT_DO_EXT_VISITOR(op1->GetType() == DataType::REFERENCE,
                                  (std::cerr << "If 1st operand type is not a reference\n", inst->Dump(&std::cerr),
                                   op1->Dump(&std::cerr)));
        }
        if (op2->IsConst()) {
            ASSERT_DO_EXT_VISITOR(IsZeroConstant(op2), (std::cerr << "Constant reference input must be integer 0: \n",
                                                        inst->Dump(&std::cerr), op2->Dump(&std::cerr)));
        } else {
            ASSERT_DO_EXT_VISITOR(op2->GetType() == DataType::REFERENCE,
                                  (std::cerr << "If 2nd operand type is not a reference\n", inst->Dump(&std::cerr),
                                   op2->Dump(&std::cerr)));
        }
    }
}

void GraphChecker::VisitIfImm([[maybe_unused]] GraphVisitor *v, Inst *inst)
{
    CheckContrlFlowInst(inst);
    [[maybe_unused]] auto num_succs = inst->GetBasicBlock()->GetSuccsBlocks().size();
    ASSERT_EXT_PRINT_VISITOR(num_succs == MAX_SUCCS_NUM, "Basic block with IfImm must have 2 successesors");

    [[maybe_unused]] auto op1 = inst->GetInput(0).GetInst();
    [[maybe_unused]] auto op2 = inst->CastToIfImm()->GetImm();
    ASSERT_DO_EXT_VISITOR(inst->GetInputType(0) != DataType::NO_TYPE,
                          std::cerr << "Source operand type is not set: " << *inst << std::endl);
    if (inst->GetInputType(0) == DataType::REFERENCE) {
        [[maybe_unused]] auto cc = inst->CastToIfImm()->GetCc();
        ASSERT_DO_EXT_VISITOR(
            cc == ConditionCode::CC_NE || cc == ConditionCode::CC_EQ,
            (std::cerr << "Reference comparison in IfImm must have CC_NE or CC_EQ: \n", inst->Dump(&std::cerr)));
        if (op1->IsConst()) {
            ASSERT_DO_EXT_VISITOR(IsZeroConstant(op1), (std::cerr << "Constant reference input must be integer 0: \n",
                                                        inst->Dump(&std::cerr), op1->Dump(&std::cerr)));
        } else {
            ASSERT_DO_EXT_VISITOR(op1->GetType() == DataType::REFERENCE,
                                  (std::cerr << "IfImm operand type should be here a reference: \n",
                                   inst->Dump(&std::cerr), op1->Dump(&std::cerr)));
        }
        ASSERT_DO_EXT_VISITOR(
            op2 == 0, (std::cerr << "Reference can be compared only with 0 immediate: \n", inst->Dump(&std::cerr)));
    } else {
        ASSERT_EXT_PRINT_VISITOR(
            DataType::GetCommonType(op1->GetType()) == DataType::INT64 ||
                (static_cast<GraphChecker *>(v)->GetGraph()->IsDynamicMethod() && op1->GetType() == DataType::ANY),
            "IfImm operand type should be here an integer");
    }
}

void GraphChecker::VisitTry([[maybe_unused]] GraphVisitor *v, Inst *inst)
{
    [[maybe_unused]] auto bb = inst->GetBasicBlock();
    ASSERT_EXT_PRINT_VISITOR(bb->IsTryBegin(), "TryInst should be placed in the try-begin basic block");
}

void GraphChecker::VisitNOP([[maybe_unused]] GraphVisitor *v, [[maybe_unused]] Inst *inst)
{
    ASSERT_EXT_PRINT_VISITOR(inst->GetUsers().Empty(), "NOP can not have users\n");
}

void GraphChecker::VisitAndNot([[maybe_unused]] GraphVisitor *v, Inst *inst)
{
    CheckBinaryOperationTypes(inst, true);
}
void GraphChecker::VisitOrNot([[maybe_unused]] GraphVisitor *v, Inst *inst)
{
    CheckBinaryOperationTypes(inst, true);
}
void GraphChecker::VisitXorNot([[maybe_unused]] GraphVisitor *v, Inst *inst)
{
    CheckBinaryOperationTypes(inst, true);
}
void GraphChecker::VisitMNeg([[maybe_unused]] GraphVisitor *v, Inst *inst)
{
    CheckBinaryOperationTypes(inst, false);
}
void GraphChecker::VisitMAdd([[maybe_unused]] GraphVisitor *v, Inst *inst)
{
    CheckTernaryOperationTypes(inst);
}
void GraphChecker::VisitMSub([[maybe_unused]] GraphVisitor *v, Inst *inst)
{
    CheckTernaryOperationTypes(inst);
}

void GraphChecker::VisitCompareAnyType([[maybe_unused]] GraphVisitor *v, [[maybe_unused]] Inst *inst)
{
    ASSERT_DO_EXT_VISITOR(
        static_cast<GraphChecker *>(v)->GetGraph()->IsDynamicMethod(),
        (std::cerr << "CompareAnyType is supported only for dynamic languages:\n", inst->Dump(&std::cerr)));
    ASSERT_DO_EXT_VISITOR(inst->CastToCompareAnyType()->GetAllowedInputType() == profiling::AnyInputType::DEFAULT,
                          (std::cerr << "CompareAnyType doesn't support special values or treating int as double\n",
                           inst->Dump(&std::cerr)));
}

void GraphChecker::VisitCastAnyTypeValue([[maybe_unused]] GraphVisitor *v, [[maybe_unused]] Inst *inst)
{
    ASSERT_DO_EXT_VISITOR(
        static_cast<GraphChecker *>(v)->GetGraph()->IsDynamicMethod(),
        (std::cerr << "CastAnyTypeValue is supported only for dynamic languages:\n", inst->Dump(&std::cerr)));
    if ((inst->CastToCastAnyTypeValue()->GetAllowedInputType() & profiling::AnyInputType::INTEGER) != 0) {
        [[maybe_unused]] auto type = AnyBaseTypeToDataType(inst->CastToCastAnyTypeValue()->GetAnyType());
        ASSERT_DO_EXT_VISITOR(type == DataType::FLOAT64,
                              (std::cerr << "Target type of CastAnyTypeValue with Integer allowed must be FLOAT64\n",
                               inst->Dump(&std::cerr)));
    } else if (inst->CastToCastAnyTypeValue()->GetAllowedInputType() != profiling::AnyInputType::DEFAULT) {
        [[maybe_unused]] auto type = AnyBaseTypeToDataType(inst->CastToCastAnyTypeValue()->GetAnyType());
        ASSERT_DO_EXT_VISITOR(
            type == DataType::FLOAT64 || type == DataType::INT32,
            (std::cerr << "Target type of CastAnyTypeValue with special value allowed must be FLOAT64 or INT32\n",
             inst->Dump(&std::cerr)));
    }
}

void GraphChecker::VisitCastValueToAnyType([[maybe_unused]] GraphVisitor *v, [[maybe_unused]] Inst *inst)
{
    ASSERT_DO_EXT_VISITOR(
        static_cast<GraphChecker *>(v)->GetGraph()->IsDynamicMethod(),
        (std::cerr << "CastValueToAnyType is supported only for dynamic languages:\n", inst->Dump(&std::cerr)));

    const auto *input_inst = inst->GetInput(0).GetInst();
    auto input_type = inst->GetInputType(0);
    auto output_type = AnyBaseTypeToDataType(inst->CastToCastValueToAnyType()->GetAnyType());

    // ASSERT_DO_EXT(input_type != DataType::ANY, // TODO(vpukhov): AnyConst
    //          (std::cerr << "CastValueToAnyType cannot accept inputs of ANY type:\n", inst->Dump(&std::cerr)));

    if (input_inst->IsConst() && (input_type == DataType::Type::INT64 || input_type == DataType::Type::INT32)) {
        if (output_type == DataType::Type::BOOL) {
            ASSERT_DO_EXT_VISITOR(
                input_inst->IsBoolConst(),
                (std::cerr << "Integral constant input not coercible to BOOL:\n", inst->Dump(&std::cerr)));
            return;
        }

        if (output_type == DataType::INT32 && input_type == DataType::INT64) {
            [[maybe_unused]] int64_t value = input_inst->CastToConstant()->GetInt64Value();
            ASSERT_DO_EXT_VISITOR(
                value == static_cast<int32_t>(value),
                (std::cerr << "Integral constant input not coercible to INT32:\n", inst->Dump(&std::cerr)));
            return;
        }

        if (output_type == DataType::Type::REFERENCE) {
            return;  // Always coercible
        }

        if (output_type == DataType::Type::VOID) {
            return;  // Always coercible
        }

        // Otherwise proceed with the generic check.
    }
}

void GraphChecker::VisitAnyTypeCheck([[maybe_unused]] GraphVisitor *v, [[maybe_unused]] Inst *inst)
{
    ASSERT_DO_EXT_VISITOR(
        static_cast<GraphChecker *>(v)->GetGraph()->IsDynamicMethod(),
        (std::cerr << "AnyTypeCheck is supported only for dynamic languages:\n", inst->Dump(&std::cerr)));
    ASSERT_DO_EXT_VISITOR(inst->GetInput(0).GetInst()->GetType() == DataType::Type::ANY,
                          (std::cerr << "First input in AnyTypeCheck must be Any type:\n", inst->Dump(&std::cerr)));
    ASSERT_DO_EXT_VISITOR(inst->GetInput(1).GetInst()->IsSaveState(),
                          (std::cerr << "Second input in AnyTypeCheck must be SaveState:\n", inst->Dump(&std::cerr)));
    if ((inst->CastToAnyTypeCheck()->GetAllowedInputType() & profiling::AnyInputType::INTEGER) != 0) {
        [[maybe_unused]] auto type = AnyBaseTypeToDataType(inst->CastToAnyTypeCheck()->GetAnyType());
        ASSERT_DO_EXT_VISITOR(type == DataType::FLOAT64,
                              (std::cerr << "Target type of AnyTypeCheck with Integer allowed must be FLOAT64\n",
                               inst->Dump(&std::cerr)));
    } else if (inst->CastToAnyTypeCheck()->GetAllowedInputType() != profiling::AnyInputType::DEFAULT) {
        [[maybe_unused]] auto type = AnyBaseTypeToDataType(inst->CastToAnyTypeCheck()->GetAnyType());
        ASSERT_DO_EXT_VISITOR(
            type == DataType::FLOAT64 || type == DataType::INT32,
            (std::cerr << "Target type of AnyTypeCheck with special value allowed must be FLOAT64 or INT32\n",
             inst->Dump(&std::cerr)));
    }
}

void GraphChecker::VisitHclassCheck([[maybe_unused]] GraphVisitor *v, [[maybe_unused]] Inst *inst)
{
    ASSERT_DO_EXT_VISITOR(
        static_cast<GraphChecker *>(v)->GetGraph()->IsDynamicMethod(),
        (std::cerr << "HclassCheck is supported only for dynamic languages:\n", inst->Dump(&std::cerr)));
    ASSERT_DO_EXT_VISITOR(inst->GetInput(0).GetInst()->GetType() == DataType::Type::REFERENCE,
                          (std::cerr << "First input in HclassCheck must be Ref type:\n", inst->Dump(&std::cerr)));
    ASSERT_DO_EXT_VISITOR(
        (inst->GetInput(0).GetInst()->GetOpcode() == Opcode::LoadObject &&
         inst->GetInput(0).GetInst()->CastToLoadObject()->GetObjectType() == ObjectType::MEM_DYN_HCLASS),
        (std::cerr << "First input in HclassCheck must be LoadObject Hclass:\n", inst->Dump(&std::cerr)));
    ASSERT_DO_EXT_VISITOR(inst->GetInput(1).GetInst()->IsSaveState(),
                          (std::cerr << "Second input in HclassCheck must be SaveState:\n", inst->Dump(&std::cerr)));
    [[maybe_unused]] auto hclass_check = inst->CastToHclassCheck();
    ASSERT_DO_EXT_VISITOR(
        hclass_check->GetCheckIsFunction() || hclass_check->GetCheckFunctionIsNotClassConstructor(),
        (std::cerr << "HclassCheck must have at least one check: IsFunction or FunctionIsNotClassConstructor\n",
         inst->Dump(&std::cerr)));
}

void GraphChecker::VisitBitcast([[maybe_unused]] GraphVisitor *v, [[maybe_unused]] Inst *inst)
{
    [[maybe_unused]] auto arch = static_cast<GraphChecker *>(v)->GetGraph()->GetArch();
    ASSERT_DO_EXT_VISITOR(DataType::GetTypeSize(inst->GetInput(0).GetInst()->GetType(), arch) ==
                              DataType::GetTypeSize(inst->GetType(), arch),
                          (std::cerr << "Size of input and output types must be equal\n", inst->Dump(&std::cerr)));
}

void GraphChecker::VisitLoadString([[maybe_unused]] GraphVisitor *v, [[maybe_unused]] Inst *inst)
{
    ASSERT_EXT_VISITOR(!static_cast<GraphChecker *>(v)->GetGraph()->IsDynamicMethod() ||
                       static_cast<GraphChecker *>(v)->GetGraph()->IsBytecodeOptimizer());
}

void GraphChecker::VisitLoadType([[maybe_unused]] GraphVisitor *v, [[maybe_unused]] Inst *inst)
{
    ASSERT_EXT_VISITOR(!static_cast<GraphChecker *>(v)->GetGraph()->IsDynamicMethod());
}

void GraphChecker::VisitLoadUnresolvedType([[maybe_unused]] GraphVisitor *v, [[maybe_unused]] Inst *inst)
{
    ASSERT_EXT_VISITOR(!static_cast<GraphChecker *>(v)->GetGraph()->IsDynamicMethod());
}

void GraphChecker::VisitLoadFromConstantPool([[maybe_unused]] GraphVisitor *v, [[maybe_unused]] Inst *inst)
{
    ASSERT_EXT_VISITOR(static_cast<GraphChecker *>(v)->GetGraph()->IsDynamicMethod());
}

void GraphChecker::VisitLoadImmediate([[maybe_unused]] GraphVisitor *v, Inst *inst)
{
    auto load_imm = inst->CastToLoadImmediate();
    [[maybe_unused]] auto type = inst->GetType();
    [[maybe_unused]] auto obj_type = load_imm->GetObjectType();
    ASSERT_EXT_VISITOR(obj_type != LoadImmediateInst::ObjectType::UNKNOWN);
    ASSERT_EXT_VISITOR((type == DataType::REFERENCE && obj_type == LoadImmediateInst::ObjectType::CLASS) ||
                       (type == DataType::POINTER && (obj_type == LoadImmediateInst::ObjectType::METHOD ||
                                                      obj_type == LoadImmediateInst::ObjectType::STRING ||
                                                      obj_type == LoadImmediateInst::ObjectType::PANDA_FILE_OFFSET)) ||
                       (type == DataType::ANY && obj_type == LoadImmediateInst::ObjectType::CONSTANT_POOL));
    ASSERT_EXT_VISITOR(obj_type != LoadImmediateInst::ObjectType::PANDA_FILE_OFFSET ||
                       static_cast<GraphChecker *>(v)->GetGraph()->IsAotMode());
    ASSERT_EXT_VISITOR(load_imm->GetObject() != nullptr);
}

// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define VisitBinaryShiftedRegister(opc)                                                         \
    void GraphChecker::Visit##opc(GraphVisitor *v, Inst *inst)                                  \
    {                                                                                           \
        CheckBinaryOperationWithShiftedOperandTypes(                                            \
            v, inst, inst->GetOpcode() != Opcode::AddSR && inst->GetOpcode() != Opcode::SubSR); \
    }

VisitBinaryShiftedRegister(AddSR) VisitBinaryShiftedRegister(SubSR) VisitBinaryShiftedRegister(AndSR)
    VisitBinaryShiftedRegister(OrSR) VisitBinaryShiftedRegister(XorSR) VisitBinaryShiftedRegister(AndNotSR)
        VisitBinaryShiftedRegister(OrNotSR) VisitBinaryShiftedRegister(XorNotSR)
#undef VisitBinaryShiftedRegister

            void GraphChecker::VisitNegSR([[maybe_unused]] GraphVisitor *v, [[maybe_unused]] Inst *inst)
{
    ASSERT_DO_EXT_VISITOR(DataType::GetCommonType(inst->GetType()) == DataType::INT64,
                          (std::cerr << "NegSR must have integer type\n", inst->Dump(&std::cerr)));
    CheckUnaryOperationTypes(inst);
    [[maybe_unused]] auto shift_type = static_cast<UnaryShiftedRegisterOperation *>(inst)->GetShiftType();
    ASSERT_DO_EXT_VISITOR(shift_type != ShiftType::INVALID_SHIFT && shift_type != ShiftType::ROR,
                          (std::cerr << "Operation has invalid shift type\n", inst->Dump(&std::cerr)));
}

#undef ASSERT_DO_EXT
#undef ASSERT_DO_EXT_VISITOR

}  // namespace panda::compiler
