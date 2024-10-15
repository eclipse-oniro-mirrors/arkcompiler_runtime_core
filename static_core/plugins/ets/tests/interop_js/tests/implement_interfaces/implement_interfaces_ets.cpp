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

class EtsImplementInterfacesEts : public EtsInteropTest {};
// NOTE(andreypetukhov) enable after fixibng #12808
TEST_F(EtsImplementInterfacesEts, DISABLED_arktsClassGetProperty)
{
    auto ret = CallEtsMethod<bool>("arktsClassGetProperty");
    ASSERT_EQ(ret, true);
}
// NOTE(andreypetukhov) enable after fixibng #12808
TEST_F(EtsImplementInterfacesEts, DISABLED_arktsClassUseStringFn)
{
    auto ret = CallEtsMethod<bool>("arktsClassUseStringFn");
    ASSERT_EQ(ret, true);
}
// NOTE(andreypetukhov) enable after fixibng #12808
TEST_F(EtsImplementInterfacesEts, DISABLED_arktsClassUseNumberFn)
{
    auto ret = CallEtsMethod<bool>("arktsClassUseNumberFn");
    ASSERT_EQ(ret, true);
}

}  // namespace ark::ets::interop::js::testing