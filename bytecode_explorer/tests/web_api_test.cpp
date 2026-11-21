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
#include "core/reader/physical_reader.h"
#include "web/api/web_api.h"

#include <gtest/gtest.h>
#include <libpandafile/file.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <future>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace panda::bytecode_explorer::test {

class WebApiTestAccess {
public:
    static std::string Objects(const MetadataPage<MetadataObject> &page)
    {
        return WebApi::SerializeMetadataObjectsPage("synthetic-document", page);
    }

    static std::string Detail(const MetadataObjectDetail &detail)
    {
        return WebApi::SerializeMetadataObjectDetail("synthetic-document", detail);
    }

    static std::string References(MetadataObjectKey key, const MetadataPage<MetadataReference> &page)
    {
        return WebApi::SerializeMetadataReferencesPage("synthetic-document", key, page);
    }
};

namespace {

TEST(WebStatisticsApiTest, StartsOnlyOnRequestAndMatchesRealDecodedTotals)
{
    auto loaded = LoadExplorerDocument(BYTECODE_EXPLORER_STRUCTURE_TEST_ABC);
    ASSERT_TRUE(loaded.HasValue());
    const auto root = loaded.Value().structure->RootIds().front();
    const auto inventory = loaded.Value().statistics;
    ASSERT_NE(inventory, nullptr);
    WebApi api(loaded.Value());
    const auto initial = nlohmann::json::parse(api.StatisticsJson(root).Value());
    EXPECT_EQ(initial.at("phase"), "NotStarted");
    EXPECT_TRUE(initial.at("codeBytes").is_null());
    EXPECT_TRUE(initial.at("instructionCount").is_null());
    EXPECT_EQ(initial.at("fileSize"), std::to_string(loaded.Value().summary.fileSize));
    EXPECT_EQ(initial.at("methodCount"), std::to_string(loaded.Value().methods->Size()));
    EXPECT_FALSE(api.StartStatisticsJson(root, "{\"extra\":1}").HasValue());
    ASSERT_TRUE(api.StartStatisticsJson(root, "{}").HasValue());
    // Poll with a bounded deadline; the production lifetime retains the worker between requests.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    nlohmann::json result;
    do {
        result = nlohmann::json::parse(api.StatisticsJson(root).Value());
        if (result.at("phase") != "Building") {
            break;
        }
        std::this_thread::yield();
    } while (std::chrono::steady_clock::now() < deadline);
    EXPECT_EQ(result.at("phase"), "Complete") << result;
    uint64_t bytes = 0;
    uint64_t instructions = 0;
    FunctionDisassembler decoder;
    for (auto offset : inventory->Find(root)->executableOffsets) {
        auto decoded = decoder.Disassemble(loaded.Value().document->File(), *loaded.Value().methods,
                                          ExecutableTarget {root, offset, "", std::nullopt});
        ASSERT_EQ(decoded.status, DisassemblyStatus::SUCCESS);
        bytes += decoded.disassembly->codeSize;
        instructions += decoded.disassembly->instructions.size();
    }
    EXPECT_EQ(result.at("codeBytes"), std::to_string(bytes));
    EXPECT_EQ(result.at("instructionCount"), std::to_string(instructions));
    EXPECT_EQ(result.at("knownCodeBytes"), result.at("codeBytes"));
    EXPECT_EQ(result.at("apiVersion"), 1);
    EXPECT_EQ(result.at("documentId"), initial.at("documentId"));
    EXPECT_FALSE(api.StatisticsJson(99999999).HasValue());
}

std::string ExpectedHex(uint64_t value)
{
    std::ostringstream output;
    output << "0x" << std::uppercase << std::hex << value;
    return output.str();
}

bool IsCanonicalHex(const nlohmann::json &value)
{
    if (!value.is_string()) {
        return false;
    }
    const auto &text = value.get_ref<const std::string &>();
    return text.size() > 2U && text[0] == '0' && text[1] == 'x' &&
           std::all_of(text.begin() + 2, text.end(),
                       [](char character) { return std::isxdigit(static_cast<unsigned char>(character)) != 0; });
}

void ExpectMetadataEnvelope(const nlohmann::json &json, const std::string &documentId)
{
    EXPECT_EQ(json.at("apiVersion"), 1);
    EXPECT_EQ(json.at("documentId"), documentId);
}

size_t SumItemArraySizes(const nlohmann::json &page, std::string_view field)
{
    size_t total = 0U;
    for (const auto &item : page.at("items")) {
        total += item.at(field).size();
    }
    return total;
}

size_t SumDiagnosticBytes(const nlohmann::json &json)
{
    auto total = std::make_shared<size_t>(0U);
    const auto add = [total](const nlohmann::json &diagnostics) {
        for (const auto &diagnostic : diagnostics) {
            *total += diagnostic.get_ref<const std::string &>().size();
        }
    };
    if (json.contains("items")) {
        for (const auto &item : json.at("items")) {
            add(item.at("diagnostics"));
        }
    } else {
        add(json.at("diagnostics"));
    }
    return *total;
}

const EntityNode *FindChild(const StructureIndex &structure, EntityId parentId, EntityKind kind,
                            const std::string &name)
{
    const auto *parent = structure.Find(parentId);
    if (parent == nullptr) {
        return nullptr;
    }
    for (const auto childId : parent->children) {
        const auto *child = structure.Find(childId);
        if (child != nullptr && child->kind == kind && child->VisibleName() == name) {
            return child;
        }
    }
    return nullptr;
}

const EntityNode *FindNode(const StructureIndex &structure, EntityKind kind, const std::string &name)
{
    for (const auto *node : structure.NodesInIdOrder()) {
        if (node->kind == kind && node->VisibleName() == name) {
            return node;
        }
    }
    return nullptr;
}

TEST(WebApiTest, PublishesDocumentWithoutLeakingItsHostPath)
{
    auto loaded = LoadExplorerDocument(BYTECODE_EXPLORER_STRUCTURE_TEST_ABC);
    ASSERT_TRUE(loaded.HasValue()) << loaded.Error().Message();
    const auto expectedFileSize = loaded.Value().summary.fileSize;
    WebApi api(std::move(loaded.Value()));

    const auto response = api.DocumentJson();
    ASSERT_TRUE(response.HasValue()) << response.Error().Message();
    const auto json = nlohmann::json::parse(response.Value());

    EXPECT_EQ(json.at("apiVersion"), 1);
    EXPECT_EQ(json.at("fileName"), "structure_sample.abc");
    EXPECT_EQ(json.at("fileSize"), expectedFileSize);
    EXPECT_FALSE(json.at("documentId").get<std::string>().empty());
    EXPECT_EQ(response.Value().find(BYTECODE_EXPLORER_STRUCTURE_TEST_ABC), std::string::npos);
}

TEST(WebApiTest, PublishesBuildModeVerdictWithItsEvidence)
{
    auto debugLoaded = LoadExplorerDocument(BYTECODE_EXPLORER_BUILD_MODE_DEBUG_TEST_ABC);
    ASSERT_TRUE(debugLoaded.HasValue()) << debugLoaded.Error().Message();
    WebApi debugApi(std::move(debugLoaded.Value()));
    const auto debugResponse = debugApi.DocumentJson();
    ASSERT_TRUE(debugResponse.HasValue()) << debugResponse.Error().Message();
    const auto debugJson = nlohmann::json::parse(debugResponse.Value());

    EXPECT_EQ(debugJson.at("buildMode"), "debug");
    EXPECT_TRUE(debugJson.at("buildEvidence").at("scanComplete").get<bool>());
    EXPECT_GE(debugJson.at("buildEvidence").at("debugOnlyInstructions").get<uint64_t>(), 1U);
    EXPECT_GE(debugJson.at("buildEvidence").at("variableTableEntries").get<uint64_t>(), 1U);
    EXPECT_GE(debugJson.at("buildEvidence").at("scannedMethods").get<uint64_t>(), 1U);

    auto releaseLoaded = LoadExplorerDocument(BYTECODE_EXPLORER_BUILD_MODE_RELEASE_TEST_ABC);
    ASSERT_TRUE(releaseLoaded.HasValue()) << releaseLoaded.Error().Message();
    WebApi releaseApi(std::move(releaseLoaded.Value()));
    const auto releaseResponse = releaseApi.DocumentJson();
    ASSERT_TRUE(releaseResponse.HasValue()) << releaseResponse.Error().Message();
    const auto releaseJson = nlohmann::json::parse(releaseResponse.Value());

    EXPECT_EQ(releaseJson.at("buildMode"), "release");
    EXPECT_TRUE(releaseJson.at("buildEvidence").at("scanComplete").get<bool>());
    EXPECT_EQ(releaseJson.at("buildEvidence").at("debugOnlyInstructions"), 0U);
    EXPECT_EQ(releaseJson.at("buildEvidence").at("variableTableEntries"), 0U);
    EXPECT_EQ(releaseJson.at("buildEvidence").at("sourceCodeMarkers"), 0U);
}

TEST(WebApiTest, PublishesOnlyTheRequestedHexWindow)
{
    auto loaded = LoadExplorerDocument(BYTECODE_EXPLORER_STRUCTURE_TEST_ABC);
    ASSERT_TRUE(loaded.HasValue()) << loaded.Error().Message();
    ASSERT_GT(loaded.Value().summary.fileSize, 48U);
    std::vector<uint8_t> expected(loaded.Value().document->File().GetBase() + 16U,
                                  loaded.Value().document->File().GetBase() + 48U);  // NOLINT
    const auto fileSize = loaded.Value().summary.fileSize;
    WebApi api(std::move(loaded.Value()));

    const auto response = api.HexJson(16U, 32U);
    ASSERT_TRUE(response.HasValue()) << response.Error().Message();
    const auto json = nlohmann::json::parse(response.Value());
    EXPECT_EQ(json.at("fileSize"), ExpectedHex(fileSize));
    EXPECT_EQ(json.at("window").at("offset"), "0x10");
    EXPECT_EQ(json.at("window").at("size"), 32U);
    EXPECT_EQ(json.at("window").at("bytes").get<std::vector<uint8_t>>(), expected);
    EXPECT_EQ(response.Value().find(BYTECODE_EXPLORER_STRUCTURE_TEST_ABC), std::string::npos);
    EXPECT_EQ(fileSize > 48U, true);
}

TEST(WebApiTest, PublishesEofAndRejectsInvalidHexWindows)
{
    auto loaded = LoadExplorerDocument(BYTECODE_EXPLORER_STRUCTURE_TEST_ABC);
    ASSERT_TRUE(loaded.HasValue()) << loaded.Error().Message();
    const auto fileSize = loaded.Value().summary.fileSize;
    WebApi api(std::move(loaded.Value()));

    const auto response = api.HexJson(fileSize, 32U);
    ASSERT_TRUE(response.HasValue()) << response.Error().Message();
    const auto json = nlohmann::json::parse(response.Value());
    EXPECT_TRUE(json.at("window").at("bytes").empty());
    EXPECT_TRUE(json.at("window").at("eof"));
    EXPECT_FALSE(api.HexJson(0U, 0U).HasValue());
    EXPECT_FALSE(api.HexJson(0U, 65537U).HasValue());
    EXPECT_FALSE(api.HexJson(fileSize + 1U, 1U).HasValue());
}

TEST(WebApiTest, PublishesTheSameMostSpecificLocationAsTheCoreIndex)
{
    auto loaded = LoadExplorerDocument(BYTECODE_EXPLORER_STRUCTURE_TEST_ABC);
    ASSERT_TRUE(loaded.HasValue()) << loaded.Error().Message();
    const auto nodes = loaded.Value().structure->NodesInIdOrder();
    const auto ranged = std::find_if(nodes.begin(), nodes.end(), [](const EntityNode *node) {
        return node->primaryRange.has_value() && node->primaryRange->size != 0U;
    });
    ASSERT_NE(ranged, nodes.end());
    const auto offset = (*ranged)->primaryRange->offset;
    const auto expected = loaded.Value().locations->FindMostSpecific(offset);
    ASSERT_TRUE(expected.has_value());
    WebApi api(std::move(loaded.Value()));

    const auto response = api.LocationJson(offset);
    ASSERT_TRUE(response.HasValue()) << response.Error().Message();
    const auto json = nlohmann::json::parse(response.Value());
    const auto &match = json.at("structureMatch");
    EXPECT_EQ(match.at("entityId"), std::to_string(expected->entityId));
    EXPECT_EQ(match.at("rangeKind"), expected->source == FileRangeSource::PRIMARY ? "primary" : "backing");
    EXPECT_EQ(match.at("range").at("offset"), ExpectedHex(expected->range.offset));
    EXPECT_EQ(match.at("range").at("size"), expected->range.size);
    if (expected->backingIndex.has_value()) {
        EXPECT_EQ(match.at("backingIndex"), *expected->backingIndex);
    } else {
        EXPECT_TRUE(match.at("backingIndex").is_null());
    }
}

TEST(WebApiTest, PublishesNullWhenTheLocationIndexHasNoMatch)
{
    auto loaded = LoadExplorerDocument(BYTECODE_EXPLORER_STRUCTURE_TEST_ABC);
    ASSERT_TRUE(loaded.HasValue()) << loaded.Error().Message();
    const auto fileSize = loaded.Value().summary.fileSize;
    loaded.Value().locations = FileRangeLocator::Build(StructureIndex {}, fileSize);
    WebApi api(std::move(loaded.Value()));

    const auto response = api.LocationJson(0U);
    ASSERT_TRUE(response.HasValue()) << response.Error().Message();
    const auto json = nlohmann::json::parse(response.Value());
    EXPECT_TRUE(json.at("structureMatch").is_null());
    EXPECT_FALSE(api.LocationJson(fileSize).HasValue());
}

void ExpectInitialMetadataStatus(WebApi &api, std::string &documentId)
{
    const auto statusResponse = api.MetadataStatusJson();
    ASSERT_TRUE(statusResponse.HasValue()) << statusResponse.Error().Message();
    const auto status = nlohmann::json::parse(statusResponse.Value());
    documentId = status.at("documentId").get<std::string>();
    ExpectMetadataEnvelope(status, documentId);
    EXPECT_EQ(status.at("state"), "NotStarted");
    EXPECT_EQ(status.at("revision"), "0");
    EXPECT_GT(std::stoull(status.at("objectCount").get<std::string>()), 0U);
    EXPECT_TRUE(status.at("scannedMethods").is_string());
    EXPECT_TRUE(status.at("totalMethods").is_string());
    EXPECT_TRUE(status.at("referenceCount").is_string());
    EXPECT_LE(status.at("diagnostics").size(), 64U);
}

void ExpectMetadataObjectsPageWellFormed(WebApi &api, const MetadataObjectQuery &query, const std::string &documentId,
                                         MetadataObjectKey &selectedKey)
{
    const auto pageResponse = api.MetadataObjectsJson(query);
    ASSERT_TRUE(pageResponse.HasValue()) << pageResponse.Error().Message();
    const auto page = nlohmann::json::parse(pageResponse.Value());
    ExpectMetadataEnvelope(page, documentId);
    EXPECT_TRUE(page.at("revision").is_string());
    EXPECT_TRUE(page.at("total").is_string());
    EXPECT_LE(page.at("items").size(), 100U);
    EXPECT_TRUE(page.contains("nextCursor"));

    const nlohmann::json *selected = nullptr;
    for (const auto &item : page.at("items")) {
        EXPECT_TRUE(item.at("kind") == "String" || item.at("kind") == "Method" || item.at("kind") == "Literal Array");
        EXPECT_TRUE(IsCanonicalHex(item.at("key").at("offset")));
        EXPECT_EQ(item.at("key").at("kind"), item.at("kind"));
        EXPECT_LE(item.at("preview").get_ref<const std::string &>().size(), MetadataLimits::MAX_PREVIEW_BYTES);
        EXPECT_LE(item.at("entityIds").size(), 16U);
        EXPECT_TRUE(item.at("entityIdsTruncated").is_boolean());
        EXPECT_TRUE(item.at("entityIdsTotal").is_number_unsigned());
        EXPECT_GE(item.at("entityIdsTotal").get<size_t>(), item.at("entityIds").size());
        EXPECT_EQ(item.at("entityIdsTruncated").get<bool>(),
                  item.at("entityIdsTotal").get<size_t>() > item.at("entityIds").size());
        EXPECT_LE(item.at("diagnostics").size(), 64U);
        EXPECT_TRUE(item.at("range").is_null() || item.at("range").at("size").get<uint64_t>() > 0U);
        for (const auto &entityId : item.at("entityIds")) {
            EXPECT_TRUE(entityId.is_string());
        }
        if (selected == nullptr && item.at("kind") == "String" && item.at("referenceCount").get<size_t>() > 0U) {
            selected = &item;
        }
    }
    ASSERT_NE(selected, nullptr);
    selectedKey = MetadataObjectKey {
        MetadataKind::STRING, std::stoull(selected->at("key").at("offset").get<std::string>().substr(2U), nullptr, 16)};
}

void ExpectMetadataDetailWellFormed(const nlohmann::json &detail, const std::string &documentId)
{
    ExpectMetadataEnvelope(detail, documentId);
    EXPECT_EQ(detail.at("kind"), "String");
    EXPECT_TRUE(IsCanonicalHex(detail.at("key").at("offset")));
    EXPECT_TRUE(detail.at("range").is_null() || IsCanonicalHex(detail.at("range").at("offset")));
    EXPECT_LE(detail.at("preview").get_ref<const std::string &>().size(), MetadataLimits::MAX_PREVIEW_BYTES);
    EXPECT_TRUE(detail.at("value").is_string());
    EXPECT_LE(detail.at("value").get_ref<const std::string &>().size(), MetadataLimits::MAX_DETAIL_BYTES);
    EXPECT_TRUE(detail.at("valueTruncated").is_boolean());
}

void ExpectMetadataReferencesWellFormed(const nlohmann::json &references, const std::string &documentId)
{
    ExpectMetadataEnvelope(references, documentId);
    EXPECT_TRUE(references.at("revision").is_string());
    EXPECT_TRUE(references.at("total").is_string());
    EXPECT_LE(references.at("items").size(), 100U);
    ASSERT_FALSE(references.at("items").empty());
    for (const auto &reference : references.at("items")) {
        EXPECT_TRUE(reference.at("kind").is_string());
        EXPECT_LE(reference.at("sourceEntityIds").size(), 16U);
        EXPECT_TRUE(reference.at("sourceEntityIdsTruncated").is_boolean());
        EXPECT_TRUE(reference.at("sourceEntityIdsTotal").is_number_unsigned());
        EXPECT_GE(reference.at("sourceEntityIdsTotal").get<size_t>(), reference.at("sourceEntityIds").size());
        EXPECT_EQ(reference.at("sourceEntityIdsTruncated").get<bool>(),
                  reference.at("sourceEntityIdsTotal").get<size_t>() > reference.at("sourceEntityIds").size());
        for (const auto &entityId : reference.at("sourceEntityIds")) {
            EXPECT_TRUE(entityId.is_string());
        }
        EXPECT_TRUE(reference.at("sourceMethodOffset").is_null() || IsCanonicalHex(reference.at("sourceMethodOffset")));
        EXPECT_TRUE(reference.at("pc").is_null() || reference.at("pc").is_number_unsigned());
        EXPECT_TRUE(reference.at("sourceRange").is_null() || IsCanonicalHex(reference.at("sourceRange").at("offset")));
        EXPECT_TRUE(reference.at("sourceRange").is_null() ||
                    reference.at("sourceRange").at("size").get<uint64_t>() > 0U);
        EXPECT_LE(reference.at("sourceName").get_ref<const std::string &>().size(), MetadataLimits::MAX_PREVIEW_BYTES);
        EXPECT_LE(reference.at("mnemonic").get_ref<const std::string &>().size(), 128U);
        EXPECT_TRUE(reference.at("operandIndex").is_null() || reference.at("operandIndex").is_number_unsigned());
    }
}

TEST(WebApiTest, PublishesBoundedMetadataStatusPagesDetailsAndReferences)
{
    auto loaded = LoadExplorerDocument(BYTECODE_EXPLORER_STRUCTURE_TEST_ABC);
    ASSERT_TRUE(loaded.HasValue()) << loaded.Error().Message();
    WebApi api(std::move(loaded.Value()));

    std::string documentId;
    ASSERT_NO_FATAL_FAILURE(ExpectInitialMetadataStatus(api, documentId));

    MetadataObjectQuery staleQuery;
    staleQuery.kind = MetadataKind::STRING;
    staleQuery.limit = 1U;
    const auto stalePageResponse = api.MetadataObjectsJson(staleQuery);
    ASSERT_TRUE(stalePageResponse.HasValue()) << stalePageResponse.Error().Message();
    const auto stalePage = nlohmann::json::parse(stalePageResponse.Value());
    ASSERT_FALSE(stalePage.at("nextCursor").is_null());
    const auto staleCursor = stalePage.at("nextCursor").get<std::string>();

    const auto startResponse = api.StartMetadataIndexJson("{}");
    ASSERT_TRUE(startResponse.HasValue()) << startResponse.Error().Message();
    ExpectMetadataEnvelope(nlohmann::json::parse(startResponse.Value()), documentId);
    api.WaitForMetadataIndexForTesting();
    const auto readyResponse = api.MetadataStatusJson();
    ASSERT_TRUE(readyResponse.HasValue()) << readyResponse.Error().Message();
    const auto ready = nlohmann::json::parse(readyResponse.Value());
    ExpectMetadataEnvelope(ready, documentId);
    EXPECT_TRUE(ready.at("state") == "Ready" || ready.at("state") == "Partial");

    MetadataObjectQuery query;
    query.limit = 100U;
    MetadataObjectKey key;
    ASSERT_NO_FATAL_FAILURE(ExpectMetadataObjectsPageWellFormed(api, query, documentId, key));

    const auto detailResponse = api.MetadataObjectJson(key);
    ASSERT_TRUE(detailResponse.HasValue()) << detailResponse.Error().Message();
    const auto detail = nlohmann::json::parse(detailResponse.Value());
    ExpectMetadataDetailWellFormed(detail, documentId);

    const auto referencesResponse = api.MetadataReferencesJson(key, std::nullopt, 100U);
    ASSERT_TRUE(referencesResponse.HasValue()) << referencesResponse.Error().Message();
    const auto references = nlohmann::json::parse(referencesResponse.Value());
    ASSERT_NO_FATAL_FAILURE(ExpectMetadataReferencesWellFormed(references, documentId));

    staleQuery.cursor = staleCursor;
    const auto stale = api.MetadataObjectsJson(staleQuery);
    EXPECT_FALSE(stale.HasValue());
    EXPECT_EQ(stale.Error().Code(), ErrorCode::CANCELLED);

    MetadataObjectQuery invalidKind;
    invalidKind.kind = static_cast<MetadataKind>(255U);
    const auto invalid = api.MetadataObjectsJson(invalidKind);
    EXPECT_FALSE(invalid.HasValue());
    EXPECT_EQ(invalid.Error().Code(), ErrorCode::INVALID_ARGUMENT);
}

TEST(WebApiTest, PublishesRealLiteralArraysWithoutFabricatingNavigableRanges)
{
    auto loaded = LoadExplorerDocument(BYTECODE_EXPLORER_STRUCTURE_TEST_ABC);
    ASSERT_TRUE(loaded.HasValue()) << loaded.Error().Message();
    WebApi api(std::move(loaded.Value()));

    MetadataObjectQuery query;
    query.kind = MetadataKind::LITERAL_ARRAY;
    query.limit = 100U;
    const auto response = api.MetadataObjectsJson(query);
    ASSERT_TRUE(response.HasValue()) << response.Error().Message();
    const auto page = nlohmann::json::parse(response.Value());

    ASSERT_FALSE(page.at("items").empty());
    for (const auto &item : page.at("items")) {
        EXPECT_EQ(item.at("kind"), "Literal Array");
        EXPECT_TRUE(item.at("range").is_null());
    }
}

TEST(WebApiTest, PublishesUnknownReferenceExtentsAsNull)
{
    auto loaded = LoadExplorerDocument(BYTECODE_EXPLORER_STRUCTURE_TEST_ABC);
    ASSERT_TRUE(loaded.HasValue()) << loaded.Error().Message();
    auto physical = PhysicalReader::Read(loaded.Value().document->File());
    ASSERT_TRUE(physical.HasValue()) << physical.Error().Message();
    auto unknownExtent = std::make_shared<const PhysicalMetadataReference *>(nullptr);
    const auto findUnknown = [unknownExtent](const auto &references) {
        const auto found = std::find_if(references.begin(), references.end(), [](const auto &item) {
            return item.sourceRange.has_value() && item.sourceRange->size == 0U;
        });
        if (found != references.end()) {
            *unknownExtent = &*found;
        }
    };
    findUnknown(physical.Value().metadataReferences);
    for (const auto &literal : physical.Value().literalArrays) {
        if (*unknownExtent == nullptr) {
            findUnknown(literal.metadataReferences);
        }
    }
    ASSERT_NE(*unknownExtent, nullptr);
    MetadataKind kind = MetadataKind::STRING;
    if ((*unknownExtent)->targetKind == PhysicalMetadataTargetKind::METHOD) {
        kind = MetadataKind::METHOD;
    } else if ((*unknownExtent)->targetKind == PhysicalMetadataTargetKind::LITERAL_ARRAY) {
        kind = MetadataKind::LITERAL_ARRAY;
    }
    const MetadataObjectKey key {kind, (*unknownExtent)->targetOffset};
    WebApi api(std::move(loaded.Value()));

    const auto response = api.MetadataReferencesJson(key, std::nullopt, 100U);
    ASSERT_TRUE(response.HasValue()) << response.Error().Message();
    const auto page = nlohmann::json::parse(response.Value());

    ASSERT_FALSE(page.at("items").empty());
    for (const auto &item : page.at("items")) {
        EXPECT_TRUE(item.at("sourceRange").is_null() || item.at("sourceRange").at("size").get<uint64_t>() > 0U);
    }
}

TEST(WebApiTest, MetadataIndexAcceptsOnlyAnEmptyJsonObject)
{
    auto loaded = LoadExplorerDocument(BYTECODE_EXPLORER_STRUCTURE_TEST_ABC);
    ASSERT_TRUE(loaded.HasValue()) << loaded.Error().Message();
    WebApi api(std::move(loaded.Value()));

    EXPECT_FALSE(api.StartMetadataIndexJson("").HasValue());
    EXPECT_FALSE(api.StartMetadataIndexJson("[]").HasValue());
    EXPECT_FALSE(api.StartMetadataIndexJson(R"({"x":1})").HasValue());
    EXPECT_FALSE(api.StartMetadataIndexJson("{} trailing").HasValue());
    EXPECT_TRUE(api.StartMetadataIndexJson(" \n { } \t ").HasValue());
    api.WaitForMetadataIndexForTesting();
}

TEST(WebApiTest, MetadataStatusReportsCatalogDiagnosticTruncation)
{
    auto loaded = LoadExplorerDocument(BYTECODE_EXPLORER_STRUCTURE_TEST_ABC);
    ASSERT_TRUE(loaded.HasValue()) << loaded.Error().Message();
    auto &diagnostics = const_cast<std::vector<std::string> &>(loaded.Value().metadata->Diagnostics());
    diagnostics.clear();
    for (size_t index = 0U; index < 65U; ++index) {
        diagnostics.push_back("catalog-diagnostic-" + std::to_string(index));
    }
    WebApi api(std::move(loaded.Value()));

    const auto response = api.MetadataStatusJson();
    ASSERT_TRUE(response.HasValue()) << response.Error().Message();
    const auto status = nlohmann::json::parse(response.Value());

    EXPECT_EQ(status.at("diagnostics").size(), 64U);
    EXPECT_TRUE(status.at("diagnosticsTruncated"));
}

// All reference kinds in MetadataReferenceKind enum order, so the serializer must emit
// each label exactly once across the fixture rows.
const std::vector<std::string> REFERENCE_KINDS {
    "Record Name",
    "Method Name",
    "Field Name",
    "Annotation Name",
    "Annotation Element Name",
    "Annotation String Value",
    "Module Request",
    "Import Name",
    "Export Name",
    "Local Name",
    "Scope Name",
    "Literal String",
    "Literal Method",
    "Nested Literal Array",
    "Instruction Operand",
};

// Builds an object page whose rows have asymmetric id and diagnostic counts and
// checks that serialization caps each array per row. Returns the serialized page.
nlohmann::json ExpectObjectPageSerializersBoundPerRow()
{
    MetadataPage<MetadataObject> objectPage;
    objectPage.total = 2U;
    for (size_t itemIndex = 0U; itemIndex < 2U; ++itemIndex) {
        MetadataObject object;
        object.key = {MetadataKind::STRING, itemIndex};
        object.origins = {MetadataOrigin::INDEX, MetadataOrigin::STRUCTURE, MetadataOrigin::LITERAL,
                          MetadataOrigin::INSTRUCTION};
        const size_t stored = itemIndex == 0U ? 60U : 10U;
        for (size_t index = 0U; index < stored; ++index) {
            object.entityIds.push_back(itemIndex * 100U + index + 1U);
            object.diagnostics.push_back("diagnostic-" + std::to_string(itemIndex) + "-" + std::to_string(index));
        }
        objectPage.items.push_back(std::move(object));
    }

    const auto objects = nlohmann::json::parse(WebApiTestAccess::Objects(objectPage));

    // Entity ids are capped per array: the first item keeps 16 of its 60 and the second item
    // still receives its full 10 instead of being starved by the earlier rows.
    EXPECT_EQ(SumItemArraySizes(objects, "entityIds"), 26U);
    EXPECT_EQ(objects.at("items").at(0U).at("entityIds").size(), 16U);
    EXPECT_TRUE(objects.at("items").at(0U).at("entityIdsTruncated"));
    EXPECT_EQ(objects.at("items").at(0U).at("entityIdsTotal"), 60U);
    EXPECT_EQ(objects.at("items").at(1U).at("entityIds").size(), 10U);
    EXPECT_FALSE(objects.at("items").at(1U).at("entityIdsTruncated"));
    EXPECT_EQ(objects.at("items").at(1U).at("entityIdsTotal"), 10U);
    // Diagnostics keep the response-wide budget.
    EXPECT_EQ(SumItemArraySizes(objects, "diagnostics"), 64U);
    EXPECT_LE(SumDiagnosticBytes(objects), 64U * MetadataLimits::MAX_PREVIEW_BYTES);
    EXPECT_FALSE(objects.at("items").at(0U).at("diagnosticsTruncated"));
    EXPECT_TRUE(objects.at("items").at(1U).at("diagnosticsTruncated"));
    EXPECT_EQ(objects.at("items").at(0U).at("key").at("offset"), "0x0");
    EXPECT_EQ(objects.at("items").at(0U).at("origins"),
              nlohmann::json({"Index", "Structure", "Literal", "Instruction"}));
    return objects;
}

// Builds a reference page across every reference kind with asymmetric source-id counts
// and checks the per-row caps and the emitted kind labels. Returns the serialized page.
nlohmann::json ExpectReferencePageSerializersBoundPerRow()
{
    MetadataPage<MetadataReference> referencePage;
    for (size_t index = 0U; index < REFERENCE_KINDS.size(); ++index) {
        MetadataReference reference;
        reference.target = {MetadataKind::STRING, 0U};
        reference.kind = static_cast<MetadataReferenceKind>(index);
        reference.sourceMethodOffset = 0U;
        reference.sourceRange = FileRange {0U, 1U};
        const size_t stored = index == 0U ? 60U : (index == 1U ? 10U : 0U);
        for (size_t entity = 0U; entity < stored; ++entity) {
            reference.sourceEntityIds.push_back(index * 100U + entity + 1U);
        }
        referencePage.items.push_back(std::move(reference));
    }

    const auto references =
        nlohmann::json::parse(WebApiTestAccess::References({MetadataKind::STRING, 0U}, referencePage));

    // Each reference row keeps ids up to the per-row cap: the 60-id row is truncated to 16,
    // and later rows are no longer starved by it.
    EXPECT_EQ(SumItemArraySizes(references, "sourceEntityIds"), 26U);
    EXPECT_EQ(references.at("items").at(0U).at("sourceEntityIds").size(), 16U);
    EXPECT_TRUE(references.at("items").at(0U).at("sourceEntityIdsTruncated"));
    EXPECT_EQ(references.at("items").at(0U).at("sourceEntityIdsTotal"), 60U);
    EXPECT_EQ(references.at("items").at(1U).at("sourceEntityIds").size(), 10U);
    EXPECT_FALSE(references.at("items").at(1U).at("sourceEntityIdsTruncated"));
    EXPECT_EQ(references.at("items").at(1U).at("sourceEntityIdsTotal"), 10U);
    EXPECT_EQ(references.at("items").at(0U).at("sourceMethodOffset"), "0x0");
    EXPECT_EQ(references.at("items").at(0U).at("sourceRange").at("offset"), "0x0");
    for (size_t index = 0U; index < REFERENCE_KINDS.size(); ++index) {
        EXPECT_EQ(references.at("items").at(index).at("kind"), REFERENCE_KINDS[index]);
    }
    return references;
}

TEST(WebApiTest, MetadataPageSerializersBoundEachArrayIndependently)
{
    const auto objects = ExpectObjectPageSerializersBoundPerRow();

    const auto references = ExpectReferencePageSerializersBoundPerRow();

    // Both serializers ran to completion without interference: each response carries
    // every row it was built with.
    EXPECT_EQ(objects.at("items").size(), 2U);
    EXPECT_EQ(references.at("items").size(), REFERENCE_KINDS.size());
}

TEST(WebApiTest, MetadataDetailSerializerReceivesAFreshBoundedBudget)
{
    MetadataObjectDetail detail;
    detail.object.key = {MetadataKind::STRING, 0U};
    detail.object.preview = "detail";
    detail.object.diagnostics.push_back(std::string(MetadataLimits::MAX_PREVIEW_BYTES + 1U, 'x'));
    for (size_t index = 1U; index < 65U; ++index) {
        detail.object.diagnostics.push_back("detail-diagnostic-" + std::to_string(index));
    }
    for (size_t index = 0U; index < 300U; ++index) {
        detail.object.entityIds.push_back(index + 1U);
    }

    const auto json = nlohmann::json::parse(WebApiTestAccess::Detail(detail));

    EXPECT_EQ(json.at("entityIds").size(), 256U);
    EXPECT_TRUE(json.at("entityIdsTruncated"));
    EXPECT_EQ(json.at("entityIdsTotal"), 300U);
    EXPECT_EQ(json.at("diagnostics").size(), 64U);
    EXPECT_TRUE(json.at("diagnosticsTruncated"));
    EXPECT_EQ(json.at("diagnostics").front().get_ref<const std::string &>().size(), MetadataLimits::MAX_PREVIEW_BYTES);
    EXPECT_LE(SumDiagnosticBytes(json), 64U * MetadataLimits::MAX_PREVIEW_BYTES);
    EXPECT_EQ(json.at("key").at("offset"), "0x0");
}

TEST(WebApiTest, StructureSerializationHonorsCancellationBeforeCachedOrUncachedResponses)
{
    auto loaded = LoadExplorerDocument(BYTECODE_EXPLORER_STRUCTURE_TEST_ABC);
    ASSERT_TRUE(loaded.HasValue()) << loaded.Error().Message();
    WebApi api(std::move(loaded.Value()));
    CancellationSource cancellation;
    cancellation.Cancel();

    const auto cancelled = api.StructureJson(cancellation.Token());
    ASSERT_FALSE(cancelled.HasValue());
    EXPECT_EQ(cancelled.Error().Code(), ErrorCode::CANCELLED);
    ASSERT_TRUE(api.StructureJson().HasValue());
    const auto cachedCancelled = api.StructureJson(cancellation.Token());
    ASSERT_FALSE(cachedCancelled.HasValue());
    EXPECT_EQ(cachedCancelled.Error().Code(), ErrorCode::CANCELLED);
}

TEST(WebApiTest, StructureSerializationCachesSuccessfulPayloadWithoutRevisitingNodes)
{
    auto loaded = LoadExplorerDocument(BYTECODE_EXPLORER_STRUCTURE_TEST_ABC);
    ASSERT_TRUE(loaded.HasValue()) << loaded.Error().Message();
    const auto nodeCount = loaded.Value().structure->Size();
    WebApi api(std::move(loaded.Value()));
    auto checks = std::make_shared<size_t>(0U);
    const CancellationToken cancellation([checks] {
        ++*checks;
        return false;
    });

    const auto first = api.StructureJson(cancellation);
    ASSERT_TRUE(first.HasValue()) << first.Error().Message();
    EXPECT_GT(*checks, nodeCount);
    *checks = 0U;
    const auto repeated = api.StructureJson(cancellation);
    ASSERT_TRUE(repeated.HasValue()) << repeated.Error().Message();
    EXPECT_EQ(repeated.Value(), first.Value());
    EXPECT_GT(*checks, 0U);
    EXPECT_LE(*checks, 2U);
    EXPECT_EQ(nlohmann::json::parse(repeated.Value()), nlohmann::json::parse(first.Value()));
}

TEST(WebApiTest, StructureSerializationDoesNotCachePartialOrFinallyCancelledPayloads)
{
    auto loaded = LoadExplorerDocument(BYTECODE_EXPLORER_STRUCTURE_TEST_ABC);
    ASSERT_TRUE(loaded.HasValue()) << loaded.Error().Message();
    auto completeChecks = std::make_shared<size_t>(0U);
    WebApi baseline(loaded.Value());
    const auto expected = baseline.StructureJson(CancellationToken([completeChecks] {
        ++*completeChecks;
        return false;
    }));
    ASSERT_TRUE(expected.HasValue()) << expected.Error().Message();
    ASSERT_GT(*completeChecks, 4U);

    for (const auto cancelAt : {*completeChecks / 2U, *completeChecks}) {
        WebApi api(loaded.Value());
        auto checks = std::make_shared<size_t>(0U);
        const auto cancelled =
            api.StructureJson(CancellationToken([checks, cancelAt] { return ++*checks == cancelAt; }));
        ASSERT_FALSE(cancelled.HasValue());
        EXPECT_EQ(cancelled.Error().Code(), ErrorCode::CANCELLED);
        EXPECT_EQ(*checks, cancelAt);

        *checks = 0U;
        const auto retried = api.StructureJson(CancellationToken([checks] {
            ++*checks;
            return false;
        }));
        ASSERT_TRUE(retried.HasValue()) << retried.Error().Message();
        EXPECT_EQ(*checks, *completeChecks);
        EXPECT_EQ(retried.Value(), expected.Value());
    }
}

TEST(WebApiTest, StructureSerializationConcurrentRequestsShareOneCompleteBuild)
{
    auto loaded = LoadExplorerDocument(BYTECODE_EXPLORER_STRUCTURE_TEST_ABC);
    ASSERT_TRUE(loaded.HasValue()) << loaded.Error().Message();
    auto api = std::make_shared<WebApi>(std::move(loaded.Value()));
    std::vector<std::future<std::pair<Result<std::string>, size_t>>> requests;
    for (size_t index = 0U; index < 4U; ++index) {
        requests.push_back(std::async(std::launch::async, [api] {
            auto checks = std::make_shared<size_t>(0U);
            auto response = api->StructureJson(CancellationToken([checks] {
                ++*checks;
                return false;
            }));
            return std::make_pair(std::move(response), *checks);
        }));
    }
    size_t builders = 0U;
    std::optional<std::string> expected;
    for (auto &request : requests) {
        auto [response, checks] = request.get();
        ASSERT_TRUE(response.HasValue()) << response.Error().Message();
        builders += checks > 2U ? 1U : 0U;
        if (!expected.has_value()) {
            expected = response.Value();
        }
        EXPECT_EQ(response.Value(), *expected);
    }
    EXPECT_EQ(builders, 1U);
}

TEST(WebApiTest, StructureSerializationPreservesAllFieldsEscapingAndNodeOrder)
{
    auto loaded = LoadExplorerDocument(BYTECODE_EXPLORER_STRUCTURE_TEST_ABC);
    ASSERT_TRUE(loaded.HasValue()) << loaded.Error().Message();
    auto structure = std::make_shared<StructureIndex>();
    ASSERT_TRUE(structure->Add(EntityNode(20U, INVALID_ENTITY_ID, EntityKind::FILE, "root")));
    EntityNode method(3U, 20U, EntityKind::METHOD, "semantic\n\"name");
    method.displayName = "方法\\display";
    method.internalName = std::string("invalid-") + static_cast<char>(0xFF);
    method.role = EntityRole::STATIC_METHOD;
    method.group = EntityGroup::COMPILER_GENERATED;
    method.origin = EntityOrigin::RECONSTRUCTED;
    method.isStatic = true;
    method.defaultCollapsed = true;
    method.primaryRange = FileRange {32U, 8U};
    method.backing.push_back({PhysicalEntityKind::METHOD, {32U, 8U}, "physical\"method"});
    method.diagnostics.push_back({DiagnosticSeverity::WARNING, "warning\nmessage", FileRange {33U, 1U}});
    method.diagnostics.push_back({DiagnosticSeverity::INFO, "no range", std::nullopt});
    ASSERT_TRUE(structure->Add(std::move(method)));
    loaded.Value().structure = structure;
    WebApi api(std::move(loaded.Value()));

    const auto response = api.StructureJson();
    ASSERT_TRUE(response.HasValue()) << response.Error().Message();
    const auto json = nlohmann::json::parse(response.Value());
    EXPECT_EQ(json.at("apiVersion"), 1);
    EXPECT_EQ(json.at("documentId"), nlohmann::json::parse(api.DocumentJson().Value()).at("documentId"));
    EXPECT_EQ(json.at("roots"), nlohmann::json::array({"20"}));
    ASSERT_EQ(json.at("nodes").size(), 2U);
    const nlohmann::json expectedMethod = {
        {"id", "3"}, {"parentId", "20"}, {"kind", "Method"}, {"role", "Static Method"},
        {"group", "Compiler Generated"}, {"origin", "Reconstructed"}, {"name", "方法\\display"},
        {"semanticName", "semantic\n\"name"}, {"internalName", "invalid-\xEF\xBF\xBD"}, {"isStatic", true},
        {"defaultCollapsed", true}, {"executable", true}, {"executableTargetCount", 1U},
        {"children", nlohmann::json::array()}, {"primaryRange", {{"offset", "0x20"}, {"size", 8U}}},
        {"backing", {{{"kind", "Method"}, {"name", "physical\"method"},
                       {"range", {{"offset", "0x20"}, {"size", 8U}}}}}},
        {"diagnostics", {{{"severity", "Warning"}, {"message", "warning\nmessage"},
                           {"range", {{"offset", "0x21"}, {"size", 1U}}}},
                         {{"severity", "Info"}, {"message", "no range"}, {"range", nullptr}}}},
    };
    EXPECT_EQ(json.at("nodes").front(), expectedMethod);
    const auto &root = json.at("nodes").back();
    EXPECT_EQ(root.at("id"), "20");
    EXPECT_EQ(root.at("children"), nlohmann::json::array({"3"}));
    EXPECT_TRUE(root.at("parentId").is_null());
    EXPECT_TRUE(root.at("primaryRange").is_null());
    EXPECT_FALSE(root.at("executable"));
}

TEST(WebApiTest, PublishesStableStructureIdsAndRealDisassembly)
{
    auto loaded = LoadExplorerDocument(BYTECODE_EXPLORER_STRUCTURE_TEST_ABC);
    ASSERT_TRUE(loaded.HasValue()) << loaded.Error().Message();
    const auto *model = FindNode(*loaded.Value().structure, EntityKind::CLASS, "BetaModel");
    ASSERT_NE(model, nullptr);
    const auto *create = FindChild(*loaded.Value().structure, model->id, EntityKind::METHOD, "create");
    ASSERT_NE(create, nullptr);
    const auto createId = create->id;
    WebApi api(std::move(loaded.Value()));

    const auto structureResponse = api.StructureJson();
    ASSERT_TRUE(structureResponse.HasValue()) << structureResponse.Error().Message();
    const auto structure = nlohmann::json::parse(structureResponse.Value());
    ASSERT_FALSE(structure.at("roots").empty());
    EXPECT_TRUE(structure.at("roots").front().is_string());
    const auto createIdText = std::to_string(createId);
    const auto found = std::find_if(structure.at("nodes").begin(), structure.at("nodes").end(),
                                    [createIdText](const auto &node) { return node.at("id") == createIdText; });
    ASSERT_NE(found, structure.at("nodes").end());
    EXPECT_EQ(found->at("kind"), "Method");
    EXPECT_EQ(found->at("name"), "create");
    EXPECT_TRUE(found->at("executable"));

    const auto disassemblyResponse = api.DisassemblyJson(createId);
    ASSERT_TRUE(disassemblyResponse.HasValue()) << disassemblyResponse.Error().Message();
    const auto disassembly = nlohmann::json::parse(disassemblyResponse.Value());
    EXPECT_EQ(disassembly.at("entityId"), createIdText);
    EXPECT_EQ(disassembly.at("function").at("name"), "create");
    EXPECT_GT(disassembly.at("instructions").size(), 0U);
    EXPECT_TRUE(disassembly.at("instructions").front().contains("pc"));
    EXPECT_TRUE(disassembly.at("instructions").front().contains("mnemonic"));
    EXPECT_TRUE(disassembly.at("instructions").front().contains("operands"));
}

TEST(WebApiTest, PublishesRealCfgAnalysisBlocksAndInitialLayout)
{
    auto loaded = LoadExplorerDocument(BYTECODE_EXPLORER_STRUCTURE_TEST_ABC);
    ASSERT_TRUE(loaded.HasValue()) << loaded.Error().Message();
    const auto *model = FindNode(*loaded.Value().structure, EntityKind::CLASS, "BetaModel");
    ASSERT_NE(model, nullptr);
    const auto *create = FindChild(*loaded.Value().structure, model->id, EntityKind::METHOD, "create");
    ASSERT_NE(create, nullptr);
    const auto createId = create->id;
    WebApi api(std::move(loaded.Value()));

    const auto response = api.CfgJson(createId);

    ASSERT_TRUE(response.HasValue()) << response.Error().Message();
    const auto json = nlohmann::json::parse(response.Value());
    EXPECT_EQ(json.at("entityId"), std::to_string(createId));
    ASSERT_FALSE(json.at("graph").at("blocks").empty());
    const auto &firstBlock = json.at("graph").at("blocks").front();
    EXPECT_TRUE(firstBlock.at("id").is_string());
    EXPECT_TRUE(firstBlock.contains("instructions"));
    EXPECT_TRUE(firstBlock.contains("reachable"));
    EXPECT_TRUE(firstBlock.contains("immediateDominator"));
    EXPECT_TRUE(firstBlock.at("immediateDominator").is_null() || firstBlock.at("immediateDominator").is_string());
    EXPECT_TRUE(json.at("graph").at("entryBlock").is_string());
    for (const auto &edge : json.at("graph").at("edges")) {
        EXPECT_TRUE(edge.at("id").is_string());
        EXPECT_TRUE(edge.at("source").is_string());
        EXPECT_TRUE(edge.at("target").is_null() || edge.at("target").is_string());
    }
    EXPECT_EQ(json.at("layout").at("nodes").size(), json.at("graph").at("blocks").size());
    EXPECT_EQ(json.at("layout").at("edges").size(), json.at("graph").at("edges").size());
    EXPECT_TRUE(json.at("layout").at("nodes").front().at("block").is_string());
    EXPECT_TRUE(json.at("layout").at("edges").front().at("edge").is_string());
    EXPECT_GT(json.at("layout").at("canvas").at("width").get<double>(), 0.0);
}

TEST(WebApiTest, RecomputesCfgLayoutFromMeasuredSizesAndPinsWithoutSessionState)
{
    auto loaded = LoadExplorerDocument(BYTECODE_EXPLORER_STRUCTURE_TEST_ABC);
    ASSERT_TRUE(loaded.HasValue()) << loaded.Error().Message();
    const auto *model = FindNode(*loaded.Value().structure, EntityKind::CLASS, "BetaModel");
    ASSERT_NE(model, nullptr);
    const auto *create = FindChild(*loaded.Value().structure, model->id, EntityKind::METHOD, "create");
    ASSERT_NE(create, nullptr);
    const auto createId = create->id;
    WebApi api(std::move(loaded.Value()));
    const nlohmann::json request = {
        {"revision", 9U},
        {"nodes", {{{"block", "0"}, {"width", 512.0}, {"height", 240.0}, {"pinned", {{"x", 32.0}, {"y", 48.0}}}}}},
    };

    const auto initial = api.CfgJson(createId);
    ASSERT_TRUE(initial.HasValue()) << initial.Error().Message();
    const auto response = api.CfgLayoutJson(createId, request.dump());

    ASSERT_TRUE(response.HasValue()) << response.Error().Message();
    const auto json = nlohmann::json::parse(response.Value());
    EXPECT_EQ(json.at("layout").at("revision"), 9U);
    const auto &node = json.at("layout").at("nodes").front();
    EXPECT_TRUE(node.at("pinned"));
    EXPECT_DOUBLE_EQ(node.at("x").get<double>(), 32.0);
    EXPECT_DOUBLE_EQ(node.at("y").get<double>(), 48.0);
    EXPECT_DOUBLE_EQ(node.at("width").get<double>(), 512.0);
    EXPECT_DOUBLE_EQ(node.at("height").get<double>(), 240.0);
    EXPECT_EQ(api.CfgAnalysisBuildCountForTesting(), 1U);

    const auto malformed = api.CfgLayoutJson(createId, R"({"nodes":"not-an-array"})");
    EXPECT_FALSE(malformed.HasValue());
    EXPECT_EQ(malformed.Error().Code(), ErrorCode::INVALID_ARGUMENT);
}

}  // namespace
}  // namespace panda::bytecode_explorer::test
