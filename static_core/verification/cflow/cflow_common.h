/**
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

#ifndef _PANDA_VERIFICATION_CFLOW_CFLOW_COMMON_H__
#define _PANDA_VERIFICATION_CFLOW_CFLOW_COMMON_H__

#include "runtime/include/mem/panda_string.h"

namespace ark::verifier {

PandaString OffsetAsHexStr(const void *base, const void *ptr);

}  // namespace ark::verifier

#endif  // ! _PANDA_VERIFICATION_CFLOW_CFLOW_COMMON_H__
