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

#include <algorithm>
#include <iterator>
#include <vector>

#include <gtest/gtest.h>

#include "assembler/assembly-parser.h"
#include "compiler/optimizer/ir/basicblock.h"
#include "graph_test.h"
#include "mem/pool_manager.h"
#include "optimize_bytecode.h"

using namespace testing::ext;

namespace panda::bytecodeopt {
using namespace compiler;
using namespace panda::pandasm;

class OptimizeBytecodeTest : public testing::Test {
public:
    static void SetUpTestCase(void) {}
    static void TearDownTestCase(void) {}
    void SetUp() {}
    void TearDown() {}

    static size_t CountOpcode(const std::vector<pandasm::InsPtr> &instructions, pandasm::Opcode opcode)
    {
        return std::count_if(instructions.begin(), instructions.end(),
                             [opcode](const auto &inst) { return inst->GetOpcode() == opcode; });
    }

    void EmitAndOptimize(const std::string &abc_file_name, panda::pandasm::Program &program) const
    {
        panda::pandasm::AsmEmitter::PandaFileToPandaAsmMaps panda_file_to_asm_maps {};
        EXPECT_TRUE(panda::pandasm::AsmEmitter::Emit(abc_file_name, program, nullptr, &panda_file_to_asm_maps, false));
        EXPECT_TRUE(panda::bytecodeopt::OptimizeBytecode(&program, &panda_file_to_asm_maps, abc_file_name, true));
    }
};

/**
 * @tc.name: optimize_bytecode_test_001
 * @tc.desc: Verify the OptimizeBytecode function.
 * @tc.type: FUNC
 * @tc.require: issueNumber
 */
HWTEST_F(OptimizeBytecodeTest, optimize_bytecode_test_001, TestSize.Level1) {
    const auto source = R"(
        .language ECMAScript
        .array array_unsigned_byte u8 3 { 1 2 3 }
        .record Asm4 { i32 asm1 }
        .function any func_main_0(any a0, any a1, any a2) <static> {
            ldundefined
            stglobalvar 0x0, "j"
            ldundefined
            stglobalvar 0x1, "n"
            lda.str "a b c"
            stglobalvar 0x2, "n"
            ldglobalvar 0x3, "n"
            getpropiterator
            sta v0
        jump_label_1:
            getnextpropname v0
            sta v1
            ldundefined
            eq 0x4, v1
            jnez jump_label_0
            lda v1
            stglobalvar 0x5, "j"
            tryldglobalbyname 0x6, "console"
            sta v1
            ldobjbyname 0x7, "log"
            sta v2
            ldglobalvar 0x9, "n"
            sta v3
            ldglobalvar 0xa, "j"
            ldobjbyvalue 0xb, v3
            sta v3
            lda v2
            callthis1 0xd, v1, v3
            jmp jump_label_1
        jump_label_0:
            ldundefined
            returnundefined
        }
    )";

    panda::pandasm::Parser parser;
    auto res = parser.Parse(source);
    auto &program = res.Value();
    const std::string fun_name = "func_main_0:(any,any,any)";
    auto it = program.function_table.find(fun_name);
    EXPECT_NE(it, program.function_table.end());
    EmitAndOptimize("codegenTryCatch.abc", program);
}

/**
 * @tc.name: optimize_bytecode_test_002
 * @tc.desc: Verify the OptimizeBytecode function.
 * @tc.type: FUNC
 * @tc.require: issueNumber
 */
HWTEST_F(OptimizeBytecodeTest, optimize_bytecode_test_002, TestSize.Level1) {
    const auto source = R"(
        .language ECMAScript
        .array array_unsigned_byte u8 3 { 1 2 3 }
        .record Asm4 { i32 asm1 }
        .function any func_main_0(any a0, any a1, any a2) <static> {
            ldundefined
            stglobalvar 0x0, "j"
            ldundefined
            stglobalvar 0x1, "n"
            lda.str "a b c"
            stglobalvar 0x2, "n"
            ldglobalvar 0x3, "n"
            getpropiterator
            sta v0
        jump_label_1:
            getnextpropname v0
            sta v1
            ldundefined
            eq 0x4, v1
            jnez jump_label_0
            lda v1
            stglobalvar 0x5, "j"
            tryldglobalbyname 0x6, "console"
            sta v1
            ldobjbyname 0x7, "log"
            sta v2
            ldglobalvar 0x9, "n"
            sta v3
            ldglobalvar 0xa, "j"
            ldobjbyvalue 0xb, v3
            sta v3
            lda v2
            callthis1 0xd, v1, v3
            jmp jump_label_1
        jump_label_0:
            ldundefined
            returnundefined
        }
    )";
    panda::pandasm::Parser parser;
    auto res = parser.Parse(source);
    auto &program = res.Value();
    const std::string fun_name = "func_main_0:(any,any,any)";
    auto it = program.function_table.find(fun_name);

    EXPECT_NE(it, program.function_table.end());
    const std::string abc_file_name = "codegenTryCatch.abc";

    panda::pandasm::AsmEmitter::PandaFileToPandaAsmMaps panda_file_to_asm_maps {};
    EXPECT_TRUE(panda::pandasm::AsmEmitter::Emit(abc_file_name, program, nullptr, &panda_file_to_asm_maps, false));
}

/**
 * @tc.name: optimize_bytecode_test_003
 * @tc.desc: Verify the OptimizeBytecode function.
 * @tc.type: FUNC
 * @tc.require: issueNumber
 */
HWTEST_F(OptimizeBytecodeTest, optimize_bytecode_test_003, TestSize.Level1) {
    const auto source = R"(
        .language ECMAScript
        .array array_unsigned_byte u8 3 { 1 2 3 }
        .record Asm4 { i32 asm1 }
        .function any func_main_0(any a0, any a1, any a2) <static> {
            ldundefined
            stglobalvar 0x0, "j"
            ldundefined
            stglobalvar 0x1, "n"
            lda.str "a b c"
            stglobalvar 0x2, "n"
            ldglobalvar 0x3, "n"
            getpropiterator
            sta v0
        jump_label_1:
            getnextpropname v0
            sta v1
            ldundefined
            eq 0x4, v1
            jnez jump_label_0
            lda v1
            stglobalvar 0x5, "j"
            tryldglobalbyname 0x6, "console"
            sta v1
            ldobjbyname 0x7, "log"
            sta v2
            ldglobalvar 0x9, "n"
            sta v3
            ldglobalvar 0xa, "j"
            ldobjbyvalue 0xb, v3
            sta v3
            lda v2
            callthis1 0xd, v1, v3
            jmp jump_label_1
        jump_label_0:
            ldundefined
            returnundefined
        }
    )";

    panda::pandasm::Parser parser;
    auto res = parser.Parse(source);
    auto &program = res.Value();
    const std::string fun_name = "func_main_0:(any,any,any)";
    auto it = program.function_table.find(fun_name);

    Function::CatchBlock cat;
    cat.try_end_label = "random";  // random: It's a random string
    it->second.catch_blocks.push_back(cat);
    EXPECT_NE(it, program.function_table.end());

    const std::string abc_file_name = "codegenTryCatch.abc";

    panda::pandasm::AsmEmitter::PandaFileToPandaAsmMaps panda_file_to_asm_maps {};
    EXPECT_TRUE(panda::pandasm::AsmEmitter::Emit(abc_file_name, program, nullptr, &panda_file_to_asm_maps, false));
    EXPECT_TRUE(panda::bytecodeopt::OptimizeBytecode(&program, &panda_file_to_asm_maps, abc_file_name, true));
}

HWTEST_F(OptimizeBytecodeTest, RemovesUnusedSideEffectFreeResults, TestSize.Level1)
{
    const auto source = R"(
        .language ECMAScript
        .function any func_main_0(any a0) <static> {
            lda a0
            istrue
            lda a0
            isfalse
            lda a0
            sta v0
            stricteq 0x0, v0
            lda a0
            strictnoteq 0x0, v0
            lda a0
            eq 0x0, v0
            ldundefined
            returnundefined
        }
    )";

    panda::pandasm::Parser parser;
    auto res = parser.Parse(source);
    ASSERT_TRUE(res.HasValue());
    auto &program = res.Value();
    const std::string fun_name = "func_main_0:(any)";
    auto it = program.function_table.find(fun_name);
    ASSERT_NE(it, program.function_table.end());

    EmitAndOptimize("unusedPureResults.abc", program);

    const auto &instructions = it->second.ins;
    EXPECT_EQ(CountOpcode(instructions, pandasm::Opcode::ISTRUE), 0U);
    EXPECT_EQ(CountOpcode(instructions, pandasm::Opcode::ISFALSE), 0U);
    EXPECT_EQ(CountOpcode(instructions, pandasm::Opcode::STRICTEQ), 0U);
    EXPECT_EQ(CountOpcode(instructions, pandasm::Opcode::STRICTNOTEQ), 0U);
    EXPECT_GT(CountOpcode(instructions, pandasm::Opcode::EQ), 0U);
}

HWTEST_F(OptimizeBytecodeTest, RemovesUnusedRegisterSave, TestSize.Level1)
{
    const auto source = R"(
        .language ECMAScript
        .function any func_main_0() <static> {
            tryldglobalbyname 0x0, "unused"
            sta v0
            ldundefined
            returnundefined
        }
    )";

    panda::pandasm::Parser parser;
    auto res = parser.Parse(source);
    ASSERT_TRUE(res.HasValue());
    auto &program = res.Value();
    const std::string fun_name = "func_main_0:()";
    auto it = program.function_table.find(fun_name);
    ASSERT_NE(it, program.function_table.end());

    EmitAndOptimize("unusedRegisterSave.abc", program);

    const auto &instructions = it->second.ins;
    EXPECT_GT(CountOpcode(instructions, pandasm::Opcode::TRYLDGLOBALBYNAME), 0U);
    EXPECT_EQ(CountOpcode(instructions, pandasm::Opcode::STA), 0U);
}

HWTEST_F(OptimizeBytecodeTest, KeepsUsedSideEffectFreeResults, TestSize.Level1)
{
    const auto source = R"(
        .language ECMAScript
        .function any func_main_0(any a0) <static> {
            lda a0
            istrue
            return
        }
    )";

    panda::pandasm::Parser parser;
    auto res = parser.Parse(source);
    ASSERT_TRUE(res.HasValue());
    auto &program = res.Value();
    const std::string fun_name = "func_main_0:(any)";
    auto it = program.function_table.find(fun_name);
    ASSERT_NE(it, program.function_table.end());

    EmitAndOptimize("usedSideEffectFreeResult.abc", program);

    const auto &instructions = it->second.ins;
    EXPECT_GT(CountOpcode(instructions, pandasm::Opcode::ISTRUE), 0U);
}

HWTEST_F(OptimizeBytecodeTest, FoldsNullishCheckChain, TestSize.Level1)
{
    const auto source = R"(
        .language ECMAScript
        .function any func_main_0(any a0) <static> {
            ldnull
            strictnoteq 0x0, a0
            jeqz nullish
            ldundefined
            strictnoteq 0x1, a0
            jnez nonnull
        nullish:
            ldai 0x0
            return
        nonnull:
            ldai 0x1
            return
        }
    )";

    panda::pandasm::Parser parser;
    auto res = parser.Parse(source);
    ASSERT_TRUE(res.HasValue());
    auto &program = res.Value();
    const std::string fun_name = "func_main_0:(any)";
    auto it = program.function_table.find(fun_name);
    ASSERT_NE(it, program.function_table.end());

    EmitAndOptimize("nullishCheckChain.abc", program);

    const auto &instructions = it->second.ins;
    EXPECT_EQ(CountOpcode(instructions, pandasm::Opcode::NOTEQ), 1);
    EXPECT_EQ(CountOpcode(instructions, pandasm::Opcode::STRICTNOTEQ), 0);
    EXPECT_EQ(CountOpcode(instructions, pandasm::Opcode::LDUNDEFINED), 0);
}

HWTEST_F(OptimizeBytecodeTest, EliminatesRepeatedDynamicStrictEqInPipeline, TestSize.Level1)
{
    const auto source = R"(
        .language ECMAScript
        .function any func_main_0(any a0, any a1) <static> {
            lda a0
            stricteq 0x0, a1
            sta v0
            lda a0
            stricteq 0x1, a1
            sta v1
            lda v1
            istrue
            sta v2
            lda v0
            typeof 0x2
            sta v3
            lda v2
            add2 0x3, v3
            return
        }
    )";

    panda::pandasm::Parser parser;
    auto res = parser.Parse(source);
    ASSERT_TRUE(res.HasValue());
    auto &program = res.Value();
    const std::string fun_name = "func_main_0:(any,any)";
    auto it = program.function_table.find(fun_name);
    ASSERT_NE(it, program.function_table.end());

    EmitAndOptimize("dynamicCsePositive.abc", program);

    const auto &instructions = it->second.ins;
    EXPECT_EQ(CountOpcode(instructions, pandasm::Opcode::STRICTEQ), 1U);
}

HWTEST_F(OptimizeBytecodeTest, KeepsRepeatedDynamicStrictEqWhenProducerIsNotLiveAcrossCurrent, TestSize.Level1)
{
    const auto source = R"(
        .language ECMAScript
        .function any func_main_0(any a0, any a1) <static> {
            lda a0
            stricteq 0x0, a1
            sta v0
            lda v0
            typeof 0x1
            sta v1
            lda a0
            stricteq 0x2, a1
            sta v2
            lda v2
            add2 0x3, v1
            return
        }
    )";

    panda::pandasm::Parser parser;
    auto res = parser.Parse(source);
    ASSERT_TRUE(res.HasValue());
    auto &program = res.Value();
    const std::string fun_name = "func_main_0:(any,any)";
    auto it = program.function_table.find(fun_name);
    ASSERT_NE(it, program.function_table.end());

    EmitAndOptimize("dynamicCseNegative.abc", program);

    const auto &instructions = it->second.ins;
    EXPECT_EQ(CountOpcode(instructions, pandasm::Opcode::STRICTEQ), 2U);
}

HWTEST_F(OptimizeBytecodeTest, EliminatesRepeatedSuperCallStateChecksInPipeline, TestSize.Level1)
{
    const auto source = R"(
        .language ECMAScript
        .function any func_main_0(any a0) <static> {
            lda a0
            throw.ifsupernotcorrectcall 0x0
            sta v0
            lda a0
            throw.ifsupernotcorrectcall 0x0
            return
        }
    )";

    panda::pandasm::Parser parser;
    auto res = parser.Parse(source);
    ASSERT_TRUE(res.HasValue());
    auto &program = res.Value();
    const std::string fun_name = "func_main_0:(any)";
    auto it = program.function_table.find(fun_name);
    ASSERT_NE(it, program.function_table.end());

    auto lda_count_before = CountOpcode(it->second.ins, pandasm::Opcode::LDA);
    auto sta_count_before = CountOpcode(it->second.ins, pandasm::Opcode::STA);

    EmitAndOptimize("redundantSuperCheck.abc", program);

    const auto &instructions = it->second.ins;
    EXPECT_EQ(CountOpcode(instructions, pandasm::Opcode::THROW_IFSUPERNOTCORRECTCALL), 1U);
    EXPECT_LE(CountOpcode(instructions, pandasm::Opcode::LDA), lda_count_before);
    EXPECT_LE(CountOpcode(instructions, pandasm::Opcode::STA), sta_count_before);
}

HWTEST_F(OptimizeBytecodeTest, KeepsSuperChecksAcrossThisInitializationInPipeline, TestSize.Level1)
{
    const auto source = R"(
        .language ECMAScript
        .function any func_main_0(any a0, any a1) <static> {
            lda a0
            sta v0
            lda v0
            throw.ifsupernotcorrectcall 0x1
            lda a1
            sta v0
            lda v0
            throw.ifsupernotcorrectcall 0x0
            return
        }
    )";

    panda::pandasm::Parser parser;
    auto res = parser.Parse(source);
    ASSERT_TRUE(res.HasValue());
    auto &program = res.Value();
    const std::string fun_name = "func_main_0:(any,any)";
    auto it = program.function_table.find(fun_name);
    ASSERT_NE(it, program.function_table.end());

    EmitAndOptimize("superCheckStateTransition.abc", program);

    const auto &instructions = it->second.ins;
    EXPECT_EQ(CountOpcode(instructions, pandasm::Opcode::THROW_IFSUPERNOTCORRECTCALL), 2U);
}

}  // namespace panda::bytecodeopt
