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

class EtsFunctionTsToEtsTest : public EtsInteropTest {};

TEST_F(EtsFunctionTsToEtsTest, check_create_class_function_main)
{
    ASSERT_EQ(true, CallEtsMethod<bool>("check_create_class_function_main"));
}

TEST_F(EtsFunctionTsToEtsTest, check_create_class_function_parent)
{
    ASSERT_EQ(true, CallEtsMethod<bool>("check_create_class_function_parent"));
}

TEST_F(EtsFunctionTsToEtsTest, check_create_class_function_child)
{
    ASSERT_EQ(true, CallEtsMethod<bool>("check_create_class_function_child"));
}

TEST_F(EtsFunctionTsToEtsTest, check_create_class_function_anonymous)
{
    ASSERT_EQ(true, CallEtsMethod<bool>("check_create_class_function_anonymous"));
}

TEST_F(EtsFunctionTsToEtsTest, check_create_class_function_IIFE)
{
    ASSERT_EQ(true, CallEtsMethod<bool>("check_create_class_function_IIFE"));
}

TEST_F(EtsFunctionTsToEtsTest, check_create_class_arrow_function_main)
{
    ASSERT_EQ(true, CallEtsMethod<bool>("check_create_class_arrow_function_main"));
}

TEST_F(EtsFunctionTsToEtsTest, check_create_class_arrow_function_parent)
{
    ASSERT_EQ(true, CallEtsMethod<bool>("check_create_class_arrow_function_parent"));
}

TEST_F(EtsFunctionTsToEtsTest, check_create_class_arrow_function_child)
{
    ASSERT_EQ(true, CallEtsMethod<bool>("check_create_class_arrow_function_child"));
}

TEST_F(EtsFunctionTsToEtsTest, check_create_class_arrow_function_anonymous)
{
    ASSERT_EQ(true, CallEtsMethod<bool>("check_create_class_arrow_function_anonymous"));
}

TEST_F(EtsFunctionTsToEtsTest, check_create_class_arrow_function_IIFE)
{
    ASSERT_EQ(true, CallEtsMethod<bool>("check_create_class_arrow_function_IIFE"));
}

}  // namespace ark::ets::interop::js::testing