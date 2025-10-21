/**
 * Copyright (c) 2025 Huawei Device Co., Ltd.
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

#include "json_util.h"

#include <optional>

#include "assert_util.h"
#include "error.h"

namespace {
template <typename T>
std::optional<T> GetJsonValue(const ark::JsonObject *object, const std::string_view &field)
{
    auto value = object->GetValue<T>(field.data());
    if (!value) {
        return std::nullopt;
    }
    return *value;
}
}  // namespace

const ark::JsonObject *ark::guard::JsonUtil::GetJsonObject(const JsonObject *object, const std::string_view &field)
{
    auto value = object->GetValue<JsonObject::JsonObjPointer>(field.data());
    if (!value) {
        return nullptr;
    }
    return value->get();
}

std::string ark::guard::JsonUtil::GetStringValue(const JsonObject *object, const std::string_view &field,
                                                 bool optionalField)
{
    auto result = GetJsonValue<JsonObject::StringT>(object, field);
    if (result.has_value()) {
        return result.value();
    }
    ARK_GUARD_ASSERT(!optionalField, ErrorCode::CONFIGURATION_FILE_FORMAT_ERROR,
                     "Fail to obtain required field:" + std::string(field));
    return "";
}

bool ark::guard::JsonUtil::GetBoolValue(const JsonObject *object, const std::string_view &field, bool optionalField)
{
    auto result = GetJsonValue<JsonObject::BoolT>(object, field);
    if (result.has_value()) {
        return result.value();
    }
    ARK_GUARD_ASSERT(!optionalField, ErrorCode::CONFIGURATION_FILE_FORMAT_ERROR,
                     "Fail to obtain required field:" + std::string(field));
    return false;
}

std::vector<std::string> ark::guard::JsonUtil::GetArrayStringValue(const JsonObject *object,
                                                                   const std::string_view &field, bool optionalField)
{
    std::vector<std::string> res;
    auto arrValues = object->GetValue<JsonObject::ArrayT>(field.data());
    if (!arrValues) {
        ARK_GUARD_ASSERT(!optionalField, ErrorCode::CONFIGURATION_FILE_FORMAT_ERROR,
                         "Fail to obtain required field:" + std::string(field));
        return res;
    }
    res.reserve(arrValues->size());
    for (auto &value : *arrValues) {
        res.emplace_back(*(value.Get<JsonObject::StringT>()));
    }
    return res;
}