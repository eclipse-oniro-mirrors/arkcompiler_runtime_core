/**
 * Copyright (c) 2023-2024 Huawei Device Co., Ltd.
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

class EtsInteropInstanceOf : public EtsInteropTest {};

/*
 * =============================================
 * === Tests <object type>_instanceof_object ===
 * =============================================
 */
TEST_F(EtsInteropInstanceOf, Test_object_instanceof_object)
{
    ASSERT_EQ(true, CallEtsMethod<bool>("Test_object_instanceof_object"));
}

TEST_F(EtsInteropInstanceOf, Test_etstype_instanceof_object)
{
    ASSERT_EQ(true, CallEtsMethod<bool>("Test_etstype_instanceof_object"));
}

TEST_F(EtsInteropInstanceOf, Test_jsvalue_instanceof_object)
{
    ASSERT_EQ(true, CallEtsMethod<bool>("Test_jsvalue_instanceof_object"));
}

TEST_F(EtsInteropInstanceOf, Test_dynvalue_instanceof_object)
{
    ASSERT_EQ(true, CallEtsMethod<bool>("Test_dynvalue_instanceof_object"));
}

TEST_F(EtsInteropInstanceOf, Test_dyndecl_instanceof_object)
{
    ASSERT_EQ(true, CallEtsMethod<bool>("Test_dyndecl_instanceof_object"));
}

/*
 * ==============================================
 * === Tests <object type>_instanceof_etstype ===
 * ==============================================
 */
TEST_F(EtsInteropInstanceOf, Test_object_instanceof_etstype)
{
    ASSERT_EQ(true, CallEtsMethod<bool>("Test_object_instanceof_etstype"));
}

TEST_F(EtsInteropInstanceOf, Test_etstype_instanceof_etstype)
{
    ASSERT_EQ(true, CallEtsMethod<bool>("Test_etstype_instanceof_etstype"));
}

TEST_F(EtsInteropInstanceOf, Test_jsvalue_instanceof_etstype)
{
    ASSERT_EQ(true, CallEtsMethod<bool>("Test_jsvalue_instanceof_etstype"));
}

TEST_F(EtsInteropInstanceOf, Test_dynvalue_instanceof_etstype)
{
    ASSERT_EQ(true, CallEtsMethod<bool>("Test_dynvalue_instanceof_etstype"));
}

TEST_F(EtsInteropInstanceOf, Test_dyndecl_instanceof_etstype)
{
    ASSERT_EQ(true, CallEtsMethod<bool>("Test_dyndecl_instanceof_etstype"));
}

/*
 * ==============================================
 * === Tests <object type>_instanceof_dyndecl ===
 * ==============================================
 */
TEST_F(EtsInteropInstanceOf, Test_object_instanceof_dyndecl)
{
    ASSERT_EQ(true, CallEtsMethod<bool>("Test_object_instanceof_dyndecl"));
}

TEST_F(EtsInteropInstanceOf, Test_etstype_instanceof_dyndecl)
{
    ASSERT_EQ(true, CallEtsMethod<bool>("Test_etstype_instanceof_dyndecl"));
}

TEST_F(EtsInteropInstanceOf, Test_jsvalue_instanceof_dyndecl)
{
    ASSERT_EQ(true, CallEtsMethod<bool>("Test_jsvalue_instanceof_dyndecl"));
}

TEST_F(EtsInteropInstanceOf, Test_dynvalue_instanceof_dyndecl)
{
    ASSERT_EQ(true, CallEtsMethod<bool>("Test_dynvalue_instanceof_dyndecl"));
}

TEST_F(EtsInteropInstanceOf, Test_dyndecl_instanceof_dyndecl)
{
    ASSERT_EQ(true, CallEtsMethod<bool>("Test_dyndecl_instanceof_dyndecl"));
}

}  // namespace ark::ets::interop::js::testing