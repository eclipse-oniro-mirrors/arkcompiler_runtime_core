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

#include "web/api/web_api.h"

#include "core/cfg/cfg_analysis.h"
#include "core/cfg/cfg_builder.h"
#include "core/cfg/cfg_layout.h"
#include "core/executable_target_resolver.h"
#include "core/function_disassembler.h"
#include "core/metadata/metadata_index_service.h"
#include "core/reader/hex_window_reader.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <charconv>
#include <iomanip>
#include <list>
#include <limits>
#include <mutex>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace panda::bytecode_explorer {

struct WebStructureCache {
    std::mutex mutex;
    std::optional<std::string> json;
};

struct WebCfgCache {
    static constexpr uint64_t MAX_BYTES = 64U * 1024U * 1024U;
    static constexpr size_t MAX_ENTRIES = 32U;

    struct Entry {
        CfgAnalysisPtr analysis;
        uint64_t bytes {0U};
        std::list<uint64_t>::iterator lru;
    };

    std::mutex mutex;
    std::list<uint64_t> lru;
    std::unordered_map<uint64_t, Entry> entries;
    std::optional<uint64_t> activeMethodOffset;
    CfgAnalysisPtr activeAnalysis;
    uint64_t liveBytes {0U};
    size_t buildCount {0U};
};

namespace {

using Json = nlohmann::json;

constexpr size_t MAX_WEB_ROW_ENTITY_IDS = 16U;    // per reference row / list item
constexpr size_t MAX_WEB_OBJECT_ENTITY_IDS = 256U; // per object detail association list
constexpr size_t MAX_WEB_DIAGNOSTICS = 64U;
constexpr size_t MAX_WEB_DIAGNOSTIC_BYTES = MAX_WEB_DIAGNOSTICS * MetadataLimits::MAX_PREVIEW_BYTES;
constexpr int HEX_BYTE_WIDTH = 2;

struct MetadataJsonBudget {
    size_t diagnostics {MAX_WEB_DIAGNOSTICS};
    size_t diagnosticBytes {MAX_WEB_DIAGNOSTIC_BYTES};
};

struct BoundedJsonArray {
    Json values {Json::array()};
    bool truncated {false};
    size_t total {0U};
};

std::string Hex(uint64_t value, size_t width = 0U)
{
    std::ostringstream output;
    output << "0x" << std::uppercase << std::hex << std::setfill('0');
    if (width > 0U) {
        output << std::setw(static_cast<int>(width));
    }
    output << value;
    return output.str();
}

std::string FileName(std::string_view path)
{
    const auto separator = path.find_last_of("/\\");
    return std::string(path.substr(separator == std::string_view::npos ? 0U : separator + 1U));
}

std::string Version(const std::array<uint8_t, ABC_VERSION_SIZE> &version)
{
    std::ostringstream output;
    for (size_t index = 0U; index < version.size(); ++index) {
        if (index != 0U) {
            output << '.';
        }
        output << static_cast<uint32_t>(version[index]);
    }
    return output.str();
}

std::string DocumentId(const DocumentSummary &summary)
{
    return "abc-" + Hex(summary.checksum, 8U).substr(2U) + "-" + std::to_string(summary.fileSize);
}

Json RangeJson(const FileRange &range)
{
    return {{"offset", Hex(range.offset)}, {"size", range.size}};
}

std::string BoundedMetadataText(std::string_view text, size_t limit)
{
    std::string bounded;
    bounded.reserve(std::min(text.size(), limit));
    metadata_model_detail::AppendBoundedUtf8(&bounded, text, limit);
    return bounded;
}

const char *MetadataKindName(MetadataKind kind)
{
    switch (kind) {
        case MetadataKind::STRING:
            return "String";
        case MetadataKind::METHOD:
            return "Method";
        case MetadataKind::LITERAL_ARRAY:
            return "Literal Array";
    }
    return nullptr;
}

const char *MetadataOriginName(MetadataOrigin origin)
{
    switch (origin) {
        case MetadataOrigin::INDEX:
            return "Index";
        case MetadataOrigin::STRUCTURE:
            return "Structure";
        case MetadataOrigin::LITERAL:
            return "Literal";
        case MetadataOrigin::INSTRUCTION:
            return "Instruction";
    }
    return "Unknown";
}

const char *MetadataReferenceKindName(MetadataReferenceKind kind)
{
    switch (kind) {
        case MetadataReferenceKind::RECORD_NAME:
            return "Record Name";
        case MetadataReferenceKind::METHOD_NAME:
            return "Method Name";
        case MetadataReferenceKind::FIELD_NAME:
            return "Field Name";
        case MetadataReferenceKind::ANNOTATION_NAME:
            return "Annotation Name";
        case MetadataReferenceKind::ANNOTATION_ELEMENT_NAME:
            return "Annotation Element Name";
        case MetadataReferenceKind::ANNOTATION_STRING_VALUE:
            return "Annotation String Value";
        case MetadataReferenceKind::MODULE_REQUEST:
            return "Module Request";
        case MetadataReferenceKind::IMPORT_NAME:
            return "Import Name";
        case MetadataReferenceKind::EXPORT_NAME:
            return "Export Name";
        case MetadataReferenceKind::LOCAL_NAME:
            return "Local Name";
        case MetadataReferenceKind::SCOPE_NAME:
            return "Scope Name";
        case MetadataReferenceKind::LITERAL_STRING:
            return "Literal String";
        case MetadataReferenceKind::LITERAL_METHOD:
            return "Literal Method";
        case MetadataReferenceKind::NESTED_LITERAL_ARRAY:
            return "Nested Literal Array";
        case MetadataReferenceKind::INSTRUCTION_OPERAND:
            return "Instruction Operand";
    }
    return "Unknown";
}

const char *MetadataPhaseName(MetadataIndexPhase phase)
{
    switch (phase) {
        case MetadataIndexPhase::NOT_STARTED:
            return "NotStarted";
        case MetadataIndexPhase::BUILDING:
            return "Building";
        case MetadataIndexPhase::READY:
            return "Ready";
        case MetadataIndexPhase::PARTIAL:
            return "Partial";
        case MetadataIndexPhase::FAILED:
            return "Failed";
        case MetadataIndexPhase::CANCELLED:
            return "Cancelled";
    }
    return "Failed";
}

Json MetadataDiagnosticsJson(const std::vector<std::string> &diagnostics)
{
    Json result = Json::array();
    const auto count = std::min(diagnostics.size(), MAX_WEB_DIAGNOSTICS);
    for (size_t index = 0U; index < count; ++index) {
        result.push_back(BoundedMetadataText(diagnostics[index], MetadataLimits::MAX_PREVIEW_BYTES));
    }
    return result;
}

BoundedJsonArray MetadataEntityIdsJson(const std::vector<EntityId> &ids, size_t cap)
{
    BoundedJsonArray result;
    const auto count = std::min(ids.size(), cap);
    for (size_t index = 0U; index < count; ++index) {
        result.values.push_back(std::to_string(ids[index]));
    }
    result.truncated = ids.size() > count;
    result.total = ids.size();
    return result;
}

BoundedJsonArray MetadataObjectDiagnosticsJson(const std::vector<std::string> &diagnostics, MetadataJsonBudget *budget)
{
    BoundedJsonArray result;
    for (const auto &diagnostic : diagnostics) {
        if (budget->diagnostics == 0U || budget->diagnosticBytes == 0U) {
            result.truncated = true;
            break;
        }
        const auto limit = std::min(MetadataLimits::MAX_PREVIEW_BYTES, budget->diagnosticBytes);
        auto bounded = BoundedMetadataText(diagnostic, limit);
        result.truncated = result.truncated || bounded.size() != diagnostic.size();
        budget->diagnosticBytes -= bounded.size();
        --budget->diagnostics;
        result.values.push_back(std::move(bounded));
    }
    return result;
}

Json MetadataObjectJsonValue(const MetadataObject &object, MetadataJsonBudget *budget, size_t entityIdsCap)
{
    const auto *kind = MetadataKindName(object.key.kind);
    Json origins = Json::array();
    for (const auto origin : object.origins) {
        origins.push_back(MetadataOriginName(origin));
    }
    const auto preview = BoundedMetadataText(object.preview, MetadataLimits::MAX_PREVIEW_BYTES);
    auto entityIds = MetadataEntityIdsJson(object.entityIds, entityIdsCap);
    auto diagnostics = MetadataObjectDiagnosticsJson(object.diagnostics, budget);
    Json result = {{"key", {{"kind", kind == nullptr ? "Unknown" : kind}, {"offset", Hex(object.key.offset)}}},
                   {"kind", kind == nullptr ? "Unknown" : kind},
                   {"preview", preview},
                   {"truncated", object.truncated || preview.size() != object.preview.size()},
                   {"origins", std::move(origins)},
                   {"entityIds", std::move(entityIds.values)},
                   {"entityIdsTruncated", entityIds.truncated},
                   {"entityIdsTotal", entityIds.total},
                   {"referenceCount", object.referenceCount},
                   {"diagnostics", std::move(diagnostics.values)},
                   {"diagnosticsTruncated", diagnostics.truncated}};
    result["range"] = object.range.has_value() ? RangeJson(*object.range) : Json(nullptr);
    return result;
}

Json MetadataReferenceJsonValue(const MetadataReference &reference)
{
    auto sourceEntityIds = MetadataEntityIdsJson(reference.sourceEntityIds, MAX_WEB_ROW_ENTITY_IDS);
    Json result = {{"kind", MetadataReferenceKindName(reference.kind)},
                   {"sourceEntityIds", std::move(sourceEntityIds.values)},
                   {"sourceEntityIdsTruncated", sourceEntityIds.truncated},
                   {"sourceEntityIdsTotal", sourceEntityIds.total},
                   {"sourceName", BoundedMetadataText(reference.sourceName, MetadataLimits::MAX_PREVIEW_BYTES)},
                   {"mnemonic", BoundedMetadataText(reference.mnemonic, 128U)}};
    result["sourceMethodOffset"] =
        reference.sourceMethodOffset.has_value() ? Json(Hex(*reference.sourceMethodOffset)) : Json(nullptr);
    result["pc"] = reference.pc.has_value() ? Json(*reference.pc) : Json(nullptr);
    result["sourceRange"] = reference.sourceRange.has_value() ? RangeJson(*reference.sourceRange) : Json(nullptr);
    result["operandIndex"] = reference.operandIndex.has_value() ? Json(*reference.operandIndex) : Json(nullptr);
    return result;
}

Status InvalidMetadataKind()
{
    return Status(ErrorCode::INVALID_ARGUMENT, "Metadata kind is invalid");
}

const char *GroupName(EntityGroup group)
{
    switch (group) {
        case EntityGroup::NONE:
            return "None";
        case EntityGroup::IMPORTS:
            return "Imports";
        case EntityGroup::EXPORTS:
            return "Exports";
        case EntityGroup::COMPILER_GENERATED:
            return "Compiler Generated";
        case EntityGroup::ABC_INTERNALS:
            return "ABC Internals";
    }
    return "Unknown";
}

const char *SeverityName(DiagnosticSeverity severity)
{
    switch (severity) {
        case DiagnosticSeverity::INFO:
            return "Info";
        case DiagnosticSeverity::WARNING:
            return "Warning";
        case DiagnosticSeverity::ERROR:
            return "Error";
    }
    return "Unknown";
}

const char *ImmediateKindName(ImmediateKind kind)
{
    switch (kind) {
        case ImmediateKind::SIGNED:
            return "Signed";
        case ImmediateKind::UNSIGNED:
            return "Unsigned";
        case ImmediateKind::FLOAT:
            return "Float";
    }
    return "Unknown";
}

const char *IdKindName(IdOperandKind kind)
{
    switch (kind) {
        case IdOperandKind::STRING:
            return "String";
        case IdOperandKind::METHOD:
            return "Method";
        case IdOperandKind::LITERAL_ARRAY:
            return "Literal Array";
        case IdOperandKind::UNKNOWN:
            return "Unknown";
    }
    return "Unknown";
}

const char *IdModeName(IdResolutionMode mode)
{
    switch (mode) {
        case IdResolutionMode::INDEX:
            return "Index";
        case IdResolutionMode::DIRECT_OFFSET:
            return "Direct Offset";
    }
    return "Unknown";
}

const char *ControlFlowKindName(ControlFlowKind kind)
{
    switch (kind) {
        case ControlFlowKind::NORMAL:
            return "Normal";
        case ControlFlowKind::CONDITIONAL_JUMP:
            return "Conditional Jump";
        case ControlFlowKind::UNCONDITIONAL_JUMP:
            return "Unconditional Jump";
        case ControlFlowKind::RETURN:
            return "Return";
        case ControlFlowKind::THROW:
            return "Throw";
    }
    return "Unknown";
}

const char *CfgEdgeKindName(CfgEdgeKind kind)
{
    switch (kind) {
        case CfgEdgeKind::FALLTHROUGH:
            return "Fallthrough";
        case CfgEdgeKind::TRUE_BRANCH:
            return "True";
        case CfgEdgeKind::FALSE_BRANCH:
            return "False";
        case CfgEdgeKind::JUMP:
            return "Jump";
        case CfgEdgeKind::EXCEPTION:
            return "Exception";
        case CfgEdgeKind::RETURN:
            return "Return";
        case CfgEdgeKind::THROW:
            return "Throw";
    }
    return "Unknown";
}

std::string ModelText(const ModelString &text)
{
    return std::string(text.data(), text.size());
}

std::optional<BasicBlockId> ParseBasicBlockId(const Json &value)
{
    if (!value.is_string()) {
        return std::nullopt;
    }
    const auto &text = value.get_ref<const std::string &>();
    if (text.empty()) {
        return std::nullopt;
    }
    uint64_t parsed = 0U;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (result.ec != std::errc {} || result.ptr != text.data() + text.size() || parsed >= INVALID_BASIC_BLOCK_ID) {
        return std::nullopt;
    }
    return static_cast<BasicBlockId>(parsed);
}

template <class Container>
Json BlockIdsJson(const Container &ids)
{
    Json result = Json::array();
    for (const auto id : ids) {
        result.push_back(std::to_string(id));
    }
    return result;
}

Json ImmediateJson(const ImmediateValue &immediate)
{
    Json result = {{"kind", ImmediateKindName(immediate.kind)},
                   {"bitWidth", immediate.bitWidth},
                   {"rawBits", Hex(immediate.rawBits)}};
    if (std::holds_alternative<int64_t>(immediate.value)) {
        result["value"] = std::to_string(std::get<int64_t>(immediate.value));
    } else if (std::holds_alternative<uint64_t>(immediate.value)) {
        result["value"] = std::to_string(std::get<uint64_t>(immediate.value));
    } else {
        result["value"] = std::get<double>(immediate.value);
    }
    return result;
}

Json IdJson(const IdOperand &id)
{
    Json result = {{"kind", IdKindName(id.kind)},
                   {"mode", IdModeName(id.resolutionMode)},
                   {"rawValue", id.rawValue},
                   {"rawHex", Hex(id.rawValue)},
                   {"text", ModelText(id.resolvedText)}};
    result["resolvedOffset"] = id.resolvedOffset.has_value() ? Json(Hex(*id.resolvedOffset)) : Json(nullptr);
    return result;
}

Json InstructionJson(const InstructionInfo &instruction)
{
    std::ostringstream byteText;
    for (size_t index = 0U; index < instruction.bytes.size(); ++index) {
        if (index != 0U) {
            byteText << ' ';
        }
        byteText << std::uppercase << std::hex << std::setfill('0') << std::setw(HEX_BYTE_WIDTH)
                 << static_cast<uint32_t>(instruction.bytes[index]);
    }

    Json registers = Json::array();
    for (const auto value : instruction.registers) {
        registers.push_back(value);
    }
    Json immediates = Json::array();
    for (const auto &value : instruction.immediates) {
        immediates.push_back(ImmediateJson(value));
    }
    Json ids = Json::array();
    for (const auto &value : instruction.ids) {
        ids.push_back(IdJson(value));
    }

    Json result = {{"pc", instruction.pc},
                   {"pcHex", Hex(instruction.pc, 4U)},
                   {"byteSize", instruction.byteSize},
                   {"opcode", instruction.numericOpcode},
                   {"mnemonic", ModelText(instruction.mnemonic)},
                   {"operands", ModelText(instruction.operands)},
                   {"bytes", byteText.str()},
                   {"registers", std::move(registers)},
                   {"immediates", std::move(immediates)},
                   {"ids", std::move(ids)},
                   {"fileRange", RangeJson(instruction.fileRange)},
                   {"flowKind", ControlFlowKindName(instruction.flowKind)},
                   {"canThrow", instruction.canThrow},
                   {"terminator", instruction.isTerminator}};
    result["jumpTarget"] = instruction.jumpTarget.has_value() ? Json(*instruction.jumpTarget) : Json(nullptr);
    return result;
}

Json NodeJson(const EntityNode &node)
{
    Json children = Json::array();
    for (const auto id : node.children) {
        children.push_back(std::to_string(id));
    }
    Json backing = Json::array();
    for (const auto &reference : node.backing) {
        backing.push_back({{"kind", PhysicalEntityKindName(reference.kind)},
                           {"name", reference.name},
                           {"range", RangeJson(reference.range)}});
    }
    Json diagnostics = Json::array();
    for (const auto &diagnostic : node.diagnostics) {
        Json item = {{"severity", SeverityName(diagnostic.severity)}, {"message", diagnostic.message}};
        item["range"] = diagnostic.range.has_value() ? RangeJson(*diagnostic.range) : Json(nullptr);
        diagnostics.push_back(std::move(item));
    }
    const auto targets = ExecutableTargetResolver::Resolve(node);
    Json result = {{"id", std::to_string(node.id)},
                   {"kind", EntityKindName(node.kind)},
                   {"role", EntityRoleName(node.role)},
                   {"group", GroupName(node.group)},
                   {"origin", EntityOriginName(node.origin)},
                   {"name", node.VisibleName()},
                   {"semanticName", node.name},
                   {"internalName", node.internalName},
                   {"isStatic", node.isStatic},
                   {"defaultCollapsed", node.defaultCollapsed},
                   {"executable", !targets.empty()},
                   {"executableTargetCount", targets.size()},
                   {"children", std::move(children)},
                   {"backing", std::move(backing)},
                   {"diagnostics", std::move(diagnostics)}};
    result["parentId"] = node.parentId == INVALID_ENTITY_ID ? Json(nullptr) : Json(std::to_string(node.parentId));
    result["primaryRange"] = node.primaryRange.has_value() ? RangeJson(*node.primaryRange) : Json(nullptr);
    return result;
}

std::string Dump(Json json)
{
    return json.dump(-1, ' ', false, Json::error_handler_t::replace);
}

Status DisassemblyError(const DisassemblyResult &result)
{
    ErrorCode code = ErrorCode::BACKEND_ERROR;
    switch (result.status) {
        case DisassemblyStatus::INVALID_METHOD:
        case DisassemblyStatus::NO_CODE:
            code = ErrorCode::INVALID_ARGUMENT;
            break;
        case DisassemblyStatus::MALFORMED_BYTECODE:
            code = ErrorCode::INVALID_FORMAT;
            break;
        case DisassemblyStatus::CANCELLED:
            code = ErrorCode::CANCELLED;
            break;
        case DisassemblyStatus::RESOURCE_LIMIT_EXCEEDED:
        case DisassemblyStatus::SUCCESS:
            break;
    }
    return Status(code, result.diagnostic.empty() ? "Cannot disassemble the selected entity" : result.diagnostic);
}

struct DecodedSelection {
    const EntityNode *node {nullptr};
    ExecutableTarget target;
    FunctionDisassemblyPtr disassembly;
};

struct ResolvedSelection {
    const EntityNode *node {nullptr};
    ExecutableTarget target;
};

struct CfgArtifacts {
    CfgAnalysisPtr analysis;
    CfgLayoutPtr layout;
};

Result<ResolvedSelection> ResolveSelection(const LoadedDocument &document, EntityId entityId,
                                           std::optional<uint64_t> methodOffset)
{
    const auto *node = document.structure->Find(entityId);
    if (node == nullptr) {
        return Result<ResolvedSelection>::Failure(
            Status(ErrorCode::INVALID_ARGUMENT, "Unknown structure entity ID: " + std::to_string(entityId)));
    }
    auto targets = ExecutableTargetResolver::Resolve(*node);
    if (targets.empty()) {
        return Result<ResolvedSelection>::Failure(
            Status(ErrorCode::INVALID_ARGUMENT, "The selected structure entity is not executable"));
    }
    auto selected = targets.begin();
    if (methodOffset.has_value()) {
        selected = std::find_if(targets.begin(), targets.end(),
                                [methodOffset](const auto &target) { return target.methodOffset == *methodOffset; });
        if (selected == targets.end()) {
            return Result<ResolvedSelection>::Failure(
                Status(ErrorCode::INVALID_ARGUMENT, "The requested method is not owned by the selected entity"));
        }
    } else if (targets.size() != 1U) {
        return Result<ResolvedSelection>::Failure(Status(
            ErrorCode::INVALID_ARGUMENT, "The selected entity has multiple executable targets; choose a method"));
    }

    return Result<ResolvedSelection>::Success({node, *selected});
}

Result<DecodedSelection> DecodeSelection(const LoadedDocument &document, const ResolvedSelection &selection)
{
    auto decoded = FunctionDisassembler().Disassemble(document.document->File(), *document.methods, selection.target);
    if (decoded.status != DisassemblyStatus::SUCCESS || decoded.disassembly == nullptr) {
        return Result<DecodedSelection>::Failure(DisassemblyError(decoded));
    }
    return Result<DecodedSelection>::Success({selection.node, selection.target, std::move(decoded.disassembly)});
}

Result<DecodedSelection> DecodeSelection(const LoadedDocument &document, EntityId entityId,
                                         std::optional<uint64_t> methodOffset)
{
    const auto selected = ResolveSelection(document, entityId, methodOffset);
    if (!selected.HasValue()) {
        return Result<DecodedSelection>::Failure(selected.Error());
    }
    return DecodeSelection(document, selected.Value());
}

Status CfgError(const CfgBuildResult &result)
{
    ErrorCode code = ErrorCode::BACKEND_ERROR;
    if (result.status == CfgStatus::CANCELLED) {
        code = ErrorCode::CANCELLED;
    } else if (result.status == CfgStatus::INVALID_DISASSEMBLY) {
        code = ErrorCode::INVALID_FORMAT;
    }
    return Status(code, result.diagnostic.empty() ? "Cannot build the control-flow graph" : result.diagnostic);
}

Status CfgAnalysisError(const CfgAnalysisResult &result)
{
    const auto code = result.status == CfgAnalysisStatus::CANCELLED ? ErrorCode::CANCELLED : ErrorCode::BACKEND_ERROR;
    return Status(code, result.diagnostic.empty() ? "Cannot analyze the control-flow graph" : result.diagnostic);
}

Status CfgLayoutError(const CfgLayoutResult &result)
{
    ErrorCode code = ErrorCode::BACKEND_ERROR;
    if (result.status == CfgLayoutStatus::CANCELLED) {
        code = ErrorCode::CANCELLED;
    } else if (result.status == CfgLayoutStatus::INVALID_REQUEST) {
        code = ErrorCode::INVALID_ARGUMENT;
    }
    return Status(code, result.diagnostic.empty() ? "Cannot lay out the control-flow graph" : result.diagnostic);
}

Result<CfgAnalysisPtr> BuildCfgAnalysis(const DecodedSelection &decoded)
{
    const auto built = ControlFlowGraphBuilder {}.Build(decoded.disassembly);
    if (built.status != CfgStatus::SUCCESS || built.graph == nullptr) {
        return Result<CfgAnalysisPtr>::Failure(CfgError(built));
    }
    const auto analyzed = CfgAnalyzer {}.Analyze(built.graph);
    if (analyzed.status != CfgAnalysisStatus::SUCCESS || analyzed.analysis == nullptr) {
        return Result<CfgAnalysisPtr>::Failure(CfgAnalysisError(analyzed));
    }
    return Result<CfgAnalysisPtr>::Success(std::move(analyzed.analysis));
}

Result<CfgArtifacts> LayoutCfg(CfgAnalysisPtr analysis, const CfgLayoutRequest &request)
{
    const auto laidOut = CfgLayoutEngine {}.Layout(analysis, request);
    if (laidOut.status != CfgLayoutStatus::SUCCESS || laidOut.layout == nullptr) {
        return Result<CfgArtifacts>::Failure(CfgLayoutError(laidOut));
    }
    return Result<CfgArtifacts>::Success({std::move(analysis), std::move(laidOut.layout)});
}

uint64_t CfgAnalysisBytes(const CfgAnalysis &analysis)
{
    uint64_t bytes = 0U;
    const auto add = [&bytes](const BoundedMemoryResource *resource) {
        if (resource != nullptr && bytes <= std::numeric_limits<uint64_t>::max() - resource->LiveBytes()) {
            bytes += resource->LiveBytes();
        } else if (resource != nullptr) {
            bytes = std::numeric_limits<uint64_t>::max();
        }
    };
    add(analysis.OwnedResource());
    add(analysis.graph->OwnedResource());
    add(analysis.graph->disassembly->OwnedResource());
    return bytes;
}

// Attempts to serve the CFG analysis from the cache without building it.
std::optional<Result<CfgAnalysisPtr>> LookupCfgAnalysis(WebCfgCache &cache, uint64_t methodOffset)
{
    std::lock_guard guard(cache.mutex);
    if (cache.activeMethodOffset == methodOffset && cache.activeAnalysis != nullptr) {
        return Result<CfgAnalysisPtr>::Success(cache.activeAnalysis);
    }
    const auto found = cache.entries.find(methodOffset);
    if (found == cache.entries.end()) {
        return std::nullopt;
    }
    cache.lru.splice(cache.lru.begin(), cache.lru, found->second.lru);
    cache.activeMethodOffset = methodOffset;
    cache.activeAnalysis = found->second.analysis;
    return Result<CfgAnalysisPtr>::Success(found->second.analysis);
}

// Stores a freshly built CFG analysis, evicting LRU entries to honor the entry and byte limits.
Result<CfgAnalysisPtr> StoreCfgAnalysis(WebCfgCache &cache, uint64_t methodOffset, Result<CfgAnalysisPtr> built,
                                        uint64_t bytes)
{
    std::lock_guard guard(cache.mutex);
    ++cache.buildCount;
    const auto existing = cache.entries.find(methodOffset);
    if (existing != cache.entries.end()) {
        cache.lru.splice(cache.lru.begin(), cache.lru, existing->second.lru);
        cache.activeMethodOffset = methodOffset;
        cache.activeAnalysis = existing->second.analysis;
        return Result<CfgAnalysisPtr>::Success(existing->second.analysis);
    }
    if (bytes > WebCfgCache::MAX_BYTES) {
        return Result<CfgAnalysisPtr>::Failure(
            Status(ErrorCode::BACKEND_ERROR, "CFG analysis exceeds the interactive web cache byte limit"));
    }
    // Keep the graph most recently returned to the single-page client alive even when it is
    // evicted from the multi-entry LRU. A following layout POST must only re-layout it.
    cache.activeMethodOffset = methodOffset;
    cache.activeAnalysis = built.Value();
    while (!cache.lru.empty() &&
           (cache.entries.size() >= WebCfgCache::MAX_ENTRIES || cache.liveBytes > WebCfgCache::MAX_BYTES - bytes)) {
        const auto victim = cache.lru.back();
        const auto found = cache.entries.find(victim);
        cache.liveBytes -= found->second.bytes;
        cache.entries.erase(found);
        cache.lru.pop_back();
    }
    cache.lru.push_front(methodOffset);
    cache.entries.emplace(methodOffset, WebCfgCache::Entry {built.Value(), bytes, cache.lru.begin()});
    cache.liveBytes += bytes;
    return built;
}

Result<CfgAnalysisPtr> CachedCfgAnalysis(const LoadedDocument &document, WebCfgCache &cache,
                                         const ResolvedSelection &selection)
{
    const auto methodOffset = selection.target.methodOffset;
    const auto cached = LookupCfgAnalysis(cache, methodOffset);
    if (cached.has_value()) {
        return *cached;
    }
    auto decoded = DecodeSelection(document, selection);
    if (!decoded.HasValue()) {
        return Result<CfgAnalysisPtr>::Failure(decoded.Error());
    }
    auto built = BuildCfgAnalysis(decoded.Value());
    if (!built.HasValue()) {
        return built;
    }
    const auto bytes = CfgAnalysisBytes(*built.Value());
    return StoreCfgAnalysis(cache, methodOffset, std::move(built), bytes);
}

// Parses one node geometry entry of a CFG layout request; returns the failure diagnostic when invalid.
std::optional<std::string> ParseCfgNodeGeometry(const Json &item, CfgNodeGeometry *node)
{
    const auto block = item.is_object() && item.contains("block") ? ParseBasicBlockId(item.at("block")) : std::nullopt;
    if (!block.has_value() || !item.contains("width") || !item.at("width").is_number() || !item.contains("height") ||
        !item.at("height").is_number()) {
        return "CFG layout node geometry has invalid required fields";
    }
    node->block = *block;
    node->width = item.at("width").get<double>();
    node->height = item.at("height").get<double>();
    if (item.contains("pinned") && !item.at("pinned").is_null()) {
        const auto &pinned = item.at("pinned");
        if (!pinned.is_object() || !pinned.contains("x") || !pinned.at("x").is_number() || !pinned.contains("y") ||
            !pinned.at("y").is_number()) {
            return "CFG pinned position must contain numeric x and y values";
        }
        node->pinnedPosition = CfgPoint {pinned.at("x").get<double>(), pinned.at("y").get<double>()};
    }
    return std::nullopt;
}

Result<CfgLayoutRequest> ParseCfgLayoutRequest(std::string_view body)
{
    if (body.empty()) {
        return Result<CfgLayoutRequest>::Failure(
            Status(ErrorCode::INVALID_ARGUMENT, "CFG layout request body must be a JSON object"));
    }
    const auto json = Json::parse(body.begin(), body.end(), nullptr, false);
    if (json.is_discarded() || !json.is_object()) {
        return Result<CfgLayoutRequest>::Failure(
            Status(ErrorCode::INVALID_ARGUMENT, "CFG layout request body must be a valid JSON object"));
    }
    CfgLayoutRequest request;
    if (json.contains("revision")) {
        if (!json.at("revision").is_number_unsigned()) {
            return Result<CfgLayoutRequest>::Failure(
                Status(ErrorCode::INVALID_ARGUMENT, "CFG layout revision must be an unsigned integer"));
        }
        request.revision = json.at("revision").get<uint64_t>();
    }
    if (!json.contains("nodes") || !json.at("nodes").is_array() ||
        json.at("nodes").size() > CfgLayoutLimits::DEFAULT_MAX_NODE_COUNT) {
        return Result<CfgLayoutRequest>::Failure(
            Status(ErrorCode::INVALID_ARGUMENT, "CFG layout nodes must be one bounded JSON array"));
    }
    request.nodes.reserve(json.at("nodes").size());
    for (const auto &item : json.at("nodes")) {
        CfgNodeGeometry node;
        const auto error = ParseCfgNodeGeometry(item, &node);
        if (error.has_value()) {
            return Result<CfgLayoutRequest>::Failure(Status(ErrorCode::INVALID_ARGUMENT, *error));
        }
        request.nodes.push_back(std::move(node));
    }
    return Result<CfgLayoutRequest>::Success(std::move(request));
}

Json TargetJson(const ExecutableTarget &target)
{
    Json result = {{"methodOffset", Hex(target.methodOffset)}, {"name", target.methodName}};
    result["codeRange"] = target.codeRange.has_value() ? RangeJson(*target.codeRange) : Json(nullptr);
    return result;
}

Json FunctionJson(const DecodedSelection &decoded)
{
    return {{"name", decoded.node->VisibleName()},
            {"role", EntityRoleName(decoded.node->role)},
            {"methodOffset", Hex(decoded.disassembly->methodOffset)},
            {"codeOffset", Hex(decoded.disassembly->codeOffset)},
            {"instructionsOffset", Hex(decoded.disassembly->instructionsOffset)},
            {"codeSize", decoded.disassembly->codeSize},
            {"vregCount", decoded.disassembly->vregCount},
            {"argumentCount", decoded.disassembly->argumentCount},
            {"instructionCount", decoded.disassembly->instructions.size()}};
}

// Serializes one CFG block with its instructions and analysis attributes.
Json GraphBlockJson(const CfgAnalysis &analysis, const BasicBlock &block)
{
    const auto &info = analysis.blockInfo[block.id];
    const auto &graph = *analysis.graph;
    Json instructions = Json::array();
    for (uint32_t index = 0U; index < block.instructionCount; ++index) {
        instructions.push_back(
            InstructionJson(graph.disassembly->instructions[static_cast<size_t>(block.firstInstructionIndex) + index]));
    }
    Json item = {{"id", std::to_string(block.id)},
                 {"startPc", block.startPc},
                 {"startPcHex", Hex(block.startPc, 4U)},
                 {"endPc", block.endPc},
                 {"endPcHex", Hex(block.endPc, 4U)},
                 {"firstInstructionIndex", block.firstInstructionIndex},
                 {"instructionCount", block.instructionCount},
                 {"entry", block.isEntry},
                 {"handler", block.isHandler},
                 {"hasThrowingInstruction", block.hasThrowingInstruction},
                 {"reachable", info.reachable},
                 {"dominatorDepth", info.dominatorDepth},
                 {"loopDepth", info.loopDepth},
                 {"loopHeader", info.isLoopHeader},
                 {"irreducible", info.isIrreducible},
                 {"instructions", std::move(instructions)}};
    item["immediateDominator"] =
        info.immediateDominator.has_value() ? Json(std::to_string(*info.immediateDominator)) : Json(nullptr);
    item["scc"] =
        info.stronglyConnectedComponent != INVALID_SCC_ID ? Json(info.stronglyConnectedComponent) : Json(nullptr);
    return item;
}

// Serializes one CFG edge with its optional target and handler indices.
Json GraphEdgeJson(const ControlFlowEdge &edge)
{
    Json item = {
        {"id", std::to_string(edge.id)}, {"source", std::to_string(edge.source)}, {"kind", CfgEdgeKindName(edge.kind)}};
    item["target"] = edge.target.has_value() ? Json(std::to_string(*edge.target)) : Json(nullptr);
    item["tryRegion"] = edge.tryRegionIndex.has_value() ? Json(*edge.tryRegionIndex) : Json(nullptr);
    item["handler"] = edge.handlerIndex.has_value() ? Json(*edge.handlerIndex) : Json(nullptr);
    item["catchTypeIndex"] = edge.catchTypeIndex.has_value() ? Json(*edge.catchTypeIndex) : Json(nullptr);
    return item;
}

Json GraphJson(const CfgAnalysis &analysis)
{
    const auto &graph = *analysis.graph;
    Json blocks = Json::array();
    for (const auto &block : graph.blocks) {
        blocks.push_back(GraphBlockJson(analysis, block));
    }

    Json edges = Json::array();
    for (const auto &edge : graph.edges) {
        edges.push_back(GraphEdgeJson(edge));
    }

    Json components = Json::array();
    for (const auto &component : analysis.stronglyConnectedComponents) {
        components.push_back({{"id", component.id},
                              {"cyclic", component.cyclic},
                              {"irreducible", component.irreducible},
                              {"members", BlockIdsJson(component.members)},
                              {"entryBlocks", BlockIdsJson(component.entryBlocks)}});
    }
    Json loops = Json::array();
    for (const auto &loop : analysis.naturalLoops) {
        loops.push_back({{"id", loop.id},
                         {"header", std::to_string(loop.header)},
                         {"backEdgeSources", BlockIdsJson(loop.backEdgeSources)},
                         {"members", BlockIdsJson(loop.members)}});
    }
    return {{"entryBlock", std::to_string(graph.entryBlock)},
            {"blocks", std::move(blocks)},
            {"edges", std::move(edges)},
            {"stronglyConnectedComponents", std::move(components)},
            {"naturalLoops", std::move(loops)}};
}

Json LayoutJson(const CfgLayout &layout)
{
    Json nodes = Json::array();
    for (const auto &node : layout.nodes) {
        nodes.push_back({{"block", std::to_string(node.block)},
                         {"x", node.x},
                         {"y", node.y},
                         {"width", node.width},
                         {"height", node.height},
                         {"rank", node.rank},
                         {"pinned", node.pinned}});
    }
    Json edges = Json::array();
    for (const auto &edge : layout.edges) {
        Json points = Json::array();
        for (const auto &point : edge.points) {
            points.push_back({{"x", point.x}, {"y", point.y}});
        }
        Json item = {{"edge", std::to_string(edge.edge)},
                     {"kind", CfgEdgeKindName(edge.kind)},
                     {"backEdge", edge.isBackEdge},
                     {"exceptionEdge", edge.isExceptionEdge},
                     {"points", std::move(points)}};
        item["outerLane"] = edge.outerLane.has_value() ? Json(*edge.outerLane) : Json(nullptr);
        edges.push_back(std::move(item));
    }
    return {{"revision", layout.revision},
            {"canvas", {{"width", layout.canvasWidth}, {"height", layout.canvasHeight}}},
            {"nodes", std::move(nodes)},
            {"edges", std::move(edges)}};
}

Json CfgEnvelope(const std::string &documentId, EntityId entityId, const DecodedSelection &decoded,
                 const CfgArtifacts &artifacts, bool includeGraph)
{
    Json result = {{"apiVersion", 1},
                   {"documentId", documentId},
                   {"entityId", std::to_string(entityId)},
                   {"target", TargetJson(decoded.target)},
                   {"function", FunctionJson(decoded)},
                   {"layout", LayoutJson(*artifacts.layout)}};
    if (includeGraph) {
        result["graph"] = GraphJson(*artifacts.analysis);
    }
    return result;
}

}  // namespace

WebApi::WebApi(LoadedDocument document)
    : documentId_(DocumentId(document.summary)),
      document_(std::move(document)),
      cfgCache_(std::make_shared<WebCfgCache>()),
      structureCache_(std::make_shared<WebStructureCache>())
{
    metadataIndex_ = MetadataIndexService::Create(document_.document, document_.methods, document_.metadata);
    statistics_ = CreateStatistics(document_);
}

Result<std::string> WebApi::DocumentJson() const
{
    Json version = Json::array();
    for (const auto byte : document_.summary.version) {
        version.push_back(byte);
    }
    const auto buildMode = [mode = document_.summary.build.mode] {
        switch (mode) {
            case AbcBuildMode::DEBUG:
                return "debug";
            case AbcBuildMode::RELEASE:
                return "release";
            case AbcBuildMode::UNKNOWN:
                break;
        }
        return "unknown";
    }();
    const auto &buildEvidence = document_.summary.build.evidence;
    Json evidenceJson {{"debugOnlyInstructions", buildEvidence.debugOnlyInstructions},
                       {"variableTableEntries", buildEvidence.variableTableEntries},
                       {"sourceCodeMarkers", buildEvidence.sourceCodeMarkers},
                       {"scannedMethods", buildEvidence.scannedMethods},
                       {"scanComplete", buildEvidence.scanComplete}};
    return Result<std::string>::Success(Dump({{"apiVersion", 1},
                                              {"documentId", documentId_},
                                              {"fileName", FileName(document_.summary.path)},
                                              {"fileSize", document_.summary.fileSize},
                                              {"checksum", Hex(document_.summary.checksum, 8U)},
                                              {"version", Version(document_.summary.version)},
                                              {"versionBytes", std::move(version)},
                                              {"buildMode", buildMode},
                                              {"buildEvidence", std::move(evidenceJson)}}));
}

Result<std::string> WebApi::StructureJson(const CancellationToken &cancellation) const
{
    const Status cancelled(ErrorCode::CANCELLED, "Structure serialization cancelled");
    if (cancellation.IsCancellationRequested()) {
        return Result<std::string>::Failure(cancelled);
    }
    std::lock_guard<std::mutex> lock(structureCache_->mutex);
    if (cancellation.IsCancellationRequested()) {
        return Result<std::string>::Failure(cancelled);
    }
    if (structureCache_->json.has_value()) {
        return Result<std::string>::Success(*structureCache_->json);
    }

    // Keep only one node's JSON tree alive at a time; the complete tree can be
    // substantially larger than the serialized payload for large ABC files.
    std::string output = "{\"apiVersion\":1,\"documentId\":" + Dump(documentId_) + ",\"nodes\":[";
    bool first = true;
    for (const auto *node : document_.structure->NodesInIdOrder()) {
        if (cancellation.IsCancellationRequested()) {
            return Result<std::string>::Failure(cancelled);
        }
        if (!first) {
            output.push_back(',');
        }
        first = false;
        output += Dump(NodeJson(*node));
    }
    output += "],\"roots\":[";
    first = true;
    for (const auto id : document_.structure->RootIds()) {
        if (cancellation.IsCancellationRequested()) {
            return Result<std::string>::Failure(cancelled);
        }
        if (!first) {
            output.push_back(',');
        }
        first = false;
        output += Dump(std::to_string(id));
    }
    output += "]}";
    if (cancellation.IsCancellationRequested()) {
        return Result<std::string>::Failure(cancelled);
    }
    structureCache_->json = std::move(output);
    return Result<std::string>::Success(*structureCache_->json);
}

Result<std::string> WebApi::HexJson(uint64_t offset, uint64_t size) const
{
    auto window = HexWindowReader::Read(*document_.document, offset, size);
    if (!window.HasValue()) {
        return Result<std::string>::Failure(window.Error());
    }
    return Result<std::string>::Success(Dump({{"apiVersion", 1},
                                              {"documentId", documentId_},
                                              {"fileSize", Hex(document_.summary.fileSize)},
                                              {"window",
                                               {{"offset", Hex(window.Value().range.offset)},
                                                {"size", window.Value().range.size},
                                                {"eof", window.Value().eof},
                                                {"bytes", window.Value().bytes}}}}));
}

Result<std::string> WebApi::LocationJson(uint64_t offset) const
{
    if (offset >= document_.summary.fileSize) {
        return Result<std::string>::Failure(
            Status(ErrorCode::INVALID_ARGUMENT, "Location offset is outside the ABC file"));
    }

    Json result = {{"apiVersion", 1}, {"documentId", documentId_}, {"offset", Hex(offset)}};
    const auto match = document_.locations->FindMostSpecific(offset);
    if (!match.has_value()) {
        result["structureMatch"] = nullptr;
        return Result<std::string>::Success(Dump(std::move(result)));
    }

    Json structureMatch = {{"entityId", std::to_string(match->entityId)},
                           {"rangeKind", match->source == FileRangeSource::PRIMARY ? "primary" : "backing"},
                           {"range", RangeJson(match->range)}};
    structureMatch["backingIndex"] = match->backingIndex.has_value() ? Json(*match->backingIndex) : Json(nullptr);
    result["structureMatch"] = std::move(structureMatch);
    return Result<std::string>::Success(Dump(std::move(result)));
}

Result<std::string> WebApi::DisassemblyJson(EntityId entityId, std::optional<uint64_t> methodOffset) const
{
    auto decoded = DecodeSelection(document_, entityId, methodOffset);
    if (!decoded.HasValue()) {
        return Result<std::string>::Failure(decoded.Error());
    }

    Json instructions = Json::array();
    for (const auto &instruction : decoded.Value().disassembly->instructions) {
        instructions.push_back(InstructionJson(instruction));
    }
    return Result<std::string>::Success(Dump({{"apiVersion", 1},
                                              {"documentId", documentId_},
                                              {"entityId", std::to_string(entityId)},
                                              {"target", TargetJson(decoded.Value().target)},
                                              {"function", FunctionJson(decoded.Value())},
                                              {"instructions", std::move(instructions)}}));
}

Result<std::string> WebApi::CfgJson(EntityId entityId, std::optional<uint64_t> methodOffset) const
{
    const auto selected = ResolveSelection(document_, entityId, methodOffset);
    if (!selected.HasValue()) {
        return Result<std::string>::Failure(selected.Error());
    }
    const auto analysis = CachedCfgAnalysis(document_, *cfgCache_, selected.Value());
    if (!analysis.HasValue()) {
        return Result<std::string>::Failure(analysis.Error());
    }
    const auto artifacts = LayoutCfg(analysis.Value(), {});
    if (!artifacts.HasValue()) {
        return Result<std::string>::Failure(artifacts.Error());
    }
    const DecodedSelection decoded {selected.Value().node, selected.Value().target,
                                    analysis.Value()->graph->disassembly};
    return Result<std::string>::Success(Dump(CfgEnvelope(documentId_, entityId, decoded, artifacts.Value(), true)));
}

Result<std::string> WebApi::CfgLayoutJson(EntityId entityId, std::string_view requestBody,
                                          std::optional<uint64_t> methodOffset) const
{
    auto request = ParseCfgLayoutRequest(requestBody);
    if (!request.HasValue()) {
        return Result<std::string>::Failure(request.Error());
    }
    const auto selected = ResolveSelection(document_, entityId, methodOffset);
    if (!selected.HasValue()) {
        return Result<std::string>::Failure(selected.Error());
    }
    const auto analysis = CachedCfgAnalysis(document_, *cfgCache_, selected.Value());
    if (!analysis.HasValue()) {
        return Result<std::string>::Failure(analysis.Error());
    }
    const auto artifacts = LayoutCfg(analysis.Value(), request.Value());
    if (!artifacts.HasValue()) {
        return Result<std::string>::Failure(artifacts.Error());
    }
    const DecodedSelection decoded {selected.Value().node, selected.Value().target,
                                    analysis.Value()->graph->disassembly};
    return Result<std::string>::Success(Dump(CfgEnvelope(documentId_, entityId, decoded, artifacts.Value(), false)));
}

Result<std::string> WebApi::MetadataStatusJson() const
{
    if (metadataIndex_ == nullptr) {
        return Result<std::string>::Failure(Status(ErrorCode::BACKEND_ERROR, "Metadata index service is unavailable"));
    }
    const auto status = metadataIndex_->Status();
    return Result<std::string>::Success(Dump({{"apiVersion", 1},
                                              {"documentId", documentId_},
                                              {"state", MetadataPhaseName(status.phase)},
                                              {"revision", std::to_string(status.revision)},
                                              {"scannedMethods", std::to_string(status.scannedMethods)},
                                              {"totalMethods", std::to_string(status.totalMethods)},
                                              {"objectCount", std::to_string(status.objectCount)},
                                              {"referenceCount", std::to_string(status.referenceCount)},
                                              {"diagnostics", MetadataDiagnosticsJson(status.diagnostics)},
                                              {"diagnosticsTruncated", status.diagnosticsTruncated}}));
}

Result<std::string> WebApi::StartMetadataIndexJson(std::string_view requestBody) const
{
    if (requestBody.empty()) {
        return Result<std::string>::Failure(
            Status(ErrorCode::INVALID_ARGUMENT, "Metadata index request body must be an empty JSON object"));
    }
    const auto json = Json::parse(requestBody.begin(), requestBody.end(), nullptr, false);
    if (json.is_discarded() || !json.is_object() || !json.empty()) {
        return Result<std::string>::Failure(
            Status(ErrorCode::INVALID_ARGUMENT, "Metadata index request body must be an empty JSON object"));
    }
    if (metadataIndex_ == nullptr) {
        return Result<std::string>::Failure(Status(ErrorCode::BACKEND_ERROR, "Metadata index service is unavailable"));
    }
    (void)metadataIndex_->Start();
    return MetadataStatusJson();
}

std::string WebApi::SerializeMetadataObjectsPage(const std::string &documentId,
                                                 const MetadataPage<MetadataObject> &page)
{
    MetadataJsonBudget budget;
    Json items = Json::array();
    for (const auto &object : page.items) {
        items.push_back(MetadataObjectJsonValue(object, &budget, MAX_WEB_ROW_ENTITY_IDS));
    }
    Json result = {{"apiVersion", 1},
                   {"documentId", documentId},
                   {"revision", std::to_string(page.revision)},
                   {"total", std::to_string(page.total)},
                   {"items", std::move(items)}};
    result["nextCursor"] = page.nextCursor.has_value() ? Json(*page.nextCursor) : Json(nullptr);
    return Dump(std::move(result));
}

std::string WebApi::SerializeMetadataObjectDetail(const std::string &documentId, const MetadataObjectDetail &detail)
{
    MetadataJsonBudget budget;
    auto result = MetadataObjectJsonValue(detail.object, &budget, MAX_WEB_OBJECT_ENTITY_IDS);
    result["apiVersion"] = 1;
    result["documentId"] = documentId;
    result["value"] = detail.value.has_value()
                          ? Json(BoundedMetadataText(*detail.value, MetadataLimits::MAX_DETAIL_BYTES))
                          : Json(nullptr);
    result["valueTruncated"] =
        detail.valueTruncated ||
        (detail.value.has_value() && result.at("value").get_ref<const std::string &>().size() != detail.value->size());
    return Dump(std::move(result));
}

std::string WebApi::SerializeMetadataReferencesPage(const std::string &documentId, MetadataObjectKey key,
                                                    const MetadataPage<MetadataReference> &page)
{
    Json items = Json::array();
    for (const auto &reference : page.items) {
        items.push_back(MetadataReferenceJsonValue(reference));
    }
    Json result = {{"apiVersion", 1},
                   {"documentId", documentId},
                   {"key", {{"kind", MetadataKindName(key.kind)}, {"offset", Hex(key.offset)}}},
                   {"revision", std::to_string(page.revision)},
                   {"total", std::to_string(page.total)},
                   {"items", std::move(items)}};
    result["nextCursor"] = page.nextCursor.has_value() ? Json(*page.nextCursor) : Json(nullptr);
    return Dump(std::move(result));
}

Result<std::string> WebApi::MetadataObjectsJson(const MetadataObjectQuery &query) const
{
    if (query.kind.has_value() && MetadataKindName(*query.kind) == nullptr) {
        return Result<std::string>::Failure(InvalidMetadataKind());
    }
    if (metadataIndex_ == nullptr) {
        return Result<std::string>::Failure(Status(ErrorCode::BACKEND_ERROR, "Metadata index service is unavailable"));
    }
    const auto page = metadataIndex_->QueryObjects(query);
    if (!page.HasValue()) {
        return Result<std::string>::Failure(page.Error());
    }
    return Result<std::string>::Success(SerializeMetadataObjectsPage(documentId_, page.Value()));
}

Result<std::string> WebApi::MetadataObjectJson(MetadataObjectKey key) const
{
    if (MetadataKindName(key.kind) == nullptr) {
        return Result<std::string>::Failure(InvalidMetadataKind());
    }
    if (metadataIndex_ == nullptr) {
        return Result<std::string>::Failure(Status(ErrorCode::BACKEND_ERROR, "Metadata index service is unavailable"));
    }
    const auto detail = metadataIndex_->QueryObject(key);
    if (!detail.HasValue()) {
        return Result<std::string>::Failure(detail.Error());
    }
    return Result<std::string>::Success(SerializeMetadataObjectDetail(documentId_, detail.Value()));
}

Result<std::string> WebApi::MetadataReferencesJson(MetadataObjectKey key, std::optional<std::string> cursor,
                                                   size_t limit) const
{
    if (MetadataKindName(key.kind) == nullptr) {
        return Result<std::string>::Failure(InvalidMetadataKind());
    }
    if (metadataIndex_ == nullptr) {
        return Result<std::string>::Failure(Status(ErrorCode::BACKEND_ERROR, "Metadata index service is unavailable"));
    }
    const auto page = metadataIndex_->QueryReferences(key, std::move(cursor), limit);
    if (!page.HasValue()) {
        return Result<std::string>::Failure(page.Error());
    }
    return Result<std::string>::Success(SerializeMetadataReferencesPage(documentId_, key, page.Value()));
}

size_t WebApi::CfgAnalysisBuildCountForTesting() const
{
    std::lock_guard guard(cfgCache_->mutex);
    return cfgCache_->buildCount;
}

void WebApi::WaitForMetadataIndexForTesting() const
{
    if (metadataIndex_ != nullptr) {
        metadataIndex_->WaitForIdleForTesting();
    }
}

}  // namespace panda::bytecode_explorer
