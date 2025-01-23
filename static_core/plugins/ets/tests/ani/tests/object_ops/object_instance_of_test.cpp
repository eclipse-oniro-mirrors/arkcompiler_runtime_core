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
// NOLINTBEGIN(cppcoreguidelines-pro-type-vararg)
namespace ark::ets::ani::testing {

/**
 * @brief Unit test class for testing boolean method calls on ani objects.
 *
 * Inherits from the AniTest base class and provides test cases to verify
 * correct functionality of calling boolean-returning methods with various
 * parameter scenarios.
 */
class ObjectInstanceOfTest : public AniTest {
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
        ASSERT_EQ(env_->Class_GetStaticMethod(cls, newClassName, signature, &newMethod), ANI_OK);
        ani_ref ref;
        ASSERT_EQ(env_->Class_CallStaticMethod_Ref(cls, newMethod, &ref), ANI_OK);

        *objectResult = static_cast<ani_object>(ref);
        *classResult = cls;
    }
};

/**
 * @brief Test case for calling a boolean-returning method with an argument array.
 *
 * This test verifies the correct behavior of calling a method using an array
 * of integer arguments and checks the return value.
 */
TEST_F(ObjectInstanceOfTest, object_instance_of)
{
    ani_object objectA;
    ani_class classA;
    GetMethodData(&objectA, &classA, "LA;", "new_A", ":LA;");

    ani_object objectB;
    ani_class classB;
    GetMethodData(&objectB, &classB, "LB;", "new_B", ":LB;");

    ani_object objectC;
    ani_class classC;
    GetMethodData(&objectC, &classC, "LC;", "new_C", ":LC;");

    ani_object objectD;
    ani_class classD;
    GetMethodData(&objectD, &classD, "LD;", "new_D", ":LD;");

    ani_type typeRefC = classC;
    ani_type typeRefA = classA;
    ani_boolean res;

    ASSERT_EQ(env_->Object_InstanceOf(objectC, typeRefC, &res), ANI_OK);
    ASSERT_EQ(res, ANI_TRUE);

    ASSERT_EQ(env_->Object_InstanceOf(objectB, typeRefC, &res), ANI_OK);
    ASSERT_EQ(res, false);

    ASSERT_EQ(env_->Object_InstanceOf(objectC, typeRefA, &res), ANI_OK);
    ASSERT_EQ(res, ANI_TRUE);

    ASSERT_EQ(env_->Object_InstanceOf(objectD, typeRefA, &res), ANI_OK);
    ASSERT_EQ(res, ANI_FALSE);

    ASSERT_EQ(env_->Object_InstanceOf(nullptr, typeRefA, &res), ANI_INVALID_ARGS);

    ASSERT_EQ(env_->Object_InstanceOf(objectC, nullptr, &res), ANI_INVALID_ARGS);
}

}  // namespace ark::ets::ani::testing

// NOLINTEND(cppcoreguidelines-pro-type-vararg)