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

#ifndef BYTECODE_EXPLORER_CORE_METADATA_METADATA_REFERENCE_INDEXER_H
#define BYTECODE_EXPLORER_CORE_METADATA_METADATA_REFERENCE_INDEXER_H

#include "core/common/cancellation.h"
#include "core/function_disassembler.h"
#include "core/metadata/metadata_catalog.h"
#include "core/method_index.h"

#include <cstdint>
#include <functional>
#include <vector>

namespace panda::bytecode_explorer {

class MetadataReferenceIndexer final {
public:
    using DecodeMethod = std::function<DisassemblyResult(uint64_t, const CancellationToken &)>;
    using PublishBatch = std::function<bool(MetadataIndexBatch)>;

    static MetadataIndexBuildResult Build(const MetadataCatalog &catalog, const std::vector<MethodMetadata> &methods,
                                          const DecodeMethod &decode, const PublishBatch &publish,
                                          const MetadataIndexLimits &limits,
                                          const CancellationToken &cancellation = {});
};

}  // namespace panda::bytecode_explorer

#endif  // BYTECODE_EXPLORER_CORE_METADATA_METADATA_REFERENCE_INDEXER_H
