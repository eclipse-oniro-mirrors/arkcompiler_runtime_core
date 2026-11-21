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

#include <nlohmann/json.hpp>

namespace panda::bytecode_explorer {
namespace {

bool IsEmptyObject(std::string_view body)
{
    const auto parsed = nlohmann::json::parse(body, nullptr, false);
    return parsed.is_object() && parsed.empty();
}

}  // namespace

std::shared_ptr<ScopeStatistics> WebApi::CreateStatistics(const LoadedDocument &loaded)
{
    return std::make_shared<ScopeStatistics>(loaded.statistics, [document = loaded.document, methods = loaded.methods](
                                                                    uint64_t offset, const CancellationToken &token) {
        if (document == nullptr || methods == nullptr) {
            return StatisticsMeasurement {DisassemblyStatus::INVALID_METHOD, 0, 0, "Document is unavailable"};
        }
        const auto decoded = FunctionDisassembler().Disassemble(
            document->File(), *methods, ExecutableTarget {INVALID_ENTITY_ID, offset, {}, std::nullopt}, {}, token);
        StatisticsMeasurement result;
        result.status = decoded.status;
        result.diagnostic = decoded.diagnostic;
        if (decoded.status == DisassemblyStatus::SUCCESS && decoded.disassembly != nullptr) {
            result.codeBytes = decoded.disassembly->codeSize;
            result.instructionCount = decoded.disassembly->instructions.size();
        }
        return result;
    });
}

Result<std::string> WebApi::SerializeStatistics(const Result<StatisticsSnapshot> &result) const
{
    if (!result.HasValue()) {
        return Result<std::string>::Failure(result.Error());
    }
    using Json = nlohmann::json;
    const auto &value = result.Value();
    const bool complete = value.phase == StatisticsPhase::COMPLETE;
    Json json = {{"apiVersion", 1},
                 {"documentId", documentId_},
                 {"entityId", std::to_string(value.id)},
                 {"kind", EntityKindName(value.kind)},
                 {"phase", StatisticsPhaseName(value.phase)},
                 {"fileSize", value.fileSize ? Json(std::to_string(*value.fileSize)) : Json(nullptr)},
                 {"methodCount", std::to_string(value.methodCount)},
                 {"executableMethods", std::to_string(value.executableMethods)},
                 {"unassignedMethods", std::to_string(value.unassignedMethods)},
                 {"unassignedExecutables", std::to_string(value.unassignedExecutables)},
                 {"scannedMethods", std::to_string(value.scannedMethods)},
                 {"decodedMethods", std::to_string(value.decodedMethods)},
                 {"failedMethods", std::to_string(value.failedMethods)},
                 {"limitedMethods", std::to_string(value.limitedMethods)},
                 {"knownCodeBytes", std::to_string(value.codeBytes)},
                 {"knownInstructionCount", std::to_string(value.instructionCount)},
                 {"codeBytes", complete ? Json(std::to_string(value.codeBytes)) : Json(nullptr)},
                 {"instructionCount", complete ? Json(std::to_string(value.instructionCount)) : Json(nullptr)},
                 {"activeScopeId",
                  value.activeScopeId == INVALID_ENTITY_ID ? Json(nullptr) : Json(std::to_string(value.activeScopeId))},
                 {"diagnostic", value.diagnostic}};
    return Result<std::string>::Success(json.dump(-1, ' ', false, Json::error_handler_t::replace));
}

Result<std::string> WebApi::StatisticsJson(EntityId entityId) const
{
    return SerializeStatistics(statistics_->Snapshot(entityId));
}

Result<std::string> WebApi::StartStatisticsJson(EntityId entityId, std::string_view requestBody) const
{
    if (!IsEmptyObject(requestBody)) {
        return Result<std::string>::Failure(Status(ErrorCode::INVALID_ARGUMENT, "Statistics action requires {}"));
    }
    return SerializeStatistics(statistics_->Start(entityId));
}

Result<std::string> WebApi::CancelStatisticsJson(EntityId entityId, std::string_view requestBody) const
{
    if (!IsEmptyObject(requestBody)) {
        return Result<std::string>::Failure(Status(ErrorCode::INVALID_ARGUMENT, "Statistics action requires {}"));
    }
    return SerializeStatistics(statistics_->Cancel(entityId));
}

}  // namespace panda::bytecode_explorer
