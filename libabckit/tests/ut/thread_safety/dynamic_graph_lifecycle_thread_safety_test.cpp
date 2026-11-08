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

#include <atomic>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <future>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <vector>

#include <gtest/gtest.h>

#include "helpers/helpers.h"
#include "helpers/helpers_runtime.h"
#include "libabckit/c/abckit.h"
#include "libabckit/c/extensions/js/metadata_js.h"
#include "libabckit/c/ir_core.h"
#include "libabckit/c/isa/isa_dynamic.h"
#include "libabckit/c/metadata_core.h"
#include "libabckit/src/adapter_dynamic/abckit_dynamic.h"
#include "libabckit/src/adapter_dynamic/metadata_modify_dynamic.h"
#include "libabckit/src/ir_impl.h"
#include "libabckit/src/metadata_inspect_impl.h"

namespace libabckit {
void InvalidateDynamicGraphCacheForStructuralChange(AbckitFile *file);
AbckitLiteral *FindOrCreateLiteralLiteralArrayDynamicImpl(AbckitFile *file, const std::string &value);
AbckitValue *FindOrCreateLiteralArrayValueDynamicImpl(AbckitFile *file, const std::string &value);
}  // namespace libabckit

namespace libabckit::test {

static auto g_impl = AbckitGetApiImpl(ABCKIT_VERSION_RELEASE_1_0_0);
static auto g_implI = AbckitGetInspectApiImpl(ABCKIT_VERSION_RELEASE_1_0_0);
static auto g_implM = AbckitGetModifyApiImpl(ABCKIT_VERSION_RELEASE_1_0_0);
static auto g_implG = AbckitGetGraphApiImpl(ABCKIT_VERSION_RELEASE_1_0_0);
static auto g_dynG = AbckitGetIsaApiDynamicImpl(ABCKIT_VERSION_RELEASE_1_0_0);

class LibAbcKitDynamicThreadSafetyTest : public ::testing::Test {};

static bool WaitForActiveOperationCount(const std::shared_ptr<AbckitFileThreadState> &threadState, size_t expectedCount)
{
    constexpr auto TIMEOUT = std::chrono::seconds(30);
    const auto deadline = std::chrono::steady_clock::now() + TIMEOUT;
    while (std::chrono::steady_clock::now() < deadline) {
        if (threadState->GetActiveOperationCount() >= expectedCount) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return threadState->GetActiveOperationCount() >= expectedCount;
}

struct NestedCreateGraphContext {
    AbckitCoreFunction *function = nullptr;
    AbckitGraph *graph = nullptr;
    AbckitStatus createStatus = ABCKIT_STATUS_NO_ERROR;
    AbckitStatus setStatus = ABCKIT_STATUS_NO_ERROR;
    AbckitStatus destroyStatus = ABCKIT_STATUS_NO_ERROR;
};

// Test: test-kind=api, api=ModifyApiImpl::functionSetGraph, abc-kind=ArkTS1, category=positive, extension=c
TEST_F(LibAbcKitDynamicThreadSafetyTest, DynamicPreparedFunctionsCommitAfterGraphsAreDestroyed)
{
    constexpr auto INPUT_PATH = ABCKIT_ABC_DIR "internal/implementation_api/abc_dynamic.abc";
    AbckitFile *file = nullptr;
    helpers::AssertOpenAbc(INPUT_PATH, &file);

    auto *firstFunction = helpers::FindMethodByName(file, "foo");
    auto *secondFunction = helpers::FindMethodByName(file, "func_main_0");
    ASSERT_NE(firstFunction, nullptr);
    ASSERT_NE(secondFunction, nullptr);

    auto *firstGraph = g_implI->createGraphFromFunction(firstFunction);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    ASSERT_NE(firstGraph, nullptr);
    auto *secondGraph = g_implI->createGraphFromFunction(secondFunction);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    ASSERT_NE(secondGraph, nullptr);

    auto *firstPrepared = PrepareFunctionSetGraphDynamic(firstFunction, firstGraph);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    ASSERT_NE(firstPrepared, nullptr);
    auto *secondPrepared = PrepareFunctionSetGraphDynamic(secondFunction, secondGraph);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    ASSERT_NE(secondPrepared, nullptr);

    g_impl->destroyGraph(firstGraph);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    g_impl->destroyGraph(secondGraph);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);

    CommitPreparedFunctionDynamic(firstPrepared);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    CommitPreparedFunctionDynamic(secondPrepared);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);

    g_impl->closeFile(file);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
}

// Test: test-kind=api, api=ApiImpl::closeFile, abc-kind=ArkTS1, category=positive, extension=c
TEST_F(LibAbcKitDynamicThreadSafetyTest, DynamicCloseFileRejectedWhilePreparedFunctionAlive)
{
    constexpr auto INPUT_PATH = ABCKIT_ABC_DIR "internal/implementation_api/abc_dynamic.abc";
    AbckitFile *file = nullptr;
    helpers::AssertOpenAbc(INPUT_PATH, &file);

    auto *function = helpers::FindMethodByName(file, "foo");
    ASSERT_NE(function, nullptr);
    auto *graph = g_implI->createGraphFromFunction(function);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    ASSERT_NE(graph, nullptr);
    auto *prepared = PrepareFunctionSetGraphDynamic(function, graph);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    ASSERT_NE(prepared, nullptr);

    g_impl->destroyGraph(graph);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    g_impl->closeFile(file);
    const auto closeStatus = g_impl->getLastError();
    EXPECT_EQ(closeStatus, ABCKIT_STATUS_WRONG_STATE);
    if (closeStatus != ABCKIT_STATUS_WRONG_STATE) {
        return;
    }

    DestroyPreparedFunctionDynamic(prepared);
    g_impl->closeFile(file);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
}

// Test: test-kind=api, api=ApiImpl::writeAbc, abc-kind=ArkTS1, category=positive, extension=c
TEST_F(LibAbcKitDynamicThreadSafetyTest, DynamicWriteFileRejectedWhileGraphAlive)
{
    constexpr auto INPUT_PATH = ABCKIT_ABC_DIR "internal/implementation_api/abc_dynamic.abc";
    constexpr auto OUTPUT_PATH = ABCKIT_ABC_DIR "internal/implementation_api/abc_dynamic_thread_safe_graph_alive.abc";
    AbckitFile *file = nullptr;
    helpers::AssertOpenAbc(INPUT_PATH, &file);

    auto *function = helpers::FindMethodByName(file, "foo");
    ASSERT_NE(function, nullptr);
    auto *graph = g_implI->createGraphFromFunction(function);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    ASSERT_NE(graph, nullptr);

    g_impl->writeAbc(file, OUTPUT_PATH, strlen(OUTPUT_PATH));
    EXPECT_EQ(g_impl->getLastError(), ABCKIT_STATUS_WRONG_STATE);

    g_impl->destroyGraph(graph);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    g_impl->writeAbc(file, OUTPUT_PATH, strlen(OUTPUT_PATH));
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    g_impl->closeFile(file);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
}

// Test: test-kind=api, api=ApiImpl::writeAbc, abc-kind=ArkTS1, category=positive, extension=c
TEST_F(LibAbcKitDynamicThreadSafetyTest, DynamicWriteFileRejectedWhilePreparedFunctionAlive)
{
    constexpr auto INPUT_PATH = ABCKIT_ABC_DIR "internal/implementation_api/abc_dynamic.abc";
    constexpr auto OUTPUT_PATH =
        ABCKIT_ABC_DIR "internal/implementation_api/abc_dynamic_thread_safe_prepared_alive.abc";
    AbckitFile *file = nullptr;
    helpers::AssertOpenAbc(INPUT_PATH, &file);

    auto *function = helpers::FindMethodByName(file, "foo");
    ASSERT_NE(function, nullptr);
    auto *graph = g_implI->createGraphFromFunction(function);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    ASSERT_NE(graph, nullptr);
    auto *prepared = PrepareFunctionSetGraphDynamic(function, graph);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    ASSERT_NE(prepared, nullptr);
    g_impl->destroyGraph(graph);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);

    g_impl->writeAbc(file, OUTPUT_PATH, strlen(OUTPUT_PATH));
    EXPECT_EQ(g_impl->getLastError(), ABCKIT_STATUS_WRONG_STATE);

    CommitPreparedFunctionDynamic(prepared);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    g_impl->writeAbc(file, OUTPUT_PATH, strlen(OUTPUT_PATH));
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    g_impl->closeFile(file);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
}

// Test: test-kind=api, api=ModifyApiImpl::functionSetGraph, abc-kind=ArkTS1, category=positive, extension=c
TEST_F(LibAbcKitDynamicThreadSafetyTest, DynamicRejectsStalePreparedFunction)
{
    constexpr auto INPUT_PATH = ABCKIT_ABC_DIR "internal/implementation_api/abc_dynamic.abc";
    AbckitFile *file = nullptr;
    helpers::AssertOpenAbc(INPUT_PATH, &file);

    auto *function = helpers::FindMethodByName(file, "foo");
    ASSERT_NE(function, nullptr);
    auto *firstGraph = g_implI->createGraphFromFunction(function);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    ASSERT_NE(firstGraph, nullptr);
    auto *staleGraph = g_implI->createGraphFromFunction(function);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    ASSERT_NE(staleGraph, nullptr);

    auto *firstPrepared = PrepareFunctionSetGraphDynamic(function, firstGraph);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    ASSERT_NE(firstPrepared, nullptr);
    auto *stalePrepared = PrepareFunctionSetGraphDynamic(function, staleGraph);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    ASSERT_NE(stalePrepared, nullptr);
    g_impl->destroyGraph(firstGraph);
    g_impl->destroyGraph(staleGraph);

    CommitPreparedFunctionDynamic(firstPrepared);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    CommitPreparedFunctionDynamic(stalePrepared);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_WRONG_STATE);

    g_impl->closeFile(file);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
}

// Test: test-kind=api, api=ModifyApiImpl::functionSetGraph, abc-kind=ArkTS1, category=positive, extension=c
TEST_F(LibAbcKitDynamicThreadSafetyTest, DynamicMetadataChangeRejectsPreparedFunction)
{
    constexpr auto INPUT_PATH = ABCKIT_ABC_DIR "internal/implementation_api/abc_dynamic.abc";
    AbckitFile *file = nullptr;
    helpers::AssertOpenAbc(INPUT_PATH, &file);

    auto *function = helpers::FindMethodByName(file, "foo");
    ASSERT_NE(function, nullptr);
    auto *graph = g_implI->createGraphFromFunction(function);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    ASSERT_NE(graph, nullptr);
    auto *prepared = PrepareFunctionSetGraphDynamic(function, graph);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    ASSERT_NE(prepared, nullptr);
    g_impl->destroyGraph(graph);

    {
        AbckitFileApiWriteLock apiLock(file);
        ASSERT_FALSE(apiLock.HasConflict());
        InvalidateDynamicGraphCacheForStructuralChange(file);
    }
    CommitPreparedFunctionDynamic(prepared);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_WRONG_STATE);

    g_impl->closeFile(file);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
}

// Test: test-kind=api, api=ModifyApiImpl::functionSetGraph, abc-kind=ArkTS1, category=positive, extension=c
TEST_F(LibAbcKitDynamicThreadSafetyTest, DynamicRejectsStaleGraphForSameFunction)
{
    constexpr auto INPUT_PATH = ABCKIT_ABC_DIR "internal/implementation_api/abc_dynamic.abc";
    AbckitFile *file = nullptr;
    helpers::AssertOpenAbc(INPUT_PATH, &file);

    auto *function = helpers::FindMethodByName(file, "foo");
    ASSERT_NE(function, nullptr);

    auto *firstGraph = g_implI->createGraphFromFunction(function);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    ASSERT_NE(firstGraph, nullptr);
    auto *staleGraph = g_implI->createGraphFromFunction(function);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    ASSERT_NE(staleGraph, nullptr);

    g_implM->functionSetGraph(function, firstGraph);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    g_implM->functionSetGraph(function, staleGraph);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_WRONG_STATE);

    g_impl->destroyGraph(firstGraph);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    g_impl->destroyGraph(staleGraph);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    g_impl->closeFile(file);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
}

// Test: test-kind=api, api=ModifyApiImpl::functionSetGraph, abc-kind=ArkTS1, category=positive, extension=c
TEST_F(LibAbcKitDynamicThreadSafetyTest, DynamicPublicSetGraphPreparesUnderSharedFileLock)
{
    constexpr auto INPUT_PATH = ABCKIT_ABC_DIR "internal/implementation_api/abc_dynamic.abc";
    AbckitFile *file = nullptr;
    helpers::AssertOpenAbc(INPUT_PATH, &file);

    auto *function = helpers::FindMethodByName(file, "foo");
    ASSERT_NE(function, nullptr);
    auto *graph = g_implI->createGraphFromFunction(function);
    ASSERT_NE(graph, nullptr);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);

    AbckitStatus setStatus = ABCKIT_STATUS_INTERNAL_ERROR;
    std::thread setter;
    bool reachedCommit = false;
    {
        std::shared_lock<std::shared_mutex> holdSharedFileLock(file->threadState->apiMutex);
        setter = std::thread([&]() {
            g_implM->functionSetGraph(function, graph);
            setStatus = g_impl->getLastError();
        });
        // Prepare owns a read-operation token and its prepared result owns a
        // second token while Commit waits for this shared lock to be released.
        reachedCommit = WaitForActiveOperationCount(file->threadState, 2U);
    }
    setter.join();

    EXPECT_TRUE(reachedCommit);
    EXPECT_EQ(setStatus, ABCKIT_STATUS_NO_ERROR);
    g_impl->destroyGraph(graph);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    g_impl->closeFile(file);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
}

// Test: test-kind=internal, api=InspectApiImpl::createGraphFromFunction, abc-kind=NoABC, category=positive,
// extension=c
TEST_F(LibAbcKitDynamicThreadSafetyTest, DynamicSnapshotBuildGuardPublishesFailureAndWakesWaiter)
{
    AbckitFile::DynamicGraphCache cache;
    cache.state = AbckitDynamicGraphSnapshotState::BUILDING;
    cache.invalidationEpoch = 7U;
    std::atomic<bool> waiterAwakened {false};
    std::thread waiter([&]() {
        std::unique_lock<std::mutex> lock(cache.mutex);
        cache.cv.wait(lock, [&]() { return cache.state != AbckitDynamicGraphSnapshotState::BUILDING; });
        waiterAwakened.store(true, std::memory_order_release);
    });
    {
        AbckitFile::DynamicGraphCache::BuildStateGuard guard(cache, 7U);
    }
    waiter.join();

    EXPECT_EQ(cache.state, AbckitDynamicGraphSnapshotState::FAILED);
    EXPECT_TRUE(waiterAwakened.load(std::memory_order_acquire));
}

// Test: test-kind=internal, api=InspectApiImpl::createGraphFromFunction, abc-kind=NoABC, category=positive,
// extension=c
TEST_F(LibAbcKitDynamicThreadSafetyTest, DynamicSnapshotBuildGuardPublishesStaleAfterInvalidation)
{
    AbckitFile::DynamicGraphCache cache;
    cache.state = AbckitDynamicGraphSnapshotState::BUILDING;
    cache.invalidationEpoch = 8U;
    {
        AbckitFile::DynamicGraphCache::BuildStateGuard guard(cache, 7U);
    }
    EXPECT_EQ(cache.state, AbckitDynamicGraphSnapshotState::STALE);
}

// Test: test-kind=api, api=ModifyApiImpl::functionSetGraph, abc-kind=ArkTS1, category=positive, extension=c
TEST_F(LibAbcKitDynamicThreadSafetyTest, DynamicMetadataChangeRejectsStaleGraph)
{
    constexpr auto INPUT_PATH = ABCKIT_ABC_DIR "internal/implementation_api/abc_dynamic.abc";
    AbckitFile *file = nullptr;
    helpers::AssertOpenAbc(INPUT_PATH, &file);

    auto *function = helpers::FindMethodByName(file, "foo");
    ASSERT_NE(function, nullptr);
    auto *graph = g_implI->createGraphFromFunction(function);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    ASSERT_NE(graph, nullptr);

    std::atomic<bool> mutationCompleted {false};
    std::atomic<bool> mutationConflict {true};
    std::thread metadataModifier([&]() {
        AbckitFileApiWriteLock apiLock(file);
        mutationConflict.store(apiLock.HasConflict(), std::memory_order_release);
        if (!apiLock.HasConflict()) {
            InvalidateDynamicGraphCacheForStructuralChange(file);
        }
        mutationCompleted.store(true, std::memory_order_release);
    });
    metadataModifier.join();
    ASSERT_TRUE(mutationCompleted.load(std::memory_order_acquire));
    ASSERT_FALSE(mutationConflict.load(std::memory_order_acquire));
    ASSERT_EQ(graph->ownerThreadId, std::this_thread::get_id());
    ASSERT_EQ(graph->baseStructuralGeneration, 0U);
    ASSERT_EQ(graph->baseOwnerStructuralGeneration, 0U);
    ASSERT_EQ(file->dynamicStructuralGeneration.load(std::memory_order_acquire), 1U);
    ASSERT_EQ(file->dynamicStructuralGenerationsByThread.find(graph->ownerThreadId),
              file->dynamicStructuralGenerationsByThread.end());

    g_implM->functionSetGraph(function, graph);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_WRONG_STATE);

    g_impl->destroyGraph(graph);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    g_impl->closeFile(file);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
}

// Test: test-kind=api, api=InspectApiImpl::createGraphFromFunction, abc-kind=ArkTS1, category=positive,
// extension=c
TEST_F(LibAbcKitDynamicThreadSafetyTest, DynamicCreateGraphSynchronizesOwnerStructuralGeneration)
{
    constexpr auto INPUT_PATH = ABCKIT_ABC_DIR "internal/implementation_api/abc_dynamic.abc";
    constexpr size_t CREATE_ATTEMPTS = 2048U;
    constexpr size_t MODIFIER_THREADS = 64U;
    AbckitFile *file = nullptr;
    helpers::AssertOpenAbc(INPUT_PATH, &file);
    auto *function = helpers::FindMethodByName(file, "foo");
    ASSERT_NE(function, nullptr);

    // Keep the snapshot ready so the creator repeatedly reaches the graph-owner
    // generation capture while new modifier threads insert their generation entries.
    auto *primingGraph = g_implI->createGraphFromFunction(function);
    ASSERT_NE(primingGraph, nullptr);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    g_impl->destroyGraph(primingGraph);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);

    std::atomic<bool> start {false};
    std::atomic<bool> failed {false};
    std::atomic<size_t> createdGraphs {0U};
    std::thread creator([&]() {
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        for (size_t attempt = 0; attempt < CREATE_ATTEMPTS; ++attempt) {
            auto *graph = g_implI->createGraphFromFunction(function);
            auto status = g_impl->getLastError();
            if (graph == nullptr) {
                if (status != ABCKIT_STATUS_WRONG_STATE) {
                    failed.store(true, std::memory_order_release);
                }
                continue;
            }
            if (status != ABCKIT_STATUS_NO_ERROR) {
                failed.store(true, std::memory_order_release);
            }
            createdGraphs.fetch_add(1U, std::memory_order_relaxed);
            g_impl->destroyGraph(graph);
            if (g_impl->getLastError() != ABCKIT_STATUS_NO_ERROR) {
                failed.store(true, std::memory_order_release);
            }
        }
    });

    std::vector<std::thread> modifiers;
    modifiers.reserve(MODIFIER_THREADS);
    for (size_t i = 0; i < MODIFIER_THREADS; ++i) {
        modifiers.emplace_back([&]() {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            AbckitFileApiWriteLock apiLock(file);
            if (apiLock.HasConflict()) {
                failed.store(true, std::memory_order_release);
                return;
            }
            ++file->dynamicStructuralGenerationsByThread[std::this_thread::get_id()];
        });
    }

    start.store(true, std::memory_order_release);
    creator.join();
    for (auto &modifier : modifiers) {
        modifier.join();
    }

    ASSERT_FALSE(failed.load(std::memory_order_acquire));
    ASSERT_GT(createdGraphs.load(std::memory_order_relaxed), 0U);
    g_impl->closeFile(file);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
}

// Test: test-kind=api, api=ModifyApiImpl::createString, abc-kind=ArkTS1, category=positive, extension=c
TEST_F(LibAbcKitDynamicThreadSafetyTest, DynamicCreateStringKeepsExistingGraphCurrent)
{
    constexpr auto INPUT_PATH = ABCKIT_ABC_DIR "internal/implementation_api/abc_dynamic.abc";
    constexpr auto NEW_STRING = "thread-safe-structural-version";
    AbckitFile *file = nullptr;
    helpers::AssertOpenAbc(INPUT_PATH, &file);

    auto *function = helpers::FindMethodByName(file, "foo");
    ASSERT_NE(function, nullptr);
    auto *graph = g_implI->createGraphFromFunction(function);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    ASSERT_NE(graph, nullptr);

    auto *string = g_implM->createString(file, NEW_STRING, strlen(NEW_STRING));
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    ASSERT_NE(string, nullptr);
    g_implM->functionSetGraph(function, graph);
    EXPECT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);

    g_impl->destroyGraph(graph);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    g_impl->closeFile(file);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
}

// Test: test-kind=api, api=ModifyApiImpl::createLiteralArray, abc-kind=ArkTS1, category=positive, extension=c
TEST_F(LibAbcKitDynamicThreadSafetyTest, DynamicCreateLiteralArrayKeepsPreparedFunctionCurrent)
{
    constexpr auto INPUT_PATH = ABCKIT_ABC_DIR "internal/implementation_api/abc_dynamic.abc";
    AbckitFile *file = nullptr;
    helpers::AssertOpenAbc(INPUT_PATH, &file);

    auto *function = helpers::FindMethodByName(file, "foo");
    ASSERT_NE(function, nullptr);
    auto *graph = g_implI->createGraphFromFunction(function);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    ASSERT_NE(graph, nullptr);
    auto *prepared = PrepareFunctionSetGraphDynamic(function, graph);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    ASSERT_NE(prepared, nullptr);
    g_impl->destroyGraph(graph);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);

    auto *literal = g_implM->createLiteralU32(file, 1U);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    ASSERT_NE(literal, nullptr);
    std::array<AbckitLiteral *, 1U> literals = {literal};
    auto *literalArray = g_implM->createLiteralArray(file, literals.data(), literals.size());
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    ASSERT_NE(literalArray, nullptr);
    CommitPreparedFunctionDynamic(prepared);
    EXPECT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);

    g_impl->closeFile(file);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
}

// Test: test-kind=api, api=ModifyApiImpl::functionSetGraph, abc-kind=ArkTS1, category=positive, extension=c
TEST_F(LibAbcKitDynamicThreadSafetyTest, DynamicSetGraphKeepsOtherFunctionGraphCurrent)
{
    constexpr auto INPUT_PATH = ABCKIT_ABC_DIR "internal/implementation_api/abc_dynamic.abc";
    AbckitFile *file = nullptr;
    helpers::AssertOpenAbc(INPUT_PATH, &file);

    auto *firstFunction = helpers::FindMethodByName(file, "foo");
    auto *secondFunction = helpers::FindMethodByName(file, "func_main_0");
    ASSERT_NE(firstFunction, nullptr);
    ASSERT_NE(secondFunction, nullptr);

    auto *firstGraph = g_implI->createGraphFromFunction(firstFunction);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    ASSERT_NE(firstGraph, nullptr);
    auto *secondGraph = g_implI->createGraphFromFunction(secondFunction);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    ASSERT_NE(secondGraph, nullptr);

    g_implM->functionSetGraph(firstFunction, firstGraph);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    g_implM->functionSetGraph(secondFunction, secondGraph);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);

    g_impl->destroyGraph(firstGraph);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    g_impl->destroyGraph(secondGraph);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    g_impl->closeFile(file);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
}

// Test: test-kind=api, api=InspectApiImpl::createGraphFromFunction, abc-kind=ArkTS1, category=positive, extension=c
TEST_F(LibAbcKitDynamicThreadSafetyTest, DynamicFunctionCommitKeepsSnapshotForUnmodifiedFunction)
{
    constexpr auto INPUT_PATH = ABCKIT_ABC_DIR "internal/implementation_api/abc_dynamic.abc";
    AbckitFile *file = nullptr;
    helpers::AssertOpenAbc(INPUT_PATH, &file);

    auto *firstFunction = helpers::FindMethodByName(file, "foo");
    auto *secondFunction = helpers::FindMethodByName(file, "func_main_0");
    ASSERT_NE(firstFunction, nullptr);
    ASSERT_NE(secondFunction, nullptr);

    auto *firstGraph = g_implI->createGraphFromFunction(firstFunction);
    ASSERT_NE(firstGraph, nullptr);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    g_implM->functionSetGraph(firstFunction, firstGraph);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    g_impl->destroyGraph(firstGraph);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);

    auto *secondGraph = g_implI->createGraphFromFunction(secondFunction);
    ASSERT_NE(secondGraph, nullptr);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    EXPECT_EQ(file->dynamicGraphCache.GetSuccessfulBuildCount(), 1U);

    g_impl->destroyGraph(secondGraph);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    g_impl->closeFile(file);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
}

// Test: test-kind=api, api=InspectApiImpl::createGraphFromFunction, abc-kind=ArkTS1, category=positive, extension=c
TEST_F(LibAbcKitDynamicThreadSafetyTest, DynamicModifiedFunctionRefreshesItsSnapshot)
{
    constexpr auto INPUT_PATH = ABCKIT_ABC_DIR "internal/implementation_api/abc_dynamic.abc";
    AbckitFile *file = nullptr;
    helpers::AssertOpenAbc(INPUT_PATH, &file);

    auto *function = helpers::FindMethodByName(file, "foo");
    ASSERT_NE(function, nullptr);
    auto *firstGraph = g_implI->createGraphFromFunction(function);
    ASSERT_NE(firstGraph, nullptr);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    g_implM->functionSetGraph(function, firstGraph);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    g_impl->destroyGraph(firstGraph);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);

    auto *refreshedGraph = g_implI->createGraphFromFunction(function);
    ASSERT_NE(refreshedGraph, nullptr);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    EXPECT_EQ(file->dynamicGraphCache.GetSuccessfulBuildCount(), 2U);

    g_impl->destroyGraph(refreshedGraph);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    g_impl->closeFile(file);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
}

// Test: test-kind=api, api=ApiImpl::closeFile, abc-kind=ArkTS1, category=positive, extension=c
TEST_F(LibAbcKitDynamicThreadSafetyTest, DynamicCloseFileRejectedWhileGraphAlive)
{
    constexpr auto INPUT_PATH = ABCKIT_ABC_DIR "internal/implementation_api/abc_dynamic.abc";
    AbckitFile *file = nullptr;
    helpers::AssertOpenAbc(INPUT_PATH, &file);

    auto *function = helpers::FindMethodByName(file, "foo");
    ASSERT_NE(function, nullptr);
    auto *graph = g_implI->createGraphFromFunction(function);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    ASSERT_NE(graph, nullptr);

    g_impl->closeFile(file);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_WRONG_STATE);

    g_impl->destroyGraph(graph);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    g_impl->closeFile(file);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
}

// Test: test-kind=api, api=ApiImpl::closeFile, abc-kind=ArkTS1, category=positive, extension=c
TEST_F(LibAbcKitDynamicThreadSafetyTest, DynamicReadOperationRegisteredBeforeApiLockWait)
{
    constexpr auto INPUT_PATH = ABCKIT_ABC_DIR "internal/implementation_api/abc_dynamic.abc";
    AbckitFile *file = nullptr;
    helpers::AssertOpenAbc(INPUT_PATH, &file);

    std::thread reader;
    bool operationRegistered = false;
    {
        AbckitFileApiWriteLock writeLock(file);
        ASSERT_FALSE(writeLock.HasConflict());
        reader = std::thread([file]() { AbckitFileApiReadLock readLock(file); });

        operationRegistered = WaitForActiveOperationCount(file->threadState, 2U);
        if (operationRegistered) {
            EXPECT_FALSE(file->threadState->TryBeginClose());
        }
    }
    reader.join();

    ASSERT_TRUE(operationRegistered);
    g_impl->closeFile(file);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
}

static void CheckFinalOperationWaitsForMetadataRead(bool close)
{
    constexpr auto INPUT_PATH = ABCKIT_ABC_DIR "internal/implementation_api/abc_dynamic.abc";
    constexpr auto OUTPUT_PATH = ABCKIT_ABC_DIR "internal/implementation_api/abc_dynamic_waited_read.abc";
    AbckitFile *file = nullptr;
    helpers::AssertOpenAbc(INPUT_PATH, &file);
    ASSERT_NE(file, nullptr);

    struct ReadContext {
        std::promise<void> entered;
        std::shared_future<void> release;
        bool timedOut = false;
    };
    std::promise<void> releaseRead;
    ReadContext context {{}, releaseRead.get_future().share(), false};
    auto readEntered = context.entered.get_future();
    bool enumerated = true;
    AbckitStatus readStatus = ABCKIT_STATUS_INTERNAL_ERROR;
    std::thread reader([&]() {
        enumerated = g_implI->fileEnumerateModules(file, &context, [](AbckitCoreModule *, void *data) {
            auto &ctx = *static_cast<ReadContext *>(data);
            ctx.entered.set_value();
            ctx.timedOut = ctx.release.wait_for(std::chrono::seconds(30)) != std::future_status::ready;
            return false;
        });
        readStatus = g_impl->getLastError();
    });
    const bool entered = readEntered.wait_for(std::chrono::seconds(30)) == std::future_status::ready;
    if (!entered) {
        releaseRead.set_value();
        reader.join();
        g_impl->closeFile(file);
        FAIL() << "Metadata callback did not start";
    }

    // While the callback is suspended, only the finalizer can acquire another state reference.
    // CloseFile does not register an operation, but its write guard takes such a reference.
    auto threadState = file->threadState;
    const auto readerReferences = threadState.use_count();
    std::promise<AbckitStatus> finalFinished;
    auto finished = finalFinished.get_future();
    std::thread finalizer([&]() {
        if (close) {
            g_impl->closeFile(file);
        } else {
            g_impl->writeAbc(file, OUTPUT_PATH, std::strlen(OUTPUT_PATH));
        }
        finalFinished.set_value(g_impl->getLastError());
    });
    bool finalizerEntered = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < deadline) {
        finalizerEntered =
            close ? threadState.use_count() > readerReferences : threadState->GetActiveOperationCount() >= 2U;
        if (finalizerEntered || finished.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    EXPECT_TRUE(finalizerEntered) << "Final operation did not enter its file guard";
    EXPECT_EQ(finished.wait_for(std::chrono::milliseconds(100)), std::future_status::timeout);
    releaseRead.set_value();
    reader.join();
    finalizer.join();
    const auto finalStatus = finished.get();
    EXPECT_FALSE(context.timedOut);
    EXPECT_FALSE(enumerated);
    EXPECT_EQ(readStatus, ABCKIT_STATUS_NO_ERROR);
    EXPECT_EQ(finalStatus, ABCKIT_STATUS_NO_ERROR);
    if (!close || finalStatus != ABCKIT_STATUS_NO_ERROR) {
        g_impl->closeFile(file);
        EXPECT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    }
    // A successful final operation must leave either the output or original input reloadable.
    AbckitFile *reopened = nullptr;
    helpers::AssertOpenAbc(close ? INPUT_PATH : OUTPUT_PATH, &reopened);
    ASSERT_NE(reopened, nullptr);
    g_impl->closeFile(reopened);
    EXPECT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
}

TEST_F(LibAbcKitDynamicThreadSafetyTest, DynamicWriteWaitsForPublicMetadataRead)
{
    ASSERT_NO_FATAL_FAILURE(CheckFinalOperationWaitsForMetadataRead(false));
}

TEST_F(LibAbcKitDynamicThreadSafetyTest, DynamicCloseWaitsForPublicMetadataRead)
{
    ASSERT_NO_FATAL_FAILURE(CheckFinalOperationWaitsForMetadataRead(true));
}

// Test: test-kind=api, api=ApiImpl::closeFile, abc-kind=ArkTS1, category=positive, extension=c
TEST_F(LibAbcKitDynamicThreadSafetyTest, DynamicApisRejectedAfterCloseBegins)
{
    constexpr auto INPUT_PATH = ABCKIT_ABC_DIR "internal/implementation_api/abc_dynamic.abc";
    AbckitFile *file = nullptr;
    helpers::AssertOpenAbc(INPUT_PATH, &file);
    auto threadState = file->threadState;

    {
        std::lock_guard<std::shared_mutex> apiLock(threadState->apiMutex);
        ASSERT_TRUE(threadState->TryBeginClose());
    }

    bool enumerateResult = true;
    AbckitStatus enumerateStatus = ABCKIT_STATUS_NO_ERROR;
    std::thread reader([&]() {
        enumerateResult = g_implI->fileEnumerateModules(file, nullptr, [](AbckitCoreModule *, void *) { return true; });
        enumerateStatus = g_impl->getLastError();
    });
    AbckitString *createdString = reinterpret_cast<AbckitString *>(1);
    AbckitStatus createStatus = ABCKIT_STATUS_NO_ERROR;
    std::thread writer([&]() {
        constexpr auto VALUE = "must-not-be-created";
        createdString = g_implM->createString(file, VALUE, strlen(VALUE));
        createStatus = g_impl->getLastError();
    });
    reader.join();
    writer.join();

    EXPECT_FALSE(enumerateResult);
    EXPECT_EQ(enumerateStatus, ABCKIT_STATUS_WRONG_STATE);
    EXPECT_EQ(createdString, nullptr);
    EXPECT_EQ(createStatus, ABCKIT_STATUS_WRONG_STATE);

    {
        std::lock_guard<std::shared_mutex> apiLock(threadState->apiMutex);
        CloseFileDynamic(file);
    }
    threadState->FinishClose();
}

// Test: test-kind=api, api=InspectApiImpl::createGraphFromFunction, abc-kind=ArkTS1, category=positive, extension=c
TEST_F(LibAbcKitDynamicThreadSafetyTest, DynamicCreateGraphInsideInspectCallback)
{
    constexpr auto INPUT_PATH = ABCKIT_ABC_DIR "internal/implementation_api/abc_dynamic.abc";
    AbckitFile *file = nullptr;
    helpers::AssertOpenAbc(INPUT_PATH, &file);

    NestedCreateGraphContext ctx;
    ctx.function = helpers::FindMethodByName(file, "foo");
    ASSERT_NE(ctx.function, nullptr);

    g_implI->fileEnumerateModules(file, &ctx, [](AbckitCoreModule *, void *data) {
        auto *nestedCtx = reinterpret_cast<NestedCreateGraphContext *>(data);
        nestedCtx->graph = g_implI->createGraphFromFunction(nestedCtx->function);
        nestedCtx->createStatus = g_impl->getLastError();
        if (nestedCtx->graph == nullptr) {
            return false;
        }
        g_implM->functionSetGraph(nestedCtx->function, nestedCtx->graph);
        nestedCtx->setStatus = g_impl->getLastError();
        g_impl->destroyGraph(nestedCtx->graph);
        nestedCtx->destroyStatus = g_impl->getLastError();
        nestedCtx->graph = nullptr;
        return false;
    });

    ASSERT_EQ(ctx.createStatus, ABCKIT_STATUS_NO_ERROR);
    ASSERT_EQ(ctx.setStatus, ABCKIT_STATUS_NO_ERROR);
    ASSERT_EQ(ctx.destroyStatus, ABCKIT_STATUS_NO_ERROR);
    ASSERT_EQ(ctx.graph, nullptr);
    ASSERT_EQ(file->dynamicGraphCache.GetSuccessfulBuildCount(), 1U);

    g_impl->closeFile(file);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
}

constexpr auto PREPARED_FUNCTION_INPUT_PATH = ABCKIT_ABC_DIR "ut/isa/isa_dynamic/load_string/load_string_dynamic.abc";
constexpr auto PREPARED_FUNCTION_OUTPUT_PATH =
    ABCKIT_ABC_DIR "ut/isa/isa_dynamic/load_string/load_string_dynamic_thread_safe_modified.abc";
constexpr auto PREPARED_FUNCTION_NAME = "load_string_dynamic.foo";
constexpr auto PREPARED_FUNCTION_MARKER = "thread-safe-prepared";

static void PersistIsaModificationAfterGraphDestroyed()
{
    AbckitFile *file = nullptr;
    helpers::AssertOpenAbc(PREPARED_FUNCTION_INPUT_PATH, &file);
    auto *function = helpers::FindMethodByName(file, PREPARED_FUNCTION_NAME);
    ASSERT_NE(function, nullptr);

    // Build the snapshot first. The new constant-pool string must remain usable
    // by this already-created graph and survive prepare/commit and serialization.
    auto *graph = g_implI->createGraphFromFunction(function);
    ASSERT_NE(graph, nullptr);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    auto *marker = g_implM->createString(file, PREPARED_FUNCTION_MARKER, strlen(PREPARED_FUNCTION_MARKER));
    ASSERT_NE(marker, nullptr);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    auto *ret = helpers::FindFirstInst(graph, ABCKIT_ISA_API_DYNAMIC_OPCODE_RETURN);
    ASSERT_NE(ret, nullptr);
    auto *load = g_dynG->iCreateLoadString(graph, marker);
    ASSERT_NE(load, nullptr);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    g_implG->iInsertBefore(load, ret);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    g_implG->iSetInput(ret, load, 0);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);

    auto *prepared = PrepareFunctionSetGraphDynamic(function, graph);
    ASSERT_NE(prepared, nullptr);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    g_impl->destroyGraph(graph);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    CommitPreparedFunctionDynamic(prepared);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);

    g_impl->writeAbc(file, PREPARED_FUNCTION_OUTPUT_PATH, strlen(PREPARED_FUNCTION_OUTPUT_PATH));
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    g_impl->closeFile(file);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);

    AbckitFile *reopened = nullptr;
    helpers::AssertOpenAbc(PREPARED_FUNCTION_OUTPUT_PATH, &reopened);
    auto *reopenedFunction = helpers::FindMethodByName(reopened, PREPARED_FUNCTION_NAME);
    ASSERT_NE(reopenedFunction, nullptr);
    auto *reopenedGraph = g_implI->createGraphFromFunction(reopenedFunction);
    ASSERT_NE(reopenedGraph, nullptr);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    auto *reopenedLoad = helpers::FindFirstInst(reopenedGraph, ABCKIT_ISA_API_DYNAMIC_OPCODE_LOADSTRING);
    ASSERT_NE(reopenedLoad, nullptr);
    auto *reopenedMarker = g_implG->iGetString(reopenedLoad);
    ASSERT_NE(reopenedMarker, nullptr);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    ASSERT_EQ(helpers::AbckitStringToString(reopenedMarker), PREPARED_FUNCTION_MARKER);
    g_impl->destroyGraph(reopenedGraph);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    g_impl->closeFile(reopened);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
}

// Test: test-kind=api, api=ModifyApiImpl::functionSetGraph, abc-kind=ArkTS1, category=positive, extension=c
TEST_F(LibAbcKitDynamicThreadSafetyTest, DynamicPreparedFunctionPersistsIsaModificationAfterGraphDestroyed)
{
    ASSERT_NO_FATAL_FAILURE(PersistIsaModificationAfterGraphDestroyed());
}

// Keep VM execution separate from the LibAbcKit TSAN boundary: the ETS runtime starts its own GC worker threads.
// Test: test-kind=api, api=ModifyApiImpl::functionSetGraph, abc-kind=ArkTS1, category=positive, extension=c
TEST_F(LibAbcKitDynamicThreadSafetyTest, DynamicPreparedFunctionPersistsIsaModificationAfterGraphDestroyedExecution)
{
    ASSERT_NO_FATAL_FAILURE(PersistIsaModificationAfterGraphDestroyed());
    const auto output = helpers::ExecuteDynamicAbc(PREPARED_FUNCTION_OUTPUT_PATH, "load_string_dynamic");
    EXPECT_TRUE(helpers::Match(output, "thread-safe-prepared\n"));
}

}  // namespace libabckit::test
