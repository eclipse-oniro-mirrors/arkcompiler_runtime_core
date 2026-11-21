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

#include "core/reader/module_binding_analyzer.h"

namespace panda::bytecode_explorer {
namespace {

void AppendDetail(std::string *details, const std::string &label, const std::string &value)
{
    if (value.empty()) {
        return;
    }
    if (!details->empty()) {
        *details += ", ";
    }
    *details += label + '=' + value;
}

std::string QuoteModuleRequest(const std::string &request)
{
    std::string quoted {'"'};
    for (const char character : request) {
        if (character == '\\' || character == '"') {
            quoted.push_back('\\');
        }
        quoted.push_back(character);
    }
    quoted.push_back('"');
    return quoted;
}

std::string Alias(const std::string &source, const std::string &target)
{
    if (source.empty()) {
        return target;
    }
    return target.empty() || source == target ? source : source + " as " + target;
}

std::string Braced(const std::string &binding)
{
    return binding.empty() ? binding : "{ " + binding + " }";
}

std::string FromRequest(std::string label, const ModuleBinding &binding)
{
    if (!label.empty() && binding.moduleRequestValid && !binding.moduleRequest.empty()) {
        label += " from " + QuoteModuleRequest(binding.moduleRequest);
    }
    return label;
}

// Picks the primary name of the binding: the local, import, or export name depending on the kind,
// falling back to the star form.
std::string ResolveBindingName(const ModuleBinding &binding)
{
    const bool isImport =
        binding.kind == ModuleBindingKind::REGULAR_IMPORT || binding.kind == ModuleBindingKind::NAMESPACE_IMPORT;
    if (isImport && !binding.localName.empty()) {
        return binding.localName;
    }
    if (isImport && !binding.importName.empty()) {
        return binding.importName;
    }
    if (!isImport && !binding.exportName.empty()) {
        return binding.exportName;
    }
    if (!isImport && !binding.localName.empty()) {
        return binding.localName;
    }
    return "*";
}

// Formats the display name for the binding kind, using the resolved name as the local-export
// fallback.
std::string ResolveBindingDisplayName(const ModuleBinding &binding, const std::string &name)
{
    switch (binding.kind) {
        case ModuleBindingKind::REGULAR_IMPORT:
            return FromRequest(Braced(Alias(binding.importName, binding.localName)), binding);
        case ModuleBindingKind::NAMESPACE_IMPORT:
            return FromRequest(binding.localName.empty() ? "*" : "* as " + binding.localName, binding);
        case ModuleBindingKind::LOCAL_EXPORT:
            return binding.localName.empty() || binding.exportName.empty() || binding.localName == binding.exportName
                       ? name
                       : Braced(Alias(binding.localName, binding.exportName));
        case ModuleBindingKind::INDIRECT_EXPORT:
            return FromRequest(Braced(Alias(binding.importName, binding.exportName)), binding);
        case ModuleBindingKind::STAR_EXPORT:
            return FromRequest("*", binding);
    }
    return std::string();
}

}  // namespace

ModuleBindingPresentation ModuleBindingAnalyzer::Analyze(const ModuleBinding &binding)
{
    ModuleBindingPresentation result;
    const bool isImport =
        binding.kind == ModuleBindingKind::REGULAR_IMPORT || binding.kind == ModuleBindingKind::NAMESPACE_IMPORT;
    result.kind = isImport ? EntityKind::IMPORT : EntityKind::EXPORT;
    result.group = isImport ? EntityGroup::IMPORTS : EntityGroup::EXPORTS;
    result.name = ResolveBindingName(binding);
    result.displayName = ResolveBindingDisplayName(binding, result.name);
    if (result.displayName.empty()) {
        result.displayName = result.name;
    }
    AppendDetail(&result.details, "local", binding.localName);
    AppendDetail(&result.details, "import", binding.importName);
    AppendDetail(&result.details, "export", binding.exportName);
    AppendDetail(&result.details, "request", binding.moduleRequest);
    if (!binding.moduleRequestValid) {
        result.diagnostic =
            "Module request index " + std::to_string(binding.moduleRequestIndex) + " is outside the request table";
    }
    return result;
}

}  // namespace panda::bytecode_explorer
