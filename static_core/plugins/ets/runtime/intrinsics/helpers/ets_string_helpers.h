/*
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

#ifndef PANDA_PLUGINS_ETS_RUNTIME_ETS_STRING_HELPERS
#define PANDA_PLUGINS_ETS_RUNTIME_ETS_STRING_HELPERS

#include "plugins/ets/runtime/types/ets_string.h"

namespace ark::ets {
class EtsEscompatArray;
}  // namespace ark::ets

namespace ark::ets::intrinsics::helpers {

EtsString *CreateNewStringFromCharCode(EtsEscompatArray *charCodes);
EtsString *CreateNewStringFromCharCode(EtsObjectArray *charCodes, size_t actualLength);

}  // namespace ark::ets::intrinsics::helpers
#endif  // PANDA_PLUGINS_ETS_RUNTIME_ETS_STRING_HELPERS