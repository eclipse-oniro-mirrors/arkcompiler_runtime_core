/**
 * Copyright (c) 2026 Huawei Device Co., Ltd.
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

#ifndef ASSEMBLER_ASSEMBLY_STRING_POOL_H
#define ASSEMBLER_ASSEMBLY_STRING_POOL_H

#include <string>
#include <string_view>

#include "libpandabase/panda_visibility.h"

namespace panda::pandasm {

// Process-wide string intern pool shared by pandasm structures that hold many
// repeated strings (Type names, debug local-variable names, Ins ids). Returns a
// stable const std::string* so identical strings share storage.
// Thread safety: the returned pointer is safe to read from any thread once
// obtained — inserted pool nodes are never mutated or erased, and the bucket
// locks only guard the insertion path. The thread-local lookup cache only
// re-validates against its own thread's lookups.
// Lifetime: the pool lives until process exit and is bounded by the distinct
// string vocabulary, not by the number of referencing objects. Referencing
// objects only store the pointer and never free it, so there is no teardown
// ordering hazard. Long-lived hosts (e.g. libabckit) that process many
// unrelated files should expect the pool to retain every distinct name seen.
PANDA_PUBLIC_API const std::string *InternString(std::string_view s);

}  // namespace panda::pandasm

#endif  // ASSEMBLER_ASSEMBLY_STRING_POOL_H
