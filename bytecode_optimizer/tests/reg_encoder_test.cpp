/**
 * Copyright (c) 2023-2026 Huawei Device Co., Ltd.
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
 */

#include <gtest/gtest.h>

#include "compiler/optimizer/ir/basicblock.h"
#include "compiler/optimizer/optimizations/regalloc/reg_alloc.h"
#include "graph_test.h"
#include "mem/pool_manager.h"
#include "reg_acc_alloc.h"
#include "reg_encoder.h"

using namespace testing::ext;

namespace panda::bytecodeopt {
using namespace compiler;
using namespace panda;
class RegEncoderTest : public testing::Test {
public:
    static void SetUpTestCase(void) {}
    static void TearDownTestCase(void) {}
    void SetUp() {}
    void TearDown() {}

    GraphTest graph_test_;
};

class RegEncoderRuntime : public RuntimeInterface {
public:
    size_t GetMethodTotalArgumentsCount([[maybe_unused]] MethodPtr method) const override
    {
        return 3;
    }
};

HWTEST_F(RegEncoderTest, SparseLocalsDoNotOverlapArguments, TestSize.Level1)
{
    constexpr Register NUM_LOCALS = 8;
    constexpr Register NUM_ARGS = 3;
    constexpr Register ARG_START = VIRTUAL_FRAME_SIZE - NUM_ARGS;
    for (bool leading_hole : {false, true}) {
        for (bool has_temp : {false, true}) {
            ArenaAllocator allocator {SpaceType::SPACE_TYPE_COMPILER};
            ArenaAllocator local_allocator {SpaceType::SPACE_TYPE_COMPILER};
            RegEncoderRuntime runtime;
            auto *graph = allocator.New<Graph>(&allocator, &local_allocator, Arch::NONE, nullptr, &runtime, false,
                                               nullptr, true, true);
            auto *start = graph->CreateStartBlock();
            auto *body = graph->CreateEmptyBlock();
            auto *end = graph->CreateEndBlock();
            start->AddSucc(body);
            body->AddSucc(end);

            ArenaVector<bool> used_regs(allocator.Adapter());
            used_regs.resize(VIRTUAL_FRAME_SIZE, false);
            for (Register reg = 0; reg < NUM_LOCALS; ++reg) {
                used_regs[reg] = reg != 3 && (!leading_hole || reg != 0);
            }
            // The middle argument is unused; it must still occupy a frame slot.
            used_regs[ARG_START] = true;
            used_regs[ARG_START + 2] = true;
            used_regs[ARG_START - 1] = has_temp;
            graph->InitUsedRegs<DataType::INT64>(&used_regs);

            auto *moves = graph->CreateInstSpillFill();
            moves->AddMove(NUM_LOCALS - 1, ARG_START, DataType::ANY);
            moves->AddMove(ARG_START + 2, 1, DataType::ANY);
            if (has_temp) {
                moves->AddMove(ARG_START - 1, 2, DataType::ANY);
            }
            body->AppendInst(moves);

            RegEncoder encoder(graph);
            ASSERT_TRUE(encoder.RunImpl());
            auto expected_args_start = NUM_LOCALS + static_cast<Register>(has_temp);
            EXPECT_EQ(graph->GetStackSlotsCount(), expected_args_start);
            EXPECT_EQ(moves->GetSpillFill(0).SrcValue(), NUM_LOCALS - 1);
            EXPECT_EQ(moves->GetSpillFill(0).DstValue(), expected_args_start);
            EXPECT_EQ(moves->GetSpillFill(1).SrcValue(), expected_args_start + 2);
            if (has_temp) {
                EXPECT_EQ(moves->GetSpillFill(2).SrcValue(), NUM_LOCALS);
            }
        }
    }
}

/**
 * @tc.name: reg_encoder_test_001
 * @tc.desc: Verify the RunImpl function.
 * @tc.type: FUNC
 * @tc.require: issueNumber
 */
HWTEST_F(RegEncoderTest, reg_encoder_test_001, TestSize.Level1)
{
    std::string pFile = GRAPH_TEST_ABC_DIR "codegenTryCatch.abc";
    const char *test_method_name = "func4";
    bool status = false;

    graph_test_.TestBuildGraphFromFile(pFile, [test_method_name, &status](Graph* graph, std::string &method_name) {
        if (test_method_name != method_name) {
            return;
        }
        status = true;
        EXPECT_NE(graph, nullptr);
        EXPECT_TRUE(RegAlloc(graph));
        EXPECT_TRUE(graph->RunPass<RegEncoder>());
    });
    EXPECT_TRUE(status);
}

/**
 * @tc.name: reg_encoder_test_002
 * @tc.desc: Verify the RunImpl function.
 * @tc.type: FUNC
 * @tc.require: issueNumber
 */
HWTEST_F(RegEncoderTest, reg_encoder_test_002, TestSize.Level1)
{
    std::string pFile = GRAPH_TEST_ABC_DIR "optimizeTryCatch.abc";
    const char *test_method_name = "func_main_0";
    bool status = false;

    graph_test_.TestBuildGraphFromFile(pFile, [test_method_name, &status](Graph* graph, std::string &method_name) {
        if (test_method_name != method_name) {
            return;
        }
        status = true;
        EXPECT_NE(graph, nullptr);
        EXPECT_TRUE(graph->RunPass<RegAccAlloc>());
    });
    EXPECT_TRUE(status);
}
}  // namespace panda::bytecodeopt
