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

static constexpr auto CONCURRENT_INPUT_PATH =
    ABCKIT_ABC_DIR "ut/ir_core/basic_blocks/js_src/create_empty_dynamic.abc";

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
}  // namespace

class LibAbcKitDynamicThreadSafetyTest : public ::testing::Test {};

// 20260807927972-91-1.1: concurrent first-snapshot. Multiple threads creating Graphs on the same
// file exercise the single-builder snapshot protocol: one builder runs EmitDynamicProgram under the
// cache mutex, the others wait on the condition variable and reuse the immutable READY snapshot.
// getLastError() is a process-global last-error (not thread-local), so concurrent threads assert
// only on the returned Graph pointer and structural validity, not on the global last-error.
TEST_F(LibAbcKitDynamicThreadSafetyTest, DynamicCreateGraphConcurrentFirstSnapshot)
{
    AbckitFile *file = g_impl->openAbc(CONCURRENT_INPUT_PATH, std::strlen(CONCURRENT_INPUT_PATH));
    ASSERT_NE(file, nullptr);
    // needOptimize=true enables the cached emitted snapshot, so the single-builder shared path is
    // exercised (rather than each call re-emitting).
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
            successes.fetch_add(1, std::memory_order_relaxed);
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

// 20260807927972-91-2.2: stale Graph rejection. A Graph based on an old per-function version
// (created before a successful functionSetGraph advanced the version) must be rejected with
// ABCKIT_STATUS_WRONG_STATE and must not overwrite the newer result. needOptimize=false so
// functionSetGraph goes through FunctionSetGraphDynamicSync (immediate writeback + version bump).
TEST_F(LibAbcKitDynamicThreadSafetyTest, DynamicRejectsStaleGraphForSameFunction)
{
    AbckitFile *file = g_impl->openAbc(CONCURRENT_INPUT_PATH, std::strlen(CONCURRENT_INPUT_PATH));
    ASSERT_NE(file, nullptr);
    AbckitCoreFunction *func = helpers::FindMethodByName(file, "test");
    ASSERT_NE(func, nullptr);

    AbckitGraph *graph = g_implI->createGraphFromFunction(func);
    ASSERT_NE(graph, nullptr);
    // First writeback succeeds (Graph's captured version == current).
    g_implM->functionSetGraph(func, graph);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    // Setting the SAME (now-stale) Graph again -> WRONG_STATE (version advanced on the first set).
    g_implM->functionSetGraph(func, graph);
    EXPECT_EQ(g_impl->getLastError(), ABCKIT_STATUS_WRONG_STATE);
    g_impl->destroyGraph(graph);
    g_impl->closeFile(file);
}

// 20260807927972-91-2.1: different functions' concurrent writeback both take effect. Two threads
// each setGraph a distinct function's Graph. Because the prepare/commit commit recheck is per-
// function, neither write loses: both advance their own per-function version. Correctness is then
// verified sequentially: re-setting each (now-stale) Graph must return WRONG_STATE, proving each
// writeback committed. needOptimize=false so functionSetGraph goes through the sync prepare/commit.
// getLastError() is process-global, so the concurrent phase asserts only on absence of failure
// flags (not on the global last-error); the post-join stale checks are sequential and reliable.
TEST_F(LibAbcKitDynamicThreadSafetyTest, DynamicFunctionSetGraphConcurrentDifferentFunctions)
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

    std::atomic<bool> failed {false};
    std::thread tA([&]() {
        g_implM->functionSetGraph(funcA, graphA);
        // Cannot assert on the process-global last-error concurrently; flag only on structural fail.
        if (g_impl->getLastError() != ABCKIT_STATUS_NO_ERROR) {
            failed.store(true);
        }
    });
    std::thread tB([&]() {
        g_implM->functionSetGraph(funcB, graphB);
        if (g_impl->getLastError() != ABCKIT_STATUS_NO_ERROR) {
            failed.store(true);
        }
    });
    tA.join();
    tB.join();
    EXPECT_FALSE(failed.load());

    // Sequential re-set of each Graph: each is now stale (its function's version advanced during
    // the concurrent commit), so each must be rejected with WRONG_STATE. Both being stale proves
    // both writebacks took effect.
    g_implM->functionSetGraph(funcA, graphA);
    EXPECT_EQ(g_impl->getLastError(), ABCKIT_STATUS_WRONG_STATE);
    g_implM->functionSetGraph(funcB, graphB);
    EXPECT_EQ(g_impl->getLastError(), ABCKIT_STATUS_WRONG_STATE);

    g_impl->destroyGraph(graphA);
    g_impl->destroyGraph(graphB);
    g_impl->closeFile(file);
}

// 20260807927972-91-2.5: writeback->destroy->writeAbc->reload observes the modification. A
// LoadString added to the Graph must survive functionSetGraph (sync prepare/commit writeback into
// the live program), destroyGraph (the modification lives in the program, not the Graph object),
// writeAbc (emission to a new abc), and reopening that abc + recreating the Graph. needOptimize=
// false so functionSetGraph runs the sync prepare/commit; createString is constant-pool and is
// called before createGraphFromFunction (matching the established add_log scenario order) so the
// marker string is present in the program at emission time.
TEST_F(LibAbcKitDynamicThreadSafetyTest, DynamicGraphModificationPersistsAfterDestroyAndReload)
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

    constexpr auto OUTPUT_PATH = "/tmp/20260807927972_persist_dynamic.abc";
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

// 20260807927972.09: a structural metadata change rejects a pre-change Graph. Adding an external
// module via the public Metadata API (fileAddExternalModule) is a real file-structure change; it
// advances the file's structuralGeneration. A Graph created before the change carries the old
// structuralGeneration, so functionSetGraph's commit recheck must reject it with WRONG_STATE and
// must not overwrite the current function. needOptimize=false so functionSetGraph runs the sync
// prepare/commit.
TEST_F(LibAbcKitDynamicThreadSafetyTest, DynamicMetadataChangeRejectsStaleGraph)
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
    AbckitJsModule *mod = g_implJsM->fileAddExternalModule(file, &params);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
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

// 20260807927972-91-1.2/1.3: a failed snapshot build publishes FAILED and wakes a waiter; a
// subsequent caller sees FAILED and returns null without retrying. A real emission failure
// (EmitDynamicProgram returns null) is what sets graphCacheFailed=true in production; here the
// FAILED state is injected to deterministically exercise the snapshot-cache state machine: the CV
// wake (a waiter never blocks forever on a failed builder) + the FAILED-return branch (no retry, no
// builder re-emission). needOptimize=true enables the cached snapshot + the FAILED state.
// NOTE: GraphBuildStateGuard is file-local to metadata_inspect_dynamic.cpp, so the builder's RAII
// publish is exercised through the real createGraphFromFunction path elsewhere; this test injects
// the post-failure state to cover the `else if (needOptimize && graphCacheFailed)` return branch.
TEST_F(LibAbcKitDynamicThreadSafetyTest, DynamicSnapshotBuildGuardPublishesFailureAndWakesWaiter)
{
    AbckitFile *file = g_impl->openAbc(CONCURRENT_INPUT_PATH, std::strlen(CONCURRENT_INPUT_PATH));
    ASSERT_NE(file, nullptr);
    g_impl->setNeedOptimize(file, true);  // enables the cached snapshot + FAILED state
    AbckitCoreFunction *func = helpers::FindMethodByName(file, "test");
    ASSERT_NE(func, nullptr);

    // Inject a prior failed build: a subsequent caller must see FAILED + return null (no retry, no
    // builder re-emission). Exercises the `else if (needOptimize && graphCacheFailed)` branch.
    {
        std::lock_guard<std::mutex> lk(file->graphCacheMtx);
        file->graphCacheBuilding = false;
        file->graphCacheFailed = true;
    }
    AbckitGraph *g0 = g_implI->createGraphFromFunction(func);
    EXPECT_EQ(g0, nullptr);  // FAILED -> null, no retry

    // Reset for the wake test: a builder is "mid-emission".
    {
        std::lock_guard<std::mutex> lk(file->graphCacheMtx);
        file->graphCacheFailed = false;
        file->graphCacheBuilding = true;
    }

    // Waiter: createGraphFromFunction blocks on the CV while building=true.
    std::atomic<bool> waiter_done {false};
    std::thread waiter([&]() {
        AbckitGraph *g = g_implI->createGraphFromFunction(func);
        EXPECT_EQ(g, nullptr);  // woken -> sees FAILED -> null
        waiter_done.store(true);
    });

    // Let the waiter reach CV.wait (blocked while building=true).
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // The builder fails: publish building=false + graphCacheFailed=true + notify_all (what the
    // builder's emission-failure path + BuildStateGuard dtor do). This MUST wake the waiter.
    {
        std::lock_guard<std::mutex> lk(file->graphCacheMtx);
        file->graphCacheBuilding = false;
        file->graphCacheFailed = true;
        file->graphCacheCv.notify_all();
    }

    // The waiter must have been woken + returned (not blocked forever).
    waiter.join();
    EXPECT_TRUE(waiter_done.load());

    g_impl->closeFile(file);
}

// 20260807927972-91-3.2: concurrent same-function functionSetGraph — D4 one-wins-one-rejects.
// Two threads setGraph the SAME function with two Graphs that captured the same version. D4's
// prepare/commit guarantees exactly one commit advances the per-function version (the winner);
// the other's commit-recheck rejects with WRONG_STATE (or, if the winner fully completed first,
// the loser's early check rejects — either way, exactly one version advance). The concurrent phase
// exercises the commit-recheck stale-reject branch when both threads pass the early check before
// either commits (the likely path, since codegen outlasts the early check); coverage of that branch
// is best-effort because the race may instead take the early-reject path. The assertion is only the
// guaranteed safety property — exactly one version advance + both threads complete — so the test is
// never flaky. getLastError() is process-global (not thread-local), so the concurrent phase asserts
// nothing on it; the post-join version check is sequential and reliable.
TEST_F(LibAbcKitDynamicThreadSafetyTest, DynamicConcurrentSameFunctionSetGraphOneWinsOneRejects)
{
    AbckitFile *file = g_impl->openAbc(CONCURRENT_INPUT_PATH, std::strlen(CONCURRENT_INPUT_PATH));
    ASSERT_NE(file, nullptr);  // needOptimize=false default -> D4 prepare/commit
    AbckitCoreFunction *func = helpers::FindMethodByName(file, "test");
    ASSERT_NE(func, nullptr);

    constexpr int ITERS = 50;
    for (int i = 0; i < ITERS; ++i) {
        AbckitGraph *gA = g_implI->createGraphFromFunction(func);
        AbckitGraph *gB = g_implI->createGraphFromFunction(func);
        ASSERT_NE(gA, nullptr);
        ASSERT_NE(gB, nullptr);

        uint64_t v0;
        {
            std::lock_guard<std::mutex> lk(file->graphCacheMtx);
            v0 = file->functionVersions[func];
        }

        // Barrier so both threads enter functionSetGraph together (maximizes the chance both pass
        // the early check before either commits, exercising the commit-recheck path).
        std::atomic<int> arrived {0};
        std::atomic<bool> go {false};
        auto task = [&](AbckitGraph *g) {
            arrived.fetch_add(1, std::memory_order_relaxed);
            while (!go.load(std::memory_order_relaxed)) {
            }
            g_implM->functionSetGraph(func, g);  // getLastError() is racy here -> not checked
        };
        std::thread tA(task, gA);
        std::thread tB(task, gB);
        while (arrived.load(std::memory_order_relaxed) < 2) {
        }
        go.store(true, std::memory_order_relaxed);
        tA.join();
        tB.join();

        // D4 guarantee: exactly one commit advanced the per-function version (catches both
        // both-reject (v1==v0) and both-win (v1==v0+2) D4 failures).
        uint64_t v1;
        {
            std::lock_guard<std::mutex> lk(file->graphCacheMtx);
            v1 = file->functionVersions[func];
        }
        EXPECT_EQ(v1, v0 + 1);

        g_impl->destroyGraph(gA);
        g_impl->destroyGraph(gB);
    }
    g_impl->closeFile(file);
}

// 20260807927972 Task 01-04 (91-2.7): constant-pool extension must NOT invalidate a Graph.
// createString extends the file's constant pool but must NOT advance structuralGeneration (only
// metadata-topology changes — module/class/function add — do). So a Graph captured before
// createString stays structurally current + functionSetGraph succeeds (no WRONG_STATE).
TEST_F(LibAbcKitDynamicThreadSafetyTest, DynamicCreateStringKeepsExistingGraphCurrent)
{
    AbckitFile *file = g_impl->openAbc(CONCURRENT_INPUT_PATH, std::strlen(CONCURRENT_INPUT_PATH));
    ASSERT_NE(file, nullptr);  // needOptimize=false default -> D4 prepare/commit
    AbckitCoreFunction *func = helpers::FindMethodByName(file, "test");
    ASSERT_NE(func, nullptr);

    AbckitGraph *graph = g_implI->createGraphFromFunction(func);
    ASSERT_NE(graph, nullptr);

    uint64_t structGenBefore;
    {
        std::lock_guard<std::mutex> lk(file->graphCacheMtx);
        structGenBefore = file->structuralGeneration;
    }

    // Constant-pool extension.
    AbckitString *s = g_implM->createString(file, "new_constant_string", std::strlen("new_constant_string"));
    ASSERT_NE(s, nullptr);

    uint64_t structGenAfter;
    {
        std::lock_guard<std::mutex> lk(file->graphCacheMtx);
        structGenAfter = file->structuralGeneration;
    }
    EXPECT_EQ(structGenAfter, structGenBefore);  // unchanged -> Graph still structurally current

    // The Graph (captured before createString) is still current -> functionSetGraph succeeds.
    g_implM->functionSetGraph(func, graph);
    EXPECT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);

    g_impl->destroyGraph(graph);
    g_impl->closeFile(file);
}

// 20260807927972 Task 01-04 (91-2.7): createLiteralArray likewise must NOT advance
// structuralGeneration, so a Graph captured after a prior prepare (functionSetGraph) stays current.
TEST_F(LibAbcKitDynamicThreadSafetyTest, DynamicCreateLiteralArrayKeepsPreparedFunctionCurrent)
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
        std::lock_guard<std::mutex> lk(file->graphCacheMtx);
        structGenBefore = file->structuralGeneration;
    }

    // Constant-pool extension via a literal array.
    AbckitLiteral *lit = g_implM->createLiteralString(file, "lit_str", std::strlen("lit_str"));
    ASSERT_NE(lit, nullptr);
    std::vector<AbckitLiteral *> arr = {lit};
    AbckitLiteralArray *la = g_implM->createLiteralArray(file, arr.data(), arr.size());
    ASSERT_NE(la, nullptr);

    uint64_t structGenAfter;
    {
        std::lock_guard<std::mutex> lk(file->graphCacheMtx);
        structGenAfter = file->structuralGeneration;
    }
    EXPECT_EQ(structGenAfter, structGenBefore);  // unchanged -> prepared function still current

    // G2 (captured after the prepare, before createLiteralArray) is still current -> setGraph succeeds.
    g_implM->functionSetGraph(func, g2);
    EXPECT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);

    g_impl->destroyGraph(g2);
    g_impl->closeFile(file);
}

// 20260807927972 Task 01-06 (91-3.3): single-thread regression. The D1/D4 thread-safety changes must
// not alter single-thread Graph/ISA results. Round-trip an UNMODIFIED Graph (createGraphFromFunction
// -> functionSetGraph -> writeAbc -> execute) on the add_log_dynamic abc; the output must equal the
// origin (no transform applied -> codegen reproduces the original body).
TEST_F(LibAbcKitDynamicThreadSafetyTest, DynamicGraphIsaSingleThreadRegression)
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
    g_impl->writeAbc(file, OUT, std::strlen(OUT));
    g_impl->destroyGraph(graph);
    g_impl->closeFile(file);

    auto out = helpers::ExecuteDynamicAbc(OUT, ENTRY);
    EXPECT_EQ(out, origin);  // single-thread round-trip preserves output
}

// 20260807927972 Task 01-06 (91-3.4): Static isolation. The Static inspect/modify path must NOT use
// the Dynamic graphCacheMtx (the D1/D4 lock is Dynamic-only). Hold the lock in the test, then run
// static createGraphFromFunction + functionSetGraph in a worker; if the Static path touched the lock
// it would block (deadlock) + the deadline fires. The lock is released before join so a failure is
// reported, not a hang.
TEST_F(LibAbcKitDynamicThreadSafetyTest, StaticDoesNotUseDynamicFileLock)
{
    constexpr auto STATIC_INPUT = ABCKIT_ABC_DIR "clean_scenarios/c_api/static/add_log/add_log_static.abc";
    AbckitFile *file = g_impl->openAbc(STATIC_INPUT, std::strlen(STATIC_INPUT));
    ASSERT_NE(file, nullptr);
    AbckitCoreFunction *func = helpers::FindMethodByName(file, "handle");
    ASSERT_NE(func, nullptr);

    std::unique_lock<std::mutex> lk(file->graphCacheMtx);  // hold the Dynamic lock
    std::atomic<bool> done {false};
    std::thread t([&]() {
        AbckitGraph *g = g_implI->createGraphFromFunction(func);
        if (g != nullptr) {
            g_implM->functionSetGraph(func, g);
            g_impl->destroyGraph(g);
        }
        done.store(true);
    });
    // Static path must NOT block on graphCacheMtx -> completes well within 5s.
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!done.load(std::memory_order_relaxed) && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_TRUE(done.load()) << "Static path blocked on the Dynamic graphCacheMtx (would deadlock)";
    lk.unlock();  // release before join so a blocked worker can proceed + the test never hangs
    t.join();
    g_impl->closeFile(file);
}

// 20260807927972 Task 01-02 (91-1.3): exclusive-owner scheduling. Each Graph has a single owner
// thread for its full lifecycle (createGraphFromFunction -> functionSetGraph -> destroyGraph); no
// Graph is shared across threads. Two owners each run the full lifecycle on a DIFFERENT function
// concurrently — exercises single-owner scheduling under concurrency (distinct from 01-03, which
// creates graphs sequentially then setGraphs concurrently; this owns the whole lifecycle per
// thread). The library deliberately does NOT enforce owner-thread at runtime — it is a caller
// contract documented by the @note headers (01-00); this test + the scenario tests (02-04..08)
// orchestrate it.
TEST_F(LibAbcKitDynamicThreadSafetyTest, DynamicGraphExclusiveOwnerScheduling)
{
    AbckitFile *file = g_impl->openAbc(CONCURRENT_INPUT_PATH, std::strlen(CONCURRENT_INPUT_PATH));
    ASSERT_NE(file, nullptr);
    AbckitCoreFunction *funcA = helpers::FindMethodByName(file, "test");
    AbckitCoreFunction *funcB = helpers::FindMethodByName(file, "test2");
    ASSERT_NE(funcA, nullptr);
    ASSERT_NE(funcB, nullptr);

    std::atomic<bool> ok {true};
    auto owner = [&](AbckitCoreFunction *func) {
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

// 20260807927972 Task 01-05 (91-3.1): PassManager execution-id concurrency. N threads each
// createGraphFromFunction concurrently -> each runs the PassManager (StartExecution increments the
// global executionCounter_). A non-atomic counter would data-race (TSan exit 66); the std::atomic
// fix (Task 01-05) is race-free (TSan exit 0). On a regular build this asserts no crash; the TSan
// build (global -fsanitize=thread, instrumenting libarkruntime.so where pass_manager.cpp lives)
// verifies no data race. needOptimize=true exercises the cached snapshot path that runs the checker
// (via PassManager) per Graph.
TEST_F(LibAbcKitDynamicThreadSafetyTest, PassManagerExecutionIdConcurrent)
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
            successes.fetch_add(1, std::memory_order_relaxed);
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
