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

#include "app/document_loader.h"

#include "core/metadata/metadata_catalog.h"
#include "core/reader/physical_reader.h"
#include "core/reader/structure_classifier.h"

#include <utility>

namespace panda::bytecode_explorer {

LoadedDocumentResult LoadExplorerDocument(const std::string &path)
{
    return LoadExplorerDocument(path, {});
}

namespace {

// Runs the stage callback when cancellation has not been requested; returns false when
// the load must stop because cancellation was observed before or during the stage.
bool BeginLoadStage(const DocumentLoadOptions &options, DocumentLoadStage stage)
{
    if (options.cancellation.IsCancellationRequested()) {
        return false;
    }
    if (options.onStage) {
        options.onStage(stage);
    }
    return !options.cancellation.IsCancellationRequested();
}

LoadedDocumentResult LoadCancelled()
{
    return LoadedDocumentResult::Failure(Status(ErrorCode::CANCELLED, "ABC loading was cancelled"));
}

// Loads every derived model stage for an already opened document: METHOD index, physical
// program, semantic structure, statistics inventory, file ranges and metadata catalog.
// Returns the completed document or the first failing stage result.
LoadedDocumentResult BuildDocumentModels(const std::string &path, const DocumentLoadOptions &options,
                                         LoadedDocument loaded)
{
    if (!BeginLoadStage(options, DocumentLoadStage::METHODS)) {
        return LoadCancelled();
    }
    auto methods = MethodIndex::Build(loaded.document->File());
    if (methods.status != MethodIndexStatus::SUCCESS || methods.index == nullptr) {
        return LoadedDocumentResult::Failure(
            Status(ErrorCode::INVALID_FORMAT,
                   methods.diagnostic.empty() ? "Cannot build validated METHOD index: " + path : methods.diagnostic));
    }
    loaded.methods = std::move(methods.index);
    if (!BeginLoadStage(options, DocumentLoadStage::PHYSICAL)) {
        return LoadCancelled();
    }
    auto physical = PhysicalReader::Read(loaded.document->File());
    if (!physical.HasValue()) {
        return LoadedDocumentResult::Failure(physical.Error());
    }
    if (!BeginLoadStage(options, DocumentLoadStage::STRUCTURE)) {
        return LoadCancelled();
    }
    auto structure = StructureClassifier::Build(path, physical.Value());
    if (!structure.HasValue()) {
        return LoadedDocumentResult::Failure(structure.Error());
    }
    loaded.structure = std::move(structure.Value());
    loaded.statistics =
        StatisticsInventory::Build(physical.Value(), *loaded.structure, loaded.methods->MethodsInOffsetOrder());
    if (!BeginLoadStage(options, DocumentLoadStage::LOCATIONS)) {
        return LoadCancelled();
    }
    loaded.locations = FileRangeLocator::Build(*loaded.structure, loaded.summary.fileSize);
    if (!BeginLoadStage(options, DocumentLoadStage::METADATA)) {
        return LoadCancelled();
    }
    auto metadata = MetadataCatalog::Build(loaded.document->File(), physical.Value(), *loaded.structure);
    if (!metadata.HasValue()) {
        return LoadedDocumentResult::Failure(metadata.Error());
    }
    loaded.metadata = std::move(metadata.Value());
    if (options.cancellation.IsCancellationRequested()) {
        return LoadCancelled();
    }
    return LoadedDocumentResult::Success(std::move(loaded));
}

}  // namespace

LoadedDocumentResult LoadExplorerDocument(const std::string &path, const DocumentLoadOptions &options)
{
    if (!BeginLoadStage(options, DocumentLoadStage::OPENING)) {
        return LoadCancelled();
    }
    auto result = AbcDocument::Open(path);
    if (!result.HasValue()) {
        return LoadedDocumentResult::Failure(result.Error());
    }
    LoadedDocument loaded;
    loaded.document = result.Value();
    loaded.summary = loaded.document->Summary();
    return BuildDocumentModels(path, options, std::move(loaded));
}

}  // namespace panda::bytecode_explorer
