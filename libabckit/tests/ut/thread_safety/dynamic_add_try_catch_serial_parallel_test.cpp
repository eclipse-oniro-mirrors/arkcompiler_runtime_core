/**
 * Copyright (c) 2024-2026 Huawei Device Co., Ltd.
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

// 20260807927972 Task 02-05: add_try_catch scenario — serial vs 4-worker-parallel equivalence.
//
// Four workers each wrap a DIFFERENT function's (run1..run4) body in a try-catch on the SAME
// shared file. The transform mirrors scenarios/add_try_catch: split the entry block into a
// try-begin, split before the return into a try-end, add a catch block that prints the caught
// error, rewrite the epilogue phi so the caught path returns false, and insert the try-catch edges.
//
// As with 02-04, the concurrent phase exercises the D1 single-builder snapshot protocol
// (createGraphFromFunction) and the D4 prepare/commit two-phase writeback (functionSetGraph)
// under the default needOptimize=false. getLastError() is process-global (not thread-local), so
// workers assert only on returned pointers (nullptr => failure); the post-join output comparison
// is the reliable correctness check. Output is fully deterministic (no timestamps), so the
// normalized serial/parallel equality is in fact an exact-byte equality.

#include "libabckit/c/abckit.h"
#include "libabckit/c/ir_core.h"
#include "libabckit/c/metadata_core.h"
#include "libabckit/c/statuses.h"
#include "libabckit/c/isa/isa_dynamic.h"

#include "helpers/helpers.h"
#include "helpers/helpers_runtime.h"

#include <gtest/gtest.h>
#include <array>
#include <atomic>
#include <cstring>
#include <regex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

// NOLINTBEGIN(readability-magic-numbers)

namespace libabckit::test {

namespace {
auto g_impl = AbckitGetApiImpl(ABCKIT_VERSION_RELEASE_1_0_0);
auto g_implI = AbckitGetInspectApiImpl(ABCKIT_VERSION_RELEASE_1_0_0);
auto g_implM = AbckitGetModifyApiImpl(ABCKIT_VERSION_RELEASE_1_0_0);
auto g_implG = AbckitGetGraphApiImpl(ABCKIT_VERSION_RELEASE_1_0_0);
auto g_dynG = AbckitGetIsaApiDynamicImpl(ABCKIT_VERSION_RELEASE_1_0_0);

constexpr auto INPUT_PATH =
    ABCKIT_ABC_DIR "ut/thread_safety/add_try_catch_parallel/add_try_catch_parallel_dynamic.abc";
constexpr auto SERIAL_OUT_PATH =
    ABCKIT_ABC_DIR "ut/thread_safety/add_try_catch_parallel/add_try_catch_parallel_dynamic_serial.abc";
constexpr auto PARALLEL_OUT_PATH =
    ABCKIT_ABC_DIR "ut/thread_safety/add_try_catch_parallel/add_try_catch_parallel_dynamic_parallel.abc";
constexpr auto ENTRY = "add_try_catch_parallel_dynamic";

struct UserData {
    AbckitString *print = nullptr;
};

// Thread-safe variant of the add_try_catch TransformIr (mirrors scenarios/add_try_catch, but
// checks returned pointers instead of the process-global getLastError).
static bool TransformIrChecked(AbckitGraph *graph, UserData *ud)
{
    AbckitBasicBlock *startBB = g_implG->gGetStartBasicBlock(graph);
    if (startBB == nullptr) {
        return false;
    }
    AbckitBasicBlock *endBB = g_implG->gGetEndBasicBlock(graph);
    if (endBB == nullptr) {
        return false;
    }
    std::vector<AbckitBasicBlock *> succBBs = helpers::BBgetSuccBlocks(startBB);
    if (succBBs.empty()) {
        return false;
    }
    AbckitBasicBlock *bb = succBBs[0];
    AbckitInst *initInst = g_implG->bbGetFirstInst(bb);
    if (initInst == nullptr) {
        return false;
    }
    AbckitInst *lastInst = g_implG->bbGetLastInst(bb);
    if (lastInst == nullptr) {
        return false;
    }
    AbckitInst *prevRetInst = g_implG->iGetPrev(lastInst);
    if (prevRetInst == nullptr) {
        return false;
    }

    AbckitBasicBlock *tryBegin =
        g_implG->bbSplitBlockAfterInstruction(g_implG->iGetBasicBlock(initInst), initInst, true);
    if (tryBegin == nullptr) {
        return false;
    }
    AbckitBasicBlock *tryEnd =
        g_implG->bbSplitBlockAfterInstruction(g_implG->iGetBasicBlock(prevRetInst), prevRetInst, true);
    if (tryEnd == nullptr) {
        return false;
    }

    // Catch block: catchPhi (the caught error) + print(it).
    AbckitBasicBlock *catchBlock = g_implG->bbCreateEmpty(graph);
    if (catchBlock == nullptr) {
        return false;
    }
    AbckitInst *catchPhi = g_implG->bbCreateCatchPhi(catchBlock, 0);
    if (catchPhi == nullptr) {
        return false;
    }
    AbckitInst *print = g_dynG->iCreateTryldglobalbyname(graph, ud->print);
    if (print == nullptr) {
        return false;
    }
    g_implG->bbAddInstBack(catchBlock, print);
    AbckitInst *callArg = g_dynG->iCreateCallarg1(graph, print, catchPhi);
    if (callArg == nullptr) {
        return false;
    }
    g_implG->bbAddInstBack(catchBlock, callArg);

    // Epilogue: merge the normal path's value with false on the caught path.
    std::vector<AbckitBasicBlock *> predBBs = helpers::BBgetPredBlocks(endBB);
    if (predBBs.empty()) {
        return false;
    }
    AbckitBasicBlock *epilogueBB = predBBs[0];
    AbckitInst *retInst = g_implG->bbGetLastInst(epilogueBB);
    if (retInst == nullptr) {
        return false;
    }
    AbckitInst *firstPhiInput = g_implG->iGetInput(retInst, 0);
    if (firstPhiInput == nullptr) {
        return false;
    }
    AbckitInst *secondPhiInput = g_dynG->iCreateLdfalse(graph);
    if (secondPhiInput == nullptr) {
        return false;
    }
    g_implG->bbAddInstBack(bb, secondPhiInput);
    AbckitInst *phiInst = g_implG->bbCreatePhi(epilogueBB, 2, firstPhiInput, secondPhiInput);
    if (phiInst == nullptr) {
        return false;
    }
    g_implG->iSetInput(retInst, phiInst, 0);
    g_implG->gInsertTryCatch(tryBegin, tryEnd, catchBlock, catchBlock);
    return true;
}

static UserData BuildUserData(AbckitFile *file)
{
    UserData ud {};
    ud.print = g_implM->createString(file, "print", std::strlen("print"));
    return ud;
}

// Expected 4-block output (run1..4 in main's call order). Each runN: THROW (from throwableApi),
// the caught Error printed by the catch, then false (runN returns false on the caught path, main
// prints it). Fully deterministic.
static std::string BuildExpectedOutput()
{
    return "(THROW\nError: DUMMY_ERROR\nfalse\n){4}";
}

// No nondeterministic number lines in this scenario; normalize is a no-op, so the equality check
// is an exact-byte comparison. Kept for pattern uniformity with 02-04.
static std::string NormalizeNumberLines(const std::string &s)
{
    const std::regex numRe("-?\\d+");
    std::string out;
    out.reserve(s.size());
    std::istringstream iss(s);
    std::string line;
    while (std::getline(iss, line)) {
        std::smatch m;
        out += std::regex_match(line, m, numRe) ? "N" : line;
        out += '\n';
    }
    return out;
}
}  // namespace

class LibAbcKitDynamicScenarioParallelTest : public ::testing::Test {};

// CC-OFFNXT(huge_method, C_RULE_ID_FUNCTION_SIZE) test, solid logic
// Test: test-kind=scenario, abc-kind=ArkTS1, category=positive, extension=c
TEST_F(LibAbcKitDynamicScenarioParallelTest, LibAbcKitTestDynamicAddTryCatchSerialParallel)
{
    // Baseline: the unmodified abc throws out of run1 and dies after printing THROW once.
    auto base = helpers::ExecuteDynamicAbc(INPUT_PATH, ENTRY);
    EXPECT_TRUE(helpers::Match(base, "THROW\n"));

    const std::string expected = BuildExpectedOutput();

    // ---- SERIAL baseline: wrap all four functions sequentially on one file ----
    {
        AbckitFile *file = g_impl->openAbc(INPUT_PATH, std::strlen(INPUT_PATH));
        ASSERT_NE(file, nullptr);
        std::array<UserData, 4> uds;
        std::array<AbckitCoreFunction *, 4> fns {};
        std::array<AbckitGraph *, 4> graphs {};
        for (int i = 0; i < 4; ++i) {
            uds[i] = BuildUserData(file);
            fns[i] = helpers::FindMethodByName(file, "run" + std::to_string(i + 1));
            ASSERT_NE(fns[i], nullptr);
            graphs[i] = g_implI->createGraphFromFunction(fns[i]);
            ASSERT_NE(graphs[i], nullptr);
            ASSERT_TRUE(TransformIrChecked(graphs[i], &uds[i]));
            g_implM->functionSetGraph(fns[i], graphs[i]);
        }
        g_impl->writeAbc(file, SERIAL_OUT_PATH, std::strlen(SERIAL_OUT_PATH));
        for (int i = 0; i < 4; ++i) {
            g_impl->destroyGraph(graphs[i]);
        }
        g_impl->closeFile(file);
    }
    auto serialOut = helpers::ExecuteDynamicAbc(SERIAL_OUT_PATH, ENTRY);
    EXPECT_TRUE(helpers::Match(serialOut, expected));

    // ---- PARALLEL: 4 workers, each a different function on the same shared file ----
    {
        AbckitFile *file = g_impl->openAbc(INPUT_PATH, std::strlen(INPUT_PATH));
        ASSERT_NE(file, nullptr);
        std::array<UserData, 4> uds;
        std::array<AbckitCoreFunction *, 4> fns {};
        std::array<AbckitGraph *, 4> graphs {};
        for (int i = 0; i < 4; ++i) {
            uds[i] = BuildUserData(file);
            fns[i] = helpers::FindMethodByName(file, "run" + std::to_string(i + 1));
            ASSERT_NE(fns[i], nullptr);
        }
        std::atomic<bool> failed {false};
        std::vector<std::thread> threads;
        threads.reserve(4);
        for (int i = 0; i < 4; ++i) {
            threads.emplace_back([&fns, &graphs, &uds, i, &failed]() {
                AbckitGraph *g = g_implI->createGraphFromFunction(fns[i]);
                if (g == nullptr) {
                    failed.store(true);
                    return;
                }
                if (!TransformIrChecked(g, &uds[i])) {
                    failed.store(true);
                    g_impl->destroyGraph(g);
                    return;
                }
                g_implM->functionSetGraph(fns[i], g);
                graphs[i] = g;
            });
        }
        for (auto &t : threads) {
            t.join();
        }
        ASSERT_FALSE(failed.load()) << "parallel add_try_catch transform reported a failure";
        g_impl->writeAbc(file, PARALLEL_OUT_PATH, std::strlen(PARALLEL_OUT_PATH));
        for (int i = 0; i < 4; ++i) {
            if (graphs[i] != nullptr) {
                g_impl->destroyGraph(graphs[i]);
            }
        }
        g_impl->closeFile(file);
    }
    auto parallelOut = helpers::ExecuteDynamicAbc(PARALLEL_OUT_PATH, ENTRY);
    EXPECT_TRUE(helpers::Match(parallelOut, expected));

    // Exact-byte equality (output is deterministic).
    EXPECT_EQ(NormalizeNumberLines(serialOut), NormalizeNumberLines(parallelOut));
}

}  // namespace libabckit::test

// NOLINTEND(readability-magic-numbers)
