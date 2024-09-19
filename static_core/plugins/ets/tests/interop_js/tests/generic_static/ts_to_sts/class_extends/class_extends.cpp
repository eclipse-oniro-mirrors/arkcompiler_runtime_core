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

class EtsClassExtendsTsToEtsTest : public EtsInteropTest {};

TEST_F(EtsClassExtendsTsToEtsTest, checkClassExtendsInt)
{
    ASSERT_EQ(true, CallEtsMethod<bool>("checkClassExtendsInt"));
}

TEST_F(EtsClassExtendsTsToEtsTest, checkClassExtendsString)
{
    ASSERT_EQ(true, CallEtsMethod<bool>("checkClassExtendsString"));
}

TEST_F(EtsClassExtendsTsToEtsTest, checkClassExtendsBool)
{
    ASSERT_EQ(true, CallEtsMethod<bool>("checkClassExtendsBool"));
}

TEST_F(EtsClassExtendsTsToEtsTest, checkClassExtendsArr)
{
    ASSERT_EQ(true, CallEtsMethod<bool>("checkClassExtendsArr"));
}

TEST_F(EtsClassExtendsTsToEtsTest, checkClassExtendsMethodCallFromTsInt)
{
    ASSERT_EQ(true, CallEtsMethod<bool>("checkClassExtendsMethodCallFromTsInt"));
}

TEST_F(EtsClassExtendsTsToEtsTest, checkClassExtendsMethodCallFromTsString)
{
    ASSERT_EQ(true, CallEtsMethod<bool>("checkClassExtendsMethodCallFromTsString"));
}

TEST_F(EtsClassExtendsTsToEtsTest, checkClassExtendsMethodCallFromTsBool)
{
    ASSERT_EQ(true, CallEtsMethod<bool>("checkClassExtendsMethodCallFromTsBool"));
}

TEST_F(EtsClassExtendsTsToEtsTest, checkClassExtendsMethodCallFromTsArr)
{
    ASSERT_EQ(true, CallEtsMethod<bool>("checkClassExtendsMethodCallFromTsArr"));
}

}  // namespace ark::ets::interop::js::testing