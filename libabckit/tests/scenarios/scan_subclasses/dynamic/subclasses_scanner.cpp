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

#include "subclasses_scanner.h"
#include <string>

void SubclassesScanner::Run()
{
    // Preserve the original helper behavior: repeated full-file runs append
    // subclasses instead of clearing earlier results.
    Reset(false);
    visitor_.EnumerateModules([&](AbckitCoreModule *mod) {
        if (status_ == ABCKIT_STATUS_NO_ERROR) {
            ScanModule(mod);
        }
    });
    RecordLastError();
}

void SubclassesScanner::RunForModules(const std::vector<AbckitCoreModule *> &modules)
{
    Reset(true);
    for (auto *module : modules) {
        if (module == nullptr) {
            SetFirstError(ABCKIT_STATUS_BAD_ARGUMENT);
            break;
        }
        ScanModule(module);
        if (status_ != ABCKIT_STATUS_NO_ERROR) {
            break;
        }
    }
}

void SubclassesScanner::ScanModule(AbckitCoreModule *module)
{
    std::vector<std::pair<AbckitCoreImportDescriptor *, size_t>> impDescriptors;
    GetImportDescriptors(module, impDescriptors);
    if (status_ != ABCKIT_STATUS_NO_ERROR || impDescriptors.empty()) {
        return;
    }

    visitor_.EnumerateModuleTopLevelFunctions(module, [&](AbckitCoreFunction *method) {
        if (status_ == ABCKIT_STATUS_NO_ERROR) {
            CollectSubClasses(method, impDescriptors);
        }
    });
    RecordLastError();
    if (status_ != ABCKIT_STATUS_NO_ERROR) {
        return;
    }

    visitor_.EnumerateModuleClasses(module, [&](AbckitCoreClass *klass) {
        if (status_ != ABCKIT_STATUS_NO_ERROR) {
            return;
        }
        visitor_.EnumerateClassMethods(klass, [&](AbckitCoreFunction *method) {
            if (status_ == ABCKIT_STATUS_NO_ERROR) {
                CollectSubClasses(method, impDescriptors);
            }
        });
        RecordLastError();
    });
    RecordLastError();
}

void SubclassesScanner::Reset(bool clearSubclasses)
{
    status_ = ABCKIT_STATUS_NO_ERROR;
    if (clearSubclasses) {
        subClasses_.clear();
    }
}

void SubclassesScanner::SetFirstError(AbckitStatus status)
{
    if (status_ == ABCKIT_STATUS_NO_ERROR && status != ABCKIT_STATUS_NO_ERROR) {
        status_ = status;
    }
}

bool SubclassesScanner::RecordLastError()
{
    SetFirstError(impl_->getLastError());
    return status_ == ABCKIT_STATUS_NO_ERROR;
}

std::string SubclassesScanner::GetString(AbckitString *value)
{
    if (value == nullptr) {
        RecordLastError();
        SetFirstError(ABCKIT_STATUS_INTERNAL_ERROR);
        return {};
    }
    const char *data = implI_->abckitStringToString(value);
    if (!RecordLastError() || data == nullptr) {
        SetFirstError(ABCKIT_STATUS_INTERNAL_ERROR);
        return {};
    }
    return data;
}

void SubclassesScanner::CollectSubClasses(AbckitCoreFunction *method,
                                          const std::vector<std::pair<AbckitCoreImportDescriptor *, size_t>> &impDescrs)
{
    auto *graph = implI_->createGraphFromFunction(method);
    if (!RecordLastError() || graph == nullptr) {
        SetFirstError(ABCKIT_STATUS_INTERNAL_ERROR);
        return;
    }

    struct GraphVisitContext {
        SubclassesScanner *scanner;
        const std::vector<std::pair<AbckitCoreImportDescriptor *, size_t>> *importDescriptors;
    } context {this, &impDescrs};

    bool visitResult = implG_->gVisitBlocksRpo(graph, &context, [](AbckitBasicBlock *basicBlock, void *data) {
        auto &ctx = *static_cast<GraphVisitContext *>(data);
        auto *inst = ctx.scanner->implG_->bbGetFirstInst(basicBlock);
        if (!ctx.scanner->RecordLastError()) {
            return false;
        }
        while (inst != nullptr) {
            for (const auto &[importDescriptor, index] : *ctx.importDescriptors) {
                static_cast<void>(index);
                ClassInfo classInfo;
                if (ctx.scanner->IsLoadApi(importDescriptor, inst, classInfo)) {
                    ctx.scanner->subClasses_.emplace_back(std::move(classInfo));
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

    impl_->destroyGraph(graph);
    RecordLastError();
}

bool SubclassesScanner::IsLoadApi(AbckitCoreImportDescriptor *id, AbckitInst *inst, ClassInfo &subclassInfo)
{
    auto opcode = dynG_->iGetOpcode(inst);
    if (!RecordLastError() || opcode != ABCKIT_ISA_API_DYNAMIC_OPCODE_LDEXTERNALMODULEVAR) {
        return false;
    }

    auto *descriptor = dynG_->iGetImportDescriptor(inst);
    if (!RecordLastError() || descriptor != id) {
        return false;
    }

    bool found = false;
    visitor_.EnumerateInstUsers(inst, [&](AbckitInst *user) {
        if (found || status_ != ABCKIT_STATUS_NO_ERROR) {
            return;
        }
        auto userOpcode = dynG_->iGetOpcode(user);
        if (!RecordLastError() || userOpcode != ABCKIT_ISA_API_DYNAMIC_OPCODE_DEFINECLASSWITHBUFFER) {
            return;
        }
        auto *method = implG_->iGetFunction(user);
        if (!RecordLastError() || method == nullptr) {
            SetFirstError(ABCKIT_STATUS_INTERNAL_ERROR);
            return;
        }
        auto *klass = implI_->functionGetParentClass(method);
        if (!RecordLastError() || klass == nullptr) {
            SetFirstError(ABCKIT_STATUS_INTERNAL_ERROR);
            return;
        }
        auto *module = implI_->classGetModule(klass);
        if (!RecordLastError() || module == nullptr) {
            SetFirstError(ABCKIT_STATUS_INTERNAL_ERROR);
            return;
        }
        subclassInfo.className = GetString(implI_->classGetName(klass));
        subclassInfo.path = GetString(implI_->moduleGetName(module));
        found = status_ == ABCKIT_STATUS_NO_ERROR;
    });
    RecordLastError();

    return found;
}

// CC-OFFNXT(G.INC.12-CPP) false positive. this function can not be made static.
void SubclassesScanner::GetImportDescriptors(
    AbckitCoreModule *mod, std::vector<std::pair<AbckitCoreImportDescriptor *, size_t>> &importDescriptors)
{
    visitor_.EnumerateModuleImports(mod, [&](AbckitCoreImportDescriptor *id) {
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
        for (size_t i = 0; i < baseClasses_.size(); ++i) {
            const auto &baseClass = baseClasses_[i];
            if (source != baseClass.path) {
                continue;
            }
            if (importName == baseClass.className) {
                importDescriptors.emplace_back(id, i);
            }
        }
    });
    RecordLastError();
}

bool SubclassesScanner::IsEqualsSubClasses(const std::vector<ClassInfo> &otherSubClasses)
{
    for (auto &otherSubClass : otherSubClasses) {
        auto iter = std::find_if(subClasses_.begin(), subClasses_.end(), [&otherSubClass](const ClassInfo &classInfo) {
            return (otherSubClass.className == classInfo.className) && (otherSubClass.path == classInfo.path);
        });
        if (iter == subClasses_.end()) {
            return false;
        }
    }
    return true;
}
