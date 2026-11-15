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

#include "core/reader/annotation_analyzer.h"

namespace panda::bytecode_explorer {

bool AnnotationAnalyzer::IsCompilerOwned(const PhysicalAnnotation &annotation)
{
    return annotation.name.compare(0, 3U, "_ES") == 0;
}

std::string AnnotationAnalyzer::Describe(const PhysicalAnnotation &annotation)
{
    std::string description = annotation.name;
    if (annotation.elements.empty()) {
        return description;
    }
    description += '(';
    for (size_t index = 0; index < annotation.elements.size(); ++index) {
        if (index != 0U) {
            description += ", ";
        }
        const auto &element = annotation.elements[index];
        description += element.name + '=' + element.displayValue;
        if (!element.decoded) {
            description += " [tag ";
            description.push_back(element.tag);
            description += ']';
        }
    }
    description += ')';
    return description;
}

}  // namespace panda::bytecode_explorer
