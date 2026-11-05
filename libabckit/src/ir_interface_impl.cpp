/**
 * Copyright (c) 2024-2026 Huawei Device Co., Ltd.
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

#include "libabckit/src/ir_impl.h"
#include "libabckit/src/statuses_impl.h"
#include <cassert>

namespace {
std::string ReportMissingId(const char *idKind, uint32_t offset)
{
    LIBABCKIT_LOG(ERROR) << "Missing " << idKind << " ID for offset " << offset << '\n';
    libabckit::statuses::SetLastError(ABCKIT_STATUS_INTERNAL_ERROR);
    return {};
}
}  // namespace

AbckitIrInterface::AbckitIrInterface(const AbckitIrInterface *lookupSource) : lookupSource_(lookupSource)
{
    ASSERT(lookupSource_ != nullptr);
}

const AbckitIrInterface::IdMap &AbckitIrInterface::GetMethods() const
{
    return lookupSource_ == nullptr ? methods : lookupSource_->GetMethods();
}

const AbckitIrInterface::IdMap &AbckitIrInterface::GetFields() const
{
    return lookupSource_ == nullptr ? fields : lookupSource_->GetFields();
}

const AbckitIrInterface::IdMap &AbckitIrInterface::GetClasses() const
{
    return lookupSource_ == nullptr ? classes : lookupSource_->GetClasses();
}

const AbckitIrInterface::IdMap &AbckitIrInterface::GetStrings() const
{
    return lookupSource_ == nullptr ? strings : lookupSource_->GetStrings();
}

const AbckitIrInterface::IdMap &AbckitIrInterface::GetLiteralArrays() const
{
    return lookupSource_ == nullptr ? literalarrays : lookupSource_->GetLiteralArrays();
}

std::string AbckitIrInterface::GetMethodIdByOffset(uint32_t offset) const
{
    auto it = methods.find(offset);
    if (it != methods.cend()) {
        return std::string(it->second);
    }
    if (lookupSource_ != nullptr) {
        return lookupSource_->GetMethodIdByOffset(offset);
    }

    return ReportMissingId("method", offset);
}

std::string AbckitIrInterface::GetStringIdByOffset(uint32_t offset) const
{
    auto it = strings.find(offset);
    if (it != strings.cend()) {
        return std::string(it->second);
    }
    if (lookupSource_ != nullptr) {
        return lookupSource_->GetStringIdByOffset(offset);
    }

    return ReportMissingId("string", offset);
}

std::string AbckitIrInterface::GetLiteralArrayIdByOffset(uint32_t offset) const
{
    auto it = literalarrays.find(offset);
    if (it != literalarrays.cend()) {
        return std::string(it->second);
    }
    if (lookupSource_ != nullptr) {
        return lookupSource_->GetLiteralArrayIdByOffset(offset);
    }

    return ReportMissingId("literal array", offset);
}

std::string AbckitIrInterface::GetTypeIdByOffset(uint32_t offset) const
{
    auto it = classes.find(offset);
    if (it != classes.cend()) {
        return std::string(it->second);
    }
    if (lookupSource_ != nullptr) {
        return lookupSource_->GetTypeIdByOffset(offset);
    }

    return ReportMissingId("type", offset);
}

std::string AbckitIrInterface::GetFieldIdByOffset(uint32_t offset) const
{
    auto it = fields.find(offset);
    if (it != fields.cend()) {
        return std::string(it->second);
    }
    if (lookupSource_ != nullptr) {
        return lookupSource_->GetFieldIdByOffset(offset);
    }

    return ReportMissingId("field", offset);
}

void AbckitIrInterface::SetPcToLineColumnForCurrentMethod(std::unordered_map<size_t, std::pair<size_t, uint32_t>> &&map)
{
    pcToLineColumn_ = std::move(map);
}

size_t AbckitIrInterface::GetLineNumberByPc(size_t pc) const
{
    auto it = pcToLineColumn_.find(pc);
    if (it == pcToLineColumn_.end()) {
        return 0;
    }
    return it->second.first;
}

uint32_t AbckitIrInterface::GetColumnNumberByPc(size_t pc) const
{
    auto it = pcToLineColumn_.find(pc);
    if (it == pcToLineColumn_.end()) {
        return 0;
    }
    return it->second.second;
}

bool AbckitIrInterface::TryGetLineColumnByPc(size_t pc, size_t *line, uint32_t *column) const
{
    auto it = pcToLineColumn_.find(pc);
    if (it == pcToLineColumn_.end()) {
        return false;
    }
    if (line != nullptr) {
        *line = it->second.first;
    }
    if (column != nullptr) {
        *column = it->second.second;
    }
    return true;
}
