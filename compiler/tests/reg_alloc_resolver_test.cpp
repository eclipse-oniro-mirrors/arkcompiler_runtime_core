/**
 * Copyright (c) 2024 Shenzhen Kaihong Digital Industry Development Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Copyright (c) 2024-2026 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at

 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <gtest/gtest.h>
#include <utility>
#include <vector>

#include "graph_test.h"
#include "optimizer/analysis/liveness_analyzer.h"
#include "optimizer/ir/constants.h"
#include "optimizer/ir/graph.h"
#include "optimizer/ir/inst.h"
#include "optimizer/ir/locations.h"
#include "optimizer/optimizations/regalloc/reg_alloc_resolver.h"
#include "optimizer/optimizations/regalloc/spill_fills_resolver.h"
#include "optimizer/optimizations/regalloc/split_resolver.h"
#include "reg_acc_alloc.h"
#include "utils/arena_containers.h"

using namespace testing::ext;

namespace panda::compiler {
class RegAllocResolverTest : public testing::Test {
public:
    using CatchPhiSaveCandidate = RegAllocResolver::CatchPhiSaveCandidate;

    static void SetUpTestCase(void) {};
    static void TearDownTestCase(void) {};
    void SetUp() {};
    void TearDown() {};

    GraphTest graph_test_;

    template<typename Callback>
    static void ForEachInst(Graph *graph, Callback cb)
    {
        ASSERT(graph != nullptr);
        for (auto bb : graph->GetBlocksRPO()) {
            for (auto inst : bb->AllInsts()) {
                cb(inst);
            }
        }
    }

    static bool IsIntrinsic(Inst *inst, IntrinsicInst::IntrinsicId id)
    {
        return inst->IsIntrinsic() && inst->CastToIntrinsic()->GetIntrinsicId() == id;
    }

    static void InitUsedRegs(Graph *graph, size_t count)
    {
        ASSERT(graph != nullptr);
        ArenaVector<bool> used_regs(count, false, graph->GetAllocator()->Adapter());
        graph->InitUsedRegs<DataType::INT64>(&used_regs);
    }

    static Graph *CreateBytecodeGraph(ArenaAllocator *allocator, ArenaAllocator *local_allocator)
    {
        auto graph = allocator->New<Graph>(allocator, local_allocator, Arch::NONE, false, true);
        graph->CreateStartBlock();
        graph->CreateEndBlock();
        return graph;
    }

    static SpillFillInst *AppendMove(Graph *graph, BasicBlock *block, Register src, Register dst)
    {
        auto spill_fill = graph->CreateInstSpillFill();
        spill_fill->SetSpillFillType(SpillFillType::INPUT_FILL);
        spill_fill->AddMove(src, dst, DataType::ANY);
        block->AppendInst(spill_fill);
        return spill_fill;
    }

    static void AppendRegisterWrite(Graph *graph, BasicBlock *block, Register dst, DataType::Type type = DataType::ANY)
    {
        auto writer = graph->CreateInstParameter(0);
        writer->SetType(type);
        writer->SetDstReg(dst);
        block->AppendInst(writer);
    }

    static void AppendCandidate(Graph *graph, BasicBlock *block, Register src, Register slot,
                                ArenaVector<CatchPhiSaveCandidate> *candidates)
    {
        auto spill_fill = AppendMove(graph, block, src, slot);
        candidates->push_back({spill_fill, 0, src, slot});
    }

    static void EliminateRedundantCatchPhiSaves(Graph *graph, const ArenaVector<CatchPhiSaveCandidate> &candidates)
    {
        RegAllocResolver::EliminateRedundantCatchPhiSaves(graph, candidates);
    }

    static size_t CountMoves(Graph *graph, Register src, Register dst)
    {
        size_t count = 0;
        ForEachInst(graph, [src, dst, &count](Inst *inst) {
            if (!inst->IsSpillFill()) {
                return;
            }
            for (const auto &spill_fill : inst->CastToSpillFill()->GetSpillFills()) {
                if (spill_fill.SrcType() == LocationType::REGISTER && spill_fill.DstType() == LocationType::REGISTER &&
                    spill_fill.SrcValue() == src && spill_fill.DstValue() == dst) {
                    ++count;
                }
            }
        });
        return count;
    }
};

/**
 * @tc.name: reg_alloc_resolver_test_001
 * @tc.desc: Verify the AddMoveToFixedLocation function.
 * @tc.type: FUNC
 * @tc.require:
 */
HWTEST_F(RegAllocResolverTest, reg_alloc_resolver_test_001, TestSize.Level1)
{
    std::string pfile = GRAPH_TEST_ABC_DIR "regallocTest.abc";
    const char *test_method_name = "func4";
    bool status = false;
    graph_test_.TestBuildGraphFromFile(pfile, [&test_method_name, &status](Graph* graph, std::string &method_name) {
        if (test_method_name != method_name) {
            return;
        }

        graph->RunPass<LivenessAnalyzer>();
        auto intervals = graph->GetAnalysis<LivenessAnalyzer>().GetLifeIntervals();

        graph->RunPass<bytecodeopt::RegAccAlloc>();

        // Allocate registers
        Register preassign_count = 0;
        for (auto interval : intervals) {
            interval->SetPreassignedReg(preassign_count++);
        }

        // Make some insts require a fixed input register, then spillfill insts are expected to be created.
        Register fixed_input_reg = preassign_count;
        std::vector<std::pair<Inst*, Register>> pairs;
        ForEachInst(graph, [&pairs, &fixed_input_reg](Inst *inst) {
            if (IsIntrinsic(inst, IntrinsicInst::IntrinsicId::ADD2_IMM8_V8) ||
                IsIntrinsic(inst, IntrinsicInst::IntrinsicId::SUB2_IMM8_V8)) {
                EXPECT_GE(inst->GetInputsCount(), 2);

                inst->SetLocation(0, Location::MakeRegister(fixed_input_reg));
                inst->SetLocation(1, Location::MakeRegister(fixed_input_reg + 1));
                pairs.emplace_back(inst, fixed_input_reg);
                fixed_input_reg += 2;
            }
        });
        EXPECT_FALSE(pairs.empty());

        // Run resolver
        InitUsedRegs(graph, 256);
        RegAllocResolver(graph).Resolve();

        for (auto [inst, input_reg] : pairs) {
            EXPECT_EQ(inst->GetSrcReg(0), input_reg);
            EXPECT_EQ(inst->GetSrcReg(1), input_reg + 1);

            auto sf_inst = inst->GetPrev();
            EXPECT_TRUE(sf_inst != nullptr);
            EXPECT_TRUE(sf_inst->IsSpillFill());
            EXPECT_EQ(sf_inst->CastToSpillFill()->GetSpillFillType(), SpillFillType::INPUT_FILL);

            auto sf_data1 = sf_inst->CastToSpillFill()->GetSpillFill(0);
            EXPECT_EQ(sf_data1.GetSrc(), inst->GetInput(0).GetInst()->GetDstLocation());
            EXPECT_EQ(sf_data1.GetDst(), inst->GetLocation(0));

            auto sf_data2 = sf_inst->CastToSpillFill()->GetSpillFill(1);
            EXPECT_EQ(sf_data2.GetSrc(), inst->GetInput(1).GetInst()->GetDstLocation());
            EXPECT_EQ(sf_data2.GetDst(), inst->GetLocation(1));
        }

        status = true;
    });
    EXPECT_TRUE(status);
}

/**
 * @tc.name: reg_alloc_resolver_test_002
 * @tc.desc: Verify redundant CatchPhi register saves are eliminated.
 * @tc.type: FUNC
 * @tc.require:
 */
HWTEST_F(RegAllocResolverTest, reg_alloc_resolver_test_002, TestSize.Level1)
{
    ArenaAllocator allocator {SpaceType::SPACE_TYPE_COMPILER};
    ArenaAllocator local_allocator {SpaceType::SPACE_TYPE_COMPILER, nullptr, true};
    auto graph = CreateBytecodeGraph(&allocator, &local_allocator);

    auto start = graph->GetStartBlock();
    auto body = graph->CreateEmptyBlock();
    auto end = graph->GetEndBlock();
    start->AddSucc(body);
    body->AddSucc(end);

    constexpr Register SRC_REG = 1;
    constexpr Register SLOT_REG = 2;
    ArenaVector<CatchPhiSaveCandidate> candidates(local_allocator.Adapter());
    for (size_t i = 0; i < 2; ++i) {
        AppendCandidate(graph, body, SRC_REG, SLOT_REG, &candidates);
    }

    EliminateRedundantCatchPhiSaves(graph, candidates);
    EXPECT_EQ(CountMoves(graph, SRC_REG, SLOT_REG), 1U);
}

/**
 * @tc.name: reg_alloc_resolver_test_003
 * @tc.desc: Verify a source register write keeps the next CatchPhi save.
 * @tc.type: FUNC
 * @tc.require:
 */
HWTEST_F(RegAllocResolverTest, reg_alloc_resolver_test_003, TestSize.Level1)
{
    ArenaAllocator allocator {SpaceType::SPACE_TYPE_COMPILER};
    ArenaAllocator local_allocator {SpaceType::SPACE_TYPE_COMPILER, nullptr, true};
    auto graph = CreateBytecodeGraph(&allocator, &local_allocator);
    auto body = graph->CreateEmptyBlock();
    graph->GetStartBlock()->AddSucc(body);
    body->AddSucc(graph->GetEndBlock());

    constexpr Register SRC_REG = 1;
    constexpr Register SLOT_REG = 2;
    ArenaVector<CatchPhiSaveCandidate> candidates(local_allocator.Adapter());
    AppendCandidate(graph, body, SRC_REG, SLOT_REG, &candidates);
    AppendRegisterWrite(graph, body, SRC_REG);
    AppendCandidate(graph, body, SRC_REG, SLOT_REG, &candidates);

    EliminateRedundantCatchPhiSaves(graph, candidates);
    EXPECT_EQ(CountMoves(graph, SRC_REG, SLOT_REG), 2U);
}

/**
 * @tc.name: reg_alloc_resolver_test_004
 * @tc.desc: Verify a slot register write keeps the next CatchPhi save.
 * @tc.type: FUNC
 * @tc.require:
 */
HWTEST_F(RegAllocResolverTest, reg_alloc_resolver_test_004, TestSize.Level1)
{
    ArenaAllocator allocator {SpaceType::SPACE_TYPE_COMPILER};
    ArenaAllocator local_allocator {SpaceType::SPACE_TYPE_COMPILER, nullptr, true};
    auto graph = CreateBytecodeGraph(&allocator, &local_allocator);
    auto body = graph->CreateEmptyBlock();
    graph->GetStartBlock()->AddSucc(body);
    body->AddSucc(graph->GetEndBlock());

    constexpr Register SRC_REG = 1;
    constexpr Register SLOT_REG = 2;
    ArenaVector<CatchPhiSaveCandidate> candidates(local_allocator.Adapter());
    AppendCandidate(graph, body, SRC_REG, SLOT_REG, &candidates);
    AppendRegisterWrite(graph, body, SLOT_REG);
    AppendCandidate(graph, body, SRC_REG, SLOT_REG, &candidates);

    EliminateRedundantCatchPhiSaves(graph, candidates);
    EXPECT_EQ(CountMoves(graph, SRC_REG, SLOT_REG), 2U);
}

/**
 * @tc.name: CatchPhiSaveSurvivesFloatRegisterWrite
 * @tc.desc: Verify floating-point writes invalidate saved virtual-register equalities.
 * @tc.type: FUNC
 * @tc.require:
 */
HWTEST_F(RegAllocResolverTest, CatchPhiSaveSurvivesFloatRegisterWrite, TestSize.Level1)
{
    ArenaAllocator allocator {SpaceType::SPACE_TYPE_COMPILER};
    ArenaAllocator local_allocator {SpaceType::SPACE_TYPE_COMPILER, nullptr, true};
    auto graph = CreateBytecodeGraph(&allocator, &local_allocator);
    auto body = graph->CreateEmptyBlock();
    graph->GetStartBlock()->AddSucc(body);
    body->AddSucc(graph->GetEndBlock());

    constexpr Register SRC_REG = 1;
    constexpr Register SLOT_REG = 2;
    ArenaVector<CatchPhiSaveCandidate> candidates(local_allocator.Adapter());
    AppendCandidate(graph, body, SRC_REG, SLOT_REG, &candidates);
    // Bytecode FLOAT64 values occupy ordinary virtual registers too.
    AppendRegisterWrite(graph, body, SRC_REG, DataType::FLOAT64);
    AppendCandidate(graph, body, SRC_REG, SLOT_REG, &candidates);
    AppendRegisterWrite(graph, body, SLOT_REG, DataType::FLOAT64);
    AppendCandidate(graph, body, SRC_REG, SLOT_REG, &candidates);

    EliminateRedundantCatchPhiSaves(graph, candidates);
    EXPECT_EQ(CountMoves(graph, SRC_REG, SLOT_REG), 3U);
}

HWTEST_F(RegAllocResolverTest, reg_alloc_resolver_test_005, TestSize.Level1)
{
    ArenaAllocator allocator {SpaceType::SPACE_TYPE_COMPILER};
    ArenaAllocator local_allocator {SpaceType::SPACE_TYPE_COMPILER, nullptr, true};
    auto graph = CreateBytecodeGraph(&allocator, &local_allocator);
    auto left = graph->CreateEmptyBlock();
    auto right = graph->CreateEmptyBlock();
    auto merge = graph->CreateEmptyBlock();
    graph->GetStartBlock()->AddSucc(left);
    graph->GetStartBlock()->AddSucc(right);
    left->AddSucc(merge);
    right->AddSucc(merge);
    merge->AddSucc(graph->GetEndBlock());

    constexpr Register SRC_REG = 1;
    constexpr Register SLOT_REG = 2;
    ArenaVector<CatchPhiSaveCandidate> candidates(local_allocator.Adapter());
    AppendCandidate(graph, left, SRC_REG, SLOT_REG, &candidates);
    AppendCandidate(graph, merge, SRC_REG, SLOT_REG, &candidates);

    EliminateRedundantCatchPhiSaves(graph, candidates);
    EXPECT_EQ(CountMoves(graph, SRC_REG, SLOT_REG), 2U);
}

/**
 * @tc.name: reg_alloc_resolver_test_006
 * @tc.desc: Verify identical predecessor facts survive a merge.
 * @tc.type: FUNC
 * @tc.require:
 */
HWTEST_F(RegAllocResolverTest, reg_alloc_resolver_test_006, TestSize.Level1)
{
    ArenaAllocator allocator {SpaceType::SPACE_TYPE_COMPILER};
    ArenaAllocator local_allocator {SpaceType::SPACE_TYPE_COMPILER, nullptr, true};
    auto graph = CreateBytecodeGraph(&allocator, &local_allocator);
    auto left = graph->CreateEmptyBlock();
    auto right = graph->CreateEmptyBlock();
    auto merge = graph->CreateEmptyBlock();
    graph->GetStartBlock()->AddSucc(left);
    graph->GetStartBlock()->AddSucc(right);
    left->AddSucc(merge);
    right->AddSucc(merge);
    merge->AddSucc(graph->GetEndBlock());

    constexpr Register SRC_REG = 1;
    constexpr Register SLOT_REG = 2;
    ArenaVector<CatchPhiSaveCandidate> candidates(local_allocator.Adapter());
    AppendCandidate(graph, left, SRC_REG, SLOT_REG, &candidates);
    AppendCandidate(graph, right, SRC_REG, SLOT_REG, &candidates);
    AppendCandidate(graph, merge, SRC_REG, SLOT_REG, &candidates);

    EliminateRedundantCatchPhiSaves(graph, candidates);
    EXPECT_EQ(CountMoves(graph, SRC_REG, SLOT_REG), 2U);
}

/**
 * @tc.name: reg_alloc_resolver_test_007
 * @tc.desc: Verify loop fixed point removes only the save covered before the first iteration.
 * @tc.type: FUNC
 * @tc.require:
 */
HWTEST_F(RegAllocResolverTest, reg_alloc_resolver_test_007, TestSize.Level1)
{
    ArenaAllocator allocator {SpaceType::SPACE_TYPE_COMPILER};
    ArenaAllocator local_allocator {SpaceType::SPACE_TYPE_COMPILER, nullptr, true};
    auto graph = CreateBytecodeGraph(&allocator, &local_allocator);
    auto preheader = graph->CreateEmptyBlock();
    auto header = graph->CreateEmptyBlock();
    auto loop = graph->CreateEmptyBlock();
    graph->GetStartBlock()->AddSucc(preheader);
    preheader->AddSucc(header);
    header->AddSucc(loop);
    header->AddSucc(graph->GetEndBlock());
    loop->AddSucc(header);

    constexpr Register SRC_REG = 1;
    constexpr Register SLOT_REG = 2;
    ArenaVector<CatchPhiSaveCandidate> candidates(local_allocator.Adapter());
    AppendCandidate(graph, preheader, SRC_REG, SLOT_REG, &candidates);
    AppendCandidate(graph, loop, SRC_REG, SLOT_REG, &candidates);

    EliminateRedundantCatchPhiSaves(graph, candidates);
    EXPECT_EQ(CountMoves(graph, SRC_REG, SLOT_REG), 1U);
}

/**
 * @tc.name: reg_alloc_resolver_test_008
 * @tc.desc: Verify catch entry and untracked moves never provide a removable fact.
 * @tc.type: FUNC
 * @tc.require:
 */
HWTEST_F(RegAllocResolverTest, reg_alloc_resolver_test_008, TestSize.Level1)
{
    ArenaAllocator allocator {SpaceType::SPACE_TYPE_COMPILER};
    ArenaAllocator local_allocator {SpaceType::SPACE_TYPE_COMPILER, nullptr, true};
    auto graph = CreateBytecodeGraph(&allocator, &local_allocator);
    auto try_block = graph->CreateEmptyBlock();
    auto catch_block = graph->CreateEmptyBlock();
    catch_block->SetCatchBegin(true);
    graph->GetStartBlock()->AddSucc(try_block);
    try_block->AddSucc(catch_block);
    catch_block->AddSucc(graph->GetEndBlock());

    constexpr Register SRC_REG = 1;
    constexpr Register SLOT_REG = 2;
    ArenaVector<CatchPhiSaveCandidate> candidates(local_allocator.Adapter());
    AppendCandidate(graph, try_block, SRC_REG, SLOT_REG, &candidates);
    AppendMove(graph, catch_block, SRC_REG, SLOT_REG);
    AppendCandidate(graph, catch_block, SRC_REG, SLOT_REG, &candidates);

    EliminateRedundantCatchPhiSaves(graph, candidates);
    EXPECT_EQ(CountMoves(graph, SRC_REG, SLOT_REG), 3U);
}

/**
 * @tc.name: reg_alloc_resolver_test_009
 * @tc.desc: Verify parallel moves that overwrite the source prevent a fact from escaping the bundle.
 * @tc.type: FUNC
 * @tc.require:
 */
HWTEST_F(RegAllocResolverTest, reg_alloc_resolver_test_009, TestSize.Level1)
{
    ArenaAllocator allocator {SpaceType::SPACE_TYPE_COMPILER};
    ArenaAllocator local_allocator {SpaceType::SPACE_TYPE_COMPILER, nullptr, true};
    auto graph = CreateBytecodeGraph(&allocator, &local_allocator);
    auto body = graph->CreateEmptyBlock();
    graph->GetStartBlock()->AddSucc(body);
    body->AddSucc(graph->GetEndBlock());

    constexpr Register SRC_REG = 1;
    constexpr Register SLOT_REG = 2;
    ArenaVector<CatchPhiSaveCandidate> candidates(local_allocator.Adapter());
    AppendCandidate(graph, body, SRC_REG, SLOT_REG, &candidates);
    auto parallel_moves = AppendMove(graph, body, SRC_REG, SLOT_REG);
    parallel_moves->AddMove(3, SRC_REG, DataType::ANY);
    candidates.push_back({parallel_moves, 0, SRC_REG, SLOT_REG});
    AppendCandidate(graph, body, SRC_REG, SLOT_REG, &candidates);

    EliminateRedundantCatchPhiSaves(graph, candidates);
    EXPECT_EQ(CountMoves(graph, SRC_REG, SLOT_REG), 2U);
}

/**
 * @tc.name: reg_alloc_resolver_test_010
 * @tc.desc: Verify a loop save remains when no fact is available before the first iteration.
 * @tc.type: FUNC
 * @tc.require:
 */
HWTEST_F(RegAllocResolverTest, reg_alloc_resolver_test_010, TestSize.Level1)
{
    ArenaAllocator allocator {SpaceType::SPACE_TYPE_COMPILER};
    ArenaAllocator local_allocator {SpaceType::SPACE_TYPE_COMPILER, nullptr, true};
    auto graph = CreateBytecodeGraph(&allocator, &local_allocator);
    auto header = graph->CreateEmptyBlock();
    auto loop = graph->CreateEmptyBlock();
    graph->GetStartBlock()->AddSucc(header);
    header->AddSucc(loop);
    header->AddSucc(graph->GetEndBlock());
    loop->AddSucc(header);

    constexpr Register SRC_REG = 1;
    constexpr Register SLOT_REG = 2;
    ArenaVector<CatchPhiSaveCandidate> candidates(local_allocator.Adapter());
    AppendCandidate(graph, loop, SRC_REG, SLOT_REG, &candidates);

    EliminateRedundantCatchPhiSaves(graph, candidates);
    EXPECT_EQ(CountMoves(graph, SRC_REG, SLOT_REG), 1U);
}

/**
 * @tc.name: reg_alloc_resolver_test_011
 * @tc.desc: Verify production CatchPhi candidate recording and downstream SpillFill resolution.
 * @tc.type: FUNC
 * @tc.require:
 */
HWTEST_F(RegAllocResolverTest, reg_alloc_resolver_test_011, TestSize.Level1)
{
    ArenaAllocator allocator {SpaceType::SPACE_TYPE_COMPILER};
    ArenaAllocator local_allocator {SpaceType::SPACE_TYPE_COMPILER, nullptr, true};
    auto graph = CreateBytecodeGraph(&allocator, &local_allocator);
    auto body = graph->CreateEmptyBlock();
    auto catch_block = graph->CreateEmptyBlock();
    catch_block->SetCatch(true);
    catch_block->SetCatchBegin(true);
    graph->GetStartBlock()->AddSucc(body);
    body->AddSucc(graph->GetEndBlock());
    body->AddSucc(catch_block);
    catch_block->AddSucc(graph->GetEndBlock());

    auto parameter = graph->CreateInstParameter(0);
    parameter->SetType(DataType::ANY);
    graph->GetStartBlock()->AppendInst(parameter);
    auto first_throwable = graph->CreateInstParameter(1);
    first_throwable->SetType(DataType::ANY);
    auto second_throwable = graph->CreateInstParameter(2);
    second_throwable->SetType(DataType::ANY);
    body->AppendInst(first_throwable);
    body->AppendInst(second_throwable);

    auto catch_phi = graph->CreateInstCatchPhi(DataType::ANY, 0);
    catch_block->AppendInst(catch_phi);
    catch_phi->AppendInput(parameter);
    catch_phi->AppendThrowableInst(first_throwable);
    catch_phi->AppendInput(parameter);
    catch_phi->AppendThrowableInst(second_throwable);
    graph->AppendThrowableInst(first_throwable, catch_block);
    graph->AppendThrowableInst(second_throwable, catch_block);

    ASSERT_TRUE(graph->RunPass<LivenessAnalyzer>());
    graph->InitDefaultLocations();
    auto &liveness = graph->GetAnalysis<LivenessAnalyzer>();
    liveness.GetInstLifeIntervals(parameter)->SetLocation(Location::MakeRegister(1));
    liveness.GetInstLifeIntervals(catch_phi)->SetLocation(Location::MakeRegister(2));
    InitUsedRegs(graph, 16);

    RegAllocResolver resolver(graph);
    resolver.Resolve();
    EXPECT_EQ(CountMoves(graph, 1, 2), 2U);

    SplitResolver(graph).Run();
    resolver.EliminateRedundantCatchPhiSaves();
    EXPECT_EQ(CountMoves(graph, 1, 2), 1U);

    SpillFillsResolver(graph).Run();
    EXPECT_EQ(CountMoves(graph, 1, 2), 1U);
}
}  // namespace panda::compiler
