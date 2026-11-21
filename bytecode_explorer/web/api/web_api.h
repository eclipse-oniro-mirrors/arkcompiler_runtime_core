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

#ifndef BYTECODE_EXPLORER_WEB_API_WEB_API_H
#define BYTECODE_EXPLORER_WEB_API_WEB_API_H

#include "app/document_loader.h"
#include "core/common/cancellation.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace panda::bytecode_explorer {

struct WebCfgCache;
struct WebStructureCache;
class MetadataIndexService;
namespace test {
class WebApiTestAccess;
}  // namespace test

class WebApi final {
public:
    explicit WebApi(LoadedDocument document);

    Result<std::string> DocumentJson() const;
    Result<std::string> StructureJson(const CancellationToken &cancellation = {}) const;
    Result<std::string> StatisticsJson(EntityId entityId) const;
    Result<std::string> StartStatisticsJson(EntityId entityId, std::string_view requestBody) const;
    Result<std::string> CancelStatisticsJson(EntityId entityId, std::string_view requestBody) const;
    Result<std::string> HexJson(uint64_t offset, uint64_t size) const;
    Result<std::string> LocationJson(uint64_t offset) const;
    Result<std::string> DisassemblyJson(EntityId entityId, std::optional<uint64_t> methodOffset = std::nullopt) const;
    Result<std::string> CfgJson(EntityId entityId, std::optional<uint64_t> methodOffset = std::nullopt) const;
    Result<std::string> CfgLayoutJson(EntityId entityId, std::string_view requestBody,
                                      std::optional<uint64_t> methodOffset = std::nullopt) const;
    Result<std::string> MetadataStatusJson() const;
    Result<std::string> StartMetadataIndexJson(std::string_view requestBody) const;
    Result<std::string> MetadataObjectsJson(const MetadataObjectQuery &query) const;
    Result<std::string> MetadataObjectJson(MetadataObjectKey key) const;
    Result<std::string> MetadataReferencesJson(MetadataObjectKey key, std::optional<std::string> cursor,
                                               size_t limit) const;
    size_t CfgAnalysisBuildCountForTesting() const;
    void WaitForMetadataIndexForTesting() const;

private:
    friend class test::WebApiTestAccess;

    static std::shared_ptr<ScopeStatistics> CreateStatistics(const LoadedDocument &document);
    Result<std::string> SerializeStatistics(const Result<StatisticsSnapshot> &result) const;

    static std::string SerializeMetadataObjectsPage(const std::string &documentId,
                                                    const MetadataPage<MetadataObject> &page);
    static std::string SerializeMetadataObjectDetail(const std::string &documentId, const MetadataObjectDetail &detail);
    static std::string SerializeMetadataReferencesPage(const std::string &documentId, MetadataObjectKey key,
                                                       const MetadataPage<MetadataReference> &page);

    std::string documentId_;
    LoadedDocument document_;
    std::shared_ptr<MetadataIndexService> metadataIndex_;
    std::shared_ptr<WebCfgCache> cfgCache_;
    std::shared_ptr<WebStructureCache> structureCache_;
    std::shared_ptr<ScopeStatistics> statistics_;
};

}  // namespace panda::bytecode_explorer

#endif  // BYTECODE_EXPLORER_WEB_API_WEB_API_H
