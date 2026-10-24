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

#ifndef ASSEMBLER_CROSS_MODULE_HOLE_CHECK_H
#define ASSEMBLER_CROSS_MODULE_HOLE_CHECK_H

#include "assembly-program.h"
#include "libpandabase/panda_visibility.h"

#include <string>
#include <vector>

namespace panda::pandasm {

// Cross-module hole-check elimination, done at pandasm-ins level in
// EmitPrograms (no bytecode optimizer involved): an external module variable
// (ldexternalmodulevar) is hole only inside a cyclic import chain. With ALL
// programs of the merged output at hand, build the import graph from each
// module record's module-request/regular-import literal, detect cycles, and
// remove the redundant `ldexternalmodulevar N; throw.undefinedifholewithname`
// pairs whose import is provably acyclic. Conservative on any unknown
// (missing record/literal, unrecognized layout) — the throw is kept.
PANDA_PUBLIC_API void EliminateCrossModuleHoleChecks(const std::vector<Program *> &progs);

}  // namespace panda::pandasm

#endif  // ASSEMBLER_CROSS_MODULE_HOLE_CHECK_H
