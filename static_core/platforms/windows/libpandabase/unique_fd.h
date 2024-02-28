/**
 * Copyright (c) 2022 Huawei Device Co., Ltd.
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

#ifndef PANDA_LIBPANDABASE_OS_WINDOWS_UNIQUE_FD_H
#define PANDA_LIBPANDABASE_OS_WINDOWS_UNIQUE_FD_H

#include "libpandabase/macros.h"

namespace panda::os::unique_fd {

inline int DupCloexec([[maybe_unused]] int fd)
{
    // Unsupported on windows platform
    UNREACHABLE();
}

}  // namespace panda::os::unique_fd

#endif  // PANDA_LIBPANDABASE_OS_WINDOWS_UNIQUE_FD_H_
