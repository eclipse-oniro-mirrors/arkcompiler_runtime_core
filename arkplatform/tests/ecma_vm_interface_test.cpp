/**
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "hybrid/ecma_vm_interface.h"

#include <gtest/gtest.h>
#include <memory>

namespace arkplatform::test {

class EcmaVMInterfaceTest : public testing::Test {
protected:
    class DefaultEcmaVMInterface : public EcmaVMInterface {
    public:
        std::unique_ptr<common::dump::AbstractDumper> CreateHeapDumper(
            [[maybe_unused]] const common::dump::DumpRequest &request) override
        {
            return nullptr;
        }
    };
};

TEST_F(EcmaVMInterfaceTest, DefaultGetArkTSModeTest)
{
    DefaultEcmaVMInterface iface;
    EXPECT_EQ(iface.GetArkTSMode(), "dynamic");
}
}  // namespace arkplatform::test
