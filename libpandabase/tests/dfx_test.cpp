/*
 * Copyright (c) 2021-2022 Huawei Device Co., Ltd.
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

#include "os/thread.h"
#include "utils/dfx.h"
#include "utils/logger.h"
#include "utils/string_helpers.h"

#include <gtest/gtest.h>

namespace panda::test {

void MapDfxOption(std::map<DfxOptionHandler::DfxOption, uint8_t> &option_map, DfxOptionHandler::DfxOption option)
{
    switch (option) {
#ifdef PANDA_TARGET_UNIX
        case DfxOptionHandler::COMPILER_NULLCHECK:
            option_map[DfxOptionHandler::COMPILER_NULLCHECK] = 1;
            break;
        case DfxOptionHandler::SIGNAL_CATCHER:
            option_map[DfxOptionHandler::SIGNAL_CATCHER] = 1;
            break;
        case DfxOptionHandler::SIGNAL_HANDLER:
            option_map[DfxOptionHandler::SIGNAL_HANDLER] = 1;
            break;
        case DfxOptionHandler::ARK_SIGQUIT:
            option_map[DfxOptionHandler::ARK_SIGQUIT] = 1;
            break;
        case DfxOptionHandler::ARK_SIGUSR1:
            option_map[DfxOptionHandler::ARK_SIGUSR1] = 1;
            break;
        case DfxOptionHandler::ARK_SIGUSR2:
            option_map[DfxOptionHandler::ARK_SIGUSR2] = 1;
            break;
        case DfxOptionHandler::MOBILE_LOG:
            option_map[DfxOptionHandler::MOBILE_LOG] = 1;
            break;
#endif  // PANDA_TARGET_UNIX
        case DfxOptionHandler::REFERENCE_DUMP:
            option_map[DfxOptionHandler::REFERENCE_DUMP] = 1;
            break;
        case DfxOptionHandler::DFXLOG:
            option_map[DfxOptionHandler::DFXLOG] = 0;
            break;
        default:
            break;
    }
}

HWTEST(DfxController, Initialization, testing::ext::TestSize.Level0)
{
    if (DfxController::IsInitialized()) {
        DfxController::Destroy();
    }
    EXPECT_FALSE(DfxController::IsInitialized());

    DfxController::Initialize();
    EXPECT_TRUE(DfxController::IsInitialized());

    DfxController::Destroy();
    EXPECT_FALSE(DfxController::IsInitialized());

    std::map<DfxOptionHandler::DfxOption, uint8_t> option_map;
    for (auto option = DfxOptionHandler::DfxOption(0); option < DfxOptionHandler::END_FLAG;
        option = DfxOptionHandler::DfxOption(option + 1)) {
            MapDfxOption(option_map, option);
    }

    DfxController::Initialize(option_map);
    EXPECT_TRUE(DfxController::IsInitialized());

    DfxController::Destroy();
    EXPECT_FALSE(DfxController::IsInitialized());
}

HWTEST(DfxController, TestResetOptionValueFromString, testing::ext::TestSize.Level0)
{
    if (DfxController::IsInitialized()) {
        DfxController::Destroy();
    }
    EXPECT_FALSE(DfxController::IsInitialized());

    DfxController::Initialize();
    EXPECT_TRUE(DfxController::IsInitialized());

    DfxController::ResetOptionValueFromString("dfx-log:1");
    EXPECT_EQ(DfxController::GetOptionValue(DfxOptionHandler::DFXLOG), 1);

    DfxController::Destroy();
    EXPECT_FALSE(DfxController::IsInitialized());
}

}  // namespace panda::test
