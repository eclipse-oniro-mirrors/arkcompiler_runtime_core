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

#ifndef BYTECODE_EXPLORER_CORE_READER_CALLABLE_ANALYZER_H
#define BYTECODE_EXPLORER_CORE_READER_CALLABLE_ANALYZER_H

#include "core/model/physical_program.h"
#include "core/reader/dynamic_name_parser.h"

#include <string>
#include <vector>

namespace panda::bytecode_explorer {

struct AnalyzedMethod {
    const PhysicalMethod *physical {nullptr};
    ParsedDynamicName parsed;
    std::optional<uint32_t> classLiteralArrayOffset;
    bool isStatic {false};
};

struct UnclassifiedMethod {
    const PhysicalMethod *physical {nullptr};
    std::string diagnostic;
};

struct CallableAnalysis {
    std::vector<AnalyzedMethod> methods;
    std::vector<UnclassifiedMethod> unclassified;
};

class CallableAnalyzer final {
public:
    static CallableAnalysis Analyze(const PhysicalRecord &record);
};

}  // namespace panda::bytecode_explorer

#endif  // BYTECODE_EXPLORER_CORE_READER_CALLABLE_ANALYZER_H
