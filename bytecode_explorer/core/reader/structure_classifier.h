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

#ifndef BYTECODE_EXPLORER_CORE_READER_STRUCTURE_CLASSIFIER_H
#define BYTECODE_EXPLORER_CORE_READER_STRUCTURE_CLASSIFIER_H

#include "core/common/result.h"
#include "core/index/structure_index.h"
#include "core/model/physical_program.h"

#include <memory>
#include <string>

namespace panda::bytecode_explorer {

class StructureClassifier final {
public:
    using BuildResult = Result<std::shared_ptr<const StructureIndex>>;

    static BuildResult Build(const std::string &path, const PhysicalProgram &program);
};

}  // namespace panda::bytecode_explorer

#endif  // BYTECODE_EXPLORER_CORE_READER_STRUCTURE_CLASSIFIER_H
