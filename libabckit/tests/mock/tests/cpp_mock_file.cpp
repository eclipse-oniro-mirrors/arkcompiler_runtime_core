/**
 * Copyright (c) 2024 Huawei Device Co., Ltd.
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

#include "include/cpp/abckit_cpp.h"
#include "../check_mock.h"

#include <gtest/gtest.h>

namespace libabckit::test {

class LibAbcKitCppMockTest : public ::testing::Test {};

// Test: test-kind=mock, api=File::WriteAbc, abc-kind=ArkTS1, category=internal, extension=cpp
TEST_F(LibAbcKitCppMockTest, CppTestMockFile)
{
    {
        abckit::File file("abckit.abc");
        ASSERT_TRUE(CheckMockedApi("openAbc"));
        file.WriteAbc("abckit.abc");
        ASSERT_TRUE(CheckMockedApi("writeAbc"));
    }
    ASSERT_TRUE(CheckMockedApi("closeFile"));
}

}  // namespace libabckit::test
