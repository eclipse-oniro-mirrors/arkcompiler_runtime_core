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

#ifndef PANDA_LIBPANDABASE_PBASE_OS_UNIX_PROP_H_
#define PANDA_LIBPANDABASE_PBASE_OS_UNIX_PROP_H_

#include <string>
#include <vector>
#include <map>
#include "libpandabase/macros.h"

namespace ark::os::unix::property {

static const char ARK_DFX_PROP[] = "ark.dfx.options";     // NOLINT(modernize-avoid-c-arrays)
static const char ARK_TRACE_PROP[] = "ark.trace.enable";  // NOLINT(modernize-avoid-c-arrays)

PANDA_PUBLIC_API bool GetPropertyBuffer(const char *arkProp, std::string &out);  // NOLINT(google-runtime-references)

}  // namespace ark::os::unix::property

#endif  // PANDA_LIBPANDABASE_PBASE_OS_UNIX_PROP_H_
