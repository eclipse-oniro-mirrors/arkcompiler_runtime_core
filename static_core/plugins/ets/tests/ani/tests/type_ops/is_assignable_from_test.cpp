/**
 * Copyright (c) 2025 Huawei Device Co., Ltd.
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

#include "ani/ani.h"
#include "ani_gtest.h"

// NOLINTBEGIN(cppcoreguidelines-pro-type-vararg, modernize-avoid-c-arrays)
namespace ark::ets::ani::testing {

class IsAssignableFromTest : public AniTest {
public:
    template <bool IS_ASSIGNABLE>
    void CheckIsAssignableFrom(const char *fromClsName, const char *toClsName)
    {
        ani_class fromCls;
        ASSERT_EQ(env_->FindClass(fromClsName, &fromCls), ANI_OK);
        ASSERT_NE(fromCls, nullptr);

        ani_class toCls;
        ASSERT_EQ(env_->FindClass(toClsName, &toCls), ANI_OK);
        ASSERT_NE(toCls, nullptr);

        ani_boolean result;
        ASSERT_EQ(env_->Type_IsAssignableFrom(fromCls, toCls, &result), ANI_OK);

        if constexpr (IS_ASSIGNABLE) {
            ASSERT_TRUE(result);
        } else {
            ASSERT_FALSE(result);
        }
    }
};

TEST_F(IsAssignableFromTest, is_assignable)
{
    CheckIsAssignableFrom<true>("LA;", "LA;");
    CheckIsAssignableFrom<true>("LB;", "LA;");
    CheckIsAssignableFrom<true>("LA;", "Lstd/core/Object;");
    CheckIsAssignableFrom<true>("LB;", "Lstd/core/Object;");
    CheckIsAssignableFrom<true>("LA;", "LI;");
    CheckIsAssignableFrom<true>("LB;", "LI;");
}

TEST_F(IsAssignableFromTest, not_assignable)
{
    CheckIsAssignableFrom<false>("LA;", "LB;");
    CheckIsAssignableFrom<false>("Lstd/core/Object;", "LA;");
    CheckIsAssignableFrom<false>("Lstd/core/Object;", "LB;");
    CheckIsAssignableFrom<false>("LI;", "LB;");
    CheckIsAssignableFrom<false>("LI;", "LA;");
}

TEST_F(IsAssignableFromTest, ani_invalid_args)
{
    ani_class clsA;

    ASSERT_EQ(env_->FindClass("LA;", &clsA), ANI_OK);
    ASSERT_NE(clsA, nullptr);

    ani_boolean result;
    ASSERT_EQ(env_->Type_IsAssignableFrom(nullptr, clsA, &result), ANI_INVALID_ARGS);
    ASSERT_EQ(env_->Type_IsAssignableFrom(clsA, nullptr, &result), ANI_INVALID_ARGS);
    ASSERT_EQ(env_->Type_IsAssignableFrom(clsA, clsA, nullptr), ANI_INVALID_ARGS);
}

TEST_F(IsAssignableFromTest, is_assignable_combind_scenes_001)
{
    CheckIsAssignableFrom<true>("LBaseA;", "LBaseA;");
    CheckIsAssignableFrom<true>("LSubB;", "LSubB;");
    CheckIsAssignableFrom<true>("LSubB;", "LBaseA;");
    CheckIsAssignableFrom<true>("LSubC;", "LSubC;");
    CheckIsAssignableFrom<true>("LSubC;", "LSubB;");
    CheckIsAssignableFrom<true>("LSubC;", "LBaseA;");
    CheckIsAssignableFrom<true>("LD;", "LD;");

    CheckIsAssignableFrom<false>("LBaseA;", "LSubB;");
    CheckIsAssignableFrom<false>("LBaseA;", "LSubC;");
    CheckIsAssignableFrom<false>("LSubB;", "LSubC;");
    CheckIsAssignableFrom<false>("LSubC;", "LD;");
    CheckIsAssignableFrom<false>("LSubB;", "LD;");
    CheckIsAssignableFrom<false>("LBaseA;", "LD;");
}
}  // namespace ark::ets::ani::testing

// NOLINTEND(cppcoreguidelines-pro-type-vararg, modernize-avoid-c-arrays)
