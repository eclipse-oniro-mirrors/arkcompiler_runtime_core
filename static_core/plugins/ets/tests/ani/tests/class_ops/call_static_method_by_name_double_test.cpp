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
    static constexpr ani_double VAL1 = 1.5;
    static constexpr ani_double VAL2 = 2.5;
    static constexpr size_t ARG_COUNT = 2U;

    void GetMethodData(ani_class *clsResult)
    {
        ani_class cls;
        ASSERT_EQ(env_->FindClass("LOperations;", &cls), ANI_OK);
        ASSERT_NE(cls, nullptr);
        *clsResult = cls;
    }
};

TEST_F(CallStaticMethodTest, call_static_method_by_name_double)
{
    ani_class cls;
    GetMethodData(&cls);

    ani_double sum;
    ASSERT_EQ(env_->c_api->Class_CallStaticMethodByName_Double(env_, cls, "sum", &sum, VAL1, VAL2), ANI_OK);
    ASSERT_EQ(sum, VAL1 + VAL2);
}

TEST_F(CallStaticMethodTest, call_static_method_by_name_double_v)
{
    ani_class cls;
    GetMethodData(&cls);

    ani_double sum;
    ASSERT_EQ(env_->Class_CallStaticMethodByName_Double(cls, "sum", &sum, VAL1, VAL2), ANI_OK);
    ASSERT_EQ(sum, VAL1 + VAL2);
}

TEST_F(CallStaticMethodTest, call_static_method_by_name_double_A)
{
    ani_class cls;
    GetMethodData(&cls);

    ani_value args[ARG_COUNT];
    args[0U].d = VAL1;
    args[1U].d = VAL2;

    ani_double sum;
    ASSERT_EQ(env_->Class_CallStaticMethodByName_Double_A(cls, "sum", &sum, args), ANI_OK);
    ASSERT_EQ(sum, VAL1 + VAL2);
}

TEST_F(CallStaticMethodTest, call_static_method_by_name_double_null_class)
{
    ani_double sum;
    ASSERT_EQ(env_->Class_CallStaticMethodByName_Double(nullptr, "sum", &sum, VAL1, VAL2), ANI_INVALID_ARGS);
}

TEST_F(CallStaticMethodTest, call_static_method_double_by_name_null_name)
{
    ani_class cls;
    GetMethodData(&cls);

    ani_double sum;
    ASSERT_EQ(env_->Class_CallStaticMethodByName_Double(cls, nullptr, &sum, VAL1, VAL2), ANI_INVALID_ARGS);
    ASSERT_EQ(env_->Class_CallStaticMethodByName_Double(cls, "sum_not_exist", &sum, VAL1, VAL2), ANI_NOT_FOUND);
}

TEST_F(CallStaticMethodTest, call_static_method_by_name_double_null_result)
{
    ani_class cls;
    GetMethodData(&cls);

    ASSERT_EQ(env_->Class_CallStaticMethodByName_Double(cls, "sum", nullptr, VAL1, VAL2), ANI_INVALID_ARGS);
}

TEST_F(CallStaticMethodTest, call_static_method_by_name_double_A_null_class)
{
    ani_value args[ARG_COUNT];
    args[0U].d = VAL1;
    args[1U].d = VAL2;

    ani_double sum;
    ASSERT_EQ(env_->Class_CallStaticMethodByName_Double_A(nullptr, "sum", &sum, args), ANI_INVALID_ARGS);
}

TEST_F(CallStaticMethodTest, call_static_method_by_name_double_A_null_name)
{
    ani_class cls;
    GetMethodData(&cls);

    ani_value args[ARG_COUNT];
    args[0U].d = VAL1;
    args[1U].d = VAL2;

    ani_double sum;
    ASSERT_EQ(env_->Class_CallStaticMethodByName_Double_A(cls, nullptr, &sum, args), ANI_INVALID_ARGS);
    ASSERT_EQ(env_->Class_CallStaticMethodByName_Double_A(cls, "sum_not_exist", &sum, args), ANI_NOT_FOUND);
}

TEST_F(CallStaticMethodTest, call_static_method_by_name_double_A_null_result)
{
    ani_class cls;
    GetMethodData(&cls);

    ani_value args[ARG_COUNT];
    args[0U].d = VAL1;
    args[1U].d = VAL2;

    ASSERT_EQ(env_->Class_CallStaticMethodByName_Double_A(cls, "sum", nullptr, args), ANI_INVALID_ARGS);
}

TEST_F(CallStaticMethodTest, call_static_method_by_name_double_A_null_args)
{
    ani_class cls;
    GetMethodData(&cls);

    ani_double sum;
    ASSERT_EQ(env_->Class_CallStaticMethodByName_Double_A(cls, "sum", &sum, nullptr), ANI_INVALID_ARGS);
}
}  // namespace ark::ets::ani::testing
// NOLINTEND(cppcoreguidelines-pro-type-vararg, modernize-avoid-c-arrays)
