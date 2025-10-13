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

#ifndef ANI_GTEST_ARRAY_GTEST_HELPER_H
#define ANI_GTEST_ARRAY_GTEST_HELPER_H

#include "ani_gtest.h"

namespace ark::ets::ani::testing {

class ArrayHelperTest : public AniTest {
public:
    void InitTest()
    {
        env_->FindClass("std.core.Boolean", &booleanClass);
        env_->FindClass("std.core.Int", &intClass);
        ASSERT(booleanClass != nullptr);
        ASSERT(intClass != nullptr);
        env_->Class_FindMethod(booleanClass, "<ctor>", "z:", &booleanCtor);
        env_->Class_FindMethod(intClass, "<ctor>", "i:", &intCtor);
        ASSERT(booleanCtor != nullptr);
        ASSERT(intCtor != nullptr);
        env_->Class_FindMethod(booleanClass, "toBoolean", ":z", &booleanUnbox);
        env_->Class_FindMethod(intClass, "toInt", ":i", &intUnbox);
        ASSERT(booleanUnbox != nullptr);
        ASSERT(intUnbox != nullptr);
    }
    // NOLINTBEGIN(misc-non-private-member-variables-in-classes)
    ani_class booleanClass = nullptr;
    ani_class intClass = nullptr;
    ani_method booleanCtor = nullptr;
    ani_method intCtor = nullptr;
    ani_method booleanUnbox = nullptr;
    ani_method intUnbox = nullptr;
    // NOLINTEND(misc-non-private-member-variables-in-classes)

    template <typename T, ani_size LENGTH = 5U>
    void CompareArray(const std::array<T, LENGTH> &nativeBuffer1, const std::array<T, LENGTH> &nativeBuffer2)
    {
        for (ani_size i = 0; i < LENGTH; i++) {
            ASSERT_EQ(nativeBuffer1[i], nativeBuffer2[i]);
        }
    }

    static constexpr ani_size OFFSET_0 = 0;
    static constexpr ani_size OFFSET_1 = 1;
    static constexpr ani_size OFFSET_2 = 2;
    static constexpr ani_size OFFSET_3 = 3;
    static constexpr ani_size OFFSET_4 = 4;
    static constexpr ani_size OFFSET_5 = 5;
    static constexpr ani_size LENGTH_1 = 1;
    static constexpr ani_size LENGTH_2 = 2;
    static constexpr ani_size LENGTH_3 = 3;
    static constexpr ani_size LENGTH_5 = 5;
    static constexpr ani_size LENGTH_6 = 6;
    static constexpr ani_size LENGTH_10 = 10;
    static constexpr ani_int LOOP_COUNT = 3;
};
}  // namespace ark::ets::ani::testing

#endif  // ANI_GTEST_ARRAY_GTEST_HELPER_H
