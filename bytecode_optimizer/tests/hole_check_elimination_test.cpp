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

/*
 * The optimizer matches throws/stores ONLY by the frontend-recorded var_id
 * (pandasm::Function::holeCheckVarTable) — there is no IR-derived fallback.
 * These tests build a pandasm::Program, populate that table (mimicking the
 * frontend's per-binding var_id assignment), and run OptimizeBytecode (which
 * builds pc_ins_map and runs HoleCheckElimination with the ir_interface), then
 * count throw.undefinedifholewithname remaining in the function body.
 */
class HoleCheckEliminationTest : public testing::Test {
public:
    static void SetUpTestCase(void) {}
    static void TearDownTestCase(void) {}
    void SetUp() {}
    void TearDown() {}

    /*
     * Assign var_ids to throws and stores by the (level, slot) of the variable
     * they reference. Same (level, slot) -> same var_id. Stores are recorded
     * in storeVarTable only when not fed directly by a hole-producing load.
     */
    static void PopulateTable(pandasm::Function &func, std::unordered_map<uint32_t, uint32_t> &key_to_id,
                              uint32_t &next_id)
    {
        auto id_for = [&](uint32_t level, uint32_t slot) -> uint32_t {
            uint32_t key = (level << 16U) | slot;
            auto [it, inserted] = key_to_id.try_emplace(key, 0U);
            if (inserted) {
                it->second = next_id++;
            }
            return it->second;
        };
        uint32_t curLevel = 0U;
        uint32_t curSlot = 0U;
        auto curKind = pandasm::HoleCheckTarget::LEXICAL;
        const pandasm::Ins *prevIns = nullptr;
        for (auto &insp : func.ins) {
            auto *ins = insp.get();
            auto mn = ins->OpcodeToString();
            UpdateLoadContext(mn, ins, curLevel, curSlot, curKind);
            RecordStoreOrThrow(mn, ins, prevIns, func, id_for, curLevel, curSlot, curKind);
            prevIns = ins;
        }
    }

    static void UpdateLoadContext(const std::string &mn, const pandasm::Ins *ins, uint32_t &curLevel, uint32_t &curSlot,
                                  pandasm::HoleCheckTarget::Kind &curKind)
    {
        const auto &imms = ins->Imms();
        if (mn == "ldlexvar") {
            curLevel = imms.size() > 0U ? static_cast<uint32_t>(std::get<int64_t>(imms[0U])) : 0U;
            curSlot = imms.size() > 1U ? static_cast<uint32_t>(std::get<int64_t>(imms[1U])) : 0U;
            curKind = pandasm::HoleCheckTarget::LEXICAL;
        } else if (mn == "ldlocalmodulevar") {
            curLevel = 0U;
            curSlot = imms.size() > 0U ? static_cast<uint32_t>(std::get<int64_t>(imms[0U])) : 0U;
            curKind = pandasm::HoleCheckTarget::MODULE_LOCAL;
        }
    }

    static void RecordStoreOrThrow(const std::string &mn, const pandasm::Ins *ins, const pandasm::Ins *prevIns,
                                   pandasm::Function &func, const std::function<uint32_t(uint32_t, uint32_t)> &idFor,
                                   uint32_t curLevel, uint32_t curSlot, pandasm::HoleCheckTarget::Kind curKind)
    {
        auto isHoleLoad = [&prevIns]() {
            static const std::unordered_set<std::string> loads = {"ldlexvar",
                                                                  "wide.ldlexvar",
                                                                  "ldlocalmodulevar",
                                                                  "wide.ldlocalmodulevar",
                                                                  "ldexternalmodulevar",
                                                                  "wide.ldexternalmodulevar",
                                                                  "ldhole"};
            return prevIns != nullptr && loads.count(prevIns->OpcodeToString()) != 0U;
        };
        const auto &imms = ins->Imms();
        if (mn == "stlexvar" && !isHoleLoad()) {
            uint32_t l = imms.size() > 0U ? static_cast<uint32_t>(std::get<int64_t>(imms[0U])) : 0U;
            uint32_t s = imms.size() > 1U ? static_cast<uint32_t>(std::get<int64_t>(imms[1U])) : 0U;
            func.storeVarTable[ins] = pandasm::HoleCheckTarget::ForId(pandasm::HoleCheckTarget::LEXICAL, idFor(l, s));
        } else if (mn == "stmodulevar" && !isHoleLoad()) {
            uint32_t s = imms.size() > 0U ? static_cast<uint32_t>(std::get<int64_t>(imms[0U])) : 0U;
            func.storeVarTable[ins] =
                pandasm::HoleCheckTarget::ForId(pandasm::HoleCheckTarget::MODULE_LOCAL, idFor(0U, s));
        } else if (mn == "throw.undefinedifholewithname") {
            func.holeCheckVarTable[ins] = pandasm::HoleCheckTarget::ForId(curKind, idFor(curLevel, curSlot));
        }
    }

    // Populate the hole-check/store tables for ALL functions in a program with
    // a shared var_id space (mimicking the frontend's module-level assignment).
    static void PopulateAllTables(pandasm::Program &program)
    {
        std::unordered_map<uint32_t, uint32_t> key_to_id;
        uint32_t next_id = 1U;
        for (auto &[name, func] : program.function_table) {
            PopulateTable(func, key_to_id, next_id);
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

    // Count throws across ALL functions in a program (used for multi-function
    // seed tests where the eliminated throw lives in a nested function).
    static size_t CountThrowsAll(const pandasm::Program &program)
    {
        size_t c = 0U;
        for (auto &[name, func] : program.function_table) {
            c += CountThrows(func);
        }
        return c;
    }

    /*
     * Run on a single-function program (func_main_0 only). Populates the
     * hole-check/store tables, emits, optimizes, and checks the throw count in
     * func_main_0 before and after.
     */
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
        PopulateAllTables(program);
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

    /*
     * Run on a multi-function program and check the total throw count across
     * all functions (used for cross-function entry-seed tests where the
     * eliminated throw lives in a nested function referenced by definefunc).
     */
    bool RunAll(const std::string &source, size_t expect_before, size_t expect_after)
    {
        pandasm::Parser parser;
        auto res = parser.Parse(source);
        if (!res) {
            EXPECT_TRUE(false) << "parse failed";
            return false;
        }
        auto &program = res.Value();
        PopulateAllTables(program);
        size_t before = CountThrowsAll(program);
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
        size_t after = CountThrowsAll(program);
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
 * @tc.desc: Rule S — a store of a Constant (ldai 0x1) proves the variable
 *           non-hole, so the subsequent read-side throw is eliminated. The
 *           store is recorded in storeVarTable (its preceding instruction is
 *           lda v0, not a hole-producing load), so ApplyTransfer gen's the
 *           var_id at the store.
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
    EXPECT_TRUE(Run(src, 1U, 0U));
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

/*
 * === Local export (module variable) scenarios ===
 * `export let` binds a module variable accessed via stmodulevar /
 * ldlocalmodulevar. The frontend records the same module-level var_id for
 * these throws/stores (HoleCheckTarget::MODULE_LOCAL), so the optimizer
 * treats them identically to lexical bindings — Rule T applies.
 */
/**
 * @tc.name: hole_check_elim_test_005
 * @tc.desc: Module var Rule T — a prior throw for the same module variable
 *           (same var_id, MODULE_LOCAL kind) makes a later throw redundant.
 * @tc.type: FUNC
 * @tc.require: issueNumber
 */
HWTEST_F(HoleCheckEliminationTest, hole_check_elim_test_005, TestSize.Level1)
{
    const std::string src = std::string(HEADER) + R"(
.function any func_main_0(any a0, any a1, any a2) <static> {
    ldlocalmodulevar 0x0
    throw.undefinedifholewithname "x"
    ldlocalmodulevar 0x0
    throw.undefinedifholewithname "x"
    return
}
)";
    EXPECT_TRUE(Run(src, 2U, 1U));
}

/**
 * @tc.name: hole_check_elim_test_006
 * @tc.desc: Module var write-then-read — the write-side throw (before the
 *           reassignment stmodulevar) is kept (no prior proof), but the
 *           read-side throw (after the store) is eliminated by Rule T.
 * @tc.type: FUNC
 * @tc.require: issueNumber
 */
HWTEST_F(HoleCheckEliminationTest, hole_check_elim_test_006, TestSize.Level1)
{
    const std::string src = std::string(HEADER) + R"(
.function any func_main_0(any a0, any a1, any a2) <static> {
    ldlocalmodulevar 0x0
    throw.undefinedifholewithname "x"
    ldai 0x1
    stmodulevar 0x0
    ldlocalmodulevar 0x0
    throw.undefinedifholewithname "x"
    return
}
)";
    EXPECT_TRUE(Run(src, 2U, 1U));
}

/**
 * @tc.name: hole_check_elim_test_007
 * @tc.desc: Module var soundness — two throws for different module variables
 *           (different slot -> different var_id) are both kept.
 * @tc.type: FUNC
 * @tc.require: issueNumber
 */
HWTEST_F(HoleCheckEliminationTest, hole_check_elim_test_007, TestSize.Level1)
{
    const std::string src = std::string(HEADER) + R"(
.function any func_main_0(any a0, any a1, any a2) <static> {
    ldlocalmodulevar 0x0
    throw.undefinedifholewithname "x"
    ldlocalmodulevar 0x1
    throw.undefinedifholewithname "y"
    return
}
)";
    EXPECT_TRUE(Run(src, 2U, 2U));
}

/**
 * @tc.name: hole_check_elim_test_008
 * @tc.desc: Module var declaration store then read (Rule S) — a declaration
 *           stmodulevar (fed by a non-hole producer, e.g. a class/object
 *           creation) in the SAME function as the subsequent read proves the
 *           variable non-hole; the read-side throw is eliminated. This is the
 *           `export class A {}; let b = new A()` pattern: the declaration
 *           store and the read are both in func_main_0, so the entry-seed
 *           mechanism (which only propagates to nested functions) does not
 *           cover it — Rule S in the intra-function transfer function does.
 * @tc.type: FUNC
 * @tc.require: issueNumber
 */
HWTEST_F(HoleCheckEliminationTest, hole_check_elim_test_008, TestSize.Level1)
{
    const std::string src = std::string(HEADER) + R"(
.function any func_main_0(any a0, any a1, any a2) <static> {
    lda.str "A"
    stmodulevar 0x0
    ldlocalmodulevar 0x0
    throw.undefinedifholewithname "A"
    return
}
)";
    EXPECT_TRUE(Run(src, 1U, 0U));
}

/*
 * The cross-function entry-seed scenario for module variables (a stmodulevar
 * in an enclosing function before definefunc seeds the nested function's
 * read-side throw) is validated end-to-end by the disassembler golden fixture
 * hole-check.js::canLocalEntrySeed and its nested @0#inner (the throw is
 * eliminated in the optimized .pa). It is not duplicated here because
 * hand-written pandasm text cannot round-trip definefunc's method_id
 * reference (the # in the ECMAScript mangled name starts a pandasm comment,
 * and a bare name does not resolve to a valid method entity for the
 * optimizer), so the unit test would crash during emission rather than test
 * the optimization.
 *
 * NB: a hand-written pandasm try/catch regression for Rule S's
 * try_begin/try_end intersection invariant cannot run in this harness (the
 * catch-block round-trip through AsmEmitter::Emit crashes in the mixed
 * shared/static linking environment of host unittests). The invariant is
 * covered END-TO-END by assembler/tests/js/cross_module_hole_check/try_catch.js
 * compiled by the real es2abc (whose optimizer runs the same
 * HoleCheckElimination pass) and asserted in
 * CrossModuleHoleCheckE2eTest.e2e_try_catch_rule_s_keeps_throw.
 */
}  // namespace panda::bytecodeopt
