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

// 20260807927972 Task 01-01: thread-safety UTs for Dynamic Graph/ISA snapshot acquisition.

#include "libabckit/c/abckit.h"
#include "libabckit/c/ir_core.h"
#include "libabckit/c/metadata_core.h"
#include "libabckit/c/statuses.h"
#include "libabckit/c/isa/isa_dynamic.h"
#include "libabckit/c/extensions/js/metadata_js.h"
#include "libabckit/src/metadata_inspect_impl.h"

#include "helpers/helpers.h"
#include "helpers/helpers_runtime.h"

#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

// NOLINTBEGIN(readability-magic-numbers)

namespace libabckit::test {

namespace {
auto g_impl = AbckitGetApiImpl(ABCKIT_VERSION_RELEASE_1_0_0);
auto g_implI = AbckitGetInspectApiImpl(ABCKIT_VERSION_RELEASE_1_0_0);
auto g_implG = AbckitGetGraphApiImpl(ABCKIT_VERSION_RELEASE_1_0_0);
auto g_implM = AbckitGetModifyApiImpl(ABCKIT_VERSION_RELEASE_1_0_0);
auto g_dynG = AbckitGetIsaApiDynamicImpl(ABCKIT_VERSION_RELEASE_1_0_0);
auto g_implJsM = AbckitGetJsModifyApiImpl(ABCKIT_VERSION_RELEASE_1_0_0);

static constexpr auto CONCURRENT_INPUT_PATH = ABCKIT_ABC_DIR "ut/ir_core/basic_blocks/js_src/create_empty_dynamic.abc";

// Returns true iff `graph` contains a LOADSTRING instruction (used to verify an added LoadString
// survived writeback + destroy + writeAbc + reload).
static bool GraphHasLoadString(AbckitGraph *graph)
{
    bool found = false;
    auto *cb = +[](AbckitBasicBlock *bb, void *data) -> bool {
        auto *out = reinterpret_cast<bool *>(data);
        auto *inst = g_implG->bbGetFirstInst(bb);
        while (inst != nullptr) {
            if (g_dynG->iGetOpcode(inst) == ABCKIT_ISA_API_DYNAMIC_OPCODE_LOADSTRING) {
                *out = true;
                return false;  // stop traversal
            }
            inst = g_implG->iGetNext(inst);
        }
        return true;
    };
    g_implG->gVisitBlocksRpo(graph, &found, cb);
    return found;
}

// Append a used LoadString chain (load global `print`; call print(marker)) to the first real block.
// An unused LoadString is dropped by codegen (dead-code elimination), so its result is consumed by
// Callarg1 to make the modification survive writeback + writeAbc + reload.
static void AddLoadStringCallChain(AbckitGraph *graph, AbckitString *marker, AbckitString *printName)
{
    auto *start = g_implG->gGetStartBasicBlock(graph);
    ASSERT_NE(start, nullptr);
    ASSERT_GT(g_implG->bbGetSuccBlockCount(start), 0u);
    auto *bb = g_implG->bbGetSuccBlock(start, 0);
    ASSERT_NE(bb, nullptr);
    auto *ldstr = g_dynG->iCreateLoadString(graph, marker);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    ASSERT_NE(ldstr, nullptr);
    g_implG->bbAddInstFront(bb, ldstr);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    auto *print = g_dynG->iCreateTryldglobalbyname(graph, printName);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    ASSERT_NE(print, nullptr);
    g_implG->iInsertAfter(print, ldstr);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    auto *callArg = g_dynG->iCreateCallarg1(graph, print, ldstr);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    ASSERT_NE(callArg, nullptr);
    g_implG->iInsertAfter(callArg, print);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
}
}  // namespace

class LibAbcKitGraphIsaIntegrationTest : public ::testing::Test {};

// 20260807927972-91-1.1: concurrent first-snapshot construction returns independent working graphs.
TEST_F(LibAbcKitGraphIsaIntegrationTest, DynamicCreateGraphConcurrentFirstSnapshot)
{
    AbckitFile *file = g_impl->openAbc(CONCURRENT_INPUT_PATH, std::strlen(CONCURRENT_INPUT_PATH));
    ASSERT_NE(file, nullptr);
    // The deprecated compatibility switch must not bypass snapshot synchronization.
    g_impl->setNeedOptimize(file, true);

    AbckitCoreFunction *func = helpers::FindMethodByName(file, "test");
    ASSERT_NE(func, nullptr);

    constexpr int N = 4;
    std::vector<std::thread> threads;
    threads.reserve(N);
    std::atomic<int> successes {0};
    std::atomic<bool> failed {false};

    for (int i = 0; i < N; ++i) {
        threads.emplace_back([&]() {
            AbckitGraph *graph = g_implI->createGraphFromFunction(func);
            if (graph == nullptr) {
                failed.store(true);
                return;
            }
            auto *start = g_implG->gGetStartBasicBlock(graph);
            if (start == nullptr) {
                failed.store(true);
                g_impl->destroyGraph(graph);
                return;
            }
            successes.fetch_add(1);
            g_impl->destroyGraph(graph);
        });
    }
    for (auto &t : threads) {
        t.join();
    }

    EXPECT_FALSE(failed.load());
    EXPECT_EQ(successes.load(), N);

    g_impl->closeFile(file);
}

// 20260807927972-91-2.2: an older graph cannot overwrite a newer function version.
TEST_F(LibAbcKitGraphIsaIntegrationTest, DynamicRejectsStaleGraphForSameFunction)
{
    AbckitFile *file = g_impl->openAbc(CONCURRENT_INPUT_PATH, std::strlen(CONCURRENT_INPUT_PATH));
    ASSERT_NE(file, nullptr);
    AbckitCoreFunction *func = helpers::FindMethodByName(file, "test");
    ASSERT_NE(func, nullptr);

    AbckitGraph *graph = g_implI->createGraphFromFunction(func);
    ASSERT_NE(graph, nullptr);
    AbckitGraph *staleGraph = g_implI->createGraphFromFunction(func);
    ASSERT_NE(staleGraph, nullptr);
    // First writeback succeeds (Graph's captured version == current).
    g_implM->functionSetGraph(func, graph);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    // A different Graph captured before the writeback is now stale.
    g_implM->functionSetGraph(func, staleGraph);
    EXPECT_EQ(g_impl->getLastError(), ABCKIT_STATUS_WRONG_STATE);
    g_impl->destroyGraph(staleGraph);
    g_impl->destroyGraph(graph);
    g_impl->closeFile(file);
}

// 20260807927972-91-2.1: distinct functions commit independently on the same file.
TEST_F(LibAbcKitGraphIsaIntegrationTest, DynamicFunctionSetGraphConcurrentDifferentFunctions)
{
    AbckitFile *file = g_impl->openAbc(CONCURRENT_INPUT_PATH, std::strlen(CONCURRENT_INPUT_PATH));
    ASSERT_NE(file, nullptr);
    AbckitCoreFunction *funcA = helpers::FindMethodByName(file, "test");
    AbckitCoreFunction *funcB = helpers::FindMethodByName(file, "test2");
    ASSERT_NE(funcA, nullptr);
    ASSERT_NE(funcB, nullptr);

    // Create the two Graphs sequentially (each emits under the snapshot mutex); setGraph below runs
    // concurrently to exercise the per-function prepare/commit.
    AbckitGraph *graphA = g_implI->createGraphFromFunction(funcA);
    ASSERT_NE(graphA, nullptr);
    AbckitGraph *graphB = g_implI->createGraphFromFunction(funcB);
    ASSERT_NE(graphB, nullptr);

    AbckitGraph *staleA = g_implI->createGraphFromFunction(funcA);
    ASSERT_NE(staleA, nullptr);
    AbckitGraph *staleB = g_implI->createGraphFromFunction(funcB);
    ASSERT_NE(staleB, nullptr);

    std::atomic<bool> failed {false};
    std::thread tA([&failed, funcA, graphA]() {
        g_implM->functionSetGraph(funcA, graphA);
        // Last-error is thread-local; capture the commit result in the worker.
        if (g_impl->getLastError() != ABCKIT_STATUS_NO_ERROR) {
            failed.store(true);
        }
    });
    std::thread tB([&failed, funcB, graphB]() {
        g_implM->functionSetGraph(funcB, graphB);
        if (g_impl->getLastError() != ABCKIT_STATUS_NO_ERROR) {
            failed.store(true);
        }
    });
    tA.join();
    tB.join();
    EXPECT_FALSE(failed.load());

    // Each successful Graph can be reused, while the other pre-commit Graph stays stale.
    g_implM->functionSetGraph(funcA, graphA);
    EXPECT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    g_implM->functionSetGraph(funcB, graphB);
    EXPECT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    g_implM->functionSetGraph(funcA, staleA);
    EXPECT_EQ(g_impl->getLastError(), ABCKIT_STATUS_WRONG_STATE);
    g_implM->functionSetGraph(funcB, staleB);
    EXPECT_EQ(g_impl->getLastError(), ABCKIT_STATUS_WRONG_STATE);

    g_impl->destroyGraph(staleA);
    g_impl->destroyGraph(staleB);
    g_impl->destroyGraph(graphA);
    g_impl->destroyGraph(graphB);
    g_impl->closeFile(file);
}

// 20260807927972-91-2.5: modifications survive graph destruction and ABC persistence.
TEST_F(LibAbcKitGraphIsaIntegrationTest, DynamicGraphModificationPersistsAfterDestroyAndReload)
{
    AbckitFile *file = g_impl->openAbc(CONCURRENT_INPUT_PATH, std::strlen(CONCURRENT_INPUT_PATH));
    ASSERT_NE(file, nullptr);
    AbckitCoreFunction *func = helpers::FindMethodByName(file, "test2");  // non-trivial body
    ASSERT_NE(func, nullptr);

    // Constant-pool extension before graph creation (does not advance structuralGeneration).
    AbckitString *marker = g_implM->createString(file, "ts_marker", std::strlen("ts_marker"));
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    ASSERT_NE(marker, nullptr);
    AbckitString *printName = g_implM->createString(file, "print", std::strlen("print"));
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    ASSERT_NE(printName, nullptr);

    AbckitGraph *graph = g_implI->createGraphFromFunction(func);
    ASSERT_NE(graph, nullptr);
    ASSERT_FALSE(GraphHasLoadString(graph)) << "baseline test2 graph must have no LoadString";

    // Add a USED LoadString chain (load global `print`; call print(marker)) to the first real
    // block. An unused LoadString is dropped by codegen (dead-code elimination), so its result is
    // consumed by Callarg1 to make the modification survive writeback + writeAbc + reload.
    ASSERT_NO_FATAL_FAILURE(AddLoadStringCallChain(graph, marker, printName));
    ASSERT_TRUE(GraphHasLoadString(graph)) << "LoadString must be present in graph right after add";

    // Writeback (sync prepare/commit), then destroy the Graph, then persist + reload.
    g_implM->functionSetGraph(func, graph);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);

    // The writeback placed the modification into the live program: re-emit + rebuild a Graph from
    // the same file (before destroy) and the LoadString must be present.
    {
        AbckitGraph *graphCheck = g_implI->createGraphFromFunction(func);
        ASSERT_NE(graphCheck, nullptr);
        EXPECT_TRUE(GraphHasLoadString(graphCheck)) << "modification missing from live program after setGraph";
        g_impl->destroyGraph(graphCheck);
    }
    g_impl->destroyGraph(graph);

    constexpr auto OUTPUT_PATH = ABCKIT_ABC_DIR "ut/ir_core/basic_blocks/js_src/pr14961_persist_dynamic.abc";
    g_impl->writeAbc(file, OUTPUT_PATH, std::strlen(OUTPUT_PATH));
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    g_impl->closeFile(file);

    // Reload the persisted abc and rebuild the Graph; the LoadString must be present.
    AbckitFile *file2 = g_impl->openAbc(OUTPUT_PATH, std::strlen(OUTPUT_PATH));
    ASSERT_NE(file2, nullptr);
    AbckitCoreFunction *func2 = helpers::FindMethodByName(file2, "test2");
    ASSERT_NE(func2, nullptr);
    AbckitGraph *graph2 = g_implI->createGraphFromFunction(func2);
    ASSERT_NE(graph2, nullptr);
    EXPECT_TRUE(GraphHasLoadString(graph2)) << "added LoadString must survive destroy+write+reload";

    g_impl->destroyGraph(graph2);
    g_impl->closeFile(file2);
    std::remove(OUTPUT_PATH);
}

// 20260807927972.09: structural changes from another thread invalidate an existing graph.
TEST_F(LibAbcKitGraphIsaIntegrationTest, DynamicMetadataChangeRejectsStaleGraph)
{
    AbckitFile *file = g_impl->openAbc(CONCURRENT_INPUT_PATH, std::strlen(CONCURRENT_INPUT_PATH));
    ASSERT_NE(file, nullptr);
    AbckitCoreFunction *func = helpers::FindMethodByName(file, "test");
    ASSERT_NE(func, nullptr);

    // Graph created BEFORE the structural change captures the old structuralGeneration.
    AbckitGraph *graph = g_implI->createGraphFromFunction(func);
    ASSERT_NE(graph, nullptr);

    // Structural change via the public Metadata API: add an external module.
    AbckitJsExternalModuleCreateParams params {"ts_structural_module"};
    AbckitJsModule *mod = nullptr;
    AbckitStatus mutationStatus = ABCKIT_STATUS_INTERNAL_ERROR;
    // An independent writer invalidates the owner's graph; owner-local edits remain supported.
    std::thread modifier([&mod, &mutationStatus, file, &params]() {
        mod = g_implJsM->fileAddExternalModule(file, &params);
        mutationStatus = g_impl->getLastError();
    });
    modifier.join();
    ASSERT_EQ(mutationStatus, ABCKIT_STATUS_NO_ERROR);
    ASSERT_NE(mod, nullptr);

    // The pre-change Graph is now structurally stale; setGraph must reject it (WRONG_STATE),
    // not overwrite the current function body.
    g_implM->functionSetGraph(func, graph);
    EXPECT_EQ(g_impl->getLastError(), ABCKIT_STATUS_WRONG_STATE);

    // Re-process with a fresh Graph (captures the new structuralGeneration) -> NO_ERROR; the
    // earlier WRONG_STATE did not corrupt the function body (AC .09: re-process is 0).
    AbckitGraph *graph2 = g_implI->createGraphFromFunction(func);
    ASSERT_NE(graph2, nullptr);
    g_implM->functionSetGraph(func, graph2);
    EXPECT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    g_impl->destroyGraph(graph2);

    g_impl->destroyGraph(graph);
    g_impl->closeFile(file);
}

// 20260807927972-91-1.2/1.3: failure publication wakes waiters and permits a later retry.
TEST_F(LibAbcKitGraphIsaIntegrationTest, DynamicSnapshotBuildGuardPublishesFailureAndWakesWaiter)
{
    AbckitFile *file = g_impl->openAbc(CONCURRENT_INPUT_PATH, std::strlen(CONCURRENT_INPUT_PATH));
    ASSERT_NE(file, nullptr);
    auto *function = helpers::FindMethodByName(file, "test");
    ASSERT_NE(function, nullptr);
    auto &cache = file->dynamicGraphCache;
    cache.state = AbckitDynamicGraphSnapshotState::BUILDING;
    std::atomic<bool> waiterDone {false};
    std::thread waiter([&cache, &waiterDone]() {
        std::unique_lock<std::mutex> lock(cache.mutex);
        cache.cv.wait(lock, [&cache]() { return cache.state != AbckitDynamicGraphSnapshotState::BUILDING; });
        waiterDone.store(true);
    });
    {
        AbckitFile::DynamicGraphCache::BuildStateGuard guard(cache, cache.invalidationEpoch);
    }
    waiter.join();
    EXPECT_TRUE(waiterDone.load());
    EXPECT_EQ(cache.state, AbckitDynamicGraphSnapshotState::FAILED);

    // A later independent request retries the failed snapshot instead of poisoning the file forever.
    auto *graph = g_implI->createGraphFromFunction(function);
    EXPECT_NE(graph, nullptr);
    EXPECT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    if (graph != nullptr) {
        g_impl->destroyGraph(graph);
    }
    g_impl->closeFile(file);
}

// 20260807927972-91-3.2: concurrent commits based on the same version have exactly one winner.
TEST_F(LibAbcKitGraphIsaIntegrationTest, DynamicConcurrentSameFunctionSetGraphOneWinsOneRejects)
{
    AbckitFile *file = g_impl->openAbc(CONCURRENT_INPUT_PATH, std::strlen(CONCURRENT_INPUT_PATH));
    ASSERT_NE(file, nullptr);
    AbckitCoreFunction *func = helpers::FindMethodByName(file, "test");
    ASSERT_NE(func, nullptr);

    constexpr int ITERS = 50;
    constexpr int kConcurrentThreads = 2;  // tA + tB enter functionSetGraph together
    for (int i = 0; i < ITERS; ++i) {
        AbckitGraph *gA = g_implI->createGraphFromFunction(func);
        AbckitGraph *gB = g_implI->createGraphFromFunction(func);
        ASSERT_NE(gA, nullptr);
        ASSERT_NE(gB, nullptr);

        uint64_t v0;
        {
            AbckitFileApiReadLock lk(file);
            v0 = (file->dynamicFunctionVersions.count(func) == 0 ? 0 : file->dynamicFunctionVersions.at(func));
        }

        // Barrier so both threads enter functionSetGraph together (maximizes the chance both pass
        // the early check before either commits, exercising the commit-recheck path).
        std::atomic<int> arrived {0};
        std::atomic<bool> go {false};
        auto task = [&arrived, &go, func](AbckitGraph *g) {
            arrived.fetch_add(1);
            while (!go.load()) {
            }
            g_implM->functionSetGraph(func, g);  // The final version check also rejects a double commit.
        };
        std::thread tA(task, gA);
        std::thread tB(task, gB);
        while (arrived.load() < kConcurrentThreads) {
        }
        go.store(true);
        tA.join();
        tB.join();

        // D4 guarantee: exactly one commit advanced the per-function version (catches both
        // both-reject (v1==v0) and both-win (v1==v0+2) D4 failures).
        uint64_t v1;
        {
            AbckitFileApiReadLock lk(file);
            v1 = (file->dynamicFunctionVersions.count(func) == 0 ? 0 : file->dynamicFunctionVersions.at(func));
        }
        EXPECT_EQ(v1, v0 + 1);

        g_impl->destroyGraph(gA);
        g_impl->destroyGraph(gB);
    }
    g_impl->closeFile(file);
}

// 20260807927972-91-2.7: adding a string does not invalidate a working graph structurally.
TEST_F(LibAbcKitGraphIsaIntegrationTest, DynamicCreateStringKeepsExistingGraphCurrent)
{
    AbckitFile *file = g_impl->openAbc(CONCURRENT_INPUT_PATH, std::strlen(CONCURRENT_INPUT_PATH));
    ASSERT_NE(file, nullptr);
    AbckitCoreFunction *func = helpers::FindMethodByName(file, "test");
    ASSERT_NE(func, nullptr);

    AbckitGraph *graph = g_implI->createGraphFromFunction(func);
    ASSERT_NE(graph, nullptr);

    uint64_t structGenBefore;
    {
        std::lock_guard<std::mutex> lk(file->dynamicGraphCache.mutex);
        structGenBefore = file->dynamicStructuralGeneration.load();
    }

    // Constant-pool extension.
    AbckitString *s = g_implM->createString(file, "new_constant_string", std::strlen("new_constant_string"));
    ASSERT_NE(s, nullptr);

    uint64_t structGenAfter;
    {
        std::lock_guard<std::mutex> lk(file->dynamicGraphCache.mutex);
        structGenAfter = file->dynamicStructuralGeneration.load();
    }
    EXPECT_EQ(structGenAfter, structGenBefore);  // unchanged -> Graph still structurally current

    // The Graph (captured before createString) is still current -> functionSetGraph succeeds.
    g_implM->functionSetGraph(func, graph);
    EXPECT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);

    g_impl->destroyGraph(graph);
    g_impl->closeFile(file);
}

// 20260807927972-91-2.7: adding a literal array does not advance the structural generation.
TEST_F(LibAbcKitGraphIsaIntegrationTest, DynamicCreateLiteralArrayKeepsPreparedFunctionCurrent)
{
    AbckitFile *file = g_impl->openAbc(CONCURRENT_INPUT_PATH, std::strlen(CONCURRENT_INPUT_PATH));
    ASSERT_NE(file, nullptr);
    AbckitCoreFunction *func = helpers::FindMethodByName(file, "test");
    ASSERT_NE(func, nullptr);

    // Prepare: functionSetGraph advances the per-function version (V -> V+1).
    AbckitGraph *g1 = g_implI->createGraphFromFunction(func);
    ASSERT_NE(g1, nullptr);
    g_implM->functionSetGraph(func, g1);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    g_impl->destroyGraph(g1);

    // Capture a fresh Graph (V+1, current structuralGeneration).
    AbckitGraph *g2 = g_implI->createGraphFromFunction(func);
    ASSERT_NE(g2, nullptr);

    uint64_t structGenBefore;
    {
        std::lock_guard<std::mutex> lk(file->dynamicGraphCache.mutex);
        structGenBefore = file->dynamicStructuralGeneration.load();
    }

    // Constant-pool extension via a literal array.
    AbckitLiteral *lit = g_implM->createLiteralString(file, "lit_str", std::strlen("lit_str"));
    ASSERT_NE(lit, nullptr);
    std::vector<AbckitLiteral *> arr = {lit};
    AbckitLiteralArray *la = g_implM->createLiteralArray(file, arr.data(), arr.size());
    ASSERT_NE(la, nullptr);

    uint64_t structGenAfter;
    {
        std::lock_guard<std::mutex> lk(file->dynamicGraphCache.mutex);
        structGenAfter = file->dynamicStructuralGeneration.load();
    }
    EXPECT_EQ(structGenAfter, structGenBefore);  // unchanged -> prepared function still current

    // G2 (captured after the prepare, before createLiteralArray) is still current -> setGraph succeeds.
    g_implM->functionSetGraph(func, g2);
    EXPECT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);

    g_impl->destroyGraph(g2);
    g_impl->closeFile(file);
}

// 20260807927972-91-3.3: single-thread graph roundtrip preserves execution output.
TEST_F(LibAbcKitGraphIsaIntegrationTest, DynamicGraphIsaSingleThreadRegression)
{
    constexpr auto INPUT = ABCKIT_ABC_DIR "scenarios/add_log/add_log_dynamic.abc";
    constexpr auto OUT = ABCKIT_ABC_DIR "scenarios/add_log/add_log_dynamic_regression.abc";
    constexpr auto ENTRY = "add_log_dynamic";

    auto origin = helpers::ExecuteDynamicAbc(INPUT, ENTRY);
    EXPECT_TRUE(helpers::Match(origin, "abckit\n"));

    AbckitFile *file = g_impl->openAbc(INPUT, std::strlen(INPUT));
    ASSERT_NE(file, nullptr);
    AbckitCoreFunction *func = helpers::FindMethodByName(file, "handle");
    ASSERT_NE(func, nullptr);
    AbckitGraph *graph = g_implI->createGraphFromFunction(func);
    ASSERT_NE(graph, nullptr);
    g_implM->functionSetGraph(func, graph);  // unmodified graph set back
    EXPECT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    g_impl->destroyGraph(graph);
    g_impl->writeAbc(file, OUT, std::strlen(OUT));
    EXPECT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    g_impl->closeFile(file);

    auto out = helpers::ExecuteDynamicAbc(OUT, ENTRY);
    EXPECT_EQ(out, origin);  // single-thread round-trip preserves output
}

// 20260807927972-91-3.4: Static graph operations do not use the Dynamic snapshot lock.
TEST_F(LibAbcKitGraphIsaIntegrationTest, StaticDoesNotUseDynamicFileLock)
{
    constexpr auto STATIC_INPUT = ABCKIT_ABC_DIR "clean_scenarios/c_api/static/add_log/add_log_static.abc";
    AbckitFile *file = g_impl->openAbc(STATIC_INPUT, std::strlen(STATIC_INPUT));
    ASSERT_NE(file, nullptr);
    AbckitCoreFunction *func = helpers::FindMethodByName(file, "handle");
    ASSERT_NE(func, nullptr);

    std::unique_lock<std::mutex> lk(file->dynamicGraphCache.mutex);  // hold the Dynamic lock
    std::atomic<bool> done {false};
    std::thread t([&done, func]() {
        AbckitGraph *g = g_implI->createGraphFromFunction(func);
        if (g != nullptr) {
            g_implM->functionSetGraph(func, g);
            g_impl->destroyGraph(g);
        }
        done.store(true);
    });
    // Static path must NOT block on snapshot mutex -> completes well within 5s.
    constexpr int kPollIntervalMs = 10;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!done.load() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(kPollIntervalMs));
    }
    EXPECT_TRUE(done.load()) << "Static path blocked on the Dynamic snapshot mutex (would deadlock)";
    lk.unlock();  // release before join so a blocked worker can proceed + the test never hangs
    t.join();
    g_impl->closeFile(file);
}

// 20260807927972-91-1.3: independent workers own their graphs through construction, commit and destruction.
TEST_F(LibAbcKitGraphIsaIntegrationTest, DynamicGraphExclusiveOwnerScheduling)
{
    AbckitFile *file = g_impl->openAbc(CONCURRENT_INPUT_PATH, std::strlen(CONCURRENT_INPUT_PATH));
    ASSERT_NE(file, nullptr);
    AbckitCoreFunction *funcA = helpers::FindMethodByName(file, "test");
    AbckitCoreFunction *funcB = helpers::FindMethodByName(file, "test2");
    ASSERT_NE(funcA, nullptr);
    ASSERT_NE(funcB, nullptr);

    std::atomic<bool> ok {true};
    auto owner = [&ok](AbckitCoreFunction *func) {
        AbckitGraph *g = g_implI->createGraphFromFunction(func);
        if (g == nullptr) {
            ok.store(false);
            return;
        }
        g_implM->functionSetGraph(func, g);  // D4 prepare/commit
        g_impl->destroyGraph(g);
    };
    std::thread tA(owner, funcA);
    std::thread tB(owner, funcB);
    tA.join();
    tB.join();
    EXPECT_TRUE(ok.load());  // both owners completed their full lifecycle without interference
    g_impl->closeFile(file);
}

// 20260807927972-91-3.1: graph construction stresses concurrent compiler pass execution.
TEST_F(LibAbcKitGraphIsaIntegrationTest, PassManagerExecutionIdConcurrent)
{
    AbckitFile *file = g_impl->openAbc(CONCURRENT_INPUT_PATH, std::strlen(CONCURRENT_INPUT_PATH));
    ASSERT_NE(file, nullptr);
    g_impl->setNeedOptimize(file, true);
    AbckitCoreFunction *func = helpers::FindMethodByName(file, "test");
    ASSERT_NE(func, nullptr);

    constexpr int N = 8;  // more threads than 01-01 to stress the global executionCounter_
    std::atomic<int> successes {0};
    std::atomic<bool> failed {false};
    std::vector<std::thread> threads;
    threads.reserve(N);
    for (int i = 0; i < N; ++i) {
        threads.emplace_back([&]() {
            AbckitGraph *g = g_implI->createGraphFromFunction(func);
            if (g == nullptr) {
                failed.store(true);
                return;
            }
            successes.fetch_add(1);
            g_impl->destroyGraph(g);
        });
    }
    for (auto &t : threads) {
        t.join();
    }
    EXPECT_FALSE(failed.load());
    EXPECT_EQ(successes.load(), N);

    g_impl->closeFile(file);
}

}  // namespace libabckit::test

// NOLINTEND(readability-magic-numbers)
