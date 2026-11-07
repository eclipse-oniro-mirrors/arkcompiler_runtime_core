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
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <vector>

#include <gtest/gtest.h>

#include "helpers/helpers.h"
#include "helpers/helpers_runtime.h"
#include "libabckit/tests/scenarios/thread_safety/common/scenario_harness.h"
#include "libabckit/c/abckit.h"
#include "libabckit/c/extensions/arkts/metadata_arkts.h"
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
static auto g_arktsInspect = AbckitGetArktsInspectApiImpl(ABCKIT_VERSION_RELEASE_1_0_0);
static auto g_jsInspect = AbckitGetJsInspectApiImpl(ABCKIT_VERSION_RELEASE_1_0_0);
static auto g_jsModify = AbckitGetJsModifyApiImpl(ABCKIT_VERSION_RELEASE_1_0_0);

class LibAbcKitDynamicThreadSafetyTest : public ::testing::Test {};

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

struct ArktsExtensionReadState {
    AbckitCoreFunction *function;
    AbckitCoreModule *module;
    bool isNative;
};

struct JsExtensionReadState {
    AbckitCoreFunction *function;
    AbckitCoreModule *module;
};

static bool InitializeArktsExtensionReadState(AbckitFile *file, ArktsExtensionReadState *state)
{
    state->function = helpers::FindMethodByName(file, "bar");
    if (state->function == nullptr) {
        return false;
    }
    state->module = g_implI->functionGetModule(state->function);
    if (state->module == nullptr || g_impl->getLastError() != ABCKIT_STATUS_NO_ERROR) {
        return false;
    }
    auto *arktsFunction = g_arktsInspect->coreFunctionToArktsFunction(state->function);
    if (arktsFunction == nullptr || g_impl->getLastError() != ABCKIT_STATUS_NO_ERROR) {
        return false;
    }
    state->isNative = g_arktsInspect->functionIsNative(arktsFunction);
    return g_impl->getLastError() == ABCKIT_STATUS_NO_ERROR;
}

static bool CheckArktsExtensionRead(const ArktsExtensionReadState &state)
{
    auto *arktsFunction = g_arktsInspect->coreFunctionToArktsFunction(state.function);
    if (arktsFunction == nullptr || g_impl->getLastError() != ABCKIT_STATUS_NO_ERROR ||
        g_arktsInspect->arktsFunctionToCoreFunction(arktsFunction) != state.function ||
        g_impl->getLastError() != ABCKIT_STATUS_NO_ERROR) {
        return false;
    }
    auto *arktsModule = g_arktsInspect->coreModuleToArktsModule(state.module);
    if (arktsModule == nullptr || g_impl->getLastError() != ABCKIT_STATUS_NO_ERROR ||
        g_arktsInspect->arktsModuleToCoreModule(arktsModule) != state.module ||
        g_impl->getLastError() != ABCKIT_STATUS_NO_ERROR) {
        return false;
    }
    return g_arktsInspect->functionIsNative(arktsFunction) == state.isNative &&
           g_impl->getLastError() == ABCKIT_STATUS_NO_ERROR;
}

static bool InitializeJsExtensionReadState(AbckitFile *file, JsExtensionReadState *state)
{
    state->function = helpers::FindMethodByName(file, "foo");
    if (state->function == nullptr) {
        return false;
    }
    state->module = g_implI->functionGetModule(state->function);
    return state->module != nullptr && g_impl->getLastError() == ABCKIT_STATUS_NO_ERROR;
}

static bool CheckJsExtensionRead(const JsExtensionReadState &state)
{
    auto *jsFunction = g_jsInspect->coreFunctionToJsFunction(state.function);
    if (jsFunction == nullptr || g_impl->getLastError() != ABCKIT_STATUS_NO_ERROR) {
        return false;
    }
    auto *coreFunction = g_jsInspect->jsFunctionToCoreFunction(jsFunction);
    if (coreFunction != state.function || g_impl->getLastError() != ABCKIT_STATUS_NO_ERROR) {
        return false;
    }
    auto *jsModule = g_jsInspect->coreModuleToJsModule(state.module);
    if (jsModule == nullptr || g_impl->getLastError() != ABCKIT_STATUS_NO_ERROR ||
        g_jsInspect->jsModuleToCoreModule(jsModule) != state.module ||
        g_impl->getLastError() != ABCKIT_STATUS_NO_ERROR) {
        return false;
    }
    auto *name = g_implI->functionGetName(coreFunction);
    if (name == nullptr || g_impl->getLastError() != ABCKIT_STATUS_NO_ERROR) {
        return false;
    }
    auto *value = g_implI->abckitStringToString(name);
    return value != nullptr && g_impl->getLastError() == ABCKIT_STATUS_NO_ERROR && std::string_view(value) == "foo";
}

template <class Check>
static bool RunConcurrentExtensionReads(Check check)
{
    constexpr size_t THREAD_COUNT = 4U;
    constexpr size_t ITERATION_COUNT = 64U;
    struct Result {
        AbckitStatus status = ABCKIT_STATUS_NO_ERROR;
    };
    auto run = scenario::RunShards<Result>(THREAD_COUNT, THREAD_COUNT, [&](const auto & /*shard*/) {
        Result result;
        for (size_t iteration = 0; iteration < ITERATION_COUNT; ++iteration) {
            if (!check()) {
                result.status = ABCKIT_STATUS_INTERNAL_ERROR;
                break;
            }
        }
        return result;
    });
    return run.status == ABCKIT_STATUS_NO_ERROR;
}

static void CheckArktsExtensionConcurrentReadAndReopen()
{
    constexpr auto INPUT = ABCKIT_ABC_DIR "ut/extensions/arkts/modify_api/annotations/annotations_dynamic.abc";
    AbckitFile *rawFile = nullptr;
    helpers::AssertOpenAbc(INPUT, &rawFile);
    ASSERT_NE(rawFile, nullptr);
    scenario::ScopedFile file(rawFile);
    ArktsExtensionReadState state {};
    ASSERT_TRUE(InitializeArktsExtensionReadState(file.Get(), &state));
    ASSERT_TRUE(RunConcurrentExtensionReads([&]() { return CheckArktsExtensionRead(state); }));
    const bool isNative = state.isNative;
    ASSERT_EQ(file.Close(), ABCKIT_STATUS_NO_ERROR);

    AbckitFile *rawReopenedFile = nullptr;
    helpers::AssertOpenAbc(INPUT, &rawReopenedFile);
    ASSERT_NE(rawReopenedFile, nullptr);
    scenario::ScopedFile reopenedFile(rawReopenedFile);
    ArktsExtensionReadState reopenedState {};
    ASSERT_TRUE(InitializeArktsExtensionReadState(reopenedFile.Get(), &reopenedState));
    ASSERT_EQ(reopenedState.isNative, isNative);
    ASSERT_TRUE(CheckArktsExtensionRead(reopenedState));
    ASSERT_EQ(reopenedFile.Close(), ABCKIT_STATUS_NO_ERROR);
}

static void CheckJsExtensionConcurrentReadAndReopen()
{
    constexpr auto INPUT = ABCKIT_ABC_DIR "ut/extensions/js/inspect_api/api_casts/JSapi_casts.abc";
    AbckitFile *rawFile = nullptr;
    helpers::AssertOpenAbc(INPUT, &rawFile);
    ASSERT_NE(rawFile, nullptr);
    scenario::ScopedFile file(rawFile);
    JsExtensionReadState state {};
    ASSERT_TRUE(InitializeJsExtensionReadState(file.Get(), &state));
    ASSERT_TRUE(RunConcurrentExtensionReads([&]() { return CheckJsExtensionRead(state); }));
    ASSERT_EQ(file.Close(), ABCKIT_STATUS_NO_ERROR);

    AbckitFile *rawReopenedFile = nullptr;
    helpers::AssertOpenAbc(INPUT, &rawReopenedFile);
    ASSERT_NE(rawReopenedFile, nullptr);
    scenario::ScopedFile reopenedFile(rawReopenedFile);
    JsExtensionReadState reopenedState {};
    ASSERT_TRUE(InitializeJsExtensionReadState(reopenedFile.Get(), &reopenedState));
    ASSERT_TRUE(CheckJsExtensionRead(reopenedState));
    ASSERT_EQ(reopenedFile.Close(), ABCKIT_STATUS_NO_ERROR);
}

// Test: test-kind=api, api=ArktsInspectApiImpl::coreFunctionToArktsFunction, abc-kind=ArkTS1, category=positive,
// extension=c
TEST_F(LibAbcKitDynamicThreadSafetyTest, DynamicExtensionInspectConversionAndPropertyConcurrentRead)
{
    ASSERT_NO_FATAL_FAILURE(CheckArktsExtensionConcurrentReadAndReopen());
    ASSERT_NO_FATAL_FAILURE(CheckJsExtensionConcurrentReadAndReopen());
}

static AbckitCoreAnnotation *FindValueTestAnnotation(AbckitFile *file, bool classOwner)
{
    auto *function = helpers::FindMethodByName(file, "bar");
    if (function == nullptr) {
        return nullptr;
    }
    AbckitCoreAnnotation *annotation = nullptr;
    auto callback = [](AbckitCoreAnnotation *anno, void *data) {
        *static_cast<AbckitCoreAnnotation **>(data) = anno;
        return false;
    };
    if (classOwner) {
        auto *klass = g_implI->functionGetParentClass(function);
        g_implI->classEnumerateAnnotations(klass, &annotation, callback);
    } else {
        g_implI->functionEnumerateAnnotations(function, &annotation, callback);
    }
    return annotation;
}

static void CheckAnnotationValueConcurrency(bool classOwner)
{
    auto *arkInspect = AbckitGetArktsInspectApiImpl(ABCKIT_VERSION_RELEASE_1_0_0);
    auto *arkModify = AbckitGetArktsModifyApiImpl(ABCKIT_VERSION_RELEASE_1_0_0);
    AbckitFile *file = nullptr;
    helpers::AssertOpenAbc(ABCKIT_ABC_DIR "ut/extensions/arkts/modify_api/annotations/annotations_dynamic.abc", &file);
    auto *annotation = FindValueTestAnnotation(file, classOwner);
    ASSERT_NE(annotation, nullptr);
    auto *arkAnnotation = arkInspect->coreAnnotationToArktsAnnotation(annotation);
    ASSERT_NE(arkAnnotation, nullptr);

    AbckitCoreAnnotationElement *initialElement = nullptr;
    g_implI->annotationEnumerateElements(
        annotation, &initialElement, [](AbckitCoreAnnotationElement *elem, void *data) {
            if (helpers::AbckitStringToString(g_implI->annotationElementGetName(elem)) == "a") {
                *static_cast<AbckitCoreAnnotationElement **>(data) = elem;
                return false;
            }
            return true;
        });
    ASSERT_NE(initialElement, nullptr);
    arkModify->annotationRemoveAnnotationElement(
        arkAnnotation, arkInspect->coreAnnotationElementToArktsAnnotationElement(initialElement));
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    constexpr double VALUE = 0.12;
    auto *value = g_implM->createValueDouble(file, VALUE);
    ASSERT_NE(value, nullptr);

    ThreadBarrier barrier(2U);
    constexpr size_t ITERATIONS = 2000U;
    std::thread reader([&]() {
        barrier.Wait();
        for (size_t i = 0; i < ITERATIONS; ++i) {
            EXPECT_DOUBLE_EQ(g_implI->valueGetDouble(value), VALUE);
            EXPECT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
        }
    });
    std::thread writer([&]() {
        barrier.Wait();
        for (size_t i = 0; i < ITERATIONS; ++i) {
            AbckitArktsAnnotationElementCreateParams params {"a", value};
            auto *element = arkModify->annotationAddAnnotationElement(arkAnnotation, &params);
            ASSERT_NE(element, nullptr);
            ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
            auto *coreElement = arkInspect->arktsAnnotationElementToCoreAnnotationElement(element);
            EXPECT_EQ(g_implI->annotationElementGetValue(coreElement), value);
            arkModify->annotationRemoveAnnotationElement(arkAnnotation, element);
            ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
        }
    });
    reader.join();
    writer.join();
    EXPECT_EQ(value->file, file);
    g_impl->closeFile(file);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
}

// Test: test-kind=api, api=ArktsModifyApiImpl::annotationAddAnnotationElement, abc-kind=ArkTS1, category=positive,
// extension=c
TEST_F(LibAbcKitDynamicThreadSafetyTest, FunctionAnnotationElementDoesNotRaceWithValueRead)
{
    CheckAnnotationValueConcurrency(false);
}

// Test: test-kind=api, api=ArktsModifyApiImpl::annotationAddAnnotationElement, abc-kind=ArkTS1, category=positive,
// extension=c
TEST_F(LibAbcKitDynamicThreadSafetyTest, ClassAnnotationElementDoesNotRaceWithValueRead)
{
    CheckAnnotationValueConcurrency(true);
}

static void CheckAnnotationRejectsForeignValue(bool classOwner)
{
    auto *arkInspect = AbckitGetArktsInspectApiImpl(ABCKIT_VERSION_RELEASE_1_0_0);
    auto *arkModify = AbckitGetArktsModifyApiImpl(ABCKIT_VERSION_RELEASE_1_0_0);
    constexpr auto INPUT = ABCKIT_ABC_DIR "ut/extensions/arkts/modify_api/annotations/annotations_dynamic.abc";
    AbckitFile *file = nullptr;
    AbckitFile *otherFile = nullptr;
    helpers::AssertOpenAbc(INPUT, &file);
    helpers::AssertOpenAbc(INPUT, &otherFile);
    auto *annotation = FindValueTestAnnotation(file, classOwner);
    ASSERT_NE(annotation, nullptr);
    auto *arkAnnotation = arkInspect->coreAnnotationToArktsAnnotation(annotation);
    auto *value = g_implM->createValueDouble(otherFile, 0.12);
    ASSERT_NE(value, nullptr);
    auto generation = file->dynamicStructuralGeneration.load();
    auto elementCount = annotation->elements.size();
    AbckitArktsAnnotationElementCreateParams params {"a", value};
    EXPECT_EQ(arkModify->annotationAddAnnotationElement(arkAnnotation, &params), nullptr);
    EXPECT_EQ(g_impl->getLastError(), ABCKIT_STATUS_WRONG_CTX);
    EXPECT_EQ(annotation->elements.size(), elementCount);
    EXPECT_EQ(file->dynamicStructuralGeneration.load(), generation);
    EXPECT_EQ(value->file, otherFile);
    const bool sourceOwnerPreserved = value->file == otherFile;
    g_impl->closeFile(file);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    // Rejecting the request must leave the source value usable after closing the target.
    if (sourceOwnerPreserved) {
        EXPECT_DOUBLE_EQ(g_implI->valueGetDouble(value), 0.12);
        EXPECT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    }
    g_impl->closeFile(otherFile);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
}

// Test: test-kind=api, api=ArktsModifyApiImpl::annotationAddAnnotationElement, abc-kind=ArkTS1, category=negative,
// extension=c
TEST_F(LibAbcKitDynamicThreadSafetyTest, FunctionAnnotationRejectsForeignValue)
{
    CheckAnnotationRejectsForeignValue(false);
}

// Test: test-kind=api, api=ArktsModifyApiImpl::annotationAddAnnotationElement, abc-kind=ArkTS1, category=negative,
// extension=c
TEST_F(LibAbcKitDynamicThreadSafetyTest, ClassAnnotationRejectsForeignValue)
{
    CheckAnnotationRejectsForeignValue(true);
}

#if GTEST_HAS_DEATH_TEST
static void RunConcurrentFirstApiCalls()
{
    // The threadsafe death test re-execs the binary; do not reset a live Logger.
    if (Logger::logger_ != nullptr) {
        std::_Exit(2);
    }
    constexpr size_t THREADS = 8U;
    ThreadBarrier barrier(THREADS);
    std::atomic<bool> failed {false};
    std::vector<std::thread> workers;
    for (size_t i = 0; i < THREADS; ++i) {
        workers.emplace_back([&]() {
            barrier.Wait();
            for (size_t j = 0; j < 100U; ++j) {
                if (g_impl->getLastError() != ABCKIT_STATUS_NO_ERROR) {
                    failed.store(true, std::memory_order_relaxed);
                }
            }
        });
    }
    for (auto &worker : workers) {
        worker.join();
    }
    std::_Exit(failed.load(std::memory_order_relaxed) ? 1 : 0);
}

// Test: test-kind=api, api=ApiImpl::getLastError, abc-kind=NoABC, category=positive, extension=c
TEST_F(LibAbcKitDynamicThreadSafetyTest, ConcurrentFirstApiCallsInitializeLogger)
{
    ::testing::FLAGS_gtest_death_test_style = "threadsafe";
    ASSERT_EXIT(RunConcurrentFirstApiCalls(), ::testing::ExitedWithCode(0), "");
}
#endif

// Test: test-kind=api, api=InspectApiImpl::literalGetLiteralArray, abc-kind=ArkTS1, category=negative,
// extension=c
TEST_F(LibAbcKitDynamicThreadSafetyTest, DynamicLiteralArrayInspectDoesNotInsertMissingName)
{
    AbckitFile *file = nullptr;
    helpers::AssertOpenAbc(ABCKIT_ABC_DIR "internal/implementation_api/abc_dynamic.abc", &file);
    constexpr std::string_view MISSING_NAME = "thread_safety_missing_literal_array";
    const auto cachedArraysBefore = file->litarrs.size();

    auto *literal = FindOrCreateLiteralLiteralArrayDynamicImpl(file, std::string(MISSING_NAME));
    ASSERT_NE(literal, nullptr);
    EXPECT_EQ(g_implI->literalGetLiteralArray(literal), nullptr);
    EXPECT_EQ(g_impl->getLastError(), ABCKIT_STATUS_INTERNAL_ERROR);
    EXPECT_EQ(file->litarrs.size(), cachedArraysBefore);

    g_impl->closeFile(file);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
}

// Test: test-kind=api, api=InspectApiImpl::arrayValueGetLiteralArray, abc-kind=ArkTS1, category=negative,
// extension=c
TEST_F(LibAbcKitDynamicThreadSafetyTest, DynamicArrayValueInspectDoesNotInsertMissingName)
{
    AbckitFile *file = nullptr;
    helpers::AssertOpenAbc(ABCKIT_ABC_DIR "internal/implementation_api/abc_dynamic.abc", &file);
    constexpr std::string_view MISSING_NAME = "thread_safety_missing_array_value";
    const auto cachedArraysBefore = file->litarrs.size();

    auto *value = FindOrCreateLiteralArrayValueDynamicImpl(file, std::string(MISSING_NAME));
    ASSERT_NE(value, nullptr);
    EXPECT_EQ(g_implI->arrayValueGetLiteralArray(value), nullptr);
    EXPECT_EQ(g_impl->getLastError(), ABCKIT_STATUS_INTERNAL_ERROR);
    EXPECT_EQ(file->litarrs.size(), cachedArraysBefore);

    g_impl->closeFile(file);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
}

// Test: test-kind=api, api=InspectApiImpl::annotationIsExternal, abc-kind=NoABC, category=negative,
// extension=c
TEST_F(LibAbcKitDynamicThreadSafetyTest, MetadataInspectFailsClosedWhenOwnerFileCannotBeResolved)
{
    AbckitCoreAnnotation annotation;
    EXPECT_FALSE(g_implI->annotationIsExternal(&annotation));
    EXPECT_EQ(g_impl->getLastError(), ABCKIT_STATUS_INTERNAL_ERROR);
}

// Test: test-kind=api, api=InspectApiImpl::moduleEnumerateTopLevelFunctions, abc-kind=NoABC, category=negative,
// extension=c
TEST_F(LibAbcKitDynamicThreadSafetyTest, DynamicMetadataInspectFailsClosedWhenModuleFileIsMissing)
{
    for (auto target : {ABCKIT_TARGET_ARK_TS_V1, ABCKIT_TARGET_JS}) {
        SCOPED_TRACE(target);
        AbckitCoreModule module;
        module.target = target;
        module.file = nullptr;
        EXPECT_EQ(g_implI->moduleGetName(&module), nullptr);
        EXPECT_EQ(g_impl->getLastError(), ABCKIT_STATUS_INTERNAL_ERROR);

        bool callbackCalled = false;
        EXPECT_FALSE(
            g_implI->moduleEnumerateTopLevelFunctions(&module, &callbackCalled, [](AbckitCoreFunction *, void *data) {
                *static_cast<bool *>(data) = true;
                return true;
            }));
        EXPECT_EQ(g_impl->getLastError(), ABCKIT_STATUS_INTERNAL_ERROR);
        EXPECT_FALSE(callbackCalled);

        AbckitCoreFunction function;
        function.owningModule = &module;
        EXPECT_EQ(g_implI->functionGetName(&function), nullptr);
        EXPECT_EQ(g_impl->getLastError(), ABCKIT_STATUS_INTERNAL_ERROR);
    }
}

static void CheckModuleConversionWaitsForWriter(bool js)
{
    AbckitFile *rawFile = nullptr;
    helpers::AssertOpenAbc(ABCKIT_ABC_DIR "internal/implementation_api/abc_dynamic.abc", &rawFile);
    scenario::ScopedFile file(rawFile);
    constexpr auto NAME = "conversion_write_lock";
    auto *arktsModify = AbckitGetArktsModifyApiImpl(ABCKIT_VERSION_RELEASE_1_0_0);
    AbckitCoreModule *module = nullptr;
    if (js) {
        const AbckitJsExternalModuleCreateParams params {NAME};
        auto *extension = g_jsModify->fileAddExternalModule(file.Get(), &params);
        ASSERT_NE(extension, nullptr);
        module = g_jsInspect->jsModuleToCoreModule(extension);
    } else {
        const AbckitArktsV1ExternalModuleCreateParams params {NAME};
        auto *extension = arktsModify->fileAddExternalModuleArktsV1(file.Get(), &params);
        ASSERT_NE(extension, nullptr);
        module = g_arktsInspect->arktsModuleToCoreModule(extension);
    }
    ASSERT_NE(module, nullptr);
    std::atomic<bool> completed {false};
    bool converted = false;
    AbckitStatus status = ABCKIT_STATUS_INTERNAL_ERROR;
    std::thread reader;
    bool registered = false;
    bool completedWhileLocked = false;
    {
        AbckitFileApiWriteLock writer(file.Get(), true, AbckitFileApiWriteMode::STRUCTURAL);
        ASSERT_FALSE(writer.HasConflict());
        reader = std::thread([&]() {
            converted = js ? g_jsInspect->coreModuleToJsModule(module) != nullptr
                           : g_arktsInspect->coreModuleToArktsModule(module) != nullptr;
            status = g_impl->getLastError();
            completed.store(true, std::memory_order_release);
        });
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (std::chrono::steady_clock::now() < deadline) {
            registered = file.Get()->threadState->GetActiveOperationCount() == 2U;
            if (registered || completed.load(std::memory_order_acquire)) {
                break;
            }
            std::this_thread::yield();
        }
        completedWhileLocked = completed.load(std::memory_order_acquire);
    }
    reader.join();
    EXPECT_TRUE(registered);
    EXPECT_FALSE(completedWhileLocked);
    EXPECT_TRUE(converted);
    EXPECT_EQ(status, ABCKIT_STATUS_NO_ERROR);
    EXPECT_EQ(file.Get()->threadState->GetActiveOperationCount(), 0U);
    EXPECT_EQ(file.Close(), ABCKIT_STATUS_NO_ERROR);
}

// Test: test-kind=api, api=JsInspectApiImpl::coreModuleToJsModule, abc-kind=JS, category=positive, extension=c
TEST_F(LibAbcKitDynamicThreadSafetyTest, DynamicJsModuleConversionWaitsForWriter)
{
    CheckModuleConversionWaitsForWriter(true);
}

// Test: test-kind=api, api=ArktsInspectApiImpl::coreModuleToArktsModule, abc-kind=ArkTS1, category=positive,
// extension=c
TEST_F(LibAbcKitDynamicThreadSafetyTest, DynamicArktsModuleConversionWaitsForWriter)
{
    CheckModuleConversionWaitsForWriter(false);
}

static bool CheckModuleConversions(AbckitCoreModule *module)
{
    // Replacement may change the target between calls. Do not dereference the returned extension
    // handles: only the Core handle remains stable across subsequent replacements.
    bool converted = g_jsInspect->coreModuleToJsModule(module) != nullptr;
    auto status = g_impl->getLastError();
    if (status != (converted ? ABCKIT_STATUS_NO_ERROR : ABCKIT_STATUS_WRONG_TARGET)) {
        return false;
    }
    converted = g_arktsInspect->coreModuleToArktsModule(module) != nullptr;
    status = g_impl->getLastError();
    return status == (converted ? ABCKIT_STATUS_NO_ERROR : ABCKIT_STATUS_WRONG_TARGET);
}

static void CheckExternalModuleReplacement(bool checkConversions)
{
    AbckitFile *rawFile = nullptr;
    helpers::AssertOpenAbc(ABCKIT_ABC_DIR "internal/implementation_api/abc_dynamic.abc", &rawFile);
    ASSERT_NE(rawFile, nullptr);
    scenario::ScopedFile file(rawFile);
    constexpr auto MODULE_NAME = "thread_safety_target_changes";
    const AbckitJsExternalModuleCreateParams jsParams {MODULE_NAME};
    const AbckitArktsV1ExternalModuleCreateParams arktsParams {MODULE_NAME};
    auto *arktsModify = AbckitGetArktsModifyApiImpl(ABCKIT_VERSION_RELEASE_1_0_0);
    ASSERT_NE(arktsModify, nullptr);

    AbckitCoreModule *module = nullptr;
    {
        auto *jsModule = g_jsModify->fileAddExternalModule(file.Get(), &jsParams);
        ASSERT_NE(jsModule, nullptr);
        ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
        module = g_jsInspect->jsModuleToCoreModule(jsModule);
        ASSERT_NE(module, nullptr);
        ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    }

    constexpr size_t WORKER_COUNT = 4U;
    constexpr size_t ITERATION_COUNT = 1024U;
    struct Result {
        AbckitStatus status = ABCKIT_STATUS_NO_ERROR;
        size_t completed = 0U;
    };
    // Replacing a same-name module preserves its Core handle but rebuilds its
    // extension implementation. Workers only retain the stable Core handle.
    auto run = scenario::RunShards<Result>(WORKER_COUNT, WORKER_COUNT, [&](const auto &shard) {
        Result result;
        for (size_t iteration = 0; iteration < ITERATION_COUNT; ++iteration) {
            if (shard.front() == 0U) {
                if (arktsModify->fileAddExternalModuleArktsV1(file.Get(), &arktsParams) == nullptr ||
                    g_impl->getLastError() != ABCKIT_STATUS_NO_ERROR ||
                    g_jsModify->fileAddExternalModule(file.Get(), &jsParams) == nullptr ||
                    g_impl->getLastError() != ABCKIT_STATUS_NO_ERROR) {
                    result.status = ABCKIT_STATUS_INTERNAL_ERROR;
                    return result;
                }
            } else {
                auto target = g_implI->moduleGetTarget(module);
                if ((target != ABCKIT_TARGET_JS && target != ABCKIT_TARGET_ARK_TS_V1) ||
                    g_impl->getLastError() != ABCKIT_STATUS_NO_ERROR) {
                    result.status = ABCKIT_STATUS_INTERNAL_ERROR;
                    return result;
                }
                if (checkConversions && !CheckModuleConversions(module)) {
                    result.status = ABCKIT_STATUS_INTERNAL_ERROR;
                    return result;
                }
                auto *name = g_implI->moduleGetName(module);
                if (name == nullptr || g_impl->getLastError() != ABCKIT_STATUS_NO_ERROR) {
                    result.status = ABCKIT_STATUS_INTERNAL_ERROR;
                    return result;
                }
                auto *value = g_implI->abckitStringToString(name);
                if (value == nullptr || g_impl->getLastError() != ABCKIT_STATUS_NO_ERROR ||
                    std::string_view(value) != MODULE_NAME) {
                    result.status = ABCKIT_STATUS_INTERNAL_ERROR;
                    return result;
                }
            }
            ++result.completed;
        }
        return result;
    });
    ASSERT_EQ(run.status, ABCKIT_STATUS_NO_ERROR);
    for (const auto &worker : run.workers) {
        EXPECT_EQ(worker.completed, ITERATION_COUNT);
    }
    EXPECT_EQ(file.Get()->externalModules.at(MODULE_NAME).get(), module);
    EXPECT_EQ(file.Close(), ABCKIT_STATUS_NO_ERROR);
}

// Test: test-kind=api, api=InspectApiImpl::moduleGetTarget, abc-kind=ArkTS1, category=positive, extension=c
TEST_F(LibAbcKitDynamicThreadSafetyTest, DynamicExternalModuleTargetChangesSerializeWithCoreReads)
{
    CheckExternalModuleReplacement(false);
}

// Test: test-kind=api, api=JsInspectApiImpl::coreModuleToJsModule, abc-kind=ArkTS1, category=positive, extension=c
TEST_F(LibAbcKitDynamicThreadSafetyTest, DynamicExternalModuleConversionsSerializeWithReplacement)
{
    CheckExternalModuleReplacement(true);
}

// Test: test-kind=api, api=JsModifyApiImpl::fileAddExternalModule, abc-kind=ArkTS1, category=positive,
// extension=c
TEST_F(LibAbcKitDynamicThreadSafetyTest, DynamicFileAddExternalJsModuleUsesFileWriteLock)
{
    AbckitFile *file = nullptr;
    helpers::AssertOpenAbc(ABCKIT_ABC_DIR "internal/implementation_api/abc_dynamic.abc", &file);
    AbckitJsExternalModuleCreateParams params {"thread_safety_external_js"};
    std::atomic<bool> started {false};
    std::atomic<bool> completed {false};
    AbckitJsModule *created = nullptr;
    AbckitStatus status = ABCKIT_STATUS_NO_ERROR;
    std::thread writer;

    {
        AbckitFileApiReadLock readLock(file);
        ASSERT_FALSE(readLock.HasConflict());
        writer = std::thread([&]() {
            started.store(true, std::memory_order_release);
            created = g_jsModify->fileAddExternalModule(file, &params);
            status = g_impl->getLastError();
            completed.store(true, std::memory_order_release);
        });

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (!completed.load(std::memory_order_acquire) &&
               (!started.load(std::memory_order_acquire) || file->threadState->GetActiveOperationCount() < 2U) &&
               std::chrono::steady_clock::now() < deadline) {
            std::this_thread::yield();
        }
        EXPECT_TRUE(started.load(std::memory_order_acquire));
        EXPECT_EQ(file->threadState->GetActiveOperationCount(), 2U);
        EXPECT_FALSE(completed.load(std::memory_order_acquire));
    }

    writer.join();
    EXPECT_NE(created, nullptr);
    EXPECT_EQ(status, ABCKIT_STATUS_NO_ERROR);
    g_impl->closeFile(file);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
}

// Public Modify callbacks suspend their enclosing read lock before acquiring a write lock. Exercise the unsupported
// unsuspended upgrade directly, while using writeAbc to verify the public WRONG_STATE mapping.
// Test: test-kind=api, api=ApiImpl::writeAbc, abc-kind=ArkTS1, category=negative, extension=c
TEST_F(LibAbcKitDynamicThreadSafetyTest, DynamicReadUpgradeReturnsWrongState)
{
    constexpr auto INPUT = ABCKIT_ABC_DIR "internal/implementation_api/abc_dynamic.abc";
    constexpr auto OUTPUT = ABCKIT_ABC_DIR "internal/implementation_api/dynamic_read_upgrade_output.abc";
    errno = 0;
    ASSERT_TRUE(std::remove(OUTPUT) == 0 || errno == ENOENT);

    AbckitFile *rawFile = nullptr;
    helpers::AssertOpenAbc(INPUT, &rawFile);
    ASSERT_NE(rawFile, nullptr);
    scenario::ScopedFile file(rawFile);
    auto *function = helpers::FindMethodByName(file.Get(), "foo");
    ASSERT_NE(function, nullptr);
    auto threadState = file.Get()->threadState;
    ASSERT_EQ(threadState->GetActiveOperationCount(), 0U);

    {
        AbckitFileApiReadLock readLock(file.Get());
        ASSERT_FALSE(readLock.HasConflict());
        ASSERT_EQ(threadState->GetActiveOperationCount(), 1U);

        auto *nameBefore = g_implI->functionGetName(function);
        ASSERT_NE(nameBefore, nullptr);
        ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);

        g_impl->writeAbc(file.Get(), OUTPUT, strlen(OUTPUT));
        EXPECT_EQ(g_impl->getLastError(), ABCKIT_STATUS_WRONG_STATE);
        EXPECT_EQ(threadState->GetActiveOperationCount(), 1U);

        auto *nameAfter = g_implI->functionGetName(function);
        ASSERT_NE(nameAfter, nullptr);
        EXPECT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
        EXPECT_EQ(helpers::AbckitStringToString(nameAfter), helpers::AbckitStringToString(nameBefore));
    }

    EXPECT_EQ(threadState->GetActiveOperationCount(), 0U);
    constexpr auto VALUE = "write-after-read-upgrade-conflict";
    EXPECT_NE(g_implM->createString(file.Get(), VALUE, strlen(VALUE)), nullptr);
    EXPECT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    EXPECT_EQ(threadState->GetActiveOperationCount(), 0U);
    EXPECT_EQ(file.Close(), ABCKIT_STATUS_NO_ERROR);

    std::error_code error;
    const bool outputExists = std::filesystem::exists(OUTPUT, error);
    EXPECT_FALSE(error);
    EXPECT_FALSE(outputExists);
    if (outputExists) {
        std::remove(OUTPUT);
    }
}

// Test: test-kind=api, api=InspectApiImpl::fileGetVersion, abc-kind=ArkTS2, category=positive, extension=c
TEST_F(LibAbcKitDynamicThreadSafetyTest, StaticInspectDoesNotUseDynamicFileLock)
{
    constexpr auto INPUT_PATH = ABCKIT_ABC_DIR "internal/implementation_api/abc_static.abc";
    AbckitFile *file = nullptr;
    helpers::AssertOpenAbc(INPUT_PATH, &file);
    ASSERT_EQ(file->frontend, Mode::STATIC);

    std::atomic<bool> started {false};
    std::atomic<bool> completed {false};
    AbckitFileVersion version = nullptr;
    AbckitStatus inspectStatus = ABCKIT_STATUS_NO_ERROR;
    std::thread reader;
    {
        std::lock_guard<std::shared_mutex> apiLock(file->threadState->apiMutex);
        reader = std::thread([&]() {
            started.store(true, std::memory_order_release);
            version = g_implI->fileGetVersion(file);
            inspectStatus = g_impl->getLastError();
            completed.store(true, std::memory_order_release);
        });
        EXPECT_TRUE(WaitForWorkerCompletion(started, completed));
    }
    reader.join();

    EXPECT_NE(version, nullptr);
    EXPECT_EQ(inspectStatus, ABCKIT_STATUS_NO_ERROR);
    g_impl->closeFile(file);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
}

// Test: test-kind=api, api=ApiImpl::writeAbc, abc-kind=ArkTS2, category=positive, extension=c
TEST_F(LibAbcKitDynamicThreadSafetyTest, StaticWriteDoesNotUseDynamicFileLock)
{
    constexpr auto INPUT_PATH = ABCKIT_ABC_DIR "ut/ir_core/graph_basic_block/graph_basic_block.abc";
    constexpr std::string_view OUTPUT_PATH =
        ABCKIT_ABC_DIR "ut/ir_core/graph_basic_block/abckit_static_write_without_dynamic_lock.abc";
    AbckitFile *file = nullptr;
    helpers::AssertOpenAbc(INPUT_PATH, &file);
    ASSERT_EQ(file->frontend, Mode::STATIC);

    std::atomic<bool> started {false};
    std::atomic<bool> completed {false};
    AbckitStatus writeStatus = ABCKIT_STATUS_NO_ERROR;
    std::thread writer;
    {
        std::lock_guard<std::shared_mutex> apiLock(file->threadState->apiMutex);
        writer = std::thread([&]() {
            started.store(true, std::memory_order_release);
            g_impl->writeAbc(file, OUTPUT_PATH.data(), OUTPUT_PATH.size());
            writeStatus = g_impl->getLastError();
            completed.store(true, std::memory_order_release);
        });
        EXPECT_TRUE(WaitForWorkerCompletion(started, completed));
    }
    writer.join();

    EXPECT_EQ(writeStatus, ABCKIT_STATUS_NO_ERROR);
    EXPECT_EQ(std::remove(OUTPUT_PATH.data()), 0);
    g_impl->closeFile(file);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
}

// Test: test-kind=api, api=ModifyApiImpl::functionSetGraph, abc-kind=ArkTS2, category=positive, extension=c
TEST_F(LibAbcKitDynamicThreadSafetyTest, StaticSetGraphDoesNotUseDynamicFileLock)
{
    constexpr auto INPUT_PATH = ABCKIT_ABC_DIR "ut/ir_core/graph_basic_block/graph_basic_block.abc";
    AbckitFile *file = nullptr;
    helpers::AssertOpenAbc(INPUT_PATH, &file);
    ASSERT_EQ(file->frontend, Mode::STATIC);
    auto *function = helpers::FindMethodByName(file, "foo");
    ASSERT_NE(function, nullptr);
    auto *graph = g_implI->createGraphFromFunction(function);
    ASSERT_NE(graph, nullptr);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);

    std::atomic<bool> started {false};
    std::atomic<bool> completed {false};
    AbckitStatus setGraphStatus = ABCKIT_STATUS_NO_ERROR;
    std::thread setter;
    {
        std::lock_guard<std::shared_mutex> apiLock(file->threadState->apiMutex);
        setter = std::thread([&]() {
            started.store(true, std::memory_order_release);
            g_implM->functionSetGraph(function, graph);
            setGraphStatus = g_impl->getLastError();
            completed.store(true, std::memory_order_release);
        });
        EXPECT_TRUE(WaitForWorkerCompletion(started, completed));
    }
    setter.join();

    EXPECT_EQ(setGraphStatus, ABCKIT_STATUS_NO_ERROR);
    g_impl->destroyGraph(graph);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    g_impl->closeFile(file);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
}

// Test: test-kind=api, api=ApiImpl::destroyGraph, abc-kind=ArkTS2, category=positive, extension=c
TEST_F(LibAbcKitDynamicThreadSafetyTest, StaticDestroyGraphDoesNotUseDynamicFileLock)
{
    constexpr auto INPUT_PATH = ABCKIT_ABC_DIR "ut/ir_core/graph_basic_block/graph_basic_block.abc";
    AbckitFile *file = nullptr;
    helpers::AssertOpenAbc(INPUT_PATH, &file);
    ASSERT_EQ(file->frontend, Mode::STATIC);
    auto *function = helpers::FindMethodByName(file, "foo");
    ASSERT_NE(function, nullptr);
    auto *graph = g_implI->createGraphFromFunction(function);
    ASSERT_NE(graph, nullptr);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);

    std::atomic<bool> started {false};
    std::atomic<bool> completed {false};
    AbckitStatus destroyStatus = ABCKIT_STATUS_NO_ERROR;
    std::thread destroyer;
    {
        std::lock_guard<std::shared_mutex> apiLock(file->threadState->apiMutex);
        destroyer = std::thread([&]() {
            started.store(true, std::memory_order_release);
            g_impl->destroyGraph(graph);
            destroyStatus = g_impl->getLastError();
            completed.store(true, std::memory_order_release);
        });
        EXPECT_TRUE(WaitForWorkerCompletion(started, completed));
    }
    destroyer.join();

    EXPECT_EQ(destroyStatus, ABCKIT_STATUS_NO_ERROR);
    g_impl->closeFile(file);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
}

// Test: test-kind=api, api=ApiImpl::closeFile, abc-kind=ArkTS2, category=positive, extension=c
TEST_F(LibAbcKitDynamicThreadSafetyTest, StaticCloseDoesNotUseDynamicLifecycleState)
{
    constexpr auto INPUT_PATH = ABCKIT_ABC_DIR "internal/implementation_api/abc_static.abc";
    AbckitFile *file = nullptr;
    helpers::AssertOpenAbc(INPUT_PATH, &file);
    ASSERT_EQ(file->frontend, Mode::STATIC);

    auto threadState = file->threadState;
    ASSERT_TRUE(threadState->TryBeginOperation());
    g_impl->closeFile(file);
    auto closeStatus = g_impl->getLastError();
    threadState->FinishOperation();

    if (closeStatus != ABCKIT_STATUS_NO_ERROR) {
        g_impl->closeFile(file);
    }
    EXPECT_EQ(closeStatus, ABCKIT_STATUS_NO_ERROR);
}

struct InspectStressContext {
    std::atomic<size_t> *moduleCount = nullptr;
    std::atomic<size_t> *functionCount = nullptr;
    std::atomic<size_t> *classCount = nullptr;
    std::atomic<bool> *failed = nullptr;
};

struct LazyCacheStressContext {
    std::atomic<size_t> *elementCount = nullptr;
    std::atomic<bool> *failed = nullptr;
};

static void MarkFailed(InspectStressContext *ctx)
{
    ctx->failed->store(true, std::memory_order_relaxed);
}

static bool CheckLastError(InspectStressContext *ctx)
{
    if (g_impl->getLastError() != ABCKIT_STATUS_NO_ERROR) {
        MarkFailed(ctx);
        return false;
    }
    return true;
}

static bool CheckLastError(LazyCacheStressContext *ctx)
{
    if (g_impl->getLastError() != ABCKIT_STATUS_NO_ERROR) {
        ctx->failed->store(true, std::memory_order_relaxed);
        return false;
    }
    return true;
}

static bool FunctionVisitor(AbckitCoreFunction *function, void *data)
{
    auto *ctx = reinterpret_cast<InspectStressContext *>(data);
    auto *name = g_implI->functionGetName(function);
    if (name == nullptr || !CheckLastError(ctx)) {
        return false;
    }
    if (helpers::AbckitStringToString(name).empty()) {
        MarkFailed(ctx);
        return false;
    }
    ctx->functionCount->fetch_add(1, std::memory_order_relaxed);
    return true;
}

static bool ClassVisitor(AbckitCoreClass *klass, void *data)
{
    auto *ctx = reinterpret_cast<InspectStressContext *>(data);
    auto *name = g_implI->classGetName(klass);
    if (name == nullptr || !CheckLastError(ctx)) {
        return false;
    }
    if (helpers::AbckitStringToString(name).empty()) {
        MarkFailed(ctx);
        return false;
    }
    ctx->classCount->fetch_add(1, std::memory_order_relaxed);

    if (!g_implI->classEnumerateMethods(klass, data, FunctionVisitor)) {
        MarkFailed(ctx);
        return false;
    }
    return CheckLastError(ctx);
}

static bool ModuleVisitor(AbckitCoreModule *module, void *data)
{
    auto *ctx = reinterpret_cast<InspectStressContext *>(data);
    auto *file = g_implI->moduleGetFile(module);
    if (file == nullptr || !CheckLastError(ctx)) {
        return false;
    }

    auto *name = g_implI->moduleGetName(module);
    if (name == nullptr || !CheckLastError(ctx)) {
        return false;
    }
    if (helpers::AbckitStringToString(name).empty()) {
        MarkFailed(ctx);
        return false;
    }
    ctx->moduleCount->fetch_add(1, std::memory_order_relaxed);

    if (!g_implI->moduleEnumerateTopLevelFunctions(module, data, FunctionVisitor)) {
        MarkFailed(ctx);
        return false;
    }
    if (!CheckLastError(ctx)) {
        return false;
    }

    if (!g_implI->moduleEnumerateClasses(module, data, ClassVisitor)) {
        MarkFailed(ctx);
        return false;
    }
    return CheckLastError(ctx);
}

static bool LiteralArrayElementVisitor(AbckitFile *, AbckitLiteral *lit, void *data)
{
    auto *ctx = reinterpret_cast<LazyCacheStressContext *>(data);
    auto tag = g_implI->literalGetTag(lit);
    if (!CheckLastError(ctx)) {
        return false;
    }

    switch (tag) {
        case ABCKIT_LITERAL_TAG_STRING: {
            auto *str = g_implI->literalGetString(lit);
            if (str == nullptr || !CheckLastError(ctx) || helpers::AbckitStringToString(str) != "thread-cache") {
                ctx->failed->store(true, std::memory_order_relaxed);
                return false;
            }
            break;
        }
        case ABCKIT_LITERAL_TAG_BOOL:
            if (!g_implI->literalGetBool(lit) || !CheckLastError(ctx)) {
                ctx->failed->store(true, std::memory_order_relaxed);
                return false;
            }
            break;
        default:
            ctx->failed->store(true, std::memory_order_relaxed);
            return false;
    }

    ctx->elementCount->fetch_add(1, std::memory_order_relaxed);
    return true;
}

// Test: test-kind=api, api=InspectApiImpl::fileEnumerateModules, abc-kind=ArkTS1, category=positive, extension=c
TEST_F(LibAbcKitDynamicThreadSafetyTest, DynamicCoreInspectConcurrentReadTraversal)
{
    AbckitFile *file = nullptr;
    helpers::AssertOpenAbc(ABCKIT_ABC_DIR "ut/metadata_core/inspect_api/classes/classes_dynamic.abc", &file);

    constexpr size_t THREAD_COUNT = 8;
    constexpr size_t ITERATIONS_PER_THREAD = 64;
    std::atomic<size_t> moduleCount {0};
    std::atomic<size_t> functionCount {0};
    std::atomic<size_t> classCount {0};
    std::atomic<bool> failed {false};
    std::atomic<bool> start {false};

    InspectStressContext ctx {&moduleCount, &functionCount, &classCount, &failed};
    std::vector<std::thread> threads;
    threads.reserve(THREAD_COUNT);
    for (size_t i = 0; i < THREAD_COUNT; ++i) {
        threads.emplace_back([&]() {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            for (size_t iter = 0; iter < ITERATIONS_PER_THREAD && !failed.load(std::memory_order_relaxed); ++iter) {
                if (!g_implI->fileEnumerateModules(file, &ctx, ModuleVisitor)) {
                    failed.store(true, std::memory_order_relaxed);
                    return;
                }
                if (g_impl->getLastError() != ABCKIT_STATUS_NO_ERROR) {
                    failed.store(true, std::memory_order_relaxed);
                    return;
                }
            }
        });
    }

    start.store(true, std::memory_order_release);
    for (auto &thread : threads) {
        thread.join();
    }

    ASSERT_FALSE(failed.load(std::memory_order_relaxed));
    ASSERT_GT(moduleCount.load(std::memory_order_relaxed), 0);
    ASSERT_GT(functionCount.load(std::memory_order_relaxed), 0);
    ASSERT_GT(classCount.load(std::memory_order_relaxed), 0);

    g_impl->closeFile(file);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
}

// Test: test-kind=api, api=InspectApiImpl::literalGetString, abc-kind=ArkTS1, category=positive, extension=c
TEST_F(LibAbcKitDynamicThreadSafetyTest, DynamicLazyMetadataCacheConcurrentRead)
{
    AbckitFile *file = nullptr;
    helpers::AssertOpenAbc(ABCKIT_ABC_DIR "ut/metadata_core/inspect_api/values/values_dynamic.abc", &file);

    auto *literal = g_implM->createLiteralString(file, "thread-cache", strlen("thread-cache"));
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    ASSERT_NE(literal, nullptr);

    std::vector<AbckitValue *> values {
        g_implM->createValueString(file, "thread-cache", strlen("thread-cache")),
        g_implM->createValueU1(file, true),
    };
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    auto *arrayValue = g_implM->createLiteralArrayValue(file, values.data(), values.size());
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    ASSERT_NE(arrayValue, nullptr);

    constexpr size_t THREAD_COUNT = 8;
    constexpr size_t ITERATIONS_PER_THREAD = 64;
    std::atomic<size_t> elementCount {0};
    std::atomic<bool> failed {false};
    std::atomic<bool> start {false};
    LazyCacheStressContext ctx {&elementCount, &failed};

    std::vector<std::thread> threads;
    threads.reserve(THREAD_COUNT);
    for (size_t i = 0; i < THREAD_COUNT; ++i) {
        threads.emplace_back([&]() {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            for (size_t iter = 0; iter < ITERATIONS_PER_THREAD && !failed.load(std::memory_order_relaxed); ++iter) {
                auto *str = g_implI->literalGetString(literal);
                if (str == nullptr || g_impl->getLastError() != ABCKIT_STATUS_NO_ERROR ||
                    helpers::AbckitStringToString(str) != "thread-cache") {
                    failed.store(true, std::memory_order_relaxed);
                    return;
                }

                auto *type = g_implI->valueGetType(values[0]);
                if (type == nullptr || g_impl->getLastError() != ABCKIT_STATUS_NO_ERROR ||
                    g_implI->typeGetTypeId(type) != ABCKIT_TYPE_ID_STRING ||
                    g_impl->getLastError() != ABCKIT_STATUS_NO_ERROR) {
                    failed.store(true, std::memory_order_relaxed);
                    return;
                }

                auto *litArr = g_implI->arrayValueGetLiteralArray(arrayValue);
                if (litArr == nullptr || g_impl->getLastError() != ABCKIT_STATUS_NO_ERROR) {
                    failed.store(true, std::memory_order_relaxed);
                    return;
                }
                if (!g_implI->literalArrayEnumerateElements(litArr, &ctx, LiteralArrayElementVisitor) ||
                    g_impl->getLastError() != ABCKIT_STATUS_NO_ERROR) {
                    failed.store(true, std::memory_order_relaxed);
                    return;
                }
            }
        });
    }

    start.store(true, std::memory_order_release);
    for (auto &thread : threads) {
        thread.join();
    }

    ASSERT_FALSE(failed.load(std::memory_order_relaxed));
    ASSERT_GT(elementCount.load(std::memory_order_relaxed), 0);

    g_impl->closeFile(file);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
}

// Test: test-kind=api, api=ModifyApiImpl::createValueInt, abc-kind=ArkTS1, category=positive, extension=c
TEST_F(LibAbcKitDynamicThreadSafetyTest, DynamicModifyValueAndLiteralCachesConcurrentCreate)
{
    constexpr auto INPUT_PATH = ABCKIT_ABC_DIR "internal/implementation_api/abc_dynamic.abc";
    AbckitFile *file = nullptr;
    helpers::AssertOpenAbc(INPUT_PATH, &file);
    auto *function = helpers::FindMethodByName(file, "foo");
    ASSERT_NE(function, nullptr);

    auto *arrayElement = g_implM->createLiteralU32(file, 0xABCDEFU);
    ASSERT_NE(arrayElement, nullptr);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    std::array<AbckitLiteral *, 1U> arrayElements {arrayElement};
    auto *literalArray = g_implM->createLiteralArray(file, arrayElements.data(), arrayElements.size());
    ASSERT_NE(literalArray, nullptr);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);

    constexpr size_t THREAD_COUNT = 8U;
    constexpr size_t ITERATION_COUNT = 128U;
    constexpr size_t CREATED_KIND_COUNT = 16U;
    using CreatedObjects = std::array<const void *, CREATED_KIND_COUNT>;
    std::vector<CreatedObjects> created(THREAD_COUNT * ITERATION_COUNT);
    std::atomic<bool> failed {false};
    ThreadBarrier barrier(THREAD_COUNT);
    std::vector<std::thread> threads;
    threads.reserve(THREAD_COUNT);

    for (size_t threadIndex = 0; threadIndex < THREAD_COUNT; ++threadIndex) {
        threads.emplace_back([&, threadIndex]() {
            for (size_t iteration = 0; iteration < ITERATION_COUNT; ++iteration) {
                auto &objects = created[threadIndex * ITERATION_COUNT + iteration];
                auto key = std::string("concurrent-modify-cache-") + std::to_string(iteration);
                size_t kind = 0U;
                auto create = [&](auto creator) {
                    barrier.Wait();
                    objects[kind++] = creator();
                    if (objects[kind - 1U] == nullptr || g_impl->getLastError() != ABCKIT_STATUS_NO_ERROR) {
                        failed.store(true, std::memory_order_relaxed);
                    }
                    barrier.Wait();
                };

                create([&]() { return g_implM->createValueU1(file, (iteration & 1U) != 0U); });
                create([&]() { return g_implM->createValueInt(file, static_cast<int>(iteration)); });
                create([&]() { return g_implM->createValueDouble(file, static_cast<double>(iteration) + 0.25); });
                create([&]() { return g_implM->createValueString(file, key.c_str(), key.size()); });
                create([&]() { return g_implM->createLiteralBool(file, (iteration & 1U) != 0U); });
                create([&]() { return g_implM->createLiteralU8(file, static_cast<uint8_t>(iteration)); });
                create([&]() { return g_implM->createLiteralU16(file, static_cast<uint16_t>(iteration)); });
                create([&]() { return g_implM->createLiteralMethodAffiliate(file, static_cast<uint16_t>(iteration)); });
                create([&]() { return g_implM->createLiteralU32(file, static_cast<uint32_t>(iteration + 0x10000U)); });
                create([&]() { return g_implM->createLiteralU64(file, static_cast<uint64_t>(iteration + 0x100000U)); });
                create([&]() { return g_implM->createLiteralFloat(file, static_cast<float>(iteration) + 0.5F); });
                create([&]() { return g_implM->createLiteralDouble(file, static_cast<double>(iteration) + 0.75); });
                create([&]() { return g_implM->createLiteralLiteralArray(file, literalArray); });
                create([&]() { return g_implM->createLiteralString(file, key.c_str(), key.size()); });
                create([&]() { return g_implM->createLiteralMethod(file, function); });
                create([&]() { return g_implM->createLiteralNullValue(file); });
            }
        });
    }

    for (auto &thread : threads) {
        thread.join();
    }

    ASSERT_FALSE(failed.load(std::memory_order_relaxed));
    for (size_t iteration = 0; iteration < ITERATION_COUNT; ++iteration) {
        const auto &expected = created[iteration];
        for (size_t threadIndex = 1U; threadIndex < THREAD_COUNT; ++threadIndex) {
            const auto &actual = created[threadIndex * ITERATION_COUNT + iteration];
            for (size_t kind = 0; kind < CREATED_KIND_COUNT; ++kind) {
                EXPECT_EQ(actual[kind], expected[kind]) << "iteration=" << iteration << ", kind=" << kind;
            }
        }
    }

    g_impl->closeFile(file);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
}

// Test: test-kind=api, api=ModifyApiImpl::createValueInt, abc-kind=ArkTS1, category=negative, extension=c
TEST_F(LibAbcKitDynamicThreadSafetyTest, DynamicModifyCacheApisRejectedAfterCloseBegins)
{
    constexpr auto INPUT_PATH = ABCKIT_ABC_DIR "internal/implementation_api/abc_dynamic.abc";
    AbckitFile *file = nullptr;
    helpers::AssertOpenAbc(INPUT_PATH, &file);
    auto *function = helpers::FindMethodByName(file, "foo");
    ASSERT_NE(function, nullptr);

    auto *arrayElement = g_implM->createLiteralU32(file, 0x1234U);
    ASSERT_NE(arrayElement, nullptr);
    std::array<AbckitLiteral *, 1U> arrayElements {arrayElement};
    auto *literalArray = g_implM->createLiteralArray(file, arrayElements.data(), arrayElements.size());
    ASSERT_NE(literalArray, nullptr);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);

    auto threadState = file->threadState;
    {
        std::lock_guard<std::shared_mutex> apiLock(threadState->apiMutex);
        ASSERT_TRUE(threadState->TryBeginClose());
    }

    auto expectRejected = [](auto creator) {
        EXPECT_EQ(creator(), nullptr);
        EXPECT_EQ(g_impl->getLastError(), ABCKIT_STATUS_WRONG_STATE);
    };
    expectRejected([&]() { return g_implM->createType(file, ABCKIT_TYPE_ID_I32); });
    expectRejected([&]() { return g_implM->createValueU1(file, true); });
    expectRejected([&]() { return g_implM->createValueInt(file, 42); });
    expectRejected([&]() { return g_implM->createValueDouble(file, 42.5); });
    expectRejected([&]() { return g_implM->createValueString(file, "closed", strlen("closed")); });
    expectRejected([&]() { return g_implM->createLiteralBool(file, true); });
    expectRejected([&]() { return g_implM->createLiteralU8(file, 1U); });
    expectRejected([&]() { return g_implM->createLiteralU16(file, 2U); });
    expectRejected([&]() { return g_implM->createLiteralMethodAffiliate(file, 3U); });
    expectRejected([&]() { return g_implM->createLiteralU32(file, 4U); });
    expectRejected([&]() { return g_implM->createLiteralU64(file, 5U); });
    expectRejected([&]() { return g_implM->createLiteralFloat(file, 6.0F); });
    expectRejected([&]() { return g_implM->createLiteralDouble(file, 7.0); });
    expectRejected([&]() { return g_implM->createLiteralLiteralArray(file, literalArray); });
    expectRejected([&]() { return g_implM->createLiteralString(file, "closed", strlen("closed")); });
    expectRejected([&]() { return g_implM->createLiteralMethod(file, function); });
    expectRejected([&]() { return g_implM->createLiteralNullValue(file); });

    {
        std::lock_guard<std::shared_mutex> apiLock(threadState->apiMutex);
        CloseFileDynamic(file);
    }
    threadState->FinishClose();
}

// Test: test-kind=api, api=ModifyApiImpl::createString, abc-kind=ArkTS1, category=positive, extension=c
TEST_F(LibAbcKitDynamicThreadSafetyTest, DynamicModifyDifferentFilesDoNotShareApiLock)
{
    constexpr auto INPUT_PATH = ABCKIT_ABC_DIR "internal/implementation_api/abc_dynamic.abc";
    AbckitFile *lockedFile = nullptr;
    AbckitFile *modifiedFile = nullptr;
    helpers::AssertOpenAbc(INPUT_PATH, &lockedFile);
    helpers::AssertOpenAbc(INPUT_PATH, &modifiedFile);

    std::atomic<bool> started {false};
    std::atomic<bool> completed {false};
    AbckitString *created = nullptr;
    AbckitStatus createStatus = ABCKIT_STATUS_NO_ERROR;
    std::thread creator;
    {
        AbckitFileApiWriteLock writeLock(lockedFile);
        ASSERT_FALSE(writeLock.HasConflict());
        creator = std::thread([&]() {
            started.store(true, std::memory_order_release);
            constexpr auto VALUE = "independent-file-modify";
            created = g_implM->createString(modifiedFile, VALUE, strlen(VALUE));
            createStatus = g_impl->getLastError();
            completed.store(true, std::memory_order_release);
        });
        EXPECT_TRUE(WaitForWorkerCompletion(started, completed));
    }
    creator.join();

    ASSERT_NE(created, nullptr);
    ASSERT_EQ(createStatus, ABCKIT_STATUS_NO_ERROR);
    g_impl->closeFile(lockedFile);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    g_impl->closeFile(modifiedFile);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
}

// Test: test-kind=api, api=IsaApiDynamicImpl::iCreateCreatearraywithbuffer, abc-kind=ArkTS1, category=positive,
// extension=c

}  // namespace libabckit::test
