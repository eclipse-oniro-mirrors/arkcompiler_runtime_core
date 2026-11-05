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

#include <gtest/gtest.h>
#include <cstring>
#include <string_view>

#include "libabckit/c/abckit.h"
#include "libabckit/c/metadata_core.h"

namespace libabckit::test {

static auto g_impl = AbckitGetApiImpl(ABCKIT_VERSION_RELEASE_1_0_0);
static auto g_implI = AbckitGetInspectApiImpl(ABCKIT_VERSION_RELEASE_1_0_0);
static auto g_implM = AbckitGetModifyApiImpl(ABCKIT_VERSION_RELEASE_1_0_0);

class LibAbcKitInspectApiStringsTest : public ::testing::Test {};

// Test: test-kind=api, api=InspectApiImpl::abckitStringToString, abc-kind=ArkTS2, category=positive, extension=c
TEST_F(LibAbcKitInspectApiStringsTest, AbckitStringToStringStatic)
{
    ASSERT_NE(g_impl, nullptr);
    ASSERT_NE(g_implI, nullptr);

    g_implI->abckitStringToString(nullptr);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_BAD_ARGUMENT);

    constexpr auto INPUT_PATH = ABCKIT_ABC_DIR "internal/implementation_api/abc_static.abc";
    auto *file = g_impl->openAbc(INPUT_PATH, strlen(INPUT_PATH));
    ASSERT_NE(file, nullptr);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);

    constexpr std::string_view TEST_STRING = "testString";
    auto *abcTestString = g_implM->createString(file, TEST_STRING.data(), TEST_STRING.size());
    ASSERT_NE(abcTestString, nullptr);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    auto name = g_implI->abckitStringToString(abcTestString);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    ASSERT_EQ(name, TEST_STRING);

    g_impl->closeFile(file);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
}

// Test: test-kind=api, api=InspectApiImpl::abckitStringToString, abc-kind=ArkTS1, category=positive, extension=c
TEST_F(LibAbcKitInspectApiStringsTest, AbckitStringToStringDynamic)
{
    ASSERT_NE(g_impl, nullptr);
    ASSERT_NE(g_implI, nullptr);

    g_implI->abckitStringToString(nullptr);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_BAD_ARGUMENT);

    constexpr auto INPUT_PATH = ABCKIT_ABC_DIR "internal/implementation_api/abc_dynamic.abc";
    auto *file = g_impl->openAbc(INPUT_PATH, strlen(INPUT_PATH));
    ASSERT_NE(file, nullptr);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);

    constexpr std::string_view TEST_STRING = "testString";
    auto *abcTestString = g_implM->createString(file, TEST_STRING.data(), TEST_STRING.size());
    ASSERT_NE(abcTestString, nullptr);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    auto name = g_implI->abckitStringToString(abcTestString);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
    ASSERT_EQ(name, TEST_STRING);

    g_impl->closeFile(file);
    ASSERT_EQ(g_impl->getLastError(), ABCKIT_STATUS_NO_ERROR);
}

}  // namespace libabckit::test
