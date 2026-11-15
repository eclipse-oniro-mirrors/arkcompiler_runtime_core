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

#ifndef BYTECODE_EXPLORER_APP_DOCUMENT_LOADER_H
#define BYTECODE_EXPLORER_APP_DOCUMENT_LOADER_H

#include "core/common/result.h"
#include "core/common/cancellation.h"
#include "core/document/abc_document.h"
#include "core/index/file_range_locator.h"
#include "core/index/structure_index.h"
#include "core/metadata/metadata_catalog.h"
#include "core/method_index.h"
#include "core/statistics/scope_statistics.h"

#include <functional>
#include <memory>
#include <string>

namespace panda::bytecode_explorer {

struct LoadedDocument {
    std::shared_ptr<const AbcDocument> document;
    std::shared_ptr<const MethodIndex> methods;
    std::shared_ptr<const StructureIndex> structure;
    std::shared_ptr<const FileRangeLocator> locations;
    std::shared_ptr<const MetadataCatalog> metadata;
    DocumentSummary summary;
    std::shared_ptr<const StatisticsInventory> statistics;
};

using LoadedDocumentResult = Result<LoadedDocument>;
using DocumentLoader = std::function<LoadedDocumentResult(const std::string &)>;

enum class DocumentLoadStage { OPENING, METHODS, PHYSICAL, STRUCTURE, LOCATIONS, METADATA };

struct DocumentLoadOptions {
    CancellationToken cancellation;
    std::function<void(DocumentLoadStage)> onStage;
};

LoadedDocumentResult LoadExplorerDocument(const std::string &path);
LoadedDocumentResult LoadExplorerDocument(const std::string &path, const DocumentLoadOptions &options);

}  // namespace panda::bytecode_explorer

#endif  // BYTECODE_EXPLORER_APP_DOCUMENT_LOADER_H
