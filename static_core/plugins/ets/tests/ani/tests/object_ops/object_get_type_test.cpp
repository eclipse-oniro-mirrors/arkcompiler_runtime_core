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

/**
 * @brief Unit test class for retrieves the type of a given object.
 *
 * Inherits from the AniTest base class and provides test cases to verify
 * correct functionality of calling  retrieves the type of the specified object methods
 * with various parameter scenarios.
 */
class ObjectGetTypeTest : public AniTest {
public:
    void GetMethodData(ani_object *objectResult, ani_class *classResult, const char *className,
                       const char *newClassName, const char *signature)
    {
        ani_class cls;
        // Locate the class in the environment.
        ASSERT_EQ(env_->FindClass(className, &cls), ANI_OK);
        ASSERT_NE(cls, nullptr);

        // Emulate allocation an instance of class.
        ani_static_method newMethod;
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
        ASSERT_EQ(env_->Class_FindStaticMethod(cls, newClassName, signature, &newMethod), ANI_OK);
        ani_ref ref;
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
        ASSERT_EQ(env_->Class_CallStaticMethod_Ref(cls, newMethod, &ref), ANI_OK);

        *objectResult = static_cast<ani_object>(ref);
        *classResult = cls;
    }
};

TEST_F(ObjectGetTypeTest, class_obect_type)
{
    ani_class classA;
    ani_object objectA;
    GetMethodData(&objectA, &classA, "LA;", "new_A", ":LA;");

    ani_type type;
    ani_boolean res;
    ASSERT_EQ(env_->Object_GetType(objectA, &type), ANI_OK);
    ASSERT_EQ(env_->Object_InstanceOf(objectA, type, &res), ANI_OK);
    ASSERT_EQ(res, ANI_TRUE);
}

TEST_F(ObjectGetTypeTest, string_obect_type)
{
    ani_string result = nullptr;
    auto status = env_->String_NewUTF8("a", 1U, &result);
    ASSERT_EQ(status, ANI_OK);
    ASSERT_NE(result, nullptr);

    ani_type type;
    ani_boolean res;
    ASSERT_EQ(env_->Object_GetType(result, &type), ANI_OK);
    ASSERT_EQ(env_->Object_InstanceOf(result, type, &res), ANI_OK);
    ASSERT_EQ(res, ANI_TRUE);
}

TEST_F(ObjectGetTypeTest, invalid_parameters)
{
    ani_string result = nullptr;
    auto status = env_->String_NewUTF8("a", 1U, &result);
    ASSERT_EQ(status, ANI_OK);
    ASSERT_NE(result, nullptr);

    ASSERT_EQ(env_->Object_GetType(result, nullptr), ANI_INVALID_ARGS);
    ASSERT_EQ(env_->Object_GetType(nullptr, nullptr), ANI_INVALID_ARGS);
}
}  // namespace ark::ets::ani::testing

// NOLINTEND(cppcoreguidelines-pro-type-vararg)