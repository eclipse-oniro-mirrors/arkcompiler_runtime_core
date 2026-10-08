/**
 * Copyright (c) 2021-2026 Huawei Device Co., Ltd.
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

#ifndef ASSEMBLER_ASSEMBLY_PROGRAM_H
#define ASSEMBLER_ASSEMBLY_PROGRAM_H

#include <map>
#include <set>
#include <string>
#include <unordered_map>

#include "assembly-function.h"
#include "assembly-record.h"
#include "assembly-type.h"
#include "assembly-methodhandle.h"
#include "assembly-literals.h"
#include "extensions/extensions.h"
#include "macros.h"

namespace panda::pandasm {

struct Program {
    panda::panda_file::SourceLang lang {panda::panda_file::SourceLang::PANDA_ASSEMBLY};
    // All name-keyed tables stay ordered: dump outputs (--dump-assembly, abc2program, ark_disasm
    // goldens) iterate them directly and are compared line-by-line, so the iteration order must be
    // deterministic across machines (unordered containers hash-order differently per stdlib).
    std::map<std::string, panda::pandasm::Record> record_table;
    std::map<std::string, panda::pandasm::Function> function_table;
    // Retains the only record data needed while EmitPrograms emits functions after releasing record_table.
    std::unordered_map<std::string, std::string> record_source_files;
    std::unordered_map<std::string, std::vector<std::string>> function_synonyms;
    std::map<std::string, panda::pandasm::LiteralArray> literalarray_table;
    std::set<std::string> strings;
    std::unordered_set<Type> array_types;
    bool isGeneratedFromMergedAbc {false};

    /*
     * Returns a JSON string with the program structure and scopes locations
     */
    std::string JsonDump() const;
};

}  // namespace panda::pandasm

#endif  // ASSEMBLER_ASSEMBLY_PROGRAM_H
