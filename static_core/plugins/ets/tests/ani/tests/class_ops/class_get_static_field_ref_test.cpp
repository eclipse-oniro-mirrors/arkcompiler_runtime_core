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

#include "ani_gtest.h"

// NOLINTBEGIN(cppcoreguidelines-pro-type-vararg, modernize-avoid-c-arrays,  readability-magic-numbers)
namespace ark::ets::ani::testing {

class ClassGetStaticFieldRefTest : public AniTest {};

TEST_F(ClassGetStaticFieldRefTest, get_ref)
{
    ani_class cls;
    ASSERT_EQ(env_->FindClass("LTestRef;", &cls), ANI_OK);
    ani_static_field field;
    ASSERT_EQ(env_->Class_FindStaticField(cls, "string_value", &field), ANI_OK);
    ASSERT_NE(field, nullptr);
    ani_ref result = nullptr;
    ASSERT_EQ(env_->Class_GetStaticField_Ref(cls, field, &result), ANI_OK);
    auto getRes = static_cast<ani_string>(result);
    std::array<char, 11U> buffer {};
    ani_size resSize;
    ASSERT_EQ(env_->String_GetUTF8SubString(getRes, 0U, 10U, buffer.data(), buffer.size(), &resSize), ANI_OK);
    ASSERT_EQ(resSize, 10U);
    ASSERT_STREQ(buffer.data(), "testString");
}

TEST_F(ClassGetStaticFieldRefTest, get_ref_c_api)
{
    ani_class cls;
    ASSERT_EQ(env_->FindClass("LTestRef;", &cls), ANI_OK);
    ani_static_field field;
    ASSERT_EQ(env_->Class_FindStaticField(cls, "string_value", &field), ANI_OK);
    ASSERT_NE(field, nullptr);
    ani_ref result = nullptr;
    ASSERT_EQ(env_->c_api->Class_GetStaticField_Ref(env_, cls, field, &result), ANI_OK);
    auto getRes = static_cast<ani_string>(result);
    std::array<char, 11U> buffer {};
    ani_size resSize;
    ASSERT_EQ(env_->String_GetUTF8SubString(getRes, 0U, 10U, buffer.data(), buffer.size(), &resSize), ANI_OK);
    ASSERT_EQ(resSize, 10U);
    ASSERT_STREQ(buffer.data(), "testString");
}

TEST_F(ClassGetStaticFieldRefTest, get_invalid_field_type)
{
    ani_class cls;
    ASSERT_EQ(env_->FindClass("LTestRef;", &cls), ANI_OK);
    ani_static_field field;
    ASSERT_EQ(env_->Class_FindStaticField(cls, "int_value", &field), ANI_OK);
    ASSERT_NE(field, nullptr);
    ani_ref result = nullptr;
    ASSERT_EQ(env_->Class_GetStaticField_Ref(cls, field, &result), ANI_INVALID_TYPE);
}

TEST_F(ClassGetStaticFieldRefTest, invalid_argument1)
{
    ani_class cls;
    ASSERT_EQ(env_->FindClass("LTestRef;", &cls), ANI_OK);
    ani_static_field field;
    ASSERT_EQ(env_->Class_FindStaticField(cls, "string_value", &field), ANI_OK);
    ASSERT_NE(field, nullptr);
    ani_ref result = nullptr;
    ASSERT_EQ(env_->Class_GetStaticField_Ref(nullptr, field, &result), ANI_INVALID_ARGS);
}

TEST_F(ClassGetStaticFieldRefTest, invalid_argument2)
{
    ani_class cls;
    ASSERT_EQ(env_->FindClass("LTestRef;", &cls), ANI_OK);
    ani_ref result = nullptr;
    ASSERT_EQ(env_->Class_GetStaticField_Ref(cls, nullptr, &result), ANI_INVALID_ARGS);
}

TEST_F(ClassGetStaticFieldRefTest, invalid_argument3)
{
    ani_class cls;
    ASSERT_EQ(env_->FindClass("LTestRef;", &cls), ANI_OK);
    ani_static_field field;
    ASSERT_EQ(env_->Class_FindStaticField(cls, "string_value", &field), ANI_OK);
    ASSERT_NE(field, nullptr);
    ASSERT_EQ(env_->Class_GetStaticField_Ref(cls, field, nullptr), ANI_INVALID_ARGS);
}
}  // namespace ark::ets::ani::testing
   // NOLINTEND(cppcoreguidelines-pro-type-vararg, modernize-avoid-c-arrays, readability-magic-numbers)