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

/*
 * Cross-module hole-check elimination, done at pandasm-ins level in
 * EmitPrograms (no bytecode optimizer involved): an external module variable
 * (ldexternalmodulevar) is hole only inside a cyclic import chain. With ALL
 * programs of the merged output at hand, build the import graph from each
 * module record's module-request/regular-import literal, detect cycles, and
 * remove the redundant `ldexternalmodulevar N; throw.undefinedifholewithname`
 * pairs whose import is provably acyclic. Conservative on any unknown
 * (missing record/literal, unrecognized layout) — the throw is kept — EXCEPT
 * SDK records (record names starting with @ohos: / @system: / @hms), which
 * are pre-compiled system modules that never import back into project source,
 * so a chain that reaches an absent SDK record is treated as acyclic.
 * Additionally, a module that re-exports an imported binding whose module
 * request is lazy (recorded in the module-request phase literal buffer as
 * INTEGER_8 = 1) is treated as cyclic: the re-export does not trigger the
 * lazy load, so the binding may still be hole. Re-exports are collected from
 * both the local-export form (`import lazy { x } from 'mod'; export { x };`)
 * and the indirect-export form — including the implicit indirect form the
 * frontend generates from the lazy pattern above, which reuses the lazy
 * import's request index (CheckImplicitIndirectExport /
 * ConvertLocalExportToIndirect).
 *
 * Concurrency contract: import-graph collection and the one-shot cycle
 * analysis are global and always run serially on the caller thread. The
 * per-function elimination reads only those frozen results and mutates
 * exactly one Function per task, so with `threadCount > 1` that stage runs on
 * `panda::WorkerQueue` worker threads with no shared mutable state — the
 * produced bytecode is identical to the serial path (`threadCount <= 1`,
 * also the default) and independent of the scheduling order.
 */
PANDA_PUBLIC_API void EliminateCrossModuleHoleChecks(const std::vector<Program *> &progs, size_t threadCount = 1);

}  // namespace panda::pandasm

#endif  // ASSEMBLER_CROSS_MODULE_HOLE_CHECK_H
