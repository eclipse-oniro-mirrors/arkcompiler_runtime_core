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

// 20260807927972 Task 02-05: add_try_catch, serial versus four workers on distinct functions.
// Each worker owns its graph; committed graphs are destroyed before file persistence.

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

constexpr auto INPUT_PATH = ABCKIT_ABC_DIR "ut/thread_safety/add_try_catch_parallel/add_try_catch_parallel_dynamic.abc";
constexpr auto SERIAL_OUT_PATH =
    ABCKIT_ABC_DIR "ut/thread_safety/add_try_catch_parallel/add_try_catch_parallel_dynamic_serial.abc";
constexpr auto PARALLEL_OUT_PATH =
    ABCKIT_ABC_DIR "ut/thread_safety/add_try_catch_parallel/add_try_catch_parallel_dynamic_parallel.abc";
constexpr auto ENTRY = "add_try_catch_parallel_dynamic";

struct UserData {
    AbckitString *print = nullptr;
};

// Number of independent worker functions (run1..runN) transformed in parallel/serial.
constexpr int K_WORKER_COUNT = 4;

// Resolve the basic blocks and instructions that the try/catch transform targets.
// Returns false (surfaced to the worker) when any required handle is missing.
static bool ResolveTransformTargets(AbckitGraph *graph, AbckitBasicBlock **outEnd, AbckitBasicBlock **outTargetBB,
                                    AbckitInst **outInit, AbckitInst **outPrevRet)
{
    AbckitBasicBlock *startBB = g_implG->gGetStartBasicBlock(graph);
    if (startBB == nullptr) {
        return false;
    }
    *outEnd = g_implG->gGetEndBasicBlock(graph);
    if (*outEnd == nullptr) {
        return false;
    }
    std::vector<AbckitBasicBlock *> succBBs = helpers::BBgetSuccBlocks(startBB);
    if (succBBs.empty()) {
        return false;
    }
    *outTargetBB = succBBs[0];
    *outInit = g_implG->bbGetFirstInst(*outTargetBB);
    if (*outInit == nullptr) {
        return false;
    }
    AbckitInst *lastInst = g_implG->bbGetLastInst(*outTargetBB);
    if (lastInst == nullptr) {
        return false;
    }
    *outPrevRet = g_implG->iGetPrev(lastInst);
    if (*outPrevRet == nullptr) {
        return false;
    }
    return true;
}

// Split the region around init/prevRet into try-begin/try-end boundaries.
static bool SplitTryRegion(AbckitInst *initInst, AbckitInst *prevRetInst, AbckitBasicBlock **outTryBegin,
                           AbckitBasicBlock **outTryEnd)
{
    *outTryBegin = g_implG->bbSplitBlockAfterInstruction(g_implG->iGetBasicBlock(initInst), initInst, true);
    if (*outTryBegin == nullptr) {
        return false;
    }
    *outTryEnd = g_implG->bbSplitBlockAfterInstruction(g_implG->iGetBasicBlock(prevRetInst), prevRetInst, true);
    if (*outTryEnd == nullptr) {
        return false;
    }
    return true;
}

// Catch block: catchPhi (the caught error) + print(it).
static bool BuildCatchBlock(AbckitGraph *graph, UserData *ud, AbckitBasicBlock **outCatchBlock)
{
    *outCatchBlock = g_implG->bbCreateEmpty(graph);
    if (*outCatchBlock == nullptr) {
        return false;
    }
    AbckitInst *catchPhi = g_implG->bbCreateCatchPhi(*outCatchBlock, 0);
    if (catchPhi == nullptr) {
        return false;
    }
    AbckitInst *print = g_dynG->iCreateTryldglobalbyname(graph, ud->print);
    if (print == nullptr) {
        return false;
    }
    g_implG->bbAddInstBack(*outCatchBlock, print);
    AbckitInst *callArg = g_dynG->iCreateCallarg1(graph, print, catchPhi);
    if (callArg == nullptr) {
        return false;
    }
    g_implG->bbAddInstBack(*outCatchBlock, callArg);
    return true;
}

// Epilogue: merge the normal path's value with false on the caught path.
static bool BuildEpiloguePhi(AbckitGraph *graph, AbckitBasicBlock *targetBB, AbckitBasicBlock *endBB)
{
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
    g_implG->bbAddInstBack(targetBB, secondPhiInput);
    AbckitInst *phiInst = g_implG->bbCreatePhi(epilogueBB, 2, firstPhiInput, secondPhiInput);
    if (phiInst == nullptr) {
        return false;
    }
    g_implG->iSetInput(retInst, phiInst, 0);
    return true;
}

// Return failures to the worker while constructing the try/catch graph.
static bool TransformIrChecked(AbckitGraph *graph, UserData *ud)
{
    AbckitBasicBlock *endBB = nullptr;
    AbckitBasicBlock *targetBB = nullptr;
    AbckitInst *initInst = nullptr;
    AbckitInst *prevRetInst = nullptr;
    if (!ResolveTransformTargets(graph, &endBB, &targetBB, &initInst, &prevRetInst)) {
        return false;
    }
    AbckitBasicBlock *tryBegin = nullptr;
    AbckitBasicBlock *tryEnd = nullptr;
    if (!SplitTryRegion(initInst, prevRetInst, &tryBegin, &tryEnd)) {
        return false;
    }
    AbckitBasicBlock *catchBlock = nullptr;
    if (!BuildCatchBlock(graph, ud, &catchBlock)) {
        return false;
    }
    if (!BuildEpiloguePhi(graph, targetBB, endBB)) {
        return false;
    }
    g_implG->gInsertTryCatch(tryBegin, tryEnd, catchBlock, catchBlock);
    return true;
}

static UserData BuildUserData(AbckitFile *file)
{
    UserData ud {};
    ud.print = g_implM->createString(file, "print", std::strlen("print"));
    return ud;
}

// Build per-worker UserData and resolve the runN functions; false on any miss.
static bool BuildUserDataAndFunctions(AbckitFile *file, std::array<UserData, K_WORKER_COUNT> &uds,
                                      std::array<AbckitCoreFunction *, K_WORKER_COUNT> &fns)
{
    for (int i = 0; i < K_WORKER_COUNT; ++i) {
        uds[i] = BuildUserData(file);
        fns[i] = helpers::FindMethodByName(file, "run" + std::to_string(i + 1));
        if (fns[i] == nullptr) {
            return false;
        }
    }
    return true;
}

// Per-worker parallel body: build graph, transform, attach; flags failure atomically.
static void RunParallelWorker(int i, std::array<AbckitCoreFunction *, K_WORKER_COUNT> &fns,
                              std::array<AbckitGraph *, K_WORKER_COUNT> &graphs,
                              std::array<UserData, K_WORKER_COUNT> &uds, std::atomic<bool> &failed)
{
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
    if (g_impl->getLastError() != ABCKIT_STATUS_NO_ERROR) {
        failed.store(true);
    }
    graphs[i] = g;
}

// Destroy committed graphs, persist the file, then close it. False on write error.
static bool FinalizeAndWrite(AbckitFile *file, std::array<AbckitGraph *, K_WORKER_COUNT> &graphs, const char *outPath)
{
    for (int i = 0; i < K_WORKER_COUNT; ++i) {
        if (graphs[i] != nullptr) {
            g_impl->destroyGraph(graphs[i]);
        }
    }
    g_impl->writeAbc(file, outPath, std::strlen(outPath));
    bool ok = g_impl->getLastError() == ABCKIT_STATUS_NO_ERROR;
    g_impl->closeFile(file);
    return ok;
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
        std::array<UserData, K_WORKER_COUNT> uds;
        std::array<AbckitCoreFunction *, K_WORKER_COUNT> fns {};
        std::array<AbckitGraph *, K_WORKER_COUNT> graphs {};
        ASSERT_TRUE(BuildUserDataAndFunctions(file, uds, fns));
        for (int i = 0; i < K_WORKER_COUNT; ++i) {
            graphs[i] = g_implI->createGraphFromFunction(fns[i]);
            ASSERT_NE(graphs[i], nullptr);
            ASSERT_TRUE(TransformIrChecked(graphs[i], &uds[i]));
            g_implM->functionSetGraph(fns[i], graphs[i]);
            EXPECT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
        }
        EXPECT_TRUE(FinalizeAndWrite(file, graphs, SERIAL_OUT_PATH));
    }
    auto serialOut = helpers::ExecuteDynamicAbc(SERIAL_OUT_PATH, ENTRY);
    EXPECT_TRUE(helpers::Match(serialOut, expected));

    // ---- PARALLEL: K_WORKER_COUNT workers, each a different function on the same shared file ----
    {
        AbckitFile *file = g_impl->openAbc(INPUT_PATH, std::strlen(INPUT_PATH));
        ASSERT_NE(file, nullptr);
        std::array<UserData, K_WORKER_COUNT> uds;
        std::array<AbckitCoreFunction *, K_WORKER_COUNT> fns {};
        std::array<AbckitGraph *, K_WORKER_COUNT> graphs {};
        ASSERT_TRUE(BuildUserDataAndFunctions(file, uds, fns));
        std::atomic<bool> failed {false};
        std::vector<std::thread> threads;
        threads.reserve(K_WORKER_COUNT);
        for (int i = 0; i < K_WORKER_COUNT; ++i) {
            threads.emplace_back(
                [i, &fns, &graphs, &uds, &failed]() { RunParallelWorker(i, fns, graphs, uds, failed); });
        }
        for (auto &t : threads) {
            t.join();
        }
        ASSERT_FALSE(failed.load()) << "parallel add_try_catch transform reported a failure";
        EXPECT_TRUE(FinalizeAndWrite(file, graphs, PARALLEL_OUT_PATH));
    }
    auto parallelOut = helpers::ExecuteDynamicAbc(PARALLEL_OUT_PATH, ENTRY);
    EXPECT_TRUE(helpers::Match(parallelOut, expected));

    // Exact-byte equality (output is deterministic).
    EXPECT_EQ(NormalizeNumberLines(serialOut), NormalizeNumberLines(parallelOut));
}

}  // namespace libabckit::test

// NOLINTEND(readability-magic-numbers)
