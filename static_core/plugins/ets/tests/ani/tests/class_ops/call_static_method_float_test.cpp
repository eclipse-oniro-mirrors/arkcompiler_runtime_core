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

#include "ani_gtest.h"

// NOLINTBEGIN(cppcoreguidelines-pro-type-vararg, modernize-avoid-c-arrays)
namespace ark::ets::ani::testing {

class CallStaticMethodTest : public AniTest {
public:
    static constexpr ani_float FLOAT_VAL1 = 1.5F;
    static constexpr ani_float FLOAT_VAL2 = 2.5F;
    static constexpr size_t ARG_COUNT = 2U;

    void GetMethodData(ani_class *clsResult, ani_static_method *methodResult)
    {
        ani_class cls;
        ASSERT_EQ(env_->FindClass("LOperations;", &cls), ANI_OK);
        ASSERT_NE(cls, nullptr);

        ani_static_method method;
        ASSERT_EQ(env_->Class_FindStaticMethod(cls, "sum", "FF:F", &method), ANI_OK);
        ASSERT_NE(method, nullptr);

        *clsResult = cls;
        *methodResult = method;
    }
};

TEST_F(CallStaticMethodTest, call_static_method_float)
{
    ani_class cls;
    ani_static_method method;
    GetMethodData(&cls, &method);

    ani_float sum;
    ASSERT_EQ(env_->c_api->Class_CallStaticMethod_Float(env_, cls, method, &sum, FLOAT_VAL1, FLOAT_VAL2), ANI_OK);
    ASSERT_EQ(sum, FLOAT_VAL1 + FLOAT_VAL2);
}

TEST_F(CallStaticMethodTest, call_static_method_float_v)
{
    ani_class cls;
    ani_static_method method;
    GetMethodData(&cls, &method);

    ani_float sum;
    ASSERT_EQ(env_->Class_CallStaticMethod_Float(cls, method, &sum, FLOAT_VAL1, FLOAT_VAL2), ANI_OK);
    ASSERT_EQ(sum, FLOAT_VAL1 + FLOAT_VAL2);
}

TEST_F(CallStaticMethodTest, call_static_method_float_A)
{
    ani_class cls;
    ani_static_method method;
    GetMethodData(&cls, &method);

    ani_value args[ARG_COUNT];
    args[0U].f = FLOAT_VAL1;
    args[1U].f = FLOAT_VAL2;

    ani_float sum;
    ASSERT_EQ(env_->Class_CallStaticMethod_Float_A(cls, method, &sum, args), ANI_OK);
    ASSERT_EQ(sum, FLOAT_VAL1 + FLOAT_VAL2);
}

TEST_F(CallStaticMethodTest, call_static_method_float_null_class)
{
    ani_class cls;
    ani_static_method method;
    GetMethodData(&cls, &method);

    ani_float sum;
    ASSERT_EQ(env_->Class_CallStaticMethod_Float(nullptr, method, &sum, FLOAT_VAL1, FLOAT_VAL2), ANI_INVALID_ARGS);
}

TEST_F(CallStaticMethodTest, call_static_method_float_null_method)
{
    ani_class cls;
    ani_static_method method;
    GetMethodData(&cls, &method);

    ani_float sum;
    ASSERT_EQ(env_->Class_CallStaticMethod_Float(cls, nullptr, &sum, FLOAT_VAL1, FLOAT_VAL2), ANI_INVALID_ARGS);
}

TEST_F(CallStaticMethodTest, call_static_method_float_null_result)
{
    ani_class cls;
    ani_static_method method;
    GetMethodData(&cls, &method);

    ASSERT_EQ(env_->Class_CallStaticMethod_Float(cls, method, nullptr, FLOAT_VAL1, FLOAT_VAL2), ANI_INVALID_ARGS);
}

TEST_F(CallStaticMethodTest, call_static_method_float_A_null_class)
{
    ani_class cls;
    ani_static_method method;
    GetMethodData(&cls, &method);

    ani_value args[ARG_COUNT];
    args[0U].f = FLOAT_VAL1;
    args[1U].f = FLOAT_VAL2;

    ani_float sum;
    ASSERT_EQ(env_->Class_CallStaticMethod_Float_A(nullptr, method, &sum, args), ANI_INVALID_ARGS);
}

TEST_F(CallStaticMethodTest, call_static_method_float_A_null_method)
{
    ani_class cls;
    ani_static_method method;
    GetMethodData(&cls, &method);

    ani_value args[ARG_COUNT];
    args[0U].f = FLOAT_VAL1;
    args[1U].f = FLOAT_VAL2;

    ani_float sum;
    ASSERT_EQ(env_->Class_CallStaticMethod_Float_A(cls, nullptr, &sum, args), ANI_INVALID_ARGS);
}

TEST_F(CallStaticMethodTest, call_static_method_float_A_null_result)
{
    ani_class cls;
    ani_static_method method;
    GetMethodData(&cls, &method);

    ani_value args[ARG_COUNT];
    args[0U].f = FLOAT_VAL1;
    args[1U].f = FLOAT_VAL2;

    ASSERT_EQ(env_->Class_CallStaticMethod_Float_A(cls, method, nullptr, args), ANI_INVALID_ARGS);
}

TEST_F(CallStaticMethodTest, call_static_method_float_A_null_args)
{
    ani_class cls;
    ani_static_method method;
    GetMethodData(&cls, &method);

    ani_float sum;
    ASSERT_EQ(env_->Class_CallStaticMethod_Float_A(cls, method, &sum, nullptr), ANI_INVALID_ARGS);
}
}  // namespace ark::ets::ani::testing
// NOLINTEND(cppcoreguidelines-pro-type-vararg, modernize-avoid-c-arrays)
