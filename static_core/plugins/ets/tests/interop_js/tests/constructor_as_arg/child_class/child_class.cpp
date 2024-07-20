/**
 * Copyright (c) 2024 Huawei Device Co., Ltd.
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

#include <gtest/gtest.h>
#include "ets_interop_js_gtest.h"

namespace ark::ets::interop::js::testing {

class EtsChildClassTsToEtsTest : public EtsInteropTest {};

TEST_F(EtsChildClassTsToEtsTest, check_inheritor_class_main)
{
    ASSERT_EQ(true, CallEtsMethod<bool>("check_inheritor_class_main"));
}

TEST_F(EtsChildClassTsToEtsTest, check_inheritor_class_anonymous)
{
    ASSERT_EQ(true, CallEtsMethod<bool>("check_inheritor_class_anonymous"));
}

TEST_F(EtsChildClassTsToEtsTest, check_inheritor_class_IIFE)
{
    ASSERT_EQ(true, CallEtsMethod<bool>("check_inheritor_class_IIFE"));
}

TEST_F(EtsChildClassTsToEtsTest, check_create_child_class_with_arg_main_from_ts)
{
    ASSERT_EQ(true, CallEtsMethod<bool>("check_create_child_class_with_arg_main_from_ts"));
}

TEST_F(EtsChildClassTsToEtsTest, check_create_child_class_with_arg_anonymous_from_ts)
{
    ASSERT_EQ(true, CallEtsMethod<bool>("check_create_child_class_with_arg_anonymous_from_ts"));
}

TEST_F(EtsChildClassTsToEtsTest, check_create_child_class_with_arg_IIFE_from_ts)
{
    ASSERT_EQ(true, CallEtsMethod<bool>("check_create_child_class_with_arg_IIFE_from_ts"));
}

TEST_F(EtsChildClassTsToEtsTest, check_create_child_class_with_main_from_ts)
{
    ASSERT_EQ(true, CallEtsMethod<bool>("check_create_child_class_with_main_from_ts"));
}

TEST_F(EtsChildClassTsToEtsTest, check_child_class_with_main_instance)
{
    ASSERT_EQ(true, CallEtsMethod<bool>("check_child_class_with_main_instance"));
}

TEST_F(EtsChildClassTsToEtsTest, check_create_child_class_with_anonymous_from_ts)
{
    ASSERT_EQ(true, CallEtsMethod<bool>("check_create_child_class_with_anonymous_from_ts"));
}

TEST_F(EtsChildClassTsToEtsTest, check_child_class_with_anonymous_instance)
{
    ASSERT_EQ(true, CallEtsMethod<bool>("check_child_class_with_anonymous_instance"));
}

TEST_F(EtsChildClassTsToEtsTest, check_create_child_class_with_IIFE_from_ts)
{
    ASSERT_EQ(true, CallEtsMethod<bool>("check_create_child_class_with_IIFE_from_ts"));
}

TEST_F(EtsChildClassTsToEtsTest, check_child_class_with_IIFE_instance)
{
    ASSERT_EQ(true, CallEtsMethod<bool>("check_child_class_with_IIFE_instance"));
}

}  // namespace ark::ets::interop::js::testing