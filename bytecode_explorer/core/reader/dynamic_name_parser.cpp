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

#include "core/reader/dynamic_name_parser.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <optional>
#include <string_view>

namespace panda::bytecode_explorer {
namespace {

struct Separator {
    std::string_view text;
    EntityKind kind;
    EntityRole role;
};

constexpr Separator SEPARATORS[] = {
    {"=#", EntityKind::METHOD, EntityRole::CONSTRUCTOR},
    {">#", EntityKind::METHOD, EntityRole::INSTANCE_METHOD},
    {"<#", EntityKind::METHOD, EntityRole::STATIC_METHOD},
    {"*#", EntityKind::FUNCTION, EntityRole::NORMAL},
    {"%#", EntityKind::ENUM, EntityRole::ENUM_INITIALIZER},
    {"&#", EntityKind::NAMESPACE, EntityRole::NAMESPACE_INITIALIZER},
};

EntityKind ScopeKind(char marker)
{
    switch (marker) {
        case '&':
            return EntityKind::NAMESPACE;
        case '~':
            return EntityKind::CLASS;
        case '%':
            return EntityKind::ENUM;
        case '*':
            return EntityKind::FUNCTION;
        case '>':
        case '<':
        case '=':
            return EntityKind::METHOD;
        default:
            return EntityKind::ABC_ITEM;
    }
}

bool IsScopeMarker(char marker)
{
    return marker == '&' || marker == '~' || marker == '%' || marker == '*' || marker == '>' || marker == '<' ||
           marker == '=';
}

std::string DisplayName(std::string_view name)
{
    const auto duplicateSuffix = name.rfind('^');
    if (duplicateSuffix != std::string_view::npos && duplicateSuffix + 1U < name.size()) {
        bool numeric = true;
        for (size_t index = duplicateSuffix + 1U; index < name.size(); ++index) {
            numeric = numeric && std::isxdigit(static_cast<unsigned char>(name[index])) != 0;
        }
        if (numeric) {
            name = name.substr(0, duplicateSuffix);
        }
    }
    return std::string(name);
}

std::string CallableDisplayName(std::string_view name)
{
    auto display = DisplayName(name);
    if (!display.empty() || (!name.empty() && name.front() != '^')) {
        return display;
    }
    return name.empty() ? "<anonymous>" : "<anonymous " + std::string(name) + ">";
}

struct ResolvedScopeName {
    std::string display;
    std::string key;
};

std::string_view DuplicateSuffix(std::string_view name)
{
    const auto suffix = name.rfind('^');
    if (suffix == std::string_view::npos || suffix + 1U == name.size()) {
        return {};
    }
    const bool numeric =
        std::all_of(name.begin() + static_cast<std::ptrdiff_t>(suffix + 1U), name.end(),
                    [](char character) { return std::isxdigit(static_cast<unsigned char>(character)) != 0; });
    return numeric ? name.substr(suffix) : std::string_view {};
}

std::optional<ResolvedScopeName> ResolveScopeName(std::string_view mangledName, EntityKind kind,
                                                  const std::vector<std::string> &scopeNames, ParsedDynamicName *result)
{
    if (mangledName.empty()) {
        return std::nullopt;
    }
    if (mangledName.front() != '@') {
        std::string display;
        if (kind == EntityKind::CLASS) {
            display = DisplayName(mangledName);
        } else if (kind == EntityKind::FUNCTION || kind == EntityKind::METHOD) {
            display = CallableDisplayName(mangledName);
        } else {
            display = std::string(mangledName);
        }
        const auto suffix = DuplicateSuffix(mangledName);
        return ResolvedScopeName {display, suffix.empty() ? display : display + '\x1e' + std::string(suffix)};
    }

    const auto suffix = DuplicateSuffix(mangledName);
    if (!suffix.empty()) {
        mangledName.remove_suffix(suffix.size());
    }
    const auto indexText = std::string(mangledName.substr(1U));
    if (indexText.empty() || !std::all_of(indexText.begin(), indexText.end(), [](char character) {
            return std::isxdigit(static_cast<unsigned char>(character)) != 0;
        })) {
        return std::nullopt;
    }
    char *end = nullptr;
    const auto index = std::strtoul(indexText.c_str(), &end, 16);
    if (end != indexText.c_str() + indexText.size()) {
        return std::nullopt;
    }
    if (index < scopeNames.size()) {
        const auto &display = scopeNames[index];
        return ResolvedScopeName {display, suffix.empty() ? display : display + '\x1e' + std::string(suffix)};
    }
    result->reliable = false;
    result->diagnostic = "Dynamic scope index is outside the scopeNames table";
    const auto display = "<scope @" + indexText + ">";
    return ResolvedScopeName {display, suffix.empty() ? display : display + '\x1e' + std::string(suffix)};
}

bool ParseScopes(std::string_view context, const std::vector<std::string> &scopeNames, ParsedDynamicName *result)
{
    size_t cursor = 0;
    while (cursor < context.size()) {
        const char marker = context[cursor++];
        const size_t nameBegin = cursor;
        while (cursor < context.size() && !IsScopeMarker(context[cursor])) {
            ++cursor;
        }
        const auto kind = ScopeKind(marker);
        const auto name = ResolveScopeName(context.substr(nameBegin, cursor - nameBegin), kind, scopeNames, result);
        if (!name.has_value()) {
            return false;
        }
        result->parents.push_back(name->display);
        result->parentKeys.push_back(name->key);
        result->parentKinds.push_back(kind);
        if (kind == EntityKind::ABC_ITEM) {
            result->reliable = false;
            result->diagnostic = "Unknown dynamic scope marker retained without semantic classification";
        }
    }
    return true;
}

}  // namespace

std::optional<ParsedDynamicName> DynamicNameParser::Parse(const std::string &moduleName,
                                                          const std::string &internalName,
                                                          const std::vector<std::string> &scopeNames)
{
    const std::string prefix = moduleName + ".";
    std::string_view body(internalName);
    if (internalName.compare(0, prefix.size(), prefix) == 0) {
        body.remove_prefix(prefix.size());
    } else if (body.empty() || (body.front() != '#' && body != "func_main_0")) {
        return std::nullopt;
    }
    if (body == "func_main_0") {
        ParsedDynamicName result;
        result.kind = EntityKind::FUNCTION;
        result.role = EntityRole::MODULE_INITIALIZER;
        result.name = "<module initializer>";
        result.internalName = internalName;
        return result;
    }
    if (body.empty() || body.front() != '#') {
        return std::nullopt;
    }

    for (const auto &separator : SEPARATORS) {
        const auto position = body.rfind(separator.text);
        if (position == std::string_view::npos) {
            continue;
        }

        ParsedDynamicName result;
        result.kind = separator.kind;
        result.role = separator.role;
        const auto terminalName = body.substr(position + separator.text.size());
        const bool callable = separator.role != EntityRole::CONSTRUCTOR &&
                              (separator.kind == EntityKind::FUNCTION || separator.kind == EntityKind::METHOD);
        result.name = callable ? CallableDisplayName(terminalName) : DisplayName(terminalName);
        result.internalName = internalName;
        const auto context = body.substr(1U, position - 1U);
        if (result.name.empty() || !ParseScopes(context, scopeNames, &result)) {
            return std::nullopt;
        }
        return result;
    }
    return std::nullopt;
}

}  // namespace panda::bytecode_explorer
