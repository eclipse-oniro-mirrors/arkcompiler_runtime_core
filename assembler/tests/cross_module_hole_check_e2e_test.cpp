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

/*
 * End-to-end tests for the cross-module hole-check elimination on REAL
 * frontend artifacts. NB: this binary is linked fully STATICALLY
 * (abc2program_frontend_static -> libarkassembler_frontend_static -> ...).
 * Do NOT mix it with the shared assembler/pandafile libraries: the shared and
 * static builds keep separate copies of the libraries' globals, and unloading
 * both copies at process exit double-frees (see __cxa_finalize crash).
 */
#include "cross_module_hole_check.h"
#include "abc2program_compiler.h"

#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <vector>

using namespace testing::ext;

namespace panda::pandasm {

namespace {

/*
 * The fixtures under tests/js/cross_module_hole_check/ are compiled by the
 * real es2abc in GN actions in both layouts:
 *  - A.abc/B.abc/SRC.abc (+ AP/BP/SRCP): UNMERGED (single-file) .abc — module
 *    and phase literals hang off the shared _ESModuleRecord /
 *    _ModuleRequestPhaseRecord records (the phase record carries the FIXED
 *    field name `moduleRequestPhaseIdx`). The tests load them through the
 *    merge flow's bytecode loader (Abc2ProgramCompiler) and run the
 *    elimination entry point EmitPrograms uses.
 *  - e2e_lazy_merged.abc / e2e_plain_merged.abc: ONE --merge-abc es2abc
 *    invocation per scenario driven by filesInfo.txt (mirroring
 *    dedup_literal_array) — the REAL multi-module pipeline: EmitMergedAbcJob
 *    merges the per-module programs and EmitPrograms runs the elimination
 *    INSIDE the frontend, so the merged artifact already reflects the result.
 * The scenarios:
 *  - lazy:  A -> B(`import lazy {x} from './SRC.js'; export {x};`) -> SRC,
 *    acyclic chain; the frontend rewrites the re-export into an IMPLICIT
 *    INDIRECT export reusing the lazy request index, so the elimination must
 *    KEEP the throw in A;
 *  - plain: AP -> BP(`import {x} from './SRCP.js'; export {x};`) -> SRCP,
 *    acyclic chain with an eager re-export, so the throw must be REMOVED.
 */
/*
 * Count throw.undefinedifholewithname instructions in every func_main_0 of
 * the program (the entry function where the frontend emits import hole
 * checks). Only the consumer module in each fixture has any throws, so the
 * aggregate is deterministic.
 */
size_t CountThrowsInEntryFunctions(const Program &prog)
{
    size_t c = 0U;
    for (const auto &[key, func] : prog.function_table) {
        if (key.find("func_main_0") == std::string::npos) {
            continue;
        }
        for (const auto &insp : func.ins) {
            if (insp->OpcodeToString() == "throw.undefinedifholewithname") {
                ++c;
            }
        }
    }
    return c;
}

/*
 * Load a real es2abc-generated .abc into a mutable pandasm Program via the
 * SAME loader the frontend's merge flow uses for bytecode inputs
 * (Abc2ProgramCompiler::CompileAbcFile). Returns an empty compiler pointer on
 * failure. The compiler owns the program and must outlive it.
 */
struct LoadedAbc {
    std::unique_ptr<abc2program::Abc2ProgramCompiler> compiler;
    Program *prog = nullptr;
};

LoadedAbc LoadRealAbc(const std::string &path)
{
    LoadedAbc out;
    auto compiler = std::make_unique<abc2program::Abc2ProgramCompiler>();
    if (!compiler->OpenAbcFile(path)) {
        return out;
    }
    out.prog = compiler->CompileAbcFile();
    if (out.prog == nullptr) {
        return LoadedAbc {};
    }
    out.compiler = std::move(compiler);
    return out;
}

/*
 * The unmerged layout must match what CollectPhaseLiteralIds expects: module
 * literals hang off the shared `_ESModuleRecord` keyed by the source path.
 * `_ModuleRequestPhaseRecord` is emitted ONLY when the module has at least
 * one lazy request, and it carries the FIXED field name
 * `moduleRequestPhaseIdx` (NOT the source-file path — see the review comment
 * on the phase-record keying).
 */
void ValidateSingleFileLayout(const Program &prog, bool expectLazy)
{
    bool hasESModuleRecord = prog.record_table.find("_ESModuleRecord") != prog.record_table.end();
    EXPECT_TRUE(hasESModuleRecord) << "fixture: module literal must hang off the shared _ESModuleRecord";
    auto phaseIt = prog.record_table.find("_ModuleRequestPhaseRecord");
    if (!expectLazy) {
        EXPECT_EQ(phaseIt, prog.record_table.end()) << "fixture: no lazy requests -> phase record must be absent";
        return;
    }
    ASSERT_NE(phaseIt, prog.record_table.end());
    bool hasFixedNamePhaseField = false;
    for (const auto &field : phaseIt->second.field_list) {
        if (field.name == "moduleRequestPhaseIdx") {
            hasFixedNamePhaseField = true;
        }
    }
    EXPECT_TRUE(hasFixedNamePhaseField) << "fixture: phase record must use the fixed field name";
}

/*
 * Assert the merge-layout premise of the fixture: module and phase literals
 * hang off each module's own record (moduleRecordIdx /
 * moduleRequestPhaseIdx fields), and — for the lazy scenario — the phase
 * literal marks the request lazy (the implicit indirect re-export reuses the
 * lazy request index).
 */
void ValidateMergeLayoutRecord(const Program &prog, const std::string &recordName, bool expectLazy)
{
    auto recIt = prog.record_table.find(recordName);
    ASSERT_NE(recIt, prog.record_table.end());
    bool hasModuleRecordIdx = false;
    bool hasPhaseIdx = false;
    for (const auto &field : recIt->second.field_list) {
        hasModuleRecordIdx = hasModuleRecordIdx || field.name == "moduleRecordIdx";
        hasPhaseIdx = hasPhaseIdx || field.name == "moduleRequestPhaseIdx";
    }
    EXPECT_TRUE(hasModuleRecordIdx) << "fixture: merge layout must carry moduleRecordIdx on the module record";
    EXPECT_EQ(hasPhaseIdx, expectLazy) << "fixture: phase field exists only when a request is lazy";
}

}  // namespace

class CrossModuleHoleCheckE2eTest : public testing::Test {
public:
    static void SetUpTestCase() {}
    static void TearDownTestCase() {}
    void SetUp() {}
    void TearDown() {}
};

HWTEST_F(CrossModuleHoleCheckE2eTest, e2e_single_file_lazy_indirect_reexport_keeps_throw, TestSize.Level1)
{
    auto consumer = LoadRealAbc(GRAPH_TEST_ABC_DIR "A.abc");
    auto reexporter = LoadRealAbc(GRAPH_TEST_ABC_DIR "B.abc");
    auto source = LoadRealAbc(GRAPH_TEST_ABC_DIR "SRC.abc");
    ASSERT_NE(consumer.prog, nullptr);
    ASSERT_NE(reexporter.prog, nullptr);
    ASSERT_NE(source.prog, nullptr);

    ValidateSingleFileLayout(*reexporter.prog, true);
    ASSERT_EQ(CountThrowsInEntryFunctions(*consumer.prog), 1U) << "fixture: import hole check must be emitted";

    // The entry point AsmEmitter::EmitPrograms invokes on the merge flow.
    std::vector<Program *> progs {consumer.prog, reexporter.prog, source.prog};
    EliminateCrossModuleHoleChecks(progs);

    EXPECT_EQ(CountThrowsInEntryFunctions(*consumer.prog), 1U) << "unmerged: lazy INDIRECT re-export -> throw kept";
}

HWTEST_F(CrossModuleHoleCheckE2eTest, e2e_single_file_plain_reexport_removes_throw, TestSize.Level1)
{
    auto consumer = LoadRealAbc(GRAPH_TEST_ABC_DIR "AP.abc");
    auto reexporter = LoadRealAbc(GRAPH_TEST_ABC_DIR "BP.abc");
    auto source = LoadRealAbc(GRAPH_TEST_ABC_DIR "SRCP.abc");
    ASSERT_NE(consumer.prog, nullptr);
    ASSERT_NE(reexporter.prog, nullptr);
    ASSERT_NE(source.prog, nullptr);

    ValidateSingleFileLayout(*reexporter.prog, false);
    ASSERT_EQ(CountThrowsInEntryFunctions(*consumer.prog), 1U) << "fixture: import hole check must be emitted";

    std::vector<Program *> progs {consumer.prog, reexporter.prog, source.prog};
    EliminateCrossModuleHoleChecks(progs);

    EXPECT_EQ(CountThrowsInEntryFunctions(*consumer.prog), 0U) << "unmerged: eager re-export -> throw removed";
}

HWTEST_F(CrossModuleHoleCheckE2eTest, e2e_merge_abc_lazy_indirect_reexport_keeps_throw, TestSize.Level1)
{
    /*
     * The artifact was produced by the REAL multi-module merge pipeline
     * (es2abc --input-file-list --merge-abc): per-module programs were merged
     * by EmitMergedAbcJob -> EmitPrograms, which ran the elimination. The
     * fixture validity (frontend emits the throw; record/phase layout) is
     * pinned by ValidateMergeLayoutRecord below and by the unmerged tests on
     * the SAME sources.
     */
    auto merged = LoadRealAbc(GRAPH_TEST_ABC_DIR "e2e_lazy_merged.abc");
    ASSERT_NE(merged.prog, nullptr);

    ValidateMergeLayoutRecord(*merged.prog, "B", true);
    EXPECT_EQ(CountThrowsInEntryFunctions(*merged.prog), 1U) << "merge: lazy INDIRECT re-export -> throw kept";
}

HWTEST_F(CrossModuleHoleCheckE2eTest, e2e_merge_abc_plain_reexport_removes_throw, TestSize.Level1)
{
    auto merged = LoadRealAbc(GRAPH_TEST_ABC_DIR "e2e_plain_merged.abc");
    ASSERT_NE(merged.prog, nullptr);

    ValidateMergeLayoutRecord(*merged.prog, "BP", false);
    EXPECT_EQ(CountThrowsInEntryFunctions(*merged.prog), 0U) << "merge: eager re-export -> throw removed";
}

/*
 * SDK scenario: AS -> BS -> '@ohos:mock_sdk'. ModulePathToRecordName keeps the
 * colon form verbatim (no '/' or '.' to cut at), so the missing SDK node is
 * named "@ohos:mock_sdk" and IsSdkRecord recognizes it — this pins the REAL
 * frontend record-name format (reviewer-requested e2e for the SDK rule).
 * Per the SDK contract, a pre-compiled SDK module never imports back into
 * project source, so the acyclic chain allows the throw to be removed.
 */

HWTEST_F(CrossModuleHoleCheckE2eTest, e2e_single_file_sdk_import_removes_throw, TestSize.Level1)
{
    auto consumer = LoadRealAbc(GRAPH_TEST_ABC_DIR "AS.abc");
    auto importer = LoadRealAbc(GRAPH_TEST_ABC_DIR "BS.abc");
    ASSERT_NE(consumer.prog, nullptr);
    ASSERT_NE(importer.prog, nullptr);

    ASSERT_EQ(CountThrowsInEntryFunctions(*consumer.prog), 1U) << "fixture: import hole check must be emitted";

    std::vector<Program *> progs {consumer.prog, importer.prog};
    EliminateCrossModuleHoleChecks(progs);

    EXPECT_EQ(CountThrowsInEntryFunctions(*consumer.prog), 0U) << "unmerged: missing SDK -> acyclic -> throw removed";
}

HWTEST_F(CrossModuleHoleCheckE2eTest, e2e_merge_abc_sdk_import_removes_throw, TestSize.Level1)
{
    auto merged = LoadRealAbc(GRAPH_TEST_ABC_DIR "e2e_sdk_merged.abc");
    ASSERT_NE(merged.prog, nullptr);

    EXPECT_EQ(CountThrowsInEntryFunctions(*merged.prog), 0U) << "merge: missing SDK record -> acyclic -> throw removed";
}

/*
 * Rule S try/catch regression on a REAL es2abc artifact (reviewer-requested).
 * try_catch.js: `try { x = 1; } catch (e) { y = x; } let x;` — the frontend
 * emits a write-side throw + Rule S store inside the try, and a read-side
 * throw in the catch. Rule S's soundness at the handler relies on the
 * try_begin/try_end predecessor intersection (the store's gen must NOT reach
 * the handler entry), so the OPTIMIZED func_main_0 must keep BOTH throws:
 * the write-side throw (the catch contains a rethrowing check for the same
 * variable) and the catch's read-side throw (x is still hole there).
 */
HWTEST_F(CrossModuleHoleCheckE2eTest, e2e_try_catch_rule_s_keeps_throw, TestSize.Level1)
{
    auto compiled = LoadRealAbc(GRAPH_TEST_ABC_DIR "try_catch.abc");
    ASSERT_NE(compiled.prog, nullptr);

    EXPECT_EQ(CountThrowsInEntryFunctions(*compiled.prog), 2U) << "Rule S: try store must not reach the catch handler";
}

}  // namespace panda::pandasm
