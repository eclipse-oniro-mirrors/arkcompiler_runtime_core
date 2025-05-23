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

class EtsInteropScenariosEtsToJsIndirectCallUnion : public EtsInteropTest {};

TEST_F(EtsInteropScenariosEtsToJsIndirectCallUnion, test_function_arg_type_union_number_call)
{
    ASSERT_EQ(true, RunJsTestSuite("js_suites/type_union/test_function_arg_type_union_number_call.js"));
}

TEST_F(EtsInteropScenariosEtsToJsIndirectCallUnion, test_function_arg_type_union_number_apply)
{
    ASSERT_EQ(true, RunJsTestSuite("js_suites/type_union/test_function_arg_type_union_number_apply.js"));
}

TEST_F(EtsInteropScenariosEtsToJsIndirectCallUnion, test_function_arg_type_union_number_bind_with_arg)
{
    ASSERT_EQ(true, RunJsTestSuite("js_suites/type_union/test_function_arg_type_union_number_bind_with_arg.js"));
}

TEST_F(EtsInteropScenariosEtsToJsIndirectCallUnion, test_function_arg_type_union_number_bind_without_arg)
{
    ASSERT_EQ(true, RunJsTestSuite("js_suites/type_union/test_function_arg_type_union_number_bind_without_arg.js"));
}

TEST_F(EtsInteropScenariosEtsToJsIndirectCallUnion, test_function_arg_type_union_string_call)
{
    ASSERT_EQ(true, RunJsTestSuite("js_suites/type_union/test_function_arg_type_union_string_call.js"));
}

TEST_F(EtsInteropScenariosEtsToJsIndirectCallUnion, test_function_arg_type_union_string_apply)
{
    ASSERT_EQ(true, RunJsTestSuite("js_suites/type_union/test_function_arg_type_union_string_apply.js"));
}

TEST_F(EtsInteropScenariosEtsToJsIndirectCallUnion, test_function_arg_type_union_string_bind_with_arg)
{
    ASSERT_EQ(true, RunJsTestSuite("js_suites/type_union/test_function_arg_type_union_string_bind_with_arg.js"));
}

TEST_F(EtsInteropScenariosEtsToJsIndirectCallUnion, test_function_arg_type_union_string_bind_without_arg)
{
    ASSERT_EQ(true, RunJsTestSuite("js_suites/type_union/test_function_arg_type_union_string_bind_without_arg.js"));
}

TEST_F(EtsInteropScenariosEtsToJsIndirectCallUnion, test_function_return_type_union_number_call)
{
    ASSERT_EQ(true, RunJsTestSuite("js_suites/type_union/test_function_return_type_union_number_call.js"));
}

TEST_F(EtsInteropScenariosEtsToJsIndirectCallUnion, test_function_return_type_union_number_apply)
{
    ASSERT_EQ(true, RunJsTestSuite("js_suites/type_union/test_function_return_type_union_number_apply.js"));
}

TEST_F(EtsInteropScenariosEtsToJsIndirectCallUnion, test_function_return_type_union_number_bind_with_arg)
{
    ASSERT_EQ(true, RunJsTestSuite("js_suites/type_union/test_function_return_type_union_number_bind_with_arg.js"));
}

TEST_F(EtsInteropScenariosEtsToJsIndirectCallUnion, test_function_return_type_union_number_bind_without_arg)
{
    ASSERT_EQ(true, RunJsTestSuite("js_suites/type_union/test_function_return_type_union_number_bind_without_arg.js"));
}

TEST_F(EtsInteropScenariosEtsToJsIndirectCallUnion, test_function_return_type_union_string_call)
{
    ASSERT_EQ(true, RunJsTestSuite("js_suites/type_union/test_function_return_type_union_string_call.js"));
}

TEST_F(EtsInteropScenariosEtsToJsIndirectCallUnion, test_function_return_type_union_string_apply)
{
    ASSERT_EQ(true, RunJsTestSuite("js_suites/type_union/test_function_return_type_union_string_apply.js"));
}

}  // namespace ark::ets::interop::js::testing
