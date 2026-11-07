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

#include <algorithm>
#include <sstream>
#include "libabckit/c/abckit.h"
#include "api_scanner.h"

ApiScanner::ApiScanner(enum AbckitApiVersion version, AbckitFile *file, std::vector<ApiInfo> apiList)
    : file_(file), apiList_(std::move(apiList))
{
    impl_ = AbckitGetApiImpl(version);
    implI_ = AbckitGetInspectApiImpl(version);
    implG_ = AbckitGetGraphApiImpl(version);
    dynG_ = AbckitGetIsaApiDynamicImpl(version);
    vh_ = VisitHelper(file_, impl_, implI_, implG_, dynG_);
}

void ApiScanner::CollectApiUsages()
{
    // Preserve the original helper behavior: repeated full-file scans append
    // usages instead of clearing results collected by earlier calls.
    Reset(false);
    vh_.EnumerateModules([&](AbckitCoreModule *mod) {
        if (status_ == ABCKIT_STATUS_NO_ERROR) {
            CollectApiUsagesInModule(mod);
        }
    });
    RecordLastError();
}

const ApiUsageMap &ApiScanner::GetApiUsages()
{
    CollectApiUsages();
    return apiUsages_;
}

const ApiUsageMap &ApiScanner::GetApiUsagesForModules(const std::vector<AbckitCoreModule *> &modules)
{
    Reset(true);
    for (auto *module : modules) {
        if (module == nullptr) {
            SetFirstError(ABCKIT_STATUS_BAD_ARGUMENT);
            break;
        }
        CollectApiUsagesInModule(module);
        if (status_ != ABCKIT_STATUS_NO_ERROR) {
            break;
        }
    }
    return apiUsages_;
}

void ApiScanner::CollectApiUsagesInModule(AbckitCoreModule *module)
{
    SuspectsMap suspects;
    GetSuspects(module, suspects);
    if (status_ != ABCKIT_STATUS_NO_ERROR || suspects.empty()) {
        return;
    }
    vh_.EnumerateModuleTopLevelFunctions(module, [&](AbckitCoreFunction *method) {
        if (status_ == ABCKIT_STATUS_NO_ERROR) {
            CollectUsageInMethod(method, suspects);
        }
    });
    RecordLastError();
    if (status_ != ABCKIT_STATUS_NO_ERROR) {
        return;
    }
    vh_.EnumerateModuleClasses(module, [&](AbckitCoreClass *klass) {
        if (status_ != ABCKIT_STATUS_NO_ERROR) {
            return;
        }
        vh_.EnumerateClassMethods(klass, [&](AbckitCoreFunction *method) {
            if (status_ == ABCKIT_STATUS_NO_ERROR) {
                CollectUsageInMethod(method, suspects);
            }
        });
        RecordLastError();
    });
    RecordLastError();
}

void ApiScanner::Reset(bool clearUsages)
{
    status_ = ABCKIT_STATUS_NO_ERROR;
    if (clearUsages) {
        apiUsages_.clear();
    }
}

void ApiScanner::SetFirstError(AbckitStatus status)
{
    if (status_ == ABCKIT_STATUS_NO_ERROR && status != ABCKIT_STATUS_NO_ERROR) {
        status_ = status;
    }
}

bool ApiScanner::RecordLastError()
{
    SetFirstError(impl_->getLastError());
    return status_ == ABCKIT_STATUS_NO_ERROR;
}

std::string ApiScanner::GetString(AbckitString *str)
{
    if (str == nullptr) {
        RecordLastError();
        SetFirstError(ABCKIT_STATUS_INTERNAL_ERROR);
        return {};
    }
    const char *value = implI_->abckitStringToString(str);
    if (!RecordLastError() || value == nullptr) {
        SetFirstError(ABCKIT_STATUS_INTERNAL_ERROR);
        return {};
    }
    return value;
}

bool ApiScanner::GetSuspects(AbckitCoreModule *mod, SuspectsMap &suspects)
{
    vh_.EnumerateModuleImports(mod, [&](AbckitCoreImportDescriptor *id) {
        auto importName = GetString(implI_->importDescriptorGetName(id));
        if (!RecordLastError()) {
            return;
        }
        auto *importedModule = implI_->importDescriptorGetImportedModule(id);
        if (!RecordLastError() || importedModule == nullptr) {
            SetFirstError(ABCKIT_STATUS_INTERNAL_ERROR);
            return;
        }
        auto source = GetString(implI_->moduleGetName(importedModule));
        if (!RecordLastError()) {
            return;
        }
        for (size_t i = 0; i < apiList_.size(); ++i) {
            const auto &api = apiList_[i];
            if (source != api.source) {
                continue;
            }
            if (importName == api.objName) {
                suspects.emplace(id, i);
            }
        }
    });
    RecordLastError();
    return !suspects.empty();
}

bool ApiScanner::IsLoadApi(AbckitCoreImportDescriptor *id, size_t apiIndex, AbckitInst *inst)
{
    // find the following pattern:
    // 1. ldExternalModuleVar (import {ApiNamespace} from "./modules/myApi")
    // 2. ldObjByName v1, "foo" (api.propName)
    // or
    // 1. ldExternalModuleVar (import {bar} from "./modules/myApi")

    auto opcode = dynG_->iGetOpcode(inst);
    if (!RecordLastError() || opcode != ABCKIT_ISA_API_DYNAMIC_OPCODE_LDEXTERNALMODULEVAR) {
        return false;
    }

    auto *descriptor = dynG_->iGetImportDescriptor(inst);
    if (!RecordLastError() || descriptor != id) {
        return false;
    }

    const auto &prop = apiList_[apiIndex].propName;
    if (prop.empty()) {
        return true;
    }

    bool found = false;
    vh_.EnumerateInstUsers(inst, [&](AbckitInst *user) {
        if (found || status_ != ABCKIT_STATUS_NO_ERROR) {
            return;
        }
        auto userOpcode = dynG_->iGetOpcode(user);
        if (!RecordLastError() || userOpcode != ABCKIT_ISA_API_DYNAMIC_OPCODE_LDOBJBYNAME) {
            return;
        }
        auto property = GetString(implG_->iGetString(user));
        RecordLastError();
        found = property == prop;
    });
    RecordLastError();

    return found;
}

std::string ApiScanner::GetMethodName(AbckitCoreFunction *method)
{
    auto methodName = GetString(implI_->functionGetName(method));
    if (!RecordLastError()) {
        return {};
    }
    auto *cls = implI_->functionGetParentClass(method);
    if (!RecordLastError()) {
        return {};
    }
    if (cls == nullptr) {
        return methodName;
    }
    auto className = GetString(implI_->classGetName(cls));
    RecordLastError();
    return className + '.' + methodName;
}

void ApiScanner::CollectUsageInMethod(AbckitCoreFunction *method, SuspectsMap &suspects)
{
    auto methodName = GetMethodName(method);
    if (status_ != ABCKIT_STATUS_NO_ERROR) {
        return;
    }
    auto *module = implI_->functionGetModule(method);
    if (!RecordLastError() || module == nullptr) {
        SetFirstError(ABCKIT_STATUS_INTERNAL_ERROR);
        return;
    }
    auto source = GetString(implI_->moduleGetName(module));
    if (!RecordLastError()) {
        return;
    }
    UsageInfo usage = {source.data(), methodName, 0};
    auto *graph = implI_->createGraphFromFunction(method);
    if (!RecordLastError() || graph == nullptr) {
        SetFirstError(ABCKIT_STATUS_INTERNAL_ERROR);
        return;
    }

    struct GraphVisitContext {
        ApiScanner *scanner;
        UsageInfo *usage;
        SuspectsMap *suspects;
    } context {this, &usage, &suspects};

    bool visitResult = implG_->gVisitBlocksRpo(graph, &context, [](AbckitBasicBlock *basicBlock, void *data) {
        auto &ctx = *static_cast<GraphVisitContext *>(data);
        auto *inst = ctx.scanner->implG_->bbGetFirstInst(basicBlock);
        if (!ctx.scanner->RecordLastError()) {
            return false;
        }
        while (inst != nullptr) {
            for (const auto &[id, apiIndex] : *ctx.suspects) {
                if (ctx.scanner->IsLoadApi(id, apiIndex, inst)) {
                    ctx.usage->idx = apiIndex;
                    ctx.scanner->AddApiUsage(apiIndex, *ctx.usage);
                }
                if (ctx.scanner->status_ != ABCKIT_STATUS_NO_ERROR) {
                    return false;
                }
            }
            inst = ctx.scanner->implG_->iGetNext(inst);
            if (!ctx.scanner->RecordLastError()) {
                return false;
            }
        }
        return true;
    });
    bool visitStatusOk = RecordLastError();
    if (!visitResult && visitStatusOk) {
        SetFirstError(ABCKIT_STATUS_INTERNAL_ERROR);
    }

    // Keep the first traversal error, but always release the Graph in the
    // worker thread that created it.
    impl_->destroyGraph(graph);
    RecordLastError();
}

static std::string GetQuatatedStr(const std::string &str)
{
    const static std::string QUOTATION = "\"";
    return QUOTATION + str + QUOTATION;
}

std::string ApiScanner::ApiUsageMapGetString() const
{
    std::stringstream ss;
    const static std::string INDENT_1 = "  ";
    const static std::string INDENT_2 = INDENT_1 + INDENT_1;
    const static std::string INDENT_3 = INDENT_1 + INDENT_2;
    const static std::string INDENT_4 = INDENT_1 + INDENT_3;
    const static std::string LEFT_BRACKET = "{";
    const static std::string RIGHT_BRACKET = "}";
    const static std::string COLON = ": ";
    const static std::string COMMA = ",";
    const static std::string APIINFO = "APIInfo";
    const static std::string APIINFO_SOURCE = "source";
    const static std::string APIINFO_OBJNAME = "objName";
    const static std::string APIINFO_PROPNAME = "propName";
    const static std::string USAGES = "Usages";
    const static std::string USAGES_SOURCE = "source";
    const static std::string USAGES_FUNCNAME = "funcName";
    const static std::string API_IDX = "indexOfAPI";

    LIBABCKIT_LOG(DEBUG) << LEFT_BRACKET << std::endl;
    for (const auto &[index, usageInfos] : apiUsages_) {
        LIBABCKIT_LOG(DEBUG) << INDENT_1 << LEFT_BRACKET << std::endl;

        if (usageInfos.empty()) {
            continue;
        }
        const auto &apiInfo = apiList_[index];
        LIBABCKIT_LOG(DEBUG) << INDENT_2 << APIINFO << COLON << LEFT_BRACKET << std::endl;
        LIBABCKIT_LOG(DEBUG) << INDENT_3 << APIINFO_SOURCE << COLON << GetQuatatedStr(apiInfo.source) << COMMA
                             << std::endl;
        LIBABCKIT_LOG(DEBUG) << INDENT_3 << APIINFO_OBJNAME << COLON << GetQuatatedStr(apiInfo.objName) << COMMA
                             << std::endl;
        LIBABCKIT_LOG(DEBUG) << INDENT_3 << APIINFO_PROPNAME << COLON << GetQuatatedStr(apiInfo.propName) << COMMA
                             << std::endl;
        LIBABCKIT_LOG(DEBUG) << INDENT_2 << RIGHT_BRACKET << COMMA << std::endl;

        LIBABCKIT_LOG(DEBUG) << INDENT_2 << USAGES << COLON << LEFT_BRACKET << std::endl;
        for (const auto &usage : usageInfos) {
            LIBABCKIT_LOG(DEBUG) << INDENT_3 << LEFT_BRACKET << std::endl;
            LIBABCKIT_LOG(DEBUG) << INDENT_4 << USAGES_SOURCE << COLON << GetQuatatedStr(usage.source) << COMMA
                                 << std::endl;
            LIBABCKIT_LOG(DEBUG) << INDENT_4 << USAGES_FUNCNAME << COLON << GetQuatatedStr(usage.funcName) << COMMA
                                 << std::endl;
            LIBABCKIT_LOG(DEBUG) << INDENT_4 << API_IDX << COLON << usage.idx << COMMA << std::endl;
            LIBABCKIT_LOG(DEBUG) << INDENT_3 << RIGHT_BRACKET << COMMA << std::endl;
        }
        LIBABCKIT_LOG(DEBUG) << INDENT_2 << RIGHT_BRACKET << std::endl;

        LIBABCKIT_LOG(DEBUG) << INDENT_1 << RIGHT_BRACKET << COMMA << std::endl;
    }
    LIBABCKIT_LOG(DEBUG) << RIGHT_BRACKET << std::endl;

    return ss.str();
}

bool ApiScanner::IsUsagesEqual(const std::vector<UsageInfo> &otherUsages) const
{
    for (auto &otherUsage : otherUsages) {
        auto usages = apiUsages_.at(otherUsage.idx);
        auto iter = std::find_if(usages.begin(), usages.end(), [&otherUsage](const UsageInfo &usage) {
            return (otherUsage.funcName == usage.funcName) && (otherUsage.source == usage.source) &&
                   (otherUsage.idx == usage.idx);
        });
        if (iter == usages.end()) {
            return false;
        }
    }
    return true;
}
