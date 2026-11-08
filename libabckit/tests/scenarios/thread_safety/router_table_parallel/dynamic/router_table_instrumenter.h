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

#ifndef LIBABCKIT_TESTS_SCENARIOS_THREAD_SAFETY_ROUTER_TABLE_INSTRUMENTER_H
#define LIBABCKIT_TESTS_SCENARIOS_THREAD_SAFETY_ROUTER_TABLE_INSTRUMENTER_H

#include <cstddef>
#include <string>
#include <vector>

#include "libabckit/c/abckit.h"
#include "libabckit/c/extensions/arkts/metadata_arkts.h"
#include "libabckit/c/ir_core.h"
#include "libabckit/c/isa/isa_dynamic.h"
#include "libabckit/c/metadata_core.h"

namespace libabckit::test {

struct RouterEntryTask {
    AbckitCoreModule *handlerModule = nullptr;
    AbckitCoreClass *handlerClass = nullptr;
    AbckitCoreAnnotation *annotation = nullptr;
    AbckitString *className = nullptr;
    std::string classNameText;
    std::string fullPath;
    AbckitCoreImportDescriptor *import = nullptr;
};

struct RouterTableTask {
    AbckitCoreModule *routerModule = nullptr;
    AbckitCoreFunction *initializer = nullptr;
    std::string domain;
    std::string moduleName;
    std::vector<RouterEntryTask> entries;
};

struct RouterTableTransformResult {
    AbckitStatus status = ABCKIT_STATUS_NO_ERROR;
    size_t importsAdded = 0;
    size_t annotationsRemoved = 0;
    size_t tablesProcessed = 0;
    size_t routesInserted = 0;
    size_t tablesCommitted = 0;
};

class RouterTableInstrumenter final {
public:
    explicit RouterTableInstrumenter(AbckitApiVersion version);

    RouterTableTransformResult PrepareMetadata(RouterTableTask &task) const;
    RouterTableTransformResult BuildTable(const RouterTableTask &task) const;

private:
    const AbckitApi *impl_ = nullptr;
    const AbckitInspectApi *implI_ = nullptr;
    const AbckitModifyApi *implM_ = nullptr;
    const AbckitGraphApi *implG_ = nullptr;
    const AbckitIsaApiDynamic *dynG_ = nullptr;
    const AbckitArktsInspectApi *arkI_ = nullptr;
    const AbckitArktsModifyApi *arkM_ = nullptr;
};

}  // namespace libabckit::test

#endif  // LIBABCKIT_TESTS_SCENARIOS_THREAD_SAFETY_ROUTER_TABLE_INSTRUMENTER_H
