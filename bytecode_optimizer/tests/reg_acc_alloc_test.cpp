/**
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

#include <gtest/gtest.h>

#include "compiler/optimizer/ir/basicblock.h"
#include "libpandabase/mem/arena_allocator.h"
#include "libpandabase/mem/mem.h"
#include "libpandabase/mem/pool_manager.h"
#include "reg_acc_alloc.h"

namespace panda::bytecodeopt {

using compiler::BasicBlock;
using compiler::Graph;
using compiler::Inst;
using compiler::RuntimeInterface;
namespace DataType = compiler::DataType;

class RegAccAllocTest : public testing::Test {
public:
    RegAccAllocTest()
    {
        mem::MemConfig::Initialize(128_MB, 64_MB, 64_MB, 32_MB);
        PoolManager::Initialize();
        allocator_ = new ArenaAllocator(SpaceType::SPACE_TYPE_INTERNAL);
        local_allocator_ = new ArenaAllocator(SpaceType::SPACE_TYPE_INTERNAL);
    }

    ~RegAccAllocTest() override
    {
        delete allocator_;
        delete local_allocator_;
        PoolManager::Finalize();
        mem::MemConfig::Finalize();
    }

protected:
    Graph *CreateGraph()
    {
        auto *graph = allocator_->New<Graph>(allocator_, local_allocator_, Arch::NONE, true, true);
        auto *start = graph->CreateStartBlock();
        auto *body = graph->CreateEmptyBlock();
        auto *end = graph->CreateEndBlock();
        start->AddSucc(body);
        body->AddSucc(end);
        body_ = body;
        return graph;
    }

    Inst *AddAccProducer(Graph *graph)
    {
        auto *producer = graph->CreateInstIntrinsic(DataType::ANY, compiler::INVALID_PC,
                                                    RuntimeInterface::IntrinsicId::LDEXTERNALMODULEVAR_IMM8);
        producer->ClearFlag(compiler::inst_flags::REQUIRE_STATE);
        producer->SetFlag(compiler::inst_flags::ACC_WRITE);
        body_->AppendInst(producer);
        return producer;
    }

    Inst *AddAccReadOnlyCheck(Graph *graph, Inst *input)
    {
        auto *check =
            graph->CreateInstIntrinsic(DataType::VOID, compiler::INVALID_PC,
                                       RuntimeInterface::IntrinsicId::THROW_UNDEFINEDIFHOLEWITHNAME_PREF_ID16);
        check->ReserveInputs(2);
        check->AllocateInputTypes(graph->GetAllocator(), 2);
        check->AppendInput(input);
        check->AddInputType(DataType::ANY);
        auto *save_state = graph->CreateInstSaveState();
        check->AppendInput(save_state);
        check->AddInputType(DataType::NO_TYPE);
        check->SetFlag(compiler::inst_flags::ACC_READ);
        check->ClearFlag(compiler::inst_flags::ACC_WRITE);
        body_->AppendInst(save_state);
        body_->AppendInst(check);
        return check;
    }

    Inst *AddReturn(Graph *graph, Inst *input)
    {
        auto *ret = graph->CreateInstReturn(DataType::ANY, compiler::INVALID_PC);
        ret->SetInput(0, input);
        body_->AppendInst(ret);
        return ret;
    }

    ArenaAllocator *allocator_ {nullptr};
    ArenaAllocator *local_allocator_ {nullptr};
    BasicBlock *body_ {nullptr};
};

TEST_F(RegAccAllocTest, PreservesAccumulatorAcrossReadOnlyCheck)
{
    auto *graph = CreateGraph();
    auto *producer = AddAccProducer(graph);
    auto *check = AddAccReadOnlyCheck(graph, producer);
    auto *ret = AddReturn(graph, producer);

    ASSERT_TRUE(graph->RunPass<RegAccAlloc>());

    EXPECT_EQ(producer->GetDstReg(), compiler::ACC_REG_ID);
    EXPECT_EQ(check->GetSrcReg(0), compiler::ACC_REG_ID);
    EXPECT_EQ(ret->GetSrcReg(0), compiler::ACC_REG_ID);
}

TEST_F(RegAccAllocTest, DoesNotPreserveAccumulatorAcrossAccWrite)
{
    auto *graph = CreateGraph();
    auto *producer = AddAccProducer(graph);
    AddAccReadOnlyCheck(graph, producer);
    AddAccProducer(graph);
    auto *ret = AddReturn(graph, producer);

    ASSERT_TRUE(graph->RunPass<RegAccAlloc>());

    EXPECT_NE(ret->GetSrcReg(0), compiler::ACC_REG_ID);
}

}  // namespace panda::bytecodeopt
