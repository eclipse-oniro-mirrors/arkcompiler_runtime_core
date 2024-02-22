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

#ifndef ABC2PROGRAM_ABC_FILE_UTILS_H
#define ABC2PROGRAM_ABC_FILE_UTILS_H

#include <string>

namespace panda::abc2program {

// attribute constant
constexpr std::string_view ABC_ATTR_EXTERNAL = "external";
constexpr std::string_view ABC_ATTR_STATIC = "static";

// dump constant
constexpr std::string_view DUMP_TITLE_SOURCE_BINARY = "# source binary: ";
constexpr std::string_view DUMP_TITLE_LANGUAGE = ".language ";
constexpr std::string_view DUMP_TITLE_LITERALS = "# LITERALS";
constexpr std::string_view DUMP_TITLE_RECORDS = "# RECORDS";
constexpr std::string_view DUMP_TITLE_RECORD = ".record ";
constexpr std::string_view DUMP_TITLE_RECORD_SOURCE_FILE = ".record.source_file ";
constexpr std::string_view DUMP_TITLE_METHODS = "# METHODS";
constexpr std::string_view DUMP_TITLE_FUNCTION = ".function ";
constexpr std::string_view DUMP_TITLE_STRING = "# STRING\n\n";
constexpr std::string_view DUMP_TITLE_SEPARATOR = "# ====================\n";
constexpr std::string_view DUMP_CONTENT_ECMA_SCRIPT = "ECMAScript";
constexpr std::string_view DUMP_CONTENT_PANDA_ASSEMBLY = "PandaAssembly";
constexpr std::string_view DUMP_CONTENT_SINGLE_ENDL = "\n";
constexpr std::string_view DUMP_CONTENT_DOUBLE_ENDL = "\n\n";
constexpr std::string_view DUMP_CONTENT_ATTR_EXTERNAL = "<external>";
constexpr std::string_view DUMP_CONTENT_ATTR_STATIC = "<static>";

class AbcFileUtils {
public:
    static bool IsSystemTypeName(const std::string &type_name);
    static std::string GetFileNameByAbsolutePath(const std::string &absolute_path);
};  // class AbcFileUtils

}  // namespace panda::abc2program

#endif  // ABC2PROGRAM_ABC_FILE_UTILS_H
