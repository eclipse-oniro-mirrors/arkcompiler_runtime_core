/**
 * Copyright (c) 2023-2025 Huawei Device Co., Ltd.
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

#ifndef PANDA_RUNTIME_COROUTINES_STACKFUL_COMMON_H
#define PANDA_RUNTIME_COROUTINES_STACKFUL_COMMON_H

namespace ark::stackful_coroutines {

// common constants and globals will reside here...

// taskpool eaworker limit
inline constexpr size_t TASKPOOL_EAWORKER_LIMIT = 2;
// taskpool eaworker mode
inline static constexpr const char *TASKPOOL_EAWORKER_MODE = "eaworker";
}  // namespace ark::stackful_coroutines

#endif /* PANDA_RUNTIME_COROUTINES_STACKFUL_COMMON_H */
