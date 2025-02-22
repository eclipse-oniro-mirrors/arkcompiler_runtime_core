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

// NOLINTBEGIN(cppcoreguidelines-pro-type-vararg, modernize-avoid-c-arrays)
namespace ark::ets::ani::testing {

class CallStaticMethodTest : public AniTest {
public:
    void GetMethodData(ani_class *clsResult)
    {
        ani_class cls;
        ASSERT_EQ(env_->FindClass("LPhone;", &cls), ANI_OK);
        ASSERT_NE(cls, nullptr);
        *clsResult = cls;
    }
};

TEST_F(CallStaticMethodTest, call_static_method_by_name_ref_one)
{
    ani_class cls = nullptr;
    GetMethodData(&cls);

    ani_ref ref = nullptr;
    ASSERT_EQ(env_->Class_CallStaticMethodByName_Ref(cls, "get_button_names", &ref), ANI_OK);
    ASSERT_NE(ref, nullptr);
}

TEST_F(CallStaticMethodTest, call_static_method_by_name_ref_two)
{
    ani_class cls = nullptr;
    GetMethodData(&cls);

    ani_ref ref = nullptr;
    ASSERT_EQ(env_->c_api->Class_CallStaticMethodByName_Ref(env_, cls, "get_button_names", &ref), ANI_OK);
    ASSERT_NE(ref, nullptr);

    auto string = reinterpret_cast<ani_string>(ref);
    ani_size result = 0U;
    ASSERT_EQ(env_->String_GetUTF8Size(string, &result), ANI_OK);
    ASSERT_EQ(result, 2U);

    ani_size substrOffset = 0U;
    ani_size substrSize = result;
    const uint32_t bufferSize = 5U;
    char utfBuffer[bufferSize] = {};
    result = 0U;
    auto status =
        env_->String_GetUTF8SubString(string, substrOffset, substrSize, utfBuffer, sizeof(utfBuffer), &result);
    ASSERT_EQ(status, ANI_OK);
    ASSERT_STREQ(utfBuffer, "up");
}

TEST_F(CallStaticMethodTest, call_static_method_by_name_ref_v)
{
    ani_class cls = nullptr;
    GetMethodData(&cls);

    ani_ref ref = nullptr;
    ASSERT_EQ(env_->Class_CallStaticMethodByName_Ref(cls, "get_num_string", &ref, 5U, 6U), ANI_OK);
    ASSERT_NE(ref, nullptr);

    auto string = reinterpret_cast<ani_string>(ref);
    ani_size result = 0U;
    ASSERT_EQ(env_->String_GetUTF8Size(string, &result), ANI_OK);
    ani_size substrOffset = 0U;
    ani_size substrSize = result;
    const uint32_t bufferSize = 10U;
    char utfBuffer[bufferSize] = {};
    result = 0U;
    auto status =
        env_->String_GetUTF8SubString(string, substrOffset, substrSize, utfBuffer, sizeof(utfBuffer), &result);
    ASSERT_EQ(status, ANI_OK);
    ASSERT_STREQ(utfBuffer, "INT5");
}

TEST_F(CallStaticMethodTest, call_static_method_by_name_ref_a)
{
    ani_class cls = nullptr;
    GetMethodData(&cls);

    ani_ref ref = nullptr;
    ani_value args[2U];
    args[0U].i = 5U;
    args[1U].i = 6U;
    ASSERT_EQ(env_->Class_CallStaticMethodByName_Ref_A(cls, "get_num_string", &ref, args), ANI_OK);
    ASSERT_NE(ref, nullptr);

    auto string = reinterpret_cast<ani_string>(ref);
    ani_size result = 0U;
    ASSERT_EQ(env_->String_GetUTF8Size(string, &result), ANI_OK);
    ani_size substrOffset = 0U;
    ani_size substrSize = result;
    const uint32_t bufferSize = 10U;
    char utfBuffer[bufferSize] = {};
    result = 0U;
    auto status =
        env_->String_GetUTF8SubString(string, substrOffset, substrSize, utfBuffer, sizeof(utfBuffer), &result);
    ASSERT_EQ(status, ANI_OK);
    ASSERT_STREQ(utfBuffer, "INT5");
}

TEST_F(CallStaticMethodTest, call_static_method_by_name_ref_invalid_cls)
{
    ani_ref ref = nullptr;
    ASSERT_EQ(env_->c_api->Class_CallStaticMethodByName_Ref(env_, nullptr, "get_button_names", &ref), ANI_INVALID_ARGS);
    ASSERT_EQ(ref, nullptr);
}

TEST_F(CallStaticMethodTest, call_static_method_by_name_ref_invalid_name)
{
    ani_class cls = nullptr;
    GetMethodData(&cls);

    ani_ref ref = nullptr;
    ASSERT_EQ(env_->c_api->Class_CallStaticMethodByName_Ref(env_, cls, nullptr, &ref), ANI_INVALID_ARGS);
    ASSERT_EQ(env_->c_api->Class_CallStaticMethodByName_Ref(env_, cls, "sum_not_exist", &ref), ANI_NOT_FOUND);
    ASSERT_EQ(ref, nullptr);
}

TEST_F(CallStaticMethodTest, call_static_method_by_name_ref_invalid_result)
{
    ani_class cls = nullptr;
    GetMethodData(&cls);

    ani_ref ref = nullptr;
    ASSERT_EQ(env_->c_api->Class_CallStaticMethodByName_Ref(env_, cls, "get_button_names", nullptr), ANI_INVALID_ARGS);
    ASSERT_EQ(ref, nullptr);
}

TEST_F(CallStaticMethodTest, call_static_method_by_name_ref_v_invalid_cls)
{
    ani_ref ref;
    ASSERT_EQ(env_->Class_CallStaticMethodByName_Ref(nullptr, "get_num_string", &ref, 5U, 6U), ANI_INVALID_ARGS);
}

TEST_F(CallStaticMethodTest, call_static_method_by_name_ref_v_invalid_name)
{
    ani_class cls = nullptr;
    GetMethodData(&cls);

    ani_ref ref;
    ASSERT_EQ(env_->Class_CallStaticMethodByName_Ref(cls, nullptr, &ref, 5U, 6U), ANI_INVALID_ARGS);
    ASSERT_EQ(env_->Class_CallStaticMethodByName_Ref(cls, "sum_not_exist", &ref, 5U, 6U), ANI_NOT_FOUND);
}

TEST_F(CallStaticMethodTest, call_static_method_by_name_ref_v_invalid_result)
{
    ani_class cls = nullptr;
    GetMethodData(&cls);

    ASSERT_EQ(env_->Class_CallStaticMethodByName_Ref(cls, "get_num_string", nullptr, 5U, 6U), ANI_INVALID_ARGS);
}

TEST_F(CallStaticMethodTest, call_static_method_by_name_ref_a_invalid_cls)
{
    ani_value args[2U];
    args[0U].i = 5U;
    args[1U].i = 6U;
    ani_ref ref;
    ASSERT_EQ(env_->Class_CallStaticMethodByName_Ref_A(nullptr, "get_num_string", &ref, args), ANI_INVALID_ARGS);
}

TEST_F(CallStaticMethodTest, call_static_method_by_name_ref_a_invalid_name)
{
    ani_class cls = nullptr;
    GetMethodData(&cls);

    ani_value args[2U];
    args[0U].i = 5U;
    args[1U].i = 6U;
    ani_ref ref;
    ASSERT_EQ(env_->Class_CallStaticMethodByName_Ref_A(cls, nullptr, &ref, args), ANI_INVALID_ARGS);
}

TEST_F(CallStaticMethodTest, call_static_method_by_name_ref_a_invalid_result)
{
    ani_class cls = nullptr;
    GetMethodData(&cls);

    ani_value args[2U];
    args[0U].i = 5U;
    args[1U].i = 6U;
    ASSERT_EQ(env_->Class_CallStaticMethodByName_Ref_A(cls, "get_num_string", nullptr, args), ANI_INVALID_ARGS);
}

TEST_F(CallStaticMethodTest, call_static_method_by_name_ref_a_invalid_args)
{
    ani_class cls = nullptr;
    GetMethodData(&cls);

    ani_ref ref;
    ASSERT_EQ(env_->Class_CallStaticMethodByName_Ref_A(cls, "get_num_string", &ref, nullptr), ANI_INVALID_ARGS);
}
}  // namespace ark::ets::ani::testing

// NOLINTEND(cppcoreguidelines-pro-type-vararg, modernize-avoid-c-arrays)