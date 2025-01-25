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

class ObjectSetFieldRefTest : public AniTest {
public:
    void GetTestData(ani_object *boxResult, ani_field *fieldIntResult, ani_field *fieldStringResult)
    {
        auto boxRef = CallEtsFunction<ani_ref>("newBoxObject");

        ani_class cls;
        ASSERT_EQ(env_->FindClass("LBoxx;", &cls), ANI_OK);

        ani_field fieldInt;
        ASSERT_EQ(env_->Class_GetField(cls, "int_value", &fieldInt), ANI_OK);

        ani_field fieldString;
        ASSERT_EQ(env_->Class_GetField(cls, "string_value", &fieldString), ANI_OK);

        *boxResult = static_cast<ani_object>(boxRef);
        *fieldIntResult = fieldInt;
        *fieldStringResult = fieldString;
    }
};

// NOTE: Enable when #22354 is resolved
TEST_F(ObjectSetFieldRefTest, DISABLED_set_field_ref)
{
    ani_object box {};
    ani_field fieldInt {};
    ani_field fieldString {};
    GetTestData(&box, &fieldInt, &fieldString);

    ani_string string {};
    ASSERT_EQ(env_->String_NewUTF8("abcdef", 6U, &string), ANI_OK);

    ASSERT_EQ(env_->Object_SetField_Ref(box, fieldString, string), ANI_OK);
    ASSERT_EQ(CallEtsFunction<ani_boolean>("checkStringValue", box, string), ANI_TRUE);
}

TEST_F(ObjectSetFieldRefTest, set_field_ref_invalid_field_type)
{
    ani_object box {};
    ani_field fieldInt {};
    ani_field fieldString {};
    GetTestData(&box, &fieldInt, &fieldString);

    ani_string string {};
    ASSERT_EQ(env_->String_NewUTF8("abcdef", 6U, &string), ANI_OK);

    ASSERT_EQ(env_->Object_SetField_Ref(box, fieldInt, string), ANI_INVALID_TYPE);
}

TEST_F(ObjectSetFieldRefTest, set_field_ref_invalid_args_object)
{
    ani_object box {};
    ani_field fieldInt {};
    ani_field fieldString {};
    GetTestData(&box, &fieldInt, &fieldString);

    ani_string string {};
    ASSERT_EQ(env_->String_NewUTF8("abcdef", 6U, &string), ANI_OK);

    ASSERT_EQ(env_->Object_SetField_Ref(nullptr, fieldString, string), ANI_INVALID_ARGS);
}

TEST_F(ObjectSetFieldRefTest, set_field_ref_invalid_args_field)
{
    ani_object box {};
    ani_field fieldInt {};
    ani_field fieldString {};
    GetTestData(&box, &fieldInt, &fieldString);

    ani_string string {};
    ASSERT_EQ(env_->String_NewUTF8("abcdef", 6U, &string), ANI_OK);

    ASSERT_EQ(env_->Object_SetField_Ref(box, nullptr, string), ANI_INVALID_ARGS);
}

TEST_F(ObjectSetFieldRefTest, set_field_ref_invalid_args_value)
{
    ani_object box {};
    ani_field fieldInt {};
    ani_field fieldString {};
    GetTestData(&box, &fieldInt, &fieldString);

    ani_string string {};
    ASSERT_EQ(env_->String_NewUTF8("abcdef", 6U, &string), ANI_OK);

    ASSERT_EQ(env_->Object_SetField_Ref(box, fieldString, nullptr), ANI_INVALID_ARGS);
}

}  // namespace ark::ets::ani::testing
