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

#include "libarkbase/utils/utils.h"
#include "common.h"
#include "check_resolver.h"
#include "compiler/optimizer/optimizations/cleanup.h"
#include "compiler/optimizer/optimizations/lowering.h"
#include "codegen.h"
#include "optimize_bytecode.h"
#include "mangling.h"

namespace ark::bytecodeopt::test {
// NOLINTBEGIN(readability-magic-numbers)

TEST_F(AsmTest, TestRenumbering)
{
    auto source = R"(
    .record Y <external>

    .function f64 foo(Y a0) {
        any.ldbyname a0, "bar", 0x1
        any.ldbyname a0, "baz", 0x2
        any.ldbyname a0, "zhcn", 0x0
        ldai.64 0x0
        return.64
    }
    )";

    pandasm::Parser parser;
    auto res = parser.Parse(source);
    ASSERT_TRUE(res);
    auto &program = res.Value();

    ASSERT_TRUE(ParseToGraph(&program, "foo"));

    EXPECT_TRUE(RunOptimizations(GetGraph()));

    std::vector<pandasm::Function::Parameter> params;
    params.emplace_back(pandasm::Type("Y", 0), panda_file::SourceLang::PANDA_ASSEMBLY);
    const auto sigFoo = pandasm::GetFunctionSignatureFromName("foo", params);
    auto &function = program.functionStaticTable.at(sigFoo);

    EXPECT_TRUE(GetGraph()->RunPass<BytecodeGen>(&function, GetIrInterface()));

    std::vector<std::pair<std::string, int64_t>> expected = {{"bar", 0}, {"baz", 1}, {"zhcn", 2}};
    size_t pos = 0;
    for (const auto &ins : function.ins) {
        if (ins.opcode == pandasm::Opcode::ANY_LDBYNAME) {
            EXPECT_EQ(ins.GetID(0), expected[pos].first);

            ASSERT_GT(ins.ImmSize(), 0U);
            EXPECT_EQ(ins.GetImm<int64_t>(ins.ImmSize() - 1), expected[pos].second);
            pos++;
        }
    }

    ASSERT_EQ(pos, 3U);
}

TEST_F(AsmTest, TestRenumberingWithHoles)
{
    auto source = R"(
    .record Y <external>

    .function f64 foo(Y a0) {
        any.ldbyname a0, "bar", 0x1
        any.ldbyname a0, "baz", 0x3
        any.ldbyname a0, "zhcn", 0x8
        ldai.64 0x0
        return.64
    }
    )";

    pandasm::Parser parser;
    auto res = parser.Parse(source);
    ASSERT_TRUE(res);
    auto &program = res.Value();

    ASSERT_TRUE(ParseToGraph(&program, "foo"));

    EXPECT_TRUE(RunOptimizations(GetGraph()));

    std::vector<pandasm::Function::Parameter> params;
    params.emplace_back(pandasm::Type("Y", 0), panda_file::SourceLang::PANDA_ASSEMBLY);
    const auto sigFoo = pandasm::GetFunctionSignatureFromName("foo", params);
    auto &function = program.functionStaticTable.at(sigFoo);
    EXPECT_TRUE(GetGraph()->RunPass<BytecodeGen>(&function, GetIrInterface()));

    std::vector<std::pair<std::string, int64_t>> expected = {{"bar", 0}, {"baz", 1}, {"zhcn", 2}};
    size_t pos = 0;
    for (const auto &ins : function.ins) {
        if (ins.opcode == pandasm::Opcode::ANY_LDBYNAME) {
            EXPECT_EQ(ins.GetID(0), expected[pos].first);

            ASSERT_GT(ins.ImmSize(), 0U);
            EXPECT_EQ(ins.GetImm<int64_t>(ins.ImmSize() - 1), expected[pos].second);
            pos++;
        }
    }

    ASSERT_EQ(pos, 3U);
}

TEST_F(AsmTest, TestRenumberingOverflow)
{
    static constexpr size_t NUMBER_OF_TESTED_ANY = 512;

    std::string body;
    for (size_t i = 0; i < NUMBER_OF_TESTED_ANY; i++) {
        body += "\tany.ldbyname a0, \"f" + std::to_string(i) + "\", 0x0\n";
    }

    std::string source =
        ".record Y <external>\n"
        ".function Y foo(Y a0) {\n" +
        body +
        "\tlda.obj a0\n"
        "\treturn.obj\n"
        "}\n";

    pandasm::Parser parser;
    auto res = parser.Parse(source);
    ASSERT_TRUE(res);
    auto &program = res.Value();
    ASSERT_TRUE(ParseToGraph(&program, "foo"));
    EXPECT_TRUE(RunOptimizations(GetGraph()));

    std::vector<pandasm::Function::Parameter> params;
    params.emplace_back(pandasm::Type("Y", 0), panda_file::SourceLang::PANDA_ASSEMBLY);
    auto sigFoo = pandasm::GetFunctionSignatureFromName("foo", params);
    auto &function = program.functionStaticTable.at(sigFoo);
    EXPECT_TRUE(GetGraph()->RunPass<BytecodeGen>(&function, GetIrInterface()));

    int64_t pos = 0;
    for (const auto &ins : function.ins) {
        if (ins.opcode == pandasm::Opcode::ANY_LDBYNAME) {
            ASSERT_GT(ins.ImmSize(), 0U);
            auto slot = ins.GetImm<int64_t>(ins.ImmSize() - 1);
            if (pos < compiler::INVALID_SLOT_ID) {
                EXPECT_EQ(slot, pos);
            } else {
                EXPECT_EQ(slot, compiler::INVALID_SLOT_ID);
            }

            pos++;
        }
    }

    EXPECT_EQ(pos, NUMBER_OF_TESTED_ANY);
}

TEST_F(AsmTest, TestRenumberingAfterEliminated)
{
    auto source = R"(
    .record Y <external>
    .function Y foo(Y a0) {
        lda.obj a0
        sta.obj v0
        any.ldbyname v0, "live0", 0x0
        movi v1, 0x0
        lda v1
        jeqz skip
        any.ldbyname v0, "dead", 0x0
    skip:
        any.ldbyname v0, "live1", 0x0
        lda.obj a0
        return.obj
    }
    )";

    pandasm::Parser parser;
    auto res = parser.Parse(source);
    ASSERT_TRUE(res);
    auto &program = res.Value();
    ASSERT_TRUE(ParseToGraph(&program, "foo"));
    EXPECT_TRUE(RunOptimizations(GetGraph()));

    std::vector<pandasm::Function::Parameter> params;
    params.emplace_back(pandasm::Type("Y", 0), panda_file::SourceLang::PANDA_ASSEMBLY);
    auto sigFoo = pandasm::GetFunctionSignatureFromName("foo", params);
    auto &function = program.functionStaticTable.at(sigFoo);
    EXPECT_TRUE(GetGraph()->RunPass<BytecodeGen>(&function, GetIrInterface()));

    std::vector<std::pair<std::string, int64_t>> expected = {{"live0", 0}, {"live1", 1}};
    size_t pos = 0;
    for (const auto &ins : function.ins) {
        if (ins.opcode == pandasm::Opcode::ANY_LDBYNAME) {
            EXPECT_EQ(ins.GetID(0), expected[pos].first);

            ASSERT_GT(ins.ImmSize(), 0U);
            EXPECT_EQ(ins.GetImm<int64_t>(ins.ImmSize() - 1), expected[pos].second);
            pos++;
        }
    }

    ASSERT_EQ(pos, 2U);
}

TEST_F(AsmTest, RegEncoderAnyCallRangeArgsSpillFill)
{
    auto source = R"(
    .record Y <external>
    .function Y foo(Y a0) {
        lda.obj a0
        sta.obj v1
        sta.obj v2
        sta.obj v3
        any.call.range a0, v1, 0x3, 0x0
        return.obj
    }
    )";

    pandasm::Parser parser;
    auto res = parser.Parse(source);
    ASSERT_TRUE(res);
    auto &program = res.Value();
    ASSERT_TRUE(ParseToGraph(&program, "foo"));

    EXPECT_TRUE(GetGraph()->RunPass<compiler::RegAllocLinearScan>(compiler::EmptyRegMask()));
    EXPECT_TRUE(GetGraph()->RunPass<RegEncoder>());

    auto expected = CreateEmptyGraph();
    GRAPH(expected)
    {
        PARAMETER(0U, 0U).ref();
        CONSTANT(3U, 0x3U).i64();

        BASIC_BLOCK(2U, -1L)
        {
            INST(1U, Opcode::SaveState).NoVregs();
            INST(5U, Opcode::SpillFill);
            INST(2U, Opcode::Intrinsic)
                .ref()
                .IntrinsicId(compiler::RuntimeInterface::IntrinsicId::INTRINSIC_COMPILER_ANY_CALL_RANGE)
                .Inputs({{compiler::DataType::REFERENCE, 0U},  //
                         {compiler::DataType::INT32, 3U},      //
                         {compiler::DataType::REFERENCE, 0U},  //
                         {compiler::DataType::REFERENCE, 0U},  //
                         {compiler::DataType::REFERENCE, 0U},  //
                         {compiler::DataType::NO_TYPE, 1U}});
            INST(4U, Opcode::Return).ref().Inputs(2U);
        }
    }

    EXPECT_TRUE(GraphComparator().Compare(GetGraph(), expected));
}
// NOLINTEND(readability-magic-numbers)

}  // namespace ark::bytecodeopt::test
