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

#include "libabckit/src/ir_impl.h"
#include "libabckit/src/statuses_impl.h"
#include "libabckit/src/adapter_dynamic/metadata_modify_dynamic.h"
#include "libabckit/src/adapter_static/metadata_modify_static.h"
#include "libabckit/c/isa/isa_dynamic.h"
#include "libabckit/c/isa/isa_static.h"
#include "helpers/helpers.h"

#include <array>
#include <chrono>
#include <future>
#include <thread>
#include <gtest/gtest.h>

namespace libabckit::test {

// Static-path lock acquisition must complete within this timeout; guarded by the dynamic mutex.
static constexpr int K_LOCK_WAIT_TIMEOUT_SEC = 5;

class LibAbcKitIrInterfaceTest : public ::testing::Test {};

// Test: test-kind=api, api=IsaApiDynamicImpl::iSetModule, abc-kind=NoABC, category=negative-wrongctx, extension=c
TEST_F(LibAbcKitIrInterfaceTest, DynamicSetModuleRejectsWrongContextBeforeFileAccess)
{
    // Match the generated wrong-context helpers: only compare these file identities, never dereference them.
    AbckitGraph graph;
    graph.file = reinterpret_cast<AbckitFile *>(0x1);
    AbckitCoreModule module;
    module.file = reinterpret_cast<AbckitFile *>(0x2);
    AbckitInst inst(&graph, nullptr);

    AbckitGetIsaApiDynamicImpl(ABCKIT_VERSION_RELEASE_1_0_0)->iSetModule(&inst, &module);
    EXPECT_EQ(AbckitGetApiImpl(ABCKIT_VERSION_RELEASE_1_0_0)->getLastError(), ABCKIT_STATUS_WRONG_CTX);
}

// The public createString API rejects length zero; the adapters also intern empty strings read from ABC input.
static void CheckEmptyStringAndMissingOffset(bool dynamic)
{
    auto *api = AbckitGetApiImpl(ABCKIT_VERSION_RELEASE_1_0_0);
    auto *inspect = AbckitGetInspectApiImpl(ABCKIT_VERSION_RELEASE_1_0_0);
    auto *graphApi = AbckitGetGraphApiImpl(ABCKIT_VERSION_RELEASE_1_0_0);
    AbckitFile *file = nullptr;
    helpers::AssertOpenAbc(dynamic ? ABCKIT_ABC_DIR "ut/ir_core/string/string_dynamic.abc"
                                   : ABCKIT_ABC_DIR "ut/ir_core/string/string_static.abc",
                           &file);
    auto *function = helpers::FindMethodByName(file, "foo");
    ASSERT_NE(function, nullptr);
    auto *graph = inspect->createGraphFromFunction(function);
    ASSERT_NE(graph, nullptr);
    auto *empty = dynamic ? CreateStringDynamic(file, "", 0) : CreateStringStatic(file, "", 0);
    ASSERT_NE(empty, nullptr);
    auto *inst = dynamic ? AbckitGetIsaApiDynamicImpl(ABCKIT_VERSION_RELEASE_1_0_0)->iCreateLoadString(graph, empty)
                         : AbckitGetIsaApiStaticImpl(ABCKIT_VERSION_RELEASE_1_0_0)->iCreateLoadString(graph, empty);
    ASSERT_NE(inst, nullptr);
    EXPECT_EQ(graphApi->iGetString(inst), empty);
    EXPECT_EQ(api->getLastError(), ABCKIT_STATUS_NO_ERROR);

    // The missing lookup must not fall back to the valid empty string already in the file.
    AbckitIrInterface missing({}, {}, {}, {}, {});
    auto *original = graph->irInterface;
    graph->irInterface = &missing;
    auto *result = graphApi->iGetString(inst);
    auto error = api->getLastError();
    graph->irInterface = original;
    EXPECT_EQ(result, nullptr);
    EXPECT_EQ(error, ABCKIT_STATUS_INTERNAL_ERROR);
    api->destroyGraph(graph);
    api->closeFile(file);
}

// Test: test-kind=api, api=GraphApiImpl::iGetString, abc-kind=ArkTS1, category=positive, extension=c
TEST_F(LibAbcKitIrInterfaceTest, DynamicEmptyStringAndMissingOffsetRemainDistinct)
{
    CheckEmptyStringAndMissingOffset(true);
}

// Test: test-kind=api, api=GraphApiImpl::iGetString, abc-kind=ArkTS2, category=positive, extension=c
TEST_F(LibAbcKitIrInterfaceTest, StaticEmptyStringAndMissingOffsetRemainDistinct)
{
    CheckEmptyStringAndMissingOffset(false);
}

// Test: test-kind=internal, abc-kind=ArkTS2, category=positive, extension=c
TEST_F(LibAbcKitIrInterfaceTest, StaticGraphGettersAndSettersDoNotAcquireDynamicLocks)
{
    auto *api = AbckitGetApiImpl(ABCKIT_VERSION_RELEASE_1_0_0);
    auto *modify = AbckitGetModifyApiImpl(ABCKIT_VERSION_RELEASE_1_0_0);
    auto *graphApi = AbckitGetGraphApiImpl(ABCKIT_VERSION_RELEASE_1_0_0);
    auto *isa = AbckitGetIsaApiStaticImpl(ABCKIT_VERSION_RELEASE_1_0_0);
    AbckitFile *file = nullptr;
    helpers::AssertOpenAbc(ABCKIT_ABC_DIR "ut/extensions/arkts/inspect_api/helpers/opcodes_static_test.abc", &file);
    helpers::InspectMethod(file, "main", [&](AbckitFile *, AbckitCoreFunction *function, AbckitGraph *graph) {
        auto *stringInst = helpers::FindFirstInst(graph, ABCKIT_ISA_API_STATIC_OPCODE_LOADSTRING);
        ASSERT_NE(stringInst, nullptr);
        auto *expectedString = graphApi->iGetString(stringInst);
        ASSERT_NE(expectedString, nullptr);
        auto *literal = modify->createLiteralU32(file, 1U);
        ASSERT_NE(literal, nullptr);
        auto *array = modify->createLiteralArray(file, &literal, 1U);
        ASSERT_NE(array, nullptr);
        auto *arrayInst = isa->iCreateLoadConstArray(graph, array);
        ASSERT_NE(arrayInst, nullptr);
        auto *callInst = isa->iCreateCallStatic(graph, function, 0U);
        ASSERT_NE(callInst, nullptr);
        std::promise<void> complete;
        auto future = complete.get_future();
        std::thread reader;
        {
            std::unique_lock apiLock(file->threadState->apiMutex);
            std::unique_lock stringsLock(file->threadState->stringsCacheMutex);
            std::unique_lock arraysLock(file->threadState->litarrsCacheMutex);
            reader = std::thread([&]() {
                EXPECT_EQ(graphApi->iGetString(stringInst), expectedString);
                EXPECT_EQ(api->getLastError(), ABCKIT_STATUS_NO_ERROR);
                EXPECT_EQ(graphApi->iGetLiteralArray(arrayInst), array);
                EXPECT_EQ(api->getLastError(), ABCKIT_STATUS_NO_ERROR);
                // Static argument validation must also bypass the dynamic file lock.
                graphApi->iSetLiteralArray(stringInst, array);
                EXPECT_EQ(api->getLastError(), ABCKIT_STATUS_BAD_ARGUMENT);
                graphApi->iSetFunction(callInst, function);
                EXPECT_EQ(api->getLastError(), ABCKIT_STATUS_NO_ERROR);
                complete.set_value();
            });
            EXPECT_EQ(future.wait_for(std::chrono::seconds(K_LOCK_WAIT_TIMEOUT_SEC)), std::future_status::ready);
        }
        reader.join();
    });
    api->closeFile(file);
}

// Test: test-kind=internal, api=AbckitIrInterface, abc-kind=NoABC, category=negative, extension=c
TEST_F(LibAbcKitIrInterfaceTest, MissingOffsetFailsClosedThroughOverlay)
{
    AbckitIrInterface base({}, {}, {}, {}, {});
    AbckitIrInterface overlay(&base);
    using Lookup = std::string (AbckitIrInterface::*)(uint32_t) const;
    constexpr std::array<Lookup, 5U> LOOKUPS = {
        &AbckitIrInterface::GetMethodIdByOffset,       &AbckitIrInterface::GetStringIdByOffset,
        &AbckitIrInterface::GetLiteralArrayIdByOffset, &AbckitIrInterface::GetTypeIdByOffset,
        &AbckitIrInterface::GetFieldIdByOffset,
    };

    for (auto lookup : LOOKUPS) {
        statuses::SetLastError(ABCKIT_STATUS_NO_ERROR);
        EXPECT_TRUE((overlay.*lookup)(42U).empty());
        EXPECT_EQ(statuses::GetLastError(), ABCKIT_STATUS_INTERNAL_ERROR);
    }
}

}  // namespace libabckit::test
