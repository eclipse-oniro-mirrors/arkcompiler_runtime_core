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

#include <gtest/gtest.h>

#include <initializer_list>
#include <utility>
#include <vector>

#include "libpandabase/mem/arena_allocator.h"
#include "libpandabase/mem/mem.h"
#include "libpandabase/mem/pool_manager.h"
#include "compiler/optimizer/ir/basicblock.h"
#include "peephole_optimizer.h"

namespace panda::bytecodeopt::test {

using compiler::BasicBlock;
using compiler::Graph;
using compiler::Inst;
using compiler::RuntimeInterface;
namespace DataType = compiler::DataType;

class PeepholeOptimizerTest : public testing::Test {
public:
    PeepholeOptimizerTest()
    {
        mem::MemConfig::Initialize(128_MB, 64_MB, 64_MB, 32_MB);
        PoolManager::Initialize();
        allocator_ = new ArenaAllocator(SpaceType::SPACE_TYPE_INTERNAL);
        local_allocator_ = new ArenaAllocator(SpaceType::SPACE_TYPE_INTERNAL);
    }

    ~PeepholeOptimizerTest() override
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

    Inst *AddParameter(Graph *graph, int id, uint16_t arg_number, DataType::Type type)
    {
        auto *param = graph->AddNewParameter(arg_number);
        param->SetId(id);
        param->SetType(type);
        return param;
    }

    Inst *AddIntrinsic(Graph *graph, int id, RuntimeInterface::IntrinsicId intrinsic_id,
                       std::initializer_list<std::pair<DataType::Type, Inst *>> inputs)
    {
        return AddIntrinsicToBlock(graph, body_, id, intrinsic_id, inputs);
    }

    Inst *AddIntrinsicToBlock(Graph *graph, BasicBlock *block, int id, RuntimeInterface::IntrinsicId intrinsic_id,
                              std::initializer_list<std::pair<DataType::Type, Inst *>> inputs)
    {
        auto *inst = graph->CreateInstIntrinsic(DataType::ANY, compiler::INVALID_PC, intrinsic_id);
        inst->ClearFlag(compiler::inst_flags::REQUIRE_STATE);
        inst->ReserveInputs(inputs.size());
        inst->AllocateInputTypes(graph->GetAllocator(), inputs.size());
        for (const auto &[type, input] : inputs) {
            inst->AppendInput(input);
            inst->AddInputType(type);
        }
        inst->SetId(id);
        block->AppendInst(inst);
        return inst;
    }

    Inst *AddIf(Graph *graph, BasicBlock *block, int id, Inst *condition, compiler::ConditionCode cc)
    {
        auto *if_inst = graph->CreateInstIfImm();
        if_inst->SetId(id);
        if_inst->SetInput(0, condition);
        if_inst->SetOperandsType(DataType::BOOL);
        if_inst->SetCc(cc);
        if_inst->SetImm(0);
        block->AppendInst(if_inst);
        return if_inst;
    }

    Inst *AddReturn(Graph *graph, int id, Inst *input)
    {
        return AddReturnToBlock(graph, body_, id, input);
    }

    Inst *AddReturnToBlock(Graph *graph, BasicBlock *block, int id, Inst *input)
    {
        auto *ret = graph->CreateInstReturn(DataType::ANY, compiler::INVALID_PC);
        ret->SetId(id);
        ret->SetInput(0, input);
        block->AppendInst(ret);
        return ret;
    }

    static Inst *FindInstruction(Graph *graph, int id)
    {
        for (auto *block : graph->GetVectorBlocks()) {
            if (block == nullptr) {
                continue;
            }
            for (auto *inst : block->AllInsts()) {
                if (inst->GetId() == id) {
                    return inst;
                }
            }
        }
        return nullptr;
    }

    struct NullishChain {
        Graph *graph {nullptr};
        BasicBlock *first {nullptr};
        BasicBlock *middle {nullptr};
        BasicBlock *nullish {nullptr};
        BasicBlock *nonnull {nullptr};
        Inst *value {nullptr};
        Inst *first_compare {nullptr};
        Inst *second_compare {nullptr};
        Inst *first_branch {nullptr};
        Inst *second_branch {nullptr};
    };

    static bool EqualityUsesTrueEdge(RuntimeInterface::IntrinsicId id, compiler::ConditionCode cc)
    {
        if (id == RuntimeInterface::IntrinsicId::STRICTEQ_IMM8_V8) {
            return cc == compiler::CC_NE;
        }
        return cc == compiler::CC_EQ;
    }

    static void AddEqualityEdges(BasicBlock *block, BasicBlock *equal, BasicBlock *not_equal,
                                 RuntimeInterface::IntrinsicId id, compiler::ConditionCode cc)
    {
        if (EqualityUsesTrueEdge(id, cc)) {
            block->AddSucc(equal);
            block->AddSucc(not_equal);
        } else {
            block->AddSucc(not_equal);
            block->AddSucc(equal);
        }
    }

    NullishChain CreateNullishChain(RuntimeInterface::IntrinsicId compare_id, compiler::ConditionCode first_cc,
                                    compiler::ConditionCode second_cc, bool reverse_constants = false)
    {
        auto *graph = allocator_->New<Graph>(allocator_, local_allocator_, Arch::NONE, true, true);
        auto *start = graph->CreateStartBlock();
        auto *first = graph->CreateEmptyBlock();
        auto *middle = graph->CreateEmptyBlock();
        auto *nullish = graph->CreateEmptyBlock();
        auto *nonnull = graph->CreateEmptyBlock();
        auto *end = graph->CreateEndBlock();
        start->AddSucc(first);
        nullish->AddSucc(end);
        nonnull->AddSucc(end);

        auto *value = AddParameter(graph, 0, 0, DataType::ANY);
        AddReturnToBlock(graph, nullish, 7, value);
        AddReturnToBlock(graph, nonnull, 8, value);
        auto first_constant_id =
            reverse_constants ? RuntimeInterface::IntrinsicId::LDUNDEFINED : RuntimeInterface::IntrinsicId::LDNULL;
        auto second_constant_id =
            reverse_constants ? RuntimeInterface::IntrinsicId::LDNULL : RuntimeInterface::IntrinsicId::LDUNDEFINED;
        auto *first_constant = AddIntrinsicToBlock(graph, first, 1, first_constant_id, {});
        auto *first_compare =
            AddIntrinsicToBlock(graph, first, 2, compare_id, {{DataType::ANY, value}, {DataType::ANY, first_constant}});
        auto *first_branch = AddIf(graph, first, 3, first_compare, first_cc);
        AddEqualityEdges(first, nullish, middle, compare_id, first_cc);

        auto *second_constant = AddIntrinsicToBlock(graph, middle, 4, second_constant_id, {});
        auto *second_compare = AddIntrinsicToBlock(graph, middle, 5, compare_id,
                                                   {{DataType::ANY, value}, {DataType::ANY, second_constant}});
        auto *second_branch = AddIf(graph, middle, 6, second_compare, second_cc);
        AddEqualityEdges(middle, nullish, nonnull, compare_id, second_cc);

        return {graph, first,         middle,         nullish,      nonnull,
                value, first_compare, second_compare, first_branch, second_branch};
    }

    ArenaAllocator *allocator_ {nullptr};
    ArenaAllocator *local_allocator_ {nullptr};
    BasicBlock *body_ {nullptr};
};

TEST_F(PeepholeOptimizerTest, SimplifiesNestedBooleanConversions)
{
    auto *graph = CreateGraph();
    auto *value = AddParameter(graph, 0, 0, DataType::ANY);
    auto *inner = AddIntrinsic(graph, 1, RuntimeInterface::IntrinsicId::ISFALSE, {{DataType::ANY, value}});
    auto *outer = AddIntrinsic(graph, 2, RuntimeInterface::IntrinsicId::ISTRUE, {{DataType::ANY, inner}});
    auto *ret = AddReturn(graph, 3, outer);

    ASSERT_TRUE(graph->RunPass<PeepholeOptimizer>());

    EXPECT_EQ(ret->GetInput(0).GetInst(), inner);
    EXPECT_FALSE(outer->HasUsers());
}

TEST_F(PeepholeOptimizerTest, SimplifiesCallRuntimeBooleanConversions)
{
    auto *graph = CreateGraph();
    auto *value = AddParameter(graph, 0, 0, DataType::ANY);
    auto *inner =
        AddIntrinsic(graph, 1, RuntimeInterface::IntrinsicId::CALLRUNTIME_ISFALSE_PREF_IMM8, {{DataType::ANY, value}});
    auto *outer =
        AddIntrinsic(graph, 2, RuntimeInterface::IntrinsicId::CALLRUNTIME_ISTRUE_PREF_IMM8, {{DataType::ANY, inner}});
    auto *ret = AddReturn(graph, 3, outer);

    ASSERT_TRUE(graph->RunPass<PeepholeOptimizer>());

    EXPECT_EQ(ret->GetInput(0).GetInst(), inner);
    EXPECT_FALSE(outer->HasUsers());
}

TEST_F(PeepholeOptimizerTest, SimplifiesCallRuntimeDoubleFalse)
{
    auto *graph = CreateGraph();
    auto *value = AddParameter(graph, 0, 0, DataType::ANY);
    auto *inner =
        AddIntrinsic(graph, 1, RuntimeInterface::IntrinsicId::CALLRUNTIME_ISFALSE_PREF_IMM8, {{DataType::ANY, value}});
    auto *outer =
        AddIntrinsic(graph, 2, RuntimeInterface::IntrinsicId::CALLRUNTIME_ISFALSE_PREF_IMM8, {{DataType::ANY, inner}});
    AddReturn(graph, 3, outer);

    ASSERT_TRUE(graph->RunPass<PeepholeOptimizer>());

    EXPECT_EQ(outer->CastToIntrinsic()->GetIntrinsicId(), RuntimeInterface::IntrinsicId::CALLRUNTIME_ISTRUE_PREF_IMM8);
    EXPECT_EQ(outer->GetInput(0).GetInst(), value);
}

TEST_F(PeepholeOptimizerTest, SimplifiesStrictCompareUnderIsFalse)
{
    auto *graph = CreateGraph();
    auto *left = AddParameter(graph, 0, 0, DataType::ANY);
    auto *right = AddParameter(graph, 1, 1, DataType::ANY);
    auto *compare = AddIntrinsic(graph, 2, RuntimeInterface::IntrinsicId::STRICTEQ_IMM8_V8,
                                 {{DataType::ANY, left}, {DataType::ANY, right}});
    auto *outer = AddIntrinsic(graph, 3, RuntimeInterface::IntrinsicId::ISFALSE, {{DataType::ANY, compare}});
    auto *ret = AddReturn(graph, 4, outer);

    ASSERT_TRUE(graph->RunPass<PeepholeOptimizer>());

    EXPECT_EQ(compare->CastToIntrinsic()->GetIntrinsicId(), RuntimeInterface::IntrinsicId::STRICTNOTEQ_IMM8_V8);
    EXPECT_EQ(ret->GetInput(0).GetInst(), compare);
    EXPECT_FALSE(compare->HasSingleUser());
    EXPECT_FALSE(outer->HasUsers());
}

TEST_F(PeepholeOptimizerTest, DoesNotRewriteCompareWithMultipleUsers)
{
    auto *graph = CreateGraph();
    auto *left = AddParameter(graph, 0, 0, DataType::ANY);
    auto *right = AddParameter(graph, 1, 1, DataType::ANY);
    auto *compare = AddIntrinsic(graph, 2, RuntimeInterface::IntrinsicId::STRICTEQ_IMM8_V8,
                                 {{DataType::ANY, left}, {DataType::ANY, right}});
    AddIntrinsic(graph, 3, RuntimeInterface::IntrinsicId::ISFALSE, {{DataType::ANY, compare}});
    auto *ret = AddReturn(graph, 4, compare);

    ASSERT_FALSE(graph->RunPass<PeepholeOptimizer>());

    EXPECT_EQ(compare->CastToIntrinsic()->GetIntrinsicId(), RuntimeInterface::IntrinsicId::STRICTEQ_IMM8_V8);
    EXPECT_EQ(ret->GetInput(0).GetInst(), compare);
}

TEST_F(PeepholeOptimizerTest, SimplifiesRepeatedToNumeric)
{
    auto *graph = CreateGraph();
    auto *value = AddParameter(graph, 0, 0, DataType::ANY);
    auto *first = AddIntrinsic(graph, 1, RuntimeInterface::IntrinsicId::TONUMERIC_IMM8, {{DataType::ANY, value}});
    auto *second = AddIntrinsic(graph, 2, RuntimeInterface::IntrinsicId::TONUMERIC_IMM8, {{DataType::ANY, first}});
    auto *ret = AddReturn(graph, 3, second);

    ASSERT_TRUE(graph->RunPass<PeepholeOptimizer>());

    EXPECT_EQ(ret->GetInput(0).GetInst(), first);
}

TEST_F(PeepholeOptimizerTest, RemovesToNumericForStrictNumericType)
{
    auto *graph = CreateGraph();
    auto *value = AddParameter(graph, 0, 0, DataType::INT32);
    auto *conversion =
        AddIntrinsic(graph, 1, RuntimeInterface::IntrinsicId::TONUMERIC_IMM8, {{DataType::INT32, value}});
    auto *ret = AddReturn(graph, 2, conversion);

    ASSERT_TRUE(graph->RunPass<PeepholeOptimizer>());

    EXPECT_EQ(ret->GetInput(0).GetInst(), value);
}

TEST_F(PeepholeOptimizerTest, RemovesToNumericForFloatType)
{
    auto *graph = CreateGraph();
    auto *value = AddParameter(graph, 0, 0, DataType::FLOAT64);
    auto *conversion =
        AddIntrinsic(graph, 1, RuntimeInterface::IntrinsicId::TONUMERIC_IMM8, {{DataType::FLOAT64, value}});
    auto *ret = AddReturn(graph, 2, conversion);

    ASSERT_TRUE(graph->RunPass<PeepholeOptimizer>());

    EXPECT_EQ(ret->GetInput(0).GetInst(), value);
}

TEST_F(PeepholeOptimizerTest, KeepsToNumericForBoolType)
{
    auto *graph = CreateGraph();
    auto *value = AddParameter(graph, 0, 0, DataType::BOOL);
    auto *conversion = AddIntrinsic(graph, 1, RuntimeInterface::IntrinsicId::TONUMERIC_IMM8, {{DataType::BOOL, value}});
    auto *ret = AddReturn(graph, 2, conversion);

    ASSERT_FALSE(graph->RunPass<PeepholeOptimizer>());

    EXPECT_EQ(ret->GetInput(0).GetInst(), conversion);
}

TEST_F(PeepholeOptimizerTest, KeepsToNumericForDynamicType)
{
    auto *graph = CreateGraph();
    auto *value = AddParameter(graph, 0, 0, DataType::ANY);
    auto *conversion = AddIntrinsic(graph, 1, RuntimeInterface::IntrinsicId::TONUMERIC_IMM8, {{DataType::ANY, value}});
    auto *ret = AddReturn(graph, 2, conversion);

    ASSERT_FALSE(graph->RunPass<PeepholeOptimizer>());

    EXPECT_EQ(ret->GetInput(0).GetInst(), conversion);
}

TEST_F(PeepholeOptimizerTest, DoesNotFoldToNumericOverToNumber)
{
    auto *graph = CreateGraph();
    auto *value = AddParameter(graph, 0, 0, DataType::ANY);
    auto *to_number = AddIntrinsic(graph, 1, RuntimeInterface::IntrinsicId::TONUMBER_IMM8, {{DataType::ANY, value}});
    auto *conversion =
        AddIntrinsic(graph, 2, RuntimeInterface::IntrinsicId::TONUMERIC_IMM8, {{DataType::ANY, to_number}});
    auto *ret = AddReturn(graph, 3, conversion);

    ASSERT_FALSE(graph->RunPass<PeepholeOptimizer>());

    EXPECT_EQ(ret->GetInput(0).GetInst(), conversion);
}

TEST_F(PeepholeOptimizerTest, FoldsStrictNotEqNullishChain)
{
    auto chain =
        CreateNullishChain(RuntimeInterface::IntrinsicId::STRICTNOTEQ_IMM8_V8, compiler::CC_EQ, compiler::CC_NE);

    ASSERT_TRUE(chain.graph->RunPass<PeepholeOptimizer>());

    EXPECT_EQ(chain.first_compare->CastToIntrinsic()->GetIntrinsicId(), RuntimeInterface::IntrinsicId::NOTEQ_IMM8_V8);
    EXPECT_EQ(chain.first->GetTrueSuccessor(), chain.nullish);
    EXPECT_EQ(chain.first->GetFalseSuccessor(), chain.nonnull);
    EXPECT_TRUE(chain.middle->GetPredsBlocks().empty());
}

TEST_F(PeepholeOptimizerTest, FoldsStrictEqNullishChain)
{
    auto chain = CreateNullishChain(RuntimeInterface::IntrinsicId::STRICTEQ_IMM8_V8, compiler::CC_NE, compiler::CC_EQ);

    ASSERT_TRUE(chain.graph->RunPass<PeepholeOptimizer>());

    EXPECT_EQ(chain.first_compare->CastToIntrinsic()->GetIntrinsicId(), RuntimeInterface::IntrinsicId::EQ_IMM8_V8);
    EXPECT_EQ(chain.first->GetTrueSuccessor(), chain.nullish);
    EXPECT_EQ(chain.first->GetFalseSuccessor(), chain.nonnull);
}

TEST_F(PeepholeOptimizerTest, FoldsReversedNullishConstants)
{
    auto chain =
        CreateNullishChain(RuntimeInterface::IntrinsicId::STRICTNOTEQ_IMM8_V8, compiler::CC_EQ, compiler::CC_NE, true);

    ASSERT_TRUE(chain.graph->RunPass<PeepholeOptimizer>());

    EXPECT_EQ(chain.first_compare->CastToIntrinsic()->GetIntrinsicId(), RuntimeInterface::IntrinsicId::NOTEQ_IMM8_V8);
    EXPECT_EQ(chain.first->GetFalseSuccessor(), chain.nonnull);
}

TEST_F(PeepholeOptimizerTest, KeepsNullishChainForDifferentValues)
{
    auto chain =
        CreateNullishChain(RuntimeInterface::IntrinsicId::STRICTNOTEQ_IMM8_V8, compiler::CC_EQ, compiler::CC_NE);
    auto *other = AddParameter(chain.graph, 9, 1, DataType::ANY);
    chain.second_compare->SetInput(0, other);

    ASSERT_FALSE(chain.graph->RunPass<PeepholeOptimizer>());

    EXPECT_EQ(chain.first_compare->CastToIntrinsic()->GetIntrinsicId(),
              RuntimeInterface::IntrinsicId::STRICTNOTEQ_IMM8_V8);
    EXPECT_TRUE(chain.first->HasSucc(chain.middle));
}

TEST_F(PeepholeOptimizerTest, KeepsNullishChainWithExtraMiddleInstruction)
{
    auto chain =
        CreateNullishChain(RuntimeInterface::IntrinsicId::STRICTNOTEQ_IMM8_V8, compiler::CC_EQ, compiler::CC_NE);
    auto *extra =
        chain.graph->CreateInstIntrinsic(DataType::ANY, compiler::INVALID_PC, RuntimeInterface::IntrinsicId::ISTRUE);
    extra->ClearFlag(compiler::inst_flags::REQUIRE_STATE);
    extra->ReserveInputs(1);
    extra->AllocateInputTypes(chain.graph->GetAllocator(), 1);
    extra->AppendInput(chain.value);
    extra->AddInputType(DataType::ANY);
    extra->SetId(9);
    chain.middle->PrependInst(extra);

    ASSERT_FALSE(chain.graph->RunPass<PeepholeOptimizer>());

    EXPECT_TRUE(chain.first->HasSucc(chain.middle));
}

TEST_F(PeepholeOptimizerTest, KeepsNullishChainWithMultipleMiddlePredecessors)
{
    auto chain =
        CreateNullishChain(RuntimeInterface::IntrinsicId::STRICTNOTEQ_IMM8_V8, compiler::CC_EQ, compiler::CC_NE);
    auto *start = chain.graph->GetStartBlock();
    auto *extra_pred = chain.graph->CreateEmptyBlock();
    start->RemoveSucc(chain.first);
    chain.first->RemovePred(start);
    start->AddSucc(extra_pred);
    auto *condition = AddIntrinsicToBlock(chain.graph, extra_pred, 9, RuntimeInterface::IntrinsicId::ISTRUE,
                                          {{DataType::ANY, chain.value}});
    AddIf(chain.graph, extra_pred, 10, condition, compiler::CC_NE);
    extra_pred->AddSucc(chain.first);
    extra_pred->AddSucc(chain.middle);

    ASSERT_FALSE(chain.graph->RunPass<PeepholeOptimizer>());

    EXPECT_EQ(chain.middle->GetPredsBlocks().size(), 2U);
    EXPECT_TRUE(chain.first->HasSucc(chain.middle));
}

TEST_F(PeepholeOptimizerTest, KeepsNullishChainWithDifferentNullishTargets)
{
    auto chain =
        CreateNullishChain(RuntimeInterface::IntrinsicId::STRICTNOTEQ_IMM8_V8, compiler::CC_EQ, compiler::CC_NE);
    auto *other_nullish = chain.graph->CreateEmptyBlock();
    other_nullish->AddSucc(chain.graph->GetEndBlock());
    AddReturnToBlock(chain.graph, other_nullish, 9, chain.value);
    chain.nullish->RemovePred(chain.middle);
    chain.middle->ReplaceSucc(chain.nullish, other_nullish);

    ASSERT_FALSE(chain.graph->RunPass<PeepholeOptimizer>());

    EXPECT_TRUE(chain.first->HasSucc(chain.middle));
}

TEST_F(PeepholeOptimizerTest, KeepsNullishChainAcrossTryRegions)
{
    auto chain =
        CreateNullishChain(RuntimeInterface::IntrinsicId::STRICTNOTEQ_IMM8_V8, compiler::CC_EQ, compiler::CC_NE);
    chain.first->SetTry(true);
    chain.first->SetTryId(1);
    chain.middle->SetTry(true);
    chain.middle->SetTryId(2);

    ASSERT_FALSE(chain.graph->RunPass<PeepholeOptimizer>());

    EXPECT_TRUE(chain.first->HasSucc(chain.middle));
}

TEST_F(PeepholeOptimizerTest, KeepsNullishChainWhenTargetHasPhi)
{
    auto chain =
        CreateNullishChain(RuntimeInterface::IntrinsicId::STRICTNOTEQ_IMM8_V8, compiler::CC_EQ, compiler::CC_NE);
    auto *phi = chain.graph->CreateInstPhi(DataType::ANY, compiler::INVALID_PC);
    phi->ReserveInputs(1);
    phi->AppendInput(chain.value);
    phi->SetId(9);
    chain.nonnull->AppendPhi(phi);

    ASSERT_FALSE(chain.graph->RunPass<PeepholeOptimizer>());

    EXPECT_TRUE(chain.first->HasSucc(chain.middle));
}

}  // namespace panda::bytecodeopt::test
