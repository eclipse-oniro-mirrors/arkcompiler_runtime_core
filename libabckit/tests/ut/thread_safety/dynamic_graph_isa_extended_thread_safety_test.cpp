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
static auto g_dynG = AbckitGetIsaApiDynamicImpl(ABCKIT_VERSION_RELEASE_1_0_0);
static auto g_implG = AbckitGetGraphApiImpl(ABCKIT_VERSION_RELEASE_1_0_0);

class LibAbcKitDynamicThreadSafetyTest : public ::testing::Test {};

// The released Dynamic ISA C table contains only function-pointer fields. Keep
// the slot count explicit so adding an API field without wiring its
// implementation cannot silently leave a null trailing aggregate member.
using DynamicIsaApiSlot = void (*)();
static_assert(sizeof(AbckitIsaApiDynamic) == 199U * sizeof(DynamicIsaApiSlot));

static bool WaitForWorkerCompletion(const std::atomic<bool> &started, const std::atomic<bool> &completed)
{
    constexpr auto TIMEOUT = std::chrono::seconds(30);
    const auto deadline = std::chrono::steady_clock::now() + TIMEOUT;
    while (!started.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
    }
    if (!started.load(std::memory_order_acquire)) {
        return false;
    }

    while (!completed.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return completed.load(std::memory_order_acquire);
}

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

class ThreadBarrier {
public:
    explicit ThreadBarrier(size_t threadCount) : threadCount_(threadCount) {}

    void Wait()
    {
        auto generation = generation_.load(std::memory_order_acquire);
        if (arrived_.fetch_add(1U, std::memory_order_acq_rel) + 1U == threadCount_) {
            arrived_.store(0U, std::memory_order_release);
            generation_.fetch_add(1U, std::memory_order_acq_rel);
            return;
        }
        while (generation_.load(std::memory_order_acquire) == generation) {
            std::this_thread::yield();
        }
    }

private:
    const size_t threadCount_;
    std::atomic<size_t> arrived_ {0U};
    std::atomic<size_t> generation_ {0U};
};

// Test: test-kind=api, api=GraphApiImpl::iSetLiteralArray, abc-kind=ArkTS1, category=positive, extension=c
TEST_F(LibAbcKitDynamicThreadSafetyTest, DynamicGraphArraySetterWithConcurrentArrayCreation)
{
    AbckitFile *file = nullptr;
    helpers::AssertOpenAbc(ABCKIT_ABC_DIR "ut/isa/isa_dynamic/arrays/create_array_dynamic.abc", &file);
    auto *function = helpers::FindMethodByName(file, "test");
    ASSERT_NE(function, nullptr);
    auto *graph = g_implI->createGraphFromFunction(function);
    ASSERT_NE(graph, nullptr);
    auto *literal = g_implM->createLiteralU32(file, 42U);
    auto *array = g_implM->createLiteralArray(file, &literal, 1U);
    ASSERT_NE(array, nullptr);
    auto *inst = g_dynG->iCreateCreatearraywithbuffer(graph, array);
    ASSERT_NE(inst, nullptr);
    ThreadBarrier barrier(2U);
    std::thread reader([&]() {
        barrier.Wait();
        for (size_t i = 0; i < 2000U; ++i) {
            g_implG->iSetLiteralArray(inst, array);
            EXPECT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
        }
    });
    std::thread writer([&]() {
        barrier.Wait();
        for (size_t i = 0; i < 2000U; ++i) {
            EXPECT_NE(g_implM->createLiteralArray(file, &literal, 1U), nullptr);
        }
    });
    reader.join();
    writer.join();
    EXPECT_EQ(g_implG->iGetLiteralArray(inst), array);
    g_impl->destroyGraph(graph);
    g_impl->closeFile(file);
}

// Test: test-kind=api, api=GraphApiImpl::iSetFunction, abc-kind=ArkTS1, category=positive, extension=c
TEST_F(LibAbcKitDynamicThreadSafetyTest, DynamicGraphFunctionSetterWithConcurrentFunctionCommit)
{
    AbckitFile *file = nullptr;
    helpers::AssertOpenAbc(ABCKIT_ABC_DIR "ut/ir_core/catchphi/catchphi_dynamic.abc", &file);
    auto *function = helpers::FindMethodByName(file, "main");
    ASSERT_NE(function, nullptr);
    auto *graph = g_implI->createGraphFromFunction(function);
    ASSERT_NE(graph, nullptr);
    auto *inst = g_dynG->iCreateDefinefunc(graph, function, 0U);
    ASSERT_NE(inst, nullptr);
    ThreadBarrier barrier(2U);
    std::atomic<bool> done {false};
    std::thread reader([&]() {
        barrier.Wait();
        do {
            g_implG->iSetFunction(inst, function);
            EXPECT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
        } while (!done.load(std::memory_order_acquire));
    });
    std::thread writer([&]() {
        barrier.Wait();
        for (size_t i = 0; i < 20U; ++i) {
            auto *otherGraph = g_implI->createGraphFromFunction(function);
            EXPECT_NE(otherGraph, nullptr);
            if (otherGraph == nullptr) {
                break;
            }
            g_implM->functionSetGraph(function, otherGraph);
            EXPECT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
            g_impl->destroyGraph(otherGraph);
        }
        done.store(true, std::memory_order_release);
    });
    reader.join();
    writer.join();
    EXPECT_EQ(g_implG->iGetFunction(inst), function);
    g_impl->destroyGraph(graph);
    g_impl->closeFile(file);
}

static void CheckCreateGraphDuringImportEnumeration(bool remove, size_t readerCount = 1U)
{
    const auto *jsInspect = AbckitGetJsInspectApiImpl(ABCKIT_VERSION_RELEASE_1_0_0);
    const auto *jsModify = AbckitGetJsModifyApiImpl(ABCKIT_VERSION_RELEASE_1_0_0);
    AbckitFile *file = nullptr;
    helpers::AssertOpenAbc(ABCKIT_ABC_DIR "ut/extensions/js/modify_api/modules/JSmodules_dynamic_modify.abc", &file);
    auto *function = helpers::FindMethodByName(file, "regularImportFunc1FromModule1");
    ASSERT_NE(function, nullptr);
    auto *module = function->owningModule;
    ASSERT_TRUE(module->id.empty());
    auto *importing = jsInspect->coreModuleToJsModule(module);
    AbckitJsExternalModuleCreateParams externalParams {"callback_test_external"};
    auto *imported = jsModify->fileAddExternalModule(file, &externalParams);
    ASSERT_NE(imported, nullptr);
    std::vector<AbckitCoreImportDescriptor *> expected;
    for (const char *name : {"first", "second"}) {
        AbckitJsImportFromDynamicModuleCreateParams params {name, name};
        auto *descriptor = jsModify->moduleAddImportFromJsToJs(importing, imported, &params);
        ASSERT_NE(descriptor, nullptr);
        expected.push_back(jsInspect->jsImportDescriptorToCoreImportDescriptor(descriptor));
    }
    struct Context {
        AbckitFile *file;
        AbckitCoreFunction *function;
        std::atomic<size_t> *entered;
        size_t readerCount;
        std::vector<AbckitCoreImportDescriptor *> observed;
    };
    std::atomic<size_t> entered {0U};
    std::vector<std::unique_ptr<Context>> contexts;
    for (size_t i = 0; i < readerCount; ++i) {
        contexts.emplace_back(new Context {file, function, &entered, readerCount, {}});
    }
    std::thread writer([&]() {
        while (entered.load(std::memory_order_acquire) < readerCount) {
            std::this_thread::yield();
        }
        if (remove) {
            jsModify->moduleRemoveImport(importing, jsInspect->coreImportDescriptorToJsImportDescriptor(expected[0]));
        } else {
            AbckitJsImportFromDynamicModuleCreateParams params {"third", "third"};
            EXPECT_NE(jsModify->moduleAddImportFromJsToJs(importing, imported, &params), nullptr);
        }
        EXPECT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    });
    auto callback = [](AbckitCoreImportDescriptor *descriptor, void *data) {
        auto *ctx = static_cast<Context *>(data);
        ctx->observed.push_back(descriptor);
        if (ctx->observed.size() == 1U) {
            ctx->entered->fetch_add(1U, std::memory_order_acq_rel);
            EXPECT_TRUE(WaitForActiveOperationCount(ctx->file->threadState, ctx->readerCount + 1U));
            auto *graph = g_implI->createGraphFromFunction(ctx->function);
            EXPECT_NE(graph, nullptr);
            EXPECT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
            if (graph != nullptr) {
                g_impl->destroyGraph(graph);
            }
        }
        // Another thread must not delete even the descriptor currently in the callback.
        EXPECT_NE(descriptor, nullptr);
        if (descriptor != nullptr) {
            EXPECT_NE(g_implI->importDescriptorGetName(descriptor), nullptr);
        }
        return true;
    };
    std::vector<std::thread> readers;
    for (const auto &context : contexts) {
        readers.emplace_back(
            [&, ctx = context.get()]() { EXPECT_TRUE(g_implI->moduleEnumerateImports(module, ctx, callback)); });
    }
    for (auto &reader : readers) {
        reader.join();
    }
    writer.join();
    for (const auto &context : contexts) {
        EXPECT_EQ(context->observed, expected);
    }
    EXPECT_EQ(module->id.size(), remove ? 1U : 3U);
    EXPECT_EQ(file->threadState->GetActiveOperationCount(), 0U);
    g_impl->closeFile(file);
    EXPECT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
}

// Test: test-kind=api, api=InspectApiImpl::moduleEnumerateImports, abc-kind=JS, category=positive, extension=c
TEST_F(LibAbcKitDynamicThreadSafetyTest, DynamicCallbackCreateGraphWithConcurrentImportInsertion)
{
    CheckCreateGraphDuringImportEnumeration(false);
}

// Test: test-kind=api, api=InspectApiImpl::moduleEnumerateImports, abc-kind=JS, category=positive, extension=c
TEST_F(LibAbcKitDynamicThreadSafetyTest, DynamicCallbackCreateGraphWithConcurrentImportRemoval)
{
    CheckCreateGraphDuringImportEnumeration(true);
}

// Test: test-kind=api, api=InspectApiImpl::moduleEnumerateImports, abc-kind=JS, category=positive, extension=c
TEST_F(LibAbcKitDynamicThreadSafetyTest, DynamicConcurrentCallbacksCreateFirstSnapshotWithImportInsertion)
{
    CheckCreateGraphDuringImportEnumeration(false, 2U);
}

// Test: test-kind=internal, api=AbckitFileApiReadLockSuspension, abc-kind=ArkTS1, category=positive, extension=c
TEST_F(LibAbcKitDynamicThreadSafetyTest, DynamicNestedSuspensionsKeepStructuralWriterWaiting)
{
    AbckitFile *file = nullptr;
    helpers::AssertOpenAbc(ABCKIT_ABC_DIR "internal/implementation_api/abc_dynamic.abc", &file);
    auto *function = helpers::FindMethodByName(file, "foo");
    ASSERT_NE(function, nullptr);
    auto state = file->threadState;
    std::promise<void> complete;
    auto future = complete.get_future();
    std::thread writer;
    {
        AbckitFileApiReadLock read(file);
        AbckitFileApiReadLockSuspension outer(file);
        {
            AbckitFileApiReadLockSuspension inner(file);
            EXPECT_EQ(state->suspendedReaders, 1U);
        }
        EXPECT_EQ(state->suspendedReaders, 1U);
        writer = std::thread([&]() {
            AbckitFileApiWriteLock write(file, true, AbckitFileApiWriteMode::STRUCTURAL);
            EXPECT_FALSE(write.HasConflict());
            complete.set_value();
        });
        EXPECT_TRUE(WaitForActiveOperationCount(state, 2U));
        EXPECT_EQ(future.wait_for(std::chrono::milliseconds(50)), std::future_status::timeout);
        auto *graph = g_implI->createGraphFromFunction(function);
        EXPECT_NE(graph, nullptr);
        if (graph != nullptr) {
            g_impl->destroyGraph(graph);
        }
    }
    writer.join();
    EXPECT_EQ(state->suspendedReaders, 0U);
    EXPECT_EQ(state->GetActiveOperationCount(), 0U);
    g_impl->closeFile(file);
    EXPECT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
}

// Test: test-kind=internal, api=AbckitFileApiWriteLock, abc-kind=ArkTS1, category=negative, extension=c
TEST_F(LibAbcKitDynamicThreadSafetyTest, DynamicSuspendedStructuralUpgradeConflictReleasesOperations)
{
    AbckitFile *file = nullptr;
    helpers::AssertOpenAbc(ABCKIT_ABC_DIR "internal/implementation_api/abc_dynamic.abc", &file);
    auto state = file->threadState;
    std::atomic<bool> suspended {false};
    std::atomic<bool> resume {false};
    std::thread other([&]() {
        AbckitFileApiReadLock read(file);
        AbckitFileApiReadLockSuspension suspension(file);
        suspended.store(true, std::memory_order_release);
        while (!resume.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
    });
    while (!suspended.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
    {
        AbckitFileApiReadLock read(file);
        AbckitFileApiReadLockSuspension suspension(file);
        const auto operations = state->GetActiveOperationCount();
        {
            AbckitFileApiWriteLock write(file, true, AbckitFileApiWriteMode::STRUCTURAL);
            EXPECT_TRUE(write.HasConflict());
        }
        EXPECT_EQ(state->GetActiveOperationCount(), operations);
        {
            AbckitFileApiWriteLock outerWrite(file);
            EXPECT_FALSE(outerWrite.HasConflict());
            AbckitFileApiWriteLock nestedWrite(file, true, AbckitFileApiWriteMode::STRUCTURAL);
            EXPECT_TRUE(nestedWrite.HasConflict());
        }
        EXPECT_EQ(state->GetActiveOperationCount(), operations);
        resume.store(true, std::memory_order_release);
    }
    other.join();
    EXPECT_EQ(state->suspendedReaders, 0U);
    EXPECT_EQ(state->GetActiveOperationCount(), 0U);
    g_impl->closeFile(file);
    EXPECT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
}

// Test: test-kind=api, api=GraphApiImpl::iGetLiteralArray, abc-kind=ArkTS1, category=positive, extension=c
TEST_F(LibAbcKitDynamicThreadSafetyTest, DynamicGraphLiteralArrayGettersShareCacheConcurrently)
{
    AbckitFile *file = nullptr;
    helpers::AssertOpenAbc(ABCKIT_ABC_DIR "ut/isa/isa_dynamic/arrays/create_array_dynamic.abc", &file);
    auto *function = helpers::FindMethodByName(file, "test");
    ASSERT_NE(function, nullptr);
    constexpr size_t WORKERS = 4;
    std::array<AbckitGraph *, WORKERS> graphs {};
    std::array<AbckitInst *, WORKERS> instructions {};
    std::array<AbckitLiteralArray *, WORKERS> results {};
    for (size_t i = 0; i < WORKERS; ++i) {
        graphs[i] = g_implI->createGraphFromFunction(function);
        ASSERT_NE(graphs[i], nullptr);
        instructions[i] = helpers::FindFirstInst(graphs[i], ABCKIT_ISA_API_DYNAMIC_OPCODE_CREATEARRAYWITHBUFFER);
        ASSERT_NE(instructions[i], nullptr);
    }
    const auto initialCacheSize = file->litarrs.size();
    ThreadBarrier barrier(WORKERS);
    std::array<std::thread, WORKERS> workers;
    for (size_t i = 0; i < WORKERS; ++i) {
        workers[i] = std::thread([&, i]() {
            barrier.Wait();
            results[i] = g_implG->iGetLiteralArray(instructions[i]);
            EXPECT_NE(results[i], nullptr);
            EXPECT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
            constexpr size_t ITERATIONS = 100;
            for (size_t j = 0; j < ITERATIONS; ++j) {
                EXPECT_EQ(g_implG->iGetLiteralArray(instructions[i]), results[i]);
                EXPECT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
            }
        });
    }
    for (auto &worker : workers) {
        worker.join();
    }
    EXPECT_EQ(file->litarrs.size(), initialCacheSize + 1U);
    for (size_t i = 0; i < WORKERS; ++i) {
        EXPECT_EQ(results[i], results[0]);
        g_impl->destroyGraph(graphs[i]);
    }
    g_impl->closeFile(file);
}

static void CheckConcurrentGraphStringGetter(bool lazyLiteralStrings)
{
    AbckitFile *file = nullptr;
    helpers::AssertOpenAbc(ABCKIT_ABC_DIR "ut/ir_core/string/string_dynamic.abc", &file);
    auto *function = helpers::FindMethodByName(file, "foo");
    ASSERT_NE(function, nullptr);
    auto *graph = g_implI->createGraphFromFunction(function);
    ASSERT_NE(graph, nullptr);
    auto *inst = helpers::FindFirstInst(graph, ABCKIT_ISA_API_DYNAMIC_OPCODE_LOADSTRING);
    ASSERT_NE(inst, nullptr);
    auto *expected = g_implG->iGetString(inst);
    ASSERT_NE(expected, nullptr);
    ThreadBarrier barrier(2);
    constexpr size_t ITERATIONS = 2000;
    std::vector<AbckitLiteral *> literals;
    if (lazyLiteralStrings) {
        for (size_t i = 0; i < ITERATIONS; ++i) {
            auto value = "concurrent-lazy-string-" + std::to_string(i);
            auto *literal = g_implM->createLiteralString(file, value.data(), value.size());
            ASSERT_NE(literal, nullptr);
            ASSERT_EQ(file->strings.count(value), 0U);
            literals.push_back(literal);
        }
    }
    std::thread reader([&]() {
        barrier.Wait();
        for (size_t i = 0; i < ITERATIONS; ++i) {
            EXPECT_EQ(g_implG->iGetString(inst), expected);
            EXPECT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
        }
    });
    std::thread writer([&]() {
        barrier.Wait();
        for (size_t i = 0; i < ITERATIONS; ++i) {
            if (lazyLiteralStrings) {
                EXPECT_NE(g_implI->literalGetString(literals[i]), nullptr);
            } else {
                auto value = "concurrent-graph-string-" + std::to_string(i);
                EXPECT_NE(g_implM->createString(file, value.data(), value.size()), nullptr);
            }
            EXPECT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
        }
    });
    reader.join();
    writer.join();
    g_impl->destroyGraph(graph);
    g_impl->closeFile(file);
}

// Test: test-kind=api, api=GraphApiImpl::iGetString, abc-kind=ArkTS1, category=positive, extension=c
TEST_F(LibAbcKitDynamicThreadSafetyTest, DynamicGraphStringGetterWithConcurrentStringCreation)
{
    CheckConcurrentGraphStringGetter(false);
}

// Test: test-kind=api, api=GraphApiImpl::iGetString, abc-kind=ArkTS1, category=positive, extension=c
TEST_F(LibAbcKitDynamicThreadSafetyTest, DynamicGraphStringGetterWithConcurrentLiteralStringInspection)
{
    CheckConcurrentGraphStringGetter(true);
}

// Test: test-kind=api, api=IsaApiDynamicImpl, abc-kind=ArkTS1, category=positive, extension=c
TEST_F(LibAbcKitDynamicThreadSafetyTest, DynamicIsaPublicTableHasAll199Entries)
{
    ASSERT_NE(g_dynG, nullptr);
    const auto *tableBytes = reinterpret_cast<const unsigned char *>(g_dynG);
    for (size_t slotIndex = 0; slotIndex < 199U; ++slotIndex) {
        DynamicIsaApiSlot slot = nullptr;
        std::memcpy(&slot, tableBytes + slotIndex * sizeof(slot), sizeof(slot));
        EXPECT_NE(slot, nullptr) << "Dynamic ISA API slot " << slotIndex << " is not initialized";
    }
}

// Test: test-kind=api, api=IsaApiDynamicImpl::iCreateCreatearraywithbuffer, abc-kind=ArkTS1, category=positive,
// extension=c
TEST_F(LibAbcKitDynamicThreadSafetyTest, DynamicIsaMutableMetadataCreateUsesFileReadLock)
{
    constexpr auto INPUT_PATH = ABCKIT_ABC_DIR "internal/implementation_api/abc_dynamic.abc";
    AbckitFile *file = nullptr;
    helpers::AssertOpenAbc(INPUT_PATH, &file);
    auto *function = helpers::FindMethodByName(file, "foo");
    ASSERT_NE(function, nullptr);
    auto *string = g_implM->createString(file, "isa-read-lock", strlen("isa-read-lock"));
    ASSERT_NE(string, nullptr);
    auto *arrayElement = g_implM->createLiteralString(file, "isa-read-lock", strlen("isa-read-lock"));
    ASSERT_NE(arrayElement, nullptr);
    std::array<AbckitLiteral *, 1U> arrayElements {arrayElement};
    auto *literalArray = g_implM->createLiteralArray(file, arrayElements.data(), arrayElements.size());
    ASSERT_NE(literalArray, nullptr);
    auto *graph = g_implI->createGraphFromFunction(function);
    ASSERT_NE(graph, nullptr);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);

    std::atomic<bool> completed {false};
    AbckitInst *created = nullptr;
    AbckitStatus createStatus = ABCKIT_STATUS_NO_ERROR;
    std::thread creator;
    {
        AbckitFileApiWriteLock writeLock(file);
        ASSERT_FALSE(writeLock.HasConflict());
        creator = std::thread([&]() {
            created = g_dynG->iCreateCreatearraywithbuffer(graph, literalArray);
            createStatus = g_impl->getLastError();
            completed.store(true, std::memory_order_release);
        });
        EXPECT_TRUE(WaitForActiveOperationCount(file->threadState, 2U));
        EXPECT_FALSE(completed.load(std::memory_order_acquire));
    }
    creator.join();

    ASSERT_TRUE(completed.load(std::memory_order_acquire));
    ASSERT_NE(created, nullptr);
    ASSERT_EQ(createStatus, ABCKIT_STATUS_NO_ERROR);

    g_impl->destroyGraph(graph);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    g_impl->closeFile(file);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
}

// Test: test-kind=api, api=IsaApiDynamicImpl::iCreateDefinefunc, abc-kind=ArkTS1, category=positive, extension=c
TEST_F(LibAbcKitDynamicThreadSafetyTest, DynamicIsaFunctionCreateUsesFileReadLock)
{
    constexpr auto INPUT_PATH = ABCKIT_ABC_DIR "internal/implementation_api/abc_dynamic.abc";
    AbckitFile *file = nullptr;
    helpers::AssertOpenAbc(INPUT_PATH, &file);
    auto *function = helpers::FindMethodByName(file, "foo");
    ASSERT_NE(function, nullptr);
    auto *graph = g_implI->createGraphFromFunction(function);
    ASSERT_NE(graph, nullptr);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);

    std::atomic<bool> completed {false};
    AbckitInst *created = nullptr;
    AbckitStatus createStatus = ABCKIT_STATUS_NO_ERROR;
    std::thread creator;
    {
        AbckitFileApiWriteLock writeLock(file);
        ASSERT_FALSE(writeLock.HasConflict());
        creator = std::thread([&]() {
            created = g_dynG->iCreateDefinefunc(graph, function, 0U);
            createStatus = g_impl->getLastError();
            completed.store(true, std::memory_order_release);
        });
        EXPECT_TRUE(WaitForActiveOperationCount(file->threadState, 2U));
        EXPECT_FALSE(completed.load(std::memory_order_acquire));
    }
    creator.join();

    ASSERT_TRUE(completed.load(std::memory_order_acquire));
    ASSERT_NE(created, nullptr);
    ASSERT_EQ(createStatus, ABCKIT_STATUS_NO_ERROR);
    g_impl->destroyGraph(graph);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    g_impl->closeFile(file);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
}

// Test: test-kind=api, api=IsaApiDynamicImpl::iCreateGetmodulenamespace, abc-kind=ArkTS1, category=positive,
// extension=c
TEST_F(LibAbcKitDynamicThreadSafetyTest, DynamicIsaModuleCreateUsesFileReadLock)
{
    constexpr auto INPUT_PATH = ABCKIT_ABC_DIR "ut/isa/isa_dynamic/modules/isa_dynamic_modules.abc";
    AbckitFile *file = nullptr;
    helpers::AssertOpenAbc(INPUT_PATH, &file);
    auto *function = helpers::FindMethodByName(file, "isa_dynamic_modules.func_main_0");
    ASSERT_NE(function, nullptr);
    helpers::ModuleByNameContext moduleFinder = {nullptr, "modules/module1"};
    g_implI->fileEnumerateModules(file, &moduleFinder, helpers::ModuleByNameFinder);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    ASSERT_NE(moduleFinder.module, nullptr);
    auto *graph = g_implI->createGraphFromFunction(function);
    ASSERT_NE(graph, nullptr);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);

    std::atomic<bool> completed {false};
    AbckitInst *created = nullptr;
    AbckitStatus createStatus = ABCKIT_STATUS_NO_ERROR;
    std::thread creator;
    {
        AbckitFileApiWriteLock writeLock(file);
        ASSERT_FALSE(writeLock.HasConflict());
        creator = std::thread([&]() {
            created = g_dynG->iCreateGetmodulenamespace(graph, moduleFinder.module);
            createStatus = g_impl->getLastError();
            completed.store(true, std::memory_order_release);
        });
        EXPECT_TRUE(WaitForActiveOperationCount(file->threadState, 2U));
        EXPECT_FALSE(completed.load(std::memory_order_acquire));
    }
    creator.join();

    ASSERT_TRUE(completed.load(std::memory_order_acquire));
    ASSERT_NE(created, nullptr);
    ASSERT_EQ(createStatus, ABCKIT_STATUS_NO_ERROR);
    g_impl->destroyGraph(graph);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    g_impl->closeFile(file);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
}

// Test: test-kind=api, api=IsaApiDynamicImpl::iCreateLoadString, abc-kind=ArkTS1, category=positive, extension=c
TEST_F(LibAbcKitDynamicThreadSafetyTest, DynamicIsaImmutableStringCreateDoesNotUseFileLock)
{
    constexpr auto INPUT_PATH = ABCKIT_ABC_DIR "internal/implementation_api/abc_dynamic.abc";
    AbckitFile *file = nullptr;
    helpers::AssertOpenAbc(INPUT_PATH, &file);
    auto *function = helpers::FindMethodByName(file, "foo");
    ASSERT_NE(function, nullptr);
    auto *string = g_implM->createString(file, "immutable-isa-string", strlen("immutable-isa-string"));
    ASSERT_NE(string, nullptr);
    auto *graph = g_implI->createGraphFromFunction(function);
    ASSERT_NE(graph, nullptr);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);

    std::atomic<bool> started {false};
    std::atomic<bool> completed {false};
    AbckitInst *created = nullptr;
    AbckitStatus createStatus = ABCKIT_STATUS_NO_ERROR;
    std::thread creator;
    {
        AbckitFileApiWriteLock writeLock(file);
        ASSERT_FALSE(writeLock.HasConflict());
        creator = std::thread([&]() {
            started.store(true, std::memory_order_release);
            created = g_dynG->iCreateLoadString(graph, string);
            createStatus = g_impl->getLastError();
            completed.store(true, std::memory_order_release);
        });
        EXPECT_TRUE(WaitForWorkerCompletion(started, completed));
    }
    creator.join();

    ASSERT_NE(created, nullptr);
    ASSERT_EQ(createStatus, ABCKIT_STATUS_NO_ERROR);
    g_impl->destroyGraph(graph);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    g_impl->closeFile(file);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
}

// Test: test-kind=api, api=IsaApiDynamicImpl::iCreateLdundefined, abc-kind=ArkTS1, category=positive, extension=c
TEST_F(LibAbcKitDynamicThreadSafetyTest, DynamicIsaGraphLocalCreateDoesNotUseFileLock)
{
    constexpr auto INPUT_PATH = ABCKIT_ABC_DIR "internal/implementation_api/abc_dynamic.abc";
    AbckitFile *file = nullptr;
    helpers::AssertOpenAbc(INPUT_PATH, &file);
    auto *function = helpers::FindMethodByName(file, "foo");
    ASSERT_NE(function, nullptr);
    auto *graph = g_implI->createGraphFromFunction(function);
    ASSERT_NE(graph, nullptr);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);

    std::atomic<bool> started {false};
    std::atomic<bool> completed {false};
    AbckitInst *created = nullptr;
    AbckitStatus createStatus = ABCKIT_STATUS_NO_ERROR;
    std::thread creator;
    {
        AbckitFileApiWriteLock writeLock(file);
        ASSERT_FALSE(writeLock.HasConflict());
        creator = std::thread([&]() {
            started.store(true, std::memory_order_release);
            created = g_dynG->iCreateLdundefined(graph);
            createStatus = g_impl->getLastError();
            completed.store(true, std::memory_order_release);
        });
        EXPECT_TRUE(WaitForWorkerCompletion(started, completed));
    }
    creator.join();

    ASSERT_NE(created, nullptr);
    ASSERT_EQ(createStatus, ABCKIT_STATUS_NO_ERROR);
    g_impl->destroyGraph(graph);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    g_impl->closeFile(file);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
}

// Test: test-kind=api, api=InspectApiImpl::createGraphFromFunction, abc-kind=ArkTS1, category=positive, extension=c
TEST_F(LibAbcKitDynamicThreadSafetyTest, DynamicCreateGraphConcurrentFirstSnapshot)
{
    constexpr auto INPUT_PATH = ABCKIT_ABC_DIR "internal/implementation_api/abc_dynamic.abc";
    AbckitFile *file = nullptr;
    helpers::AssertOpenAbc(INPUT_PATH, &file);

    auto *function = helpers::FindMethodByName(file, "foo");
    ASSERT_NE(function, nullptr);

    constexpr size_t THREAD_COUNT = 8;
    std::array<AbckitGraph *, THREAD_COUNT> graphs {};
    std::array<AbckitStatus, THREAD_COUNT> statuses {};
    std::atomic<bool> start {false};
    std::vector<std::thread> threads;
    threads.reserve(THREAD_COUNT);

    for (size_t i = 0; i < THREAD_COUNT; ++i) {
        threads.emplace_back([&, i]() {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            graphs[i] = g_implI->createGraphFromFunction(function);
            statuses[i] = g_impl->getLastError();
        });
    }

    start.store(true, std::memory_order_release);
    for (auto &thread : threads) {
        thread.join();
    }

    std::unordered_set<AbckitGraph *> uniqueGraphs;
    for (size_t i = 0; i < THREAD_COUNT; ++i) {
        ASSERT_EQ(statuses[i], ABCKIT_STATUS_NO_ERROR);
        ASSERT_NE(graphs[i], nullptr);
        uniqueGraphs.insert(graphs[i]);
    }
    ASSERT_EQ(uniqueGraphs.size(), THREAD_COUNT);
    ASSERT_EQ(file->dynamicGraphCache.GetSuccessfulBuildCount(), 1U);

    for (auto *graph : graphs) {
        g_impl->destroyGraph(graph);
        ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    }

    g_impl->closeFile(file);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
}

// Test: test-kind=api, api=InspectApiImpl::createGraphFromFunction, abc-kind=ArkTS1, category=positive, extension=c
TEST_F(LibAbcKitDynamicThreadSafetyTest, DynamicCreateGraphConcurrentFirstSnapshotAcrossFiles)
{
    constexpr auto INPUT_PATH = ABCKIT_ABC_DIR "internal/implementation_api/abc_dynamic.abc";
    constexpr size_t FILE_COUNT = 2;
    constexpr size_t ITERATIONS_PER_FILE = 4;
    std::array<AbckitFile *, FILE_COUNT> files {};
    std::array<AbckitCoreFunction *, FILE_COUNT> functions {};
    std::array<bool, FILE_COUNT> succeeded {true, true};

    for (size_t i = 0; i < FILE_COUNT; ++i) {
        helpers::AssertOpenAbc(INPUT_PATH, &files[i]);
        functions[i] = helpers::FindMethodByName(files[i], "foo");
        ASSERT_NE(functions[i], nullptr);
    }

    std::atomic<bool> start {false};
    std::vector<std::thread> threads;
    threads.reserve(FILE_COUNT);
    for (size_t i = 0; i < FILE_COUNT; ++i) {
        threads.emplace_back([&, i]() {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            for (size_t iteration = 0; iteration < ITERATIONS_PER_FILE; ++iteration) {
                auto *graph = g_implI->createGraphFromFunction(functions[i]);
                if (graph == nullptr || g_impl->getLastError() != ABCKIT_STATUS_NO_ERROR) {
                    succeeded[i] = false;
                    return;
                }
                g_impl->destroyGraph(graph);
                if (g_impl->getLastError() != ABCKIT_STATUS_NO_ERROR) {
                    succeeded[i] = false;
                    return;
                }
            }
        });
    }

    start.store(true, std::memory_order_release);
    for (auto &thread : threads) {
        thread.join();
    }

    for (size_t i = 0; i < FILE_COUNT; ++i) {
        ASSERT_TRUE(succeeded[i]);
        ASSERT_EQ(files[i]->dynamicGraphCache.GetSuccessfulBuildCount(), 1U);
        g_impl->closeFile(files[i]);
        ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    }
}

// Test: test-kind=api, api=InspectApiImpl::createGraphFromFunction, abc-kind=ArkTS1, category=positive, extension=c
TEST_F(LibAbcKitDynamicThreadSafetyTest, DynamicGraphsShareSnapshotLookupsAndIsolateDebugMaps)
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

    ASSERT_EQ(&firstGraph->irInterface->GetMethods(), &secondGraph->irInterface->GetMethods());
    ASSERT_EQ(&firstGraph->irInterface->GetFields(), &secondGraph->irInterface->GetFields());
    ASSERT_EQ(&firstGraph->irInterface->GetClasses(), &secondGraph->irInterface->GetClasses());
    ASSERT_EQ(&firstGraph->irInterface->GetStrings(), &secondGraph->irInterface->GetStrings());
    ASSERT_EQ(&firstGraph->irInterface->GetLiteralArrays(), &secondGraph->irInterface->GetLiteralArrays());

    constexpr size_t PC = 7;
    constexpr size_t LINE = 11;
    constexpr uint32_t COLUMN = 13;
    firstGraph->irInterface->SetPcToLineColumnForCurrentMethod({{PC, {LINE, COLUMN}}});
    size_t line = 0;
    uint32_t column = 0;
    ASSERT_TRUE(firstGraph->irInterface->TryGetLineColumnByPc(PC, &line, &column));
    ASSERT_EQ(line, LINE);
    ASSERT_EQ(column, COLUMN);
    ASSERT_FALSE(secondGraph->irInterface->TryGetLineColumnByPc(PC, &line, &column));

    g_impl->destroyGraph(firstGraph);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    g_impl->destroyGraph(secondGraph);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    g_impl->closeFile(file);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
}

// Test: test-kind=api, api=ModifyApiImpl::functionSetGraph, abc-kind=ArkTS1, category=positive, extension=c

}  // namespace libabckit::test
