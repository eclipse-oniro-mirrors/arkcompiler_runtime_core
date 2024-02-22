/**
 * Copyright (c) 2024-2025 Huawei Device Co., Ltd.
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

#ifndef ABC2PROGRAM_ABC2PROGRAM_COMPILER_H
#define ABC2PROGRAM_ABC2PROGRAM_COMPILER_H

#include <string>
#include <assembly-program.h>
#include "abc_string_table.h"
#include "abc2program_key_data.h"

namespace panda::abc2program {

class Abc2ProgramCompiler {
public:
    bool OpenAbcFile(const std::string &file_path);
    const panda_file::File &GetAbcFile() const;
    AbcStringTable &GetAbcStringTable() const;
    bool FillProgramData(pandasm::Program &program);
private:
    std::unique_ptr<const panda_file::File> file_;
    std::unique_ptr<AbcStringTable> string_table_;
    std::unique_ptr<Abc2ProgramKeyData> key_data_;
}; // class Abc2ProgramCompiler

} // namespace panda::abc2program

#endif // ABC2PROGRAM_ABC2PROGRAM_COMPILER_H
