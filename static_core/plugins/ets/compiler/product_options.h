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

#ifndef PANDA_PLUGINS_ETS_COMPILER_PRODUCT_OPTIONS_H
#define PANDA_PLUGINS_ETS_COMPILER_PRODUCT_OPTIONS_H

#include <array>

namespace ark::ets {

// clang-format off
static constexpr std::array AVAILABLE_COMPILER_OPTIONS_IN_PRODUCT = {
    "compiler-inline-external-methods",
    "compiler-regex",
};
// clang-format on

}  // namespace ark::ets

#endif  // PANDA_PLUGINS_ETS_COMPILER_PRODUCT_OPTIONS_H
