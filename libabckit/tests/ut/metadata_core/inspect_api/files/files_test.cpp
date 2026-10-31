/**
 * Copyright (c) 2025-2026 Huawei Device Co., Ltd.
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

#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>
#include <gtest/gtest.h>

#include "libabckit/c/abckit.h"
#include "helpers/helpers.h"
#include "helpers/helpers_runtime.h"
#include "libabckit/c/metadata_core.h"
#include "logger.h"

namespace libabckit::test {

static auto g_impl = AbckitGetApiImpl(ABCKIT_VERSION_RELEASE_1_0_0);
static auto g_implI = AbckitGetInspectApiImpl(ABCKIT_VERSION_RELEASE_1_0_0);

class LibAbcKitInspectApiFilesTest : public ::testing::Test {};

// Test: test-kind=api, api=InspectApiImpl::fileGetVersion, abc-kind=ArkTS2, category=positive, extension=c
TEST_F(LibAbcKitInspectApiFilesTest, StaticFileGetVersion)
{
    LIBABCKIT_LOG(DEBUG) << "StaticFileGetVersion source: " << ABCKIT_TEST_DIR "ut/metadata_core/inspect_api/files\n";

    AbckitFile *file = nullptr;
    helpers::AssertOpenAbc(ABCKIT_ABC_DIR "ut/metadata_core/inspect_api/files/file_static.abc", &file);

    AbckitFileVersion version = g_implI->fileGetVersion(file);
    std::array<uint8_t, ABCKIT_VERSION_SIZE> expectedVersion = {0, 1, 0, 7};

    const auto versionsEquality = std::memcmp(expectedVersion.data(), version, sizeof(uint8_t) * ABCKIT_VERSION_SIZE);
    ASSERT_EQ(versionsEquality, 0);

    g_impl->closeFile(file);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
}

// Test: test-kind=api, api=InspectApiImpl::fileGetVersion, abc-kind=ArkTS1, category=positive, extension=c
TEST_F(LibAbcKitInspectApiFilesTest, DynamicFileGetVersion)
{
    LIBABCKIT_LOG(DEBUG) << "StaticFileGetVersion source: " << ABCKIT_TEST_DIR "ut/metadata_core/inspect_api/files\n";

    AbckitFile *file = nullptr;
    helpers::AssertOpenAbc(ABCKIT_ABC_DIR "ut/metadata_core/inspect_api/files/file_dynamic.abc", &file);

    AbckitFileVersion version = g_implI->fileGetVersion(file);
    // NOLINTNEXTLINE(readability-magic-numbers)
    std::array<uint8_t, ABCKIT_VERSION_SIZE> expectedVersion = {24, 0, 0, 0};
    const auto versionsEquality = std::memcmp(expectedVersion.data(), version, sizeof(uint8_t) * ABCKIT_VERSION_SIZE);
    ASSERT_EQ(versionsEquality, 0);

    g_impl->closeFile(file);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
}

// Test: test-kind=api, api=ApiImpl::writeAbc, abc-kind=ArkTS2, category=positive, extension=c
TEST_F(LibAbcKitInspectApiFilesTest, StaticWriteAbcPreservesFileVersion)
{
    constexpr auto INPUT_PATH = ABCKIT_ABC_DIR "ut/metadata_core/inspect_api/files/file_static.abc";
    constexpr auto OUTPUT_PATH = ABCKIT_ABC_DIR "ut/metadata_core/inspect_api/files/file_static_modified.abc";

    AbckitFile *file = nullptr;
    helpers::AssertOpenAbc(INPUT_PATH, &file);

    std::array<uint8_t, ABCKIT_VERSION_SIZE> inputVersion {};
    AbckitFileVersion version = g_implI->fileGetVersion(file);
    std::copy_n(version, inputVersion.size(), inputVersion.begin());

    g_impl->writeAbc(file, OUTPUT_PATH, strlen(OUTPUT_PATH));
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    g_impl->closeFile(file);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);

    helpers::AssertOpenAbc(OUTPUT_PATH, &file);
    version = g_implI->fileGetVersion(file);
    ASSERT_EQ(std::memcmp(inputVersion.data(), version, sizeof(uint8_t) * ABCKIT_VERSION_SIZE), 0);

    g_impl->closeFile(file);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
}

// Test: test-kind=api, api=ApiImpl::writeAbc, abc-kind=ArkTS1, category=positive, extension=c
TEST_F(LibAbcKitInspectApiFilesTest, DynamicWriteAbcPreservesFileVersion)
{
    constexpr auto INPUT_PATH = ABCKIT_ABC_DIR "ut/metadata_core/inspect_api/files/file_dynamic.abc";
    constexpr auto OUTPUT_PATH = ABCKIT_ABC_DIR "ut/metadata_core/inspect_api/files/file_dynamic_modified.abc";

    AbckitFile *file = nullptr;
    helpers::AssertOpenAbc(INPUT_PATH, &file);

    std::array<uint8_t, ABCKIT_VERSION_SIZE> inputVersion {};
    AbckitFileVersion version = g_implI->fileGetVersion(file);
    std::copy_n(version, inputVersion.size(), inputVersion.begin());

    g_impl->writeAbc(file, OUTPUT_PATH, strlen(OUTPUT_PATH));
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    g_impl->closeFile(file);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);

    helpers::AssertOpenAbc(OUTPUT_PATH, &file);
    version = g_implI->fileGetVersion(file);
    ASSERT_EQ(std::memcmp(inputVersion.data(), version, sizeof(uint8_t) * ABCKIT_VERSION_SIZE), 0);

    g_impl->closeFile(file);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
}

// Test: test-kind=api, api=InspectApiImpl::fileEnumerateModules, abc-kind=ArkTS1, category=positive, extension=c
TEST_F(LibAbcKitInspectApiFilesTest, DynamicFileEnumerateModules)
{
    AbckitFile *file = nullptr;
    helpers::AssertOpenAbc(ABCKIT_ABC_DIR "ut/metadata_core/inspect_api/modules/modules_dynamic.abc", &file);

    std::unordered_map<std::string, AbckitCoreModule *> gotModulesMap;

    g_implI->fileEnumerateModules(file, &gotModulesMap, helpers::NameToModuleCollector);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);

    std::set<std::string> expectedNames = {"modules_dynamic", "modules/module1", "modules/module2", "modules/module3"};
    ASSERT_EQ(expectedNames.size(), gotModulesMap.size());
    for (auto &expectedName : expectedNames) {
        ASSERT_NE(gotModulesMap.find(expectedName), gotModulesMap.end());
    }

    constexpr auto OUTPUT_PATH = ABCKIT_ABC_DIR "ut/metadata_core/inspect_api/modules/modules_dynamic_modified.abc";
    g_impl->writeAbc(file, OUTPUT_PATH, strlen(OUTPUT_PATH));
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    g_impl->closeFile(file);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
}

// Test: test-kind=api, api=ApiImpl::setNeedOptimize, abc-kind=ArkTS1, category=negative, extension=c
TEST_F(LibAbcKitInspectApiFilesTest, DeprecatedSetNeedOptimizeRejectsNull)
{
    ASSERT_NE(g_impl->setNeedOptimize, nullptr);
    for (bool hint : {false, true}) {
        g_impl->setNeedOptimize(nullptr, hint);
        EXPECT_EQ(g_impl->getLastError(), ABCKIT_STATUS_BAD_ARGUMENT);
    }
}

// Per-hint body of CheckDeprecatedSetNeedOptimize: setNeedOptimize must not re-enable delayed
// writeback or a cached mutable Graph; the round-trip output must reflect the rewritten string.
static void CheckSetNeedOptimizeForHint(bool isDynamic, bool hint, AbckitFile *&file, const char *input,
                                        const char *output)
{
    auto *modify = AbckitGetModifyApiImpl(ABCKIT_VERSION_RELEASE_1_0_0);
    auto *graphApi = AbckitGetGraphApiImpl(ABCKIT_VERSION_RELEASE_1_0_0);
    auto findString = [isDynamic](AbckitGraph *graph) {
        return isDynamic ? helpers::FindFirstInst(graph, ABCKIT_ISA_API_DYNAMIC_OPCODE_LOADSTRING)
                         : helpers::FindFirstInst(graph, ABCKIT_ISA_API_STATIC_OPCODE_LOADSTRING);
    };
    SCOPED_TRACE(hint);
    g_impl->setNeedOptimize(nullptr, hint);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_BAD_ARGUMENT);
    g_impl->setNeedOptimize(file, hint);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);

    auto *function = helpers::FindMethodByName(file, "foo");
    ASSERT_NE(function, nullptr);
    auto *graph = g_implI->createGraphFromFunction(function);
    ASSERT_NE(graph, nullptr);
    auto *loadString = findString(graph);
    ASSERT_NE(loadString, nullptr);
    const char *expected = hint ? "hint_true" : "hint_false";
    auto *value = modify->createString(file, expected, strlen(expected));
    ASSERT_NE(value, nullptr);
    graphApi->iSetString(loadString, value);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    modify->functionSetGraph(function, graph);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    g_impl->destroyGraph(graph);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);

    // The hint must not re-enable delayed writeback or a cached mutable Graph.
    helpers::InspectMethod(file, "foo", [&](AbckitFile *, AbckitCoreFunction *, AbckitGraph *freshGraph) {
        auto *freshString = findString(freshGraph);
        ASSERT_NE(freshString, nullptr);
        EXPECT_EQ(helpers::AbckitStringToString(graphApi->iGetString(freshString)), expected);
    });
    ASSERT_FALSE(::testing::Test::HasFatalFailure());
    g_impl->writeAbc(file, output, strlen(output));
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    g_impl->closeFile(file);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    auto result = isDynamic ? helpers::ExecuteDynamicAbc(output, "string_dynamic")
                            : helpers::ExecuteStaticAbc(output, "string_static", "main");
    EXPECT_EQ(result, std::string(expected) + "\n");
    helpers::AssertOpenAbc(output, &file);
    ASSERT_NE(file, nullptr);
}

static void CheckDeprecatedSetNeedOptimize(bool isDynamic)
{
    const char *input = isDynamic ? ABCKIT_ABC_DIR "ut/ir_core/string/string_dynamic.abc"
                                  : ABCKIT_ABC_DIR "ut/ir_core/string/string_static.abc";
    const char *output = isDynamic ? ABCKIT_ABC_DIR "ut/ir_core/string/string_dynamic_legacy_hint.abc"
                                   : ABCKIT_ABC_DIR "ut/ir_core/string/string_static_legacy_hint.abc";
    AbckitFile *file = nullptr;
    helpers::AssertOpenAbc(input, &file);
    ASSERT_NE(file, nullptr);
    ASSERT_NE(g_impl->setNeedOptimize, nullptr);
    for (bool hint : {true, false}) {
        ASSERT_NO_FATAL_FAILURE(CheckSetNeedOptimizeForHint(isDynamic, hint, file, input, output));
    }
    g_impl->closeFile(file);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
}

// Test: test-kind=api, api=ApiImpl::setNeedOptimize, abc-kind=ArkTS1, category=positive, extension=c
TEST_F(LibAbcKitInspectApiFilesTest, DynamicDeprecatedSetNeedOptimizePreservesRoundTrip)
{
    CheckDeprecatedSetNeedOptimize(true);
}

// Test: test-kind=api, api=ApiImpl::setNeedOptimize, abc-kind=ArkTS2, category=positive, extension=c
TEST_F(LibAbcKitInspectApiFilesTest, StaticDeprecatedSetNeedOptimizePreservesRoundTrip)
{
    CheckDeprecatedSetNeedOptimize(false);
}

}  // namespace libabckit::test
