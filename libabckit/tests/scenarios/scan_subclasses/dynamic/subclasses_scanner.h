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
#ifndef ABCKIT_SUBCLASSES_SCANNER_H
#define ABCKIT_SUBCLASSES_SCANNER_H

#include <string>
#include <utility>
#include <vector>

#include "libabckit/c/isa/isa_dynamic.h"
#include "helpers/visit_helper/visit_helper.h"
#include "helpers/visit_helper/visit_helper-inl.h"
#include "libabckit/c/metadata_core.h"

struct ClassInfo {
    std::string path;
    std::string className;
};

class SubclassesScanner {
public:
    SubclassesScanner(enum AbckitApiVersion version, const AbckitApi *impl, AbckitFile *file,
                      std::vector<ClassInfo> baseClasses)
        : file_(file), baseClasses_ {std::move(baseClasses)}
    {
        impl_ = impl;
        implI_ = AbckitGetInspectApiImpl(version);
        implG_ = AbckitGetGraphApiImpl(version);
        dynG_ = AbckitGetIsaApiDynamicImpl(version);
        visitor_ = VisitHelper(file_, impl, implI_, implG_, dynG_);
    }

    const std::vector<ClassInfo> &GetSubclasses()
    {
        return subClasses_;
    }

    void Run();

    void RunForModules(const std::vector<AbckitCoreModule *> &modules);

    AbckitStatus GetStatus() const
    {
        return status_;
    }

    bool IsEqualsSubClasses(const std::vector<ClassInfo> &otherSubClasses);

private:
    void ScanModule(AbckitCoreModule *module);

    void Reset(bool clearSubclasses);

    void SetFirstError(AbckitStatus status);

    bool RecordLastError();

    std::string GetString(AbckitString *value);

    void CollectSubClasses(AbckitCoreFunction *method,
                           const std::vector<std::pair<AbckitCoreImportDescriptor *, size_t>> &impDescrs);

    bool IsLoadApi(AbckitCoreImportDescriptor *id, AbckitInst *inst, ClassInfo &subclassInfo);

    void GetImportDescriptors(AbckitCoreModule *mod,
                              std::vector<std::pair<AbckitCoreImportDescriptor *, size_t>> &importDescriptors);

private:
    const AbckitApi *impl_ = nullptr;
    const AbckitInspectApi *implI_ = nullptr;
    const AbckitGraphApi *implG_ = nullptr;
    const AbckitIsaApiDynamic *dynG_ = nullptr;
    AbckitFile *file_ = nullptr;
    VisitHelper visitor_;
    std::vector<ClassInfo> subClasses_;
    std::vector<ClassInfo> baseClasses_;
    AbckitStatus status_ = ABCKIT_STATUS_NO_ERROR;
};

#endif  // ABCKIT_SUBCLASSES_SCANNER_H
