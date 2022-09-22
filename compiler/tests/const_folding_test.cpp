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

#include "unit_test.h"
#include "optimizer/optimizations/const_folding.h"
#include "optimizer/optimizations/cleanup.h"
#include "optimizer/code_generator/codegen.h"

#include <optional>

namespace panda::compiler {
class ConstFoldingTest : public CommonTest {
public:
    ConstFoldingTest() : graph_(CreateGraphStartEndBlocks()) {}
    ~ConstFoldingTest() override {}

    Graph *GetGraph()
    {
        return graph_;
    }

    template <class T>
    void CmpTest(T l, T r, int32_t result, DataType::Type src_type, bool fcmpg = false)
    {
        auto graph = CreateEmptyGraph();
        GRAPH(graph)
        {
            CONSTANT(0, l);
            CONSTANT(1, r);
            BASIC_BLOCK(2, 1)
            {
                INST(2, Opcode::Cmp).s32().SrcType(src_type).Inputs(0, 1);
                INST(3, Opcode::Return).s32().Inputs(2);
            }
        }
        if (DataType::IsFloatType(src_type)) {
            INS(2).CastToCmp()->SetFcmpg(fcmpg);
            ASSERT_EQ(INS(2).CastToCmp()->IsFcmpg(), fcmpg);
        }
        ASSERT_EQ(ConstFoldingCmp(&INS(2)), true);
        GraphChecker(graph).Check();

        ConstantInst *inst = graph->FindConstant(DataType::INT64, result);
        ASSERT(inst != nullptr);
        ASSERT_EQ(INS(3).GetInput(0).GetInst(), inst);
    }

    template <class From, class To>
    void CastTest(From Src, To Dst, DataType::Type DstType)
    {
        auto graph = CreateEmptyGraph();
        GRAPH(graph)
        {
            CONSTANT(0, Src);
            BASIC_BLOCK(2, 1)
            {
                INST(1, Opcode::Cast).SrcType(INS(0).GetType()).Inputs(0);
                INS(1).SetType(DstType);
                INST(2, Opcode::Return).Inputs(1);
                INS(2).SetType(DstType);
            }
        }
        ASSERT_EQ(ConstFoldingCast(&INS(1)), true);
        GraphChecker(graph).Check();

        ConstantInst *inst = nullptr;
        if (DataType::GetCommonType(DstType) == DataType::INT64) {
            inst = graph->FindConstant(DataType::INT64, Dst);
        } else if (DstType == DataType::FLOAT32) {
            inst = graph->FindConstant(DataType::FLOAT32, bit_cast<uint32_t, float>(Dst));

        } else if (DstType == DataType::FLOAT64) {
            inst = graph->FindConstant(DataType::FLOAT64, bit_cast<uint64_t, double>(Dst));
        }
        ASSERT(inst != nullptr);
        ASSERT_EQ(INS(2).GetInput(0).GetInst(), inst);
    }

    void CheckCompareEqualInputs(DataType::Type param_type, ConditionCode cc, std::optional<uint64_t> result)
    {
        auto graph = CreateEmptyGraph();
        GRAPH(graph)
        {
            PARAMETER(0, 0);
            INS(0).SetType(param_type);
            BASIC_BLOCK(2, 1)
            {
                INST(1, Opcode::Compare).b().CC(cc).Inputs(0, 0);
                INST(2, Opcode::Return).b().Inputs(1);
            }
        }
        ASSERT_EQ(ConstFoldingCompare(&INS(1)), result.has_value());
        if (result.has_value()) {
            auto inst = graph->FindConstant(DataType::INT64, *result);
            ASSERT(inst != nullptr);
            ASSERT_EQ(INS(2).GetInput(0).GetInst(), inst);
        }
        GraphChecker(graph).Check();
    }

private:
    Graph *graph_;
};

TEST_F(ConstFoldingTest, NegInt64Test)
{
    GRAPH(GetGraph())
    {
        CONSTANT(0, UINT64_MAX);
        BASIC_BLOCK(2, 1)
        {
            INST(1, Opcode::Neg).s64().Inputs(0);
            INST(2, Opcode::Return).s64().Inputs(1);
        }
    }
    int result = 1;
    ASSERT_EQ(ConstFoldingNeg(&INS(1)), true);
    auto inst = GetGraph()->FindConstant(DataType::INT64, result);
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(2).GetInput(0).GetInst(), inst);
    GraphChecker(GetGraph()).Check();
}

TEST_F(ConstFoldingTest, NegInt32Test)
{
    GRAPH(GetGraph())
    {
        CONSTANT(0, 1);
        BASIC_BLOCK(2, 1)
        {
            INST(1, Opcode::Neg).s32().Inputs(0);
            INST(2, Opcode::Return).s32().Inputs(1);
        }
    }
    int32_t result = -1;
    ASSERT_EQ(ConstFoldingNeg(&INS(1)), true);
    auto inst = GetGraph()->FindConstant(DataType::INT64, result);
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(2).GetInput(0).GetInst(), inst);
    GraphChecker(GetGraph()).Check();
}

TEST_F(ConstFoldingTest, Constant32NegIntTest)
{
    auto graph = CreateEmptyBytecodeGraph();
    GRAPH(graph)
    {
        CONSTANT(0, 1);
        BASIC_BLOCK(2, 1)
        {
            INST(1, Opcode::Neg).s32().Inputs(0);
            INST(2, Opcode::Return).s32().Inputs(1);
        }
    }
    int32_t result = -1;
    ASSERT_EQ(ConstFoldingNeg(&INS(1)), true);
    auto inst = graph->FindConstant(DataType::INT32, result);
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(2).GetInput(0).GetInst(), inst);
    GraphChecker(graph).Check();
}

TEST_F(ConstFoldingTest, NegFloatTest)
{
    GRAPH(GetGraph())
    {
        CONSTANT(0, static_cast<float>12);
        BASIC_BLOCK(2, 1)
        {
            INST(1, Opcode::Neg).f32().Inputs(0);
            INST(2, Opcode::Return).f32().Inputs(1);
        }
    }
    float result = -12.0;
    ASSERT_EQ(ConstFoldingNeg(&INS(1)), true);
    auto inst = GetGraph()->FindConstant(DataType::FLOAT32, bit_cast<uint32_t, float>(result));
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(2).GetInput(0).GetInst(), inst);
    GraphChecker(GetGraph()).Check();
}

TEST_F(ConstFoldingTest, NegDoubleTest)
{
    GRAPH(GetGraph())
    {
        CONSTANT(0, 12.0);
        BASIC_BLOCK(2, 1)
        {
            INST(1, Opcode::Neg).f64().Inputs(0);
            INST(2, Opcode::Return).f64().Inputs(1);
        }
    }
    double result = -12.0;
    ASSERT_EQ(ConstFoldingNeg(&INS(1)), true);
    auto inst = GetGraph()->FindConstant(DataType::FLOAT64, bit_cast<uint64_t, double>(result));
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(2).GetInput(0).GetInst(), inst);
    GraphChecker(GetGraph()).Check();
}

TEST_F(ConstFoldingTest, AbsIntTest)
{
    GRAPH(GetGraph())
    {
        CONSTANT(0, -1);
        BASIC_BLOCK(2, 1)
        {
            INST(1, Opcode::Abs).s64().Inputs(0);
            INST(2, Opcode::Return).s64().Inputs(1);
        }
    }
    int64_t result = 1;
    ASSERT_EQ(ConstFoldingAbs(&INS(1)), true);
    auto inst = GetGraph()->FindConstant(DataType::INT64, result);
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(2).GetInput(0).GetInst(), inst);
    GraphChecker(GetGraph()).Check();
}

TEST_F(ConstFoldingTest, Constant32AbsIntTest)
{
    auto graph = CreateEmptyBytecodeGraph();
    GRAPH(graph)
    {
        CONSTANT(0, -1);
        BASIC_BLOCK(2, 1)
        {
            INST(1, Opcode::Abs).s32().Inputs(0);
            INST(2, Opcode::Return).s32().Inputs(1);
        }
    }
    int64_t result = 1;
    ASSERT_EQ(ConstFoldingAbs(&INS(1)), true);
    auto inst = graph->FindConstant(DataType::INT32, result);
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(2).GetInput(0).GetInst(), inst);
    GraphChecker(graph).Check();
}

TEST_F(ConstFoldingTest, AbsFloatTest)
{
    GRAPH(GetGraph())
    {
        CONSTANT(0, static_cast<float>(-12.0));
        BASIC_BLOCK(2, 1)
        {
            INST(1, Opcode::Abs).f32().Inputs(0);
            INST(2, Opcode::Return).f32().Inputs(1);
        }
    }
    float result = 12.0;
    ASSERT_EQ(ConstFoldingAbs(&INS(1)), true);
    auto inst = GetGraph()->FindConstant(DataType::FLOAT32, bit_cast<uint32_t, float>(result));
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(2).GetInput(0).GetInst(), inst);
    GraphChecker(GetGraph()).Check();
}

TEST_F(ConstFoldingTest, AbsDoubleTest)
{
    GRAPH(GetGraph())
    {
        CONSTANT(0, -12.0);
        BASIC_BLOCK(2, 1)
        {
            INST(1, Opcode::Abs).f64().Inputs(0);
            INST(2, Opcode::Return).f64().Inputs(1);
        }
    }
    double result = 12.0;
    ASSERT_EQ(ConstFoldingAbs(&INS(1)), true);
    auto inst = GetGraph()->FindConstant(DataType::FLOAT64, bit_cast<uint64_t, double>(result));
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(2).GetInput(0).GetInst(), inst);
    GraphChecker(GetGraph()).Check();
}

TEST_F(ConstFoldingTest, NotIntTest)
{
    GRAPH(GetGraph())
    {
        CONSTANT(0, -12);
        BASIC_BLOCK(2, 1)
        {
            INST(1, Opcode::Not).s64().Inputs(0);
            INST(2, Opcode::Return).s64().Inputs(1);
        }
    }
    int result = 11;
    ASSERT_EQ(ConstFoldingNot(&INS(1)), true);
    auto inst = GetGraph()->FindConstant(DataType::INT64, result);
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(2).GetInput(0).GetInst(), inst);
    GraphChecker(GetGraph()).Check();
}

TEST_F(ConstFoldingTest, Constant32NotIntTest)
{
    auto graph = CreateEmptyBytecodeGraph();
    GRAPH(graph)
    {
        CONSTANT(0, -12);
        BASIC_BLOCK(2, 1)
        {
            INST(1, Opcode::Not).s32().Inputs(0);
            INST(2, Opcode::Return).s32().Inputs(1);
        }
    }
    int result = 11;
    ASSERT_EQ(ConstFoldingNot(&INS(1)), true);
    auto inst = graph->FindConstant(DataType::INT32, result);
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(2).GetInput(0).GetInst(), inst);
    GraphChecker(graph).Check();
}

TEST_F(ConstFoldingTest, AddIntTest)
{
    GRAPH(GetGraph())
    {
        CONSTANT(0, 3);
        CONSTANT(1, -2);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Add).s64().Inputs(0, 1);
            INST(3, Opcode::Return).s64().Inputs(2);
        }
    }
    int result = 1;
    ASSERT_EQ(ConstFoldingAdd(&INS(2)), true);
    auto inst = GetGraph()->FindConstant(DataType::INT64, result);
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), inst);
    GraphChecker(GetGraph()).Check();
}

TEST_F(ConstFoldingTest, Constant32AddIntTest)
{
    auto graph = CreateEmptyBytecodeGraph();
    GRAPH(graph)
    {
        CONSTANT(0, 3);
        CONSTANT(1, -2);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Add).s32().Inputs(0, 1);
            INST(3, Opcode::Return).s32().Inputs(2);
        }
    }
    int result = 1;
    ASSERT_EQ(ConstFoldingAdd(&INS(2)), true);
    auto inst = graph->FindConstant(DataType::INT32, result);
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), inst);
    GraphChecker(graph).Check();
}

TEST_F(ConstFoldingTest, AddInt8Test)
{
    GRAPH(GetGraph())
    {
        CONSTANT(0, static_cast<uint8_t>0xffffffff);
        CONSTANT(1, static_cast<uint8_t>1);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Add).u8().Inputs(0, 1);
            INST(3, Opcode::Return).u8().Inputs(2);
        }
    }
    int result = 0;
    ASSERT_EQ(ConstFoldingAdd(&INS(2)), true);
    auto inst = GetGraph()->FindConstant(DataType::INT64, result);
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), inst);
    GraphChecker(GetGraph()).Check();
}

TEST_F(ConstFoldingTest, Constant32AddInt8Test)
{
    auto graph = CreateEmptyBytecodeGraph();
    GRAPH(graph)
    {
        CONSTANT(0, (uint8_t)0xffffffff);
        CONSTANT(1, (uint8_t)1);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Add).u8().Inputs(0, 1);
            INST(3, Opcode::Return).u8().Inputs(2);
        }
    }
    int result = 0;
    ASSERT_EQ(ConstFoldingAdd(&INS(2)), true);
    auto inst = graph->FindConstant(DataType::INT32, result);
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), inst);
    GraphChecker(graph).Check();
}

TEST_F(ConstFoldingTest, AddFloatTest)
{
    GRAPH(GetGraph())
    {
        CONSTANT(0, static_cast<float>3.0);
        CONSTANT(1, static_cast<float>(-2.0));
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Add).f32().Inputs(0, 1);
            INST(3, Opcode::Return).f32().Inputs(2);
        }
    }
    float result = 1.0;
    ASSERT_EQ(ConstFoldingAdd(&INS(2)), true);
    auto inst = GetGraph()->FindConstant(DataType::FLOAT32, bit_cast<uint32_t, float>(result));
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), inst);
    GraphChecker(GetGraph()).Check();
}

TEST_F(ConstFoldingTest, AddDoubleTest)
{
    GRAPH(GetGraph())
    {
        CONSTANT(0, 3.0);
        CONSTANT(1, -2.0);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Add).f64().Inputs(0, 1);
            INST(3, Opcode::Return).f64().Inputs(2);
        }
    }
    double result = 1.0;
    ASSERT_EQ(ConstFoldingAdd(&INS(2)), true);
    auto inst = GetGraph()->FindConstant(DataType::FLOAT64, bit_cast<uint64_t, double>(result));
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), inst);
    GraphChecker(GetGraph()).Check();
}

TEST_F(ConstFoldingTest, SubIntTest)
{
    GRAPH(GetGraph())
    {
        CONSTANT(0, 0);
        CONSTANT(1, 1);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Sub).s8().Inputs(0, 1);
            INST(3, Opcode::Return).s8().Inputs(2);
        }
    }
    int result = 0xffffffff;
    ASSERT_EQ(ConstFoldingSub(&INS(2)), true);
    auto inst = GetGraph()->FindConstant(DataType::INT64, result);
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), inst);
    GraphChecker(GetGraph()).Check();
}

TEST_F(ConstFoldingTest, Constant32SubIntTest)
{
    auto graph = CreateEmptyBytecodeGraph();
    GRAPH(graph)
    {
        CONSTANT(0, 0);
        CONSTANT(1, 1);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Sub).s8().Inputs(0, 1);
            INST(3, Opcode::Return).s8().Inputs(2);
        }
    }
    int result = 0xffffffff;
    ASSERT_EQ(ConstFoldingSub(&INS(2)), true);
    auto inst = graph->FindConstant(DataType::INT32, result);
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), inst);
    GraphChecker(graph).Check();
}

TEST_F(ConstFoldingTest, SubUIntTest)
{
    GRAPH(GetGraph())
    {
        CONSTANT(0, 0);
        CONSTANT(1, 1);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Sub).u8().Inputs(0, 1);
            INST(3, Opcode::Return).u8().Inputs(2);
        }
    }
    int result = 0xff;
    ASSERT_EQ(ConstFoldingSub(&INS(2)), true);
    auto inst = GetGraph()->FindConstant(DataType::INT64, result);
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), inst);
    GraphChecker(GetGraph()).Check();
}

TEST_F(ConstFoldingTest, Constant32SubUIntTest)
{
    auto graph = CreateEmptyBytecodeGraph();
    GRAPH(graph)
    {
        CONSTANT(0, 0);
        CONSTANT(1, 1);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Sub).u8().Inputs(0, 1);
            INST(3, Opcode::Return).u8().Inputs(2);
        }
    }
    int result = 0xff;
    ASSERT_EQ(ConstFoldingSub(&INS(2)), true);
    auto inst = graph->FindConstant(DataType::INT32, result);
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), inst);
    GraphChecker(graph).Check();
}

TEST_F(ConstFoldingTest, SubFloatTest)
{
    GRAPH(GetGraph())
    {
        CONSTANT(0, static_cast<float>3.0);
        CONSTANT(1, static_cast<float>2.0);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Sub).f32().Inputs(0, 1);
            INST(3, Opcode::Return).f32().Inputs(2);
        }
    }
    float result = 1.0;
    ASSERT_EQ(ConstFoldingSub(&INS(2)), true);
    auto inst = GetGraph()->FindConstant(DataType::FLOAT32, bit_cast<uint32_t, float>(result));
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), inst);
    GraphChecker(GetGraph()).Check();
}

TEST_F(ConstFoldingTest, SubDoubleTest)
{
    GRAPH(GetGraph())
    {
        CONSTANT(0, 3.0);
        CONSTANT(1, 2.0);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Sub).f64().Inputs(0, 1);
            INST(3, Opcode::Return).f64().Inputs(2);
        }
    }
    double result = 1.0;
    ASSERT_EQ(ConstFoldingSub(&INS(2)), true);
    auto inst = GetGraph()->FindConstant(DataType::FLOAT64, bit_cast<uint64_t, double>(result));
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), inst);
    GraphChecker(GetGraph()).Check();
}

TEST_F(ConstFoldingTest, SubTestIntXsubX)
{
    GRAPH(GetGraph())
    {
        PARAMETER(0, 0).u64();
        BASIC_BLOCK(2, 1)
        {
            INST(1, Opcode::Sub).u64().Inputs(0, 0);
            INST(2, Opcode::Return).u64().Inputs(1);
        }
    }
    ASSERT_EQ(ConstFoldingSub(&INS(1)), true);
    int result = 0;
    auto inst = GetGraph()->FindConstant(DataType::INT64, result);
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(2).GetInput(0).GetInst(), inst);
    GraphChecker(GetGraph()).Check();
}

TEST_F(ConstFoldingTest, Constant32SubTestIntXsubX)
{
    auto graph = CreateEmptyBytecodeGraph();
    GRAPH(graph)
    {
        PARAMETER(0, 0).u32();
        BASIC_BLOCK(2, 1)
        {
            INST(1, Opcode::Sub).u32().Inputs(0, 0);
            INST(2, Opcode::Return).u32().Inputs(1);
        }
    }
    ASSERT_EQ(ConstFoldingSub(&INS(1)), true);
    int result = 0;
    auto inst = graph->FindConstant(DataType::INT32, result);
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(2).GetInput(0).GetInst(), inst);
    GraphChecker(graph).Check();
}

TEST_F(ConstFoldingTest, SubTestDoubleXsubX)
{
    GRAPH(GetGraph())
    {
        PARAMETER(0, 0).f64();
        BASIC_BLOCK(2, 1)
        {
            INST(1, Opcode::Sub).f64().Inputs(0, 0);
            INST(2, Opcode::Return).f64().Inputs(1);
        }
    }
    // the optimization "x-x -> 0" is not applicable for floating point values
    ASSERT_EQ(ConstFoldingSub(&INS(1)), false);
}

TEST_F(ConstFoldingTest, MulIntTest)
{
    GRAPH(GetGraph())
    {
        CONSTANT(0, 3);
        CONSTANT(1, 2);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Mul).u64().Inputs(0, 1);
            INST(3, Opcode::Return).u64().Inputs(2);
        }
    }
    int result = 6;
    ASSERT_EQ(ConstFoldingMul(&INS(2)), true);
    auto inst = GetGraph()->FindConstant(DataType::INT64, result);
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), inst);
    GraphChecker(GetGraph()).Check();
}

TEST_F(ConstFoldingTest, Constant32MulIntTest)
{
    auto graph = CreateEmptyBytecodeGraph();
    GRAPH(graph)
    {
        CONSTANT(0, 3);
        CONSTANT(1, 2);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Mul).u32().Inputs(0, 1);
            INST(3, Opcode::Return).u32().Inputs(2);
        }
    }
    int result = 6;
    ASSERT_EQ(ConstFoldingMul(&INS(2)), true);
    auto inst = graph->FindConstant(DataType::INT32, result);
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), inst);
    GraphChecker(graph).Check();
}

TEST_F(ConstFoldingTest, MulFloatTest)
{
    GRAPH(GetGraph())
    {
        CONSTANT(0, static_cast<float>3.0);
        CONSTANT(1, static_cast<float>2.0);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Mul).f32().Inputs(0, 1);
            INST(3, Opcode::Return).f32().Inputs(2);
        }
    }
    float result = 6.0;
    ASSERT_EQ(ConstFoldingMul(&INS(2)), true);
    auto inst = GetGraph()->FindConstant(DataType::FLOAT32, bit_cast<uint32_t, float>(result));
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), inst);
    GraphChecker(GetGraph()).Check();
}

TEST_F(ConstFoldingTest, MulDoubleTest)
{
    GRAPH(GetGraph())
    {
        CONSTANT(0, 3.0);
        CONSTANT(1, 2.0);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Mul).f64().Inputs(0, 1);
            INST(3, Opcode::Return).f64().Inputs(2);
        }
    }
    double result = 6.0;
    ASSERT_EQ(ConstFoldingMul(&INS(2)), true);
    auto inst = GetGraph()->FindConstant(DataType::FLOAT64, bit_cast<uint64_t, double>(result));
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), inst);
    GraphChecker(GetGraph()).Check();
}

TEST_F(ConstFoldingTest, DivIntTest)
{
    GRAPH(GetGraph())
    {
        CONSTANT(0, 3);
        CONSTANT(1, 2);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Div).u64().Inputs(0, 1);
            INST(3, Opcode::Return).u64().Inputs(2);
        }
    }
    int result = 1;
    ASSERT_EQ(ConstFoldingDiv(&INS(2)), true);
    auto inst = GetGraph()->FindConstant(DataType::INT64, result);
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), inst);
    GraphChecker(GetGraph()).Check();
}

TEST_F(ConstFoldingTest, Constant32DivIntTest)
{
    auto graph = CreateEmptyBytecodeGraph();
    GRAPH(graph)
    {
        CONSTANT(0, 3);
        CONSTANT(1, 2);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Div).u32().Inputs(0, 1);
            INST(3, Opcode::Return).u32().Inputs(2);
        }
    }
    int result = 1;
    ASSERT_EQ(ConstFoldingDiv(&INS(2)), true);
    auto inst = graph->FindConstant(DataType::INT32, result);
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), inst);
    GraphChecker(graph).Check();
}

TEST_F(ConstFoldingTest, DivIntTest1)
{
    GRAPH(GetGraph())
    {
        CONSTANT(0, 0xffffffff80000000);
        CONSTANT(1, 0xffffffffffffffff);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Div).s32().Inputs(0, 1);
            INST(3, Opcode::Return).s32().Inputs(2);
        }
    }
    int32_t result = 0x80000000;
    ASSERT_EQ(ConstFoldingDiv(&INS(2)), true);
    auto inst = GetGraph()->FindConstant(DataType::INT64, result);
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), inst);
    GraphChecker(GetGraph()).Check();
}

TEST_F(ConstFoldingTest, Constant32DivIntTest1)
{
    auto graph = CreateEmptyBytecodeGraph();
    GRAPH(graph)
    {
        CONSTANT(0, 0xffffffff80000000);
        CONSTANT(1, 0xffffffffffffffff);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Div).s32().Inputs(0, 1);
            INST(3, Opcode::Return).s32().Inputs(2);
        }
    }
    int32_t result = 0x80000000;
    ASSERT_EQ(ConstFoldingDiv(&INS(2)), true);
    auto inst = graph->FindConstant(DataType::INT32, result);
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), inst);
    GraphChecker(graph).Check();
}

TEST_F(ConstFoldingTest, DivIntTest2)
{
    GRAPH(GetGraph())
    {
        CONSTANT(0, 0x8000000000000000);
        CONSTANT(1, 0xffffffffffffffff);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Div).s32().Inputs(0, 1);
            INST(3, Opcode::Return).s32().Inputs(2);
        }
    }
    int64_t result = 0x8000000000000000;
    ASSERT_EQ(ConstFoldingDiv(&INS(2)), true);
    auto inst = GetGraph()->FindConstant(DataType::INT64, result);
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), inst);
    GraphChecker(GetGraph()).Check();
}

TEST_F(ConstFoldingTest, Constant32DivIntTest2)
{
    auto graph = CreateEmptyBytecodeGraph();
    GRAPH(graph)
    {
        CONSTANT(0, 0x80000000);
        CONSTANT(1, 0xffffffff);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Div).s32().Inputs(0, 1);
            INST(3, Opcode::Return).s32().Inputs(2);
        }
    }
    int32_t result = 0x80000000;
    ASSERT_EQ(ConstFoldingDiv(&INS(2)), true);
    auto inst = graph->FindConstant(DataType::INT32, result);
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), inst);
    GraphChecker(graph).Check();
}

TEST_F(ConstFoldingTest, UDivIntTest)
{
    GRAPH(GetGraph())
    {
        CONSTANT(0, 0xffffffff80000000);
        CONSTANT(1, 1);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Div).u32().Inputs(0, 1);
            INST(3, Opcode::Return).u32().Inputs(2);
        }
    }
    int64_t result = 0x80000000;
    ASSERT_EQ(ConstFoldingDiv(&INS(2)), true);
    auto inst = GetGraph()->FindConstant(DataType::INT64, result);
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), inst);
    GraphChecker(GetGraph()).Check();
}

TEST_F(ConstFoldingTest, Constant32UDivIntTest)
{
    auto graph = CreateEmptyBytecodeGraph();
    GRAPH(graph)
    {
        CONSTANT(0, 0xffffffff80000000);
        CONSTANT(1, 1);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Div).u32().Inputs(0, 1);
            INST(3, Opcode::Return).u32().Inputs(2);
        }
    }
    int64_t result = 0x80000000;
    ASSERT_EQ(ConstFoldingDiv(&INS(2)), true);
    auto inst = graph->FindConstant(DataType::INT32, result);
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), inst);
    GraphChecker(graph).Check();
}

TEST_F(ConstFoldingTest, DivFloatTest)
{
    GRAPH(GetGraph())
    {
        CONSTANT(0, static_cast<float>3.0);
        CONSTANT(1, static_cast<float>2.0);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Div).f32().Inputs(0, 1);
            INST(3, Opcode::Return).f32().Inputs(2);
        }
    }
    float result = 1.5;
    ASSERT_EQ(ConstFoldingDiv(&INS(2)), true);
    auto inst = GetGraph()->FindConstant(DataType::FLOAT32, bit_cast<uint32_t, float>(result));
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), inst);
    GraphChecker(GetGraph()).Check();
}

TEST_F(ConstFoldingTest, DivDoubleTest)
{
    GRAPH(GetGraph())
    {
        CONSTANT(0, 3.0);
        CONSTANT(1, 2.0);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Div).f64().Inputs(0, 1);
            INST(3, Opcode::Return).f64().Inputs(2);
        }
    }
    double result = 1.5;
    ASSERT_EQ(ConstFoldingDiv(&INS(2)), true);
    auto inst = GetGraph()->FindConstant(DataType::FLOAT64, bit_cast<uint64_t, double>(result));
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), inst);
    GraphChecker(GetGraph()).Check();
}

TEST_F(ConstFoldingTest, MinIntTest)
{
    GRAPH(GetGraph())
    {
        CONSTANT(0, 3);
        CONSTANT(1, 2);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Min).u64().Inputs(0, 1);
            INST(3, Opcode::Return).u64().Inputs(2);
        }
    }
    int result = 2;
    ASSERT_EQ(ConstFoldingMin(&INS(2)), true);
    auto inst = GetGraph()->FindConstant(DataType::INT64, result);
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), inst);
    GraphChecker(GetGraph()).Check();
}

TEST_F(ConstFoldingTest, MinFloatTest)
{
    GRAPH(GetGraph())
    {
        CONSTANT(0, static_cast<float>3.0);
        CONSTANT(1, static_cast<float>2.0);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Min).f32().Inputs(0, 1);
            INST(3, Opcode::Return).f32().Inputs(2);
        }
    }
    float result = 2.0;
    ASSERT_EQ(ConstFoldingMin(&INS(2)), true);
    auto inst = GetGraph()->FindConstant(DataType::FLOAT32, bit_cast<uint32_t, float>(result));
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), inst);
    GraphChecker(GetGraph()).Check();
}

TEST_F(ConstFoldingTest, MinFloatNegativeZeroTest)
{
    GRAPH(GetGraph())
    {
        CONSTANT(0, -0.0F);
        CONSTANT(1, +0.0F);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Min).f32().Inputs(0, 1);
            INST(3, Opcode::Min).f32().Inputs(1, 0);
            INST(4, Opcode::Min).f32().Inputs(2, 3);
            INST(5, Opcode::Return).f32().Inputs(4);
        }
    }
    ASSERT_EQ(ConstFoldingMin(&INS(2)), true);
    auto inst = GetGraph()->FindConstant(DataType::FLOAT32, bit_cast<uint32_t, float>(-0.0));
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(4).GetInput(0).GetInst(), inst);
    ASSERT_EQ(ConstFoldingMin(&INS(3)), true);
    ASSERT_EQ(INS(4).GetInput(1).GetInst(), inst);
    GraphChecker(GetGraph()).Check();
}

TEST_F(ConstFoldingTest, MinFloatNaNTest)
{
    GRAPH(GetGraph())
    {
        CONSTANT(0, std::numeric_limits<float>::quiet_NaN());
        CONSTANT(1, 1.3F);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Min).f32().Inputs(0, 1);
            INST(3, Opcode::Min).f32().Inputs(1, 0);
            INST(4, Opcode::Min).f32().Inputs(2, 3);
            INST(5, Opcode::Return).f32().Inputs(4);
        }
    }
    ASSERT_EQ(ConstFoldingMin(&INS(2)), true);
    auto inst =
        GetGraph()->FindConstant(DataType::FLOAT32, bit_cast<uint32_t, float>(std::numeric_limits<float>::quiet_NaN()));
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(4).GetInput(0).GetInst(), inst);
    ASSERT_EQ(ConstFoldingMin(&INS(3)), true);
    ASSERT_EQ(INS(4).GetInput(1).GetInst(), inst);
    GraphChecker(GetGraph()).Check();
}

TEST_F(ConstFoldingTest, MinDoubleTest)
{
    GRAPH(GetGraph())
    {
        CONSTANT(0, 3.0);
        CONSTANT(1, 2.0);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Min).f64().Inputs(0, 1);
            INST(3, Opcode::Return).f64().Inputs(2);
        }
    }
    double result = 2.0;
    ASSERT_EQ(ConstFoldingMin(&INS(2)), true);
    auto inst = GetGraph()->FindConstant(DataType::FLOAT64, bit_cast<uint64_t, double>(result));
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), inst);
    GraphChecker(GetGraph()).Check();
}

TEST_F(ConstFoldingTest, MinDoubleNegativeZeroTest)
{
    GRAPH(GetGraph())
    {
        CONSTANT(0, -0.0);
        CONSTANT(1, +0.0);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Min).f64().Inputs(0, 1);
            INST(3, Opcode::Min).f64().Inputs(1, 0);
            INST(4, Opcode::Min).f64().Inputs(2, 3);
            INST(5, Opcode::Return).f64().Inputs(4);
        }
    }
    ASSERT_EQ(ConstFoldingMin(&INS(2)), true);
    auto inst = GetGraph()->FindConstant(DataType::FLOAT64, bit_cast<uint64_t, double>(-0.0));
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(4).GetInput(0).GetInst(), inst);
    ASSERT_EQ(ConstFoldingMin(&INS(3)), true);
    ASSERT_EQ(INS(4).GetInput(1).GetInst(), inst);
    GraphChecker(GetGraph()).Check();
}

TEST_F(ConstFoldingTest, MinDoubleNaNTest)
{
    GRAPH(GetGraph())
    {
        CONSTANT(0, std::numeric_limits<double>::quiet_NaN());
        CONSTANT(1, 1.3);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Min).f64().Inputs(0, 1);
            INST(3, Opcode::Min).f64().Inputs(1, 0);
            INST(4, Opcode::Min).f64().Inputs(2, 3);
            INST(5, Opcode::Return).f64().Inputs(4);
        }
    }
    ASSERT_EQ(ConstFoldingMin(&INS(2)), true);
    auto inst = GetGraph()->FindConstant(DataType::FLOAT64,
                                         bit_cast<uint64_t, double>(std::numeric_limits<double>::quiet_NaN()));
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(4).GetInput(0).GetInst(), inst);
    ASSERT_EQ(ConstFoldingMin(&INS(3)), true);
    ASSERT_EQ(INS(4).GetInput(1).GetInst(), inst);
    GraphChecker(GetGraph()).Check();
}

TEST_F(ConstFoldingTest, MaxIntTest)
{
    GRAPH(GetGraph())
    {
        CONSTANT(0, 3);
        CONSTANT(1, 2);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Max).u64().Inputs(0, 1);
            INST(3, Opcode::Return).u64().Inputs(2);
        }
    }
    int result = 3;
    ASSERT_EQ(ConstFoldingMax(&INS(2)), true);
    auto inst = GetGraph()->FindConstant(DataType::INT64, result);
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), inst);
    GraphChecker(GetGraph()).Check();
}

TEST_F(ConstFoldingTest, MaxFloatTest)
{
    GRAPH(GetGraph())
    {
        CONSTANT(0, static_cast<float>3.0);
        CONSTANT(1, static_cast<float>2.0);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Max).f32().Inputs(0, 1);
            INST(3, Opcode::Return).f32().Inputs(2);
        }
    }
    float result = 3.0;
    ASSERT_EQ(ConstFoldingMax(&INS(2)), true);
    auto inst = GetGraph()->FindConstant(DataType::FLOAT32, bit_cast<uint32_t, float>(result));
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), inst);
    GraphChecker(GetGraph()).Check();
}

TEST_F(ConstFoldingTest, MaxFloatNegativeZeroTest)
{
    GRAPH(GetGraph())
    {
        CONSTANT(0, -0.0F);
        CONSTANT(1, +0.0F);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Max).f32().Inputs(0, 1);
            INST(3, Opcode::Max).f32().Inputs(1, 0);
            INST(4, Opcode::Min).f32().Inputs(2, 3);
            INST(5, Opcode::Return).f32().Inputs(4);
        }
    }
    ASSERT_EQ(ConstFoldingMax(&INS(2)), true);
    auto inst = GetGraph()->FindConstant(DataType::FLOAT32, bit_cast<uint32_t, float>(+0.0));
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(4).GetInput(0).GetInst(), inst);
    ASSERT_EQ(ConstFoldingMax(&INS(3)), true);
    ASSERT_EQ(INS(4).GetInput(1).GetInst(), inst);
    GraphChecker(GetGraph()).Check();
}

TEST_F(ConstFoldingTest, MaxFloatNaNTest)
{
    GRAPH(GetGraph())
    {
        CONSTANT(0, std::numeric_limits<float>::quiet_NaN());
        CONSTANT(1, 1.3F);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Max).f32().Inputs(0, 1);
            INST(3, Opcode::Max).f32().Inputs(1, 0);
            INST(4, Opcode::Min).f32().Inputs(2, 3);
            INST(5, Opcode::Return).f32().Inputs(4);
        }
    }
    ASSERT_EQ(ConstFoldingMax(&INS(2)), true);
    auto inst =
        GetGraph()->FindConstant(DataType::FLOAT32, bit_cast<uint32_t, float>(std::numeric_limits<float>::quiet_NaN()));
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(4).GetInput(0).GetInst(), inst);
    ASSERT_EQ(ConstFoldingMax(&INS(3)), true);
    ASSERT_EQ(INS(4).GetInput(1).GetInst(), inst);
    GraphChecker(GetGraph()).Check();
}

TEST_F(ConstFoldingTest, MaxDoubleTest)
{
    GRAPH(GetGraph())
    {
        CONSTANT(0, 3.0);
        CONSTANT(1, 2.0);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Max).f64().Inputs(0, 1);
            INST(3, Opcode::Return).f64().Inputs(2);
        }
    }
    double result = 3.0;
    ASSERT_EQ(ConstFoldingMax(&INS(2)), true);
    auto inst = GetGraph()->FindConstant(DataType::FLOAT64, bit_cast<uint64_t, double>(result));
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), inst);
    GraphChecker(GetGraph()).Check();
}

TEST_F(ConstFoldingTest, MaxDoubleNegativeZeroTest)
{
    GRAPH(GetGraph())
    {
        CONSTANT(0, -0.0);
        CONSTANT(1, +0.0);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Max).f64().Inputs(0, 1);
            INST(3, Opcode::Max).f64().Inputs(1, 0);
            INST(4, Opcode::Min).f64().Inputs(2, 3);
            INST(5, Opcode::Return).f64().Inputs(4);
        }
    }
    ASSERT_EQ(ConstFoldingMax(&INS(2)), true);
    auto inst = GetGraph()->FindConstant(DataType::FLOAT64, bit_cast<uint64_t, double>(+0.0));
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(4).GetInput(0).GetInst(), inst);
    ASSERT_EQ(ConstFoldingMax(&INS(3)), true);
    ASSERT_EQ(INS(4).GetInput(1).GetInst(), inst);
    GraphChecker(GetGraph()).Check();
}

TEST_F(ConstFoldingTest, MaxDoubleNaNTest)
{
    GRAPH(GetGraph())
    {
        CONSTANT(0, std::numeric_limits<double>::quiet_NaN());
        CONSTANT(1, 1.3);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Max).f64().Inputs(0, 1);
            INST(3, Opcode::Max).f64().Inputs(1, 0);
            INST(4, Opcode::Min).f64().Inputs(2, 3);
            INST(5, Opcode::Return).f64().Inputs(4);
        }
    }
    ASSERT_EQ(ConstFoldingMax(&INS(2)), true);
    auto inst = GetGraph()->FindConstant(DataType::FLOAT64,
                                         bit_cast<uint64_t, double>(std::numeric_limits<double>::quiet_NaN()));
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(4).GetInput(0).GetInst(), inst);
    ASSERT_EQ(ConstFoldingMax(&INS(3)), true);
    ASSERT_EQ(INS(4).GetInput(1).GetInst(), inst);
    GraphChecker(GetGraph()).Check();
}

TEST_F(ConstFoldingTest, ShlTest)
{
    GRAPH(GetGraph())
    {
        CONSTANT(0, 1);
        CONSTANT(1, 2);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Shl).u64().Inputs(0, 1);
            INST(3, Opcode::Return).u64().Inputs(2);
        }
    }
    int result = 4;
    ASSERT_EQ(ConstFoldingShl(&INS(2)), true);
    auto inst = GetGraph()->FindConstant(DataType::INT64, result);
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), inst);
    GraphChecker(GetGraph()).Check();
}

TEST_F(ConstFoldingTest, Constant32ShlTest)
{
    auto graph = CreateEmptyBytecodeGraph();
    GRAPH(graph)
    {
        CONSTANT(0, 1);
        CONSTANT(1, 2);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Shl).u32().Inputs(0, 1);
            INST(3, Opcode::Return).u32().Inputs(2);
        }
    }
    int result = 4;
    ASSERT_EQ(ConstFoldingShl(&INS(2)), true);
    auto inst = graph->FindConstant(DataType::INT32, result);
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), inst);
    GraphChecker(graph).Check();
}

TEST_F(ConstFoldingTest, Shl64Test)
{
    GRAPH(GetGraph())
    {
        CONSTANT(0, 1);
        CONSTANT(1, 66);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Shl).u64().Inputs(0, 1);
            INST(3, Opcode::Return).u64().Inputs(2);
        }
    }
    int result = 4;
    ASSERT_EQ(ConstFoldingShl(&INS(2)), true);
    auto inst = GetGraph()->FindConstant(DataType::INT64, result);
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), inst);
    GraphChecker(GetGraph()).Check();
}

TEST_F(ConstFoldingTest, Shl32Test)
{
    GRAPH(GetGraph())
    {
        CONSTANT(0, 1);
        CONSTANT(1, 34);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Shl).u32().Inputs(0, 1);
            INST(3, Opcode::Return).u32().Inputs(2);
        }
    }
    int result = 4;
    ASSERT_EQ(ConstFoldingShl(&INS(2)), true);
    auto inst = GetGraph()->FindConstant(DataType::INT64, result);
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), inst);
    GraphChecker(GetGraph()).Check();
}

TEST_F(ConstFoldingTest, Constant32Shl32Test)
{
    auto graph = CreateEmptyBytecodeGraph();
    GRAPH(graph)
    {
        CONSTANT(0, 1);
        CONSTANT(1, 34);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Shl).u32().Inputs(0, 1);
            INST(3, Opcode::Return).u32().Inputs(2);
        }
    }
    int result = 4;
    ASSERT_EQ(ConstFoldingShl(&INS(2)), true);
    auto inst = graph->FindConstant(DataType::INT32, result);
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), inst);
    GraphChecker(graph).Check();
}

TEST_F(ConstFoldingTest, ShrTest)
{
    GRAPH(GetGraph())
    {
        CONSTANT(0, 4);
        CONSTANT(1, 2);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Shr).u64().Inputs(0, 1);
            INST(3, Opcode::Return).u64().Inputs(2);
        }
    }
    int result = 1;
    ASSERT_EQ(ConstFoldingShr(&INS(2)), true);
    auto inst = GetGraph()->FindConstant(DataType::INT64, result);
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), inst);
    GraphChecker(GetGraph()).Check();
}

TEST_F(ConstFoldingTest, Constant32ShrTest)
{
    auto graph = CreateEmptyBytecodeGraph();
    GRAPH(graph)
    {
        CONSTANT(0, 4);
        CONSTANT(1, 2);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Shr).u32().Inputs(0, 1);
            INST(3, Opcode::Return).u32().Inputs(2);
        }
    }
    int result = 1;
    ASSERT_EQ(ConstFoldingShr(&INS(2)), true);
    auto inst = graph->FindConstant(DataType::INT32, result);
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), inst);
    GraphChecker(graph).Check();
}

TEST_F(ConstFoldingTest, AShrUTest)
{
    GRAPH(GetGraph())
    {
        CONSTANT(0, 4);
        CONSTANT(1, 2);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::AShr).u64().Inputs(0, 1);
            INST(3, Opcode::Return).u64().Inputs(2);
        }
    }
    uint64_t result = 1;
    ASSERT_EQ(ConstFoldingAShr(&INS(2)), true);
    auto inst = GetGraph()->FindConstant(DataType::INT64, result);
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), inst);
    GraphChecker(GetGraph()).Check();
}

TEST_F(ConstFoldingTest, Constant32AShrUTest)
{
    auto graph = CreateEmptyBytecodeGraph();
    GRAPH(graph)
    {
        CONSTANT(0, 4);
        CONSTANT(1, 2);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::AShr).u32().Inputs(0, 1);
            INST(3, Opcode::Return).u32().Inputs(2);
        }
    }
    uint32_t result = 1;
    ASSERT_EQ(ConstFoldingAShr(&INS(2)), true);
    auto inst = graph->FindConstant(DataType::INT32, result);
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), inst);
    GraphChecker(graph).Check();
}

TEST_F(ConstFoldingTest, AShrTest)
{
    GRAPH(GetGraph())
    {
        CONSTANT(0, -4);
        CONSTANT(1, 2);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::AShr).s64().Inputs(0, 1);
            INST(3, Opcode::Return).s64().Inputs(2);
        }
    }
    int64_t result = -1;
    ASSERT_EQ(ConstFoldingAShr(&INS(2)), true);
    auto inst = GetGraph()->FindConstant(DataType::INT64, result);
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), inst);
    GraphChecker(GetGraph()).Check();
}

TEST_F(ConstFoldingTest, Constant32AShrTest)
{
    auto graph = CreateEmptyBytecodeGraph();
    GRAPH(graph)
    {
        CONSTANT(0, -4);
        CONSTANT(1, 2);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::AShr).s32().Inputs(0, 1);
            INST(3, Opcode::Return).s32().Inputs(2);
        }
    }
    int64_t result = -1;
    ASSERT_EQ(ConstFoldingAShr(&INS(2)), true);
    auto inst = graph->FindConstant(DataType::INT32, result);
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), inst);
    GraphChecker(graph).Check();
}

TEST_F(ConstFoldingTest, Shr32Test)
{
    GRAPH(GetGraph())
    {
        CONSTANT(0, -4);
        CONSTANT(1, 4);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Shr).s32().Inputs(0, 1);
            INST(3, Opcode::Return).s32().Inputs(2);
        }
    }
    int64_t result = 0xfffffff;
    ASSERT_EQ(ConstFoldingShr(&INS(2)), true);
    auto inst = GetGraph()->FindConstant(DataType::INT64, result);
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), inst);
    GraphChecker(GetGraph()).Check();
}

TEST_F(ConstFoldingTest, Constant32Shr32Test)
{
    auto graph = CreateEmptyBytecodeGraph();
    GRAPH(graph)
    {
        CONSTANT(0, -4);
        CONSTANT(1, 4);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Shr).s32().Inputs(0, 1);
            INST(3, Opcode::Return).s32().Inputs(2);
        }
    }
    int64_t result = 0xfffffff;
    ASSERT_EQ(ConstFoldingShr(&INS(2)), true);
    auto inst = graph->FindConstant(DataType::INT32, result);
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), inst);
    GraphChecker(graph).Check();
}

TEST_F(ConstFoldingTest, ModTestInt)
{
    GRAPH(GetGraph())
    {
        CONSTANT(0, 3);
        CONSTANT(1, 2);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Mod).u64().Inputs(0, 1);
            INST(3, Opcode::Return).u64().Inputs(2);
        }
    }
    int result = 1;
    ASSERT_EQ(ConstFoldingMod(&INS(2)), true);
    auto inst = GetGraph()->FindConstant(DataType::INT64, result);
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), inst);
    GraphChecker(GetGraph()).Check();
}

TEST_F(ConstFoldingTest, Constant32ModTestInt)
{
    auto graph = CreateEmptyBytecodeGraph();
    GRAPH(graph)
    {
        CONSTANT(0, 3);
        CONSTANT(1, 2);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Mod).u32().Inputs(0, 1);
            INST(3, Opcode::Return).u32().Inputs(2);
        }
    }
    int result = 1;
    ASSERT_EQ(ConstFoldingMod(&INS(2)), true);
    auto inst = graph->FindConstant(DataType::INT32, result);
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), inst);
    GraphChecker(graph).Check();
}

TEST_F(ConstFoldingTest, ModIntTest1)
{
    GRAPH(GetGraph())
    {
        CONSTANT(0, 0xffffffff80000000);
        CONSTANT(1, 0xffffffffffffffff);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Mod).s32().Inputs(0, 1);
            INST(3, Opcode::Return).s32().Inputs(2);
        }
    }
    int32_t result = 0;
    ASSERT_EQ(ConstFoldingMod(&INS(2)), true);
    auto inst = GetGraph()->FindConstant(DataType::INT64, result);
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), inst);
    GraphChecker(GetGraph()).Check();
}

TEST_F(ConstFoldingTest, Constant32ModIntTest1)
{
    auto graph = CreateEmptyBytecodeGraph();
    GRAPH(graph)
    {
        CONSTANT(0, 0xffffffff80000000);
        CONSTANT(1, 0xffffffffffffffff);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Mod).s32().Inputs(0, 1);
            INST(3, Opcode::Return).s32().Inputs(2);
        }
    }
    int32_t result = 0;
    ASSERT_EQ(ConstFoldingMod(&INS(2)), true);
    auto inst = graph->FindConstant(DataType::INT32, result);
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), inst);
    GraphChecker(graph).Check();
}

TEST_F(ConstFoldingTest, ModTestFloat)
{
    GRAPH(GetGraph())
    {
        CONSTANT(0, 7.3F);
        CONSTANT(1, 2.9F);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Mod).f32().Inputs(0, 1);
            INST(3, Opcode::Return).f32().Inputs(2);
        }
    }
    float result = 1.5;
    ASSERT_EQ(ConstFoldingMod(&INS(2)), true);
    auto inst = GetGraph()->FindConstant(DataType::FLOAT32, bit_cast<uint32_t, float>(result));
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), inst);
    GraphChecker(GetGraph()).Check();
}

TEST_F(ConstFoldingTest, ModTestDouble)
{
    GRAPH(GetGraph())
    {
        CONSTANT(0, 15.5);
        CONSTANT(1, 2.2);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Mod).f64().Inputs(0, 1);
            INST(3, Opcode::Return).f64().Inputs(2);
        }
    }
    double result = fmod(15.5, 2.2);
    ASSERT_EQ(ConstFoldingMod(&INS(2)), true);
    auto inst = GetGraph()->FindConstant(DataType::FLOAT64, bit_cast<uint64_t, double>(result));
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), inst);
    GraphChecker(GetGraph()).Check();
}

TEST_F(ConstFoldingTest, Mod1Test)
{
    GRAPH(GetGraph())
    {
        PARAMETER(0, 0).u64();
        CONSTANT(1, 1);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Mod).u64().Inputs(0, 1);
            INST(3, Opcode::Return).u64().Inputs(2);
        }
    }
    ASSERT_EQ(ConstFoldingMod(&INS(2)), true);
    int result = 0;
    auto inst = GetGraph()->FindConstant(DataType::INT64, result);
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), inst);
    GraphChecker(GetGraph()).Check();
}

TEST_F(ConstFoldingTest, Constant32Mod1Test)
{
    auto graph = CreateEmptyBytecodeGraph();
    GRAPH(graph)
    {
        PARAMETER(0, 0).u32();
        CONSTANT(1, 1);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Mod).u32().Inputs(0, 1);
            INST(3, Opcode::Return).u32().Inputs(2);
        }
    }
    ASSERT_EQ(ConstFoldingMod(&INS(2)), true);
    int result = 0;
    auto inst = graph->FindConstant(DataType::INT32, result);
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), inst);
    GraphChecker(graph).Check();
}

TEST_F(ConstFoldingTest, Mod1TestFloat)
{
    GRAPH(GetGraph())
    {
        PARAMETER(0, 0).f64();
        CONSTANT(1, 1.0);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Mod).f64().Inputs(0, 1);
            INST(3, Opcode::Return).f64().Inputs(2);
        }
    }
    // the optimization "x % 1 -> 0" is not applicable for floating point values
    ASSERT_EQ(ConstFoldingMod(&INS(2)), false);
}

TEST_F(ConstFoldingTest, AndTest)
{
    GRAPH(GetGraph())
    {
        CONSTANT(0, 1);
        CONSTANT(1, 2);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::And).u64().Inputs(0, 1);
            INST(3, Opcode::Return).u64().Inputs(2);
        }
    }
    int result = 0;
    ASSERT_EQ(ConstFoldingAnd(&INS(2)), true);
    auto inst = GetGraph()->FindConstant(DataType::INT64, result);
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), inst);
    GraphChecker(GetGraph()).Check();
}

TEST_F(ConstFoldingTest, Constant32AndTest)
{
    auto graph = CreateEmptyBytecodeGraph();
    GRAPH(graph)
    {
        CONSTANT(0, 1);
        CONSTANT(1, 2);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::And).u32().Inputs(0, 1);
            INST(3, Opcode::Return).u32().Inputs(2);
        }
    }
    int result = 0;
    ASSERT_EQ(ConstFoldingAnd(&INS(2)), true);
    auto inst = graph->FindConstant(DataType::INT32, result);
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), inst);
    GraphChecker(graph).Check();
}

TEST_F(ConstFoldingTest, OrTest)
{
    GRAPH(GetGraph())
    {
        CONSTANT(0, 1);
        CONSTANT(1, 2);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Or).u64().Inputs(0, 1);
            INST(3, Opcode::Return).u64().Inputs(2);
        }
    }
    int result = 3;
    ASSERT_EQ(ConstFoldingOr(&INS(2)), true);
    auto inst = GetGraph()->FindConstant(DataType::INT64, result);
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), inst);
    GraphChecker(GetGraph()).Check();
}

TEST_F(ConstFoldingTest, Constant32OrTest)
{
    auto graph = CreateEmptyBytecodeGraph();
    GRAPH(graph)
    {
        CONSTANT(0, 1);
        CONSTANT(1, 2);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Or).u32().Inputs(0, 1);
            INST(3, Opcode::Return).u32().Inputs(2);
        }
    }
    int result = 3;
    ASSERT_EQ(ConstFoldingOr(&INS(2)), true);
    auto inst = graph->FindConstant(DataType::INT32, result);
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), inst);
    GraphChecker(graph).Check();
}

TEST_F(ConstFoldingTest, XorTest)
{
    GRAPH(GetGraph())
    {
        CONSTANT(0, 1);
        CONSTANT(1, 2);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Xor).u64().Inputs(0, 1);
            INST(3, Opcode::Return).u64().Inputs(2);
        }
    }
    int result = 3;
    ASSERT_EQ(ConstFoldingXor(&INS(2)), true);
    auto inst = GetGraph()->FindConstant(DataType::INT64, result);
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), inst);
    GraphChecker(GetGraph()).Check();
}

TEST_F(ConstFoldingTest, Constant32XorTest)
{
    auto graph = CreateEmptyBytecodeGraph();
    GRAPH(graph)
    {
        CONSTANT(0, 1);
        CONSTANT(1, 2);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Xor).u32().Inputs(0, 1);
            INST(3, Opcode::Return).u32().Inputs(2);
        }
    }
    int result = 3;
    ASSERT_EQ(ConstFoldingXor(&INS(2)), true);
    auto inst = graph->FindConstant(DataType::INT32, result);
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), inst);
    GraphChecker(graph).Check();
}

TEST_F(ConstFoldingTest, CompareEQTest)
{
    GRAPH(GetGraph())
    {
        CONSTANT(0, 3);
        CONSTANT(1, 3);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Compare).b().SrcType(DataType::Type::INT64).CC(CC_EQ).Inputs(0, 1);
            INST(3, Opcode::Return).b().Inputs(2);
        }
    }
    int result = 1;
    ASSERT_EQ(ConstFoldingCompare(&INS(2)), true);
    auto inst = GetGraph()->FindConstant(DataType::INT64, result);
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), inst);
    GraphChecker(GetGraph()).Check();
}

TEST_F(ConstFoldingTest, Constant32CompareEQTest)
{
    auto graph = CreateEmptyBytecodeGraph();
    GRAPH(graph)
    {
        CONSTANT(0, 3);
        CONSTANT(1, 3);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Compare).b().SrcType(DataType::Type::INT32).CC(CC_EQ).Inputs(0, 1);
            INST(3, Opcode::Return).b().Inputs(2);
        }
    }
    int result = 1;
    ASSERT_EQ(ConstFoldingCompare(&INS(2)), true);
    auto inst = graph->FindConstant(DataType::INT32, result);
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), inst);
    GraphChecker(graph).Check();
}

TEST_F(ConstFoldingTest, CompareNETest)
{
    GRAPH(GetGraph())
    {
        CONSTANT(0, 3);
        CONSTANT(1, 3);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Compare).b().SrcType(DataType::Type::INT64).CC(CC_NE).Inputs(0, 1);
            INST(3, Opcode::Return).b().Inputs(2);
        }
    }
    int result = 0;
    ASSERT_EQ(ConstFoldingCompare(&INS(2)), true);
    auto inst = GetGraph()->FindConstant(DataType::INT64, result);
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), inst);
    GraphChecker(GetGraph()).Check();
}

TEST_F(ConstFoldingTest, Constant32CompareNETest)
{
    auto graph = CreateEmptyBytecodeGraph();
    GRAPH(graph)
    {
        CONSTANT(0, 3);
        CONSTANT(1, 3);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Compare).b().SrcType(DataType::Type::INT32).CC(CC_NE).Inputs(0, 1);
            INST(3, Opcode::Return).b().Inputs(2);
        }
    }
    int result = 0;
    ASSERT_EQ(ConstFoldingCompare(&INS(2)), true);
    auto inst = graph->FindConstant(DataType::INT32, result);
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), inst);
    GraphChecker(graph).Check();
}

TEST_F(ConstFoldingTest, CompareLTTest)
{
    GRAPH(GetGraph())
    {
        CONSTANT(0, 3);
        CONSTANT(1, 3);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Compare).b().SrcType(DataType::Type::INT64).CC(CC_LT).Inputs(0, 1);
            INST(3, Opcode::Return).b().Inputs(2);
        }
    }
    int result = 0;
    ASSERT_EQ(ConstFoldingCompare(&INS(2)), true);
    auto inst = GetGraph()->FindConstant(DataType::INT64, result);
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), inst);
    GraphChecker(GetGraph()).Check();
}

TEST_F(ConstFoldingTest, Constant32CompareLTTest)
{
    auto graph = CreateEmptyBytecodeGraph();
    GRAPH(graph)
    {
        CONSTANT(0, 3);
        CONSTANT(1, 3);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Compare).b().SrcType(DataType::Type::INT32).CC(CC_LT).Inputs(0, 1);
            INST(3, Opcode::Return).b().Inputs(2);
        }
    }
    int result = 0;
    ASSERT_EQ(ConstFoldingCompare(&INS(2)), true);
    auto inst = graph->FindConstant(DataType::INT32, result);
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), inst);
    GraphChecker(graph).Check();
}

TEST_F(ConstFoldingTest, CompareLTTest1)
{
    GRAPH(GetGraph())
    {
        CONSTANT(0, -3);
        CONSTANT(1, 3);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Compare).b().SrcType(DataType::Type::INT64).CC(CC_LT).Inputs(0, 1);
            INST(3, Opcode::Return).b().Inputs(2);
        }
    }
    int result = 1;
    ASSERT_EQ(ConstFoldingCompare(&INS(2)), true);
    auto inst = GetGraph()->FindConstant(DataType::INT64, result);
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), inst);
    GraphChecker(GetGraph()).Check();
}

TEST_F(ConstFoldingTest, Constant32CompareLTTest1)
{
    auto graph = CreateEmptyBytecodeGraph();
    GRAPH(graph)
    {
        CONSTANT(0, -3);
        CONSTANT(1, 3);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Compare).b().SrcType(DataType::Type::INT32).CC(CC_LT).Inputs(0, 1);
            INST(3, Opcode::Return).b().Inputs(2);
        }
    }
    int result = 1;
    ASSERT_EQ(ConstFoldingCompare(&INS(2)), true);
    auto inst = graph->FindConstant(DataType::INT32, result);
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), inst);
    GraphChecker(graph).Check();
}

TEST_F(ConstFoldingTest, CompareLETest)
{
    GRAPH(GetGraph())
    {
        CONSTANT(0, 3);
        CONSTANT(1, 3);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Compare).b().SrcType(DataType::Type::INT64).CC(CC_LE).Inputs(0, 1);
            INST(3, Opcode::Return).b().Inputs(2);
        }
    }
    int result = 1;
    ASSERT_EQ(ConstFoldingCompare(&INS(2)), true);
    auto inst = GetGraph()->FindConstant(DataType::INT64, result);
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), inst);
    GraphChecker(GetGraph()).Check();
}

TEST_F(ConstFoldingTest, Constant32CompareLETest)
{
    auto graph = CreateEmptyBytecodeGraph();
    GRAPH(graph)
    {
        CONSTANT(0, 3);
        CONSTANT(1, 3);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Compare).b().SrcType(DataType::Type::INT32).CC(CC_LE).Inputs(0, 1);
            INST(3, Opcode::Return).b().Inputs(2);
        }
    }
    int result = 1;
    ASSERT_EQ(ConstFoldingCompare(&INS(2)), true);
    auto inst = graph->FindConstant(DataType::INT32, result);
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), inst);
    GraphChecker(graph).Check();
}

TEST_F(ConstFoldingTest, CompareLETest1)
{
    GRAPH(GetGraph())
    {
        CONSTANT(0, -1);
        CONSTANT(1, 3);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Compare).b().SrcType(DataType::Type::INT64).CC(CC_LE).Inputs(0, 1);
            INST(3, Opcode::Return).b().Inputs(2);
        }
    }
    int result = 1;
    ASSERT_EQ(ConstFoldingCompare(&INS(2)), true);
    auto inst = GetGraph()->FindConstant(DataType::INT64, result);
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), inst);
    GraphChecker(GetGraph()).Check();
}

TEST_F(ConstFoldingTest, Constant32CompareLETest1)
{
    auto graph = CreateEmptyBytecodeGraph();
    GRAPH(graph)
    {
        CONSTANT(0, -1);
        CONSTANT(1, 3);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Compare).b().SrcType(DataType::Type::INT32).CC(CC_LE).Inputs(0, 1);
            INST(3, Opcode::Return).b().Inputs(2);
        }
    }
    int result = 1;
    ASSERT_EQ(ConstFoldingCompare(&INS(2)), true);
    auto inst = graph->FindConstant(DataType::INT32, result);
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), inst);
    GraphChecker(graph).Check();
}

TEST_F(ConstFoldingTest, CompareGTTest)
{
    GRAPH(GetGraph())
    {
        CONSTANT(0, 3);
        CONSTANT(1, 3);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Compare).b().SrcType(DataType::Type::INT64).CC(CC_GT).Inputs(0, 1);
            INST(3, Opcode::Return).b().Inputs(2);
        }
    }
    int result = 0;
    ASSERT_EQ(ConstFoldingCompare(&INS(2)), true);
    auto inst = GetGraph()->FindConstant(DataType::INT64, result);
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), inst);
    GraphChecker(GetGraph()).Check();
}

TEST_F(ConstFoldingTest, Constant32CompareGTTest)
{
    auto graph = CreateEmptyBytecodeGraph();
    GRAPH(graph)
    {
        CONSTANT(0, 3);
        CONSTANT(1, 3);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Compare).b().SrcType(DataType::Type::INT32).CC(CC_GT).Inputs(0, 1);
            INST(3, Opcode::Return).b().Inputs(2);
        }
    }
    int result = 0;
    ASSERT_EQ(ConstFoldingCompare(&INS(2)), true);
    auto inst = graph->FindConstant(DataType::INT32, result);
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), inst);
    GraphChecker(graph).Check();
}

TEST_F(ConstFoldingTest, CompareGTTest1)
{
    GRAPH(GetGraph())
    {
        CONSTANT(0, -1);
        CONSTANT(1, -2);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Compare).b().SrcType(DataType::Type::INT64).CC(CC_GT).Inputs(0, 1);
            INST(3, Opcode::Return).b().Inputs(2);
        }
    }
    int result = 1;
    ASSERT_EQ(ConstFoldingCompare(&INS(2)), true);
    auto inst = GetGraph()->FindConstant(DataType::INT64, result);
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), inst);
    GraphChecker(GetGraph()).Check();
}

TEST_F(ConstFoldingTest, Constant32CompareGTTest1)
{
    auto graph = CreateEmptyBytecodeGraph();
    GRAPH(graph)
    {
        CONSTANT(0, -1);
        CONSTANT(1, -2);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Compare).b().SrcType(DataType::Type::INT32).CC(CC_GT).Inputs(0, 1);
            INST(3, Opcode::Return).b().Inputs(2);
        }
    }
    int result = 1;
    ASSERT_EQ(ConstFoldingCompare(&INS(2)), true);
    auto inst = graph->FindConstant(DataType::INT32, result);
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), inst);
    GraphChecker(graph).Check();
}

TEST_F(ConstFoldingTest, CompareGETest)
{
    GRAPH(GetGraph())
    {
        CONSTANT(0, 3);
        CONSTANT(1, 3);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Compare).b().SrcType(DataType::Type::INT64).CC(CC_GE).Inputs(0, 1);
            INST(3, Opcode::Return).b().Inputs(2);
        }
    }
    int result = 1;
    ASSERT_EQ(ConstFoldingCompare(&INS(2)), true);
    auto inst = GetGraph()->FindConstant(DataType::INT64, result);
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), inst);
    GraphChecker(GetGraph()).Check();
}

TEST_F(ConstFoldingTest, Constant32CompareGETest)
{
    auto graph = CreateEmptyBytecodeGraph();
    GRAPH(graph)
    {
        CONSTANT(0, 3);
        CONSTANT(1, 3);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Compare).b().SrcType(DataType::Type::INT32).CC(CC_GE).Inputs(0, 1);
            INST(3, Opcode::Return).b().Inputs(2);
        }
    }
    int result = 1;
    ASSERT_EQ(ConstFoldingCompare(&INS(2)), true);
    auto inst = graph->FindConstant(DataType::INT32, result);
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), inst);
    GraphChecker(graph).Check();
}

TEST_F(ConstFoldingTest, CompareGETest1)
{
    GRAPH(GetGraph())
    {
        CONSTANT(0, -1);
        CONSTANT(1, -2);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Compare).b().SrcType(DataType::Type::INT64).CC(CC_GE).Inputs(0, 1);
            INST(3, Opcode::Return).b().Inputs(2);
        }
    }
    int result = 1;
    ASSERT_EQ(ConstFoldingCompare(&INS(2)), true);
    auto inst = GetGraph()->FindConstant(DataType::INT64, result);
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), inst);
    GraphChecker(GetGraph()).Check();
}

TEST_F(ConstFoldingTest, Constant32CompareGETest1)
{
    auto graph = CreateEmptyBytecodeGraph();
    GRAPH(graph)
    {
        CONSTANT(0, -1);
        CONSTANT(1, -2);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Compare).b().SrcType(DataType::Type::INT32).CC(CC_GE).Inputs(0, 1);
            INST(3, Opcode::Return).b().Inputs(2);
        }
    }
    int result = 1;
    ASSERT_EQ(ConstFoldingCompare(&INS(2)), true);
    auto inst = graph->FindConstant(DataType::INT32, result);
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), inst);
    GraphChecker(graph).Check();
}

TEST_F(ConstFoldingTest, CompareBTest)
{
    GRAPH(GetGraph())
    {
        CONSTANT(0, 3);
        CONSTANT(1, 3);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Compare).b().SrcType(DataType::Type::INT64).CC(CC_B).Inputs(0, 1);
            INST(3, Opcode::Return).b().Inputs(2);
        }
    }
    int result = 0;
    ASSERT_EQ(ConstFoldingCompare(&INS(2)), true);
    auto inst = GetGraph()->FindConstant(DataType::INT64, result);
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), inst);
    GraphChecker(GetGraph()).Check();
}

TEST_F(ConstFoldingTest, Constant32CompareBTest)
{
    auto graph = CreateEmptyBytecodeGraph();
    GRAPH(graph)
    {
        CONSTANT(0, 3);
        CONSTANT(1, 3);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Compare).b().SrcType(DataType::Type::INT32).CC(CC_B).Inputs(0, 1);
            INST(3, Opcode::Return).b().Inputs(2);
        }
    }
    int result = 0;
    ASSERT_EQ(ConstFoldingCompare(&INS(2)), true);
    auto inst = graph->FindConstant(DataType::INT32, result);
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), inst);
    GraphChecker(graph).Check();
}

TEST_F(ConstFoldingTest, CompareBETest)
{
    GRAPH(GetGraph())
    {
        CONSTANT(0, 3);
        CONSTANT(1, 3);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Compare).b().SrcType(DataType::Type::INT64).CC(CC_BE).Inputs(0, 1);
            INST(3, Opcode::Return).b().Inputs(2);
        }
    }
    int result = 1;
    ASSERT_EQ(ConstFoldingCompare(&INS(2)), true);
    auto inst = GetGraph()->FindConstant(DataType::INT64, result);
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), inst);
    GraphChecker(GetGraph()).Check();
}

TEST_F(ConstFoldingTest, Constant32CompareBETest)
{
    auto graph = CreateEmptyBytecodeGraph();
    GRAPH(graph)
    {
        CONSTANT(0, 3);
        CONSTANT(1, 3);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Compare).b().SrcType(DataType::Type::INT32).CC(CC_BE).Inputs(0, 1);
            INST(3, Opcode::Return).b().Inputs(2);
        }
    }
    int result = 1;
    ASSERT_EQ(ConstFoldingCompare(&INS(2)), true);
    auto inst = graph->FindConstant(DataType::INT32, result);
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), inst);
    GraphChecker(graph).Check();
}

TEST_F(ConstFoldingTest, CompareATest)
{
    GRAPH(GetGraph())
    {
        CONSTANT(0, 3);
        CONSTANT(1, 3);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Compare).b().SrcType(DataType::Type::INT64).CC(CC_A).Inputs(0, 1);
            INST(3, Opcode::Return).b().Inputs(2);
        }
    }
    int result = 0;
    ASSERT_EQ(ConstFoldingCompare(&INS(2)), true);
    auto inst = GetGraph()->FindConstant(DataType::INT64, result);
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), inst);
    GraphChecker(GetGraph()).Check();
}

TEST_F(ConstFoldingTest, Constant32CompareATest)
{
    auto graph = CreateEmptyBytecodeGraph();
    GRAPH(graph)
    {
        CONSTANT(0, 3);
        CONSTANT(1, 3);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Compare).b().SrcType(DataType::Type::INT32).CC(CC_A).Inputs(0, 1);
            INST(3, Opcode::Return).b().Inputs(2);
        }
    }
    int result = 0;
    ASSERT_EQ(ConstFoldingCompare(&INS(2)), true);
    auto inst = graph->FindConstant(DataType::INT32, result);
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), inst);
    GraphChecker(graph).Check();
}

TEST_F(ConstFoldingTest, CompareAETest)
{
    GRAPH(GetGraph())
    {
        CONSTANT(0, 3);
        CONSTANT(1, 3);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Compare).b().SrcType(DataType::Type::INT64).CC(CC_AE).Inputs(0, 1);
            INST(3, Opcode::Return).b().Inputs(2);
        }
    }
    int result = 1;
    ASSERT_EQ(ConstFoldingCompare(&INS(2)), true);
    auto inst = GetGraph()->FindConstant(DataType::INT64, result);
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), inst);
    GraphChecker(GetGraph()).Check();
}

TEST_F(ConstFoldingTest, Constant32CompareAETest)
{
    auto graph = CreateEmptyBytecodeGraph();
    GRAPH(graph)
    {
        CONSTANT(0, 3);
        CONSTANT(1, 3);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Compare).b().SrcType(DataType::Type::INT32).CC(CC_AE).Inputs(0, 1);
            INST(3, Opcode::Return).b().Inputs(2);
        }
    }
    int result = 1;
    ASSERT_EQ(ConstFoldingCompare(&INS(2)), true);
    auto inst = graph->FindConstant(DataType::INT32, result);
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), inst);
    GraphChecker(graph).Check();
}

TEST_F(ConstFoldingTest, CompareZeroWithNullPtr)
{
    for (auto cc : {CC_EQ, CC_NE}) {
        auto graph = CreateEmptyGraph();
        GRAPH(graph)
        {
            CONSTANT(0, 0);
            CONSTANT(1, nullptr);
            BASIC_BLOCK(2, 1)
            {
                INST(2, Opcode::Compare).b().CC(cc).Inputs(0, 1);
                INST(3, Opcode::Return).b().Inputs(2);
            }
        }
        ASSERT_TRUE(ConstFoldingCompare(&INS(2)));
        ASSERT_TRUE(graph->RunPass<Cleanup>());
        auto exp_graph = CreateEmptyGraph();
        GRAPH(exp_graph)
        {
            CONSTANT(0, (cc == CC_EQ ? 1 : 0));
            BASIC_BLOCK(2, 1)
            {
                INST(3, Opcode::Return).b().Inputs(0);
            }
        }
        ASSERT_TRUE(GraphComparator().Compare(graph, exp_graph));
    }
}

TEST_F(ConstFoldingTest, CompareTstEqTest)
{
    GRAPH(GetGraph())
    {
        CONSTANT(0, 1);
        CONSTANT(1, 2);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Compare).b().SrcType(DataType::Type::INT64).CC(CC_TST_EQ).Inputs(0, 1);
            INST(3, Opcode::Return).b().Inputs(2);
        }
    }
    int result = 1;
    ASSERT_EQ(ConstFoldingCompare(&INS(2)), true);
    auto inst = GetGraph()->FindConstant(DataType::INT64, result);
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), inst);
    GraphChecker(GetGraph()).Check();
}

TEST_F(ConstFoldingTest, CompareTstEqTest1)
{
    GRAPH(GetGraph())
    {
        CONSTANT(0, 3);
        CONSTANT(1, 2);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Compare).b().SrcType(DataType::Type::INT64).CC(CC_TST_EQ).Inputs(0, 1);
            INST(3, Opcode::Return).b().Inputs(2);
        }
    }
    int result = 0;
    ASSERT_EQ(ConstFoldingCompare(&INS(2)), true);
    auto inst = GetGraph()->FindConstant(DataType::INT64, result);
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), inst);
    GraphChecker(GetGraph()).Check();
}

TEST_F(ConstFoldingTest, CompareTstNeTest)
{
    GRAPH(GetGraph())
    {
        CONSTANT(0, 1);
        CONSTANT(1, 2);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Compare).b().SrcType(DataType::Type::INT64).CC(CC_TST_NE).Inputs(0, 1);
            INST(3, Opcode::Return).b().Inputs(2);
        }
    }
    int result = 0;
    ASSERT_EQ(ConstFoldingCompare(&INS(2)), true);
    auto inst = GetGraph()->FindConstant(DataType::INT64, result);
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), inst);
    GraphChecker(GetGraph()).Check();
}

TEST_F(ConstFoldingTest, CompareTstNeTest1)
{
    GRAPH(GetGraph())
    {
        CONSTANT(0, 3);
        CONSTANT(1, 2);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Compare).b().SrcType(DataType::Type::INT64).CC(CC_TST_NE).Inputs(0, 1);
            INST(3, Opcode::Return).b().Inputs(2);
        }
    }
    int result = 1;
    ASSERT_EQ(ConstFoldingCompare(&INS(2)), true);
    auto inst = GetGraph()->FindConstant(DataType::INT64, result);
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), inst);
    GraphChecker(GetGraph()).Check();
}

TEST_F(ConstFoldingTest, CompareEqualInputsTest)
{
    for (int ccInt = CC_LT; ccInt <= CC_AE; ++ccInt) {
        auto cc = static_cast<ConditionCode>(ccInt);
        for (auto type : {DataType::INT32, DataType::INT64, DataType::FLOAT64}) {
            std::optional<bool> result;
            switch (cc) {
                case ConditionCode::CC_EQ:
                case ConditionCode::CC_LE:
                case ConditionCode::CC_GE:
                case ConditionCode::CC_BE:
                case ConditionCode::CC_AE:
                    if (!IsFloatType(type)) {
                        result = true;
                    }
                    break;
                case ConditionCode::CC_NE:
                    if (!IsFloatType(type)) {
                        result = false;
                    }
                    break;
                case ConditionCode::CC_LT:
                case ConditionCode::CC_GT:
                case ConditionCode::CC_B:
                case ConditionCode::CC_A:
                    result = false;
                    break;
                default:
                    UNREACHABLE();
            }
            CheckCompareEqualInputs(type, cc, result);
        }
    }
}

TEST_F(ConstFoldingTest, DivZeroTest)
{
    GRAPH(GetGraph())
    {
        CONSTANT(0, 3);
        CONSTANT(1, 0);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Div).u64().Inputs(0, 1);
            INST(3, Opcode::Return).u64().Inputs(2);
        }
    }
    ASSERT_EQ(ConstFoldingDiv(&INS(2)), false);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), &INS(2));
    GraphChecker(GetGraph()).Check();
}

TEST_F(ConstFoldingTest, Constant32DivZeroTest)
{
    auto graph = CreateEmptyBytecodeGraph();
    GRAPH(graph)
    {
        CONSTANT(0, 3);
        CONSTANT(1, 0);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Div).u32().Inputs(0, 1);
            INST(3, Opcode::Return).u32().Inputs(2);
        }
    }
    ASSERT_EQ(ConstFoldingDiv(&INS(2)), false);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), &INS(2));
    GraphChecker(graph).Check();
}

TEST_F(ConstFoldingTest, ModZeroTest)
{
    GRAPH(GetGraph())
    {
        CONSTANT(0, 3);
        CONSTANT(1, 0);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Mod).u64().Inputs(0, 1);
            INST(3, Opcode::Return).u64().Inputs(2);
        }
    }
    ASSERT_EQ(ConstFoldingMod(&INS(2)), false);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), &INS(2));
    GraphChecker(GetGraph()).Check();
}

TEST_F(ConstFoldingTest, Constant32ModZeroTest)
{
    auto graph = CreateEmptyBytecodeGraph();
    GRAPH(graph)
    {
        CONSTANT(0, 3);
        CONSTANT(1, 0);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Mod).u32().Inputs(0, 1);
            INST(3, Opcode::Return).u32().Inputs(2);
        }
    }
    ASSERT_EQ(ConstFoldingMod(&INS(2)), false);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), &INS(2));
    GraphChecker(graph).Check();
}

TEST_F(ConstFoldingTest, MulIntZeroTest)
{
    GRAPH(GetGraph())
    {
        PARAMETER(0, 25).u64();
        CONSTANT(1, 0);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Mul).u64().Inputs(0, 1);
            INST(3, Opcode::Return).u64().Inputs(2);
        }
    }
    ASSERT_EQ(ConstFoldingMul(&INS(2)), true);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), &INS(1));
    GraphChecker(GetGraph()).Check();
}

TEST_F(ConstFoldingTest, Constant32MulIntZeroTest)
{
    auto graph = CreateEmptyBytecodeGraph();
    GRAPH(graph)
    {
        PARAMETER(0, 25).u32();
        CONSTANT(1, 0);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Mul).u32().Inputs(0, 1);
            INST(3, Opcode::Return).u32().Inputs(2);
        }
    }
    ASSERT_EQ(ConstFoldingMul(&INS(2)), true);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), &INS(1));
    GraphChecker(graph).Check();
}

TEST_F(ConstFoldingTest, MulFloatZeroTest)
{
    GRAPH(GetGraph())
    {
        CONSTANT(0, static_cast<float>3.0);
        CONSTANT(1, static_cast<float>0.0);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Mul).f32().Inputs(0, 1);
            INST(3, Opcode::Return).f32().Inputs(2);
        }
    }
    ASSERT_EQ(ConstFoldingMul(&INS(2)), true);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), &INS(1));
    GraphChecker(GetGraph()).Check();
}

TEST_F(ConstFoldingTest, MulDoubleZeroTest)
{
    GRAPH(GetGraph())
    {
        CONSTANT(0, 3.0);
        CONSTANT(1, 0.0);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Mul).f64().Inputs(0, 1);
            INST(3, Opcode::Return).f64().Inputs(2);
        }
    }
    ASSERT_EQ(ConstFoldingMul(&INS(2)), true);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), &INS(1));
    GraphChecker(GetGraph()).Check();
}

TEST_F(ConstFoldingTest, AndZeroTest)
{
    GRAPH(GetGraph())
    {
        PARAMETER(0, 25).u64();
        CONSTANT(1, 0);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::And).u64().Inputs(0, 1);
            INST(3, Opcode::Return).u64().Inputs(2);
        }
    }
    ASSERT_EQ(ConstFoldingAnd(&INS(2)), true);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), &INS(1));
    GraphChecker(GetGraph()).Check();
}

TEST_F(ConstFoldingTest, Constant32AndZeroTest)
{
    auto graph = CreateEmptyBytecodeGraph();
    GRAPH(graph)
    {
        PARAMETER(0, 25).u32();
        CONSTANT(1, 0);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::And).u32().Inputs(0, 1);
            INST(3, Opcode::Return).u32().Inputs(2);
        }
    }
    ASSERT_EQ(ConstFoldingAnd(&INS(2)), true);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), &INS(1));
    GraphChecker(graph).Check();
}

TEST_F(ConstFoldingTest, OrMinusOneTest)
{
    GRAPH(GetGraph())
    {
        PARAMETER(0, 25).u64();
        CONSTANT(1, -1);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Or).u64().Inputs(0, 1);
            INST(3, Opcode::Return).u64().Inputs(2);
        }
    }
    ASSERT_EQ(ConstFoldingOr(&INS(2)), true);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), &INS(1));
    GraphChecker(GetGraph()).Check();
}

TEST_F(ConstFoldingTest, Constant32OrMinusOneTest)
{
    auto graph = CreateEmptyBytecodeGraph();
    GRAPH(graph)
    {
        PARAMETER(0, 25).u32();
        CONSTANT(1, -1);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Or).u32().Inputs(0, 1);
            INST(3, Opcode::Return).u32().Inputs(2);
        }
    }
    ASSERT_EQ(ConstFoldingOr(&INS(2)), true);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), &INS(1));
    GraphChecker(graph).Check();
}

TEST_F(ConstFoldingTest, XorEqualInputs)
{
    GRAPH(GetGraph())
    {
        PARAMETER(0, 25).u64();
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Xor).u64().Inputs(0, 0);
            INST(3, Opcode::Return).u64().Inputs(2);
        }
    }
    ASSERT_EQ(ConstFoldingXor(&INS(2)), true);
    int result = 0;
    auto inst = GetGraph()->FindConstant(DataType::INT64, result);
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), inst);
    GraphChecker(GetGraph()).Check();
}

TEST_F(ConstFoldingTest, Constant32XorEqualInputs)
{
    auto graph = CreateEmptyBytecodeGraph();
    GRAPH(graph)
    {
        PARAMETER(0, 25).u32();
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Xor).u32().Inputs(0, 0);
            INST(3, Opcode::Return).u32().Inputs(2);
        }
    }
    ASSERT_EQ(ConstFoldingXor(&INS(2)), true);
    int result = 0;
    auto inst = graph->FindConstant(DataType::INT32, result);
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), inst);
    GraphChecker(graph).Check();
}

TEST_F(ConstFoldingTest, ShlBigOffsetTest)
{
    GRAPH(GetGraph())
    {
        PARAMETER(0, 25).u64();
        CONSTANT(1, 20);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Shl).u16().Inputs(0, 1);
            INST(3, Opcode::Return).u16().Inputs(2);
        }
    }
    ASSERT_EQ(ConstFoldingShl(&INS(2)), false);
    ASSERT_TRUE(CheckInputs(INS(2), {0, 1}));
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), &INS(2));
    GraphChecker(GetGraph()).Check();
}

TEST_F(ConstFoldingTest, Constant32ShlBigOffsetTest)
{
    auto graph = CreateEmptyBytecodeGraph();
    GRAPH(graph)
    {
        PARAMETER(0, 25).u32();
        CONSTANT(1, 20);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Shl).u16().Inputs(0, 1);
            INST(3, Opcode::Return).u16().Inputs(2);
        }
    }
    ASSERT_EQ(ConstFoldingShl(&INS(2)), false);
    ASSERT_TRUE(CheckInputs(INS(2), {0, 1}));
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), &INS(2));
    GraphChecker(graph).Check();
}

TEST_F(ConstFoldingTest, ShrBigOffsetTest)
{
    GRAPH(GetGraph())
    {
        PARAMETER(0, 25).u64();
        CONSTANT(1, 20);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Shr).u16().Inputs(0, 1);
            INST(3, Opcode::Return).u16().Inputs(2);
        }
    }
    ASSERT_EQ(ConstFoldingShr(&INS(2)), false);
    ASSERT_TRUE(CheckInputs(INS(2), {0, 1}));
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), &INS(2));
    GraphChecker(GetGraph()).Check();
}

TEST_F(ConstFoldingTest, Constant32ShrBigOffsetTest)
{
    auto graph = CreateEmptyBytecodeGraph();
    GRAPH(graph)
    {
        PARAMETER(0, 25).u32();
        CONSTANT(1, 20);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Shr).u16().Inputs(0, 1);
            INST(3, Opcode::Return).u16().Inputs(2);
        }
    }
    ASSERT_EQ(ConstFoldingShr(&INS(2)), false);
    ASSERT_TRUE(CheckInputs(INS(2), {0, 1}));
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), &INS(2));
    GraphChecker(graph).Check();
}

TEST_F(ConstFoldingTest, CastTest)
{
    uint8_t src_u8 = 0xff;
    CastTest(src_u8, static_cast<int8_t>(src_u8), DataType::INT8);
    CastTest(src_u8, static_cast<int16_t>(src_u8), DataType::INT16);
    CastTest(src_u8, static_cast<uint16_t>(src_u8), DataType::UINT16);

    int8_t src_i8 = -1;
    CastTest(src_i8, static_cast<float>(src_i8), DataType::FLOAT32);

    uint16_t src_u16 = 0xffff;
    CastTest(src_u16, static_cast<int8_t>(src_u16), DataType::INT8);
    CastTest(src_u16, static_cast<double>(src_u16), DataType::FLOAT64);
    CastTest(src_u16, src_u16, DataType::UINT16);

    int64_t src_i64 = -1;
    CastTest(src_i64, static_cast<uint8_t>(src_i64), DataType::UINT8);

    int32_t src_i32 = -1;
    CastTest(src_i32, static_cast<int8_t>(src_i32), DataType::INT8);

    float src_f = 0.25;
    CastTest(src_f, src_f, DataType::FLOAT32);

    double src_d = 0.25;
    CastTest(src_d, src_d, DataType::FLOAT64);

    CastTest(FLT_MAX, static_cast<double>(FLT_MAX), DataType::FLOAT64);
    CastTest(FLT_MIN, static_cast<double>(FLT_MIN), DataType::FLOAT64);

    // TODO (schernykh) : ub test? - convert from double_max to float
    // DBL_MAX, static_cast<float>(DBL_MAX), DataType::FLOAT32
    CastTest(DBL_MIN, static_cast<float>(DBL_MIN), DataType::FLOAT32);

    CastTest(0.0F, static_cast<uint64_t>(0.0F), DataType::UINT64);
    // FLOAT->INT32
    CastTest(FLT_MAX, INT32_MAX, DataType::INT32);
    CastTest(-FLT_MAX, INT32_MIN, DataType::INT32);
    CastTest(nanf(""), static_cast<int32_t>(0), DataType::INT32);
    CastTest(32.0F, static_cast<int32_t>(32.0F), DataType::INT32);
    // FLOAT->INT64
    CastTest(FLT_MAX, INT64_MAX, DataType::INT64);
    CastTest(-FLT_MAX, INT64_MIN, DataType::INT64);
    CastTest(nanf(""), static_cast<int64_t>(0), DataType::INT64);
    CastTest(32.0F, static_cast<int64_t>(32.0F), DataType::INT64);
    // DOUBLE->INT32
    CastTest(DBL_MAX, INT32_MAX, DataType::INT32);
    CastTest(-DBL_MAX, INT32_MIN, DataType::INT32);
    CastTest(nan(""), static_cast<int32_t>(0), DataType::INT32);
    CastTest(64.0, static_cast<int32_t>(64.0), DataType::INT32);
    // DOUBLE->INT64
    CastTest(DBL_MAX, INT64_MAX, DataType::INT64);
    CastTest(-DBL_MAX, INT64_MIN, DataType::INT64);
    CastTest(nan(""), static_cast<int64_t>(0), DataType::INT64);
    CastTest(64.0, static_cast<int64_t>(64.0), DataType::INT64);
}

TEST_F(ConstFoldingTest, CmpTest)
{
    CmpTest(0, 1, -1, DataType::INT32);
    CmpTest(1, 0, 1, DataType::INT32);
    CmpTest(0, 0, 0, DataType::INT32);

    CmpTest(0, -1, -1, DataType::UINT32);
    CmpTest(INT64_MIN, INT64_MAX, -1, DataType::INT64);
    CmpTest(INT64_MAX, INT64_MIN, 1, DataType::INT64);

    CmpTest(0.0F, 1.0F, -1, DataType::FLOAT32);
    CmpTest(1.0F, 0.0F, 1, DataType::FLOAT32);
    CmpTest(0.0F, 0.0F, 0, DataType::FLOAT32);

    CmpTest(0.0, 1.0, -1, DataType::FLOAT64);
    CmpTest(1.0, 0.0, 1, DataType::FLOAT64);
    CmpTest(0.0, 0.0, 0, DataType::FLOAT64);

    CmpTest(std::nan("0"), 1.0, -1, DataType::FLOAT64);
    CmpTest(1.0, std::nan("0"), -1, DataType::FLOAT64);
    CmpTest(std::nan("0"), 1.0, 1, DataType::FLOAT64, true);
    CmpTest(1.0, std::nan("0"), 1, DataType::FLOAT64, true);

    CmpTest(std::nanf("0"), 1.0F, -1, DataType::FLOAT32);
    CmpTest(1.0F, std::nanf("0"), -1, DataType::FLOAT32);
    CmpTest(std::nanf("0"), 1.0F, 1, DataType::FLOAT32, true);
    CmpTest(1.0F, std::nanf("0"), 1, DataType::FLOAT32, true);
}

TEST_F(ConstFoldingTest, CmpEqualInputsIntTest)
{
    GRAPH(GetGraph())
    {
        PARAMETER(0, 25).u64();
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Cmp).i32().Inputs(0, 0);
            INST(3, Opcode::Return).i32().Inputs(2);
        }
    }
    ASSERT_EQ(ConstFoldingCmp(&INS(2)), true);
    int result = 0;
    auto inst = GetGraph()->FindConstant(DataType::INT64, result);
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), inst);
    GraphChecker(GetGraph()).Check();
}

TEST_F(ConstFoldingTest, CmpEqualInputsDoubleTest)
{
    GRAPH(GetGraph())
    {
        PARAMETER(0, 25).f64();
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Cmp).i32().Inputs(0, 0);
            INST(3, Opcode::Return).i32().Inputs(2);
        }
    }
    ASSERT_EQ(ConstFoldingCmp(&INS(2)), false);
}

TEST_F(ConstFoldingTest, SqrtTest)
{
    GRAPH(GetGraph())
    {
        CONSTANT(1, 0.78539816339744828f);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Sqrt).f32().Inputs(1);
            INST(3, Opcode::Return).f32().Inputs(2);
        }
    }
    float result = 0.88622695207595825;
    ASSERT_EQ(ConstFoldingSqrt(&INS(2)), true);
    auto inst = GetGraph()->FindConstant(DataType::FLOAT32, bit_cast<uint32_t, float>(result));
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), inst);
    GraphChecker(GetGraph()).Check();
}

TEST_F(ConstFoldingTest, MaxIntTest1)
{
    GRAPH(GetGraph())
    {
        CONSTANT(0, -1);
        CONSTANT(1, 2);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Max).s64().Inputs(0, 1);
            INST(3, Opcode::Return).s64().Inputs(2);
        }
    }
    int result = 2;
    ASSERT_EQ(ConstFoldingMax(&INS(2)), true);
    auto inst = GetGraph()->FindConstant(DataType::INT64, result);
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), inst);
    GraphChecker(GetGraph()).Check();
}

TEST_F(ConstFoldingTest, MinIntTest1)
{
    GRAPH(GetGraph())
    {
        CONSTANT(0, -1);
        CONSTANT(1, 2);
        BASIC_BLOCK(2, 1)
        {
            INST(2, Opcode::Min).s64().Inputs(0, 1);
            INST(3, Opcode::Return).s64().Inputs(2);
        }
    }
    int result = -1;
    ASSERT_EQ(ConstFoldingMin(&INS(2)), true);
    auto inst = GetGraph()->FindConstant(DataType::INT64, result);
    ASSERT(inst != nullptr);
    ASSERT_EQ(INS(3).GetInput(0).GetInst(), inst);
    GraphChecker(GetGraph()).Check();
}

}  // namespace panda::compiler
