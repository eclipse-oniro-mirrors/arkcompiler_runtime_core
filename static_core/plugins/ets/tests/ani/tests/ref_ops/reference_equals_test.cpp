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

// NOLINTBEGIN(cppcoreguidelines-pro-type-vararg)
namespace ark::ets::ani::testing {

class ReferenceEqualsTest : public AniTest {
public:
    void GetMethodData(ani_ref *objectRef, ani_ref *methodRef, const char *className, const char *newClassName,
                       const char *signature)
    {
        ani_class cls {};
        // Locate the class in the environment.
        ASSERT_EQ(env_->FindClass(className, &cls), ANI_OK);
        ASSERT_NE(cls, nullptr);

        // Emulate allocation an instance of class.
        ani_static_method newMethod {};
        ASSERT_EQ(env_->Class_FindStaticMethod(cls, newClassName, signature, &newMethod), ANI_OK);
        ASSERT_EQ(env_->Class_CallStaticMethod_Ref(cls, newMethod, objectRef), ANI_OK);

        const char *methodSignature = "Lstd/core/String;Lstd/core/String;:Lstd/core/String;";
        ani_method concat {};
        ASSERT_EQ(env_->Class_FindMethod(cls, "concat", methodSignature, &concat), ANI_OK);
        ASSERT_NE(concat, nullptr);

        ani_string s0 {};
        ASSERT_EQ(env_->String_NewUTF8("abc", 3U, &s0), ANI_OK);
        ani_string s1 {};
        ASSERT_EQ(env_->String_NewUTF8("def", 3U, &s1), ANI_OK);

        ASSERT_EQ(env_->Object_CallMethod_Ref(static_cast<ani_object>(*objectRef), concat, methodRef, s0, s1), ANI_OK);
    }

    static constexpr int32_t LOOP_COUNT = 3;
};

TEST_F(ReferenceEqualsTest, check_null_and_null)
{
    auto nullRef1 = CallEtsFunction<ani_ref>("GetNull");
    auto nullRef2 = CallEtsFunction<ani_ref>("GetNull");
    ani_boolean isEquals = ANI_FALSE;
    ASSERT_EQ(env_->Reference_Equals(nullRef1, nullRef2, &isEquals), ANI_OK);
    ASSERT_EQ(isEquals, ANI_TRUE);
}

TEST_F(ReferenceEqualsTest, check_null_and_undefined)
{
    auto nullRef = CallEtsFunction<ani_ref>("GetNull");
    auto undefinedRef = CallEtsFunction<ani_ref>("GetUndefined");
    ani_boolean isEquals = ANI_FALSE;
    ASSERT_EQ(env_->Reference_Equals(nullRef, undefinedRef, &isEquals), ANI_OK);
    ASSERT_EQ(isEquals, ANI_TRUE);
}

TEST_F(ReferenceEqualsTest, check_null_and_object)
{
    auto nullRef = CallEtsFunction<ani_ref>("GetNull");
    auto objectRef = CallEtsFunction<ani_ref>("GetObject");
    ani_boolean isEquals = ANI_TRUE;
    ASSERT_EQ(env_->Reference_Equals(nullRef, objectRef, &isEquals), ANI_OK);
    ASSERT_EQ(isEquals, ANI_FALSE);
}

TEST_F(ReferenceEqualsTest, check_undefined_and_undefined)
{
    auto undefinedRef1 = CallEtsFunction<ani_ref>("GetUndefined");
    auto undefinedRef2 = CallEtsFunction<ani_ref>("GetUndefined");
    ani_boolean isEquals = ANI_FALSE;
    ASSERT_EQ(env_->Reference_Equals(undefinedRef1, undefinedRef2, &isEquals), ANI_OK);
    ASSERT_EQ(isEquals, ANI_TRUE);
}

TEST_F(ReferenceEqualsTest, check_undefined_and_object)
{
    auto undefinedRef = CallEtsFunction<ani_ref>("GetUndefined");
    auto objectRef = CallEtsFunction<ani_ref>("GetObject");
    ani_boolean isEquals = ANI_TRUE;
    ASSERT_EQ(env_->Reference_Equals(undefinedRef, objectRef, &isEquals), ANI_OK);
    ASSERT_EQ(isEquals, ANI_FALSE);
}

TEST_F(ReferenceEqualsTest, check_object_and_object)
{
    auto objectRef1 = CallEtsFunction<ani_ref>("GetObject");
    auto objectRef2 = CallEtsFunction<ani_ref>("GetObject");
    ani_boolean isEquals = ANI_FALSE;
    ASSERT_EQ(env_->Reference_Equals(objectRef1, objectRef2, &isEquals), ANI_OK);
    ASSERT_EQ(isEquals, ANI_TRUE);
}

TEST_F(ReferenceEqualsTest, invalid_argument)
{
    auto ref = CallEtsFunction<ani_ref>("GetNull");
    ASSERT_EQ(env_->Reference_Equals(ref, ref, nullptr), ANI_INVALID_ARGS);
    ani_boolean isEquals = ANI_FALSE;
    ASSERT_EQ(env_->c_api->Reference_Equals(nullptr, ref, ref, &isEquals), ANI_INVALID_ARGS);
}

TEST_F(ReferenceEqualsTest, check_custom_object)
{
    auto packRef1 = CallEtsFunction<ani_ref>("newPackObject");
    auto packRef2 = CallEtsFunction<ani_ref>("newPackObject");
    ani_boolean isEquals = ANI_TRUE;
    ASSERT_EQ(env_->Reference_Equals(packRef1, packRef2, &isEquals), ANI_OK);
    ASSERT_EQ(isEquals, ANI_FALSE);
}

TEST_F(ReferenceEqualsTest, check_custom_and_string)
{
    auto packRef = CallEtsFunction<ani_ref>("newPackObject");
    auto objectRef = CallEtsFunction<ani_ref>("GetObject");
    ani_boolean isEquals = ANI_TRUE;
    ASSERT_EQ(env_->Reference_Equals(packRef, objectRef, &isEquals), ANI_OK);
    ASSERT_EQ(isEquals, ANI_FALSE);
}

TEST_F(ReferenceEqualsTest, check_reference_equals_loop)
{
    for (int32_t i = 0; i < LOOP_COUNT; i++) {
        auto objectRef1 = CallEtsFunction<ani_ref>("GetObject");
        auto objectRef2 = CallEtsFunction<ani_ref>("GetObject");
        ani_boolean isEquals = ANI_FALSE;
        ASSERT_EQ(env_->Reference_Equals(objectRef1, objectRef2, &isEquals), ANI_OK);
        ASSERT_EQ(isEquals, ANI_TRUE);

        auto packRef = CallEtsFunction<ani_ref>("newPackObject");
        auto objectRef = CallEtsFunction<ani_ref>("GetObject");
        ASSERT_EQ(env_->Reference_Equals(packRef, objectRef, &isEquals), ANI_OK);
        ASSERT_EQ(isEquals, ANI_FALSE);
    }
}

TEST_F(ReferenceEqualsTest, check_object_and_method)
{
    ani_ref objectARef = nullptr;
    ani_ref methodARef = nullptr;
    GetMethodData(&objectARef, &methodARef, "LA;", "new_A", ":LA;");

    ani_ref objectBRef = nullptr;
    ani_ref methodBRef = nullptr;
    GetMethodData(&objectBRef, &methodBRef, "LB;", "new_B", ":LB;");

    ani_boolean isEquals = ANI_TRUE;
    ASSERT_EQ(env_->Reference_Equals(objectARef, objectBRef, &isEquals), ANI_OK);
    ASSERT_EQ(isEquals, ANI_FALSE);

    ASSERT_EQ(env_->Reference_Equals(methodARef, methodBRef, &isEquals), ANI_OK);
    ASSERT_EQ(isEquals, ANI_FALSE);
}
}  // namespace ark::ets::ani::testing

// NOLINTEND(cppcoreguidelines-pro-type-vararg)