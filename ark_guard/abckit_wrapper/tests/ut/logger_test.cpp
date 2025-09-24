/**
 * Copyright (c) 2025 Huawei Device Co., Ltd.
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
#include "../../library/logger.h"

using namespace testing::ext;

/**
 * @tc.name: logger_001
 * @tc.desc: test logger print
 * @tc.type: FUNC
 * @tc.require:
 */
HWTEST(abckit_wrapper, logger_001, TestSize.Level4)
{
    LOG_D << "debug";
    LOG_I << "info";
    LOG_W << "warn";
    LOG_E << "error";
}