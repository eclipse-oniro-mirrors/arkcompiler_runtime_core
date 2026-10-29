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

#include "hole_check_elimination.h"
#include "assembler/assembly-emitter.h"
#include "assembler/assembly-parser.h"
#include "compiler/optimizer/ir/graph.h"
#include "compiler/optimizer/ir/inst.h"
#include "compiler/optimizer/ir/runtime_interface.h"
#include "graph_test.h"
#include "mem/pool_manager.h"
#include "optimize_bytecode.h"

using namespace testing::ext;

namespace panda::bytecodeopt {

// The optimizer matches throws/stores ONLY by the frontend-recorded var_id
// (pandasm::Function::holeCheckVarTable) — there is no IR-derived fallback.
// These tests build a pandasm::Program, populate that table (mimicking the
// frontend's per-binding var_id assignment), and run OptimizeBytecode (which
// builds pc_ins_map and runs HoleCheckElimination with the ir_interface), then
// count throw.undefinedifholewithname remaining in the function body.
class HoleCheckEliminationTest : public testing::Test {
public:
    static void SetUpTestCase(void) {}
    static void TearDownTestCase(void) {}
    void SetUp() {}
    void TearDown() {}

    // Assign var_ids to throws and stores by the (level, slot) of the variable
    // they reference — for straight-line pandasm (no newlexenv/poplexenv) this is
    // a valid binding key, mimicking what the frontend records. Same (level,
    // slot) -> same var_id.
    static void PopulateTable(pandasm::Function &func)
    {
        std::unordered_map<uint32_t, uint32_t> key_to_id;
        uint32_t next_id = 1U;
        auto id_for = [&](uint32_t level, uint32_t slot) -> uint32_t {
            uint32_t key = (level << 16U) | slot;
            auto [it, inserted] = key_to_id.try_emplace(key, 0U);
            if (inserted) {
                it->second = next_id++;
            }
            return it->second;
        };
        uint32_t cur_level = 0U;
        uint32_t cur_slot = 0U;
        for (auto &insp : func.ins) {
            auto *ins = insp.get();
            auto mn = ins->OpcodeToString();
            if (mn == "ldlexvar") {
                const auto &imms = ins->Imms();
                cur_level = imms.size() > 0U ? static_cast<uint32_t>(std::get<int64_t>(imms[0U])) : 0U;
                cur_slot = imms.size() > 1U ? static_cast<uint32_t>(std::get<int64_t>(imms[1U])) : 0U;
            } else if (mn == "ldlocalmodulevar") {
                const auto &imms = ins->Imms();
                cur_level = 0U;
                cur_slot = imms.size() > 0U ? static_cast<uint32_t>(std::get<int64_t>(imms[0U])) : 0U;
            } else if (mn == "stlexvar") {
                const auto &imms = ins->Imms();
                uint32_t l = imms.size() > 0U ? static_cast<uint32_t>(std::get<int64_t>(imms[0U])) : 0U;
                uint32_t s = imms.size() > 1U ? static_cast<uint32_t>(std::get<int64_t>(imms[1U])) : 0U;
                func.holeCheckVarTable[ins] =
                    pandasm::HoleCheckTarget::ForId(pandasm::HoleCheckTarget::LEXICAL, id_for(l, s));
            } else if (mn == "stmodulevar") {
                const auto &imms = ins->Imms();
                uint32_t s = imms.size() > 0U ? static_cast<uint32_t>(std::get<int64_t>(imms[0U])) : 0U;
                func.holeCheckVarTable[ins] = pandasm::HoleCheckTarget::ForId(
                    pandasm::HoleCheckTarget::MODULE_LOCAL, id_for(0U, s));
            } else if (mn == "throw.undefinedifholewithname") {
                func.holeCheckVarTable[ins] = pandasm::HoleCheckTarget::ForId(
                    pandasm::HoleCheckTarget::LEXICAL, id_for(cur_level, cur_slot));
            }
        }
    }

    static size_t CountThrows(const pandasm::Function &func)
    {
        size_t c = 0U;
        for (auto &insp : func.ins) {
            if (insp->OpcodeToString() == "throw.undefinedifholewithname") {
                ++c;
            }
        }
        return c;
    }

    bool Run(const std::string &source, size_t expect_before, size_t expect_after)
    {
        pandasm::Parser parser;
        auto res = parser.Parse(source);
        if (!res) {
            EXPECT_TRUE(false) << "parse failed";
            return false;
        }
        auto &program = res.Value();
        const std::string func_name = "func_main_0:(any,any,any)";
        auto it = program.function_table.find(func_name);
        EXPECT_NE(it, program.function_table.end());
        if (it == program.function_table.end()) {
            return false;
        }
        PopulateTable(it->second);
        size_t before = CountThrows(it->second);
        EXPECT_EQ(before, expect_before) << "before";

        const std::string abc = "holeCheckElim_test.abc";
        pandasm::AsmEmitter::PandaFileToPandaAsmMaps maps {};
        if (!pandasm::AsmEmitter::Emit(abc, program, nullptr, &maps, false)) {
            EXPECT_TRUE(false) << "emit failed";
            return false;
        }
        if (!panda::bytecodeopt::OptimizeBytecode(&program, &maps, abc, true)) {
            EXPECT_TRUE(false) << "optimize failed";
            return false;
        }
        size_t after = CountThrows(it->second);
        EXPECT_EQ(after, expect_after) << "after";
        return true;
    }
};

static const char *HEADER = R"(
.language ECMAScript
.record Asm {
}
)";

/**
 * @tc.name: hole_check_elim_test_001
 * @tc.desc: Store without a preceding write-side throw (hand-written pandasm;
 *           real frontend code always emits a write-side throw before stores).
 *           Without a prior throw, Rule T cannot gen -> throw kept.
 * @tc.type: FUNC
 * @tc.require: issueNumber
 */
HWTEST_F(HoleCheckEliminationTest, hole_check_elim_test_001, TestSize.Level1)
{
    const std::string src = std::string(HEADER) + R"(
.function any func_main_0(any a0, any a1, any a2) <static> {
    ldai 0x1
    sta v0
    lda v0
    stlexvar 0x0, 0x0
    ldlexvar 0x0, 0x0
    throw.undefinedifholewithname "x"
    return
}
)";
    EXPECT_TRUE(Run(src, 1U, 1U));
}

/**
 * @tc.name: hole_check_elim_test_002
 * @tc.desc: Rule T — a prior throw for the same variable (same var_id) makes a
 *           later throw redundant.
 * @tc.type: FUNC
 * @tc.require: issueNumber
 */
HWTEST_F(HoleCheckEliminationTest, hole_check_elim_test_002, TestSize.Level1)
{
    const std::string src = std::string(HEADER) + R"(
.function any func_main_0(any a0, any a1, any a2) <static> {
    ldlexvar 0x0, 0x0
    throw.undefinedifholewithname "x"
    ldlexvar 0x0, 0x0
    throw.undefinedifholewithname "x"
    return
}
)";
    EXPECT_TRUE(Run(src, 2U, 1U));
}

/**
 * @tc.name: hole_check_elim_test_003
 * @tc.desc: Store without a preceding write-side throw (hand-written pandasm;
 *           real frontend code always emits a write-side throw before stores).
 *           Without a prior throw, Rule T cannot gen -> throw kept.
 * @tc.type: FUNC
 * @tc.require: issueNumber
 */
HWTEST_F(HoleCheckEliminationTest, hole_check_elim_test_003, TestSize.Level1)
{
    const std::string src = std::string(HEADER) + R"(
.function any func_main_0(any a0, any a1, any a2) <static> {
    ldlexvar 0x1, 0x0
    stlexvar 0x0, 0x0
    ldlexvar 0x0, 0x0
    throw.undefinedifholewithname "x"
    return
}
)";
    EXPECT_TRUE(Run(src, 1U, 1U));
}

/**
 * @tc.name: hole_check_elim_test_004
 * @tc.desc: Soundness — two throws for different variables (different var_id)
 *           are both kept.
 * @tc.type: FUNC
 * @tc.require: issueNumber
 */
HWTEST_F(HoleCheckEliminationTest, hole_check_elim_test_004, TestSize.Level1)
{
    const std::string src = std::string(HEADER) + R"(
.function any func_main_0(any a0, any a1, any a2) <static> {
    ldlexvar 0x0, 0x0
    throw.undefinedifholewithname "x"
    ldlexvar 0x0, 0x1
    throw.undefinedifholewithname "y"
    return
}
)";
    EXPECT_TRUE(Run(src, 2U, 2U));
}

}  // namespace panda::bytecodeopt
