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

namespace ark::ets::ani::testing {

class ObjectGetFieldFloatTest : public AniTest {
public:
    void GetTestData(ani_object *objectResult, ani_field *fieldNameResult, ani_field *fieldAgeResult)
    {
        auto sarahRef = CallEtsFunction<ani_ref>("object_get_field_float_test", "newSarahObject");
        auto sarah = static_cast<ani_object>(sarahRef);

        ani_class cls;
        ASSERT_EQ(env_->FindClass("Lobject_get_field_float_test/Woman;", &cls), ANI_OK);

        ani_field fieldName;
        ASSERT_EQ(env_->Class_FindField(cls, "name", &fieldName), ANI_OK);

        ani_field fieldAge;
        ASSERT_EQ(env_->Class_FindField(cls, "age", &fieldAge), ANI_OK);

        *objectResult = sarah;
        *fieldNameResult = fieldName;
        *fieldAgeResult = fieldAge;
    }
};

TEST_F(ObjectGetFieldFloatTest, get_field_float)
{
    ani_object sarah {};
    ani_field field {};
    ani_field fieldAge {};
    GetTestData(&sarah, &field, &fieldAge);

    ani_float age = 0.0F;
    ASSERT_EQ(env_->Object_GetField_Float(sarah, fieldAge, &age), ANI_OK);
    ASSERT_EQ(age, 24.0F);
}

TEST_F(ObjectGetFieldFloatTest, get_field_float_invalid_env)
{
    ani_object sarah {};
    ani_field field {};
    ani_field fieldAge {};
    GetTestData(&sarah, &field, &fieldAge);

    ani_float age = 0.0F;
    ASSERT_EQ(env_->c_api->Object_GetField_Float(nullptr, sarah, fieldAge, &age), ANI_INVALID_ARGS);
}

TEST_F(ObjectGetFieldFloatTest, get_field_float_invalid_field_type)
{
    ani_object sarah {};
    ani_field field {};
    ani_field fieldAge {};
    GetTestData(&sarah, &field, &fieldAge);

    ani_float age = 0.0F;
    ASSERT_EQ(env_->Object_GetField_Float(sarah, field, &age), ANI_INVALID_TYPE);
}

TEST_F(ObjectGetFieldFloatTest, invalid_argument1)
{
    ani_object sarah {};
    ani_field field {};
    ani_field fieldAge {};
    GetTestData(&sarah, &field, &fieldAge);

    ani_float age = 0.0F;
    ASSERT_EQ(env_->Object_GetField_Float(nullptr, field, &age), ANI_INVALID_ARGS);
}

TEST_F(ObjectGetFieldFloatTest, invalid_argument2)
{
    ani_object sarah {};
    ani_field field {};
    ani_field fieldAge {};
    GetTestData(&sarah, &field, &fieldAge);

    ani_float age = 0.0F;
    ASSERT_EQ(env_->Object_GetField_Float(sarah, nullptr, &age), ANI_INVALID_ARGS);
}

TEST_F(ObjectGetFieldFloatTest, invalid_argument3)
{
    ani_object sarah {};
    ani_field field {};
    ani_field fieldAge {};
    GetTestData(&sarah, &field, &fieldAge);

    ASSERT_EQ(env_->Object_GetField_Float(sarah, field, nullptr), ANI_INVALID_ARGS);
}

}  // namespace ark::ets::ani::testing
