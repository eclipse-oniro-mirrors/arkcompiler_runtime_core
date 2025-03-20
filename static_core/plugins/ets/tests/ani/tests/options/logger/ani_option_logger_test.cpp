/**
 * Copyright (c) 2025 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License"
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
#include "ani.h"

namespace ark::ets::ani::testing {

static int g_count = 0;
static int g_level = -1;
static std::string g_component;  // NOLINT(fuchsia-statically-constructed-objects)
static std::string g_message;    // NOLINT(fuchsia-statically-constructed-objects)

static void LoggerCallback([[maybe_unused]] FILE *stream, int level, const char *component, const char *message)
{
    ++g_count;
    g_level = level;
    g_component = component;
    g_message = message;
}

TEST(AniOptionLoggerTest, logger)
{
    // clang-format off
    std::array optionsArray {
        ani_option {"--ext:bla-bla-bla", nullptr},
        ani_option {"--logger", reinterpret_cast<void *>(LoggerCallback)}
    };
    // clang-format on

    ani_options options = {optionsArray.size(), optionsArray.data()};
    ani_vm *vm;
    ASSERT_EQ(ANI_CreateVM(&options, ANI_VERSION_1, &vm), ANI_ERROR);

    ASSERT_EQ(g_count, 1);
    ASSERT_EQ(g_level, ANI_LOGLEVEL_ERROR);
    ASSERT_STREQ(g_component.c_str(), "ani");
    ASSERT_STREQ(g_message.c_str(), "pandargs: Invalid option \"bla-bla-bla\"");
}

}  // namespace ark::ets::ani::testing
